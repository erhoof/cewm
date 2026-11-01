// Standalone checks for the CEWM Unicorn patch: no CERF interpreter or walker.
#include <unicorn/unicorn.h>
#include <array>
#include <cstdio>
#include <cstdlib>

extern "C" {
void cewm_uc_exception(uc_engine*, int);
int cewm_uc_take_idle(uc_engine*);
void cewm_uc_configure(uc_engine*, uint32_t, uint32_t, int, int);
uc_err cewm_uc_map_rom(uc_engine*, uint64_t, uint64_t, void*, uc_cb_mmio_write_t, void*);
}
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
static void Check(uc_err err) { REQUIRE(err == UC_ERR_OK); }
static uint32_t Read(uc_engine* u, int reg) { uint32_t v; Check(uc_reg_read(u, reg, &v)); return v; }
static void Write(uc_engine* u, int reg, uint32_t v) { Check(uc_reg_write(u, reg, &v)); }
static uint32_t Cp(uc_engine* u, unsigned n) {
    uc_arm_cp_reg r{15, 0, 0, n, 0, 0, 0, 0}; Check(uc_reg_read(u, UC_ARM_REG_CP_REG, &r)); return uint32_t(r.val);
}
static void SetCp(uc_engine* u, unsigned n, uint32_t v) {
    uc_arm_cp_reg r{15, 0, 0, n, 0, 0, 0, v}; Check(uc_reg_write(u, UC_ARM_REG_CP_REG, &r));
}
static void Intr(uc_engine* u, uint32_t n, void*) { cewm_uc_exception(u, int(n)); uc_emu_stop(u); }
static void FlashWrite(uc_engine*, uint64_t offset, unsigned size, uint64_t value, void* opaque) {
    REQUIRE(offset == 4 && size == 4 && value == 0x90);
    ++*static_cast<unsigned*>(opaque);
}
int main() {
    uc_engine* u = nullptr;
    Check(uc_open(UC_ARCH_ARM, UC_MODE_ARM, &u));
    Check(uc_ctl_set_cpu_model(u, UC_CPU_ARM_926));
    Read(u, UC_ARM_REG_CPSR);
    cewm_uc_configure(u, 0x69059201, 0x0b172172, 4, 0);
    REQUIRE(Cp(u, 0) == 0x69059201);
    Check(uc_mem_map(u, 0, 0x20000, UC_PROT_ALL));
    const std::array<uint32_t, 5> code = {0xe580f000, 0xe8a08000, 0xef000000, 0xee070f90, 0xe5901000};
    Check(uc_mem_write(u, 0x1000, code.data(), sizeof(code)));
    Write(u, UC_ARM_REG_R0, 0x8000);
    Write(u, UC_ARM_REG_CPSR, 0xd3);
    Check(uc_emu_start(u, 0x1000, 0x1008, 0, 0));
    uint32_t stored = 0; Check(uc_mem_read(u, 0x8000, &stored, 4)); REQUIRE(stored == 0x1010); // STM PC+12
    Write(u, UC_ARM_REG_R0, 0x8000);
    Check(uc_emu_start(u, 0x1000, 0x1004, 0, 0));
    Check(uc_mem_read(u, 0x8000, &stored, 4)); REQUIRE(stored == 0x100c); // STR PC+12
    uc_hook h; Check(uc_hook_add(u, &h, UC_HOOK_INTR, reinterpret_cast<void*>(Intr), nullptr, 1, 0));
    SetCp(u, 1, Cp(u, 1) | (1u << 30) | (1u << 25)); // TE/EE are reserved on ARMv5.
    Write(u, UC_ARM_REG_CPSR, 0x10);
    Write(u, UC_ARM_REG_SP, 0x9000);
    Check(uc_emu_start(u, 0x1008, 0x20000, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_PC) == 8);
    REQUIRE((Read(u, UC_ARM_REG_CPSR) & 0x1f) == 0x13);
    REQUIRE(!(Read(u, UC_ARM_REG_CPSR) & 0x220));
    REQUIRE(Read(u, UC_ARM_REG_LR) == 0x100c);
    REQUIRE(Read(u, UC_ARM_REG_SPSR) == 0x10);
    Check(uc_emu_start(u, 0x100c, 0x20000, 0, 0));
    REQUIRE(cewm_uc_take_idle(u));
    Write(u, UC_ARM_REG_R0, 0x8000);
    Check(uc_emu_start(u, 0x1010, 0x1014, 0, 0)); // resumes after WFI
    REQUIRE(Read(u, UC_ARM_REG_R1) == 0x100c);
    // Identity-map the first section, leave section 1 unmapped: Unicorn must walk and fault.
    uint32_t section = 0x00000c02;
    Check(uc_mem_write(u, 0x4000, &section, 4));
    SetCp(u, 2, 0x4000); SetCp(u, 3, 1); SetCp(u, 1, Cp(u, 1) | 1);
    Write(u, UC_ARM_REG_R0, 0x100000);
    Check(uc_emu_start(u, 0x1010, 0x20000, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_PC) == 0x10);
    REQUIRE((Read(u, UC_ARM_REG_CPSR) & 0x1f) == 0x17);
    REQUIRE(Read(u, UC_ARM_REG_LR) == 0x1018);
    REQUIRE((Cp(u, 5) & 15) == 5);
    REQUIRE(Cp(u, 6) == 0x100000);
    // A mapped but privileged-only section must fault in user mode.
    section = 0x00000402;
    Check(uc_mem_write(u, 0x4004, &section, 4));
    Check(uc_ctl_flush_tlb(u));
    Write(u, UC_ARM_REG_CPSR, 0x10);
    Check(uc_emu_start(u, 0x1010, 0x20000, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_PC) == 0x10);
    REQUIRE((Cp(u, 5) & 15) == 13);
    REQUIRE(Cp(u, 6) == 0x100000);
    REQUIRE(Read(u, UC_ARM_REG_SPSR) == 0x10);
    Check(uc_emu_start(u, 0x200000, 0x20000, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_PC) == 0x0c);
    REQUIRE(Read(u, UC_ARM_REG_LR) == 0x200004);
    REQUIRE((Cp(u, 5) & 15) == 5);
    REQUIRE(Cp(u, 6) == 0x100000); // prefetch abort must preserve DFAR
    SetCp(u, 1, Cp(u, 1) & ~1u);
    alignas(4096) std::array<uint32_t, 1024> flash{};
    flash[0] = 0xe3a0202a; // mov r2,#42
    unsigned commands = 0;
    Check(cewm_uc_map_rom(u, 0x20000, sizeof(flash), flash.data(), FlashWrite, &commands));
    Check(uc_emu_start(u, 0x20000, 0x20004, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_R2) == 42); // executable ROM reads use the backing pointer
    const uint32_t store = 0xe5801000; // str r1,[r0]
    Check(uc_mem_write(u, 0x1100, &store, 4));
    Write(u, UC_ARM_REG_R0, 0x20004); Write(u, UC_ARM_REG_R1, 0x90);
    Check(uc_emu_start(u, 0x1100, 0x1104, 0, 0));
    REQUIRE(commands == 1 && flash[1] == 0); // NOR commands never overwrite ROM bytes
    uint32_t mov = 0xe3a02001;
    Check(uc_mem_write(u, 0x1200, &mov, 4));
    Check(uc_emu_start(u, 0x1200, 0x1204, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_R2) == 1);
    Write(u, UC_ARM_REG_R0, 0x1200); Write(u, UC_ARM_REG_R1, 0xe3a02002);
    Check(uc_emu_start(u, 0x1100, 0x1104, 0, 0)); // guest modifies previously translated code
    Check(uc_emu_start(u, 0x1200, 0x1204, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_R2) == 2);
    const uint32_t load_pc = 0xe590f000, odd_pc = 0x1401;
    Check(uc_mem_write(u, 0x1300, &load_pc, 4));
    Check(uc_mem_write(u, 0x8000, &odd_pc, 4));
    Write(u, UC_ARM_REG_R0, 0x8000);
    Check(uc_emu_start(u, 0x1300, 0x1400, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_PC) == 0x1400);
    REQUIRE(!(Read(u, UC_ARM_REG_CPSR) & 0x20)); // Device Emulator LDR PC does not interwork
    // MMU-off/on trampolines keep the short prefetch window until the branch.
    const std::array<uint32_t, 3> trampoline = {0xee013f10, 0xe3a0202a, 0xe12fff14};
    const uint32_t target = 0xe3a05007;
    Check(uc_mem_write(u, 0x1500, trampoline.data(), sizeof(trampoline)));
    Check(uc_mem_write(u, 0x1600, &target, 4));
    section = 0x00000c02;
    Check(uc_mem_write(u, 0x6000, &section, 4)); // VA 0x80000000 -> PA 0
    const auto control = Cp(u, 1) & ~1u;
    SetCp(u, 1, control | 1);
    Write(u, UC_ARM_REG_R3, control); Write(u, UC_ARM_REG_R4, 0x1600);
    Check(uc_emu_start(u, 0x80001500, 0x1604, 0, 0));
    REQUIRE(Read(u, UC_ARM_REG_R2) == 42 && Read(u, UC_ARM_REG_R5) == 7);
    REQUIRE(!(Cp(u, 1) & 1));
    Write(u, UC_ARM_REG_R3, control | 1); Write(u, UC_ARM_REG_R4, 0x80001600);
    Check(uc_emu_start(u, 0x1500, 0x80001604, 0, 0));
    REQUIRE(Cp(u, 1) & 1);
    Check(uc_close(u));
    std::puts("Unicorn CPU, MMU, fault entry, banked state, PC stores, WFI and flash: passed");
}
