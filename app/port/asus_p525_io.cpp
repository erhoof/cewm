#include "asus_p525_io.h"
#include "boards/board_context.h"
#include "core/cerf_emulator.h"
#include "socs/pxa27x/pxa27x_gpio.h"
#include "state/state_stream.h"
#include "host_state.h"
#include "host/host_widget_registry.h"
#include "p525_hw_signals.h"
#include "core/device_config.h"
#include "cpu/emulated_memory.h"
#include "execution/guest_cycle_clock.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

bool AsusP525Io::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == "asus_p525";
}
void AsusP525Io::OnReady() {
    gpio_ = &emu_.Get<Pxa27xGpio>();
    battery_.SetChangeHandler([this] { DrivePowerPins(); });
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    DrivePowerPins();
    // touch.dll reads DRVGLOBALS +0x62E (set by the native LCD driver) and drops every pen sample while it is 0;
    // the Guest Additions display driver replaces the native one, so keep the flag set.
    if (emu_.Get<DeviceConfig>().guest_additions) {
        auto& clk = emu_.Get<GuestCycleClock>();
        auto* ev = clk.Add([this] { KeepTouchEnabled(); });
        touch_ev_ = ev;
        clk.Arm(ev, clk.Cycles() + clk.NsToCycles(500000000));
    }
    Irq();
}
void AsusP525Io::KeepTouchEnabled() {
    constexpr uint32_t kFlag = 0xA000062Eu;
    uint8_t v = 0;
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.CopyOut(kFlag, &v, 1);
    if (!v) { v = 1; mem.CopyIn(kFlag, &v, 1); }
    auto& clk = emu_.Get<GuestCycleClock>();
    clk.Arm(static_cast<GuestCycleClock::Event*>(touch_ev_), clk.Cycles() + clk.NsToCycles(500000000));
}
void AsusP525Io::DrivePowerPins() {
    // ms2_usbfn.dll 01431844 samples GPIO11 for cable presence, then
    // 0143190C samples GPIO97: high selects AC (DRVGLOBALS +655 = 1),
    // low selects USB (2). Publish the cable type before its presence edge.
    const bool connected = !cewm::Host().on_battery.load();
    gpio_->SetInputLevel(97, connected);
    gpio_->SetInputLevel(11, connected);
}
AsusP525Io::Chip* AsusP525Io::Selected() {
    return address_ == 0x40 ? &chips_[0] : address_ == 0x42 ? &chips_[1] : nullptr;
}
void AsusP525Io::Begin(uint8_t address) {
    address_ = address & 0xFE;
    if (address_ == kMicroP && (address & 1)) mp_pos_ = 0;
    command_next_ = !(address & 1);
}
void AsusP525Io::Write(uint8_t value) {
    auto* c = Selected();
    if (address_ == kMicroP) { mp_cmd_ = value; mp_pos_ = 0; return; }
    if (!c) return;
    if (command_next_) { c->command = value & 7; command_next_ = false; return; }
    if (address_ == 0x42 && c->command == 2 && (value & 0x10) && !(c->regs[2] & 0x10)) g_p525_gprs_reset_release++;  // GPRS_RESET released: the module boots
    if (c->command >= 2) c->regs[c->command] = value;
    c->command ^= 1;
    Irq();
}
uint8_t AsusP525Io::Read() {
    auto* c = Selected();
    if (address_ == kMicroP) return MicroPRead();
    if (!c) return 0xFF;

    auto reg = c->command;
    c->command ^= 1; // Sequential accesses alternate the two registers in a pair.
    if (reg >= 2) return c->regs[reg];
    auto shift = reg * 8;
    uint8_t pins = c->inputs >> shift;
    uint8_t direction = c->regs[6 + reg];
    uint8_t value = (pins & direction) | (c->regs[2 + reg] & ~direction);
    c->observed = (c->observed & ~(0xFFu << shift)) | (uint16_t(pins) << shift);
    Irq();
    return value ^ c->regs[4 + reg];
}
// The battery MicroP (MSP430, firmware Galaxy_MicroP_A) answers ASUSMisc.dll's MP_* calls: write a command byte,
// then read one or two bytes. Command meanings are from ASUSMisc/battdrvr disassembly: 07h = capacity percent
// (battdrvr BatteryLifePercent), 01h = status word, 35h = six-byte version block, 11h = trigger reading (no data).
uint8_t AsusP525Io::MicroPRead() {
    uint8_t v = 0;
    switch (mp_cmd_) {
    case 0x07: v = (uint8_t)std::clamp(cewm::Host().battery_percent.load(), 0, 100); break;
    case 0x01: v = mp_pos_ == 0 ? 0x00 : 0x00; break;
    case 0x35: v = mp_pos_ == 0 ? 1 : 0; break;
    }
    mp_pos_++;
    return v;
}
void AsusP525Io::Irq() {
    const auto& c = chips_[1];
    uint16_t direction = c.regs[6] | (uint16_t(c.regs[7]) << 8);
    gpio_->SetInputLevel(10, !((c.inputs ^ c.observed) & direction));
}
void AsusP525Io::Key(uint8_t vk, bool down) {
    // pxa27x_keypad_Us.dll polls the second PCA9535 (active low): bit 2000 Talk (01255348), 1000 End (01255470),
    // 8000 Start (scan 12h = Windows key), 4000 Clear (scan 20h = VK_BACK).
    uint16_t bit = vk == 0x72 ? 0x2000 : vk == 0x73 ? 0x1000 : vk == 0x5B ? 0x8000 : vk == 0x08 ? 0x4000 : 0;
    auto& c = chips_[1];
    c.inputs = down ? c.inputs & ~bit : c.inputs | bit;
    Irq();
}
void AsusP525Io::Save(StateWriter& w) {
    w.Write("p525_i2c_address", address_); w.Write("p525_i2c_command_next", command_next_);
    for (auto& c : chips_) {
        for (auto v : c.regs) w.Write("pca_reg", v);
        w.Write("pca_inputs", c.inputs); w.Write("pca_observed", c.observed); w.Write("pca_command", c.command);
    }
}
void AsusP525Io::Restore(StateReader& r) {
    // Existing snapshots predate the board's I2C slaves.
    if (r.Remaining()) {
        r.Read("p525_i2c_address", address_); r.Read("p525_i2c_command_next", command_next_);
        for (auto& c : chips_) {
            for (auto& v : c.regs) r.Read("pca_reg", v);
            r.Read("pca_inputs", c.inputs); r.Read("pca_observed", c.observed); r.Read("pca_command", c.command);
            if (c.command > 7) r.Reject("invalid PCA9535 register");
        }
    }
    Irq();
}
void AsusP525Io::PostRestore() {
    // GPIO has now restored its saved input levels; host power wins over them.
    DrivePowerPins();
    Irq();
}
REGISTER_SERVICE(AsusP525Io);
