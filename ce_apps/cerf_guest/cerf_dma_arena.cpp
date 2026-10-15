#include "cerf_dma_arena.h"
#include "cerf_regs_map.h"
#include "cerf_debug_log.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

typedef struct {
    volatile UCHAR*  arena_va;
    volatile ULONG*  ctl;
    ULONG            base;
    BOOL             base_set;
    ULONG            cursor;
    CRITICAL_SECTION cs;
    int              depth;
} CerfArenaState;

static CerfArenaState s_arena;

static CerfArenaState* Arena(void) { return &s_arena; }

void CerfArenaProcessAttach(void) {
    InitializeCriticalSection(&Arena()->cs);
}

static BOOL CerfArenaMap(CerfArenaState* a) {
    if (!a->arena_va)
        a->arena_va = (volatile UCHAR*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kDmaArenaOffset, CerfVirt::kDmaArenaSize);
    if (!a->ctl)
        a->ctl = (volatile ULONG*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kArenaCtlOffset, CerfVirt::kArenaCtlSize);
    return a->arena_va != 0 && a->ctl != 0;
}

static BOOL CerfArenaEnsure(CerfArenaState* a) {
    DWORD pid;
    ULONG i;
    if (a->base_set) return TRUE;
    if (!CerfArenaMap(a)) return FALSE;
    pid = GetCurrentProcessId();
    a->ctl[CerfVirt::kArenaCtlClaimPid / 4] = (ULONG)pid;
    for (i = 0; i < CerfVirt::kDmaArenaProcMax; ++i) {
        const ULONG off = i * CerfVirt::kDmaPartitionSize + CerfVirt::kDmaPartOwnerPid;
        if (*(volatile ULONG*)(a->arena_va + off) == (ULONG)pid) {
            a->base     = i * CerfVirt::kDmaPartitionSize;
            a->base_set = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CerfArenaEnter(void) {
    CerfArenaState* a = Arena();
    if (!CerfArenaEnsure(a)) return FALSE;
    EnterCriticalSection(&a->cs);
    if (a->depth != 0)
        CERF_FATAL("cerf_guest: DMA arena re-entered - halting");
    a->depth  = 1;
    a->cursor = CerfVirt::kDmaPartHdrSize;
    return TRUE;
}

void CerfArenaLeave(void) {
    CerfArenaState* a = Arena();
    a->depth = 0;
    LeaveCriticalSection(&a->cs);
}

void* CerfArenaAlloc(ULONG bytes, ULONG* out_offset) {
    CerfArenaState* a = Arena();
    const ULONG at = (a->cursor + 3u) & ~3u;
    if (bytes > CerfVirt::kDmaPartitionSize ||
        at > CerfVirt::kDmaPartitionSize - bytes) {
        CERF_LOG_X("cerf_guest: DMA partition exhausted, need", bytes);
        CERF_FATAL("cerf_guest: DMA partition exhausted - halting");
    }
    a->cursor = at + bytes;
    *out_offset = a->base + at;
    return (void*)(a->arena_va + a->base + at);
}
