// Device Emulator notification registers; the Windows LED widget is not used by this frontend.
#include "peripherals/peripheral_base.h"
#include "peripherals/peripheral_dispatcher.h"
#include "core/cerf_emulator.h"
#include "boards/board_context.h"
#include "boards/samsung_smdk2410/samsung_smdk2410_family_id.h"
#include "state/state_stream.h"

#include <array>

namespace {
class SamsungSmdk2410NotificationLed : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }
    uint32_t MmioBase() const override { return 0x500FFFD0u; }
    uint32_t MmioSize() const override { return regs_.size(); }
    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off >= regs_.size()) HaltUnsupportedAccess("ReadByte", addr, 0);
        return regs_[off];
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off >= regs_.size()) HaltUnsupportedAccess("WriteByte", addr, value);
        regs_[off] = value;
    }
    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_.data(), regs_.size()); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_.data(), regs_.size()); }
private:
    // Two devices (LED and vibrator), each with mode, on-time and off-time bytes.
    // shortcut: retain driver state only; add host LED/vibration output when the frontend supports it.
    std::array<uint8_t, 6> regs_{};
};
}
REGISTER_SERVICE(SamsungSmdk2410NotificationLed);
