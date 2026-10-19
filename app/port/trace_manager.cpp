// POSIX port fork of cerf/cerf/tracing/trace_manager.cpp (MIT): table-driven CRC-32 (startup time).
#include <array>
#include "tracing/trace_manager.h"

#include "core/cerf_emulator.h"
#include "core/log.h"
#include "boot/rom_parser_service.h"
#include "execution/engine_bridge.h"
#include <cerfengine/mips/mips_cpu_state.h>

REGISTER_SERVICE(TraceManager);

namespace {

/* CRC-32 / zlib (polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR
   0xFFFFFFFF). Compatible with `python -c "import zlib;
   print(hex(zlib.crc32(open('x','rb').read())))"` so callers can
   pre-compute the bundle CRC offline and embed it in hook files. */
uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t n) {
    // Same CRC-32 as upstream's bitwise loop, with a byte table: the bitwise version needs ~0.7 s for the 96 MB ROM at every start.
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

}  /* namespace */

std::optional<uint8_t> TraceContext::ReadVa8(uint32_t va) const {
    auto host = emu.Get<EngineBridge>().Guest().PeekGuestVa(va);
    if (!host) return std::nullopt;
    return **host;
}

std::optional<uint16_t> TraceContext::ReadVa16(uint32_t va) const {
    auto lo = ReadVa8(va);
    auto hi = ReadVa8(va + 1);
    if (!lo || !hi) return std::nullopt;
    return static_cast<uint16_t>(*lo | (*hi << 8));
}

std::optional<uint32_t> TraceContext::ReadVa32(uint32_t va) const {
    auto lo = ReadVa16(va);
    auto hi = ReadVa16(va + 2);
    if (!lo || !hi) return std::nullopt;
    return static_cast<uint32_t>(*lo) | (static_cast<uint32_t>(*hi) << 16);
}

void TraceManager::OnReady() {
    bundle_crc32_ = ComputeBundleCrc32();
    LOG(Trace, "bundle CRC32 = 0x%08X (awaiting hook registrations)\n",
        bundle_crc32_);
}

uint32_t TraceManager::ComputeBundleCrc32() const {
    uint32_t crc = 0;
    if (auto* rom = emu_.TryGet<RomParserService>()) {
        for (const auto& r : rom->Loaded())
            crc = Crc32Update(crc, r.raw.data(), r.raw.size());
    }
    return crc;
}

void TraceManager::RegisterForBundle(
        uint32_t expected_crc32,
        const std::function<void()>& register_fn) {
    if (expected_crc32 != bundle_crc32_) {
        ++bundles_skipped_;
        LOG(Trace, "skipping hook file (expected CRC 0x%08X, bundle is 0x%08X)\n",
            expected_crc32, bundle_crc32_);
        return;
    }
    ++bundles_matched_;
    register_fn();
}

void TraceManager::GuardUnique(uint32_t runtime_va) {
    if (pc_traces_.count(runtime_va) == 0) return;
    LOG(Caution, "TraceManager duplicate registration at runtime_va=0x%08X - a "
                 "trace handler is already bound at this guest PC. A VA may be "
                 "hooked exactly once, by OnPc or OnPcFiltered (not both, not "
                 "twice).\n", runtime_va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

namespace cerfengine { extern bool g_pc_hooks_on; }
// HasPcTrace runs at every interpreted block entry; this bitmap answers "no hook here" without a hash lookup.
// shortcut: bits are never cleared, a stale bit only costs one hash lookup; one filter shared by all TraceManagers.
uint64_t g_cewm_pc_filter[1024];  // read directly by the interpreter (engine/arm_interp.cpp)
#define g_pc_filter g_cewm_pc_filter
static inline void PcFilterSet(uint32_t pc) { const uint32_t h = (pc >> 1) & 0xFFFFu; g_pc_filter[h >> 6] |= 1ull << (h & 63); }

void TraceManager::OnPc(uint32_t runtime_va, TraceHandler handler) {
    PcFilterSet(runtime_va);
    cerfengine::g_pc_hooks_on = true;
    GuardUnique(runtime_va);
    pc_traces_[runtime_va] = {std::nullopt, std::move(handler)};
}

void TraceManager::OnPcFiltered(uint32_t       runtime_va,
                                TracePredicate predicate,
                                TraceHandler   handler) {
    PcFilterSet(runtime_va);
    cerfengine::g_pc_hooks_on = true;
    GuardUnique(runtime_va);
    pc_traces_[runtime_va] =
        {std::optional<TracePredicate>{std::move(predicate)},
         std::move(handler)};
}

bool TraceManager::HasPcTrace(uint32_t pc) const {
    const uint32_t h = (pc >> 1) & 0xFFFFu;
    if (!(g_pc_filter[h >> 6] >> (h & 63) & 1)) return false;
    return pc_traces_.count(pc) > 0;
}

void TraceManager::DispatchContext(uint32_t pc, const TraceContext& ctx) {
    auto it = pc_traces_.find(pc);
    if (it == pc_traces_.end()) return;
    const PcEntry& e = it->second;
    if (e.predicate.has_value() && !(*e.predicate)(ctx)) return;
    e.handler(ctx);
}

void TraceManager::DispatchPc(uint32_t pc,
                              const uint32_t* regs, uint32_t cpsr) {
    TraceContext ctx{regs, cpsr, pc, emu_};
    DispatchContext(pc, ctx);
}

void TraceManager::DispatchPcMips(uint32_t pc, const cerfengine::MipsCpuState* st) {
    TraceContext ctx{nullptr, 0u, pc, emu_, st};
    DispatchContext(pc, ctx);
}

#if CERF_DEV_MODE

void TraceManager::OnRunLoopIter(TraceHandler handler) {
    iter_handlers_.push_back(std::move(handler));
}

void TraceManager::DispatchIterContext(const TraceContext& ctx) {
    for (auto& h : iter_handlers_) h(ctx);
}

void TraceManager::DispatchRunLoopIter(const uint32_t* regs, uint32_t cpsr) {
    if (iter_handlers_.empty()) return;
    TraceContext ctx{regs, cpsr, regs[15], emu_};
    DispatchIterContext(ctx);
}

void TraceManager::DispatchRunLoopIterMips(const cerfengine::MipsCpuState* st) {
    if (iter_handlers_.empty()) return;
    TraceContext ctx{nullptr, 0u, st->pc, emu_, st};
    DispatchIterContext(ctx);
}

#endif  /* CERF_DEV_MODE */
