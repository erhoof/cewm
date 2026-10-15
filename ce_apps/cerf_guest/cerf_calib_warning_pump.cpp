#include <windows.h>

#include "cerf_regs_map.h"
#include "cerf_gwes_ready.h"
#include "cerf_window_owner.h"
#include "cerf_calib_warning_pump.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_CW_EVENT         0x00u
#define CERF_CW_APPEARED      1u
#define CERF_CW_DISAPPEARED   2u

#define CERF_CW_IDLE_POLLS 600u

typedef struct {
    volatile ULONG* regs;
    BOOL            dead;
    BOOL            ready;
    BOOL            present;
    DWORD           polls;
    int             miss;
} CerfCwState;

static CerfCwState s_cw;

static CerfCwState* Cw(void) { return &s_cw; }

/* iPAQ H3600 PPC2000 gwes.exe sub_1EC14 builds the calibration overlay via
   sub_1C360: class L"static", style 0x90000000, w dword_9459C, h dword_945EC;
   sub_1C360 stamps window ownership from GetCallerProcess(). */
static HWND CerfCwFindCalibWindow(void) {
    int  scrW = GetSystemMetrics(SM_CXSCREEN);
    int  scrH = GetSystemMetrics(SM_CYSCREEN);
    HWND w = GetForegroundWindow();
    if (w) w = GetWindow(w, GW_HWNDFIRST);
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        LONG  style;
        RECT  rc;
        WCHAR cls[16];
        style = GetWindowLongW(w, GWL_STYLE);
        if (!(style & WS_VISIBLE)) continue;
        if (!(style & WS_POPUP))   continue;
        if (style & WS_CHILD)      continue;
        if (!GetWindowRect(w, &rc)) continue;
        if (!(rc.left <= 0 && rc.top <= 0 && rc.right >= scrW && rc.bottom >= scrH)) continue;
        cls[0] = 0;
        GetClassNameW(w, cls, 16);
        if (lstrcmpiW(cls, L"static") != 0) continue;
        if (!CerfWindowOwnerIs(w, L"welcome.exe")) continue;
        return w;
    }
    return NULL;
}

static void CerfCwSignal(CerfCwState* cw, ULONG event) {
    cw->regs[CERF_CW_EVENT / 4] = event;
}

extern "C" void CerfCalibWarningTick(void) {
    CerfCwState* cw = Cw();
    HWND cal;

    if (cw->dead) return;

    if (!cw->ready) {
        CERF_LOG_X("cerf_guest: cwpump SH_WMGR", CerfShWmgrApiSet());
        cw->regs = (volatile ULONG*)CerfMapRegsPage(
            g_CerfVirtBase + CerfVirt::kCalibSignalOffset,
            CerfVirt::kCalibSignalSize);
        if (!cw->regs) {
            CERF_LOG("cerf_guest: cwpump map FAILED");
            cw->dead = TRUE;
            return;
        }
        if (!CerfIsApiReadyAvailable()) {
            CERF_LOG("cerf_guest: cwpump coredll has no IsAPIReady - teardown");
            cw->dead = TRUE;
            return;
        }
        cw->ready = TRUE;
        return;
    }

    if (!CerfGwesApiSetReady()) {
        if (++cw->polls > CERF_CW_IDLE_POLLS) {
            CERF_LOG("cerf_guest: cwpump wmgr never ready - teardown");
            cw->dead = TRUE;
        }
        return;
    }

    cal = CerfCwFindCalibWindow();
    if (cal) {
        cw->miss = 0;
        if (!cw->present) {
            cw->present = TRUE;
            CERF_LOG("cerf_guest: cwpump CALIB APPEARED");
            CerfCwSignal(cw, CERF_CW_APPEARED);
        }
    } else if (cw->present) {
        if (++cw->miss >= 2) {
            cw->present = FALSE;
            cw->miss = 0;
            CERF_LOG("cerf_guest: cwpump CALIB DISAPPEARED");
            CerfCwSignal(cw, CERF_CW_DISAPPEARED);
            CERF_LOG("cerf_guest: cwpump cycle complete - teardown");
            cw->dead = TRUE;
            return;
        }
    }

    if (!cw->present) {
        if (++cw->polls > CERF_CW_IDLE_POLLS) {
            CERF_LOG("cerf_guest: cwpump idle polls exhausted - teardown");
            cw->dead = TRUE;
        }
    }
}
