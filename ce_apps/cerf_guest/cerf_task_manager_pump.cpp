#include <windows.h>
#include <tlhelp32.h>

#include "cerf_regs_map.h"
#include "cerf_gwes_ready.h"
#include "cerf_toolhelp.h"
#include "cerf_process_spawn.h"
#include "cerf_task_manager_pump.h"

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

#define CERF_TM_CMD_GEN       0x00u
#define CERF_TM_CMD_CODE      0x04u
#define CERF_TM_CMD_PID       0x08u
#define CERF_TM_CMD_RUNLEN    0x0Cu
#define CERF_TM_CMD_RUNTEXT   0x100u

#define CERF_TM_RESP_CMDGEN   0x40u
#define CERF_TM_RESP_STATUS   0x44u
#define CERF_TM_RESP_ERR      0x48u
#define CERF_TM_RESP_COUNT    0x50u
#define CERF_TM_RESP_TOTAL    0x54u
#define CERF_TM_REC_INDEX     0x60u
#define CERF_TM_REC_KICK      0x64u
#define CERF_TM_RESP_KICK     0x80u
#define CERF_TM_REC_DATA      0x200u

#define CERF_TM_OP_LIST         1u
#define CERF_TM_OP_KILL         2u
#define CERF_TM_OP_SWITCHTO     3u
#define CERF_TM_OP_RUN          4u
#define CERF_TM_OP_LISTWINDOWS  5u
#define CERF_TM_OP_SWITCHTOWIN  6u

#define CERF_TM_RUN_MAX         256u
#define CERF_TM_MAX_RECORDS     256u
#define CERF_TM_NAME_WCHARS     64u
#define CERF_TM_WIN_TITLE_WCHARS 64u
#define CERF_TM_WINFLAG_VISIBLE 0x1u

#define CERF_TM_EVENT_RESET  2u
#define CERF_TM_EVENT_SET    3u
#define CERF_TM_TITLE_TIMEOUT_MS 250u

typedef struct CerfTmProcRecord {
    DWORD pid;
    DWORD parent_pid;
    DWORD thread_count;
    LONG  base_priority;
    DWORD mem_base;
    WCHAR name[CERF_TM_NAME_WCHARS];
} CerfTmProcRecord;
typedef char cerf_tm_record_size_check[(sizeof(CerfTmProcRecord) == 148) ? 1 : -1];

typedef struct CerfTmWindowRecord {
    DWORD hwnd;
    DWORD pid;
    DWORD thread_id;
    DWORD flags;
    WCHAR title[CERF_TM_WIN_TITLE_WCHARS];
} CerfTmWindowRecord;
typedef char cerf_tm_winrec_size_check[(sizeof(CerfTmWindowRecord) == 144) ? 1 : -1];

typedef struct {
    volatile ULONG* regs;

    HANDLE        wt_req;
    HANDLE        wt_done;
    volatile HWND wt_hwnd;
    WCHAR         wt_text[CERF_TM_WIN_TITLE_WCHARS];
    volatile LONG wt_busy;

    BOOL  dead;
    BOOL  ready;
    ULONG last_gen;

    volatile LONG busy;
    ULONG         job_gen;
    ULONG         job_code;
    ULONG         job_pid;
} CerfTmState;

static CerfTmState s_tm;

static CerfTmState* Tm(void) { return &s_tm; }

static void CerfTmRespond(CerfTmState* tm, DWORD gen, DWORD status, DWORD err,
                          DWORD count, DWORD total) {
    tm->regs[CERF_TM_RESP_CMDGEN / 4] = gen;
    tm->regs[CERF_TM_RESP_STATUS / 4] = status;
    tm->regs[CERF_TM_RESP_ERR / 4]    = err;
    tm->regs[CERF_TM_RESP_COUNT / 4]  = count;
    tm->regs[CERF_TM_RESP_TOTAL / 4]  = total;
    tm->regs[CERF_TM_RESP_KICK / 4]   = 1;
}

static void CerfTmSendRecord(CerfTmState* tm, const ULONG* words, DWORD count,
                             DWORD index) {
    DWORD i;
    for (i = 0; i < count; ++i)
        tm->regs[(CERF_TM_REC_DATA + i * 4) / 4] = words[i];
    tm->regs[CERF_TM_REC_INDEX / 4] = index;
    tm->regs[CERF_TM_REC_KICK / 4]  = 1;
}

static void CerfTmDoList(CerfTmState* tm, DWORD gen) {
    PROCESSENTRY32 pe;
    CerfTmProcRecord rec;
    HANDLE snap;
    DWORD count = 0, total = 0;
    BOOL ok;

    if (!CerfToolhelpReady()) {
        CerfTmRespond(tm, gen, 0, ERROR_NOT_SUPPORTED, 0, 0);
        return;
    }
    snap = CerfToolhelpSnapshotProcesses();
    if (snap == INVALID_HANDLE_VALUE) {
        CerfTmRespond(tm, gen, 0, GetLastError(), 0, 0);
        return;
    }

    pe.dwSize = sizeof(pe);
    ok = CerfToolhelpProcessFirst(snap, &pe);
    while (ok) {
        total++;
        if (count < CERF_TM_MAX_RECORDS) {
            DWORD i;
            rec.pid           = pe.th32ProcessID;
            rec.parent_pid    = pe.th32ParentProcessID;
            rec.thread_count  = pe.cntThreads;
            rec.base_priority = pe.pcPriClassBase;
            rec.mem_base      = pe.th32MemoryBase;
            for (i = 0; i < CERF_TM_NAME_WCHARS - 1 && pe.szExeFile[i]; ++i)
                rec.name[i] = pe.szExeFile[i];
            for (; i < CERF_TM_NAME_WCHARS; ++i)
                rec.name[i] = 0;
            CerfTmSendRecord(tm, (const ULONG*)&rec, sizeof(rec) / 4, count);
            count++;
        }
        pe.dwSize = sizeof(pe);
        ok = CerfToolhelpProcessNext(snap, &pe);
    }
    CerfToolhelpCloseSnapshot(snap);
    CerfTmRespond(tm, gen, 1, 0, count, total);
}

static void CerfTmDoKill(CerfTmState* tm, DWORD gen, DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    BOOL ok;
    DWORD err;
    if (!h) {
        CerfTmRespond(tm, gen, 0, GetLastError(), 0, 0);
        return;
    }
    ok  = TerminateProcess(h, 0);
    err = ok ? 0 : GetLastError();
    CloseHandle(h);
    CerfTmRespond(tm, gen, ok ? 1u : 0u, err, 0, 0);
}

static void CerfTmDoSwitchTo(CerfTmState* tm, DWORD gen, DWORD pid) {
    HWND visible = NULL, any = NULL, target;
    DWORD seen = 0;
    HWND w = GetForegroundWindow();
    CERF_LOG_X("cerf_guest: tmpump switch target pid", pid);
    CERF_LOG_X("cerf_guest: tmpump switch fg hwnd", (DWORD)w);
    if (w) w = GetWindow(w, GW_HWNDFIRST);
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        DWORD wpid = 0;
        GetWindowThreadProcessId(w, &wpid);
        seen++;
        if (wpid != pid) continue;
        if (!any) any = w;
        if (IsWindowVisible(w)) {
            visible = w;
            break;
        }
    }
    CERF_LOG_X("cerf_guest: tmpump switch walk seen", seen);
    target = visible ? visible : any;
    if (!target) {
        CerfTmRespond(tm, gen, 0, ERROR_NOT_FOUND, 0, 0);
        return;
    }
    SetForegroundWindow(target);
    CerfTmRespond(tm, gen, 1, 0, 0, 0);
}

static DWORD WINAPI CerfTmTitleWorker(LPVOID param) {
    CerfTmState* tm = (CerfTmState*)param;
    for (;;) {
        HWND  h;
        WCHAR local[CERF_TM_WIN_TITLE_WCHARS];
        DWORD i;
        WaitForSingleObject(tm->wt_req, INFINITE);
        h = tm->wt_hwnd;
        for (i = 0; i < CERF_TM_WIN_TITLE_WCHARS; ++i) local[i] = 0;
        GetWindowTextW(h, local, CERF_TM_WIN_TITLE_WCHARS);
        for (i = 0; i < CERF_TM_WIN_TITLE_WCHARS; ++i) tm->wt_text[i] = local[i];
        EventModify(tm->wt_done, CERF_TM_EVENT_SET);
    }
}

static void CerfTmStartTitleWorker(CerfTmState* tm) {
    HANDLE t;
    tm->wt_req  = CreateEventW(NULL, FALSE, FALSE, NULL);
    tm->wt_done = CreateEventW(NULL, TRUE,  FALSE, NULL);
    if (!tm->wt_req || !tm->wt_done) { tm->wt_req = NULL; tm->wt_done = NULL; return; }
    t = CreateThread(NULL, 0, CerfTmTitleWorker, tm, 0, NULL);
    if (t) CloseHandle(t);
}

static void CerfTmFetchTitle(CerfTmState* tm, HWND h, WCHAR* out, DWORD cch) {
    DWORD i;
    if (cch) out[0] = 0;
    if (!tm->wt_req || !tm->wt_done) return;
    if (tm->wt_busy) {
        if (WaitForSingleObject(tm->wt_done, 0) != WAIT_OBJECT_0) return;
        tm->wt_busy = 0;
    }
    tm->wt_hwnd = h;
    tm->wt_busy = 1;
    EventModify(tm->wt_done, CERF_TM_EVENT_RESET);
    EventModify(tm->wt_req,  CERF_TM_EVENT_SET);
    if (WaitForSingleObject(tm->wt_done, CERF_TM_TITLE_TIMEOUT_MS) == WAIT_OBJECT_0) {
        for (i = 0; i + 1 < cch && i < CERF_TM_WIN_TITLE_WCHARS; ++i) out[i] = tm->wt_text[i];
        out[i] = 0;
        tm->wt_busy = 0;
    }
}

static void CerfTmDoListWindows(CerfTmState* tm, DWORD gen) {
    CerfTmWindowRecord rec;
    HWND  w = GetForegroundWindow();
    DWORD count = 0, total = 0;
    if (w) w = GetWindow(w, GW_HWNDFIRST);
    for (; w; w = GetWindow(w, GW_HWNDNEXT)) {
        DWORD pid = 0, tid, i;
        total++;
        if (count >= CERF_TM_MAX_RECORDS) continue;
        tid           = GetWindowThreadProcessId(w, &pid);
        rec.hwnd      = (DWORD)w;
        rec.pid       = pid;
        rec.thread_id = tid;
        rec.flags     = IsWindowVisible(w) ? CERF_TM_WINFLAG_VISIBLE : 0;
        for (i = 0; i < CERF_TM_WIN_TITLE_WCHARS; ++i) rec.title[i] = 0;
        CerfTmFetchTitle(tm, w, rec.title, CERF_TM_WIN_TITLE_WCHARS);
        CerfTmSendRecord(tm, (const ULONG*)&rec, sizeof(rec) / 4, count);
        count++;
    }
    CerfTmRespond(tm, gen, 1, 0, count, total);
}

static void CerfTmDoSwitchToWin(CerfTmState* tm, DWORD gen, DWORD hwnd) {
    HWND w = (HWND)hwnd;
    if (!w || !IsWindow(w)) {
        CerfTmRespond(tm, gen, 0, ERROR_NOT_FOUND, 0, 0);
        return;
    }
    SetForegroundWindow(w);
    CerfTmRespond(tm, gen, 1, 0, 0, 0);
}

static void CerfTmDoRun(CerfTmState* tm, DWORD gen) {
    WCHAR cmd[CERF_TM_RUN_MAX + 1];
    BOOL ok;
    DWORD err = 0;
    DWORD len = tm->regs[CERF_TM_CMD_RUNLEN / 4];
    DWORD i;

    if (len == 0 || len > CERF_TM_RUN_MAX) {
        CerfTmRespond(tm, gen, 0, ERROR_INVALID_PARAMETER, 0, 0);
        return;
    }
    for (i = 0; i < (len + 1) / 2; ++i) {
        ULONG v = tm->regs[(CERF_TM_CMD_RUNTEXT + i * 4) / 4];
        cmd[i * 2] = (WCHAR)(v & 0xFFFFu);
        if (i * 2 + 1 < len) cmd[i * 2 + 1] = (WCHAR)(v >> 16);
    }
    cmd[len] = 0;

    ok = CerfSpawnCommandLine(cmd, &err);
    CERF_LOG_X("cerf_guest: tmpump run result", ok ? 1u : err);
    CerfTmRespond(tm, gen, ok ? 1u : 0u, err, 0, 0);
}

static BOOL CerfTmRequireGwes(CerfTmState* tm, DWORD gen) {
    if (CerfGwesApiSetReady()) return TRUE;
    CERF_LOG("cerf_guest: tmpump window op rejected - gwes api set not registered yet");
    CerfTmRespond(tm, gen, 0, ERROR_NOT_READY, 0, 0);
    return FALSE;
}

static void CerfTmExecute(CerfTmState* tm, ULONG gen, ULONG code, ULONG pid) {
    switch (code) {
        case CERF_TM_OP_LIST:        CerfTmDoList(tm, gen);            break;
        case CERF_TM_OP_KILL:        CerfTmDoKill(tm, gen, pid);       break;
        case CERF_TM_OP_SWITCHTO:
            if (CerfTmRequireGwes(tm, gen)) CerfTmDoSwitchTo(tm, gen, pid);
            break;
        case CERF_TM_OP_RUN:         CerfTmDoRun(tm, gen);             break;
        case CERF_TM_OP_LISTWINDOWS:
            if (CerfTmRequireGwes(tm, gen)) CerfTmDoListWindows(tm, gen);
            break;
        case CERF_TM_OP_SWITCHTOWIN:
            if (CerfTmRequireGwes(tm, gen)) CerfTmDoSwitchToWin(tm, gen, pid);
            break;
        default:
            CERF_LOG_X("cerf_guest: tmpump unknown cmd", code);
            CerfTmRespond(tm, gen, 0, ERROR_INVALID_PARAMETER, 0, 0);
            break;
    }
}

static DWORD WINAPI CerfTmCommandWorker(LPVOID param) {
    CerfTmState* tm = (CerfTmState*)param;
    CerfTmExecute(tm, tm->job_gen, tm->job_code, tm->job_pid);
    tm->busy = 0;
    return 0;
}

extern "C" void CerfTaskManagerTick(void) {
    CerfTmState* tm = Tm();
    ULONG  gen;
    HANDLE t;

    if (tm->dead) return;

    if (!tm->ready) {
        tm->regs = (volatile ULONG*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kTaskManagerOffset,
                                                    CerfVirt::kTaskManagerSize);
        if (!tm->regs) {
            CERF_LOG("cerf_guest: tmpump map FAILED");
            tm->dead = TRUE;
            return;
        }
        CerfTmStartTitleWorker(tm);
        tm->last_gen = tm->regs[CERF_TM_CMD_GEN / 4];
        tm->ready = TRUE;
        return;
    }

    if (tm->busy) return;

    gen = tm->regs[CERF_TM_CMD_GEN / 4];
    if (gen == tm->last_gen) return;

    tm->job_gen  = gen;
    tm->job_code = tm->regs[CERF_TM_CMD_CODE / 4];
    tm->job_pid  = tm->regs[CERF_TM_CMD_PID / 4];
    tm->last_gen = gen;

    tm->busy = 1;
    t = CreateThread(NULL, 0, CerfTmCommandWorker, tm, 0, NULL);
    if (t) {
        CloseHandle(t);
        return;
    }

    tm->busy = 0;
    CERF_LOG_X("cerf_guest: tmpump command thread FAILED", tm->job_code);
    CerfTmRespond(tm, gen, 0, GetLastError(), 0, 0);
}
