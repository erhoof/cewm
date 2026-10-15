#define NOMINMAX

#include "sec_flash.h"

#include "../boards/board_context.h"
#include "../boards/ford_sync_gen2/ford_sync_gen2_family_id.h"
#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../core/cerf_paths.h"
#include "../core/string_utils.h"

#include <windows.h>

#include <string>

REGISTER_SERVICE(SecFlash);

namespace {

bool FileExists(const std::string& path) {
    const DWORD a = ::GetFileAttributesW(Utf8ToWide(path.c_str()).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

}  /* namespace */

bool SecFlash::ShouldRegister() {
    if (emu_.Get<BoardContext>().GetBoardFamilyId() != BoardFamilyId::FordSyncGen2) return false;
    const auto& cfg = emu_.Get<DeviceConfig>();
    if (cfg.rom_primary.empty()) return false;
    return FileExists(ResolveDeviceFile(cfg.device_name, cfg.rom_primary));
}

void SecFlash::OnReady() {
    const auto&       cfg  = emu_.Get<DeviceConfig>();
    const std::string path = ResolveDeviceFile(cfg.device_name, cfg.rom_primary);

    if (!mf_.Open(path)) {
        LOG(Caution, "SecFlash: failed to map %s\n", path.c_str());
        return;
    }
    if (!sec_.Open(mf_)) {
        LOG(Caution, "SecFlash: %s is not a valid .sec container\n", path.c_str());
        return;
    }
    LOG(Boot, "SecFlash: %s - %u chunks, %.1f MB de-chunked flash\n",
        path.c_str(), sec_.Header().chunk_count,
        double(FlashSize()) / 1024.0 / 1024.0);
}
