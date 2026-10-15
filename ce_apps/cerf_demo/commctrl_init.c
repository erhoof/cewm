#include "cerf_demo.h"
#include <commctrl.h>

typedef BOOL (WINAPI *PfnInitCommonControlsEx)(LPINITCOMMONCONTROLSEX);
typedef void (WINAPI *PfnInitCommonControls)(void);

static HMODULE g_commctrl;

int EnsureCommonControls(void) {
    PfnInitCommonControlsEx ex;
    PfnInitCommonControls   plain;
    INITCOMMONCONTROLSEX    icex;

    if (g_commctrl) return 1;
    g_commctrl = LoadLibrary(TEXT("commctrl.dll"));
    if (!g_commctrl) return 0;

    ex = (PfnInitCommonControlsEx)GetProcAddress(g_commctrl,
                                                 TEXT("InitCommonControlsEx"));
    if (ex) {
        icex.dwSize = sizeof(icex);
        icex.dwICC  = ICC_LISTVIEW_CLASSES;
        ex(&icex);
        return 1;
    }
    plain = (PfnInitCommonControls)GetProcAddress(g_commctrl,
                                                  TEXT("InitCommonControls"));
    if (plain) plain();
    return 1;
}
