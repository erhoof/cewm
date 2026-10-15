#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

class ModalDialog : public Service {
public:
    using Service::Service;

protected:
    void RunModal(HWND owner, const wchar_t* class_name, const wchar_t* title,
                  int client_w, int client_h);

    void Finish() { done_ = true; }
    void PostDismiss();

    HWND Hwnd() const { return hwnd_; }
    bool IsOpen() const { return hwnd_ != nullptr; }

    UINT Dpi() const { return dpi_; }
    int  S(int v) const { return MulDiv(v, (int)dpi_, USER_DEFAULT_SCREEN_DPI); }

    virtual bool DpiScaledLayout() const                 { return false; }

    virtual void BuildControls(HWND hwnd)                = 0;
    virtual void OnShown()                               {}
    virtual void OnPaint(HDC)                            {}
    virtual void OnCommand(int, int)                     {}
    virtual bool OnDrawItem(const DRAWITEMSTRUCT*)       { return false; }
    virtual bool OnMessage(UINT, WPARAM, LPARAM)         { return false; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    static BOOL CALLBACK SetChildFontProc(HWND, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);
    void    ApplyDpiFont();

    HWND  hwnd_     = nullptr;
    bool  done_     = false;
    UINT  dpi_      = USER_DEFAULT_SCREEN_DPI;
    HFONT dpi_font_ = nullptr;
};
