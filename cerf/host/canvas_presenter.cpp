#define NOMINMAX

#include "canvas_presenter.h"

#include "../core/log.h"
#include "frame_source.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kScrollLineStep = 16;
}  /* namespace */

CanvasPresenter::~CanvasPresenter() { Detach(); }

void CanvasPresenter::Attach(HWND hwnd, uint32_t surf_w, uint32_t surf_h) {
    hwnd_ = hwnd;
    RebuildGuestDib(surf_w, surf_h);
}

void CanvasPresenter::Detach() {
    if (guest_dc_)  { DeleteDC(guest_dc_); guest_dc_ = nullptr; }
    if (guest_dib_) { DeleteObject(guest_dib_); guest_dib_ = nullptr; guest_bits_ = nullptr; }
    hwnd_ = nullptr;
}

void CanvasPresenter::RebuildGuestDib(uint32_t w, uint32_t h) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = (LONG)w;
    bmi.bmiHeader.biHeight      = -(LONG)h;            /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(hwnd_);
    void* bits = nullptr;
    HBITMAP nd = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(hwnd_, screen);
    if (!nd || !bits) {
        LOG(Caution, "CanvasPresenter::RebuildGuestDib %ux%u failed (gle=%lu)\n",
            w, h, GetLastError());
        if (nd) DeleteObject(nd);
        return;
    }
    if (!guest_dc_) guest_dc_ = CreateCompatibleDC(nullptr);
    SelectObject(guest_dc_, nd);
    if (guest_dib_) DeleteObject(guest_dib_);
    guest_dib_  = nd;
    guest_bits_ = static_cast<uint32_t*>(bits);
    surface_w_.store(w, std::memory_order_release);
    surface_h_.store(h, std::memory_order_release);
}

void CanvasPresenter::OnCanvasResized(int canvas_w, int canvas_h) {
    canvas_w_ = canvas_w;
    canvas_h_ = canvas_h;
    UpdateScrollbars();
}

void CanvasPresenter::SetSurfaceSize(uint32_t w, uint32_t h) {
    if (w == surface_w_.load(std::memory_order_acquire) &&
        h == surface_h_.load(std::memory_order_acquire)) return;
    RebuildGuestDib(w, h);
    scroll_x_ = scroll_y_ = 0;
    UpdateScrollbars();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasPresenter::SetActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    UpdateScrollbars();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasPresenter::SetViewportMode(ViewportMode m) {
    if (mode_ == m) return;
    mode_ = m;
    scroll_x_ = scroll_y_ = 0;
    UpdateScrollbars();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasPresenter::SetAntialias(bool on) {
    if (antialias_ == on) return;
    antialias_ = on;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasPresenter::SetIntegerScale(int factor) {
    if (factor < 1) factor = 1;
    if (mode_ == ViewportMode::Integer && integer_factor_ == factor) return;
    mode_ = ViewportMode::Integer;
    integer_factor_ = factor;
    scroll_x_ = scroll_y_ = 0;
    UpdateScrollbars();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

int CanvasPresenter::ContentWidth() const {
    const int sw = (int)surface_w_.load(std::memory_order_acquire);
    return mode_ == ViewportMode::Integer ? sw * integer_factor_ : sw;
}

int CanvasPresenter::ContentHeight() const {
    const int sh = (int)surface_h_.load(std::memory_order_acquire);
    return mode_ == ViewportMode::Integer ? sh * integer_factor_ : sh;
}

CanvasPresenter::Layout CanvasPresenter::ComputeLayout(int cw, int ch) const {
    Layout L = {};
    const int sw = (int)surface_w_.load(std::memory_order_acquire);
    const int sh = (int)surface_h_.load(std::memory_order_acquire);
    if (sw <= 0 || sh <= 0 || cw <= 0 || ch <= 0) return L;

    if (mode_ == ViewportMode::Stretch) {
        L.stretch = true;
        L.dst_x = 0; L.dst_y = 0; L.dst_w = cw; L.dst_h = ch;
        L.src_x = 0; L.src_y = 0; L.src_w = sw; L.src_h = sh;
        return L;
    }
    if (mode_ == ViewportMode::Aspect) {
        const double scale = std::min((double)cw / sw, (double)ch / sh);
        int fw = (int)(sw * scale + 0.5);
        int fh = (int)(sh * scale + 0.5);
        if (fw < 1) fw = 1;
        if (fh < 1) fh = 1;
        L.stretch = true;
        L.dst_x = (cw - fw) / 2; L.dst_y = (ch - fh) / 2;
        L.dst_w = fw;            L.dst_h = fh;
        L.src_x = 0; L.src_y = 0; L.src_w = sw; L.src_h = sh;
        return L;
    }

    if (mode_ == ViewportMode::Integer) {
        const int N = integer_factor_;
        const int scaled_w = sw * N;
        const int scaled_h = sh * N;
        L.stretch = true;
        L.src_x = 0; L.src_y = 0; L.src_w = sw; L.src_h = sh;
        L.dst_w = scaled_w; L.dst_h = scaled_h;
        /* Fits: center. Overflows: offset by the scroll position and let the
           present DC clip to the canvas - keeps each guest pixel an exact NxN
           block (crisp) while staying scrollable. */
        L.dst_x = (scaled_w <= cw) ? (cw - scaled_w) / 2
                                   : -std::clamp(scroll_x_, 0, scaled_w - cw);
        L.dst_y = (scaled_h <= ch) ? (ch - scaled_h) / 2
                                   : -std::clamp(scroll_y_, 0, scaled_h - ch);
        return L;
    }

    /* Original: 1:1, centered when it fits, scrolled when it overflows. */
    L.stretch = false;
    if (sw > cw) {
        int sx = std::clamp(scroll_x_, 0, sw - cw);
        L.src_x = sx; L.src_w = cw; L.dst_x = 0; L.dst_w = cw;
    } else {
        L.src_x = 0; L.src_w = sw; L.dst_x = (cw - sw) / 2; L.dst_w = sw;
    }
    if (sh > ch) {
        int sy = std::clamp(scroll_y_, 0, sh - ch);
        L.src_y = sy; L.src_h = ch; L.dst_y = 0; L.dst_h = ch;
    } else {
        L.src_y = 0; L.src_h = sh; L.dst_y = (ch - sh) / 2; L.dst_h = sh;
    }
    return L;
}

bool CanvasPresenter::ComposeInto(HDC present_dc, uint32_t* present_bits) {
    if (!present_bits || canvas_w_ <= 0 || canvas_h_ <= 0) return false;

    if (!source_ || !source_->HasFrame() || !guest_bits_) {
        std::memset(present_bits, 0, (size_t)canvas_w_ * canvas_h_ * 4u);
        return false;
    }
    source_->RenderInto(guest_bits_, SurfaceWidth(), SurfaceHeight());

    std::memset(present_bits, 0, (size_t)canvas_w_ * canvas_h_ * 4u);
    const Layout L = ComputeLayout(canvas_w_, canvas_h_);
    if (L.dst_w <= 0 || L.dst_h <= 0) return true;
    if (L.stretch) {
        SetStretchBltMode(present_dc, antialias_ ? HALFTONE : COLORONCOLOR);
        SetBrushOrgEx(present_dc, 0, 0, nullptr);
        StretchBlt(present_dc, L.dst_x, L.dst_y, L.dst_w, L.dst_h,
                   guest_dc_, L.src_x, L.src_y, L.src_w, L.src_h, SRCCOPY);
    } else {
        BitBlt(present_dc, L.dst_x, L.dst_y, L.dst_w, L.dst_h,
               guest_dc_, L.src_x, L.src_y, SRCCOPY);
    }
    return true;
}

bool CanvasPresenter::HostToGuest(int cx, int cy, int& sx, int& sy) const {
    const Layout L = ComputeLayout(canvas_w_, canvas_h_);
    if (L.dst_w <= 0 || L.dst_h <= 0) { sx = sy = 0; return false; }
    if (L.stretch) {
        sx = L.src_x + (int)((long long)(cx - L.dst_x) * L.src_w / L.dst_w);
        sy = L.src_y + (int)((long long)(cy - L.dst_y) * L.src_h / L.dst_h);
    } else {
        sx = L.src_x + (cx - L.dst_x);
        sy = L.src_y + (cy - L.dst_y);
    }
    return cx >= L.dst_x && cx < L.dst_x + L.dst_w &&
           cy >= L.dst_y && cy < L.dst_y + L.dst_h;
}

void CanvasPresenter::ClampGuest(int& sx, int& sy) const {
    const int sw = (int)surface_w_.load(std::memory_order_acquire);
    const int sh = (int)surface_h_.load(std::memory_order_acquire);
    sx = std::clamp(sx, 0, (sw > 0 ? sw - 1 : 0));
    sy = std::clamp(sy, 0, (sh > 0 ? sh - 1 : 0));
}

void CanvasPresenter::UpdateScrollbars() {
    if (!hwnd_) return;
    const int sw = ContentWidth();
    const int sh = ContentHeight();
    const bool scrollable = active_ && (mode_ == ViewportMode::Original ||
                                        mode_ == ViewportMode::Integer);
    const bool want_h = scrollable && sw > canvas_w_;
    const bool want_v = scrollable && sh > canvas_h_;

    if (want_h != hsb_shown_) { ShowScrollBar(hwnd_, SB_HORZ, want_h); hsb_shown_ = want_h; }
    if (want_v != vsb_shown_) { ShowScrollBar(hwnd_, SB_VERT, want_v); vsb_shown_ = want_v; }

    if (want_h) {
        scroll_x_ = std::clamp(scroll_x_, 0, sw - canvas_w_);
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = sw - 1; si.nPage = (UINT)canvas_w_; si.nPos = scroll_x_;
        SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
    } else {
        scroll_x_ = 0;
    }
    if (want_v) {
        scroll_y_ = std::clamp(scroll_y_, 0, sh - canvas_h_);
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = sh - 1; si.nPage = (UINT)canvas_h_; si.nPos = scroll_y_;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    } else {
        scroll_y_ = 0;
    }
}

void CanvasPresenter::OnHScroll(WPARAM wp) {
    if (!hwnd_) return;
    SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, SB_HORZ, &si);
    int pos = si.nPos;
    switch (LOWORD(wp)) {
        case SB_LINELEFT:  pos -= kScrollLineStep;  break;
        case SB_LINERIGHT: pos += kScrollLineStep;  break;
        case SB_PAGELEFT:  pos -= (int)si.nPage;    break;
        case SB_PAGERIGHT: pos += (int)si.nPage;    break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos;  break;
    }
    const int maxs = std::max(0, ContentWidth() - canvas_w_);
    scroll_x_ = std::clamp(pos, 0, maxs);
    SCROLLINFO s2 = { sizeof(s2) }; s2.fMask = SIF_POS; s2.nPos = scroll_x_;
    SetScrollInfo(hwnd_, SB_HORZ, &s2, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CanvasPresenter::OnVScroll(WPARAM wp) {
    if (!hwnd_) return;
    SCROLLINFO si = { sizeof(si) }; si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(wp)) {
        case SB_LINEUP:        pos -= kScrollLineStep; break;
        case SB_LINEDOWN:      pos += kScrollLineStep; break;
        case SB_PAGEUP:        pos -= (int)si.nPage;   break;
        case SB_PAGEDOWN:      pos += (int)si.nPage;   break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos;     break;
    }
    const int maxs = std::max(0, ContentHeight() - canvas_h_);
    scroll_y_ = std::clamp(pos, 0, maxs);
    SCROLLINFO s2 = { sizeof(s2) }; s2.fMask = SIF_POS; s2.nPos = scroll_y_;
    SetScrollInfo(hwnd_, SB_VERT, &s2, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool CanvasPresenter::CaptureSurface(std::vector<uint32_t>& out,
                                     uint32_t& w, uint32_t& h) {
    if (!source_ || !source_->HasFrame()) return false;
    const uint32_t sw = surface_w_.load(std::memory_order_acquire);
    const uint32_t sh = surface_h_.load(std::memory_order_acquire);
    if (sw == 0 || sh == 0 || !guest_bits_) return false;
    source_->RenderInto(guest_bits_, sw, sh);
    out.assign(guest_bits_, guest_bits_ + (size_t)sw * sh);
    w = sw;
    h = sh;
    return true;
}
