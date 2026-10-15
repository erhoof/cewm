#include "cerf_debug_log.h"
#include "cerf_regs_map.h"

#include <windows.h>

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_LOG_MAX_PROC 8
typedef struct { DWORD pid; volatile UCHAR* va; } CerfLogSlot;
static CerfLogSlot s_log_slot[CERF_LOG_MAX_PROC];
static LONG        s_log_slot_next = 0;

static volatile UCHAR* CerfLogTx(void) {
    DWORD pid = GetCurrentProcessId();
    int i;
    for (i = 0; i < CERF_LOG_MAX_PROC; ++i)
        if (s_log_slot[i].pid == pid) return s_log_slot[i].va;
    return NULL;
}

extern "C" void CerfInitLogging(ULONG id) {
    DWORD pid = GetCurrentProcessId();
    volatile UCHAR* va;
    ULONG pa;
    LONG idx;
    int i;
    for (i = 0; i < CERF_LOG_MAX_PROC; ++i)
        if (s_log_slot[i].pid == pid) return;
    pa = g_CerfVirtBase + CerfVirt::kLogChannelOffset + id * CerfVirt::kLogChannelStride;
    va = (volatile UCHAR*)CerfMapRegsPage(pa, CerfVirt::kLogChannelStride);
    if (!va) return;
    idx = InterlockedIncrement(&s_log_slot_next) - 1;
    if (idx < 0 || idx >= CERF_LOG_MAX_PROC) { VirtualFree((LPVOID)va, 0, MEM_RELEASE); return; }
    s_log_slot[idx].va  = va;
    s_log_slot[idx].pid = pid;
}

extern "C" void CerfDebugTx(const char* msg) {
    volatile UCHAR* tx = CerfLogTx();
    const char* p;
    if (!tx || !msg) return;
    for (p = msg; *p; ++p) tx[0] = (UCHAR)*p;
    tx[0] = '\n';
}

extern "C" void CerfDebugFatal(const char* msg) {
    volatile UCHAR* tx = CerfLogTx();
    const char* p;
    if (!tx)
        tx = (volatile UCHAR*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kLogChannelOffset,
            CerfVirt::kLogChannelStride);
    if (tx) {
        if (msg) {
            for (p = msg; *p; ++p) tx[CerfVirt::kLogChannelTxSlot] = (UCHAR)*p;
            tx[CerfVirt::kLogChannelTxSlot] = '\n';
        }
        tx[CerfVirt::kLogChannelFatalSlot] = 1;
    } else {
        *(volatile ULONG*)0 = 0;
    }
    for (;;) {}
}

extern "C" void CerfDebugTxX(const char* msg, DWORD value) {
    volatile UCHAR* tx = CerfLogTx();
    static const char hex[] = "0123456789ABCDEF";
    const char* p;
    int i;
    if (!tx || !msg) return;
    for (p = msg; *p; ++p) tx[0] = (UCHAR)*p;
    tx[0] = ' '; tx[0] = '0'; tx[0] = 'x';
    for (i = 7; i >= 0; --i) tx[0] = hex[(value >> (i * 4)) & 0xF];
    tx[0] = '\n';
}
