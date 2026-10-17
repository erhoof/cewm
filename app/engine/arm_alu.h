// Pure ARM ALU helpers for the interpreter (no engine dependencies, so they are unit-testable).
// Shift semantics: ARM DDI 0100I A5.1.3-A5.1.13; carry/overflow: A4.1.3 ADD, A4.1.2 ADC.
#pragma once
#include <cstdint>

namespace cewm::alu {

struct Shifted { uint32_t v; uint32_t c; };
struct Sum     { uint32_t v; uint32_t c; uint32_t o; };

enum ShiftType : uint32_t { kLsl = 0, kLsr = 1, kAsr = 2, kRor = 3, kRrx = 4 };

// Shift `val` by `amt` (0..255, the register-shift range; immediate shifts are a subset) with carry-in `cin`.
inline Shifted ShiftBy(uint32_t type, uint32_t val, uint32_t amt, uint32_t cin) {
    if (type == kRrx) return {(cin << 31) | (val >> 1), val & 1u};
    if (amt == 0) return {val, cin};
    switch (type) {
    case kLsl:
        if (amt < 32) return {val << amt, (val >> (32 - amt)) & 1u};
        return {0, amt == 32 ? (val & 1u) : 0u};
    case kLsr:
        if (amt < 32) return {val >> amt, (val >> (amt - 1)) & 1u};
        return {0, amt == 32 ? (val >> 31) : 0u};
    case kAsr:
        if (amt < 32) return {static_cast<uint32_t>(static_cast<int32_t>(val) >> amt), (val >> (amt - 1)) & 1u};
        return {static_cast<uint32_t>(static_cast<int32_t>(val) >> 31), val >> 31};
    default: {
        amt &= 31;
        if (amt == 0) return {val, val >> 31};
        const uint32_t r = (val >> amt) | (val << (32 - amt));
        return {r, r >> 31};
    }
    }
}

inline Sum AddWC(uint32_t a, uint32_t b, uint32_t c) {
    const uint64_t w = static_cast<uint64_t>(a) + b + c;
    const uint32_t r = static_cast<uint32_t>(w);
    return {r, static_cast<uint32_t>(w >> 32), ((a ^ r) & (b ^ r)) >> 31};
}

inline bool CondPasses(uint32_t cond, uint32_t n, uint32_t z, uint32_t c, uint32_t v) {
    switch (cond) {
    case 0x0: return z;
    case 0x1: return !z;
    case 0x2: return c;
    case 0x3: return !c;
    case 0x4: return n;
    case 0x5: return !n;
    case 0x6: return v;
    case 0x7: return !v;
    case 0x8: return c && !z;
    case 0x9: return !c || z;
    case 0xA: return n == v;
    case 0xB: return n != v;
    case 0xC: return !z && n == v;
    case 0xD: return z || n != v;
    default:  return true;
    }
}

struct DpResult { uint32_t v; uint32_t c, o; bool arith; bool writes; };

// Data-processing opcode (insn[24:21]) on already-shifted operand; `sc` is the shifter carry, `cf` the current C.
inline DpResult DataProc(uint32_t op, uint32_t a, uint32_t b, uint32_t sc, uint32_t cf) {
    switch (op) {
    case 0x0: return {a & b, sc, 0, false, true};
    case 0x1: return {a ^ b, sc, 0, false, true};
    case 0x2: { auto s = AddWC(a, ~b, 1); return {s.v, s.c, s.o, true, true}; }
    case 0x3: { auto s = AddWC(b, ~a, 1); return {s.v, s.c, s.o, true, true}; }
    case 0x4: { auto s = AddWC(a, b, 0); return {s.v, s.c, s.o, true, true}; }
    case 0x5: { auto s = AddWC(a, b, cf); return {s.v, s.c, s.o, true, true}; }
    case 0x6: { auto s = AddWC(a, ~b, cf); return {s.v, s.c, s.o, true, true}; }
    case 0x7: { auto s = AddWC(b, ~a, cf); return {s.v, s.c, s.o, true, true}; }
    case 0x8: return {a & b, sc, 0, false, false};
    case 0x9: return {a ^ b, sc, 0, false, false};
    case 0xA: { auto s = AddWC(a, ~b, 1); return {s.v, s.c, s.o, true, false}; }
    case 0xB: { auto s = AddWC(a, b, 0); return {s.v, s.c, s.o, true, false}; }
    case 0xC: return {a | b, sc, 0, false, true};
    case 0xD: return {b, sc, 0, false, true};
    case 0xE: return {a & ~b, sc, 0, false, true};
    default:  return {~b, sc, 0, false, true};
    }
}

}  // namespace cewm::alu
