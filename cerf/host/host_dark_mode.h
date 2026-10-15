#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

/* Dark menu bar/popups via undocumented uxtheme dark-mode ordinals + the
   undocumented UAH menu messages - best-effort, no-ops on an OS that
   doesn't send them. */

class HostDarkMode : public Service {
public:
    using Service::Service;
    ~HostDarkMode() override;

    void Init();                 /* once: load ordinals, app dark mode */
    void ApplyToWindow(HWND h);  /* per top-level + child window */

    /* One-shot dialog theming: dark title bar + dark-theme every child control +
       apply the modern UI font. Safe to call regardless of OS dark support -
       the font is applied always; dark theming only when the OS supports it. */
    void ApplyToDialog(HWND dlg);

    /* WM_CTLCOLOR{DLG,STATIC,BTN,EDIT,LISTBOX} handler: sets dark text/background
       on `(HDC)wp` and returns the matching dark brush in `out`. Returns false
       (caller falls back to default light) when the OS has no dark support. */
    bool HandleCtlColor(UINT msg, WPARAM wp, LRESULT& out);

    bool HandleLinkCustomDraw(LPARAM syslink_notify, LRESULT& out);

    /* Fills the window's client area with the dark dialog brush, for a
       WM_ERASEBKGND handler. Returns false in light mode (use the class brush). */
    bool EraseBackground(HDC hdc, HWND hwnd);

    /* For custom-painted windows (e.g. the shutdown screen) that fill their own
       client area: dark when the OS supports it, else caller keeps light. */
    bool     IsDark() const { return inited_; }
    HBRUSH   BgBrush();            /* dialog background brush (dark) */
    COLORREF BgColor() const;     /* dialog background colour (dark) */
    COLORREF TextColor() const;   /* dark-mode foreground text colour */
    HFONT    UiFont();            /* modern UI font (system message font) */

    /* Returns true (and fills `out`) when it fully handled the message. */
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);

private:
    void EnsureResources();
    void EnsureUiFont();
    static BOOL CALLBACK ThemeChildProc(HWND child, LPARAM self);

    bool   inited_ = false;
    HBRUSH bar_brush_ = nullptr;
    HBRUSH hot_brush_ = nullptr;
    HBRUSH sel_brush_ = nullptr;
    HBRUSH dlg_brush_ = nullptr;
    HBRUSH edit_brush_ = nullptr;
    HFONT  menu_font_ = nullptr;
    HFONT  ui_font_ = nullptr;
};
