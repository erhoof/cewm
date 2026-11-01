// Unicorn owns CPU execution, CP15 and the MMU; CERF owns physical memory and devices.
#include "arm_unicorn.h"
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/physical_memory_host.h>
#include <unicorn/unicorn.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <vector>

extern "C" {
void cewm_uc_exception(uc_engine*, int);
int cewm_uc_take_idle(uc_engine*);
void cewm_uc_configure(uc_engine*, uint32_t, uint32_t, int, int);
uc_err cewm_uc_map_rom(uc_engine*, uint64_t, uint64_t, void*, uc_cb_mmio_write_t, void*);
extern uint64_t* symvm_ctr;
extern int symvm_exact_clock;
}
extern std::vector<uint32_t> g_cewm_pc_addresses;

namespace cerfengine {
CERFENGINE_REGISTER(ArmUnicorn);

struct ArmUnicorn::Impl {
    Engine& e;
    ArmCpu& cpu;
    ArmCpuState& st;
    ArmMmu& mmu;
    uc_engine* uc = nullptr;
    uint64_t counter[2] = {};
    uint64_t accounted = 0;
    bool redirect = false;
    std::exception_ptr fault;
    struct Region { Impl* self; uint32_t base; };
    std::vector<std::unique_ptr<Region>> regions;
    static constexpr uint32_t modes[] = {0x1f, 0x11, 0x12, 0x13, 0x17, 0x1b};
    static constexpr int regs[] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
        UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
        UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC};

    void Check(uc_err err) {
        if (err != UC_ERR_OK) e.Halt(HaltReason::kUnimplemented, "Unicorn: %s at %08X", uc_strerror(err), st.gprs[15]);
    }
    uint32_t Reg(int id) { uint32_t v = 0; Check(uc_reg_read(uc, id, &v)); return v; }
    void Set(int id, uint32_t v) { Check(uc_reg_write(uc, id, &v)); }
    uint32_t Cp(unsigned n, unsigned op = 0) {
        uc_arm_cp_reg r{15, 0, 0, n, 0, 0, op, 0};
        Check(uc_reg_read(uc, UC_ARM_REG_CP_REG, &r)); return uint32_t(r.val);
    }
    void SetCp(unsigned n, unsigned op, uint32_t v) {
        uc_arm_cp_reg r{15, 0, 0, n, 0, 0, op, v}; Check(uc_reg_write(uc, UC_ARM_REG_CP_REG, &r));
    }
    ArmCoprocDecision Decision(unsigned n, unsigned op = 0) {
        ArmCoprocAccess a; a.cp_num = 15; a.crn = n; a.opc2 = op; a.read = true;
        return e.GetSetup().arm_system->ArmCoprocessor(a);
    }
    uint32_t Id(unsigned op) {
        const auto d = Decision(0, op);
        return d.action == ArmCoprocAction::kConstant ? d.constant : 0;
    }
    void Account() {
        st.guest_cycle_counter += uint32_t(counter[0] - accounted);
        accounted = counter[0];
    }
    void LiveOut() {
        for (unsigned i = 0; i < 16; ++i) st.gprs[i] = Reg(regs[i]);
        ArmApplyCpsr(st, Reg(UC_ARM_REG_CPSR));
    }
    void LiveIn() {
        Set(UC_ARM_REG_CPSR, ArmPackCpsr(st) & ~0x20u);
        for (unsigned i = 0; i < 15; ++i) Set(regs[i], st.gprs[i]);
        Set(UC_ARM_REG_PC, st.gprs[15] | st.cpsr.bits.thumb_mode);
    }
    void BanksIn() {
        for (unsigned i = 0; i < 6; ++i) {
            Set(UC_ARM_REG_CPSR, modes[i] | 0xc0);
            Set(UC_ARM_REG_SP, *cpu.BankedSp(modes[i]));
            const auto current = st.cpsr.bits.mode;
            const bool active = current == modes[i] || (i == 0 && current == ArmMode::kUser);
            Set(UC_ARM_REG_LR, active ? st.gprs[14] : st.lr_bank[i]);
            if (i) Set(UC_ARM_REG_SPSR, cpu.BankedSpsr(modes[i])->word);
        }
        Set(UC_ARM_REG_CPSR, (st.cpsr.bits.mode == ArmMode::kFiq ? ArmMode::kSystem : ArmMode::kFiq) | 0xc0);
        for (unsigned i = 0; i < 5; ++i) Set(regs[8 + i], st.r8_r12_fiq[i]);
        LiveIn();
    }
    void BanksOut() {
        LiveOut();
        const uint32_t cpsr = ArmPackCpsr(st);
        for (unsigned i = 0; i < 6; ++i) {
            Set(UC_ARM_REG_CPSR, modes[i] | 0xc0);
            st.sp_bank[i] = Reg(UC_ARM_REG_SP); st.lr_bank[i] = Reg(UC_ARM_REG_LR);
            if (i) st.spsr_bank[i - 1].word = Reg(UC_ARM_REG_SPSR);
        }
        Set(UC_ARM_REG_CPSR, (st.cpsr.bits.mode == ArmMode::kFiq ? ArmMode::kSystem : ArmMode::kFiq) | 0xc0);
        for (unsigned i = 0; i < 5; ++i) st.r8_r12_fiq[i] = Reg(regs[8 + i]);
        Set(UC_ARM_REG_CPSR, cpsr);
    }
    void CpIn() {
        for (unsigned n : {2u, 3u, 5u, 6u}) {
            const auto d = Decision(n); if (d.reg) SetCp(n, 0, *d.reg);
        }
        SetCp(13, 0, mmu.State()->process_id);
        SetCp(1, 0, mmu.State()->control_register.word);
        Check(uc_ctl_flush_tlb(uc)); Check(uc_ctl_flush_tb(uc));
    }
    void CpOut() {
        bool changed = false;
        for (unsigned n : {2u, 3u, 5u, 6u}) {
            const auto d = Decision(n);
            if (d.reg) { const auto v = Cp(n); changed |= *d.reg != v; *d.reg = v; }
        }
        auto* m = mmu.State();
        const auto ctl = Cp(1), pid = Cp(13);
        if (m->control_register.word != ctl) { mmu.SetControlRegister(ctl); changed = true; }
        if (m->process_id != pid) { m->process_id = pid; mmu.RefreshFcseFold(); changed = true; }
        mmu.SynchronizeSctlr();
        if (changed) mmu.InvalidateAllTlbs(); // CERF's debugger/trace probes must see Unicorn's current mappings.
    }
    template<class F> void Callback(F fn) noexcept {
        try { fn(); } catch (...) { fault = std::current_exception(); uc_emu_stop(uc); }
    }
    static void Block(uc_engine*, uint64_t, uint32_t, void* opaque) {
        auto& s = *static_cast<Impl*>(opaque);
        s.Callback([&] {
            s.Account();
            if (s.counter[0] >= s.counter[1] || std::atomic_ref<uint32_t>(s.st.chain_exit_request).load(std::memory_order_relaxed)) uc_emu_stop(s.uc);
        });
    }
    static void Interrupt(uc_engine*, uint32_t number, void* opaque) {
        auto& s = *static_cast<Impl*>(opaque);
        s.Callback([&] {
            if (number < 1 || number > 4) s.e.Halt(HaltReason::kUnimplemented, "Unicorn exception %u", number);
            cewm_uc_exception(s.uc, int(number));
        });
    }
    static bool Invalid(uc_engine*, void* opaque) {
        auto& s = *static_cast<Impl*>(opaque);
        s.Callback([&] { cewm_uc_exception(s.uc, 1); });
        return !s.fault;
    }
    static uint64_t Read(uc_engine*, uint64_t off, unsigned size, void* opaque) {
        auto& r = *static_cast<Region*>(opaque); auto& s = *r.self;
        uint64_t value = 0;
        s.Callback([&] {
            s.Account();
            const uint32_t pa = r.base + uint32_t(off);
            if (size == 8) value = host::MmioReadDword(s.e.GetSetup().mmio.context, pa);
            else { MmioRoute route; value = host::MmioReadSlow(s.e.GetSetup().mmio.context, pa, MmioWidth(size), route); }
        });
        return value;
    }
    static void Write(uc_engine*, uint64_t off, unsigned size, uint64_t value, void* opaque) {
        auto& r = *static_cast<Region*>(opaque); auto& s = *r.self;
        s.Callback([&] {
            s.Account();
            const uint32_t pa = r.base + uint32_t(off);
            if (size == 8) host::MmioWriteDword(s.e.GetSetup().mmio.context, pa, value);
            else { MmioRoute route; host::MmioWriteSlow(s.e.GetSetup().mmio.context, pa, uint32_t(value), MmioWidth(size), route); }
        });
    }
    static void Trace(uc_engine*, uint64_t addr, uint32_t, void* opaque) {
        auto& s = *static_cast<Impl*>(opaque);
        s.Callback([&] {
            if (!s.e.Host().HasPcTrace(uint32_t(addr))) return;
            s.Account(); s.LiveOut(); s.CpOut();
            const auto before = s.st;
            const auto old_pc = s.st.gprs[15];
            s.e.Host().OnArmPcTrace(old_pc, s.st.gprs, ArmPackCpsr(s.st));
            // Trace hooks can replace a guest call or change its arguments.
            if (ArmPackCpsr(s.st) != ArmPackCpsr(before)) s.Set(UC_ARM_REG_CPSR, ArmPackCpsr(s.st));
            for (unsigned i = 0; i < 16; ++i) if (s.st.gprs[i] != before.gprs[i]) s.Set(regs[i], s.st.gprs[i]);
            if (s.st.gprs[15] != old_pc) { s.redirect = true; uc_emu_stop(s.uc); }
        });
    }
    explicit Impl(Engine& engine) : e(engine), cpu(e.Get<ArmCpu>()), st(*cpu.State()), mmu(e.Get<ArmMmu>()) {
        Check(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc));
        try {
            Check(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_926));
            // Initialize the CPU before configuring the Device Emulator's ARM quirks.
            Reg(UC_ARM_REG_CPSR);
            cewm_uc_configure(uc, Id(0), Id(1), int(e.GetSetup().arm.pc_store_offset) - 8, e.GetSetup().arm.load_to_pc_interworking);
            // Coalesce linear physical pages; holes retain CERF's existing MMIO routing.
            uint64_t start = 0;
            while (start < 0x100000000ull) {
                PhysicalPage page;
                host::PhysicalTranslate(e.GetSetup().memory, uint32_t(start), false, page);
                uint64_t end = start + 4096;
                while (end < 0x100000000ull) {
                    PhysicalPage next;
                    host::PhysicalTranslate(e.GetSetup().memory, uint32_t(end), false, next);
                    if (page.read ? (next.read != page.read + (end - start) || bool(next.write) != bool(page.write)) : next.read != nullptr) break;
                    end += 4096;
                }
                if (page.read && page.write) Check(uc_mem_map_ptr(uc, start, size_t(end - start), UC_PROT_ALL, page.read));
                else {
                    regions.push_back(std::make_unique<Region>(Region{this, uint32_t(start)}));
                    if (page.read) Check(cewm_uc_map_rom(uc, start, size_t(end - start), page.read, Write, regions.back().get()));
                    else Check(uc_mmio_map(uc, start, size_t(end - start), Read, regions.back().get(), Write, regions.back().get()));
                }
                start = end;
            }
            symvm_ctr = counter; symvm_exact_clock = 1;
            uc_hook hook;
            Check(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, reinterpret_cast<void*>(Block), this, 1, 0));
            Check(uc_hook_add(uc, &hook, UC_HOOK_INTR, reinterpret_cast<void*>(Interrupt), this, 1, 0));
            Check(uc_hook_add(uc, &hook, UC_HOOK_INSN_INVALID, reinterpret_cast<void*>(Invalid), this, 1, 0));
            auto addresses = g_cewm_pc_addresses;
            std::sort(addresses.begin(), addresses.end());
            addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
            for (uint32_t pc : addresses) Check(uc_hook_add(uc, &hook, UC_HOOK_CODE, reinterpret_cast<void*>(Trace), this, pc, pc));
            fprintf(stderr, "[UNICORN] CPU + MMU enabled, %zu PC hooks\n", addresses.size());
        } catch (...) { uc_close(uc); uc = nullptr; throw; }
    }
    ~Impl() { if (uc) uc_close(uc); if (symvm_ctr == counter) symvm_ctr = nullptr; }
};

ArmUnicorn::ArmUnicorn(Engine& e) : EngineService(e) {}
ArmUnicorn::~ArmUnicorn() = default;
bool ArmUnicorn::ShouldRegister() {
    const char* enabled = std::getenv("CEWM_UNICORN");
    return enabled && *enabled == '1' && engine_.GetSetup().isa == Isa::kArm;
}
void ArmUnicorn::Flush() { reload_ = true; }
uint32_t ArmUnicorn::Run(uint32_t budget) {
    if (!impl_) impl_ = std::make_unique<Impl>(engine_);
    auto& s = *impl_;
    s.BanksIn();
    if (reload_) { s.CpIn(); reload_ = false; }
    // shortcut: guest polling loops stay in Unicorn; add native idle detection if phone idle CPU warrants it.
    s.counter[0] = s.accounted = 0; s.counter[1] = budget;
    s.fault = nullptr;
    uc_err err;
    do {
        s.redirect = false;
        const auto pc = s.Reg(UC_ARM_REG_PC) | ((s.Reg(UC_ARM_REG_CPSR) >> 5) & 1);
        err = uc_emu_start(s.uc, pc, 0x100000000ull, 0, 0);
    } while (s.redirect && !s.fault && s.counter[0] < budget);
    s.Account(); s.BanksOut(); s.CpOut();
    if (s.fault) std::rethrow_exception(s.fault);
    s.Check(err);
    if (cewm_uc_take_idle(s.uc)) engine_.Get<ArmInterruptChannel>().EnterIdle();
    return uint32_t(s.counter[0]);
}
}
