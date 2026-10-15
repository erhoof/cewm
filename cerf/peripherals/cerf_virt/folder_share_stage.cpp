#include "folder_share_stage.h"

#include "cerf_virt_addr_map.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../execution/engine_bridge.h"

REGISTER_SERVICE(FolderShareStage);

bool FolderShareStage::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

uint32_t FolderShareStage::BasePa() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
           CerfVirt::kFsStageOffset;
}

void FolderShareStage::OnReady() {
    EmulatedMemory& mem = emu_.Get<EmulatedMemory>();
    mem.AddRegion(BasePa(), CerfVirt::kFsStageSize, PAGE_READWRITE);
    base_ = mem.TryTranslate(BasePa());
    if (!base_) {
        LOG(Cerf, "[FolderShareStage] region at PA 0x%08X is not backed\n", BasePa());
        CerfFatalExit();
    }
    emu_.Get<EngineBridge>().Guest().SetDmaRegion(BasePa(), CerfVirt::kFsStageSize);
}

CerfVirt::ServerPB* FolderShareStage::Pb() {
    return reinterpret_cast<CerfVirt::ServerPB*>(base_ + CerfVirt::kFsStagePbOff);
}

uint8_t* FolderShareStage::IoBuf() {
    return base_ + CerfVirt::kFsStageIoOff;
}
