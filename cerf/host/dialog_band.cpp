#define NOMINMAX
#include "dialog_band.h"

#include <gdiplus.h>

#include "../core/cerf_emulator.h"
#include "host_gdiplus.h"

REGISTER_SERVICE(DialogBand);

namespace {

constexpr int kBandDipW = 400;
constexpr int kBandDipH = 112;

struct BandTier {
    int            max_pct;
    const wchar_t* resource;
};

constexpr BandTier kTiers[] = {
    { 100, L"ABOUT_BAND_100" },
    { 125, L"ABOUT_BAND_125" },
    { 150, L"ABOUT_BAND_150" },
    { 200, L"ABOUT_BAND_200" },
    { 300, L"ABOUT_BAND_300" },
};

int TierIndexForDpi(UINT dpi) {
    const int pct   = MulDiv(100, (int)dpi, USER_DEFAULT_SCREEN_DPI);
    const int count = (int)(sizeof(kTiers) / sizeof(kTiers[0]));
    for (int i = 0; i < count - 1; ++i)
        if (pct <= kTiers[i].max_pct) return i;
    return count - 1;
}

}  /* namespace */

void DialogBand::OnShutdown() {
    for (auto*& b : tiers_) {
        delete b;
        b = nullptr;
    }
}

Gdiplus::Bitmap* DialogBand::ForDpi(UINT dpi) {
    const int idx = TierIndexForDpi(dpi);
    if (!tiers_[idx])
        tiers_[idx] = emu_.Get<HostGdiPlus>().DecodeResourcePng(kTiers[idx].resource);
    return tiers_[idx];
}

int DialogBand::PixelWidth(UINT dpi) {
    Gdiplus::Bitmap* b = ForDpi(dpi);
    const int w = b ? (int)b->GetWidth() : 0;
    return w > 0 ? w : MulDiv(kBandDipW, (int)dpi, USER_DEFAULT_SCREEN_DPI);
}

int DialogBand::PixelHeight(UINT dpi) {
    Gdiplus::Bitmap* b = ForDpi(dpi);
    const int h = b ? (int)b->GetHeight() : 0;
    return h > 0 ? h : MulDiv(kBandDipH, (int)dpi, USER_DEFAULT_SCREEN_DPI);
}

void DialogBand::Paint(HDC dc, UINT dpi, int origin_x, int origin_y) {
    Gdiplus::Bitmap* b = ForDpi(dpi);
    if (!b) return;
    const UINT bw = b->GetWidth(), bh = b->GetHeight();
    if (bw == 0 || bh == 0) return;

    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Rect dst(-origin_x, -origin_y, (int)bw, (int)bh);
    g.DrawImage(b, dst, 0, 0, (int)bw, (int)bh, Gdiplus::UnitPixel);
}
