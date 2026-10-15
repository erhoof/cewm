#include "xplorer.h"

#define ID_START      1
#define ID_SM_RUN     10
#define ID_SM_ABOUT   11
#define ID_SM_TASKMGR 12
#define START_W       56

#define TB_MAX        24
#define ID_TASK_BASE  100        /* task buttons: 100 .. 100+TB_MAX-1 */
#define TB_TIMER      1
#define TB_TIMER_MS   1000

const WCHAR kTaskbarClass[] = L"XplorerTaskbar";

struct TbState {
    HWND win[TB_MAX];   /* the window each task button represents */
    HWND btn[TB_MAX];   /* the task buttons themselves            */
    int  count;
    int  sw, sh;        /* last-seen screen metrics (resolution-change detection) */
};

struct TbEnum {
    HWND win[TB_MAX];
    int  count;
};

static struct TbState* GetTb(HWND h) {
    return (struct TbState*)GetWindowLongW(h, GWL_USERDATA);
}

/* ---- Start menu ---------------------------------------------------------- */

static void ShowStartMenu(HWND h) {
    HINSTANCE hi = GetModuleHandleW(NULL);
    HMENU     m  = CreatePopupMenu();
    RECT      r;
    int       cmd;
    if (!m) return;
    AppendMenuW(m, MF_STRING,    ID_SM_RUN,     L"Run...");
    AppendMenuW(m, MF_STRING,    ID_SM_ABOUT,   L"About");
    AppendMenuW(m, MF_SEPARATOR, 0,             NULL);
    AppendMenuW(m, MF_STRING,    ID_SM_TASKMGR, L"Task Manager");

    GetWindowRect(GetDlgItem(h, ID_START), &r);
    SetForegroundWindow(h);   /* menus need a foreground owner to track/dismiss */
    cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD,
                         r.left, r.top, 0, h, NULL);
    DestroyMenu(m);

    if (cmd == ID_SM_RUN)
        ShowRunDialog(hi);
    else if (cmd == ID_SM_ABOUT) {
        OSVERSIONINFOW vi;
        WCHAR          text[160];
        memset(&vi, 0, sizeof(vi));
        vi.dwOSVersionInfoSize = sizeof(vi);
        GetVersionExW(&vi);
        wsprintfW(text, L"xplorer\nCERF minimal shell\n"
                        L"coredll-only, CE 2.11+\n\n"
                        L"Windows CE %d.%d (build %d)",
                  (int)vi.dwMajorVersion, (int)vi.dwMinorVersion,
                  (int)vi.dwBuildNumber);
        MessageBoxW(h, text, L"About xplorer", MB_OK | MB_ICONINFORMATION);
    }
    else if (cmd == ID_SM_TASKMGR)
        ShowTaskManager(hi);
}

/* ---- Task list ----------------------------------------------------------- */

/* A taskbar-eligible window: visible, titled, unowned, not a tool window. The
   title filter naturally drops our own chrome (desktop/taskbar carry no title)
   while keeping app windows (explorer frames, task manager, ...). */
static BOOL CALLBACK TbEnumProc(HWND w, LPARAM lp) {
    struct TbEnum* e = (struct TbEnum*)lp;
    if (e->count >= TB_MAX) return FALSE;
    if (!IsWindowVisible(w)) return TRUE;
    if (GetWindowTextLengthW(w) == 0) return TRUE;
    if (GetWindow(w, GW_OWNER) != NULL) return TRUE;
    if (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    e->win[e->count++] = w;
    return TRUE;
}

static void LayoutTaskbar(HWND h) {
    struct TbState* s = GetTb(h);
    RECT rc;
    int  ch, x, avail, bw, i;
    GetClientRect(h, &rc);
    ch = rc.bottom;
    MoveWindow(GetDlgItem(h, ID_START), 2, 2, START_W, ch - 4, TRUE);

    x     = 2 + START_W + 4;
    avail = rc.right - x - 2;
    if (s->count > 0 && avail > 0) {
        bw = avail / s->count;
        if (bw > 120) bw = 120;
        if (bw < 24)  bw = 24;
        for (i = 0; i < s->count; i++)
            if (s->btn[i]) MoveWindow(s->btn[i], x + i * bw, 2, bw - 2, ch - 4, TRUE);
    }
}

/* Pin the taskbar to the bottom of the current screen, reserve its strip as
   off-limits to maximized windows (SPI_SETWORKAREA), and follow the desktop to
   the new resolution. Driven both on resolution change and at startup. */
static void ApplyScreenLayout(HWND h) {
    struct TbState* s = GetTb(h);
    int  sw = GetSystemMetrics(SM_CXSCREEN);
    int  sh = GetSystemMetrics(SM_CYSCREEN);
    RECT work;
    HWND desk;

    s->sw = sw;
    s->sh = sh;

    SetWindowPos(h, HWND_TOPMOST, 0, sh - TASKBAR_H, sw, TASKBAR_H,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    work.left = 0; work.top = 0; work.right = sw; work.bottom = sh - TASKBAR_H;
    SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);

    desk = FindWindowW(kDesktopClass, NULL);
    if (desk) MoveWindow(desk, 0, 0, sw, sh - TASKBAR_H, TRUE);

    LayoutTaskbar(h);
}

/* Order-independent set compare: z-order changes (from activating a window) must
   NOT trigger a rebuild, or every click would flicker the buttons. */
static int TbSameSet(struct TbState* s, struct TbEnum* e) {
    int i, j;
    if (s->count != e->count) return 0;
    for (i = 0; i < s->count; i++) {
        int found = 0;
        for (j = 0; j < e->count; j++) if (s->win[i] == e->win[j]) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

static void RebuildTasks(HWND h) {
    struct TbState* s  = GetTb(h);
    HINSTANCE       hi = GetModuleHandleW(NULL);
    struct TbEnum   e;
    int             i;

    e.count = 0;
    EnumWindows(TbEnumProc, (LPARAM)&e);
    if (TbSameSet(s, &e)) return;   /* window set unchanged -> keep buttons as-is */

    for (i = 0; i < s->count; i++)
        if (s->btn[i]) { DestroyWindow(s->btn[i]); s->btn[i] = NULL; }

    s->count = e.count;
    for (i = 0; i < s->count; i++) {
        WCHAR title[64];
        title[0] = 0;
        GetWindowTextW(e.win[i], title, 64);
        s->win[i] = e.win[i];
        s->btn[i] = CreateWindowExW(0, L"BUTTON", title,
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)(ID_TASK_BASE + i), hi, NULL);
    }
    LayoutTaskbar(h);
}

/* Reconcile each button caption with its window's current title (a window can
   rename without the set changing). Writes only on an actual change to avoid a
   needless repaint. */
static void RelabelTasks(HWND h) {
    struct TbState* s = GetTb(h);
    int i;
    for (i = 0; i < s->count; i++) {
        WCHAR cur[64], old[64];
        if (!s->btn[i]) continue;
        cur[0] = 0; GetWindowTextW(s->win[i], cur, 64);
        old[0] = 0; GetWindowTextW(s->btn[i], old, 64);
        if (!XStrEq(cur, old)) SetWindowTextW(s->btn[i], cur);
    }
}

/* ---- Window proc --------------------------------------------------------- */

static LRESULT CALLBACK TaskbarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCTW*)lp)->hInstance;
        struct TbState* s = (struct TbState*)LocalAlloc(LPTR, sizeof(struct TbState));
        if (!s) return -1;
        SetWindowLongW(h, GWL_USERDATA, (LONG)s);
        CreateWindowExW(0, L"BUTTON", L"Start",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)ID_START, hi, NULL);
        s->sw = GetSystemMetrics(SM_CXSCREEN);
        s->sh = GetSystemMetrics(SM_CYSCREEN);
        SetTimer(h, TB_TIMER, TB_TIMER_MS, NULL);
        RebuildTasks(h);
        return 0;
    }

    case WM_SIZE:
        LayoutTaskbar(h);
        return 0;

    case WM_DISPLAYCHANGE:
        ApplyScreenLayout(h);
        return 0;

    case WM_TIMER:
        if (wp == TB_TIMER) {
            struct TbState* s = GetTb(h);
            if (GetSystemMetrics(SM_CXSCREEN) != s->sw ||
                GetSystemMetrics(SM_CYSCREEN) != s->sh)
                ApplyScreenLayout(h);
            RebuildTasks(h);
            RelabelTasks(h);
        }
        return 0;

    case WM_COMMAND: {
        struct TbState* s  = GetTb(h);
        int             id = LOWORD(wp);
        if (id == ID_START) { ShowStartMenu(h); return 0; }
        if (id >= ID_TASK_BASE && id < ID_TASK_BASE + s->count) {
            HWND w = s->win[id - ID_TASK_BASE];
            SetForegroundWindow(w);
            BringWindowToTop(w);
            return 0;
        }
        return 0;
    }

    case WM_DESTROY: {
        struct TbState* s = GetTb(h);
        KillTimer(h, TB_TIMER);
        if (s) LocalFree(s);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL RegisterTaskbarClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = TaskbarProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kTaskbarClass;
    return RegisterClassW(&wc) != 0;
}

HWND CreateTaskbar(HINSTANCE hi) {
    int  sw = GetSystemMetrics(SM_CXSCREEN);
    int  sh = GetSystemMetrics(SM_CYSCREEN);
    HWND tb = CreateWindowExW(WS_EX_TOPMOST, kTaskbarClass, L"",
                              WS_POPUP | WS_VISIBLE,
                              0, sh - TASKBAR_H, sw, TASKBAR_H,
                              NULL, NULL, hi, NULL);
    if (tb) ApplyScreenLayout(tb);   /* topmost-pin + reserve work area for the strip */
    return tb;
}
