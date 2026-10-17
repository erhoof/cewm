/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstdio>
#include <cstring>
#include <deque>
#include <optional>

#include <mcl/bit_cast.hpp>

#include "dynarmic/backend/arm64/a64_address_space.h"
#include "dynarmic/backend/arm64/a64_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/devirtualize.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/stack_layout.h"
#include "dynarmic/common/cast_util.h"
#include "dynarmic/common/fp/fpcr.h"
#include "dynarmic/common/llvm_disassemble.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace Dynarmic::Backend::Arm64 {

AddressSpace::AddressSpace(size_t code_cache_size)
        : code_cache_size(code_cache_size)
        , mem(code_cache_size)
        , code(mem.ptr(), mem.ptr())
        , fastmem_manager(exception_handler) {
    ASSERT_MSG(code_cache_size <= 128 * 1024 * 1024, "code_cache_size > 128 MiB not currently supported");

    exception_handler.Register(mem, code_cache_size);
    exception_handler.SetFastmemCallback([this](u64 host_pc) {
        return FastmemCallback(host_pc);
    });
}

// ---- cewm: async emit ------------------------------------------------------------------------------------------------------------
// The executing thread translates a block to IR (the frontend reads guest memory and must stay on this thread), then hands the IR to a
// worker that runs EmitArm64 into a private buffer (it only uses relative branches inside the block; everything outside the block goes
// through relocations that Link() patches after the copy). Meanwhile a stub block that interprets the instructions is installed. PollAsync()
// swaps the stub for the finished block: unlink stub, copy code into the cache, Link, relink callers.
struct AddressSpace::AsyncDone {
    u64 id = 0;
    IR::LocationDescriptor descriptor{0}, end{0};
    EmittedBlockInfo ebi;
    std::vector<u32> code;
};

class AsyncEmitter {
public:
    struct Request {
        AddressSpace* owner;
        u64 id;
        IR::LocationDescriptor descriptor, end;
        IR::Block block;
        EmitConfig conf;
    };

    static AsyncEmitter& Get() {
        static AsyncEmitter* instance = new AsyncEmitter;  // process lifetime: the worker thread is never joined
        return *instance;
    }

    void Submit(Request&& r) {
        {
            std::lock_guard lock{m};
            queue.push_back(std::move(r));
        }
        cv.notify_one();
    }

private:
    AsyncEmitter() : fastmem_manager(eh), worker([this] { Loop(); }) {}

    void Loop() {
        std::vector<u32> buffer(1u << 18);  // 1 MiB: far larger than any block
        for (;;) {
            std::optional<Request> popped;
            {
                std::unique_lock lock{m};
                cv.wait(lock, [this] { return !queue.empty(); });
                popped.emplace(std::move(queue.front()));
                queue.pop_front();
            }
            Request& r = *popped;
            oaknut::CodeGenerator code{buffer.data(), buffer.data()};
            AddressSpace::AsyncDone done;
            done.id = r.id;
            done.descriptor = r.descriptor;
            done.end = r.end;
            done.ebi = EmitArm64(code, std::move(r.block), r.conf, fastmem_manager);
            done.code.assign(buffer.data(), buffer.data() + done.ebi.size / sizeof(u32));
            {
                std::lock_guard lock{r.owner->async_mutex};
                r.owner->async_done.push_back(std::move(done));
                r.owner->async_done_count.fetch_add(1);
            }
            r.owner->async_outstanding.fetch_sub(1);
        }
    }

    std::mutex m;
    std::condition_variable cv;
    std::deque<Request> queue;
    ExceptionHandler eh;
    FastmemManager fastmem_manager;
    std::thread worker;
};

IR::Block AddressSpace::MakeInterpretStub(IR::LocationDescriptor descriptor, IR::LocationDescriptor end) const {
    IR::Block stub{descriptor};
    stub.SetEndLocation(end);
    IR::Term::Interpret term{descriptor};
    term.num_instructions = InstructionCount(descriptor, end);
    stub.SetTerminal(term);
    return stub;
}

void AddressSpace::CancelPending(const tsl::robin_set<IR::LocationDescriptor>& descriptors) {
    for (const auto& d : descriptors) {
        pending.erase(d);
    }
}

void AddressSpace::CancelAllPending() {
    pending.clear();
}

void AddressSpace::PollAsync() {
    if (async_done_count.load(std::memory_order_relaxed) == 0) {
        return;
    }
    std::vector<AsyncDone> ready;
    {
        std::lock_guard lock{async_mutex};
        ready.swap(async_done);
        async_done_count.store(0);
    }
    for (AsyncDone& r : ready) {
        const auto it = pending.find(r.descriptor);
        if (it == pending.end() || it->second != r.id) {
            continue;  // invalidated or superseded while it was being emitted
        }
        pending.erase(it);
        const size_t bytes = (r.code.size() * sizeof(u32) + 15) & ~size_t{15};
        if (GetRemainingSize() < 1024 * 1024 + bytes) {
            ClearCache();  // also drops every pending request; they get requested again
            return;
        }

        InvalidateBasicBlocks({r.descriptor});  // unlink and forget the interpreter stub

        UnprotectCodeMemory();
        code.set_offset((code.offset() + 15) & ~std::ptrdiff_t{15});
        const CodePtr entry = code.xptr<CodePtr>();
        std::memcpy(entry, r.code.data(), r.code.size() * sizeof(u32));
        code.set_xptr(reinterpret_cast<u32*>(entry + r.code.size() * sizeof(u32)));
        r.ebi.entry_point = entry;

        ASSERT(block_entries.insert({r.descriptor, entry}).second);
        ASSERT(reverse_block_entries.insert({entry, r.descriptor}).second);
        ASSERT(block_infos.insert({entry, r.ebi}).second);

        Link(r.ebi);
        RelinkForDescriptor(r.descriptor, entry);
        mem.invalidate(reinterpret_cast<u32*>(entry), r.ebi.size);
        ProtectCodeMemory();

        RegisterNewBlockRange(r.descriptor, r.end);
    }
}

AddressSpace::~AddressSpace() {
    while (async_outstanding.load() != 0) {
        std::this_thread::yield();
    }
}

CodePtr AddressSpace::Get(IR::LocationDescriptor descriptor) {
    if (const auto iter = block_entries.find(descriptor); iter != block_entries.end()) {
        return iter->second;
    }
    return nullptr;
}

std::optional<IR::LocationDescriptor> AddressSpace::ReverseGetLocation(CodePtr host_pc) {
    if (auto iter = reverse_block_entries.upper_bound(host_pc); iter != reverse_block_entries.begin()) {
        // upper_bound locates the first value greater than host_pc, so we need to decrement
        --iter;
        return iter->second;
    }
    return std::nullopt;
}

CodePtr AddressSpace::ReverseGetEntryPoint(CodePtr host_pc) {
    if (auto iter = reverse_block_entries.upper_bound(host_pc); iter != reverse_block_entries.begin()) {
        // upper_bound locates the first value greater than host_pc, so we need to decrement
        --iter;
        return iter->first;
    }
    return nullptr;
}

CodePtr AddressSpace::GetOrEmit(IR::LocationDescriptor descriptor) {
    if (CodePtr block_entry = Get(descriptor)) {
        return block_entry;
    }

    IR::Block ir_block = GenerateIR(descriptor);

    // Blocks that already end in Interpret (tier stubs) are tiny: emit them right here.
    const IR::Terminal block_terminal = ir_block.GetTerminal();
    if (async_emit && !boost::get<IR::Term::Interpret>(&block_terminal)) {
        const IR::LocationDescriptor end = ir_block.EndLocation();
        const u64 id = ++next_id;
        pending[descriptor] = id;
        NotePending(descriptor, end);
        async_outstanding.fetch_add(1);
        AsyncEmitter::Get().Submit({this, id, descriptor, end, std::move(ir_block), GetEmitConfig()});
        return Emit(MakeInterpretStub(descriptor, end)).entry_point;
    }

    const EmittedBlockInfo block_info = Emit(std::move(ir_block));
    return block_info.entry_point;
}

void AddressSpace::InvalidateBasicBlocks(const tsl::robin_set<IR::LocationDescriptor>& descriptors) {
    UnprotectCodeMemory();

    for (const auto& descriptor : descriptors) {
        const auto iter = block_entries.find(descriptor);
        if (iter == block_entries.end()) {
            continue;
        }

        // Unlink before removal because InvalidateBasicBlocks can be called within a fastmem callback,
        // and the currently executing block may have references to itself which need to be unlinked.
        RelinkForDescriptor(descriptor, nullptr);

        block_entries.erase(iter);
    }

    ProtectCodeMemory();
}

void AddressSpace::ClearCache() {
    CancelAllPending();
    block_entries.clear();
    reverse_block_entries.clear();
    block_infos.clear();
    block_references.clear();
    code.set_offset(prelude_info.end_of_prelude);
}

void AddressSpace::DumpDisassembly() const {
    for (u32* ptr = mem.ptr(); ptr < code.xptr<u32*>(); ptr++) {
        std::printf("%s", Common::DisassembleAArch64(*ptr, mcl::bit_cast<u64>(ptr)).c_str());
    }
}

size_t AddressSpace::GetRemainingSize() {
    return code_cache_size - static_cast<size_t>(code.offset());
}

EmittedBlockInfo AddressSpace::Emit(IR::Block block) {
    if (GetRemainingSize() < 1024 * 1024) {
        ClearCache();
    }

    UnprotectCodeMemory();

    EmittedBlockInfo block_info = EmitArm64(code, std::move(block), GetEmitConfig(), fastmem_manager);

    ASSERT(block_entries.insert({block.Location(), block_info.entry_point}).second);
    ASSERT(reverse_block_entries.insert({block_info.entry_point, block.Location()}).second);
    ASSERT(block_infos.insert({block_info.entry_point, block_info}).second);

    Link(block_info);
    RelinkForDescriptor(block.Location(), block_info.entry_point);

    mem.invalidate(reinterpret_cast<u32*>(block_info.entry_point), block_info.size);
    ProtectCodeMemory();

    RegisterNewBasicBlock(block, block_info);

    return block_info;
}

void AddressSpace::Link(EmittedBlockInfo& block_info) {
    using namespace oaknut;
    using namespace oaknut::util;

    for (auto [ptr_offset, target] : block_info.relocations) {
        CodeGenerator c{mem.ptr(), mem.ptr()};
        c.set_xptr(reinterpret_cast<u32*>(block_info.entry_point + ptr_offset));

        switch (target) {
        case LinkTarget::ReturnToDispatcher:
            c.B(prelude_info.return_to_dispatcher);
            break;
        case LinkTarget::ReturnFromRunCode:
            c.B(prelude_info.return_from_run_code);
            break;
        case LinkTarget::ReadMemory8:
            c.BL(prelude_info.read_memory_8);
            break;
        case LinkTarget::ReadMemory16:
            c.BL(prelude_info.read_memory_16);
            break;
        case LinkTarget::ReadMemory32:
            c.BL(prelude_info.read_memory_32);
            break;
        case LinkTarget::ReadMemory64:
            c.BL(prelude_info.read_memory_64);
            break;
        case LinkTarget::ReadMemory128:
            c.BL(prelude_info.read_memory_128);
            break;
        case LinkTarget::WrappedReadMemory8:
            c.BL(prelude_info.wrapped_read_memory_8);
            break;
        case LinkTarget::WrappedReadMemory16:
            c.BL(prelude_info.wrapped_read_memory_16);
            break;
        case LinkTarget::WrappedReadMemory32:
            c.BL(prelude_info.wrapped_read_memory_32);
            break;
        case LinkTarget::WrappedReadMemory64:
            c.BL(prelude_info.wrapped_read_memory_64);
            break;
        case LinkTarget::WrappedReadMemory128:
            c.BL(prelude_info.wrapped_read_memory_128);
            break;
        case LinkTarget::ExclusiveReadMemory8:
            c.BL(prelude_info.exclusive_read_memory_8);
            break;
        case LinkTarget::ExclusiveReadMemory16:
            c.BL(prelude_info.exclusive_read_memory_16);
            break;
        case LinkTarget::ExclusiveReadMemory32:
            c.BL(prelude_info.exclusive_read_memory_32);
            break;
        case LinkTarget::ExclusiveReadMemory64:
            c.BL(prelude_info.exclusive_read_memory_64);
            break;
        case LinkTarget::ExclusiveReadMemory128:
            c.BL(prelude_info.exclusive_read_memory_128);
            break;
        case LinkTarget::WriteMemory8:
            c.BL(prelude_info.write_memory_8);
            break;
        case LinkTarget::WriteMemory16:
            c.BL(prelude_info.write_memory_16);
            break;
        case LinkTarget::WriteMemory32:
            c.BL(prelude_info.write_memory_32);
            break;
        case LinkTarget::WriteMemory64:
            c.BL(prelude_info.write_memory_64);
            break;
        case LinkTarget::WriteMemory128:
            c.BL(prelude_info.write_memory_128);
            break;
        case LinkTarget::WrappedWriteMemory8:
            c.BL(prelude_info.wrapped_write_memory_8);
            break;
        case LinkTarget::WrappedWriteMemory16:
            c.BL(prelude_info.wrapped_write_memory_16);
            break;
        case LinkTarget::WrappedWriteMemory32:
            c.BL(prelude_info.wrapped_write_memory_32);
            break;
        case LinkTarget::WrappedWriteMemory64:
            c.BL(prelude_info.wrapped_write_memory_64);
            break;
        case LinkTarget::WrappedWriteMemory128:
            c.BL(prelude_info.wrapped_write_memory_128);
            break;
        case LinkTarget::ExclusiveWriteMemory8:
            c.BL(prelude_info.exclusive_write_memory_8);
            break;
        case LinkTarget::ExclusiveWriteMemory16:
            c.BL(prelude_info.exclusive_write_memory_16);
            break;
        case LinkTarget::ExclusiveWriteMemory32:
            c.BL(prelude_info.exclusive_write_memory_32);
            break;
        case LinkTarget::ExclusiveWriteMemory64:
            c.BL(prelude_info.exclusive_write_memory_64);
            break;
        case LinkTarget::ExclusiveWriteMemory128:
            c.BL(prelude_info.exclusive_write_memory_128);
            break;
        case LinkTarget::CallSVC:
            c.BL(prelude_info.call_svc);
            break;
        case LinkTarget::InterpreterFallback:
            c.BL(prelude_info.interpreter_fallback);
            break;
        case LinkTarget::ExceptionRaised:
            c.BL(prelude_info.exception_raised);
            break;
        case LinkTarget::InstructionSynchronizationBarrierRaised:
            c.BL(prelude_info.isb_raised);
            break;
        case LinkTarget::InstructionCacheOperationRaised:
            c.BL(prelude_info.ic_raised);
            break;
        case LinkTarget::DataCacheOperationRaised:
            c.BL(prelude_info.dc_raised);
            break;
        case LinkTarget::GetCNTPCT:
            c.BL(prelude_info.get_cntpct);
            break;
        case LinkTarget::AddTicks:
            c.BL(prelude_info.add_ticks);
            break;
        case LinkTarget::GetTicksRemaining:
            c.BL(prelude_info.get_ticks_remaining);
            break;
        default:
            ASSERT_FALSE("Invalid relocation target");
        }
    }

    for (auto [target_descriptor, list] : block_info.block_relocations) {
        block_references[target_descriptor].insert(block_info.entry_point);
        LinkBlockLinks(block_info.entry_point, Get(target_descriptor), list);
    }
}

void AddressSpace::LinkBlockLinks(const CodePtr entry_point, const CodePtr target_ptr, const std::vector<BlockRelocation>& block_relocations_list) {
    using namespace oaknut;
    using namespace oaknut::util;

    for (auto [ptr_offset, type] : block_relocations_list) {
        CodeGenerator c{mem.ptr(), mem.ptr()};
        c.set_xptr(reinterpret_cast<u32*>(entry_point + ptr_offset));

        switch (type) {
        case BlockRelocationType::Branch:
            if (target_ptr) {
                c.B((void*)target_ptr);
            } else {
                c.NOP();
            }
            break;
        case BlockRelocationType::MoveToScratch1:
            if (target_ptr) {
                c.ADRL(Xscratch1, (void*)target_ptr);
            } else {
                c.ADRL(Xscratch1, prelude_info.return_to_dispatcher);
            }
            break;
        default:
            ASSERT_FALSE("Invalid BlockRelocationType");
        }
    }
}

void AddressSpace::RelinkForDescriptor(IR::LocationDescriptor target_descriptor, CodePtr target_ptr) {
    for (auto code_ptr : block_references[target_descriptor]) {
        if (auto block_iter = block_infos.find(code_ptr); block_iter != block_infos.end()) {
            const EmittedBlockInfo& block_info = block_iter->second;

            if (auto relocation_iter = block_info.block_relocations.find(target_descriptor); relocation_iter != block_info.block_relocations.end()) {
                LinkBlockLinks(block_info.entry_point, target_ptr, relocation_iter->second);
            }

            mem.invalidate(reinterpret_cast<u32*>(block_info.entry_point), block_info.size);
        }
    }
}

FakeCall AddressSpace::FastmemCallback(u64 host_pc) {
    {
        const auto host_ptr = mcl::bit_cast<CodePtr>(host_pc);

        const auto entry_point = ReverseGetEntryPoint(host_ptr);
        if (!entry_point) {
            goto fail;
        }

        const auto block_info = block_infos.find(entry_point);
        if (block_info == block_infos.end()) {
            goto fail;
        }

        const auto patch_entry = block_info->second.fastmem_patch_info.find(host_ptr - entry_point);
        if (patch_entry == block_info->second.fastmem_patch_info.end()) {
            goto fail;
        }

        const auto fc = patch_entry->second.fc;

        if (patch_entry->second.recompile) {
            const auto marker = patch_entry->second.marker;
            fastmem_manager.MarkDoNotFastmem(marker);
            InvalidateBasicBlocks({std::get<0>(marker)});
        }

        return fc;
    }

fail:
    fmt::print("dynarmic: Segfault happened within JITted code at host_pc = {:016x}\n", host_pc);
    fmt::print("Segfault wasn't at a fastmem patch location!\n");
    ASSERT_FALSE("segfault");
}

}  // namespace Dynarmic::Backend::Arm64
