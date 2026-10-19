#pragma once
#include "core/service.h"
#include "host/battery_widget.h"
#include <cstdint>
class StateWriter;
class StateReader;
class Pxa27xGpio;
class GuestCycleClock;

// P525 PCA9535 I/O expanders, reached through the standard PXA I2C bus.
// ASUSMisc.dll accepts the wire addresses 40h/42h; GPIO10 is the latter's INT.
class AsusP525Io : public Service {
public:
    explicit AsusP525Io(CerfEmulator& e) : Service(e), battery_(e) {}
    bool ShouldRegister() override;
    void OnReady() override;
    void Begin(uint8_t address);
    void Write(uint8_t value);
    uint8_t Read();
    void Key(uint8_t vk, bool down);
    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();
private:
    struct Chip {
        uint8_t regs[8] = {0xFF,0xFF,0xFF,0xFF,0,0,0xFF,0xFF};
        uint16_t inputs = 0xFFFF;
        uint16_t observed = 0xFFFF;
        uint8_t command = 0;
    } chips_[2];
    static constexpr uint8_t kMicroP = 0x16;  // 7-bit 0x0B smart-battery address, write form
    uint8_t mp_cmd_ = 0, mp_pos_ = 0;
    uint8_t MicroPRead();
    uint8_t address_ = 0;
    bool command_next_ = false;
    Pxa27xGpio* gpio_ = nullptr;
    BatteryWidget battery_;
    void DrivePowerPins();
    void KeepTouchEnabled();
    void* touch_ev_ = nullptr;
    Chip* Selected();
    void Irq();
};
