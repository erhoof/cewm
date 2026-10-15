#include "sharp_mobilon_hc_battery.h"

#include "../../core/cerf_emulator.h"
#include "../../host/host_widget_registry.h"
#include "../board_context.h"
#include "sharp_mobilon_hc_family_id.h"

bool SharpMobilonHcBattery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
}

void SharpMobilonHcBattery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(SharpMobilonHcBattery);
