#define NOMINMAX

#include "host_dark_mode.h"

#include "../core/cerf_emulator.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

REGISTER_SERVICE(HostDarkMode);

namespace {

/* Undocumented UAH menu messages + structs (ysc3839/win32-darkmode). */
constexpr UINT kWmUahDrawMenu     = 0x0091;
constexpr UINT kWmUahDrawMenuItem = 0x0092;

union UAHMENUITEMMETRICS {
    struct { DWORD cx, cy; } rgsizeBar[2];
    struct { DWORD cx, cy; } rgsizePopup[4];
};
struct UAHMENUPOPUPMETRICS { DWORD rgcx[4]; DWORD fUpdateMaxWidths : 2; };
struct UAHMENU      { HMENU hmenu; HDC hdc; DWORD dwFlags; };
struct UAHMENUITEM  { int iPosition; UAHMENUITEMMETRICS umim; UAHMENUPOPUPMETRICS umpm; };
struct UAHDRAWMENUITEM { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; };

constexpr COLORREF kClrBar      = RGB(32, 32, 32);
constexpr COLORREF kClrHot      = RGB(60, 60, 60);
constexpr COLORREF kClrSel      = RGB(70, 70, 70);
constexpr COLORREF kClrText     = RGB(230, 230, 230);
constexpr COLORREF kClrDisabled = RGB(120, 120, 120);
constexpr COLORREF kClrDlg      = RGB(32, 32, 32);   /* dialog client background */
constexpr COLORREF kClrEdit     = RGB(45, 45, 45);   /* edit/list field background */
constexpr COLORREF kClrLink     = RGB(96, 170, 255);

using RtlGetNtVersionNumbers_t        = void (WINAPI*)(LPDWORD, LPDWORD, LPDWORD);
using SetPreferredAppMode_t           = int  (WINAPI*)(int);
using AllowDarkModeForApp_t           = bool (WINAPI*)(bool);
using AllowDarkModeForWindow_t        = BOOL (WINAPI*)(HWND, BOOL);
using FlushMenuThemes_t               = void (WINAPI*)();
using RefreshImmersiveColorPolicy_t   = void (WINAPI*)();
using ShouldAppsUseDarkMode_t         = bool (WINAPI*)();

/* ysc3839/win32-darkmode DarkMode.h: the uxtheme dark-mode ordinals exist from
   1809 (17763); ordinal 135 is AllowDarkModeForApp below 1903 (18362) and
   SetPreferredAppMode from 1903. */
constexpr DWORD kBuild1809 = 17763;
constexpr DWORD kBuild1903 = 18362;

SetPreferredAppMode_t         g_SetPreferredAppMode      = nullptr;
AllowDarkModeForWindow_t      g_AllowDarkModeForWindow   = nullptr;
FlushMenuThemes_t             g_FlushMenuThemes          = nullptr;
RefreshImmersiveColorPolicy_t g_RefreshImmersivePolicy   = nullptr;
ShouldAppsUseDarkMode_t       g_ShouldAppsUseDarkMode    = nullptr;

}  /* namespace */

HostDarkMode::~HostDarkMode() {
    if (bar_brush_)  DeleteObject(bar_brush_);
    if (hot_brush_)  DeleteObject(hot_brush_);
    if (sel_brush_)  DeleteObject(sel_brush_);
    if (dlg_brush_)  DeleteObject(dlg_brush_);
    if (edit_brush_) DeleteObject(edit_brush_);
    if (menu_font_)  DeleteObject(menu_font_);
    if (ui_font_)    DeleteObject(ui_font_);
}

void HostDarkMode::EnsureResources() {
    if (!bar_brush_)  bar_brush_  = CreateSolidBrush(kClrBar);
    if (!hot_brush_)  hot_brush_  = CreateSolidBrush(kClrHot);
    if (!sel_brush_)  sel_brush_  = CreateSolidBrush(kClrSel);
    if (!dlg_brush_)  dlg_brush_  = CreateSolidBrush(kClrDlg);
    if (!edit_brush_) edit_brush_ = CreateSolidBrush(kClrEdit);
    if (!menu_font_) {
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            menu_font_ = CreateFontIndirectW(&ncm.lfMenuFont);
    }
    EnsureUiFont();
}

void HostDarkMode::EnsureUiFont() {
    if (ui_font_) return;
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        ui_font_ = CreateFontIndirectW(&ncm.lfMessageFont);
}

void HostDarkMode::Init() {
    if (inited_) return;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto get_ver = (RtlGetNtVersionNumbers_t)GetProcAddress(
        ntdll, "RtlGetNtVersionNumbers");
    if (!get_ver) return;
    DWORD major = 0, minor = 0, build = 0;
    get_ver(&major, &minor, &build);
    build &= ~0xF0000000u;
    if (major != 10 || minor != 0 || build < kBuild1809) return;

    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    FARPROC ord135           = GetProcAddress(ux, MAKEINTRESOURCEA(135));
    g_SetPreferredAppMode    = build >= kBuild1903
                                   ? (SetPreferredAppMode_t)ord135 : nullptr;
    auto allow_dark_for_app  = build <  kBuild1903
                                   ? (AllowDarkModeForApp_t)ord135 : nullptr;
    g_AllowDarkModeForWindow = (AllowDarkModeForWindow_t)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    g_FlushMenuThemes        = (FlushMenuThemes_t)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    g_RefreshImmersivePolicy = (RefreshImmersiveColorPolicy_t)GetProcAddress(ux, MAKEINTRESOURCEA(104));
    g_ShouldAppsUseDarkMode  = (ShouldAppsUseDarkMode_t)GetProcAddress(ux, MAKEINTRESOURCEA(132));
    if (!ord135) return;

    /* MUST allow dark + RefreshImmersiveColorPolicy (104) BEFORE reading
       ShouldAppsUseDarkMode (132): 132 returns the immersive-policy cache, and a
       read against a cold cache (a board that materialises HostWindow very early)
       returns a stale "light" that leaves the dark path off. (ysc3839/win32-darkmode.) */
    if (g_SetPreferredAppMode) g_SetPreferredAppMode(1 /*AllowDark*/);
    else                       allow_dark_for_app(true);
    if (g_RefreshImmersivePolicy) g_RefreshImmersivePolicy();

    const bool sys_dark = g_ShouldAppsUseDarkMode && g_ShouldAppsUseDarkMode();
    if (g_SetPreferredAppMode)
        g_SetPreferredAppMode(sys_dark ? 2 /*ForceDark*/ : 3 /*ForceLight*/);
    else
        allow_dark_for_app(sys_dark);
    if (g_RefreshImmersivePolicy) g_RefreshImmersivePolicy();
    if (!sys_dark) return;  /* light system: leave inited_ = false */

    EnsureResources();
    inited_ = true;
}

void HostDarkMode::ApplyToWindow(HWND h) {
    if (!inited_ || !h) return;
    if (g_AllowDarkModeForWindow) g_AllowDarkModeForWindow(h, TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    /* Dark non-client caption via DWMWA_USE_IMMERSIVE_DARK_MODE (20 on 20H1+;
       Windows 10 1809-1909 accept its predecessor attribute 19). */
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
    if (g_FlushMenuThemes) g_FlushMenuThemes();
}

BOOL CALLBACK HostDarkMode::ThemeChildProc(HWND child, LPARAM self_lp) {
    auto* self = reinterpret_cast<HostDarkMode*>(self_lp);

    if (self->ui_font_)
        SendMessageW(child, WM_SETFONT, (WPARAM)self->ui_font_, TRUE);

    if (self->inited_) {
        if (g_AllowDarkModeForWindow) g_AllowDarkModeForWindow(child, TRUE);
        /* Edit/combo fields take the CFD dark theme (dark frame + field);
           buttons/checkboxes/lists take the Explorer dark theme. */
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, 63);
        const bool is_edit = (lstrcmpiW(cls, L"Edit") == 0 ||
                              lstrcmpiW(cls, L"ComboBox") == 0);
        SetWindowTheme(child, is_edit ? L"DarkMode_CFD" : L"DarkMode_Explorer",
                       nullptr);
    }
    return TRUE;
}

void HostDarkMode::ApplyToDialog(HWND dlg) {
    if (!dlg) return;
    EnsureUiFont();             /* modern font even when the OS has no dark mode */
    if (inited_) {
        EnsureResources();
        ApplyToWindow(dlg);     /* dark title bar */
    }
    EnumChildWindows(dlg, &HostDarkMode::ThemeChildProc,
                     reinterpret_cast<LPARAM>(this));
}

bool HostDarkMode::HandleCtlColor(UINT msg, WPARAM wp, LRESULT& out) {
    if (!inited_) return false;
    EnsureResources();
    HDC hdc = reinterpret_cast<HDC>(wp);
    SetTextColor(hdc, kClrText);
    switch (msg) {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetBkColor(hdc, kClrEdit);
            out = reinterpret_cast<LRESULT>(edit_brush_);
            return true;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor(hdc, kClrDlg);
            out = reinterpret_cast<LRESULT>(dlg_brush_);
            return true;
    }
    return false;
}

bool HostDarkMode::HandleLinkCustomDraw(LPARAM syslink_notify, LRESULT& out) {
    if (!inited_) return false;
    auto* cd = reinterpret_cast<NMCUSTOMDRAW*>(syslink_notify);
    if (cd->dwDrawStage == CDDS_PREPAINT) {
        out = CDRF_NOTIFYITEMDRAW;
        return true;
    }
    if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
        SetTextColor(cd->hdc, kClrLink);
        out = CDRF_NEWFONT;
        return true;
    }
    return false;
}

bool HostDarkMode::EraseBackground(HDC hdc, HWND hwnd) {
    if (!inited_) return false;
    EnsureResources();
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, dlg_brush_);
    return true;
}

HBRUSH HostDarkMode::BgBrush() {
    EnsureResources();
    return dlg_brush_;
}

COLORREF HostDarkMode::BgColor() const { return kClrDlg; }

COLORREF HostDarkMode::TextColor() const { return kClrText; }

HFONT HostDarkMode::UiFont() {
    EnsureUiFont();
    return ui_font_;
}

bool HostDarkMode::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 LRESULT& out) {
    if (!inited_) return false;

    switch (msg) {
        case kWmUahDrawMenu: {
            auto* pudm = reinterpret_cast<UAHMENU*>(lp);
            MENUBARINFO mbi = { sizeof(mbi) };
            GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi);
            RECT wr; GetWindowRect(hwnd, &wr);
            RECT rc = mbi.rcBar;
            OffsetRect(&rc, -wr.left, -wr.top);
            FillRect(pudm->hdc, &rc, bar_brush_);
            out = TRUE;
            return true;
        }

        case kWmUahDrawMenuItem: {
            auto* p = reinterpret_cast<UAHDRAWMENUITEM*>(lp);
            wchar_t buf[256] = {};
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = buf;
            mii.cch = 255;
            GetMenuItemInfoW(p->um.hmenu, (UINT)p->umi.iPosition, TRUE, &mii);

            HBRUSH bg = bar_brush_;
            if (p->dis.itemState & ODS_SELECTED)            bg = sel_brush_;
            else if (p->dis.itemState & ODS_HOTLIGHT)       bg = hot_brush_;
            FillRect(p->um.hdc, &p->dis.rcItem, bg);

            UINT dt = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
            if (p->dis.itemState & ODS_NOACCEL) dt |= DT_HIDEPREFIX;
            const COLORREF txt =
                (p->dis.itemState & (ODS_GRAYED | ODS_DISABLED))
                    ? kClrDisabled : kClrText;

            HGDIOBJ old_font = menu_font_
                ? SelectObject(p->um.hdc, menu_font_) : nullptr;
            const int old_bk = SetBkMode(p->um.hdc, TRANSPARENT);
            const COLORREF old_txt = SetTextColor(p->um.hdc, txt);
            RECT rc = p->dis.rcItem;
            DrawTextW(p->um.hdc, buf, -1, &rc, dt);
            SetTextColor(p->um.hdc, old_txt);
            SetBkMode(p->um.hdc, old_bk);
            if (old_font) SelectObject(p->um.hdc, old_font);
            out = TRUE;
            return true;
        }

        case WM_NCPAINT:
        case WM_NCACTIVATE: {
            out = DefWindowProcW(hwnd, msg, wp, lp);
            MENUBARINFO mbi = { sizeof(mbi) };
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
                RECT wr; GetWindowRect(hwnd, &wr);
                RECT rc = mbi.rcBar;
                OffsetRect(&rc, -wr.left, -wr.top);
                rc.top = rc.bottom;       /* the 1px line under the bar */
                rc.bottom = rc.top + 1;
                HDC hdc = GetWindowDC(hwnd);
                FillRect(hdc, &rc, bar_brush_);
                ReleaseDC(hwnd, hdc);
            }
            return true;
        }

        case WM_THEMECHANGED:
            if (g_FlushMenuThemes) g_FlushMenuThemes();
            return false;
    }
    return false;
}
