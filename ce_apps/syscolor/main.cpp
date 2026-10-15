#include <windows.h>

typedef BOOL (WINAPI *SetSysColorsFn)(int, CONST INT*, CONST COLORREF*);

#define ID_PICK    100
#define ID_NAME    101
#define ID_RLBL    108
#define ID_GLBL    109
#define ID_BLBL    110
#define ID_R       111
#define ID_G       112
#define ID_B       113
#define ID_APPLY   120
#define ID_STATUS  130

#define MENU_BASE  1000

typedef struct { LPCWSTR name; int id; } ColorSlot;

static const ColorSlot kSlots[] = {
    { L"COLOR_SCROLLBAR",          COLOR_SCROLLBAR },
    { L"COLOR_BACKGROUND",         COLOR_BACKGROUND },
    { L"COLOR_ACTIVECAPTION",      COLOR_ACTIVECAPTION },
    { L"COLOR_INACTIVECAPTION",    COLOR_INACTIVECAPTION },
    { L"COLOR_MENU",               COLOR_MENU },
    { L"COLOR_WINDOW",             COLOR_WINDOW },
    { L"COLOR_WINDOWFRAME",        COLOR_WINDOWFRAME },
    { L"COLOR_MENUTEXT",           COLOR_MENUTEXT },
    { L"COLOR_WINDOWTEXT",         COLOR_WINDOWTEXT },
    { L"COLOR_CAPTIONTEXT",        COLOR_CAPTIONTEXT },
    { L"COLOR_ACTIVEBORDER",       COLOR_ACTIVEBORDER },
    { L"COLOR_INACTIVEBORDER",     COLOR_INACTIVEBORDER },
    { L"COLOR_APPWORKSPACE",       COLOR_APPWORKSPACE },
    { L"COLOR_HIGHLIGHT",          COLOR_HIGHLIGHT },
    { L"COLOR_HIGHLIGHTTEXT",      COLOR_HIGHLIGHTTEXT },
    { L"COLOR_BTNFACE",            COLOR_BTNFACE },
    { L"COLOR_BTNSHADOW",          COLOR_BTNSHADOW },
    { L"COLOR_GRAYTEXT",           COLOR_GRAYTEXT },
    { L"COLOR_BTNTEXT",            COLOR_BTNTEXT },
    { L"COLOR_INACTIVECAPTIONTEXT",COLOR_INACTIVECAPTIONTEXT },
    { L"COLOR_BTNHIGHLIGHT",       COLOR_BTNHIGHLIGHT },
    { L"COLOR_3DDKSHADOW",         COLOR_3DDKSHADOW },
    { L"COLOR_3DLIGHT",            COLOR_3DLIGHT },
    { L"COLOR_INFOTEXT",           COLOR_INFOTEXT },
    { L"COLOR_INFOBK",             COLOR_INFOBK },
    { L"index 25",                 25 },
    { L"index 26",                 26 },
    { L"index 27",                 27 },
    { L"index 28",                 28 },
    { L"index 29",                 29 },
    { L"index 30",                 30 },
    { L"index 31",                 31 },
    { L"index 32",                 32 },
    { L"index 33",                 33 },
    { L"index 34",                 34 },
    { L"index 35",                 35 },
    { L"index 36",                 36 },
    { L"index 37",                 37 },
    { L"index 38",                 38 },
    { L"index 39",                 39 },
};
static const int kNumSlots = (int)(sizeof(kSlots) / sizeof(kSlots[0]));

typedef struct {
    HWND           hwnd;
    int            sel;
    COLORREF       swatch;
    SetSysColorsFn set;
    RECT           swatch_rc;
} AppState;

#define WNDX_STATE 0

static DWORD ParseByte(HWND hwnd, int id) {
    WCHAR b[16];
    DWORD v = 0;
    const WCHAR* s = b;
    GetDlgItemText(hwnd, id, b, 16);
    for (; *s >= L'0' && *s <= L'9'; ++s) v = v * 10 + (DWORD)(*s - L'0');
    if (v > 255) v = 255;
    return v;
}

static void SetByte(HWND hwnd, int id, BYTE v) {
    WCHAR b[8];
    wsprintfW(b, L"%u", (unsigned)v);
    SetDlgItemText(hwnd, id, b);
}

static void SetStatus(AppState* st, LPCWSTR text) {
    SetDlgItemText(st->hwnd, ID_STATUS, text);
}

static void LoadSelected(AppState* st) {
    const ColorSlot* s = &kSlots[st->sel];
    COLORREF c = GetSysColor(s->id);
    WCHAR    line[96];
    st->swatch = c;
    SetByte(st->hwnd, ID_R, GetRValue(c));
    SetByte(st->hwnd, ID_G, GetGValue(c));
    SetByte(st->hwnd, ID_B, GetBValue(c));
    wsprintfW(line, L"%s  (index %d)", s->name, s->id & 0xFF);
    SetDlgItemText(st->hwnd, ID_NAME, line);
    wsprintfW(line, L"GetSysColor = 0x%08X  (R=%u G=%u B=%u)",
              (unsigned)c, GetRValue(c), GetGValue(c), GetBValue(c));
    SetStatus(st, line);
    InvalidateRect(st->hwnd, &st->swatch_rc, TRUE);
}

static void PreviewSwatch(AppState* st) {
    st->swatch = RGB(ParseByte(st->hwnd, ID_R),
                     ParseByte(st->hwnd, ID_G),
                     ParseByte(st->hwnd, ID_B));
    InvalidateRect(st->hwnd, &st->swatch_rc, TRUE);
}

static void DumpAllToFile(AppState* st) {
    char buf[16];
    int  i, j;
    DWORD wr;
    COLORREF c;
    static const char hx[] = "0123456789ABCDEF";
    WCHAR path[MAX_PATH];
    WCHAR* p;
    const WCHAR* name;
    HANDLE f;
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        SetStatus(st, L"Dump FAILED: cannot resolve module path");
        return;
    }
    for (p = path + n; p > path && p[-1] != L'\\' && p[-1] != L'/'; --p) {}
    for (name = L"syscolor.out"; *name && (DWORD)(p - path) < MAX_PATH - 1; ) *p++ = *name++;
    *p = 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        SetStatus(st, L"Dump FAILED: cannot open syscolor.out");
        return;
    }
    for (i = 0; i < kNumSlots; ++i) {
        st->sel = i;
        LoadSelected(st);
        c = st->swatch;
        buf[0] = '0'; buf[1] = 'x';
        for (j = 0; j < 8; ++j) buf[2 + j] = hx[((DWORD)c >> ((7 - j) * 4)) & 0xF];
        buf[10] = '\r'; buf[11] = '\n';
        WriteFile(f, buf, 12, &wr, NULL);
    }
    CloseHandle(f);
    SetStatus(st, L"Dumped colours -> syscolor.out");
}

static void OnPick(AppState* st) {
    HMENU menu = CreatePopupMenu();
    RECT  rc;
    int   i, cmd;
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, (UINT)(MENU_BASE + kNumSlots), L"** DUMP ALL 0..29 TO FILE **");
    for (i = 0; i < kNumSlots; ++i)
        AppendMenuW(menu, MF_STRING, (UINT)(MENU_BASE + i), kSlots[i].name);
    GetWindowRect(GetDlgItem(st->hwnd, ID_PICK), &rc);
    cmd = (int)TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                              rc.left, rc.bottom, 0, st->hwnd, NULL);
    DestroyMenu(menu);
    if (cmd == MENU_BASE + kNumSlots) {
        DumpAllToFile(st);
    } else if (cmd >= MENU_BASE && cmd < MENU_BASE + kNumSlots) {
        st->sel = cmd - MENU_BASE;
        LoadSelected(st);
    }
}

static void OnApply(AppState* st) {
    const ColorSlot* s;
    COLORREF rgb;
    int      elem;
    WCHAR    line[128];
    if (st->sel < 0) {
        SetStatus(st, L"Pick a colour slot first.");
        return;
    }
    if (!st->set) {
        SetStatus(st, L"SetSysColors is not exported by this device's coredll.");
        return;
    }
    s    = &kSlots[st->sel];
    rgb  = RGB(ParseByte(st->hwnd, ID_R),
               ParseByte(st->hwnd, ID_G),
               ParseByte(st->hwnd, ID_B));
    elem = s->id;
    if (st->set(1, &elem, &rgb)) {
        COLORREF now = GetSysColor(s->id);
        st->swatch = now;
        SetByte(st->hwnd, ID_R, GetRValue(now));
        SetByte(st->hwnd, ID_G, GetGValue(now));
        SetByte(st->hwnd, ID_B, GetBValue(now));
        wsprintfW(line, L"Applied. GetSysColor now = 0x%08X (R=%u G=%u B=%u)",
                  (unsigned)now, GetRValue(now), GetGValue(now), GetBValue(now));
        SetStatus(st, line);
        InvalidateRect(st->hwnd, &st->swatch_rc, TRUE);
    } else {
        SetStatus(st, L"SetSysColors returned FALSE (slot not changed).");
    }
}

static void Layout(AppState* st) {
    HWND h = st->hwnd;
    RECT rc;
    int  m = 6, x, y, w, cell, lbl = 14;
    GetClientRect(h, &rc);
    x = m; y = m; w = rc.right - 2 * m;
    #define MW(id, mx, my, mw, mh) MoveWindow(GetDlgItem(h, id), mx, my, mw, mh, TRUE)
    MW(ID_PICK, x, y, w, 22);                              y += 26;
    MW(ID_NAME, x, y, w, 14);                              y += 18;
    cell = w / 3;
    MW(ID_RLBL, x,                  y + 2, lbl, 16);
    MW(ID_R,    x + lbl,            y,     cell - lbl - 4, 20);
    MW(ID_GLBL, x + cell,           y + 2, lbl, 16);
    MW(ID_G,    x + cell + lbl,     y,     cell - lbl - 4, 20);
    MW(ID_BLBL, x + 2 * cell,       y + 2, lbl, 16);
    MW(ID_B,    x + 2 * cell + lbl, y,     cell - lbl - 4, 20);
    y += 24;
    MW(ID_APPLY, x, y, 64, 22);
    st->swatch_rc.left   = x + 72;
    st->swatch_rc.top    = y;
    st->swatch_rc.right  = x + w;
    st->swatch_rc.bottom = y + 22;                        y += 26;
    MW(ID_STATUS, x, y, w, 34);
    #undef MW
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* st = (AppState*)GetWindowLongW(hwnd, WNDX_STATE);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        AppState* s  = (AppState*)cs->lpCreateParams;
        HINSTANCE hi = cs->hInstance;
        HMODULE   core;
        SetWindowLongW(hwnd, WNDX_STATE, (LONG)s);
        s->hwnd = hwnd;

        CreateWindowExW(0, L"BUTTON", L"Pick colour slot...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_PICK, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"(no slot selected)", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)ID_NAME, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"R:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)ID_RLBL, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"G:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)ID_GLBL, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"B:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)ID_BLBL, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
            ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_R, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
            ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_G, hi, NULL);
        CreateWindowExW(0, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
            ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, (HMENU)ID_B, hi, NULL);
        CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_APPLY, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"Pick a slot to read its system colour.",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)ID_STATUS, hi, NULL);

        core    = LoadLibraryW(L"coredll.dll");
        s->set  = core ? (SetSysColorsFn)GetProcAddressW(core, L"SetSysColors") : NULL;
        Layout(s);
        return 0;
    }

    case WM_SIZE:
        Layout(st);
        return 0;

    case WM_COMMAND:
        if (!st) return 0;
        if (LOWORD(wp) == ID_PICK  && HIWORD(wp) == BN_CLICKED) { OnPick(st);  return 0; }
        if (LOWORD(wp) == ID_APPLY && HIWORD(wp) == BN_CLICKED) { OnApply(st); return 0; }
        if ((LOWORD(wp) == ID_R || LOWORD(wp) == ID_G || LOWORD(wp) == ID_B) &&
            HIWORD(wp) == EN_CHANGE) { PreviewSwatch(st); return 0; }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC     dc  = BeginPaint(hwnd, &ps);
        HBRUSH  br  = CreateSolidBrush(st ? st->swatch : RGB(255, 255, 255));
        RECT    b   = st->swatch_rc;
        HGDIOBJ obr = SelectObject(dc, br);
        HGDIOBJ opn = SelectObject(dc, GetStockObject(BLACK_PEN));
        Rectangle(dc, b.left, b.top, b.right, b.bottom);
        SelectObject(dc, obr);
        SelectObject(dc, opn);
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev,
                              LPWSTR cmd, int show) {
    static AppState st;
    WNDCLASSW wc;
    HWND      hwnd;
    MSG       m;

    (void)hPrev; (void)cmd;
    memset(&st, 0, sizeof(st));
    st.sel    = -1;
    st.swatch = RGB(255, 255, 255);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.cbWndExtra    = sizeof(LONG);
    wc.hInstance     = hInstance;
    wc.hIcon         = NULL;
#ifdef IDC_ARROW
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
#else
    wc.hCursor       = NULL;
#endif
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"CerfSysColor";
    if (!RegisterClassW(&wc)) return 1;

    {
        DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_BORDER |
                      WS_CLIPCHILDREN | WS_VISIBLE;
        RECT  wr;
        int   ww, wh, sx, sy;
        wr.left = 0; wr.top = 0; wr.right = 200; wr.bottom = 150;
        AdjustWindowRectEx(&wr, style, FALSE, 0);
        ww = wr.right - wr.left; wh = wr.bottom - wr.top;
        sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2; if (sx < 0) sx = 0;
        sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2; if (sy < 0) sy = 0;
        hwnd = CreateWindowExW(0, L"CerfSysColor", L"SysColor", style,
                               sx, sy, ww, wh, NULL, NULL, hInstance, &st);
    }
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
