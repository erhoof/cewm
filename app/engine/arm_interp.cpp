#include "arm_interp.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/arm/arm_translator.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include <cerfengine/byte_order.h>

#ifdef CEWM_HAVE_DYNARMIC
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/terminal.h"
#endif

#include "arm_alu.h"
#include "arm/arm_decoder.h"
#include "arm/arm_opcode.h"
// ArmRoutedInstruction exposes only Complete(pc), which re-fetches and re-decodes; its per-family executors are private. We already hold the
// decoded instruction, so open them up for this one include (access control only, no layout change).
#define private public
#include "arm/arm_routed_instruction.h"
#undef private
#include "arm/arm_routed_access.h"
#include "arm/arm_routed_addressing.h"
#include "arm/arm_tlb_ops.h"
#include "arm/arm_translation_cache.h"
#include "arm/thumb32_decoder.h"
#include "arm/thumb_decoder.h"

#ifdef CEWM_HAVE_DYNARMIC
namespace Dynarmic::A32 { extern std::uint32_t g_pc_store_extra; }  // dynarmic fork: PC value stored by STR/STM
#endif

#ifndef CEWM_BLK_ENTRIES
#define CEWM_BLK_ENTRIES 8192  // direct-mapped; power of two
#endif
namespace cerfengine {

using namespace cewm::alu;

// Diagnostics of the dynarmic core (CEWM_DYNLOG=1); the CEWM_VERIFY output is not gated.
static const bool kDynLog = getenv("CEWM_DYNLOG") != nullptr;
#define DYNLOG(...) do { if (kDynLog) fprintf(stderr, __VA_ARGS__); } while (0)

struct DynCore;

// Block-cache statistics (diagnostics, printed by the headless test).
uint64_t g_idle_hits = 0;
static const bool kRegChk = getenv("CEWM_REGCHK") != nullptr;
static const bool kIdleSkip = getenv("CEWM_NOIDLESKIP") == nullptr;  // see Impl::BuildBlock: the WM idle spin turns into a host-side wait
uint64_t g_blk_runs = 0, g_blk_chained = 0, g_blk_slow = 0, g_blk_builds = 0, g_blk_ops = 0, g_blk_step = 0;

struct ArmInterp::Impl {
    Engine&               e;
    ArmCpu&               cpu;
    ArmCpuState&          st;
    ArmMmu&               mmu;
    ArmTranslator&        tr;
    ArmDecoder&           arm_dec;
    ThumbDecoder&         thumb_dec;
    Thumb32Decoder&       thumb32_dec;
    ArmRoutedInstruction& routed;
    ArmTranslationCache&  cache;
    ArmInterruptChannel&  chan;
    const ArmFeatures&    cfg;
    bool                  stop = false;
    struct FetchCache { uint32_t page = ~0u; uint8_t* base = nullptr; uint32_t sctlr = 0; uint32_t epoch = 0; bool user = false; uint32_t pa_page = 0; } fc;
    uint32_t              epoch = 1;  // bumped on every CP15 write (TLB/cache/MMU maintenance all go through MCR)
    struct DecEnt { uint32_t pc = ~0u; uint32_t raw = 0; bool thumb = false; bool ok = false; DecodedInsn d; };
    static constexpr uint32_t kDecEntries = 1u << 12;  // ~0.7 MB
    std::vector<DecEnt>   dcache = std::vector<DecEnt>(kDecEntries);
    // Data fast path: direct-mapped page cache of host pointers for RAM, same invalidation rules as the fetch cache.
    struct DataEnt { uint32_t page = ~0u; uint32_t ctx = 0; uint8_t* base = nullptr; bool writable = false; };
    // (epoch, user mode, effective SCTLR) folded into one counter so a lookup compares two words; refreshed once per block / Step.
    uint32_t t_epoch = 0, t_eff = 0, dctx = 1;
    bool t_user = false;
    void RefreshCtx() {
        const bool user = st.cpsr.bits.mode == ArmMode::kUser;
        const uint32_t eff = mmu.State()->effective_control_register.word;
        if (epoch != t_epoch || user != t_user || eff != t_eff) { t_epoch = epoch; t_user = user; t_eff = eff; ++dctx; }
    }
    static constexpr uint32_t kDataEntries = 256;
    DataEnt               dtlb[kDataEntries];
    // Block cache: runs of pre-decoded instructions inside one page, keyed by (virtual address, ISA state) and validated at entry against the
    // physical page and the code bytes, so remapping and self-modifying code both just miss.
    // kFirstLs + {0: LDR, 1: LDRB, 2: STR, 3: STRB, 4: LDR literal, 5: LDRB literal}: immediate-offset single transfers (pre-index, no writeback).
    // kDpImm..kDpShReg are the generic data-processing kinds (PC destination, odd forms); kFirstDp + form * 32 + S * 16 + opcode are the
    // specialised ones RunBlock dispatches straight to a DpOp instantiation.
    enum BlockKind : uint8_t { kDpImm, kDpReg, kDpShReg, kSingle, kBranch, kMul, kBlock, kOther, kFirstDp, kFirstLs = kFirstDp + 96 };
    static constexpr uint32_t kBlkMax = 32, kBlkEntries = CEWM_BLK_ENTRIES;
    // Compact copy of the operand fields the fast handlers use (16 bytes instead of reaching into the 152-byte DecodedInsn):
    // imm = DP immediate or the signed load/store offset, pcv = value of PC as an operand (address + 8 ARM / + 4 Thumb),
    // fl: bit0 U, bit1 P, bit2 W, bit3 L (link / load).
    struct Cop { uint8_t rd, rn, rm, rs, n, fl, pad0, pad1; uint32_t imm; uint32_t pcv; };
    struct Block {
        uint32_t va = ~0u, pa_page = 0, n = 0, span = 0;
        bool thumb = false, user = false;
        uint32_t gen = 0;                       // code_gen this block was validated under
        Block* succ[2] = {nullptr, nullptr};    // chained successors (taken / fall-through), checked by va + gen + mode
        uint32_t succ_va[2] = {~0u, ~0u};
        uint32_t succ_next = 0;
        bool idle = false;                      // spin-wait loop on RAM (see BuildBlock)
        uint8_t code[kBlkMax * 4];
        uint8_t kind[kBlkMax], len[kBlkMax];
        uint32_t aux[kBlkMax];                  // per op: absolute address of a literal load (see kFirstLs)
        uint32_t meta[kBlkMax];                 // per op: kind | cond << 8 | offset in block << 16 | length << 24 (one load per dispatch)
        Cop* cop = nullptr;                     // n compact ops, same index as d
        DecodedInsn* d = nullptr;               // exactly n ops (blocks average ~4 of kBlkMax, so a fixed array wasted ~5 KB of cache per block)
        uint32_t cap = 0;
        Block() = default;
        Block(const Block&) = delete;
        ~Block() { delete[] d; delete[] cop; }
    };
    DecodedInsn scratch[kBlkMax];
    uint32_t RdC(const Cop& c, uint32_t n) { return n == 15u ? c.pcv : R(n); }               // BuildBlock decodes here, then copies the n used ops into the block
    std::vector<Block> blocks = std::vector<Block>(kBlkEntries);
    // code_gen invalidates every chained link and cached validation at once: bumped on any CP15 write and on MMU-regime changes.
    uint32_t code_gen = 1;
    Block* last_blk = nullptr;
    // Adv = false: RunBlock keeps PC in a local and writes R15 only at block end / on redirects, so the fast handlers skip Next(d).
    template <bool Adv = true> bool FastBlockTransfer(const DecodedInsn& d);
    template <int F, bool S, int OP, bool Adv = false> bool DpOp(const Cop& c);
    template <bool L, bool WORD, bool LIT> bool LsImm(const Cop& c, uint32_t aux);
    template <bool L, bool WORD> bool LsImmWb(const Cop& c);
    template <int K, bool REG> bool HwOp(const Cop& c);
    void BuildBlock(Block& b, uint32_t pc, const uint8_t* host, bool thumb);
    InterpStep RunBlock(uint32_t* executed);
    bool                  sctlr_pending = false;
    // Events the dynarmic code cache must hear about (set by CP15 writes in the interpreter, consumed by DynCore).
    enum : uint8_t { kFlushAll = 1, kFlushRange = 2, kFlushEverything = 4, kRevalidate = 8, kRevalPage = 16 };
    uint32_t              jit_rv_addr = 0;  // page for kRevalPage
    uint8_t               jit_ev = 0;
    uint32_t              jit_ev_addr = 0, jit_ev_len = 0;
    DynCore*              dyn = nullptr;
    bool                  dyn_on = false;  // SCTLR written; applied at the next control-flow change (see Step)

    explicit Impl(Engine& eng)
        : e(eng), cpu(eng.Get<ArmCpu>()), st(*cpu.State()), mmu(eng.Get<ArmMmu>()), tr(eng.Get<ArmTranslator>()),
          arm_dec(eng.Get<ArmDecoder>()), thumb_dec(eng.Get<ThumbDecoder>()), thumb32_dec(eng.Get<Thumb32Decoder>()),
          routed(eng.Get<ArmRoutedInstruction>()), cache(eng.Get<ArmTranslationCache>()),
          chan(eng.Get<ArmInterruptChannel>()), cfg(eng.GetSetup().arm) {}

    uint32_t& R(uint32_t n) { return st.gprs[n]; }
    bool Thumb() const { return st.cpsr.bits.thumb_mode != 0u; }

    // Register read as an instruction operand: R15 reads as the instruction address + 8 (ARM) / + 4 (Thumb). ARM DDI 0100I A2.4.3.
    uint32_t Rd(const DecodedInsn& d, uint32_t n) { return n == 15u ? d.guest_address + (Thumb() ? 4u : 8u) : R(n); }

    void Next(const DecodedInsn& d) { R(15) = d.guest_address + d.length; }
    void Undef(const DecodedInsn& d) { cpu.RaiseUndefinedException(d.guest_address); }
    void SetNZ(uint32_t v) { st.nf = v >> 31; st.zf = v == 0; }

    void WritePcAlu(const DecodedInsn& d, uint32_t v) { R(15) = v & (Thumb() ? ~1u : ~3u); (void)d; }

    uint8_t* DataLookup(uint32_t addr, bool write);
    inline uint8_t* DataLookupFast(uint32_t addr, bool write);
    uint8_t* DataLookupMiss(uint32_t addr, bool write);
    template <bool Adv = true> bool FastSingle(const DecodedInsn& d);
    void DataProcessing(const DecodedInsn& d);
    void Multiply(const DecodedInsn& d);
    void Branch(const DecodedInsn& d);
    void MrsMsr(const DecodedInsn& d);
    void MsrWrite(const DecodedInsn& d, uint32_t value);
    void CoprocRegTransfer(const DecodedInsn& d);
    void Wfi(const DecodedInsn& d);
    void ExecDecoded(const DecodedInsn& d);
    InterpStep Step();
};

// Host pointer for `addr` if it is plain RAM/ROM the current mode may access this way; nullptr means "use the slow path"
// (MMIO, abort, flash writes, anything the translator declines).
uint8_t* ArmInterp::Impl::DataLookup(uint32_t addr, bool write) {
    RefreshCtx();
    return DataLookupFast(addr, write);
}

// Callers that run inside RunBlock/Step have refreshed the context already.
// The hit test is forced inline into the load/store handlers; the miss path (translate + refill) stays out of line.
__attribute__((always_inline)) inline uint8_t* ArmInterp::Impl::DataLookupFast(uint32_t addr, bool write) {
    DataEnt& e = dtlb[(addr >> 12) & (kDataEntries - 1)];
    if (__builtin_expect(e.page == (addr >> 12) && e.ctx == dctx && (!write || e.writable), 1)) return e.base + (addr & 0xFFFu);
    return DataLookupMiss(addr, write);
}

__attribute__((noinline)) uint8_t* ArmInterp::Impl::DataLookupMiss(uint32_t addr, bool write) {
    DataEnt& e = dtlb[(addr >> 12) & (kDataEntries - 1)];
    uint8_t* host = write ? tr.TranslateWrite(addr) : tr.TranslateRead(addr);
    if (host == nullptr) { mmu.ClearIoPending(); return nullptr; }
    const bool keep = e.page == (addr >> 12) && e.ctx == dctx && e.writable;
    e = {addr >> 12, dctx, host - (addr & 0xFFFu), write || keep};
    return host;
}

// LDR/STR/LDRB/STRB with word-aligned or byte addresses in plain memory; everything else (PC, unprivileged, unaligned, alignment checking,
// MMIO, faults) returns false before any state changes and runs through upstream's SingleTransfer.
template <bool Adv>
bool ArmInterp::Impl::FastSingle(const DecodedInsn& d) {
    if (d.unpriv || d.rd == 15u) return false;
    if constexpr (!Adv) { if (d.rn == 15u && !(d.n && d.p)) return false; }  // would read R15, which RunBlock no longer keeps current  // (SCTLR.A needs no check here: unaligned words bail out below, byte accesses cannot fault)
    DecodedInsn& dd = const_cast<DecodedInsn&>(d);
    const uint32_t offset_addr =
        d.n ? (d.rn == 15u ? ((d.guest_address + (Thumb() ? 4u : 8u)) & ~3u) : R(d.rn)) + static_cast<uint32_t>(d.offset)  // LDR (literal) aligns the PC
            : routed.addressing_->SingleOffsetAddr(&dd);
    const uint32_t address = d.p ? offset_addr : R(d.rn);
    const bool word = d.s == 0u;
    if (word && (address & 3u)) return false;
    uint8_t* host = DataLookupFast(address, !d.l);
    if (host == nullptr) return false;
    const bool wback = !d.p || d.w;
    if (d.l) {
        const uint32_t v = word ? le::U32(host) : *host;
        if (wback) R(d.rn) = offset_addr;
        R(d.rd) = v;
    } else {
        const uint32_t v = R(d.rd);
        if (word) le::Put32(host, v); else *host = static_cast<uint8_t>(v);
        if (wback) R(d.rn) = offset_addr;
    }
    if constexpr (Adv) Next(d);
    return true;
}

// LDM/STM (including PUSH/POP) on plain memory within one page. Anything else (S bit, PC in the list, base in the list with
// writeback, unaligned, MMIO, page crossing, faults) returns false before changing state and runs through upstream's BlockTransfer.
template <bool Adv>
bool ArmInterp::Impl::FastBlockTransfer(const DecodedInsn& d) {
    const uint32_t list = d.register_list;
    if (d.s || d.rn == 15u || (list & 0x8000u) || list == 0u) return false;
    if (d.w && (list >> d.rn & 1u)) return false;
    const uint32_t cnt = static_cast<uint32_t>(__builtin_popcount(list));
    const uint32_t base = R(d.rn);
    if (base & 3u) return false;
    uint32_t start;
    if (d.u) start = d.p ? base + 4u : base;
    else     start = d.p ? base - 4u * cnt : base - 4u * cnt + 4u;
    const uint32_t last = start + 4u * (cnt - 1u);
    if ((start >> 12) != (last >> 12)) return false;
    uint8_t* host = DataLookupFast(start, !d.l);
    if (host == nullptr) return false;
    if (d.l) {
        for (uint32_t m = list; m; m &= m - 1u) {
            R(static_cast<uint32_t>(__builtin_ctz(m))) = le::U32(host);
            host += 4;
        }
    } else {
        for (uint32_t m = list; m; m &= m - 1u) {
            le::Put32(host, R(static_cast<uint32_t>(__builtin_ctz(m))));
            host += 4;
        }
    }
    if (d.w) R(d.rn) = d.u ? base + 4u * cnt : base - 4u * cnt;
    if constexpr (Adv) Next(d);
    return true;
}

// Data processing with a non-PC destination (the bulk of it), one instantiation per (operand form, S, opcode) so a block's op kind selects
// straight-line code: no opcode switch, no S/rd tests. Operand carry is only worked out when flags are set.
// F: 0 = immediate, 1 = register with immediate shift, 2 = register-shifted register. Opcodes: 0 AND 1 EOR 2 SUB 3 RSB 4 ADD 5 ADC 6 SBC
// 7 RSC 8 TST 9 TEQ 10 CMP 11 CMN 12 ORR 13 MOV 14 BIC 15 MVN. Always returns true (BuildBlock only picks these kinds for valid forms).
template <int F, bool S, int OP, bool Adv>
bool ArmInterp::Impl::DpOp(const Cop& c) {
    constexpr bool cmp = OP >= 8 && OP <= 11;  // TST/TEQ/CMP/CMN only exist with S, write no register
    uint32_t b, sc = st.cf;
    if constexpr (F == 0) {
        b = c.imm;
        if constexpr (S) { if (c.rs != 0u) sc = b >> 31; }  // rotated immediate: carry is bit 31 (see DataProcessing)
    } else if constexpr (F == 1) {
        const uint32_t v = RdC(c, c.rm);
        if (c.n == kSrLsl && c.rs == 0u) b = v;
        else { const Shifted sh = ShiftBy(c.n, v, c.rs, st.cf); b = sh.v; sc = sh.c; }
    } else {
        const Shifted sh = ShiftBy(c.n, R(c.rm), R(c.rs) & 0xFFu, st.cf);
        b = sh.v; sc = sh.c;
    }
    const uint32_t a = (OP == 13 || OP == 15) ? 0u : RdC(c, c.rn);
    uint32_t r;
    if constexpr (S) {  // flag-setting: arithmetic as x + y + cin (SUB = x + ~y + 1), logic keeps V
        constexpr bool arith = OP == 2 || OP == 3 || OP == 4 || OP == 5 || OP == 6 || OP == 7 || OP == 10 || OP == 11;
        if constexpr (arith) {
            uint32_t x = a, y = b, cin = 0;
            if constexpr (OP == 2 || OP == 10) { y = ~b; cin = 1; }
            else if constexpr (OP == 3) { x = b; y = ~a; cin = 1; }
            else if constexpr (OP == 5) cin = st.cf;
            else if constexpr (OP == 6) { y = ~b; cin = st.cf; }
            else if constexpr (OP == 7) { x = b; y = ~a; cin = st.cf; }
            const uint64_t sum = static_cast<uint64_t>(x) + y + cin;
            r = static_cast<uint32_t>(sum);
            st.cf = static_cast<uint32_t>(sum >> 32);
            st.vf = (~(x ^ y) & (x ^ r)) >> 31;
        } else {
            if constexpr (OP == 0 || OP == 8) r = a & b;
            else if constexpr (OP == 1 || OP == 9) r = a ^ b;
            else if constexpr (OP == 12) r = a | b;
            else if constexpr (OP == 13) r = b;
            else if constexpr (OP == 14) r = a & ~b;
            else r = ~b;
            st.cf = sc;
        }
        SetNZ(r);
    } else {
        if constexpr (OP == 0) r = a & b;
        else if constexpr (OP == 1) r = a ^ b;
        else if constexpr (OP == 2) r = a - b;
        else if constexpr (OP == 3) r = b - a;
        else if constexpr (OP == 4) r = a + b;
        else if constexpr (OP == 5) r = a + b + st.cf;
        else if constexpr (OP == 6) r = a - b - (1u - st.cf);
        else if constexpr (OP == 7) r = b - a - (1u - st.cf);
        else if constexpr (OP == 12) r = a | b;
        else if constexpr (OP == 13) r = b;
        else if constexpr (OP == 14) r = a & ~b;
        else r = ~b;
    }
    if constexpr (!cmp) R(c.rd) = r;
    return true;
}

// LDR/LDRB/STR/STRB with an immediate offset, pre-indexed without writeback, base not PC (or a literal load whose address BuildBlock
// resolved). Same contract as FastSingle: false means nothing happened and the generic path takes over.
template <bool L, bool WORD, bool LIT>
bool ArmInterp::Impl::LsImm(const Cop& c, uint32_t aux) {
    const uint32_t address = LIT ? aux : R(c.rn) + c.imm;
    if (WORD && (address & 3u)) return false;
    uint8_t* host = DataLookupFast(address, !L);
    if (host == nullptr) return false;
    if constexpr (L) R(c.rd) = WORD ? le::U32(host) : *host;
    else if constexpr (WORD) le::Put32(host, R(c.rd));
    else *host = static_cast<uint8_t>(R(c.rd));
    return true;
}

// Immediate-offset LDR/STR with writeback or post-indexing (PUSH-like `str rX,[sp,#-4]!`, `ldr rX,[rY],#4`), base not PC.
template <bool L, bool WORD>
bool ArmInterp::Impl::LsImmWb(const Cop& c) {
    const uint32_t base = R(c.rn);
    const uint32_t offset_addr = base + c.imm;
    const uint32_t address = (c.fl & 2u) ? offset_addr : base;
    if (WORD && (address & 3u)) return false;
    uint8_t* host = DataLookupFast(address, !L);
    if (host == nullptr) return false;
    if constexpr (L) {
        const uint32_t v = WORD ? le::U32(host) : *host;
        R(c.rn) = offset_addr;
        R(c.rd) = v;
    } else {
        const uint32_t v = R(c.rd);
        if constexpr (WORD) le::Put32(host, v); else *host = static_cast<uint8_t>(v);
        R(c.rn) = offset_addr;
    }
    return true;
}

// Halfword / signed transfers (LDRH STRH LDRSB LDRSH), immediate or register offset, any P/W form, base not PC. K: 0 LDRH, 1 STRH,
// 2 LDRSB, 3 LDRSH. Mirrors ArmRoutedInstruction::HalfwordTransfer; unaligned halfwords and anything not plain memory return false first.
template <int K, bool REG>
bool ArmInterp::Impl::HwOp(const Cop& c) {
    const uint32_t base = R(c.rn);
    uint32_t offset_addr;
    if constexpr (REG) {
        const uint32_t v = R(c.rm);
        const uint32_t off = c.rs != 0u ? (v << c.rs) : v;
        offset_addr = (c.fl & 1u) ? base + off : base - off;
    } else {
        offset_addr = base + c.imm;
    }
    const uint32_t address = (c.fl & 2u) ? offset_addr : base;
    if (K != 2 && (address & 1u)) return false;
    uint8_t* host = DataLookupFast(address, K == 1);
    if (host == nullptr) return false;
    const bool wback = !(c.fl & 2u) || (c.fl & 4u);
    if constexpr (K == 1) {
        le::Put16(host, static_cast<uint16_t>(R(c.rd)));
        if (wback) R(c.rn) = offset_addr;
    } else {
        uint32_t v;
        if constexpr (K == 0) v = le::U16(host);
        else if constexpr (K == 2) v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(*host)));
        else v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(le::U16(host))));
        if (wback) R(c.rn) = offset_addr;
        R(c.rd) = v;
    }
    return true;
}

void ArmInterp::Impl::DataProcessing(const DecodedInsn& d) {
    uint32_t b, sc = st.cf;
    if (d.place_fn == &PlaceDataProcessing) {
        b = d.immediate;
        if (d.rs != 0u) sc = b >> 31;  // ARM rotated immediate: carry is bit 31 of the result (DDI 0100I A5.1.3)
    } else if (d.place_fn == &PlaceDataProcessingReg) {
        const Shifted s = ShiftBy(d.n, Rd(d, d.rm), d.rs, st.cf);
        b = s.v; sc = s.c;
    } else {
        const Shifted s = ShiftBy(d.n, R(d.rm), R(d.rs) & 0xFFu, st.cf);
        b = s.v; sc = s.c;
    }
    const DpResult r = DataProc(d.op1, Rd(d, d.rn), b, sc, st.cf);
    if (r.writes && d.rd == 15u) {
        if (d.s) {
            R(15) = ArmCpu::ExceptionReturnHelper(&cpu, r.v);  // SUBS pc, lr, #4 etc: CPSR = SPSR
        } else {
            WritePcAlu(d, r.v);
        }
        return;
    }
    if (d.s) {
        SetNZ(r.v);
        st.cf = r.c;
        if (r.arith) st.vf = r.o;
    }
    if (r.writes) R(d.rd) = r.v;
    Next(d);
}

// MUL/MLA/UMULL/UMLAL/SMULL/SMLAL; field layout from ArmMultiplySpaceDecoder (rd=RdHi, rs=RdLo/Ra, rm=Rs, rn=Rm).
void ArmInterp::Impl::Multiply(const DecodedInsn& d) {
    const uint32_t a = R(d.rn), b = R(d.rm);
    if (d.op1 == 0u || d.op1 == 1u) {
        const uint32_t v = a * b + (d.op1 == 1u ? R(d.rs) : 0u);
        R(d.rd) = v;
        if (d.s) SetNZ(v);
    } else if (d.op1 >= 4u && d.op1 <= 7u) {
        uint64_t p = (d.op1 >= 6u) ? static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(a)) * static_cast<int32_t>(b))
                                   : static_cast<uint64_t>(a) * b;
        if (d.op1 & 1u) p += (static_cast<uint64_t>(R(d.rd)) << 32) | R(d.rs);
        R(d.rs) = static_cast<uint32_t>(p);
        R(d.rd) = static_cast<uint32_t>(p >> 32);
        if (d.s) { st.nf = static_cast<uint32_t>(p >> 63); st.zf = p == 0; }
    } else {
        e.Halt(HaltReason::kUnimplemented, "ArmInterp: multiply row %u at 0x%08X", d.op1, d.guest_address);
    }
    Next(d);
}

void ArmInterp::Impl::Branch(const DecodedInsn& d) {
    const uint32_t pc = d.guest_address;
    if (d.place_fn == &PlaceBranch) {
        if (d.l) R(14) = pc + 4u;  // ARM BL only; Thumb B has l == 0
        R(15) = pc + (Thumb() ? 4u : 8u) + static_cast<uint32_t>(d.offset);
    } else if (d.place_fn == &PlaceBx || d.place_fn == &PlaceBlxReg) {
        const uint32_t t = Rd(d, d.rm);
        if (d.place_fn == &PlaceBlxReg) R(14) = Thumb() ? ((pc + 2u) | 1u) : pc + 4u;
        st.cpsr.bits.thumb_mode = t & 1u;
        R(15) = t & ((t & 1u) ? ~1u : ~3u);
    } else if (d.place_fn == &PlaceThumbBlPrefix) {
        R(14) = pc + 4u + static_cast<uint32_t>(d.offset);
        Next(d);
    } else {  // PlaceThumbBlSuffix
        const uint32_t target = R(14) + static_cast<uint32_t>(d.offset);
        R(14) = (pc + 2u) | 1u;
        R(15) = target & ~1u;
    }
}

// MSR/MRS; masks for ARMv4T/v5 cores (no v6): ARM DDI 0100I A4.1.39.
void ArmInterp::Impl::MsrWrite(const DecodedInsn& d, uint32_t value) {
    uint32_t byte_mask = 0;
    if (d.crn & 1u) byte_mask |= 0x000000FFu;
    if (d.crn & 2u) byte_mask |= 0x0000FF00u;
    if (d.crn & 4u) byte_mask |= 0x00FF0000u;
    if (d.crn & 8u) byte_mask |= 0xFF000000u;
    if (cfg.arch_v6) e.Halt(HaltReason::kUnimplemented, "ArmInterp: MSR on a v6+ core not supported");
    if (d.n) {
        const uint32_t mode = st.cpsr.bits.mode;
        if (mode == ArmMode::kUser || mode == ArmMode::kSystem) { Undef(d); return; }
        const uint32_t mask = byte_mask & (cfg.dsp ? 0xF80000FFu : cfg.thumb ? 0xF00000FFu : 0xF00000DFu);
        ArmCpu::WriteSpsrByInstrHelper(&cpu, value, mask);
    } else {
        const bool dsp = cfg.dsp;
        const uint32_t mask_user = byte_mask & (dsp ? 0xF8000000u : 0xF0000000u);
        const uint32_t mask_priv = byte_mask & (dsp ? 0xF80000DFu : 0xF00000DFu);
        const uint32_t trip      = (dsp || cfg.thumb) ? 0x20u : 0u;
        ArmCpu::WriteCpsrByInstrHelper(&cpu, value, mask_user, mask_priv, trip, 0u);
    }
    Next(d);
}

void ArmInterp::Impl::MrsMsr(const DecodedInsn& d) {
    if (d.s == 0u) {  // MRS
        if (d.n) {
            const uint32_t mode = st.cpsr.bits.mode;
            if (mode == ArmMode::kUser || mode == ArmMode::kSystem) { Undef(d); return; }
            R(d.rd) = ArmCpu::ReadSpsrHelper(&cpu);
        } else {
            R(d.rd) = ArmPackCpsr(st);
        }
        Next(d);
        return;
    }
    MsrWrite(d, R(d.rm));
}

// WFI: park PC after the instruction and let Run()'s housekeeping block in ResumeIdle() until an interrupt arrives.
void ArmInterp::Impl::Wfi(const DecodedInsn& d) {
    Next(d);
    chan.EnterIdle();
    stop = true;
}

// MRC/MCR through ArmSystemHost::ArmCoprocessor; action semantics mirror upstream's place_coproc_register_transfer.cpp.
void ArmInterp::Impl::CoprocRegTransfer(const DecodedInsn& d) {
    if (d.cp_num == 10u || d.cp_num == 11u || d.cp_num == 14u) {  // shortcut: no VFP/debug coprocessor modelled
        if (d.cp_num == 14u && cfg.arch_v6) e.Halt(HaltReason::kUnimplemented, "ArmInterp: cp14 access at 0x%08X", d.guest_address);
        Undef(d);
        return;
    }
    ArmCoprocAccess a;
    a.kind = ArmCoprocKind::kRegisterTransfer;
    a.cp_num = d.cp_num; a.read = d.l != 0u; a.opc1 = d.cp_opc; a.crn = d.crn; a.crm = d.crm; a.opc2 = d.cp; a.rt = d.rd;
    const ArmCoprocDecision dec = e.GetSetup().arm_system->ArmCoprocessor(a);
    if (st.cpsr.bits.mode == ArmMode::kUser && !dec.user_allowed && (d.cp_num == 14u || d.cp_num == 15u)) { Undef(d); return; }
    if (dec.gate_mask != 0u && (mmu.State()->coprocessor_access & dec.gate_mask) == 0u) { Undef(d); return; }

    const bool read = a.read;
    if (!read) ++code_gen;  // any CP15 write may change what chained blocks assume (c7 includes I-cache invalidation)
    if (!read && d.crn != 7u) ++epoch;  // c7 is cache maintenance / WFI: no translation effect
    auto set_read = [&](uint32_t v) {
        if (d.rd == 15u) ArmCpu::UpdateNzcvOnlyHelper(&cpu, v);  // MRC to PC sets NZCV
        else R(d.rd) = v;
    };
    const uint32_t wv = read ? 0u : R(d.rd);
    auto halt = [&](const char* why) {
        e.Halt(HaltReason::kUnimplemented, "ArmInterp: cp%u CRn=c%u CRm=c%u op1=%u op2=%u %s %s at 0x%08X", d.cp_num, d.crn, d.crm,
               d.cp_opc, d.cp, read ? "MRC" : "MCR", why, d.guest_address);
    };
    switch (dec.action) {
    case ArmCoprocAction::kUndefined: Undef(d); return;
    case ArmCoprocAction::kUnimplemented: halt("unimplemented"); break;
    case ArmCoprocAction::kNoOp: break;
    case ArmCoprocAction::kConstant: if (read) set_read(dec.constant); break;
    case ArmCoprocAction::kRegister:
        if (read) set_read(*dec.reg); else *dec.reg = wv;
        break;
    case ArmCoprocAction::kRegisterContextSwitch:
        if (read) set_read(*dec.reg);
        else if (*dec.reg != wv) { *dec.reg = wv; ArmTranslationCache::ContextSwitchFlushHelper(&cache); }
        break;
    case ArmCoprocAction::kRegisterFlushTlbs:
        if (read) set_read(*dec.reg);
        else { *dec.reg = wv; ArmTlbFlushAll(&mmu.State()->data_tlb); ArmTlbFlushAll(&mmu.State()->instruction_tlb); }
        break;
    case ArmCoprocAction::kReadCallback: set_read(dec.read(dec.context)); break;
    case ArmCoprocAction::kReadCallbackKeyed: set_read(dec.read_keyed(dec.context, dec.key)); break;
    case ArmCoprocAction::kWriteCallback: dec.write(dec.context, wv); break;
    case ArmCoprocAction::kWriteCallbackKeyed: dec.write_keyed(dec.context, dec.key, wv); break;
    case ArmCoprocAction::kWriteValueSwitch: {
        bool matched = false;
        for (uint32_t i = 0; i < dec.case_count && !matched; ++i) {
            if (dec.cases[i].value != wv) continue;
            matched = true;
            if (dec.cases[i].kind == ArmCoprocCase::kWaitForInterrupt) { Wfi(d); return; }
            if (dec.cases[i].kind == ArmCoprocCase::kHook) dec.hook(dec.context);
        }
        if (!matched) halt("write value not in switch");
        break;
    }
    case ArmCoprocAction::kWaitForInterrupt: Wfi(d); return;
    case ArmCoprocAction::kSystemControl:
        if (read) set_read(mmu.State()->control_register.word);
        else if (mmu.State()->control_register.word != wv) {
            mmu.SetControlRegister(wv);
            sctlr_pending = true;  // shortcut: ARM9 applies it after the 2 in-flight fetches; upstream applies it at block end, so do the same at the next branch
        }
        break;
    case ArmCoprocAction::kCoprocessorAccess:
        if (read) set_read(mmu.State()->coprocessor_access); else mmu.State()->coprocessor_access = wv;
        break;
    case ArmCoprocAction::kContextId:
        if (read) set_read(mmu.State()->contextidr);
        else if (mmu.State()->contextidr != wv) { mmu.State()->contextidr = wv; ArmTranslationCache::ContextSwitchFlushHelper(&cache); }
        break;
    case ArmCoprocAction::kFcsePid:
        if (read) { set_read(mmu.State()->process_id); break; }
        if (mmu.State()->process_id != (wv & 0xFE000000u)) {
            mmu.State()->process_id = wv & 0xFE000000u;
            ArmTranslationCache::ContextSwitchFlushHelper(&cache);
        }
        mmu.RefreshFcseFold();
        break;
    case ArmCoprocAction::kInvalidateTlbAll:
        switch (dec.tlb_target) {
        case ArmTlbTarget::kInstruction: ArmTranslationCache::ItlbInvalidateAllHelper(&cache); break;
        case ArmTlbTarget::kData:        ArmTranslationCache::DtlbInvalidateAllHelper(&cache); break;
        default:                         ArmTranslationCache::UtlbInvalidateAllHelper(&cache); break;
        }
        break;
    case ArmCoprocAction::kInvalidateTlbByMva:
        switch (dec.tlb_target) {
        case ArmTlbTarget::kInstruction: ArmTranslationCache::ItlbInvalidateMvaHelper(wv, &cache); break;
        case ArmTlbTarget::kData:        ArmTranslationCache::DtlbInvalidateMvaHelper(wv, &cache); break;
        default:                         ArmTranslationCache::UtlbInvalidateMvaHelper(wv, &cache); break;
        }
        break;
    case ArmCoprocAction::kInvalidateCode: break;  // nothing cached
    default: halt("action not supported");
    }
    if (!read) {
        auto range = [&](uint32_t addr, uint32_t len) {
            if (jit_ev & kFlushRange) jit_ev |= kFlushAll;  // second range in one go: just flush everything
            jit_ev |= kFlushRange; jit_ev_addr = addr; jit_ev_len = len;
        };
        switch (dec.action) {
        case ArmCoprocAction::kRegisterContextSwitch: case ArmCoprocAction::kRegisterFlushTlbs: case ArmCoprocAction::kSystemControl:
            jit_ev |= kFlushAll | kFlushEverything; break;  // new translation table / MMU regime: every address space is affected
        case ArmCoprocAction::kContextId: jit_ev |= kRevalidate; break;
        case ArmCoprocAction::kFcsePid: break;  // process switch: the Run loop picks the translator for the new id
        case ArmCoprocAction::kInvalidateTlbAll: if (dec.tlb_target != ArmTlbTarget::kData) jit_ev |= kRevalidate; break;
        case ArmCoprocAction::kInvalidateTlbByMva:
            if (dec.tlb_target != ArmTlbTarget::kData) {
                if (jit_ev & kRevalPage) jit_ev |= kRevalidate;  // two pages in one go: just recheck everything
                jit_ev |= kRevalPage; jit_rv_addr = wv & ~0xFFFu;
            }
            break;
        default: break;
        }
        // Instruction-cache invalidation (c7,c5,x) / unified (c7,c7,0): the architectural signal that code changed.
        if (d.cp_num == 15u && d.crn == 7u && (d.crm == 5u || d.crm == 7u)) {
            if (d.crm == 5u && d.cp == 1u) range(wv & ~31u, 32u); else jit_ev |= kFlushAll;
        }
    }
    Next(d);
}

static FILE* TraceFile() {
    static FILE* f = [] { const char* p = getenv("CEWM_TRACE"); return p ? fopen(p, "w") : nullptr; }();
    return f;
}

void ArmInterp::Impl::ExecDecoded(const DecodedInsn& d) {
    const auto f = d.place_fn;
    if (FILE* tf = TraceFile(); tf && (f == &PlaceMRSorMSR || f == &PlaceMSRImmediate || f == &PlaceCoprocRegisterTransfer || f == &PlaceSvc)) {
        static uint64_t n = 0;
        if (n++ < 400000) {  // diagnostics: privileged-instruction trace, identical in interpreter and dynarmic mode
            fprintf(tf, "%08X cpsr=%08X", d.guest_address, ArmPackCpsr(st));
            for (int r = 0; r < 15; ++r) fprintf(tf, " %X", st.gprs[r]);
            fprintf(tf, "\n");
        }
    }
    if (f == &PlaceDataProcessing || f == &PlaceDataProcessingReg || f == &PlaceDataProcessingShiftedReg) DataProcessing(d);
    else if (f == &PlaceSingleDataTransfer || f == &PlaceLoadStoreExtension || f == &PlaceBlockDataTransfer || f == &PlaceLdrex ||
             f == &PlaceStrex) {
        using Outcome = ArmRoutedInstruction::Outcome;
        DecodedInsn& dd = const_cast<DecodedInsn&>(d);  // the executors take a mutable pointer but do not modify it
        Outcome o;
        if (f == &PlaceSingleDataTransfer) { if (FastSingle<true>(d)) return; o = routed.SingleTransfer(&dd); }
        else if (f == &PlaceLoadStoreExtension)  o = d.op1 == 0u ? routed.Swap(&dd) : routed.HalfwordTransfer(&dd);
        else if (f == &PlaceBlockDataTransfer)   o = routed.BlockTransfer(&dd);
        else                                     o = routed.Exclusive(&dd, f == &PlaceStrex);
        if (o == Outcome::kNextInsn) Next(d);
    }
    else if (f == &PlaceBranch || f == &PlaceBx || f == &PlaceBlxReg || f == &PlaceThumbBlPrefix || f == &PlaceThumbBlSuffix) Branch(d);
    else if (f == &PlaceMultiply) Multiply(d);
    else if (f == &PlaceMRSorMSR) MrsMsr(d);
    else if (f == &PlaceMSRImmediate) {
        const uint32_t imm8 = d.immediate & 0xFFu, rot = ((d.immediate >> 8) & 0xFu) * 2u;
        MsrWrite(d, rot ? ((imm8 >> rot) | (imm8 << (32u - rot))) : imm8);
    }
    else if (f == &PlaceCoprocRegisterTransfer) CoprocRegTransfer(d);
    else if (f == &PlaceCoprocDataOperation || f == &PlaceCoprocDataTransfer) Undef(d);  // ARM920T has no coprocessor reachable this way
    else if (f == &PlaceSvc) cpu.RaiseSwiException(d.guest_address);
    else if (f == &PlaceClz) { const uint32_t v = R(d.rm); R(d.rd) = v ? __builtin_clz(v) : 32u; Next(d); }
    else if (f == &PlaceNop) Next(d);
    else if (f == &PlaceWfi) Wfi(d);
    else e.Halt(HaltReason::kUnimplemented, "ArmInterp: place_fn %p at pc 0x%08X (%s) not implemented", reinterpret_cast<void*>(f),
                d.guest_address, Thumb() ? "thumb" : "arm");
}

void ArmInterp::Impl::BuildBlock(Block& b, uint32_t pc, const uint8_t* host, bool thumb) {
    b.va = pc;
    b.thumb = thumb;
    b.n = 0;
    b.pa_page = fc.pa_page;
    const uint32_t page_left = 0x1000u - (pc & 0xFFFu);
    uint32_t off = 0;
    while (b.n < kBlkMax) {
        const uint32_t remain = page_left - off;
        uint32_t raw, len;
        if (thumb) {
            if (remain < 2u) break;
            raw = le::U16(host + off);
            if (thumb32_dec.IsWide(static_cast<uint16_t>(raw))) break;  // wide Thumb-2: left to Step()
            len = 2u;
        } else {
            if (remain < 4u) break;
            raw = le::U32(host + off);
            len = 4u;
        }
        DecodedInsn& d = scratch[b.n];
        std::memset(&d, 0, sizeof d);
        d.guest_address = d.actual_guest_address = pc + off;
        d.length = len;
        bool ok;
        if (thumb) ok = thumb_dec.DecodeThumb(&d, static_cast<uint16_t>(raw));
        else { ArmOpcode op; op.word = raw; ok = arm_dec.DecodeArm(&d, op); }
        if (!ok) break;  // undefined: Step() raises it
        const auto f = d.place_fn;
        uint8_t kind = kOther;
        if (f == &PlaceDataProcessing) kind = kDpImm;
        else if (f == &PlaceDataProcessingReg) kind = kDpReg;
        else if (f == &PlaceDataProcessingShiftedReg) kind = kDpShReg;
        if (kind <= kDpShReg && d.op1 < 16u && (d.op1 >= 8u && d.op1 <= 11u ? d.s != 0u : d.rd != 15u))  // fast form: specialised kind
            kind = static_cast<uint8_t>(kFirstDp + (kind - kDpImm) * 32u + (d.s ? 16u : 0u) + d.op1);
        else if (f == &PlaceSingleDataTransfer) kind = kSingle;
        else if (f == &PlaceBranch) kind = kBranch;
        else if (f == &PlaceMultiply) kind = kMul;
        else if (f == &PlaceBlockDataTransfer) kind = kBlock;
        b.aux[b.n] = 0;
        if (kind == kSingle && !d.unpriv && d.rd != 15u && d.rn != 15u && d.n && !(d.p && !d.w))
            kind = static_cast<uint8_t>(kFirstLs + 7u + (d.l ? 0u : 2u) + (d.s ? 1u : 0u));  // writeback / post-index
        else if (kind == kSingle && !d.unpriv && d.rd != 15u && d.n && d.p && !d.w) {
            const uint32_t byte = d.s ? 1u : 0u;
            if (d.rn != 15u) kind = static_cast<uint8_t>(kFirstLs + (d.l ? 0u : 2u) + byte);
            else if (d.l) { kind = static_cast<uint8_t>(kFirstLs + 4u + byte); b.aux[b.n] = ((d.guest_address + (thumb ? 4u : 8u)) & ~3u) + static_cast<uint32_t>(d.offset); }
        }
        if (f == &PlaceLoadStoreExtension && !d.unpriv && d.rd != 15u && d.rn != 15u) {
            const int k = d.op1 == 1u ? (d.l ? 0 : 1) : (d.op1 == 2u && d.l) ? 2 : (d.op1 == 3u && d.l) ? 3 : -1;
            if (k >= 0) kind = static_cast<uint8_t>(kFirstLs + 11u + k * 2u + (d.n ? 0u : 1u));
        }
        if (f == &PlaceBx && d.rm != 15u) kind = static_cast<uint8_t>(kFirstLs + 19u);
        if (f == &PlaceBranch) {  // B/BL: target resolved here
            kind = static_cast<uint8_t>(kFirstLs + 6u);
            b.aux[b.n] = d.guest_address + (thumb ? 4u : 8u) + static_cast<uint32_t>(d.offset);
        }
        // Anything that can redirect control or change the translation/mode state ends the block.
        const bool terminal = d.r15_modified || d.is_exception_return || d.context_sync || f == &PlaceSvc || f == &PlaceMRSorMSR ||
                              f == &PlaceMSRImmediate || f == &PlaceCoprocRegisterTransfer || f == &PlaceCoprocDataOperation ||
                              f == &PlaceCoprocDataTransfer || f == &PlaceWfi || f == &PlaceBx || f == &PlaceBlxReg;
        std::memcpy(b.code + off, host + off, len);
        b.kind[b.n] = kind;
        b.len[b.n] = static_cast<uint8_t>(len);
        b.meta[b.n] = kind | (d.cond << 8) | (off << 16) | (len << 24);
        ++b.n;
        off += len;
        if (terminal) break;
    }
    b.span = off;
    if (b.n > b.cap) { delete[] b.d; delete[] b.cop; b.cap = (b.n + 1u) & ~1u; b.d = new DecodedInsn[b.cap]; b.cop = new Cop[b.cap]; }
    if (b.n) std::memcpy(b.d, scratch, b.n * sizeof(DecodedInsn));
    for (uint32_t k = 0; k < b.n; ++k) {
        const DecodedInsn& x = scratch[k];
        Cop& c = b.cop[k];
        c.rd = static_cast<uint8_t>(x.rd); c.rn = static_cast<uint8_t>(x.rn); c.rm = static_cast<uint8_t>(x.rm); c.rs = static_cast<uint8_t>(x.rs);
        c.n = static_cast<uint8_t>(x.n);
        c.fl = static_cast<uint8_t>((x.u ? 1u : 0u) | (x.p ? 2u : 0u) | (x.w ? 4u : 0u) | (x.l ? 8u : 0u));
        c.pad0 = c.pad1 = 0;
        c.imm = (b.kind[k] == kDpImm || (b.kind[k] >= kFirstDp && b.kind[k] < kFirstDp + 32u)) ? x.immediate : static_cast<uint32_t>(x.offset);
        c.pcv = x.guest_address + (thumb ? 4u : 8u);
    }
    // Idle spin: ARM block of plain loads/compares ending in a conditional branch to its own start. WM's idle thread waits for an IRQ this way
    // (ldr r3,[flag]; cmp r3,#0; beq .-8); RunBlock turns it into a wait when the loads hit RAM, so it neither burns host CPU nor guest cycles.
    b.idle = false;
    if (!thumb && b.n >= 2 && (b.kind[b.n-1] == kBranch || b.kind[b.n-1] == kFirstLs + 6u) && b.d[b.n-1].cond != 14u && !b.d[b.n-1].l &&
        b.d[b.n-1].guest_address + 8u + (uint32_t)b.d[b.n-1].offset == pc) {
        bool ok = true;
        for (uint32_t k = 0; k + 1 < b.n && ok; ++k) {
            const DecodedInsn& x = b.d[k];
            if (b.kind[k] == kSingle || (b.kind[k] >= kFirstLs && b.kind[k] <= kFirstLs + 5u)) ok = x.l && x.p && !x.w && x.rd != 15u && x.cond == 14u;
            else if (b.kind[k] == kDpImm || b.kind[k] == kDpReg || (b.kind[k] >= kFirstDp && b.kind[k] < kFirstDp + 64u)) ok = x.s && x.op1 >= 8u && x.op1 <= 11u && x.cond == 14u;
            else ok = false;
        }
        b.idle = ok;
    }
}

// Runs one cached block (or one instruction through Step() when the block cannot be built). *executed = instructions retired.
InterpStep ArmInterp::Impl::RunBlock(uint32_t* executed) {
    stop = false;
    RefreshCtx();
    const uint32_t pc = R(15);
    const bool user = st.cpsr.bits.mode == ArmMode::kUser;
    const bool thumb = Thumb();
    Block* b = nullptr;
    if (kRegChk) {  // CEWM_REGCHK=1: rolling hash of the architectural state at every block entry; compare two builds at the same block counts
        static uint64_t h = 1469598103934665603ull, n = 0;
        for (int r = 0; r < 16; ++r) h = (h ^ st.gprs[r]) * 1099511628211ull;
        h = (h ^ (st.nf | st.zf << 1 | st.cf << 2 | st.vf << 3 | (uint64_t)Thumb() << 4)) * 1099511628211ull;
        if (++n % 20000000 == 0) fprintf(stderr, "[REGCHK] blocks=%llu hash=%016llx\n", (unsigned long long)n, (unsigned long long)h);
    }

    // Chained successor of the previous block: skips fetch translation, hashing and the code compare.
    if (last_blk) {
        for (int k = 0; k < 2; ++k) {
            Block* c = last_blk->succ[k];
            if (last_blk->succ_va[k] == pc && c && c->va == pc && c->gen == code_gen && c->user == user && c->thumb == thumb) { b = c; break; }
        }
    }
    if (!b) {
        const uint32_t eff = mmu.State()->effective_control_register.word;
        uint8_t* host;
        if (fc.epoch == epoch && fc.page == (pc >> 12) && fc.user == user && fc.sctlr == eff) {
            host = fc.base + (pc & 0xFFFu);
        } else {
            host = tr.TranslateExecute(pc);
            if (host == nullptr) { last_blk = nullptr; *executed = 1; return Step(); }  // Step() redoes the fetch and raises the abort
            fc = {pc >> 12, host - (pc & 0xFFFu), eff, epoch, user, tr.LastExecPa() >> 12};
        }
        Block& e = blocks[((pc >> 1) * 2654435761u >> 12) & (kBlkEntries - 1)];
        if (e.va != pc || e.thumb != thumb || e.pa_page != fc.pa_page || e.n == 0 || std::memcmp(host, e.code, e.span) != 0)
            { BuildBlock(e, pc, host, thumb); ++g_blk_builds; }
        if (e.n == 0) { last_blk = nullptr; ++g_blk_step; *executed = 1; return Step(); }
        e.gen = code_gen;
        e.user = user;
        b = &e;
        ++g_blk_slow;
        if (last_blk) {  // remember this edge
            const uint32_t k = last_blk->succ_next ^= 1u;
            last_blk->succ[k] = b;
            last_blk->succ_va[k] = pc;
        }
    }

    // Threaded dispatch (GCC labels-as-values): every handler ends with its own indirect jump to the next op's handler, so the branch
    // predictor sees one site per handler instead of one shared switch.
    // R15 is not maintained per op: the fast handlers (Adv = false) leave it alone, `next` tracks the PC, and R15 is written at block end
    // (done_fall) or by whatever redirected control. Anything that goes through the generic executors first gets R15 = its own address,
    // exactly what the previous op's Next() used to leave there.
    uint32_t i = 0;
    bool ram_only = true;
    const DecodedInsn* d = nullptr;
    uint32_t next = 0, m = 0;
    const uint32_t va0 = b->va;
#define CEWM_DP_OPS(X, F, S) X(F, S, 0) X(F, S, 1) X(F, S, 2) X(F, S, 3) X(F, S, 4) X(F, S, 5) X(F, S, 6) X(F, S, 7) \
    X(F, S, 8) X(F, S, 9) X(F, S, 10) X(F, S, 11) X(F, S, 12) X(F, S, 13) X(F, S, 14) X(F, S, 15)
#define CEWM_DP_ALL(X) CEWM_DP_OPS(X, 0, 0) CEWM_DP_OPS(X, 0, 1) CEWM_DP_OPS(X, 1, 0) CEWM_DP_OPS(X, 1, 1) CEWM_DP_OPS(X, 2, 0) CEWM_DP_OPS(X, 2, 1)
#define CEWM_DP_ADDR(F, S, OP) &&h_dp_##F##_##S##_##OP,
    static const void* const kJump[] = {&&h_dpimm, &&h_dpreg, &&h_dpshreg, &&h_single, &&h_branch, &&h_mul, &&h_block, &&h_other,
                                        CEWM_DP_ALL(CEWM_DP_ADDR)
                                        &&h_ldr, &&h_ldrb, &&h_str, &&h_strb, &&h_ldrl, &&h_ldrbl, &&h_bra, &&h_ldrw, &&h_ldrbw, &&h_strw, &&h_strbw,
                                        &&h_lh_i, &&h_lh_r, &&h_sh_i, &&h_sh_r, &&h_lsb_i, &&h_lsb_r, &&h_lsh_i, &&h_lsh_r, &&h_bx};
#undef CEWM_DP_ADDR
#define CEWM_DISPATCH()                                                                                                \
    do {                                                                                                               \
        m = b->meta[i];                                                                                                \
        d = &b->d[i];                                                                                                  \
        next = va0 + ((m >> 16) & 0xFFu) + (m >> 24);                                                                  \
        if (((m >> 8) & 0xFu) != 14u && !CondPasses((m >> 8) & 0xFu, st.nf, st.zf, st.cf, st.vf)) goto cond_failed;    \
        goto *kJump[m & 0xFFu];                                                                                        \
    } while (0)
#define CEWM_FAST_NEXT()                                                                                               \
    do {                                                                                                               \
        if (++i >= b->n) goto done_fall;                                                                               \
        CEWM_DISPATCH();                                                                                               \
    } while (0)
// Generic executor: R15 = this op's address first; afterwards R15 != next means control left the straight line.
#define CEWM_SLOW(call)                                                                                                \
    do {                                                                                                               \
        R(15) = d->guest_address;                                                                                      \
        call;                                                                                                          \
        if (sctlr_pending && R(15) != next) { sctlr_pending = false; ++code_gen; mmu.SynchronizeSctlr(); }             \
        if (stop || R(15) != next) { ++i; goto done; }                                                                 \
        CEWM_FAST_NEXT();                                                                                              \
    } while (0)
    CEWM_DISPATCH();
cond_failed:
    CEWM_FAST_NEXT();
h_dpimm:   CEWM_SLOW(DataProcessing(*d));
h_dpreg:   CEWM_SLOW(DataProcessing(*d));
h_dpshreg: CEWM_SLOW(DataProcessing(*d));
#define CEWM_DP_LABEL(F, S, OP) h_dp_##F##_##S##_##OP: DpOp<F, S, OP, false>(b->cop[i]); CEWM_FAST_NEXT();
CEWM_DP_ALL(CEWM_DP_LABEL)
#undef CEWM_DP_LABEL
h_single:  if (FastSingle<false>(*d)) CEWM_FAST_NEXT(); ram_only = false; CEWM_SLOW(ExecDecoded(*d));
#define CEWM_LS(label, call) label: if (call) CEWM_FAST_NEXT(); ram_only = false; CEWM_SLOW(ExecDecoded(*d));
CEWM_LS(h_ldr,   (LsImm<true,  true,  false>(b->cop[i], 0)))
CEWM_LS(h_ldrb,  (LsImm<true,  false, false>(b->cop[i], 0)))
CEWM_LS(h_str,   (LsImm<false, true,  false>(b->cop[i], 0)))
CEWM_LS(h_strb,  (LsImm<false, false, false>(b->cop[i], 0)))
CEWM_LS(h_ldrw,  (LsImmWb<true,  true >(b->cop[i])))
CEWM_LS(h_ldrbw, (LsImmWb<true,  false>(b->cop[i])))
CEWM_LS(h_strw,  (LsImmWb<false, true >(b->cop[i])))
CEWM_LS(h_strbw, (LsImmWb<false, false>(b->cop[i])))
CEWM_LS(h_ldrl,  (LsImm<true,  true,  true >(b->cop[i], b->aux[i])))
CEWM_LS(h_ldrbl, (LsImm<true,  false, true >(b->cop[i], b->aux[i])))
h_bra:     if (b->cop[i].fl & 8u) R(14) = next;  // ARM BL only; Thumb B has l == 0
           if (b->aux[i] != next) { R(15) = b->aux[i]; ++i; goto done; }  // a branch to the next instruction just falls through
           CEWM_FAST_NEXT();
CEWM_LS(h_lh_i,  (HwOp<0, false>(b->cop[i])))
CEWM_LS(h_lh_r,  (HwOp<0, true >(b->cop[i])))
CEWM_LS(h_sh_i,  (HwOp<1, false>(b->cop[i])))
CEWM_LS(h_sh_r,  (HwOp<1, true >(b->cop[i])))
CEWM_LS(h_lsb_i, (HwOp<2, false>(b->cop[i])))
CEWM_LS(h_lsb_r, (HwOp<2, true >(b->cop[i])))
CEWM_LS(h_lsh_i, (HwOp<3, false>(b->cop[i])))
CEWM_LS(h_lsh_r, (HwOp<3, true >(b->cop[i])))
#undef CEWM_LS
h_bx:      { const uint32_t t = R(b->cop[i].rm);  // same as Branch() for PlaceBx; rm != PC guaranteed by BuildBlock
             st.cpsr.bits.thumb_mode = t & 1u;
             R(15) = t & ((t & 1u) ? ~1u : ~3u); ++i; goto done; }
h_branch:  CEWM_SLOW(Branch(*d));
h_mul:     CEWM_SLOW(Multiply(*d));
h_block:   if (FastBlockTransfer<false>(*d)) CEWM_FAST_NEXT(); CEWM_SLOW(ExecDecoded(*d));
h_other:   CEWM_SLOW(ExecDecoded(*d));
#undef CEWM_SLOW
#undef CEWM_DP_ALL
#undef CEWM_DP_OPS
#undef CEWM_FAST_NEXT
#undef CEWM_DISPATCH
done_fall:
    R(15) = next;
done:
    if (kIdleSkip && b->idle && ram_only && !stop && i == b->n && R(15) == b->va && !st.cpsr.bits.irq_disable) { ++g_idle_hits; chan.EnterIdle(); stop = true; }
    ++g_blk_runs;
    g_blk_ops += i;
    *executed = i;
    last_blk = b;
    return {stop};
}

InterpStep ArmInterp::Impl::Step() {
    stop = false;
    RefreshCtx();
    const uint32_t pc = R(15);
    const bool user = st.cpsr.bits.mode == ArmMode::kUser;
    const uint32_t eff = mmu.State()->effective_control_register.word;

    // Fetch cache: the host pointer of the current code page, valid until a CP15 write (epoch), a user/priv switch or an MMU-control change.
    uint8_t* host;
    if (fc.epoch == epoch && fc.page == (pc >> 12) && fc.user == user && fc.sctlr == eff) {
        host = fc.base + (pc & 0xFFFu);
    } else {
        host = tr.TranslateExecute(pc);
        if (host == nullptr) {
            if (pc < 0x1000u) { static int n = 0; if (n++ < 3) { DYNLOG("[INTERP] fetch fault pc=%08X regs:", pc); for (int r = 0; r < 16; ++r) DYNLOG(" %08X", st.gprs[r]); DYNLOG("\n"); } }
            cpu.RaiseAbortPrefetchException(pc);  // shortcut: assumes the walker recorded FSR/FAR
            return {};
        }
        fc = {pc >> 12, host - (pc & 0xFFFu), eff, epoch, user, tr.LastExecPa() >> 12};
    }

    const bool thumb = Thumb();
    uint32_t raw;
    bool wide = false;
    if (thumb) {
        raw = le::U16(host);
        wide = thumb32_dec.IsWide(static_cast<uint16_t>(raw));
        if (wide) {
            uint8_t* hi = tr.TranslateExecute(pc + 2u);
            if (hi == nullptr) { cpu.RaiseAbortPrefetchException(pc + 2u); return {}; }
            raw = (raw << 16) | le::U16(hi);
        }
    } else {
        raw = le::U32(host);
    }

    // Decode cache: direct-mapped on pc, validated by pc + raw bits + ISA state, so self-modifying code is safe.
    DecEnt& e = dcache[((pc >> 1) ^ (pc >> 15)) & (kDecEntries - 1)];
    if (e.pc != pc || e.raw != raw || e.thumb != thumb) {
        std::memset(&e.d, 0, sizeof e.d);
        e.d.guest_address = e.d.actual_guest_address = pc;
        if (thumb) {
            if (wide) { e.d.length = 4u; e.ok = thumb32_dec.DecodeThumb32(&e.d, raw); }
            else      { e.d.length = 2u; e.ok = thumb_dec.DecodeThumb(&e.d, static_cast<uint16_t>(raw)); }
        } else {
            ArmOpcode op;
            op.word = raw;
            e.d.length = 4u;
            e.ok = arm_dec.DecodeArm(&e.d, op);
        }
        e.pc = pc; e.raw = raw; e.thumb = thumb;
    }
    const DecodedInsn& d = e.d;
    if (!e.ok) { cpu.RaiseUndefinedException(pc); return {}; }
    if (!CondPasses(d.cond, st.nf, st.zf, st.cf, st.vf)) { R(15) = pc + d.length; return {}; }
    ExecDecoded(d);
    if (sctlr_pending && R(15) != pc + d.length) {
        sctlr_pending = false;
        mmu.SynchronizeSctlr();
    }
    return {stop};
}


#ifdef CEWM_HAVE_DYNARMIC
// ---------------------------------------------------------------------------------------------------------------------------------
// Dynarmic core. All memory goes through our MMU (callbacks); anything privileged comes back through InterpreterFallback (the dynarmic fork
// routes undefined/unpredictable/SWI/CP15/MSR-control to Term::Interpret). Aborts use check_halt_on_memory_access for precise faults.
struct DynCore final : Dynarmic::A32::UserCallbacks {
    using Impl = ArmInterp::Impl;
    Impl& I;
    // One translator per FCSE process id: WinCE relocates each process' slot 0 with the ProcessID register instead of flushing, so
    // virtual-address-keyed code caches stay valid per id.
    static constexpr int kPids = 128;
    std::array<std::unique_ptr<Dynarmic::A32::Jit>, kPids> jits;
    Dynarmic::A32::Jit* jit = nullptr;
    int cur_pid = -1;
    // Per translator: the physical page behind every virtual page code was fetched from, so TLB maintenance can drop only blocks whose
    // mapping really changed (instead of flushing on each of the thousands of TLB invalidations a boot issues).
    std::array<std::unordered_map<uint32_t, uint32_t>, kPids> code_map;
    std::array<uint64_t, kPids> pid_epoch{};
    uint64_t mmu_epoch = 1;
    uint64_t revalidations = 0, dropped_pages = 0;

    void Revalidate(int pid) {
        auto& m = code_map[pid];
        std::vector<uint32_t> bad;
        for (auto& kv : m) {
            uint32_t pa = 0;  // probe walk: ignores access permissions, which depend on the mode we happen to be in
            // Unmapped right now is not "changed": the kernel maps a switched-in process' pages lazily. Only a page that is mapped to a
            // *different* physical page holds stale code. (Hole: unmapped -> remapped elsewhere without any TLB maintenance in between.)
            const bool mapped = I.tr.PeekVaToPa(kv.first << 12, &pa);
            static const bool drop_unmapped = getenv("CEWM_DROP_UNMAPPED") != nullptr;
            if ((mapped && (pa >> 12) != kv.second) || (!mapped && drop_unmapped)) bad.push_back(kv.first);
        }
        for (uint32_t vp : bad) { jits[pid]->InvalidateCacheRange(vp << 12, 0x1000u); m.erase(vp); ++dropped_pages; }
        pid_epoch[pid] = mmu_epoch;
        ++revalidations;
    }
    Dynarmic::A32::UserConfig conf;
    ArmRoutedAccess& access;
    int64_t budget = 0;
    bool prefetch_abort = false;
    uint32_t prefetch_pc = 0;
    bool data_abort = false;
    bool sctlr_flush = false;
    uint64_t flushes = 0, fallbacks = 0;
    uint32_t recent[16][2]; uint32_t rn = 0;  // diagnostics: last fallback pcs and words
    // Tiering: boot executes huge amounts of code once, which a compiler cannot amortise. Blocks start out as interpreter stubs
    // (PreCodeReadHook ends translation with an Interpret terminal); a block is compiled only after it has run kHotThreshold times.
    uint16_t kHotThreshold = [] { const char* e = getenv("CEWM_HOT"); return static_cast<uint16_t>(e ? atoi(e) : 12); }();
    static constexpr uint32_t kTierEntries = 1u << 22;
    std::vector<uint16_t> tier_count = std::vector<uint16_t>(kTierEntries, 0);
    std::vector<uint8_t> tier_stub = std::vector<uint8_t>(kTierEntries, 0);
    static uint32_t TierIdx(uint32_t pc) { return (pc >> 1) * 2654435761u >> 10 & (kTierEntries - 1); }
    uint64_t cold_insns = 0;
    std::unordered_map<uint32_t, uint64_t> fb_hist;  // diagnostics
    // CEWM_VERIFY=1: run each instruction on both cores and compare (see VerifyStep).
    struct JEnt { uint8_t* p; uint8_t old[4]; uint8_t nw[4]; uint8_t n; };
    std::vector<JEnt> journal;
    bool journaling = false, slow_seen = false;
    std::vector<uint8_t> code_vpages = std::vector<uint8_t>(1u << 20, 0);  // 4K virtual pages dynarmic has fetched code from
    bool verify = false;
    uint64_t verified = 0, mismatches = 0;

    // cp15 c7,c5,1 (invalidate I-cache line by MVA) is run by the kernel in loops: handled by a direct callback instead of a full interpreter fallback.
    struct Cp15 final : Dynarmic::A32::Coprocessor {
        DynCore* d;
        explicit Cp15(DynCore* c) : d(c) {}
        static uint64_t ICacheLine(void* u, uint32_t mva, uint32_t) {
            DynCore* c = static_cast<DynCore*>(u);
            const uint32_t a = mva & ~31u;
            if (c->code_vpages[a >> 12]) { c->jit->InvalidateCacheRange(a, 32); ++c->flushes; }
            return 0;
        }
        std::optional<Callback> CompileInternalOperation(bool, unsigned, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg, unsigned) override { return std::nullopt; }
        CallbackOrAccessOneWord CompileSendOneWord(bool, unsigned, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg, unsigned) override { return Callback{&ICacheLine, d}; }
        CallbackOrAccessTwoWords CompileSendTwoWords(bool, unsigned, Dynarmic::A32::CoprocReg) override { return std::monostate{}; }
        CallbackOrAccessOneWord CompileGetOneWord(bool, unsigned, Dynarmic::A32::CoprocReg, Dynarmic::A32::CoprocReg, unsigned) override { return std::monostate{}; }
        CallbackOrAccessTwoWords CompileGetTwoWords(bool, unsigned, Dynarmic::A32::CoprocReg) override { return std::monostate{}; }
        std::optional<Callback> CompileLoadWords(bool, bool, Dynarmic::A32::CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
        std::optional<Callback> CompileStoreWords(bool, bool, Dynarmic::A32::CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
    };

    explicit DynCore(Impl& i) : I(i), access(i.e.Get<ArmRoutedAccess>()) {
        Dynarmic::A32::g_pc_store_extra = I.e.GetSetup().arm.pc_store_offset - 8u;
        conf.callbacks = this;
        conf.arch_version = Dynarmic::A32::ArchVersion::v4T;
        conf.check_halt_on_memory_access = true;
        conf.always_little_endian = true;
        conf.code_cache_size = 16u * 1024 * 1024;
        conf.enable_cycle_counting = true;
        if (const char* a = getenv("CEWM_ASYNC")) conf.cewm_async_emit = *a == '1';  // arm64 backend only: emit blocks on a worker thread
        conf.coprocessors[15] = std::make_shared<Cp15>(this);
        SelectPid();
    }

    // Pick (creating on first use) the translator for the current FCSE process id.
    void SelectPid() {
        const int pid = static_cast<int>(I.mmu.State()->process_id >> 25) & (kPids - 1);
        if (pid == cur_pid && jit) return;
        cur_pid = pid;
        if (!jits[pid]) jits[pid] = std::make_unique<Dynarmic::A32::Jit>(conf);
        jit = jits[pid].get();
        if (pid_epoch[pid] != mmu_epoch && !code_map[pid].empty()) Revalidate(pid);  // mappings may have changed while this id was not running
        pid_epoch[pid] = mmu_epoch;
    }

    void ToJit() {
        for (int r = 0; r < 16; ++r) jit->Regs()[r] = I.st.gprs[r];
        jit->SetCpsr(ArmPackCpsr(I.st));
    }
    void FromJit() {
        for (int r = 0; r < 16; ++r) I.st.gprs[r] = jit->Regs()[r];
        ArmApplyCpsr(I.st, jit->Cpsr());
    }

    // Cache events raised by CP15 writes executed through the interpreter.
    void ApplyEvents() {
        if (I.jit_ev == 0) return;
        const uint8_t ev = I.jit_ev;
        I.jit_ev = 0;
        if (ev & Impl::kFlushAll) {
            std::fill(code_vpages.begin(), code_vpages.end(), uint8_t{0});
            if (ev & Impl::kFlushEverything) { for (int p = 0; p < kPids; ++p) { if (jits[p]) jits[p]->ClearCache(); code_map[p].clear(); } }
            else { jit->ClearCache(); code_map[cur_pid].clear(); }
            ++mmu_epoch;
            ++flushes;
            return;
        }
        if (ev & (Impl::kRevalidate | Impl::kRevalPage)) {
            ++mmu_epoch;  // other translators recheck when they next run
            if (ev & Impl::kRevalidate) Revalidate(cur_pid);
            else {
                auto& m = code_map[cur_pid];
                auto it = m.find(I.jit_rv_addr >> 12);
                if (it != m.end()) {
                    uint32_t pa = 0;
                    const bool same = !I.tr.PeekVaToPa(I.jit_rv_addr, &pa) || (pa >> 12) == it->second;
                    if (!same) { jit->InvalidateCacheRange(I.jit_rv_addr, 0x1000u); m.erase(it); ++dropped_pages; }
                }
                pid_epoch[cur_pid] = mmu_epoch;
            }
        }
        if (ev & Impl::kFlushRange) {
            // Cache-maintenance loops walk huge ranges one line at a time; only pages we actually compiled from matter.
            const uint32_t first = I.jit_ev_addr >> 12, last = (I.jit_ev_addr + I.jit_ev_len - 1) >> 12;
            bool hit = false;
            for (uint32_t p = first; p <= last && !hit; ++p) hit = code_vpages[p] != 0;
            if (hit) { jit->InvalidateCacheRange(I.jit_ev_addr, I.jit_ev_len); ++flushes; }
        }
    }

    // ---- code fetch
    std::optional<uint32_t> MemoryReadCode(Dynarmic::A32::VAddr vaddr) override {
        if (I.sctlr_pending) {  // MMU-control write: new regime applies once fetch leaves the window after the MCR (translation reads a whole block ahead)
            if (vaddr - sctlr_pc >= 64u) { I.sctlr_pending = false; I.mmu.SynchronizeSctlr(); }
        }
        uint8_t* h = I.tr.TranslateExecute(vaddr);
        if (h == nullptr) { I.mmu.ClearIoPending(); return std::nullopt; }
        code_vpages[vaddr >> 12] = 1;
        code_map[cur_pid][vaddr >> 12] = I.tr.LastExecPa() >> 12;
        return le::U32(h);
    }
    uint32_t sctlr_pc = 0;

    // ---- data
    template <class T> bool Load(Dynarmic::A32::VAddr a, T* out) {
        constexpr uint32_t n = sizeof(T);
        const uint32_t al = a & ~(n - 1u);
        if (uint8_t* h = I.DataLookup(al, false)) { std::memcpy(out, h + (al & 0xFFFu) - (al & 0xFFFu), n); }
        else {
            uint32_t v = 0;
            slow_seen = true;
            if (!access.Load(jit->Regs()[15], al, n, &v, false)) return false;
            *out = static_cast<T>(v);
        }
        return true;
    }
    template <class T> T Rd(Dynarmic::A32::VAddr a) {
        constexpr uint32_t n = sizeof(T);
        T v = 0;
        if (I.mmu.State()->effective_control_register.bits.a && (a & (n - 1u))) {
            I.mmu.RaiseAlignmentFault(a, false);
            return Fail<T>();
        }
        if constexpr (n == 4) {
            uint32_t w;
            if (!Load<uint32_t>(a, &w)) return Fail<T>();
            const uint32_t rot = (a & 3u) * 8u;  // pre-v6 unaligned LDR rotates the aligned word
            return static_cast<T>(rot ? (w >> rot) | (w << (32u - rot)) : w);
        } else {
            if (!Load<T>(a, &v)) return Fail<T>();
            if constexpr (n == 2) if (a & 1u) v = static_cast<T>((v >> 8) | (v << 8));  // unpredictable on ARM9; rotate like hardware
            return v;
        }
    }
    template <class T> T Fail() { data_abort = true; jit->HaltExecution(Dynarmic::HaltReason::MemoryAbort); return 0; }

    template <class T> void Wr(Dynarmic::A32::VAddr a, T value) {
        constexpr uint32_t n = sizeof(T);
        if (I.mmu.State()->effective_control_register.bits.a && (a & (n - 1u))) {
            I.mmu.RaiseAlignmentFault(a, true);
            Fail<T>();
            return;
        }
        const uint32_t al = a & ~(n - 1u);  // pre-v6 unaligned stores ignore the low address bits
        if (uint8_t* h = I.DataLookup(al, true)) {
            if (journaling) { JEnt je; je.p = h; je.n = n; std::memcpy(je.old, h, n); std::memcpy(je.nw, &value, n); journal.push_back(je); }
            std::memcpy(h, &value, n);
            return;
        }
        slow_seen = true;
        if (!access.Store(jit->Regs()[15], al, n, static_cast<uint32_t>(value), false)) Fail<T>();
    }

    std::uint8_t MemoryRead8(Dynarmic::A32::VAddr a) override { return Rd<std::uint8_t>(a); }
    std::uint16_t MemoryRead16(Dynarmic::A32::VAddr a) override { return Rd<std::uint16_t>(a); }
    std::uint32_t MemoryRead32(Dynarmic::A32::VAddr a) override { return Rd<std::uint32_t>(a); }
    std::uint64_t MemoryRead64(Dynarmic::A32::VAddr a) override {
        const uint64_t lo = Rd<std::uint32_t>(a);
        return lo | (static_cast<uint64_t>(Rd<std::uint32_t>(a + 4)) << 32);
    }
    void MemoryWrite8(Dynarmic::A32::VAddr a, std::uint8_t v) override { Wr(a, v); }
    void MemoryWrite16(Dynarmic::A32::VAddr a, std::uint16_t v) override { Wr(a, v); }
    void MemoryWrite32(Dynarmic::A32::VAddr a, std::uint32_t v) override { Wr(a, v); }
    void MemoryWrite64(Dynarmic::A32::VAddr a, std::uint64_t v) override { Wr<std::uint32_t>(a, static_cast<uint32_t>(v)); Wr<std::uint32_t>(a + 4, static_cast<uint32_t>(v >> 32)); }

    // ---- tiering hook: decide at block start whether to compile or to run this block on the interpreter
    bool PreCodeReadHook(bool, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter& ir) override {
        if (ir.block.Location() != ir.current_location) return true;  // mid-block: the decision was made at block start
        const uint32_t idx = TierIdx(pc);
        if (tier_count[idx] >= kHotThreshold) return true;
        tier_stub[idx] = 1;
        ir.SetTerm(Dynarmic::IR::Term::Interpret(ir.current_location));
        return false;
    }

    // ---- one instruction in the interpreter (privileged / unusual); regs round-trip through ArmCpuState
    void InterpreterFallback(Dynarmic::A32::VAddr pc, size_t n) override {
        ++fallbacks;
        if (kDynLog) {
            ++fb_hist[pc];
            uint8_t* hh = I.tr.TranslateExecute(pc); recent[rn & 15][0] = pc; recent[rn & 15][1] = hh ? le::U32(hh) : 0; ++rn;
        }
        FromJit();
        I.st.gprs[15] = pc;
        const uint32_t idx = TierIdx(pc);
        const bool stub = tier_stub[idx] != 0;
        size_t limit = n;
        if (stub) {  // a cold block: run it (to its end) on the interpreter, and promote it once it keeps coming back
            limit = 64;
            if (tier_count[idx] < 0xFFFFu && ++tier_count[idx] >= kHotThreshold) {
                tier_stub[idx] = 0;
                jit->InvalidateCacheRange(pc, 4);  // drop the stub; the next translation compiles for real
            }
        }
        uint32_t executed = 0;
        for (size_t k = 0; k < limit; ++k) {
            const bool was_pending = I.sctlr_pending;
            const uint32_t prev = I.st.gprs[15];
            const uint32_t seq = I.Thumb() ? 2u : 4u;
            const InterpStep r = I.Step();
            ++executed;
            if (I.sctlr_pending && !was_pending) sctlr_pc = pc;
            if (r.stop_batch) { jit->HaltExecution(Dynarmic::HaltReason::UserDefined1); break; }
            if (I.jit_ev != 0 || I.st.gprs[15] != prev + seq) break;  // branch, exception or a cache event: end of the block
        }
        cold_insns += executed;
        AddTicks(executed);
        ToJit();
        jit->PollAsyncBlocks();
        ApplyEvents();
        if (static_cast<int>(I.mmu.State()->process_id >> 25) != cur_pid) jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    void CallSVC(std::uint32_t) override { I.e.Halt(HaltReason::kUnimplemented, "dyn: CallSVC reached (SWI should be interpreted)"); }
    void ExceptionRaised(Dynarmic::A32::VAddr pc, Dynarmic::A32::Exception ex) override {
        if (ex == Dynarmic::A32::Exception::NoExecuteFault) {
            prefetch_abort = true; prefetch_pc = pc;
            jit->HaltExecution(Dynarmic::HaltReason::UserDefined2);
        }
        // hints (WFE, SEV, YIELD, preloads) are no-ops
    }
    void AddTicks(std::uint64_t t) override { I.st.guest_cycle_counter += static_cast<uint32_t>(t); budget -= static_cast<int64_t>(t); }
    std::uint64_t GetTicksRemaining() override { return budget > 0 ? static_cast<std::uint64_t>(budget) : 0; }

    // Base-register writeback happens before the access in dynarmic's translation; ARM9 restores the base on a data abort, so undo it.
    void UndoWriteback(uint32_t pc) {
        uint8_t* h = I.tr.TranslateExecute(pc);
        if (!h) return;
        DecodedInsn d;
        std::memset(&d, 0, sizeof d);
        d.guest_address = d.actual_guest_address = pc;
        bool ok;
        if (I.Thumb()) { d.length = 2; ok = I.thumb_dec.DecodeThumb(&d, static_cast<uint16_t>(le::U16(h))); }
        else { ArmOpcode op; op.word = le::U32(h); d.length = 4; ok = I.arm_dec.DecodeArm(&d, op); }
        if (!ok) return;
        if (d.place_fn == &PlaceSingleDataTransfer) {
            if (d.p && !d.w) return;
            const uint32_t post = I.R(d.rn);
            DecodedInsn tmp = d;
            // offset applied = new - old; recompute from the instruction with the *new* base: old = new -/+ offset
            const uint32_t off = d.n ? static_cast<uint32_t>(d.offset)
                                     : (d.u ? I.routed.addressing_->SingleShiftedOffset(&tmp) : 0u - I.routed.addressing_->SingleShiftedOffset(&tmp));
            I.R(d.rn) = post - off;
        } else if (d.place_fn == &PlaceLoadStoreExtension && d.op1 != 0u) {
            if (d.p && !d.w) return;
            DecodedInsn tmp = d;
            const uint32_t post = I.R(d.rn);
            const uint32_t off = d.n ? static_cast<uint32_t>(d.offset)
                                     : (d.u ? I.R(d.rm) : 0u - I.R(d.rm));
            (void)tmp;
            I.R(d.rn) = post - off;
        }  // block transfers write the base back after all accesses, so an abort leaves it untouched
    }

    // One instruction on dynarmic (stores journaled), roll back, same instruction on the interpreter, compare registers and stores.
    // Instructions that touched MMIO, aborted, or went through the interpreter fallback are not compared (side effects can't be undone).
    void VerifyStep() {
        const ArmCpuState saved = I.st;
        journal.clear(); journaling = true; slow_seen = false;
        const uint64_t fb0 = fallbacks;
        ToJit();
        const uint32_t pc = I.st.gprs[15];
        const Dynarmic::HaltReason hr = jit->Step();
        FromJit();
        journaling = false;
        const bool comparable = !slow_seen && fallbacks == fb0 && !Dynarmic::Has(hr, Dynarmic::HaltReason::MemoryAbort) && !prefetch_abort && I.jit_ev == 0;
        if (!comparable) {
            if (Dynarmic::Has(hr, Dynarmic::HaltReason::MemoryAbort)) {
                jit->ClearHalt(Dynarmic::HaltReason::MemoryAbort);
                for (auto it = journal.rbegin(); it != journal.rend(); ++it) std::memcpy(it->p, it->old, it->n);
                I.st = saved;  // redo through the interpreter so abort semantics are the reference ones
                I.Step();
            } else if (prefetch_abort) {
                prefetch_abort = false;
                jit->ClearHalt(Dynarmic::HaltReason::UserDefined2);
                I.st = saved;
                I.Step();
            }
            jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
            return;
        }
        const ArmCpuState after = I.st;
        for (auto it = journal.rbegin(); it != journal.rend(); ++it) std::memcpy(it->p, it->old, it->n);
        I.st = saved;
        I.Step();
        ++verified;
        bool bad = false;
        for (int r = 0; r < 16; ++r) if (after.gprs[r] != I.st.gprs[r]) bad = true;
        if (ArmPackCpsr(after) != ArmPackCpsr(I.st)) bad = true;
        for (auto& e : journal) if (std::memcmp(e.p, e.nw, e.n) != 0) bad = true;
        if (bad) {
            ++mismatches;
            if (mismatches <= 12) {
                uint8_t* hh = I.tr.TranslateExecute(pc);
                fprintf(stderr, "[VERIFY] MISMATCH pc=%08X insn=%08X thumb=%d\n", pc, hh ? le::U32(hh) : 0, (int)((saved.cpsr.word >> 5) & 1));
                for (int r = 0; r < 16; ++r)
                    fprintf(stderr, "   r%-2d before=%08X jit=%08X interp=%08X%s\n", r, saved.gprs[r], after.gprs[r], I.st.gprs[r], after.gprs[r] != I.st.gprs[r] ? "  <<<" : "");
                fprintf(stderr, "   cpsr before=%08X jit=%08X interp=%08X\n", ArmPackCpsr(saved), ArmPackCpsr(after), ArmPackCpsr(I.st));
                for (auto& e : journal) {
                    uint32_t nw = 0, cur = 0; std::memcpy(&nw, e.nw, e.n); std::memcpy(&cur, e.p, e.n);
                    if (nw != cur) fprintf(stderr, "   store %p n=%d jit=%08X interp=%08X\n", (void*)e.p, e.n, nw, cur);
                }
            }
        }
        if ((verified & 0xFFFFF) == 0) fprintf(stderr, "[VERIFY] %llu compared, %llu mismatches, pc=%08X\n", (unsigned long long)verified, (unsigned long long)mismatches, I.st.gprs[15]);
        // the interpreter's result is the reference trajectory
    }

    void Run(uint32_t cycles) {
        if (verify) {
            const uint32_t target = I.st.guest_cycle_counter + cycles;
            budget = 1;
            SelectPid();
            while (static_cast<int32_t>(I.st.guest_cycle_counter - target) < 0) {
                VerifyStep();
                if (static_cast<int32_t>(I.st.guest_cycle_counter - target) < 0 && (I.jit_ev != 0 || I.stop)) break;
            }
            return;
        }
        static uint64_t runs = 0;
        if ((runs + 1) % 100000 == 0 && I.st.gprs[15] >= 0xFFFF0000u) {
            uint8_t* hh = I.tr.TranslateExecute(I.st.gprs[15]);
            DYNLOG("[DYN] stuck in vectors: insn=%08X regs:", hh ? le::U32(hh) : 0xDEAD);
            for (int r = 0; r < 16; ++r) DYNLOG(" %08X", I.st.gprs[r]);
            DYNLOG("\n   spsr_abt=%08X mmu_ttbr=%08X fsr/far: see walker\n", I.st.spsr_bank[3].word, 0u);
        }
        if (runs % 20000 == 19999) {
            DYNLOG("[DYN] state pc=%08X words:", I.st.gprs[15]);
            for (int k = -2; k < 6; ++k) { uint8_t* hh = I.tr.TranslateExecute(I.st.gprs[15] + 4 * k); DYNLOG(" %08X", hh ? le::U32(hh) : 0); }
            DYNLOG(" regs:"); for (int r = 0; r < 15; ++r) DYNLOG(" %X", I.st.gprs[r]); DYNLOG("\n");
            std::vector<std::pair<uint64_t, uint32_t>> top;
            for (auto& kv : fb_hist) top.push_back({kv.second, kv.first});
            std::sort(top.rbegin(), top.rend());
            DYNLOG("[DYN] top fallback pcs:");
            for (size_t i = 0; i < top.size() && i < 8; ++i) {
                uint8_t* hh = I.tr.TranslateExecute(top[i].second);
                DYNLOG(" %08X(%08X)x%llu", top[i].second, hh ? le::U32(hh) : 0, (unsigned long long)top[i].first);
            }
            DYNLOG("\n");
        }
        if ((++runs % 20000) == 0)
            DYNLOG("[DYN] runs=%llu pc=%08X cpsr=%08X fallbacks=%llu flushes=%llu reval=%llu dropped=%llu cold_insns=%llu cycles=%u\n", (unsigned long long)runs, I.st.gprs[15],
                    ArmPackCpsr(I.st), (unsigned long long)fallbacks, (unsigned long long)flushes, (unsigned long long)revalidations, (unsigned long long)dropped_pages, (unsigned long long)cold_insns, I.st.guest_cycle_counter);
        budget = cycles;
        SelectPid();
        ToJit();
        const Dynarmic::HaltReason hr = jit->Run();
        FromJit();
        if (Dynarmic::Has(hr, Dynarmic::HaltReason::MemoryAbort)) {
            jit->ClearHalt(Dynarmic::HaltReason::MemoryAbort);
            data_abort = false;
            const uint32_t pc = I.st.gprs[15];
            { static int n = 0; if (n++ < 6) { uint8_t* hh = I.tr.TranslateExecute(pc); DYNLOG("[DYN] data abort pc=%08X insn=%08X lr=%08X sp=%08X r0-3=%08X %08X %08X %08X\n", pc, hh ? le::U32(hh) : 0, I.st.gprs[14], I.st.gprs[13], I.st.gprs[0], I.st.gprs[1], I.st.gprs[2], I.st.gprs[3]); } }
            UndoWriteback(pc);
            I.cpu.RaiseAbortDataException(pc);
        }
        if (prefetch_abort) {
            prefetch_abort = false;
            { static int n = 0; if (n++ < 40 && (prefetch_pc >> 20) != 0xF00u) {
                DYNLOG("[DYN] prefetch abort pc=%08X sctlr_eff=%08X; regs:", prefetch_pc, I.mmu.State()->effective_control_register.word);
                for (int r = 0; r < 16; ++r) DYNLOG(" %08X", I.st.gprs[r]);
                DYNLOG("\n[DYN] recent fallbacks:");
                for (uint32_t k = 0; k < 16; ++k) DYNLOG(" %08X:%08X", recent[(rn + k) & 15][0], recent[(rn + k) & 15][1]);
                DYNLOG("\n"); } }
            jit->ClearHalt(Dynarmic::HaltReason::UserDefined2);
            I.st.gprs[15] = prefetch_pc;
            // A block translated across an unmapped page has the fault baked in; drop it so the retry after paging-in re-translates.
            jit->InvalidateCacheRange(prefetch_pc & ~0xFFFu, 0x1000u);
            I.cpu.RaiseAbortPrefetchException(prefetch_pc);
        }
        jit->ClearHalt(Dynarmic::HaltReason::UserDefined1);
    }
};

#endif  // CEWM_HAVE_DYNARMIC

#ifdef CEWM_HAVE_DYNARMIC
bool ArmInterp::DynEnabled() const { return impl_->dyn_on; }
bool ArmInterp::SctlrPending() const { return impl_->sctlr_pending; }
void ArmInterp::RunDyn(uint32_t budget) { impl_->dyn->Run(budget); }

#else
bool ArmInterp::DynEnabled() const { return false; }
bool ArmInterp::SctlrPending() const { return false; }
void ArmInterp::RunDyn(uint32_t) {}
#endif

ArmInterp::ArmInterp(Engine& engine) : impl_(new Impl(engine)) {
#ifdef CEWM_HAVE_DYNARMIC
    if (const char* j = getenv("CEWM_JIT"); j && *j == '1') {
        impl_->dyn = new DynCore(*impl_);
        impl_->dyn_on = true;
        if (const char* v = getenv("CEWM_VERIFY"); v && *v == '1') impl_->dyn->verify = true;
    }
#endif
}
InterpStep ArmInterp::Step() { return impl_->Step(); }
InterpStep ArmInterp::RunBlock(uint32_t* executed) { return impl_->RunBlock(executed); }

}  // namespace cerfengine
