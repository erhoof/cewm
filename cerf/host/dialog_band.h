#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

namespace Gdiplus { class Bitmap; }

class DialogBand : public Service {
public:
    using Service::Service;

    void OnShutdown() override;

    int  PixelWidth(UINT dpi);
    int  PixelHeight(UINT dpi);
    void Paint(HDC dc, UINT dpi, int origin_x, int origin_y);

private:
    static constexpr int kTierCount = 5;

    Gdiplus::Bitmap* ForDpi(UINT dpi);

    Gdiplus::Bitmap* tiers_[kTierCount] = {};
};
