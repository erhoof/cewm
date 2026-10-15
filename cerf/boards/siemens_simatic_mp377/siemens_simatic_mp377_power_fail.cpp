#include "siemens_simatic_mp377_power_fail.h"

#include "siemens_simatic_mp377_power_reset.h"

#include "../board_context.h"
#include "siemens_simatic_mp377_family_id.h"
#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../../socs/guest_cpu_reset.h"
#include "../../socs/irq_controller.h"

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377PowerFail::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377PowerFail::OnReady() {
    Reset();
    emu_.Get<HostWidgetRegistry>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) Reset();
    });
}

void SiemensSimaticMp377PowerFail::SetAsserted(bool asserted) {
    asserted_.store(asserted, std::memory_order_release);
    RefreshIrq();
}

void SiemensSimaticMp377PowerFail::Reset() {
    asserted_.store(false, std::memory_order_release);
    RefreshIrq();
}

void SiemensSimaticMp377PowerFail::RefreshIrq() {
    auto& irq = emu_.Get<IrqController>();
    if (asserted_.load(std::memory_order_acquire))
        irq.AssertIrq(static_cast<int>(kMp377PowerFailIrqSource));
    else
        irq.DeAssertIrq(static_cast<int>(kMp377PowerFailIrqSource));
}

std::wstring SiemensSimaticMp377PowerFail::Tooltip() const {
    return asserted_.load(std::memory_order_acquire)
        ? L"External power - failure asserted"
        : L"External power - available";
}

void SiemensSimaticMp377PowerFail::DrawIcon(HDC dc, const RECT& box) const {
    const bool failed = asserted_.load(std::memory_order_acquire);
    const COLORREF outline = failed ? RGB(245, 105, 105) : RGB(155, 165, 175);
    const COLORREF fill = failed ? RGB(105, 28, 28) : RGB(38, 48, 58);
    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;

    HPEN pen = CreatePen(PS_SOLID, 2, outline);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    Rectangle(dc, cx - 5, cy - 5, cx + 5, cy + 5);
    MoveToEx(dc, cx - 2, cy - 9, nullptr);
    LineTo(dc, cx - 2, cy - 5);
    MoveToEx(dc, cx + 2, cy - 9, nullptr);
    LineTo(dc, cx + 2, cy - 5);
    MoveToEx(dc, cx, cy + 5, nullptr);
    LineTo(dc, cx, cy + 9);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

std::vector<WidgetMenuItem> SiemensSimaticMp377PowerFail::BuildMenu() {
    const bool asserted = asserted_.load(std::memory_order_acquire);
    WidgetMenuItem input;
    input.label = L"Assert power-fail input";
    input.checked = asserted;
    input.on_click = [this, asserted] { SetAsserted(!asserted); };
    return {std::move(input)};
}

bool SiemensSimaticMp377PowerFail::PollDirty() {
    const bool asserted = asserted_.load(std::memory_order_acquire);
    if (asserted == last_drawn_asserted_) return false;
    last_drawn_asserted_ = asserted;
    return true;
}

REGISTER_SERVICE(SiemensSimaticMp377PowerFail);

} // namespace siemens_mp377
