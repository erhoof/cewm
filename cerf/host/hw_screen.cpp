#define NOMINMAX

#include "hw_screen.h"

#include "../core/cerf_emulator.h"
#include "boot_bar.h"
#include "boot_screen.h"
#include "host_fonts.h"

#include <algorithm>
#include <cstring>

REGISTER_SERVICE(HwScreen);

namespace {

constexpr int      kEdgeMarginPx = 1;
constexpr COLORREF kLogTextColor   = RGB(205, 205, 205);
constexpr COLORREF kErrorTextColor = RGB(255, 110, 110);  /* light-red */
constexpr COLORREF kWarnTextColor  = RGB(235, 210,  90);  /* amber */

/* Case-insensitive substring search; needle must already be lower-case. */
bool ContainsCI(std::string_view hay, std::string_view needle) {
    if (needle.empty() || needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < needle.size(); ++j) {
            char a = hay[i + j];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (a != needle[j]) break;
        }
        if (j == needle.size()) return true;
    }
    return false;
}

/* Severity tint for one debug-console line. Error keywords win over warning so a
   line mentioning both reads as the more urgent colour. "fail" stems catch
   failed/failure/fails. */
COLORREF ClassifyLineColor(std::string_view line) {
    if (ContainsCI(line, "abort") || ContainsCI(line, "fail") ||
        ContainsCI(line, "error") || ContainsCI(line, "exception"))
        return kErrorTextColor;
    if (ContainsCI(line, "warning")) return kWarnTextColor;
    return kLogTextColor;
}

constexpr int      kFontHeightSmall      = 13;
constexpr int      kFontHeightRegular    = 15;
constexpr uint32_t kSmallTierThresholdPx = 480;

}  /* namespace */

HwScreen::~HwScreen() {
    for (HFONT& f : font_cache_) {
        if (f) { DeleteObject(f); f = nullptr; }
    }
    if (bg_dc_)  DeleteDC(bg_dc_);
    if (bg_dib_) DeleteObject(bg_dib_);
}

void HwScreen::AddLine(std::string_view line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (lines_.size() >= kMaxLines) lines_.pop_front();
    lines_.emplace_back(line);
    ++gen_;
}

std::string HwScreen::LastLine() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return lines_.empty() ? std::string() : lines_.back();
}

void HwScreen::RenderInto(HDC, uint32_t* dib_bgra32,
                          uint32_t width, uint32_t height) {
    std::vector<std::string> snapshot;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        snapshot.assign(lines_.begin(), lines_.end());
        gen = gen_;
    }

    if ((int)width != bg_w_ || (int)height != bg_h_) RebuildBgDib((int)width, (int)height);

    if (bg_bits_ && (!bg_valid_ || gen != bg_gen_)) {
        std::memset(bg_bits_, 0, (size_t)bg_w_ * bg_h_ * 4u);
        emu_.Get<BootScreen>().DrawWatermark(bg_dc_, width, height);
        DrawLog(bg_dc_, width, height, snapshot);
        GdiFlush();
        bg_gen_   = gen;
        bg_valid_ = true;
    }

    if (bg_bits_) std::memcpy(dib_bgra32, bg_bits_, (size_t)width * height * 4u);
    else          std::memset(dib_bgra32, 0, (size_t)width * height * 4u);
    emu_.Get<BootBar>().RenderInto(dib_bgra32, width, height);
}

void HwScreen::RebuildBgDib(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP nd = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!nd || !bits) { if (nd) DeleteObject(nd); return; }
    if (!bg_dc_) bg_dc_ = CreateCompatibleDC(nullptr);
    SelectObject(bg_dc_, nd);
    if (bg_dib_) DeleteObject(bg_dib_);
    bg_dib_   = nd;
    bg_bits_  = static_cast<uint32_t*>(bits);
    bg_w_     = w;
    bg_h_     = h;
    bg_valid_ = false;
}

void HwScreen::DrawLog(HDC dc, uint32_t width, uint32_t height,
                       const std::vector<std::string>& snapshot) {
    const bool small_tier = (width < kSmallTierThresholdPx) ||
                            (height < kSmallTierThresholdPx);
    const int idx       = small_tier ? 0 : 1;
    const int height_px = small_tier ? kFontHeightSmall : kFontHeightRegular;
    if (!font_cache_[idx]) {
        font_cache_[idx] = CreateFontW(
            -height_px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
            emu_.Get<HostFonts>().MonoFace());
    }
    HFONT font = font_cache_[idx];
    if (!font) return;

    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);

    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    const int line_height = (int)tm.tmHeight;
    const int char_width  = (int)tm.tmAveCharWidth;
    if (line_height <= 0 || char_width <= 0) { SelectObject(dc, old_font); return; }

    const int avail_h    = (int)height - 2 * kEdgeMarginPx;
    const int avail_w    = (int)width  - 2 * kEdgeMarginPx;
    const int max_lines  = (avail_h > 0) ? avail_h / line_height : 0;
    const int chars_wide = (avail_w > 0) ? avail_w / char_width  : 0;
    if (max_lines <= 0 || chars_wide <= 0) { SelectObject(dc, old_font); return; }

    struct Chunk { std::string_view text; COLORREF color; };
    std::vector<Chunk> wrapped;
    wrapped.reserve(snapshot.size());
    for (const std::string& line : snapshot) {
        const COLORREF color = ClassifyLineColor(line);
        if (line.empty()) { wrapped.push_back({ {}, color }); continue; }
        size_t pos = 0;
        while (pos < line.size()) {
            const size_t take = std::min((size_t)chars_wide, line.size() - pos);
            wrapped.push_back({ { line.data() + pos, take }, color });
            pos += take;
        }
    }

    const int total = (int)wrapped.size();
    const int start = (total > max_lines) ? (total - max_lines) : 0;
    int y = kEdgeMarginPx;
    for (int i = start; i < total; ++i) {
        const Chunk& chunk = wrapped[i];
        SetTextColor(dc, chunk.color);
        TextOutA(dc, kEdgeMarginPx, y, chunk.text.data(), (int)chunk.text.size());
        y += line_height;
    }
    SelectObject(dc, old_font);
}
