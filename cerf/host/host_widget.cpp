#include "host_widget.h"

#include <cstdint>

namespace {
constexpr int      kGlowTicks = 3;                 /* ~300 ms at the 100 ms tick */
constexpr COLORREF kRxColor   = RGB(229, 80, 80);  /* red - input  (rx, left dot)  */
constexpr COLORREF kTxColor   = RGB(78, 201, 90);  /* green - output (tx, right dot) */
constexpr int      kDotR      = 3;
constexpr BYTE     kDisabledAlpha = 150;           /* gray blended over a disabled icon */

void DimRect(HDC dc, const RECT& box, COLORREF bar_bg) {
    HDC mdc = CreateCompatibleDC(dc);
    if (!mdc) return;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = 1;
    bi.bmiHeader.biHeight      = 1;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        /* BGRA32 DIB pixel = 0x00RRGGBB; fade the icon toward the bar bg. */
        *static_cast<uint32_t*>(bits) = ((uint32_t)GetRValue(bar_bg) << 16) |
                                        ((uint32_t)GetGValue(bar_bg) << 8) |
                                        (uint32_t)GetBValue(bar_bg);
        HGDIOBJ ob = SelectObject(mdc, dib);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, kDisabledAlpha, 0 };
        AlphaBlend(dc, box.left, box.top, box.right - box.left,
                   box.bottom - box.top, mdc, 0, 0, 1, 1, bf);
        SelectObject(mdc, ob);
        DeleteObject(dib);
    }
    DeleteDC(mdc);
}
}  /* namespace */

void HostWidget::DrawChipIcon(HDC dc, const RECT& box) {
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    const RECT body = { cx - 8, cy - 6, cx + 8, cy + 6 };

    HBRUSH  fill = CreateSolidBrush(RGB(40, 44, 52));
    HPEN    pen  = CreatePen(PS_SOLID, 1, RGB(150, 150, 160));
    HGDIOBJ ob   = SelectObject(dc, fill);
    HGDIOBJ op   = SelectObject(dc, pen);
    Rectangle(dc, body.left, body.top, body.right, body.bottom);
    for (int i = -1; i <= 1; ++i) {
        const int px = cx + i * 5;
        MoveToEx(dc, px, body.top - 2, nullptr);    LineTo(dc, px, body.top);
        MoveToEx(dc, px, body.bottom - 1, nullptr); LineTo(dc, px, body.bottom + 1);
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(fill);
    DeleteObject(pen);
}

bool HostWidget::SampleActivity() {
    if (rx_pending_.exchange(false, std::memory_order_relaxed)) rx_glow_ = kGlowTicks;
    else if (rx_glow_ > 0)                                      --rx_glow_;
    if (tx_pending_.exchange(false, std::memory_order_relaxed)) tx_glow_ = kGlowTicks;
    else if (tx_glow_ > 0)                                      --tx_glow_;

    const bool now     = (rx_glow_ > 0) || (tx_glow_ > 0);
    const bool repaint = now || was_active_;
    was_active_ = now;
    return repaint;
}

void HostWidget::DrawComposited(HDC dc, const RECT& box, COLORREF bar_bg) {
    DrawIcon(dc, box);

    if (!IsEnabled()) {
        DimRect(dc, box, bar_bg);
        return;   /* disabled => no activity dots */
    }

    auto dot = [&](COLORREF c, int cx, int cy) {
        HBRUSH  br = CreateSolidBrush(c);
        HPEN    pn = CreatePen(PS_SOLID, 1, RGB(20, 20, 20));
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, pn);
        Ellipse(dc, cx - kDotR, cy - kDotR, cx + kDotR + 1, cy + kDotR + 1);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(br);
        DeleteObject(pn);
    };
    if (rx_glow_ > 0) dot(kRxColor, box.left  + kDotR + 1, box.bottom - kDotR - 2);
    if (tx_glow_ > 0) dot(kTxColor, box.right - kDotR - 2, box.bottom - kDotR - 2);
}
