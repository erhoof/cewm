#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_hotkey_menu.h"
#include "../../socs/vr41xx/vr41xx_piu.h"
#include "../board_context.h"
#include "casio_pocketpostpet_family_id.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {

struct StripButton { const wchar_t* label; uint16_t adc_y; };

/* touch.dll sub_137095C (Jx627 table @0x1370568): 7 right-strip hard icons, VK 0xE9..0xEF
   top-to-bottom, each record field[2..3] = its v9 (4x GDI px-Y) zone. */
constexpr uint16_t kStripAdcX = 945u;
constexpr StripButton kStrip[] = {
    { L"Top Menu",   73u },
    { L"Get Mail",  220u },
    { L"Internet",  367u },
    { L"Mic",       514u },
    { L"Tools",     660u },
    { L"Set Up",    807u },
    { L"Text Edit", 953u },
};

class CasioPocketpostpetKeyboardHotkeyMenu : public KeyboardHotkeyMenu {
public:
    using KeyboardHotkeyMenu::KeyboardHotkeyMenu;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    std::vector<MenuSection> HotkeySections() override {
        MenuSection sec;
        for (const StripButton& b : kStrip) {
            WidgetMenuItem it;
            it.label    = b.label;
            it.on_click = [this, y = b.adc_y] {
                emu_.Get<Vr41xxPiu>().SyntheticTap(kStripAdcX, y);
            };
            sec.push_back(std::move(it));
        }
        return { std::move(sec) };
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioPocketpostpetKeyboardHotkeyMenu, KeyboardHotkeyMenu);
