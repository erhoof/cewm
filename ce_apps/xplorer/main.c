#include "xplorer.h"

/* Toolbar control IDs are shared with the glyph code in xplorer.h. */

#define ROW_H     20   /* slim rows to leave more space for the view */
#define PAD        2
#define NAV_W     26   /* narrow owner-draw glyph buttons (back/fwd/up/go) */
#define REFRESH_W 52   /* "Refresh" text button                            */
#define SORT_W    40   /* "Sort" text button                               */
#define GO_W      26
#define TOOLBAR_H (PAD + ROW_H + PAD + ROW_H + PAD)

#define HIST_MAX  64

struct Frame {
    WCHAR cur[MAX_PATH];
    WCHAR hist[HIST_MAX][MAX_PATH];
    int   hist_count;
    int   hist_pos;
    HWND  view, edit, back, fwd, up, refresh, go, sort;
};

static struct Frame* GetFrame(HWND h) {
    return (struct Frame*)GetWindowLongW(h, GWL_USERDATA);
}

/* Parent of dir into out; root "\" stays root. */
static void ParentPath(const WCHAR* dir, WCHAR* out) {
    int i, last = 0;
    XStrCpy(out, dir, MAX_PATH);
    for (i = 1; out[i]; i++) if (out[i] == L'\\') last = i;
    if (last == 0) { out[0] = L'\\'; out[1] = 0; }
    else out[last] = 0;
}

static void UpdateNav(struct Frame* f) {
    EnableWindow(f->back, f->hist_pos > 0);
    EnableWindow(f->fwd,  f->hist_pos < f->hist_count - 1);
    EnableWindow(f->up,   lstrlenW(f->cur) > 1);
}

/* Push f->cur into history, truncating any forward entries first. */
static void PushHistory(struct Frame* f) {
    if (f->hist_pos < f->hist_count - 1) f->hist_count = f->hist_pos + 1;
    if (f->hist_count >= HIST_MAX) {
        memmove(f->hist[0], f->hist[1], (HIST_MAX - 1) * MAX_PATH * sizeof(WCHAR));
        f->hist_count = HIST_MAX - 1;
    }
    XStrCpy(f->hist[f->hist_count], f->cur, MAX_PATH);
    f->hist_count++;
    f->hist_pos = f->hist_count - 1;
}

/* Last path component for the window title: "\Storage Card\dir" -> "dir",
   root "\" -> "My Device". */
static void LeafName(const WCHAR* path, WCHAR* out) {
    int i, last = 0;
    if (lstrlenW(path) <= 1) { XStrCpy(out, L"My Device", MAX_PATH); return; }
    for (i = 0; path[i]; i++) if (path[i] == L'\\') last = i;
    XStrCpy(out, path + last + 1, MAX_PATH);
    if (out[0] == 0) XStrCpy(out, L"My Device", MAX_PATH);   /* trailing '\' */
}

/* Reflect f->cur into the path edit, the view, the title, and nav-button state. */
static void ApplyCurrent(HWND h) {
    struct Frame* f = GetFrame(h);
    WCHAR title[MAX_PATH];
    SetWindowTextW(f->edit, f->cur);
    LeafName(f->cur, title);
    SetWindowTextW(h, title);
    SendMessageW(f->view, XVM_SETPATH, 0, (LPARAM)f->cur);
    UpdateNav(f);
}

static void NavigateTo(HWND h, const WCHAR* path, BOOL push) {
    struct Frame* f = GetFrame(h);
    XStrCpy(f->cur, path, MAX_PATH);
    if (push) PushHistory(f);
    ApplyCurrent(h);
}

static void GoBack(HWND h) {
    struct Frame* f = GetFrame(h);
    if (f->hist_pos > 0) {
        f->hist_pos--;
        XStrCpy(f->cur, f->hist[f->hist_pos], MAX_PATH);
        ApplyCurrent(h);
    }
}

static void GoForward(HWND h) {
    struct Frame* f = GetFrame(h);
    if (f->hist_pos < f->hist_count - 1) {
        f->hist_pos++;
        XStrCpy(f->cur, f->hist[f->hist_pos], MAX_PATH);
        ApplyCurrent(h);
    }
}

static void GoUp(HWND h) {
    struct Frame* f = GetFrame(h);
    WCHAR parent[MAX_PATH];
    if (lstrlenW(f->cur) <= 1) return;   /* already at root */
    ParentPath(f->cur, parent);
    NavigateTo(h, parent, TRUE);
}

static void GoTyped(HWND h) {
    struct Frame* f = GetFrame(h);
    WCHAR typed[MAX_PATH];
    GetWindowTextW(f->edit, typed, MAX_PATH);
    if (typed[0]) NavigateTo(h, typed, TRUE);
}

/* Case-insensitive ".exe" suffix test (ASCII fold). */
static int EndsWithExe(const WCHAR* s) {
    int n = lstrlenW(s);
    const WCHAR* e;
    if (n < 4) return 0;
    e = s + n - 4;
    return e[0] == L'.' && (e[1] | 32) == L'e'
                        && (e[2] | 32) == L'x'
                        && (e[3] | 32) == L'e';
}

/* Launch an executable by full path. Zune/CE2 have no shell associations, so
   only .exe files are launchable; other files are ignored. Shared by the
   explorer frame and the desktop. */
void LaunchExe(const WCHAR* full) {
    PROCESS_INFORMATION pi;
    if (!EndsWithExe(full)) return;
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessW(full, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        MessageBoxW(NULL, full, L"xplorer - could not launch", MB_ICONERROR | MB_OK);
    }
}

static void LayoutToolbar(HWND h) {
    struct Frame* f = GetFrame(h);
    RECT rc;
    int  cw, y1, y2, x, edit_w, go_x;

    GetClientRect(h, &rc);
    cw = rc.right;

    y1 = PAD;
    x  = PAD;
    MoveWindow(f->back,    x, y1, NAV_W,     ROW_H, TRUE); x += NAV_W + PAD;
    MoveWindow(f->fwd,     x, y1, NAV_W,     ROW_H, TRUE); x += NAV_W + PAD;
    MoveWindow(f->up,      x, y1, NAV_W,     ROW_H, TRUE); x += NAV_W + PAD;
    MoveWindow(f->refresh, x, y1, REFRESH_W, ROW_H, TRUE); x += REFRESH_W + PAD;
    MoveWindow(f->sort,    x, y1, SORT_W,    ROW_H, TRUE);

    y2     = PAD + ROW_H + PAD;
    go_x   = cw - GO_W - PAD;
    edit_w = go_x - PAD - PAD;
    if (edit_w < 40) edit_w = 40;
    MoveWindow(f->edit, PAD,  y2, edit_w, ROW_H, TRUE);
    MoveWindow(f->go,   go_x, y2, GO_W,   ROW_H, TRUE);

    MoveWindow(f->view, 0, TOOLBAR_H, cw, rc.bottom - TOOLBAR_H, TRUE);
}

/* Sort dropdown: a popup under the Sort button; choice -> XVM_SETSORT to view. */
static void ShowSortMenu(HWND h) {
    struct Frame* f = GetFrame(h);
    HMENU         m = CreatePopupMenu();
    RECT          r;
    int           cmd;
    if (!m) return;
    AppendMenuW(m, MF_STRING, 1, L"Name");
    AppendMenuW(m, MF_STRING, 2, L"Size");
    AppendMenuW(m, MF_STRING, 3, L"Date");
    AppendMenuW(m, MF_STRING, 4, L"Type");
    GetWindowRect(f->sort, &r);
    SetForegroundWindow(h);
    cmd = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RETURNCMD, r.left, r.bottom, 0, h, NULL);
    DestroyMenu(m);
    if (cmd >= 1 && cmd <= 4)
        SendMessageW(f->view, XVM_SETSORT, (WPARAM)(cmd - 1), 0);   /* -> XSORT_* */
}

static LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        HINSTANCE      hi = cs->hInstance;
        const WCHAR*   initial = (const WCHAR*)cs->lpCreateParams;
        struct Frame*  f  = (struct Frame*)LocalAlloc(LPTR, sizeof(struct Frame));
        if (!f) return -1;
        SetWindowLongW(h, GWL_USERDATA, (LONG)f);

        /* Nav buttons are owner-drawn glyphs (narrow, drawn in xplorer_navglyph.c). */
        f->back    = CreateWindowExW(0, L"BUTTON", L"Back",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        0, 0, 0, 0, h, (HMENU)ID_BACK, hi, NULL);
        f->fwd     = CreateWindowExW(0, L"BUTTON", L"Forward",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        0, 0, 0, 0, h, (HMENU)ID_FWD, hi, NULL);
        f->up      = CreateWindowExW(0, L"BUTTON", L"Up",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        0, 0, 0, 0, h, (HMENU)ID_UP, hi, NULL);
        f->refresh = CreateWindowExW(0, L"BUTTON", L"Refresh",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)ID_REFRESH, hi, NULL);
        f->sort    = CreateWindowExW(0, L"BUTTON", L"Sort",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        0, 0, 0, 0, h, (HMENU)ID_SORT, hi, NULL);
        f->edit    = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        0, 0, 0, 0, h, (HMENU)ID_EDIT, hi, NULL);
        f->go      = CreateWindowExW(0, L"BUTTON", L"Go",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        0, 0, 0, 0, h, (HMENU)ID_GO, hi, NULL);
        f->view    = CreateWindowExW(0, kViewClass, L"",
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                        0, 0, 0, 0, h, (HMENU)ID_VIEW, hi, NULL);

        LayoutToolbar(h);
        NavigateTo(h, initial ? initial : L"\\", TRUE);
        return 0;
    }

    case WM_SIZE:
        LayoutToolbar(h);
        return 0;

    case WM_DRAWITEM:
        DrawNavGlyph((const DRAWITEMSTRUCT*)lp);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BACK:    GoBack(h);    return 0;
        case ID_FWD:     GoForward(h); return 0;
        case ID_UP:      GoUp(h);      return 0;
        case ID_REFRESH: ApplyCurrent(h); return 0;   /* re-read current dir */
        case ID_GO:      GoTyped(h);   return 0;
        case ID_SORT:    ShowSortMenu(h); return 0;
        }
        return 0;

    case XVN_DESCEND: {
        struct Frame* f = GetFrame(h);
        WCHAR child[MAX_PATH];
        XJoinPath(f->cur, (const WCHAR*)lp, child);
        NavigateTo(h, child, TRUE);
        return 0;
    }

    case XVN_LAUNCH: {
        struct Frame* f = GetFrame(h);
        WCHAR full[MAX_PATH];
        XJoinPath(f->cur, (const WCHAR*)lp, full);
        LaunchExe(full);
        return 0;
    }

    case WM_DESTROY: {
        struct Frame* f = GetFrame(h);
        if (f) LocalFree(f);
        return 0;   /* an explorer frame is secondary; only the desktop quits */
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static BOOL RegisterFrameClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = FrameProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"Xplorer";
    return RegisterClassW(&wc) != 0;
}

/* Open a new (non-topmost) explorer window navigated to `path`. The path is
   passed through CreateWindow's lpParam and read back in WM_CREATE. */
void OpenExplorer(HINSTANCE hi, const WCHAR* path) {
    HWND h = CreateWindowExW(0, L"Xplorer", L"xplorer",
                             WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                             WS_MAXIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN,
                             20, 20, 380, 320,
                             NULL, NULL, hi, (LPVOID)path);
    if (h) {
        ShowWindow(h, SW_SHOW);
        UpdateWindow(h);
    }
}

/* Drop a window's topmost flag and hide it. A foreign shell window stays
   full-screen even when minimized, so it must leave the visible z-order entirely
   (SW_HIDE) or it covers the bottom-most desktop backdrop. Documented USER calls
   only. */
void SubdueWindow(HWND w) {
    SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(w, SW_HIDE);
}

/* A foreign top-level window that covers ~the whole screen is the OEM shell
   (Zune gemstone, CE2 taskman.exe, HPC explorer.exe, PPC shell32.exe, ...). */
static int IsForeignFullScreen(HWND w) {
    RECT r;
    int  sw = GetSystemMetrics(SM_CXSCREEN);
    int  sh = GetSystemMetrics(SM_CYSCREEN);
    if (!IsWindowVisible(w) || XIsOurWindow(w)) return 0;
    if (!GetWindowRect(w, &r)) return 0;
    return (r.right - r.left) >= sw * 9 / 10 && (r.bottom - r.top) >= sh * 9 / 10;
}

static BOOL CALLBACK SubdueShellEnumProc(HWND w, LPARAM lp) {
    (void)lp;
    if (IsForeignFullScreen(w)) SubdueWindow(w);
    return TRUE;
}

/* Hide OEM shells so xplorer's desktop is the visible backdrop. They are
   full-screen and ignore being lowered/minimized, so they are matched by shape
   (full-screen, not one of ours - works across ROMs) and hidden outright; the
   Task Manager's per-window Hide covers any that respawn. */
static void SubdueForeignShells(void) {
    EnumWindows(SubdueShellEnumProc, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR cmd, int show) {
    MSG  m;
    HWND desk;

    (void)hPrev; (void)cmd; (void)show;

    if (!RegisterViewClass(hInstance)) return 1;
    if (!RegisterFrameClass(hInstance)) return 1;
    if (!RegisterDesktopClass(hInstance)) return 1;
    if (!RegisterTaskbarClass(hInstance)) return 1;
    if (!RegisterTaskMgrClass(hInstance)) return 1;
    if (!RegisterRunClass(hInstance)) return 1;

    /* Shell surface: desktop at the bottom, taskbar topmost, gemstone subdued.
       Explorer windows open on demand from the desktop. */
    desk = CreateDesktopWindow(hInstance);
    CreateTaskbar(hInstance);
    SubdueForeignShells();
    if (desk) SetForegroundWindow(desk);   /* first run: ensure the desktop paints up front */

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
