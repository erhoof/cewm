#include "cerf_ndis_slot.h"

static CerfMpSlot s_mp;

extern "C" CerfMpSlot* CerfMpFind(void) {
    return s_mp.pid ? &s_mp : 0;
}

extern "C" CerfMpSlot* CerfMpSelf(void) {
    s_mp.pid = GetCurrentProcessId();
    return &s_mp;
}
