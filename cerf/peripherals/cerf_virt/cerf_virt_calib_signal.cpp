#include "cerf_virt_addr_map.h"
#include "cerf_virt_calib_signal_regs.h"

#include "../peripheral_base.h"
#include "../peripheral_dispatcher.h"
#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../host/guest_calibration_warning.h"

namespace {

class CerfVirtCalibSignal : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
               CerfVirt::kCalibSignalOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kCalibSignalSize; }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() == CerfVirt::kCalSigEvent) {
            auto& warn = emu_.Get<GuestCalibrationWarning>();
            if (value == CerfVirt::kCalSigAppeared) {
                warn.OnCalibrationAppeared();
                return;
            }
            if (value == CerfVirt::kCalSigDisappeared) {
                warn.OnCalibrationDisappeared();
                return;
            }
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
};

REGISTER_SERVICE(CerfVirtCalibSignal);

}
