#include "xplorer.h"

#define TM_MAX        64
#define TM_ROW_H      24
#define TM_KILL_W     44
#define TM_HIDE_W     44
#define ID_KILL_BASE  2000
#define ID_HIDE_BASE  3000

const WCHAR kTaskMgrClass[] = L"XplorerTaskMgr";

struct TmRow {
    HWND   hwnd;
    DWORD  pid;
    HANDLE hproc;
    WCHAR  title[64];
};

struct TmState {
    HINSTANCE    hi;
    int          count;
    int          scroll_y;
    struct TmRow rows[TM_MAX];
    HWND         kill[TM_MAX];
    HWND         hide[TM_MAX];
};

static struct TmState* GetTm(HWND h) {
    return (struct TmState*)GetWindowLongW(h, GWL_USERDATA);
}

static BOOL CALLBACK TmEnumProc(HWND w, LPARAM lp) {
    struct TmState* s   = (struct TmState*)lp;
    DWORD           pid = 0;
    int             i;

    if (s->count >= TM_MAX) return FALSE;
    if (XIsOurWindow(w)) return TRUE;               /* skip xplorer's own windows */
    GetWindowThreadProcessId(w, &pid);
    for (i = 0; i < s->count; i++)
        if (s->rows[i].pid == pid) return TRUE;     /* one row per PID */

    s->rows[s->count].hwnd     = w;
    s->rows[s->count].pid      = pid;
    s->rows[s->count].hproc    = NULL;
    s->rows[s->count].title[0] = 0;
    GetWindowTextW(w, s->rows[s->count].title, 64);
    s->count++;
    return TRUE;
}

/* Reposition the per-row Hide + Kill buttons for the current client width. */
static void TmLayoutButtons(HWND h) {
    struct TmState* s = GetTm(h);
    RECT rc;
    int  i, kill_x, hide_x;
    GetClientRect(h, &rc);
    kill_x = rc.right - TM_KILL_W - 4;
    hide_x = kill_x - TM_HIDE_W - 4;
    for (i = 0; i < s->count; i++) {
        int y = i * TM_ROW_H + 2 - s->scroll_y;
        if (s->hide[i]) MoveWindow(s->hide[i], hide_x, y, TM_HIDE_W, TM_ROW_H - 4, TRUE);
        if (s->kill[i]) MoveWindow(s->kill[i], kill_x, y, TM_KILL_W, TM_ROW_H - 4, TRUE);
    }
}

/* Publish the vertical scroll range for the current row count + client height. */
static void TmUpdateScroll(HWND h) {
    struct TmState* s = GetTm(h);
    RECT       rc;
    SCROLLINFO si;
    int        content;
    GetClientRect(h, &rc);
    content = s->count * TM_ROW_H;
    if (s->scroll_y > content - rc.bottom) s->scroll_y = content - rc.bottom;
    if (s->scroll_y < 0) s->scroll_y = 0;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = content > 0 ? content - 1 : 0;
    si.nPage  = rc.bottom;
    si.nPos   = s->scroll_y;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void TmVScroll(HWND h, WPARAM wp) {
    struct TmState* s = GetTm(h);
    RECT       rc;
    SCROLLINFO si;
    int        old = s->scroll_y, content;
    GetClientRect(h, &rc);
    content = s->count * TM_ROW_H;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(h, SB_VERT, &si);
    switch (LOWORD(wp)) {
    case SB_LINEUP:        s->scroll_y -= TM_ROW_H;    break;
    case SB_LINEDOWN:      s->scroll_y += TM_ROW_H;    break;
    case SB_PAGEUP:        s->scroll_y -= rc.bottom;   break;
    case SB_PAGEDOWN:      s->scroll_y += rc.bottom;   break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: s->scroll_y = si.nTrackPos; break;
    default: break;
    }
    if (s->scroll_y > content - rc.bottom) s->scroll_y = content - rc.bottom;
    if (s->scroll_y < 0) s->scroll_y = 0;
    if (s->scroll_y != old) {
        SetScrollPos(h, SB_VERT, s->scroll_y, TRUE);
        TmLayoutButtons(h);
        InvalidateRect(h, NULL, TRUE);
    }
}

static void TmClear(struct TmState* s) {
    int i;
    for (i = 0; i < s->count; i++) {
        if (s->rows[i].hproc) { CloseHandle(s->rows[i].hproc); s->rows[i].hproc = NULL; }
        if (s->kill[i])       { DestroyWindow(s->kill[i]);     s->kill[i]       = NULL; }
        if (s->hide[i])       { DestroyWindow(s->hide[i]);     s->hide[i]       = NULL; }
    }
    s->count = 0;
}

static void TmRebuild(HWND h) {
    struct TmState* s = GetTm(h);
    int i;
    TmClear(s);
    /* Zune ships no toolhelp.dll; EnumWindows is the only process-visibility path. */
    EnumWindows(TmEnumProc, (LPARAM)s);
    for (i = 0; i < s->count; i++) {
        s->rows[i].hproc = OpenProcess(PROCESS_TERMINATE, FALSE, s->rows[i].pid);
        s->hide[i] = CreateWindowExW(0, L"BUTTON", L"Hide",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)(ID_HIDE_BASE + i), s->hi, NULL);
        s->kill[i] = CreateWindowExW(0, L"BUTTON", L"Kill",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)(ID_KILL_BASE + i), s->hi, NULL);
    }
    TmUpdateScroll(h);
    TmLayoutButtons(h);
    InvalidateRect(h, NULL, TRUE);
}

static void TmPaint(HWND h, HDC dc) {
    struct TmState* s = GetTm(h);
    RECT rc;
    int  i;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < s->count; i++) {
        WCHAR line[160];
        RECT  tr;
        wsprintfW(line, L"pid=%08X h=%08X  %s",
                  s->rows[i].pid, (DWORD)s->rows[i].hproc,
                  s->rows[i].title[0] ? s->rows[i].title : L"(no title)");
        tr.left   = 4;
        tr.top    = i * TM_ROW_H - s->scroll_y;
        tr.right  = rc.right - TM_KILL_W - TM_HIDE_W - 12;
        tr.bottom = tr.top + TM_ROW_H;
        if (tr.bottom < 0 || tr.top > rc.bottom) continue;   /* off-screen row */
        DrawTextW(dc, line, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
}

static LRESULT CALLBACK TaskMgrProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        struct TmState* s = (struct TmState*)LocalAlloc(LPTR, sizeof(struct TmState));
        if (!s) return -1;
        s->hi = ((CREATESTRUCTW*)lp)->hInstance;
        SetWindowLongW(h, GWL_USERDATA, (LONG)s);
        TmRebuild(h);
        return 0;
    }

    case WM_SIZE:
        TmUpdateScroll(h);
        TmLayoutButtons(h);
        InvalidateRect(h, NULL, TRUE);
        return 0;

    case WM_VSCROLL:
        TmVScroll(h, wp);
        return 0;

    case WM_COMMAND: {
        struct TmState* s  = GetTm(h);
        int             id = LOWORD(wp);
        if (id >= ID_HIDE_BASE && id < ID_HIDE_BASE + s->count) {
            SubdueWindow(s->rows[id - ID_HIDE_BASE].hwnd);   /* drop topmost + hide */
            TmRebuild(h);
        } else if (id >= ID_KILL_BASE && id < ID_KILL_BASE + s->count) {
            int r = id - ID_KILL_BASE;
            if (!s->rows[r].hproc)
                MessageBoxW(h, L"OpenProcess returned no handle for this process.",
                            kTaskMgrClass, MB_OK | MB_ICONWARNING);
            else if (!TerminateProcess(s->rows[r].hproc, 0))
                MessageBoxW(h, L"TerminateProcess failed.",
                            kTaskMgrClass, MB_OK | MB_ICONERROR);
            TmRebuild(h);   /* refresh the list either way */
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        TmPaint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_DESTROY: {
        struct TmState* s = GetTm(h);
        if (s) { TmClear(s); LocalFree(s); }
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL RegisterTaskMgrClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = TaskMgrProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = kTaskMgrClass;
    return RegisterClassW(&wc) != 0;
}

void ShowTaskManager(HINSTANCE hi) {
    /* Single instance: re-focus if already open. Function-local static is plain
       C app state (this is guest CE code, not a CERF host service). */
    static HWND s_tm = NULL;
    if (s_tm && IsWindow(s_tm)) {
        SetWindowPos(s_tm, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(s_tm);
        return;
    }
    s_tm = CreateWindowExW(WS_EX_TOPMOST, kTaskMgrClass, L"Task Manager",
                           WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_SIZEBOX | WS_VSCROLL,
                           40, 40, 420, 300, NULL, NULL, hi, NULL);
    if (s_tm) {
        ShowWindow(s_tm, SW_SHOW);
        UpdateWindow(s_tm);
    }
}
