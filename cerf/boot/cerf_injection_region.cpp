#include "cerf_injection_region.h"

#include "../boards/board_context.h"
#include "../boards/page_table_builder.h"
#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../execution/engine_bridge.h"
#include "../peripherals/cerf_virt/cerf_virt_addr_map.h"

REGISTER_SERVICE(CerfInjectionRegion);

bool CerfInjectionRegion::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

uint32_t CerfInjectionRegion::BandPaBase() const {
    return emu_.Get<BoardContext>().GuestAdditionsWindowBase()
         + CerfVirt::kInjectionBandOffset;
}

uint32_t CerfInjectionRegion::BandSize() const {
    return CerfVirt::kInjectionBandSize;
}

uint32_t CerfInjectionRegion::BandVaBase() {
    if (va_base_) return va_base_;

    const uint32_t va =
        emu_.Get<PageTableBuilder>().StaticWindowHole(CerfVirt::kInjectionBandSize);
    if (!va) {
        LOG(Caution, "guest-additions injection band: this board's OAT fills the "
                "kernel static window [0x80000000,0xA0000000) with no 0x%X-byte "
                "hole - the stub band cannot be placed\n",
            CerfVirt::kInjectionBandSize);
        CerfFatalExit();
    }

    emu_.Get<EmulatedMemory>().AddRegion(BandPaBase(),
                                         CerfVirt::kInjectionBandSize,
                                         PAGE_READWRITE);
    emu_.Get<EngineBridge>().MapInjectionBand(va, BandPaBase(), CerfVirt::kInjectionBandSize);
    va_base_ = va;
    LOG(GuestAdditions, "injection band: VA 0x%08X -> PA 0x%08X size 0x%X\n",
        va, BandPaBase(), CerfVirt::kInjectionBandSize);
    return va_base_;
}
