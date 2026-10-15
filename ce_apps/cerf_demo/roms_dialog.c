#include "cerf_demo.h"
#include "resource.h"
#include <commctrl.h>

#define ROMS_CLASS   TEXT("CerfRomsDlg")
#define ROMS_TITLE   TEXT("Running other ROMs")
#define ID_ROMS_LIST 1201

#define ROMS_W           460
#define ROMS_H           143
#define ROMS_PAD         16
#define ROMS_LIST_W      106
#define ROMS_LIST_H      78
#define ROMS_LIST_FONT_H 14
#define ROMS_MASK_KEY    RGB(255, 0, 255)

static const TCHAR* ROMS_LINK = TEXT("How to run other ROMs?");
static const TCHAR* ROMS_ITEM = TEXT("CE Runtime Foundation");

static const TCHAR* ROMS_BODY =
    TEXT("You are booting CerfOS - the small demo OS without any ")
    TEXT("components. If you would like to boot other ROMs - open ")
    TEXT("CE Runtime Foundation on your host.");

static RECT    g_roms_linkrect;
static HWND    g_roms_dlg;
static HWND    g_roms_list;
static HCURSOR g_roms_hand;
static HFONT   g_roms_font;
static int     g_icon_cx, g_icon_cy;

void DrawRomsLink(HDC dc, int x, int y, HFONT link_font) {
    SIZE sz;
    SelectObject(dc, link_font);
    SetTextColor(dc, RGB(20, 60, 180));
    ExtTextOut(dc, x, y, 0, NULL, ROMS_LINK, lstrlen(ROMS_LINK), NULL);
    GetTextExtentPoint32(dc, ROMS_LINK, lstrlen(ROMS_LINK), &sz);
    g_roms_linkrect.left   = x;
    g_roms_linkrect.top    = y;
    g_roms_linkrect.right  = x + sz.cx;
    g_roms_linkrect.bottom = y + sz.cy;
}

int RomsLinkHitTest(POINT pt) {
    return PtInRect(&g_roms_linkrect, pt);
}

HCURSOR RomsHandCursor(void) {
    if (!g_roms_hand) g_roms_hand = LoadCursor(NULL, IDC_HAND);
    return g_roms_hand;
}

static HFONT MakeListFont(void) {
    LOGFONT lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight         = ROMS_LIST_FONT_H;
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfQuality        = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, TEXT("Tahoma"));
    return CreateFontIndirect(&lf);
}

static void AttachIcon(HWND list) {
    HIMAGELIST himl;
    HICON      icon;
    HBITMAP    bmp;

    himl = ImageList_Create(g_icon_cx, g_icon_cy, ILC_COLOR | ILC_MASK, 1, 0);
    if (!himl) return;

    icon = LoadIcon(g_inst, MAKEINTRESOURCE(IDI_CERF));
    if (icon) {
        ImageList_ReplaceIcon(himl, -1, icon);
        DestroyIcon(icon);
    } else {
        bmp = MakeGenericAppIconDdb(g_icon_cx, g_icon_cy, ROMS_MASK_KEY);
        if (bmp) {
            ImageList_AddMasked(himl, bmp, ROMS_MASK_KEY);
            DeleteObject(bmp);
        }
    }
    SendMessage(list, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)himl);
}

static void SizeIconCell(HWND list) {
    HDC        dc = GetDC(list);
    HFONT      prev;
    SIZE       sz;
    TEXTMETRIC tm;
    int        cx, cy;

    if (!dc) return;
    prev = (HFONT)SelectObject(dc, g_roms_font);
    GetTextExtentPoint32(dc, ROMS_ITEM, lstrlen(ROMS_ITEM), &sz);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, prev);
    ReleaseDC(list, dc);

    cx = sz.cx + 24;
    if (cx < g_icon_cx + 24) cx = g_icon_cx + 24;
    cy = g_icon_cy + tm.tmHeight + 20;
    SendMessage(list, LVM_SETICONSPACING, 0, MAKELONG(cx, cy));
}

static void CreateList(HWND parent) {
    RECT   rc;
    LVITEM item;

    if (!EnsureCommonControls()) return;
    g_icon_cx = GetSystemMetrics(SM_CXICON);
    g_icon_cy = GetSystemMetrics(SM_CYICON);
    if (g_icon_cx < 1) g_icon_cx = 32;
    if (g_icon_cy < 1) g_icon_cy = 32;
    GetClientRect(parent, &rc);

    g_roms_list = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("SysListView32"), NULL,
        WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | LVS_NOLABELWRAP,
        rc.right - ROMS_PAD - ROMS_LIST_W, ROMS_PAD,
        ROMS_LIST_W, ROMS_LIST_H,
        parent, (HMENU)ID_ROMS_LIST, g_inst, NULL);
    if (!g_roms_list) return;
    if (!g_roms_font) g_roms_font = MakeListFont();
    if (g_roms_font)
        SendMessage(g_roms_list, WM_SETFONT, (WPARAM)g_roms_font, TRUE);

    AttachIcon(g_roms_list);
    SizeIconCell(g_roms_list);

    item.mask     = LVIF_TEXT | LVIF_IMAGE;
    item.iItem    = 0;
    item.iSubItem = 0;
    item.iImage   = 0;
    item.pszText  = (LPTSTR)ROMS_ITEM;
    SendMessage(g_roms_list, LVM_INSERTITEM, 0, (LPARAM)&item);
}

static void PaintBody(HWND h, HFONT ui) {
    PAINTSTRUCT ps;
    HDC         dc;
    RECT        rc, tr;

    dc = BeginPaint(h, &ps);
    GetClientRect(h, &rc);
    FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    if (ui) SelectObject(dc, ui);
    tr.left   = ROMS_PAD;
    tr.top    = ROMS_PAD;
    tr.right  = rc.right - 2 * ROMS_PAD - ROMS_LIST_W;
    tr.bottom = rc.bottom - ROMS_PAD;
    DrawText(dc, ROMS_BODY, -1, &tr, DT_LEFT | DT_WORDBREAK);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK RomsProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        CreateList(h);
        return 0;
    case WM_PAINT:
        PaintBody(h, g_ui);
        return 0;
    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lp;
        if (g_roms_list && nh->hwndFrom == g_roms_list &&
            nh->code == NM_DBLCLK) {
            MessageBox(h, TEXT("Not this one! Open CE Runtime Foundation ")
                          TEXT("on your host."),
                       TEXT("CE Runtime Foundation"),
                       MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC edc = (HDC)wp;
        SetBkColor(edc, GetSysColor(COLOR_WINDOW));
        SetTextColor(edc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_roms_dlg  = NULL;
        g_roms_list = NULL;
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

void ShowRomsDialog(HWND parent, int screen_w, int screen_h) {
    WNDCLASS wc;
    int      w = ROMS_W, h = ROMS_H, x, y;

    if (g_roms_dlg) {
        SetForegroundWindow(g_roms_dlg);
        return;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = RomsProc;
    wc.hInstance     = g_inst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = ROMS_CLASS;
    RegisterClass(&wc);

    if (w > screen_w - 2 * ROMS_PAD) w = screen_w - 2 * ROMS_PAD;
    if (h > screen_h - 2 * ROMS_PAD) h = screen_h - 2 * ROMS_PAD;
    x = (screen_w - w) / 2; if (x < 0) x = 0;
    y = (screen_h - h) / 2; if (y < 0) y = 0;

    g_roms_dlg = CreateWindowEx(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
                                ROMS_CLASS, ROMS_TITLE,
                                WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU,
                                x, y, w, h, parent, NULL, g_inst, NULL);
}
