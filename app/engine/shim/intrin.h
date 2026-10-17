// Stand-in for MSVC <intrin.h> so upstream engine sources build with gcc.
#pragma once
#include <cstdint>
inline unsigned short __popcnt16(unsigned short v) { return (unsigned short)__builtin_popcount(v); }
