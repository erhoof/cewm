/* CERF blank sample app: a small framed window with the CERF logo and two
   buttons (MsgBox / Exit). Smallest CE userspace EXE in this tree. */

#include <windows.h>

#include "cerf_icon_data.h"

#define ID_MSGBOX 201
#define ID_EXIT   202
#define ID_EDIT   203

/* Fixed client size - small, frame-bordered, never maximized. */
#define CLIENT_W 200
#define CLIENT_H 170

/* CE 2.11 GDI exposes neither StretchDIBits nor SetDIBitsToDevice; a
   DIBSection memory DC + BitBlt is the only blit path for embedded bits. */
static void DrawCerfIcon(HDC dc, int x, int y) {
    BITMAPINFO bi;
    void*   bits = NULL;
    HDC     mem;
    HBITMAP bmp, old;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = CERF_ICON_W;
    bi.bmiHeader.biHeight      = CERF_ICON_H;   /* positive => bottom-up */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    mem = CreateCompatibleDC(dc);
    if (!mem) return;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
        memcpy(bits, kCerfIconBgr, sizeof(kCerfIconBgr));
        old = (HBITMAP)SelectObject(mem, bmp);
        BitBlt(dc, x, y, CERF_ICON_W, CERF_ICON_H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
}

/* Centre the logo at the top and stack the two buttons beneath it, sized to
   whatever client rect the frame ended up giving us. */
static void LayoutControls(HWND hwnd) {
    RECT rc;
    int  cx, btnw = 120, btnh = 26, y;
    GetClientRect(hwnd, &rc);
    cx = (rc.right - btnw) / 2;
    if (cx < 4) cx = 4;

    y = CERF_ICON_H + 24;
    MoveWindow(GetDlgItem(hwnd, ID_MSGBOX), cx, y, btnw, btnh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_EXIT),   cx, y + btnh + 10, btnw, btnh, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_EDIT),   cx, y + 2 * (btnh + 10), btnw, 22, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCTW*)lp)->hInstance;
        CreateWindowExW(0, L"BUTTON", L"MsgBox",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)ID_MSGBOX, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Exit",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        0, 0, 0, 0, hwnd, (HMENU)ID_EXIT, hi, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        0, 0, 0, 0, hwnd, (HMENU)ID_EDIT, hi, NULL);
        LayoutControls(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC  dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        DrawCerfIcon(dc, (rc.right - CERF_ICON_W) / 2, 12);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_MSGBOX) {
            MessageBoxW(hwnd,
                        L"This is the CERF blank sample application.",
                        L"CERF BlankApp",
                        MB_ICONINFORMATION | MB_YESNOCANCEL);
            return 0;
        }
        if (LOWORD(wp) == ID_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* extern "C": /entry:WinMain needs an unmangled symbol; this file is C++. */
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                              LPWSTR cmd, int show) {
    WNDCLASSW wc;
    HWND      hwnd;
    MSG       m;
    RECT      r;
    int       w, h;

    (void)hPrev; (void)cmd;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"CerfBlankApp";
    if (!RegisterClassW(&wc)) return 1;

    /* Grow the requested client size by the frame so the visible client is
       CLIENT_W x CLIENT_H. WS_CAPTION | WS_SYSMENU | WS_BORDER gives a small
       framed, non-maximized window with a title bar and close box. */
    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRectEx(&r, WS_CAPTION | WS_SYSMENU | WS_BORDER, FALSE, 0);
    w = r.right - r.left;
    h = r.bottom - r.top;

    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"CerfBlankApp", L"CERF BlankApp",
                           WS_CAPTION | WS_SYSMENU | WS_BORDER,
                           20, 20, w, h,
                           NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
