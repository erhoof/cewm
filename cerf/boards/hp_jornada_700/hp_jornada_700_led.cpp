#include "hp_jornada_700_led.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_gdiplus.h"
#include "../../host/host_widget_registry.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "../board_context.h"
#include "hp_jornada_700_family_id.h"

REGISTER_SERVICE(HpJornada700Led);

namespace {
constexpr int kBlinkPeriodMs = 500;
const COLORREF kClrLit  = RGB(120, 255, 140);
const COLORREF kClrDark = RGB(34, 44, 36);
const COLORREF kClrRim  = RGB(150, 160, 150);
}  /* namespace */

bool HpJornada700Led::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void HpJornada700Led::OnReady() {
    /* Button line idles high; a press pulls it low (falling edge, GFER13). */
    emu_.Get<Sa11xxGpio>().DriveInputPin(13, /*level=*/true);
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void HpJornada700Led::SetState(State s) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    state_ = s;
}

uint8_t HpJornada700Led::StatusByte() const {
    switch (CurrentState()) {
        case State::On:    return 0x01;
        case State::Blink: return 0x03;
        default:           return 0x00;
    }
}

HpJornada700Led::State HpJornada700Led::CurrentState() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return state_;
}

void HpJornada700Led::PressButton() {
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(13, false);
    gpio.DriveInputPin(13, true);
}

std::wstring HpJornada700Led::Tooltip() const {
    switch (CurrentState()) {
        case State::On:    return L"Notification LED - on (click = press button)";
        case State::Blink: return L"Notification LED - blinking (click = press button)";
        default:           return L"Notification LED - off (click = press button)";
    }
}

std::vector<WidgetMenuItem> HpJornada700Led::BuildMenu() {
    std::vector<WidgetMenuItem> items;
    WidgetMenuItem press;
    press.label    = L"Press notification button";
    press.on_click = [this] { PressButton(); };
    items.push_back(std::move(press));
    return items;
}

void HpJornada700Led::DrawIcon(HDC dc, const RECT& box) const {
    const State s   = CurrentState();
    const bool  lit = s == State::On ||
                      (s == State::Blink &&
                       (GetTickCount() / kBlinkPeriodMs) % 2 == 0);

    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    constexpr int kR = 6;

    emu_.Get<HostGdiPlus>().FillCircleAA(dc, cx, cy, kR, lit ? kClrLit : kClrDark,
                                         kClrRim);
}

bool HpJornada700Led::PollDirty() {
    const State s   = CurrentState();
    const bool  lit = s == State::On ||
                      (s == State::Blink &&
                       (GetTickCount() / kBlinkPeriodMs) % 2 == 0);
    if (s == last_drawn_state_ && lit == last_drawn_lit_) return false;
    last_drawn_state_ = s;
    last_drawn_lit_   = lit;
    return true;
}
