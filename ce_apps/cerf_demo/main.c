/* CerfDemo - shell for the bundled demo ROM: fade-in bokeh desktop + a
   topmost Win2000-style About dialog (banner, version, expandable system
   stats, cmd link). Pure Win32/GDI, links coredll.
   Build: tools/build_ce_app.ps1 -> CerfDemo.exe. */

#include "cerf_demo.h"
#include "band_data.h"

#ifndef ANTIALIASED_QUALITY
#define ANTIALIASED_QUALITY 4
#endif

#define BG_CLASS   TEXT("CerfBg")
#define DLG_CLASS  TEXT("CerfDlg")
#define ID_DETAILS 1001
#define ID_EDIT    1002
#define ID_STATS_TIMER 1
#define STATS_TICK_MS  1000
#define BANNER_H   CERF_BAND_RGB_H
#define DLG_W      412
#define DLG_COL_H  (BANNER_H + 160)
#define DLG_EXP_H  (BANNER_H + 340)
#define STARTUP_MS 650    /* hero reveal: clipped band -> full collapsed */
#define TOGGLE_MS  220    /* details expand / collapse, near-native feel */
#define FOCUSED_FRAME_MS    16
#define UNFOCUSED_SLEEP_MS 100


static const TCHAR* DLG_TITLE = TEXT("CE Runtime Foundation - Demo");

HINSTANCE       g_inst;
static int      g_sw, g_sh, g_dlgw = DLG_W;
HWND            g_dlg;     /* non-static on purpose: desktop.c links to it */
static HWND     g_bg;
static HWND     g_btn, g_edit;
static int      g_expanded;
HFONT           g_ui;
static HFONT    g_ui_bold, g_link;
static HDC      g_banddc;
static HBITMAP  g_bandbmp;
DWORD           g_start;   /* non-static on purpose: desktop.c links to it */
DWORD           g_anim_clock;
static int      g_anim_active, g_anim_centered, g_anim_target_exp;
static int      g_anim_h0, g_anim_h1, g_anim_top, g_anim_dur;
static DWORD    g_anim_start;

HBITMAP MakeDib32(int w, int h, unsigned int** bits) {
    BITMAPINFO bi;
    HDC dc = GetDC(NULL);
    HBITMAP bmp;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)bits, NULL, 0);
    ReleaseDC(NULL, dc);
    return bmp;
}

/* --- embedded image DCs ----------------------------------------------- */
static void BuildAssets(void) {
    unsigned int* bits;
    HDC mem = GetDC(NULL);
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    int fr = GetRValue(face), fg = GetGValue(face), fb = GetBValue(face);
    int i, n;

    g_banddc  = CreateCompatibleDC(mem);
    g_bandbmp = MakeDib32(CERF_BAND_RGB_W, CERF_BAND_RGB_H, &bits);
    SelectObject(g_banddc, g_bandbmp);
    n = CERF_BAND_RGB_W * CERF_BAND_RGB_H;
    for (i = 0; i < n; i++) {
        const unsigned char* s = &cerf_band_rgb[i * 3];
        bits[i] = ((unsigned)s[0] << 16) | ((unsigned)s[1] << 8) | s[2];
    }

    ReleaseDC(NULL, mem);
}

/* --- dialog ----------------------------------------------------------- */
static void RefreshStats(void) {
    TCHAR stats[512];
    if (!g_edit) return;
    BuildStats(stats);
    SetWindowText(g_edit, stats);
}

/* Position children + repaint against the window's CURRENT client size.
   Caller guarantees the window is already at its final height. */
static void LayoutDlg(HWND h, int exp) {
    RECT rc;
    g_expanded = exp;
    GetClientRect(h, &rc);
    if (g_edit) {
        if (exp) {
            MoveWindow(g_edit, 16, BANNER_H + 14, rc.right - 32,
                       rc.bottom - (BANNER_H + 14) - 130, TRUE);
            RefreshStats();
            ShowWindow(g_edit, SW_SHOW);
            SetTimer(h, ID_STATS_TIMER, STATS_TICK_MS, NULL);
        } else {
            KillTimer(h, ID_STATS_TIMER);
            ShowWindow(g_edit, SW_HIDE);
        }
    }
    if (g_btn) {
        MoveWindow(g_btn, 22, rc.bottom - 42, 116, 30, TRUE);
        ShowWindow(g_btn, SW_SHOW);
    }
    ToolsListLayout(h, exp);
    InvalidateRect(h, NULL, TRUE);
}

/* easeOutCubic on a 0..256 parameter: quick start, gentle settle. */
static int EaseOut(int t) {
    int inv = 256 - t;
    int cube = (inv * inv / 256) * inv / 256;
    return 256 - cube;
}

/* Begin a height animation. centered != 0: recompute centred Y each tick
   (startup reveal grows from screen centre); else hold top fixed
   (expand/collapse). Children stay hidden and WM_PAINT shows the band
   alone until the final tick lays the target content out. */
static void StartDlgAnim(int h0, int h1, int centered, int top,
                         int dur, int target_exp) {
    g_anim_h0 = h0; g_anim_h1 = h1;
    g_anim_centered = centered; g_anim_top = top;
    g_anim_dur = dur; g_anim_target_exp = target_exp;
    g_anim_start = GetTickCount();
    g_anim_active = 1;
    if (g_btn)  ShowWindow(g_btn, SW_HIDE);
    if (g_edit) ShowWindow(g_edit, SW_HIDE);
    ToolsListLayout(NULL, 0);
}

static void TickDlgAnim(HWND h) {
    DWORD now; int el, hh, x, y;
    if (!g_anim_active || !h) return;
    now = GetTickCount();
    el = (int)(now - g_anim_start);
    if (el < 0) el = 0;
    x = (g_sw - g_dlgw) / 2;
    if (el >= g_anim_dur) {
        hh = g_anim_h1;
        y = g_anim_centered ? (g_sh - hh) / 2 : g_anim_top;
        SetWindowPos(h, HWND_TOPMOST, x, y, g_dlgw, hh,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        g_anim_active = 0;
        LayoutDlg(h, g_anim_target_exp);
        return;
    }
    hh = g_anim_h0 + (g_anim_h1 - g_anim_h0)
         * EaseOut(el * 256 / g_anim_dur) / 256;
    y = g_anim_centered ? (g_sh - hh) / 2 : g_anim_top;
    SetWindowPos(h, HWND_TOPMOST, x, y, g_dlgw, hh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(h, NULL, TRUE);
}

static void DrawWelcome(HDC dc, int x, int y) {
    SIZE sz;
    const TCHAR* a = TEXT("Welcome to ");
    const TCHAR* b = TEXT("CE Runtime Foundation");
    const TCHAR* c = TEXT("!");
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    SelectObject(dc, g_ui);
    ExtTextOut(dc, x, y, 0, NULL, a, lstrlen(a), NULL);
    GetTextExtentPoint32(dc, a, lstrlen(a), &sz); x += sz.cx;
    SelectObject(dc, g_ui_bold);
    ExtTextOut(dc, x, y, 0, NULL, b, lstrlen(b), NULL);
    GetTextExtentPoint32(dc, b, lstrlen(b), &sz); x += sz.cx;
    SelectObject(dc, g_ui);
    ExtTextOut(dc, x, y, 0, NULL, c, lstrlen(c), NULL);
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        TCHAR stats[512];
        RECT rc;
        GetClientRect(h, &rc);
        BuildStats(stats);
        g_edit = CreateWindowEx(0, TEXT("EDIT"), stats,
            WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER,
            0, 0, 10, 10, h, (HMENU)ID_EDIT, g_inst, NULL);
        if (g_edit && g_ui)
            SendMessage(g_edit, WM_SETFONT, (WPARAM)g_ui, TRUE);
        g_btn = CreateWindow(TEXT("BUTTON"), TEXT("More"),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            22, rc.bottom - 42, 116, 30, h, (HMENU)ID_DETAILS, g_inst, NULL);
        ToolsListCreate(h);
        return 0;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
        if (d->CtlID == ID_DETAILS) {
            RECT cr, tr;
            UINT cst = (g_expanded ? DFCS_SCROLLUP : DFCS_SCROLLDOWN) |
                       ((d->itemState & ODS_SELECTED) ? DFCS_PUSHED : 0);
            FillRect(d->hDC, &d->rcItem, GetSysColorBrush(COLOR_BTNFACE));
            cr.left = d->rcItem.left + 4;
            cr.top  = (d->rcItem.top + d->rcItem.bottom) / 2 - 8;
            cr.right = cr.left + 16; cr.bottom = cr.top + 16;
            DrawFrameControl(d->hDC, &cr, DFC_SCROLL, cst);
            tr = d->rcItem; tr.left = cr.right + 8;
            SetBkMode(d->hDC, TRANSPARENT);
            SelectObject(d->hDC, g_ui);
            SetTextColor(d->hDC, GetSysColor(COLOR_BTNTEXT));
            DrawText(d->hDC, TEXT("More"), -1, &tr,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_DETAILS && !g_anim_active) {
            RECT wr;
            int cur;
            GetWindowRect(h, &wr);
            cur = wr.bottom - wr.top;
            if (!g_expanded)
                StartDlgAnim(cur, DLG_EXP_H, 1, 0, TOGGLE_MS, 1);
            else
                StartDlgAnim(cur, DLG_COL_H, 1, 0, TOGGLE_MS, 0);
        }
        return 0;
    case WM_NOTIFY:
        if (ToolsListNotify(h, lp)) return 0;
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC edc = (HDC)wp;
        SetBkColor(edc, GetSysColor(COLOR_WINDOW));
        SetTextColor(edc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_LBUTTONDOWN: {
        POINT pt; pt.x = LOWORD(lp); pt.y = HIWORD(lp);
        if (!g_expanded && RomsLinkHitTest(pt)) ShowRomsDialog(h, g_sw, g_sh);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt;
        HCURSOR hand;
        if (LOWORD(lp) != HTCLIENT || g_anim_active) break;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (!g_expanded && RomsLinkHitTest(pt)) {
            hand = RomsHandCursor();
            if (hand) { SetCursor(hand); return TRUE; }
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        if (g_banddc)
            BitBlt(dc, 0, 0, CERF_BAND_RGB_W, CERF_BAND_RGB_H, g_banddc,
                   0, 0, SRCCOPY);
        SetBkMode(dc, TRANSPARENT);
        if (g_anim_active) {
            /* mid-animation: band only; children hidden, content not yet laid out */
        } else if (!g_expanded) {
            DrawWelcome(dc, 22, BANNER_H + 24);
            DrawRomsLink(dc, 22, BANNER_H + 52, g_link);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DISPLAYCHANGE: {
        RECT wr;
        int hh, x, y;
        g_sw = GetSystemMetrics(SM_CXSCREEN);
        g_sh = GetSystemMetrics(SM_CYSCREEN);
        RefreshStats();
        if (!g_anim_active) {
            GetWindowRect(h, &wr);
            hh = wr.bottom - wr.top;
            x = (g_sw - g_dlgw) / 2; if (x < 0) x = 0;
            y = (g_sh - hh) / 2;     if (y < 0) y = 0;
            SetWindowPos(h, HWND_TOPMOST, x, y, g_dlgw, hh,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            LayoutDlg(h, g_expanded);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == ID_STATS_TIMER && g_expanded && !g_anim_active)
            RefreshStats();
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            ShowWindow(h, SW_HIDE);
            if (g_bg) SetForegroundWindow(g_bg);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (CountProcesses() > 2) {
            if (MessageBox(h,
                    TEXT("You sure you want to exit Demo App?\r\n\r\n")
                    TEXT("If you manage to quit all the foreground apps, you ")
                    TEXT("will lock yourself out of this operating system ")
                    TEXT("until reboot."),
                    TEXT("CE Runtime Foundation"),
                    MB_YESNO | MB_ICONQUESTION) == IDYES)
                PostQuitMessage(0);
        } else {
            MessageBox(h,
                TEXT("No other app/shell is running - refusing to exit."),
                TEXT("CE Runtime Foundation"), MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

/* --- startup ---------------------------------------------------------- */
static HFONT MakeFont(int height, int weight, int underline) {
    LOGFONT lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfUnderline = (BYTE)underline;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, TEXT("Tahoma"));
    return CreateFontIndirect(&lf);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPTSTR cmd, int show) {
    WNDCLASS bc, dc;
    HWND bg;
    MSG msg;

    g_inst = hInst;
    g_sw = GetSystemMetrics(SM_CXSCREEN);
    g_sh = GetSystemMetrics(SM_CYSCREEN);

    InitDiscs();
    BuildAssets();
    g_ui      = MakeFont(14, FW_NORMAL, 0);
    g_ui_bold = MakeFont(14, FW_BOLD,   0);
    g_link    = MakeFont(14, FW_NORMAL, 1);

    ZeroMemory(&bc, sizeof(bc));
    bc.lpfnWndProc   = BgProc;
    bc.hInstance     = hInst;
    bc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    bc.lpszClassName = BG_CLASS;
    RegisterClass(&bc);

    ZeroMemory(&dc, sizeof(dc));
    dc.lpfnWndProc   = DlgProc;
    dc.hInstance     = hInst;
    dc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    dc.lpszClassName = DLG_CLASS;
    RegisterClass(&dc);

    bg = CreateWindow(BG_CLASS, TEXT("CERF"), WS_POPUP | WS_VISIBLE,
                      0, 0, g_sw, g_sh, NULL, NULL, hInst, NULL);
    if (!bg) return 1;
    g_bg = bg;

    /* Created hidden at full collapsed size so WM_CREATE lays children out
       against the final client metrics; then shrunk to a band-only height
       and shown, and the main loop animates the reveal back up to DLG_COL_H. */
    g_dlg = CreateWindowEx(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
                           DLG_CLASS, DLG_TITLE,
                           WS_POPUP | WS_CAPTION | WS_SYSMENU |
                           WS_MINIMIZEBOX,
                           (g_sw - DLG_W) / 2, (g_sh - DLG_COL_H) / 2,
                           DLG_W, DLG_COL_H, NULL, NULL, hInst, NULL);

    ShowWindow(bg, show);
    UpdateWindow(bg);

    if (g_dlg) {
        RECT wr, cr;
        int ncv, ncw, hband;
        GetWindowRect(g_dlg, &wr);
        GetClientRect(g_dlg, &cr);
        ncv = (wr.bottom - wr.top) - (cr.bottom - cr.top);  /* caption+frame */
        ncw = (wr.right - wr.left) - (cr.right - cr.left);
        g_dlgw = CERF_BAND_RGB_W + ncw;
        hband = ncv + BANNER_H;                             /* client == band */
        SetWindowPos(g_dlg, HWND_TOPMOST, (g_sw - g_dlgw) / 2,
                     (g_sh - hband) / 2, g_dlgw, hband,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(g_dlg, SW_SHOWNA);
        SetForegroundWindow(g_dlg);     /* active (blue) caption, not bg */
        StartDlgAnim(hband, DLG_COL_H, 1, 0, STARTUP_MS, 0);
    }
    g_start = 0;
    g_anim_clock = 0;

    {
    DWORD prev = GetTickCount();
    int last_fps = -1;
    int   hz = GuestRefreshHz();
    int   frame_ms = hz > 0 ? 1000 / hz : FOCUSED_FRAME_MS;
    if (frame_ms < 1) frame_ms = 1;
    for (;;) {
        DWORD frame_start = GetTickCount();
        DWORD now, dt, spent;
        HWND  fg;
        int   ours;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (GetSystemMetrics(SM_CXSCREEN) != g_sw ||
            GetSystemMetrics(SM_CYSCREEN) != g_sh) {
            SendMessage(bg, WM_DISPLAYCHANGE, 0, 0);
            if (g_dlg) SendMessage(g_dlg, WM_DISPLAYCHANGE, 0, 0);
        }
        now = GetTickCount();
        dt  = now - prev;
        prev = now;
        fg   = GetForegroundWindow();
        ours = (fg == bg || fg == g_dlg);
        TickDlgAnim(g_dlg);
        if (!ours) {
            Sleep(UNFOCUSED_SLEEP_MS);
            continue;
        }
        g_anim_clock += dt;
        PresentBg(bg);
        if (g_fps != last_fps && g_dlg) {
            TCHAR title[64];
            last_fps = g_fps;
            wsprintf(title, TEXT("%s - %d FPS"), DLG_TITLE, g_fps);
            SetWindowText(g_dlg, title);
        }
        spent = GetTickCount() - frame_start;
        if (spent < (DWORD)frame_ms) Sleep((DWORD)frame_ms - spent);
    }
    }
done:
    if (g_ui)      DeleteObject(g_ui);
    if (g_ui_bold) DeleteObject(g_ui_bold);
    if (g_link)    DeleteObject(g_link);
    if (g_banddc)  DeleteDC(g_banddc);
    if (g_bandbmp) DeleteObject(g_bandbmp);
    return (int)msg.wParam;
}
