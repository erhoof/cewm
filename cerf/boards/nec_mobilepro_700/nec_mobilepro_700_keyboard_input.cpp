#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_input.h"
#include "../../host/keyboard_map.h"
#include "../../host/keyboard_router.h"
#include "../../socs/vr41xx/vr41xx_kiu.h"
#include "../board_context.h"
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace {

class NecMobilePro700KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        emu_.Get<Vr41xxKiu>().SetKeyState(static_cast<uint8_t>(code), !key_up);
    }
};

}  // namespace

REGISTER_SERVICE(NecMobilePro700KeyboardInput);
