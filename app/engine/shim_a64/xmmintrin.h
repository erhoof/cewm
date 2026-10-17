// aarch64 stand-in for the four MXCSR bits of <xmmintrin.h> that upstream's VFP arithmetic uses (arm_vfp_arith.h): the sticky
// exception flags and the rounding mode, mapped onto <fenv.h>. Only reached by VFP/NEON guests; the ARM920T has neither.
#pragma once
#include <fenv.h>

#define _MM_ROUND_TOWARD_ZERO 0x6000u

static inline unsigned _mm_getcsr(void) {
    const int e = fetestexcept(FE_ALL_EXCEPT);
    unsigned r = 0x1F80u;  // MXCSR reset value: all exceptions masked
    if (e & FE_INVALID)   r |= 0x01u;
    if (e & FE_DIVBYZERO) r |= 0x04u;
    if (e & FE_OVERFLOW)  r |= 0x08u;
    if (e & FE_UNDERFLOW) r |= 0x10u;
    if (e & FE_INEXACT)   r |= 0x20u;
    switch (fegetround()) {
    case FE_DOWNWARD:   r |= 0x2000u; break;
    case FE_UPWARD:     r |= 0x4000u; break;
    case FE_TOWARDZERO: r |= 0x6000u; break;
    default: break;
    }
    return r;
}

static inline void _mm_setcsr(unsigned v) {
    feclearexcept(FE_ALL_EXCEPT);
    switch ((v >> 13) & 3u) {
    case 1: fesetround(FE_DOWNWARD); break;
    case 2: fesetround(FE_UPWARD); break;
    case 3: fesetround(FE_TOWARDZERO); break;
    default: fesetround(FE_TONEAREST); break;
    }
}
