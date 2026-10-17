// Host-UI services the Guest Additions peripherals look up. The Win32 UI they belonged to is not ported, so the lookups
// find nothing (TryGet) or hit a no-op. HostCanvas is only ever TryGet'd: a same-named stand-in gives the linker its
// typeinfo and is never constructed.
#include "host/boot_screen.h"
#include "host/host_auto_resize.h"
#include "host/guest_calibration_warning.h"
#include "host/refresh_rate_service.h"
#include "core/cerf_emulator.h"

class HostCanvas { virtual void Key(); };
void HostCanvas::Key() {}

BootScreen::~BootScreen() {}
void BootScreen::OnReady() {}
void BootScreen::OnShutdown() {}
void BootScreen::SetRestartLabel(const wchar_t*) {}

bool GuestCalibrationWarning::ShouldRegister() { return false; }
void GuestCalibrationWarning::OnReady() {}
void GuestCalibrationWarning::OnCalibrationAppeared() {}
void GuestCalibrationWarning::OnCalibrationDisappeared() {}

int RefreshRateService::GetRefreshRate() { return 60; }
REGISTER_SERVICE(RefreshRateService);

// The GA NIC pings this widget's activity flags; the widget itself has no UI here.
HostAutoResize::~HostAutoResize() {}
bool HostAutoResize::ShouldRegister() { return true; }
void HostAutoResize::OnReady() {}
void HostAutoResize::OnUserResizeEnd(uint32_t, uint32_t) {}
std::wstring HostAutoResize::Tooltip() const { return L""; }
bool HostAutoResize::TryPrimaryAction() { return false; }
std::vector<WidgetMenuItem> HostAutoResize::BuildMenu() { return {}; }
void HostAutoResize::DrawIcon(HDC, const RECT&) const {}
bool HostAutoResize::PollDirty() { return false; }
REGISTER_SERVICE(HostAutoResize);
