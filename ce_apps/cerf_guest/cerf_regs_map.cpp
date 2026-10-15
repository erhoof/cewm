#include "cerf_regs_map.h"

#include <pkfuncs.h>

void* CerfMapRegsPage(ULONG pa, ULONG size) {
    void* va = VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
    if (!va) return NULL;
    if (!VirtualCopy(va, (LPVOID)(pa >> 8), size,
                     PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        VirtualFree(va, 0, MEM_RELEASE);
        return NULL;
    }
    return va;
}
