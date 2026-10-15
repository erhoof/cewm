#include "xplorer.h"

void DrawFolderIcon(HDC dc, int x, int y) {
    HBRUSH  body = CreateSolidBrush(RGB(255, 206, 84));
    HPEN    edge = CreatePen(PS_SOLID, 1, RGB(170, 130, 20));
    HGDIOBJ ob   = SelectObject(dc, body);
    HGDIOBJ op   = SelectObject(dc, edge);
    POINT   tab[4];

    tab[0].x = x + 2;  tab[0].y = y + 6;
    tab[1].x = x + 10; tab[1].y = y + 6;
    tab[2].x = x + 14; tab[2].y = y + 10;
    tab[3].x = x + 2;  tab[3].y = y + 10;
    Polygon(dc, tab, 4);
    Rectangle(dc, x + 2, y + 9, x + ICON_W - 2, y + ICON_H - 4);

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(body);
    DeleteObject(edge);
}

void DrawFileIcon(HDC dc, int x, int y) {
    HBRUSH  body = CreateSolidBrush(RGB(255, 255, 255));
    HPEN    edge = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    HGDIOBJ ob   = SelectObject(dc, body);
    HGDIOBJ op   = SelectObject(dc, edge);
    int     l = x + 6, t = y + 2, r = x + ICON_W - 6, b = y + ICON_H - 2;
    int     fold = 8, ln;
    POINT   page[6], f[3];

    page[0].x = l;        page[0].y = t;
    page[1].x = r - fold; page[1].y = t;
    page[2].x = r;        page[2].y = t + fold;
    page[3].x = r;        page[3].y = b;
    page[4].x = l;        page[4].y = b;
    page[5].x = l;        page[5].y = t;
    Polygon(dc, page, 6);

    f[0].x = r - fold; f[0].y = t;
    f[1].x = r - fold; f[1].y = t + fold;
    f[2].x = r;        f[2].y = t + fold;
    Polygon(dc, f, 3);

    for (ln = 0; ln < 3; ln++) {
        int   ly = t + fold + 4 + ln * 5;
        POINT seg[2];
        seg[0].x = l + 4; seg[0].y = ly;
        seg[1].x = r - 4; seg[1].y = ly;
        Polyline(dc, seg, 2);
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(body);
    DeleteObject(edge);
}

/* ExtractIconExW lives in the device's coredll on CE 5.0 (Zune) but not in every
   stripped build, and a static import would bind a CE 2.11 ordinal that need not
   match at runtime. Resolve it by name from the loaded coredll once; a NULL fn
   just means callers fall back to the drawn glyph. */
typedef UINT (WINAPI *ExtractIconExW_fn)(LPCWSTR, int, HICON*, HICON*, UINT);

static ExtractIconExW_fn ResolveExtractIconEx(void) {
    static ExtractIconExW_fn fn = NULL;
    static int               tried = 0;
    if (!tried) {
        HMODULE m = GetModuleHandleW(L"coredll.dll");
        if (m) fn = (ExtractIconExW_fn)GetProcAddressW(m, L"ExtractIconExW");
        tried = 1;
    }
    return fn;
}

HICON XExtractExeIcon(const WCHAR* fullpath) {
    ExtractIconExW_fn fn    = ResolveExtractIconEx();
    HICON             large = NULL;
    if (!fn) return NULL;
    return (fn(fullpath, 0, &large, NULL, 1) >= 1) ? large : NULL;
}
