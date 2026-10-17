// State the SDL frontend reads from the emulator side (set by the minimal host services in host_services.cpp).
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>

namespace cewm {
struct HostState {
    std::atomic<bool> lcd_enabled{false};
    std::atomic<uint32_t> guest_w{0}, guest_h{0};
    std::atomic<bool> powered_down{false};
    // Device stats, set by the Aurora frontend (later: real battery); the emulated battery follows them via BatteryWidget::PollDirty.
    std::atomic<int> battery_percent{100};
    std::atomic<bool> on_battery{false};
    // Saved-state file for the running ROM and whether to resume from it (set by EmuStart before Boot).
    std::string state_path;
    std::atomic<bool> resume{true};
    std::string board_id = "devemu_wvga";  // Device Emulator board variant (panel size): devemu 240x320, devemu_wqvga 240x400, devemu_vga 480x640, devemu_wvga 480x800
    int dpi = 0;  // Guest Additions only: logical dpi the guest sees (0 = ROM default)
    bool guest_additions = false;  // set before EmuStart: CE-side driver, shared folder, NAT
    std::string share_dir;
    // Radio stats (-1 = none). The guest has no radio model, so only the frontend shows them for now.
    std::atomic<int> cell_signal{-1}, wifi_signal{-1};
    std::atomic<int> cell_act{-1};  // network type as 3GPP AcT: 0 GSM, 3 EDGE, 2 UMTS, 4 HSDPA (the host's LTE is reported as 4 so WM draws H); -1 = unknown
    std::mutex info_mu;
    std::string operator_name, wifi_name;  // guarded by info_mu
    std::string operator_plmn;             // MCC+MNC ("25011"), guarded by info_mu; feeds the fake modem's +COPS
};
HostState& Host();
}  // namespace cewm

namespace cewm {
// Call from the frontend loop: lets widgets (battery) push changed host state into the emulated hardware.
void PollWidgets();
}  // namespace cewm
