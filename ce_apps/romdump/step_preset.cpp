/* Wizard step 1: preset picker. The CE 1.0 listbox has no owner-draw styles
   (LBS_OWNERDRAWFIXED/VARIABLE absent from WCE100 winuser.h), so the two-line
   rows (name + known devices) are painted directly with DrawText/DT_WORDBREAK,
   present WCE100..CE7. */

#include "romdump.h"

#define ID_PRESET_TITLE 211
static const WCHAR kPresetClass[] = L"CerfPresetList";

#define ROW_MX 6   /* row left/right text inset */
#define ROW_MY 4   /* row top/bottom text inset  */
#define PLX_SCROLL 0   /* control cbWndExtra slot: internal vertical scroll (px) */

static AppState* PLState(HWND h)   { return (AppState*)GetWindowLongW(GetParent(h), WNDX_STATE); }
static int  PLScroll(HWND h)       { return (int)GetWindowLongW(h, PLX_SCROLL); }
static void PLSetScroll(HWND h, int v) { SetWindowLongW(h, PLX_SCROLL, (LONG)v); }

/* Height preset i needs at content width w: name line + wrapped detail. */
static int RowHeight(HDC dc, int i, int w) {
    RECT r;
    int  tw = w - 2 * ROW_MX, h;
    if (tw < 20) tw = 20;
    r.left = 0; r.top = 0; r.right = tw; r.bottom = 0;
    DrawTextW(dc, kPresets[i].name, -1, &r, DT_SINGLELINE | DT_CALCRECT);
    h = r.bottom - r.top;
    if (kPresets[i].detail && kPresets[i].detail[0]) {
        r.left = 0; r.top = 0; r.right = tw; r.bottom = 0;
        DrawTextW(dc, kPresets[i].detail, -1, &r, DT_WORDBREAK | DT_CALCRECT);
        h += r.bottom - r.top;
    }
    return h + 2 * ROW_MY;
}

static int RowTop(HDC dc, int idx, int w) {   /* pre-scroll y of row idx */
    int i, y = 0;
    for (i = 0; i < idx; ++i) y += RowHeight(dc, i, w);
    return y;
}

static int TotalHeight(HWND h) {
    HDC  dc = GetDC(h);
    RECT rc;
    int  t;
    GetClientRect(h, &rc);
    t = RowTop(dc, kNumPresets, rc.right);
    ReleaseDC(h, dc);
    return t;
}

static void PLSyncBar(HWND h) {
    SCROLLINFO si;
    RECT rc;
    int  total = TotalHeight(h), view, sc = PLScroll(h);
    GetClientRect(h, &rc);
    view = rc.bottom;
    if (sc > total - view) sc = total - view;
    if (sc < 0) sc = 0;
    PLSetScroll(h, sc);
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = total > 0 ? total - 1 : 0;
    si.nPage  = (UINT)(view > 0 ? view : 1);
    si.nPos   = sc;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void PLEnsureVisible(HWND h, int idx) {
    HDC  dc = GetDC(h);
    RECT rc;
    int  top, bot, sc, view;
    GetClientRect(h, &rc);
    view = rc.bottom;
    top  = RowTop(dc, idx, rc.right);
    bot  = top + RowHeight(dc, idx, rc.right);
    ReleaseDC(h, dc);
    sc = PLScroll(h);
    if      (top - sc < 0)      sc = top;
    else if (bot - sc > view)   sc = bot - view;
    PLSetScroll(h, sc);
}

static int PLHitTest(HWND h, int py) {
    HDC  dc = GetDC(h);
    RECT rc;
    int  i, y, w, hit = -1;
    GetClientRect(h, &rc);
    w = rc.right;
    y = -PLScroll(h);
    for (i = 0; i < kNumPresets; ++i) {
        int rh = RowHeight(dc, i, w);
        if (py >= y && py < y + rh) { hit = i; break; }
        y += rh;
    }
    ReleaseDC(h, dc);
    return hit;
}

static void PLPaint(HWND h) {
    AppState*   st = PLState(h);
    PAINTSTRUCT ps;
    HDC   dc = BeginPaint(h, &ps);
    RECT  rc;
    int   i, y, w;
    BOOL  focused = (GetFocus() == h);
    GetClientRect(h, &rc);
    w = rc.right;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    y = -PLScroll(h);
    for (i = 0; i < kNumPresets; ++i) {
        int  rh  = RowHeight(dc, i, w);
        BOOL sel = (st && st->preset_index == i);
        RECT row, tr;
        row.left = 0; row.top = y; row.right = w; row.bottom = y + rh;
        if (sel) {
            HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            FillRect(dc, &row, hb);
            DeleteObject(hb);
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        tr.left = ROW_MX; tr.top = y + ROW_MY; tr.right = w - ROW_MX; tr.bottom = y + rh;
        {
            RECT nr;
            nr.left = tr.left; nr.top = tr.top; nr.right = tr.right; nr.bottom = tr.top;
            DrawTextW(dc, kPresets[i].name, -1, &nr, DT_SINGLELINE | DT_CALCRECT);
            DrawTextW(dc, kPresets[i].name, -1, &tr, DT_SINGLELINE);
            tr.top += nr.bottom - nr.top;
        }
        if (kPresets[i].detail && kPresets[i].detail[0]) {
            if (!sel) SetTextColor(dc, RGB(96, 96, 96));
            DrawTextW(dc, kPresets[i].detail, -1, &tr, DT_WORDBREAK);
        }
        if (sel && focused) {
            RECT fr;
            fr.left = 1; fr.top = y + 1; fr.right = w - 1; fr.bottom = y + rh - 1;
            DrawFocusRect(dc, &fr);
        }
        y += rh;
    }
    EndPaint(h, &ps);
}

static void PLMove(HWND h, int idx) {
    AppState* st = PLState(h);
    if (!st || idx < 0 || idx >= kNumPresets) return;
    st->preset_index = idx;
    PLEnsureVisible(h, idx);
    PLSyncBar(h);
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK PresetListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* st = PLState(h);
    switch (msg) {
    case WM_ERASEBKGND:  return 1;               /* PLPaint fills the client   */
    case WM_PAINT:       PLPaint(h);  return 0;
    case WM_SIZE:        PLSyncBar(h); return 0;
    case WM_GETDLGCODE:  return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:   InvalidateRect(h, NULL, FALSE); return 0;

    case WM_LBUTTONDOWN: {
        int i = PLHitTest(h, (short)HIWORD(lp));
        SetFocus(h);
        if (i >= 0) PLMove(h, i);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int i = PLHitTest(h, (short)HIWORD(lp));
        if (i >= 0 && st) {
            st->preset_index = i;
            InvalidateRect(h, NULL, FALSE);
            SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_NAV, BN_CLICKED),
                         (LPARAM)GetDlgItem(GetParent(h), ID_NAV));
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (st && wp == VK_UP)   { PLMove(h, st->preset_index - 1); return 0; }
        if (st && wp == VK_DOWN) { PLMove(h, st->preset_index + 1); return 0; }
        break;

    case WM_VSCROLL: {
        RECT rc;
        int  view, total = TotalHeight(h), y = PLScroll(h);
        GetClientRect(h, &rc);
        view = rc.bottom;
        switch (LOWORD(wp)) {
        case SB_LINEUP:        y -= 16;   break;
        case SB_LINEDOWN:      y += 16;   break;
        case SB_PAGEUP:        y -= view; break;
        case SB_PAGEDOWN:      y += view; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: y = HIWORD(wp); break;
        }
        if (y > total - view) y = total - view;
        if (y < 0) y = 0;
        PLSetScroll(h, y);
        SetScrollPos(h, SB_VERT, y, TRUE);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void StepPresetCreate(AppState* st, HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = PresetListProc;
    wc.cbWndExtra    = sizeof(LONG);
    wc.hInstance     = hi;
    wc.hCursor       = NULL;
    wc.hbrBackground = NULL;
    wc.lpszClassName = kPresetClass;
    RegisterClassW(&wc);   /* registered once per process; harmless if repeated */

    CreateWindowExW(0, L"STATIC", L"Select the device CPU / board:",
                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                    0, 0, 0, 0, st->hwnd, (HMENU)ID_PRESET_TITLE, hi, NULL);
    CreateWindowExW(0, kPresetClass, NULL,
                    WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_TABSTOP,
                    0, 0, 0, 0, st->hwnd, (HMENU)ID_PRESETLIST, hi, NULL);
    st->preset_index = 0;
}

void StepPresetShow(AppState* st, BOOL show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(st->hwnd, ID_PRESET_TITLE), cmd);
    ShowWindow(GetDlgItem(st->hwnd, ID_PRESETLIST),   cmd);
}

/* The picker fills the content area and scrolls its own rows, so the outer
   wizard scrollbar is not needed here: return the view height. */
int StepPresetLayout(AppState* st, RECT area) {
    int m = 6, x = area.left + m, w = area.right - area.left - 2 * m;
    int top = area.top + 4;
    if (w < 40) w = 40;
    MoveWindow(GetDlgItem(st->hwnd, ID_PRESET_TITLE), x, top, w, 18, TRUE);
    MoveWindow(GetDlgItem(st->hwnd, ID_PRESETLIST),
               x, top + 22, w, (area.bottom - (top + 22)) - m, TRUE);
    PLSyncBar(GetDlgItem(st->hwnd, ID_PRESETLIST));
    return area.bottom - area.top;
}

BOOL StepPresetOnNext(AppState* st) {
    if (st->preset_index < 0 || st->preset_index >= kNumPresets)
        st->preset_index = 0;
    return TRUE;
}
