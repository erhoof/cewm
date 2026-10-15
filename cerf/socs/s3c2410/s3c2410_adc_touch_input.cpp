#define NOMINMAX

#include "../../boards/board_context.h"
#include "s3c2410_id.h"
#include "../../core/cerf_emulator.h"
#include "../../host/host_canvas.h"
#include "../../host/touch_input.h"
#include "s3c2410_adc.h"
#include "s3c2410_touch_calibration.h"

#include <cstdint>

namespace {

class S3C2410AdcTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || bd->GetSocId() != SocId::S3c2410) return false;
        return emu_.TryGet<S3C2410TouchCalibration>() != nullptr;
    }

    void OnPenDown(int x, int y) override { Drive(true,  x, y); }
    void OnPenMove(int x, int y) override { Drive(true,  x, y); }
    void OnPenUp  (int x, int y) override { Drive(false, x, y); }

    void OnCaptureLost() override {
        emu_.Get<S3C2410Adc>().SetPen(false, last_x_, last_y_);
    }

private:
    void Drive(bool down, int host_x, int host_y) {
        auto& hc = emu_.Get<HostCanvas>();
        emu_.Get<S3C2410TouchCalibration>().MapHostToSample(
            host_x, host_y,
            static_cast<double>(hc.GuestSurfaceWidth()),
            static_cast<double>(hc.GuestSurfaceHeight()),
            last_x_, last_y_);
        emu_.Get<S3C2410Adc>().SetPen(down, last_x_, last_y_);
    }

    uint16_t last_x_ = 0;
    uint16_t last_y_ = 0;
};

}  /* namespace */

REGISTER_SERVICE_AS(S3C2410AdcTouchInput, TouchInput);
