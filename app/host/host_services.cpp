// Minimal replacements for CERF's Win32 host-layer services (host/*.cpp). Scope: framebuffer + soft buttons only; no
// widgets, menus, dialogs or save/restore. Class definitions follow the upstream headers in cerf/cerf/host.
#include "peripherals/cerf_virt/cerf_virt_framebuffer.h"
#include "host_state.h"

#include "core/cerf_emulator.h"
#include "core/log.h"
#include "host/guest_deep_sleep.h"
#include "boot/guest_cold_boot.h"
#include "execution/engine_bridge.h"
#include <cerfengine/guest_engine.h>
#include <chrono>
#include <thread>
#include "host/guest_power_notifier.h"
#include "host/host_widget_registry.h"
#include "host/host_window.h"
#include "host/battery_widget.h"
#include "host/host_icon_cache.h"
#include "host/hw_screen.h"
#include "state/state_stream.h"

namespace cewm {
HostState& Host() {
    static HostState s;
    return s;
}
}  // namespace cewm

// ---- HostWindow: the SDL frontend owns the real window; the SoC only reports LCD state ----
REGISTER_SERVICE(HostWindow);
void HostWindow::OnReady() {}
void HostWindow::OnShutdown() {}
void HostWindow::OnLcdEnabled() { cewm::Host().lcd_enabled = true; }
void HostWindow::NotifyGuestRemoded(uint32_t w, uint32_t h) {
    if (auto* fb = emu_.TryGet<CerfVirtFramebuffer>()) fb->ApplyGuestMode(w, h);  // the guest switched modes (Guest Additions resize): follow it
    cewm::Host().guest_w = w;
    cewm::Host().guest_h = h;
}
void HostWindow::RunOnUiThread(std::function<void()> job) { job(); }  // shortcut: runs on the caller's thread

// ---- GuestPowerNotifier: log instead of banners ----
REGISTER_SERVICE(GuestPowerNotifier);
void GuestPowerNotifier::Banner(const char* line) { LOG(Cerf, "%s\n", line); }
void GuestPowerNotifier::NotifyPowerDown() {
    cewm::Host().powered_down = true;
    Banner("guest powered down");
}
void GuestPowerNotifier::NotifyReboot() { Banner("guest reboot"); }
void GuestPowerNotifier::NotifyResume(ResumeSource) { Banner("guest resumed"); }
void GuestPowerNotifier::NotifyHardReset() { Banner("hard reset"); }

// ---- GuestDeepSleep: no prompt; a guest suspend wakes itself at once (a reset-on-wake SoC resumes through its saved vector) ----
REGISTER_SERVICE(GuestDeepSleep);
void GuestDeepSleep::RegisterWaker(DeepSleepWaker* waker) { waker_ = waker; }
void GuestDeepSleep::RegisterClockStopWaker(DeepSleepClockStop* waker) { clock_stop_ = waker; }
void GuestDeepSleep::RegisterPowerOnWake() { power_on_wake_ = true; }
void GuestDeepSleep::RegisterPowerUpListener(std::function<void()> fn) { power_up_listeners_.push_back(std::move(fn)); }
void GuestDeepSleep::ClearWakeCause() { if (waker_) waker_->ClearSleepWakeCause(); }
void GuestDeepSleep::RegisterResumeVectorProvider(SleepResumeVectorProvider* p) { resume_vector_provider_ = p; }
void GuestDeepSleep::RegisterUserWakeInput(std::function<void()> fn) { user_wake_inputs_.push_back(std::move(fn)); }
void GuestDeepSleep::RegisterSleepEntryListener(std::function<void()> fn) { sleep_entry_listeners_.push_back(std::move(fn)); }
void GuestDeepSleep::RegisterParkClock(std::function<void()>) {}
void GuestDeepSleep::RegisterParkWakeSource(std::function<bool()>) {}
void GuestDeepSleep::RegisterParkWakeDue(std::function<int64_t()>) {}
void GuestDeepSleep::PollPark() {}
void GuestDeepSleep::FinishPark() {}
int64_t GuestDeepSleep::SleptNs() const { return 0; }
int64_t GuestDeepSleep::ParkWaitNs() const { return 0; }
void GuestDeepSleep::Enter() {
    if (emu_.Get<EngineBridge>().Guest().ResetPending() || active_.exchange(true)) return;
    LOG(Cerf, "guest entered deep sleep: waking it at once\n");
    wake_claimed_.store(false, std::memory_order_release);
    emu_.Get<GuestPowerNotifier>().NotifyPowerDown();
    emu_.Get<EngineBridge>().Guest().EnterDeepSleep();
    for (auto& fn : sleep_entry_listeners_) fn();
    asleep_.store(true, std::memory_order_release);
    std::thread([this] {  // off the JIT thread, after it has parked
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        DeliverWake(ResumeSource::User);
        active_.store(false);
    }).detach();
}
void GuestDeepSleep::ObserveAsleep(bool asleep) { asleep_.store(asleep, std::memory_order_release); }
void GuestDeepSleep::ConsumeSleepSpan() {}
void GuestDeepSleep::DeliverWake(ResumeSource src) {  // the same three wake shapes as cerf/host/guest_deep_sleep.cpp
    if (wake_claimed_.exchange(true, std::memory_order_acq_rel)) return;
    if (src == ResumeSource::User)
        for (auto& fn : user_wake_inputs_) fn();
    if (clock_stop_) {
        clock_stop_->OnPowerUp();
        for (auto& fn : power_up_listeners_) fn();
        emu_.Get<GuestPowerNotifier>().NotifyResume(src);
        emu_.Get<EngineBridge>().Guest().ExitDeepSleep();
        return;
    }
    if (power_on_wake_) {
        emu_.Get<GuestPowerNotifier>().NotifyResume(src);
        emu_.Get<GuestColdBoot>().RequestHardReset();
        return;
    }
    if (!waker_) return;
    waker_->LatchSleepWakeCause();
    if (resume_vector_provider_) resume_vector_provider_->ApplyPendingResume();
    emu_.Get<EngineBridge>().Guest().SetResetPending(true);
    emu_.Get<GuestPowerNotifier>().NotifyResume(src);
}
void GuestDeepSleep::OnFullRestore() {
    wake_claimed_.store(false, std::memory_order_release);
    if (emu_.Get<EngineBridge>().Guest().DeepSleep()) DeliverWake(ResumeSource::User);
}

// ---- HostWidgetRegistry: no widgets ----
REGISTER_SERVICE(HostWidgetRegistry);
namespace {
std::vector<HostWidget*>& Widgets() {
    static std::vector<HostWidget*> w;
    return w;
}
}  // namespace
void HostWidgetRegistry::Register(HostWidget* w) { Widgets().push_back(w); }
void cewm::PollWidgets() {
    for (HostWidget* w : Widgets()) w->PollDirty();
}
std::vector<HostWidget*> HostWidgetRegistry::Ordered() { return {}; }
HMENU HostWidgetRegistry::BuildContextMenu(const std::vector<WidgetMenuItem>&) { return nullptr; }
void HostWidgetRegistry::AppendAllToMenu(HMENU) {}
void HostWidgetRegistry::Dispatch(int) {}
void HostWidgetRegistry::SaveState(StateWriter&) {}
void HostWidgetRegistry::RestoreState(StateReader&) {}

// ---- HwScreen: debug console text screen is dropped ----
REGISTER_SERVICE(HwScreen);
HwScreen::~HwScreen() = default;
void HwScreen::AddLine(std::string_view line) { LOG(Nkdbg, "%.*s\n", (int)line.size(), line.data()); }
std::string HwScreen::LastLine() const { return {}; }
void HwScreen::RenderInto(HDC, uint32_t*, uint32_t, uint32_t) {}

// ---- HostWindow destructor / HostIconCache: nothing to release ----
HostWindow::~HostWindow() = default;
HostIconCache::~HostIconCache() = default;
void HostIconCache::DrawCentered(HDC, const RECT&, const wchar_t*) {}
HICON HostIconCache::Resolve(const wchar_t*, int) { return nullptr; }

// ---- BatteryWidget: no icon/menu; level and AC state come from cewm::Host() (wire to real device stats there) ----
int BatteryWidget::FillPercent() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return fill_percent_;
}
bool BatteryWidget::IsOnBattery() const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return on_battery_;
}
std::wstring BatteryWidget::Tooltip() const { return L""; }
void BatteryWidget::DrawIcon(HDC, const RECT&) const {}
std::vector<WidgetMenuItem> BatteryWidget::BuildMenu() { return {}; }
bool BatteryWidget::PollDirty() {
    const int fill = cewm::Host().battery_percent.load();
    const bool on_batt = cewm::Host().on_battery.load();
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (fill == fill_percent_ && on_batt == on_battery_) return false;
        fill_percent_ = fill;
        on_battery_ = on_batt;
    }
    if (on_change_) on_change_();
    return true;
}
void BatteryWidget::SaveWidgetState(StateWriter&) const {}
void BatteryWidget::RestoreWidgetState(StateReader&) {}

// Widget icons are not drawn (frontend shows only the framebuffer).
#include "host/host_widget.h"
void HostWidget::DrawChipIcon(HDC, const RECT&) {}

#include "host/audio_activity_widget.h"
REGISTER_SERVICE(AudioActivityWidget);
void AudioActivityWidget::NotePresent() {}
void AudioActivityWidget::DrawIcon(HDC, const RECT&) const {}
