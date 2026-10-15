#include "nec_mobilepro_900_series_led.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_gdiplus.h"
#include "../../host/host_widget_registry.h"
#include "../board_context.h"
#include "nec_mobilepro_900_series_family_id.h"

REGISTER_SERVICE(NecMobilepro900SeriesLed);

namespace {
constexpr DWORD kBlinkPeriodMs = 500u;
const COLORREF  kClrLit  = RGB(120, 255, 140);
const COLORREF  kClrDark = RGB(34, 44, 36);
const COLORREF  kClrRim  = RGB(150, 160, 150);
const COLORREF  kClrBlinkRim = RGB(120, 230, 130);
}

bool NecMobilepro900SeriesLed::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesLed::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void NecMobilepro900SeriesLed::SetBlinking(bool blinking) {
    std::lock_guard<std::mutex> lk(mtx_);
    blinking_ = blinking;
}

bool NecMobilepro900SeriesLed::Blinking() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return blinking_;
}

bool NecMobilepro900SeriesLed::LitNow() const {
    return (GetTickCount() / kBlinkPeriodMs) % 2u == 0u;
}

std::wstring NecMobilepro900SeriesLed::Tooltip() const {
    return Blinking() ? L"Notification LED - blinking" : L"Notification LED - off";
}

void NecMobilepro900SeriesLed::DrawIcon(HDC dc, const RECT& box) const {
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    constexpr int kR = 6;
    emu_.Get<HostGdiPlus>().FillCircleAA(dc, cx, cy, kR, drawn_lit_ ? kClrLit : kClrDark,
                                         drawn_blinking_ ? kClrBlinkRim : kClrRim);
}

bool NecMobilepro900SeriesLed::PollDirty() {
    const bool blinking = Blinking();
    const bool lit      = blinking && LitNow();
    if (blinking == drawn_blinking_ && lit == drawn_lit_) return false;
    drawn_blinking_ = blinking;
    drawn_lit_      = lit;
    return true;
}
