// Stubs for services that exist upstream only for Windows UI/crash/hibernation paths. See plan/README.md.
#include "core/board_not_found_service.h"
#include "core/cerf_emulator.h"
#include "core/crash_report.h"
#include "core/device_not_found_service.h"
#include "core/device_not_selected_service.h"
#include "core/log.h"

// Never registered: our app picks the board itself, so there is no "not found" dialog to show.
REGISTER_SERVICE(BoardNotFoundService);
bool BoardNotFoundService::ShouldRegister() { return false; }
void BoardNotFoundService::EnsureFound() {}
REGISTER_SERVICE(DeviceNotFoundService);
bool DeviceNotFoundService::ShouldRegister() { return false; }
void DeviceNotFoundService::EnsureFound() {}
bool DeviceNotFoundService::IsDevicePresent() { return true; }

REGISTER_SERVICE(DeviceNotSelectedService);
void DeviceNotSelectedService::Halt(const std::string& global_config_path) {
    LOG(Caution, "no device selected (config: %s)\n", global_config_path.c_str());
    CerfFatalExit(CERF_FATAL_USER_ERROR);
}

void CrashReport::SetLogFileOverride(const char*) {}

// Default PCMCIA network card (NE2000 + slirp upstream): no networking in this port yet.
#include "peripherals/pcmcia/pcmcia_auto_insert.h"
REGISTER_SERVICE(PcmciaAutoInsert);
void PcmciaAutoInsert::InsertLaunchCompactFlash(PcmciaSlot&) {}
void PcmciaAutoInsert::InsertDefaultNetworkCard(PcmciaSlot&) { LOG(Cerf, "no default network card (networking not ported)\n"); }

// PcmciaCardCatalog: only the CompactFlash card is ported (upstream also builds NE2000, serial and VGA cards).
#include "peripherals/compactflash/compactflash_card.h"
#include "peripherals/pcmcia/pcmcia_card_catalog.h"
REGISTER_SERVICE(PcmciaCardCatalog);
void PcmciaCardCatalog::OnReady() {}
std::unique_ptr<PcmciaCard> PcmciaCardCatalog::TryCreate(const std::string& id, const std::wstring& binding) {
    if (id == "cf") return std::make_unique<CompactFlashCard>(emu_, binding);
    return nullptr;
}
std::unique_ptr<PcmciaCard> PcmciaCardCatalog::Create(const std::string& id, const std::wstring& binding) {
    auto card = TryCreate(id, binding);
    if (!card) {
        LOG(Caution, "PCMCIA card '%s' not supported in this port\n", id.c_str());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return card;
}

// HostGdiPlus: Windows GDI+ drawing helper; only the board LED widgets call it, and this port draws no LED widgets.
#include "host/host_gdiplus.h"
REGISTER_SERVICE(HostGdiPlus);
HostGdiPlus::~HostGdiPlus() {}
void HostGdiPlus::OnReady() {}
void HostGdiPlus::FillCircleAA(HDC, int, int, int, COLORREF, COLORREF) {}
