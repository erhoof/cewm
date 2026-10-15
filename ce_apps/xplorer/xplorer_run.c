#include "xplorer.h"

#define IDR_EDIT   1
#define IDR_RUN    2
#define IDR_CANCEL 3

const WCHAR kRunClass[] = L"XplorerRun";

static void RunTyped(HWND h) {
    WCHAR               cmd[MAX_PATH];
    PROCESS_INFORMATION pi;
    cmd[0] = 0;
    GetWindowTextW(GetDlgItem(h, IDR_EDIT), cmd, MAX_PATH);
    if (!cmd[0]) return;
    memset(&pi, 0, sizeof(pi));
    if (CreateProcessW(cmd, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        DestroyWindow(h);
    } else {
        MessageBoxW(h, cmd, L"Run - could not launch", MB_OK | MB_ICONERROR);
    }
}

static LRESULT CALLBACK RunProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((CREATESTRUCTW*)lp)->hInstance;
        CreateWindowExW(0, L"STATIC", L"Type a program path:",
                        WS_CHILD | WS_VISIBLE, 8, 8, 300, 16, h, NULL, hi, NULL);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                        8, 28, 304, 22, h, (HMENU)IDR_EDIT, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Run",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        160, 58, 72, 24, h, (HMENU)IDR_RUN, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                        240, 58, 72, 24, h, (HMENU)IDR_CANCEL, hi, NULL);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDR_RUN)    { RunTyped(h);    return 0; }
        if (LOWORD(wp) == IDR_CANCEL) { DestroyWindow(h); return 0; }
        return 0;

    case WM_DESTROY:
        return 0;   /* secondary window; only the desktop quits */
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL RegisterRunClass(HINSTANCE hi) {
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = RunProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kRunClass;
    return RegisterClassW(&wc) != 0;
}

void ShowRunDialog(HINSTANCE hi) {
    HWND h = CreateWindowExW(WS_EX_TOPMOST, kRunClass, L"Run",
                             WS_CAPTION | WS_SYSMENU | WS_BORDER,
                             60, 60, 330, 120, NULL, NULL, hi, NULL);
    if (h) {
        ShowWindow(h, SW_SHOW);
        UpdateWindow(h);
    }
}
