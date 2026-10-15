#include "xplorer.h"

#define ID_DESKVIEW 1
#define DESKTOP_DIR L"\\Windows\\Desktop"
#define MY_DEVICE   L"My Device"

const WCHAR kDesktopClass[] = L"XplorerDesktop";

static LRESULT CALLBACK DesktopProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCTW*)lp)->hInstance;
        RECT      rc;
        HWND      v = CreateWindowExW(0, kViewClass, L"",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                      0, 0, 0, 0, h, (HMENU)ID_DESKVIEW, hi, NULL);
        GetClientRect(h, &rc);
        if (v) {
            /* Size the view now - a WS_POPUP gets no reliable WM_SIZE on create. */
            MoveWindow(v, 0, 0, rc.right, rc.bottom, TRUE);
            SendMessageW(v, XVM_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_DESKTOP));
            SendMessageW(v, XVM_SETPATH,    0, (LPARAM)DESKTOP_DIR);
            SendMessageW(v, XVM_ADDSPECIAL, 0, (LPARAM)MY_DEVICE);
        }
        return 0;
    }

    case WM_SIZE: {
        HWND v = GetDlgItem(h, ID_DESKVIEW);
        if (v) MoveWindow(v, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        MoveWindow(h, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                   GetSystemMetrics(SM_CYSCREEN) - TASKBAR_H, TRUE);
        return 0;

    case XVN_DESCEND: {
        const WCHAR* name = (const WCHAR*)lp;
        HINSTANCE    hi   = GetModuleHandleW(NULL);
        if (XStrEq(name, MY_DEVICE)) {
            OpenExplorer(hi, L"\\");
        } else {
            WCHAR full[MAX_PATH];
            XJoinPath(DESKTOP_DIR, name, full);
            OpenExplorer(hi, full);
        }
        return 0;
    }

    case XVN_LAUNCH: {
        WCHAR full[MAX_PATH];
        XJoinPath(DESKTOP_DIR, (const WCHAR*)lp, full);
        LaunchExe(full);
        return 0;
    }

    case WM_ACTIVATE:
        /* CE 2.11 has no WM_MOUSEACTIVATE / WM_WINDOWPOSCHANGING to veto a raise,
           so on activation (e.g. a desktop click) push the desktop back to the
           bottom of the z-order; otherwise it covers the explorer windows. */
        if (LOWORD(wp) != 0)   /* != WA_INACTIVE */
            SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);   /* the desktop is the primary shell window */
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL RegisterDesktopClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = DesktopProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    wc.lpszClassName = kDesktopClass;
    return RegisterClassW(&wc) != 0;
}

HWND CreateDesktopWindow(HINSTANCE hi) {
    int  sw = GetSystemMetrics(SM_CXSCREEN);
    int  sh = GetSystemMetrics(SM_CYSCREEN);
    HWND d  = CreateWindowExW(0, kDesktopClass, L"",
                              WS_POPUP | WS_VISIBLE,
                              0, 0, sw, sh - TASKBAR_H,
                              NULL, NULL, hi, NULL);
    if (d)
        SetWindowPos(d, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return d;
}
