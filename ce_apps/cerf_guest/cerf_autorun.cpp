#include <windows.h>

#include "cerf_autorun.h"
#include "cerf_regs_map.h"
#include "cerf_process_spawn.h"
#include "cerf_shell_watch.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"
#include "cerf/peripherals/cerf_virt/cerf_virt_autorun_regs.h"

static void CerfAutorunReadEntry(const volatile ULONG* regs, ULONG idx, WCHAR* out) {
    const volatile ULONG* words =
        regs + (CerfVirt::kArEntries + idx * CerfVirt::kArEntryStride) / 4;
    ULONG i;
    for (i = 0; i < CerfVirt::kArEntryWchars / 2; ++i) {
        ULONG v = words[i];
        out[i * 2]     = (WCHAR)(v & 0xFFFFu);
        out[i * 2 + 1] = (WCHAR)(v >> 16);
    }
    out[CerfVirt::kArEntryWchars] = 0;
}

static DWORD WINAPI CerfAutorunThread(LPVOID) {
    volatile ULONG* regs = (volatile ULONG*)CerfMapRegsPage(
        g_CerfVirtBase + CerfVirt::kAutorunOffset, CerfVirt::kAutorunSize);
    ULONG count, i;
    if (!regs) {
        CERF_LOG("cerf_guest: autorun map FAILED");
        return 0;
    }
    if (regs[CerfVirt::kArPresent / 4] != CerfVirt::kArMagic) {
        CERF_LOG("cerf_guest: autorun list empty");
        return 0;
    }
    count = regs[CerfVirt::kArCount / 4];
    CERF_LOG_X("cerf_guest: autorun entries", count);
    for (i = 0; i < count; ++i) {
        WCHAR cmd[CerfVirt::kArEntryWchars + 1];
        DWORD err = 0;
        BOOL  ok;
        CerfAutorunReadEntry(regs, i, cmd);
        ok = CerfSpawnCommandLine(cmd, &err);
        CERF_LOG_X("cerf_guest: autorun entry", i);
        CERF_LOG_X("cerf_guest: autorun result", ok ? 1u : err);
    }
    return 0;
}

static void CerfAutorunOnShellIsUp(void) {
    HANDLE t = CreateThread(NULL, 0, CerfAutorunThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

extern "C" void CerfStartAutorun(void) {
    static BOOL started = FALSE;
    if (started) return;
    started = TRUE;
    CerfShellWatchRegister(CerfAutorunOnShellIsUp);
}
