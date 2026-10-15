#pragma once

#include "../core/service.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

/* The host window's hardware screen: the text-mode RX/TX surface for guest UART
   / OEM debug output and CERF's own user-facing notices (power events,
   save/restore progress), plus the bottom CPU-activity bar. */
class HwScreen : public Service {
public:
    using Service::Service;
    ~HwScreen() override;

    /* Push one completed debug-console line. Trailing newline characters are
       stripped by the caller. Lines beyond kMaxLines drop the oldest. */
    void AddLine(std::string_view line);

    std::string LastLine() const;

    /* Render into the host DIB. dc wraps the same pixels for GDI text drawing;
       dib_bgra32 / width / height describe the raw surface. */
    void RenderInto(HDC dc, uint32_t* dib_bgra32, uint32_t width, uint32_t height);

private:
    static constexpr size_t kMaxLines = 500;

    mutable std::mutex      mtx_;
    std::deque<std::string> lines_;
    uint64_t                gen_ = 0;

    HFONT font_cache_[2] = { nullptr, nullptr };

    HDC       bg_dc_    = nullptr;
    HBITMAP   bg_dib_   = nullptr;
    uint32_t* bg_bits_  = nullptr;
    int       bg_w_     = 0;
    int       bg_h_     = 0;
    uint64_t  bg_gen_   = ~0ull;
    bool      bg_valid_ = false;

    void RebuildBgDib(int w, int h);
    void DrawLog(HDC dc, uint32_t width, uint32_t height,
                 const std::vector<std::string>& lines);
};
