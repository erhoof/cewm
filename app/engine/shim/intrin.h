// Stand-in for MSVC <intrin.h> so upstream engine sources build with gcc.
#pragma once
#include <cstdint>
inline unsigned short __popcnt16(unsigned short v) { return (unsigned short)__builtin_popcount(v); }
#include <time.h>
// Only the dev-mode rate probe calls this; a nanosecond clock is a fine stand-in for the TSC.
inline unsigned long long __rdtsc() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec; }
