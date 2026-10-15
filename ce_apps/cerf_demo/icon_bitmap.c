#include "cerf_demo.h"

static HBITMAP DdbFromDc(HDC src, int w, int h) {
    HDC     screen = GetDC(NULL);
    HDC     dst    = CreateCompatibleDC(screen);
    HBITMAP ddb    = CreateCompatibleBitmap(screen, w, h);
    HBITMAP prev;

    if (!ddb) {
        DeleteDC(dst);
        ReleaseDC(NULL, screen);
        return NULL;
    }
    prev = (HBITMAP)SelectObject(dst, ddb);
    BitBlt(dst, 0, 0, w, h, src, 0, 0, SRCCOPY);
    SelectObject(dst, prev);
    DeleteDC(dst);
    ReleaseDC(NULL, screen);
    return ddb;
}

static void DrawGenericAppIcon(HDC dc, int w, int h, COLORREF bg) {
    RECT   r;
    HBRUSH brush;
    HPEN   pen, prev_pen;
    HGDIOBJ prev_brush;
    int    m    = w / 8;
    int    top  = h / 6;
    int    barh = h / 5;
    int    i, y, step;

    r.left = 0; r.top = 0; r.right = w; r.bottom = h;
    brush = CreateSolidBrush(bg);
    FillRect(dc, &r, brush);
    DeleteObject(brush);

    r.left = m; r.top = top; r.right = w - m; r.bottom = h - m;
    brush = CreateSolidBrush(RGB(252, 252, 255));
    FillRect(dc, &r, brush);
    DeleteObject(brush);

    r.bottom = top + barh;
    brush = CreateSolidBrush(RGB(56, 84, 150));
    FillRect(dc, &r, brush);
    DeleteObject(brush);

    pen        = CreatePen(PS_SOLID, 1, RGB(72, 72, 80));
    prev_pen   = (HPEN)SelectObject(dc, pen);
    prev_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, m, top, w - m, h - m);
    SelectObject(dc, prev_brush);
    SelectObject(dc, prev_pen);
    DeleteObject(pen);

    step  = h / 8;
    brush = CreateSolidBrush(RGB(148, 148, 156));
    for (i = 0; i < 3; i++) {
        y = top + barh + step / 2 + i * step;
        if (y + 1 >= h - m) break;
        r.left = m + 3; r.right = w - m - 3;
        r.top  = y;     r.bottom = y + 1;
        FillRect(dc, &r, brush);
    }
    DeleteObject(brush);
}

HBITMAP MakeGenericAppIconDdb(int w, int h, COLORREF bg) {
    HDC     screen = GetDC(NULL);
    HDC     memdc  = CreateCompatibleDC(screen);
    HBITMAP work   = CreateCompatibleBitmap(screen, w, h);
    HBITMAP prev, ddb;

    if (!work) {
        DeleteDC(memdc);
        ReleaseDC(NULL, screen);
        return NULL;
    }
    prev = (HBITMAP)SelectObject(memdc, work);
    DrawGenericAppIcon(memdc, w, h, bg);
    ddb = DdbFromDc(memdc, w, h);
    SelectObject(memdc, prev);
    DeleteObject(work);
    DeleteDC(memdc);
    ReleaseDC(NULL, screen);
    return ddb;
}
