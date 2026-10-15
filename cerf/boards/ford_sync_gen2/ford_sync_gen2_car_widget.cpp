#include "../../core/service.h"
#include "../../host/host_widget.h"
#include <cstdint>
#include <string>
#include <vector>
#include "ford_sync_gen2_temperature_section.h"
#include "../board_context.h"
#include "ford_sync_gen2_family_id.h"
#include "../../core/cerf_emulator.h"
#include "../../host/host_icon_cache.h"
#include "../../host/host_widget_registry.h"
#include "../../state/state_stream.h"

namespace {

class FordSyncGen2CarWidget : public Service, public HostWidget {
public:
    explicit FordSyncGen2CarWidget(CerfEmulator& emu)
        : Service(emu), temperature_(emu), sections_{&temperature_} {}
    bool ShouldRegister() override;
    void OnReady() override;
    std::wstring WidgetName() const override { return L"Car"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::wstring Tooltip() const override;
    std::vector<WidgetMenuItem> BuildMenu() override;
    void DrawIcon(HDC dc, const RECT& box) const override;
    bool PollDirty() override;
    void SaveWidgetState(StateWriter& w) const override;
    void RestoreWidgetState(StateReader& r) override;
private:
    TemperatureSection temperature_;
    std::vector<HostMenuSection*> sections_;
};

REGISTER_SERVICE(FordSyncGen2CarWidget);

bool FordSyncGen2CarWidget::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
}
void FordSyncGen2CarWidget::OnReady() { emu_.Get<HostWidgetRegistry>().Register(this); }
std::wstring FordSyncGen2CarWidget::Tooltip() const {
    return L"SYNC 2 car controls\n" + temperature_.Label();
}
void FordSyncGen2CarWidget::DrawIcon(HDC dc, const RECT& box) const {
    emu_.Get<HostIconCache>().DrawCentered(dc, box, L"ICON_CAR");
}
std::vector<WidgetMenuItem> FordSyncGen2CarWidget::BuildMenu() {
    std::vector<WidgetMenuItem> items;
    for (auto* section : sections_) {
        WidgetMenuItem item;
        item.label = section->Label();
        item.submenu = section->BuildItems();
        items.push_back(std::move(item));
    }
    return items;
}
bool FordSyncGen2CarWidget::PollDirty() {
    bool dirty = false;
    for (auto* section : sections_) dirty |= section->PollDirty();
    return dirty;
}
void FordSyncGen2CarWidget::SaveWidgetState(StateWriter& w) const { temperature_.SaveState(w); }
void FordSyncGen2CarWidget::RestoreWidgetState(StateReader& r) { temperature_.RestoreState(r); }
}
