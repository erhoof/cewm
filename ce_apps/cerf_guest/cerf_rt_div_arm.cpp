#include <windows.h>

#if defined(ARM)

/* jornada720 coredll.dll __rt_udiv 0x01FCD790 / __rt_sdiv 0x01FCD320: R0 is the
   divisor and R1 the dividend, the quotient returns in R0 and the remainder in
   R1, a zero divisor raises STATUS_INTEGER_DIVIDE_BY_ZERO, and the remainder
   carries the sign of the dividend. */

namespace {

unsigned __int64 PackQuotientRemainder(unsigned int quotient,
                                       unsigned int remainder) {
    return ((unsigned __int64)remainder << 32) | (unsigned __int64)quotient;
}

unsigned __int64 UnsignedDivMod(unsigned int divisor, unsigned int dividend) {
    if (divisor == 0u) {
        RaiseException(STATUS_INTEGER_DIVIDE_BY_ZERO, 0, 0, NULL);
        return 0;
    }
    unsigned int quotient  = 0u;
    unsigned int remainder = 0u;
    for (int i = 31; i >= 0; --i) {
        const unsigned int shifted_out = remainder >> 31;
        remainder = (remainder << 1) | ((dividend >> i) & 1u);
        if (shifted_out != 0u || remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1u << i;
        }
    }
    return PackQuotientRemainder(quotient, remainder);
}

unsigned int AbsToUnsigned(int v) {
    return (v < 0) ? (0u - (unsigned int)v) : (unsigned int)v;
}

}

extern "C" unsigned __int64 __rt_udiv(unsigned int divisor,
                                      unsigned int dividend) {
    return UnsignedDivMod(divisor, dividend);
}

extern "C" __int64 __rt_sdiv(int divisor, int dividend) {
    const unsigned __int64 qr =
        UnsignedDivMod(AbsToUnsigned(divisor), AbsToUnsigned(dividend));
    unsigned int quotient  = (unsigned int)qr;
    unsigned int remainder = (unsigned int)(qr >> 32);
    if ((divisor < 0) != (dividend < 0)) quotient  = 0u - quotient;
    if (dividend < 0)                    remainder = 0u - remainder;
    return (__int64)PackQuotientRemainder(quotient, remainder);
}

#endif
