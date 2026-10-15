#include "../../core/cerf_emulator.h"
#include "../../host/keyboard_hotkey_menu.h"
#include "../board_context.h"
#include "hp_jornada_700_family_id.h"
#include "hp_jornada_700_keyboard.h"
#include "hp_jornada_700_touch.h"

#include <cstdint>
#include <vector>

namespace {

struct KeyEntry { const wchar_t* label; uint8_t vk; };

/* App-launch / media hotkeys = the VKs [HKLM\Software\Microsoft\Shell\Keys]
   binds in the ROM default.reg; they ride the normal MCU-scancode path. */
constexpr KeyEntry kAppRow[] = {
    { L"Internet Explorer", 0xC1 }, { L"E-mail",         0xC2 },
    { L"Voice Recorder",    0xC3 }, { L"Contacts",       0xC4 },
    { L"Tasks",             0xC5 }, { L"Calendar",       0xC6 },
    { L"HP quick view",     0xC7 }, { L"HP quick pad",   0xC8 },
    { L"Pocket Access",     0xC9 }, { L"Pocket Excel",   0xCA },
    { L"Pocket Word",       0xCB },
};
constexpr KeyEntry kMediaKeys[] = {
    { L"Volume up", 0xD1 }, { L"Volume down", 0xD2 }, { L"Play / pause", 0xD3 },
};

/* Bezel soft buttons: raw-ADC zone centers per touch.dll sub_FB1314 (gate
   X 31..67; Y zones 151-274 / 361-469 / 591-689 / 815-915). */
struct BezelEntry { const wchar_t* label; uint16_t adc_y; };
constexpr uint16_t kBezelAdcX = 49;
constexpr BezelEntry kBezel[] = {
    { L"Settings (bezel 1)",     212 },
    { L"HP backup (bezel 2)",    415 },
    { L"HP dialer (bezel 3)",    640 },
    { L"Media player (bezel 4)", 865 },
};

class HpJornada700KeyboardHotkeyMenu : public KeyboardHotkeyMenu {
public:
    using KeyboardHotkeyMenu::KeyboardHotkeyMenu;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    std::vector<MenuSection> HotkeySections() override {
        return { KeyRow(std::begin(kAppRow),    std::end(kAppRow)),
                 KeyRow(std::begin(kMediaKeys), std::end(kMediaKeys)),
                 BezelSection() };
    }

private:
    void InjectKey(uint8_t vk) {
        auto& kbd = emu_.Get<HpJornada700Keyboard>();
        kbd.OnHostKey(vk, /*key_up=*/false);
        kbd.OnHostKey(vk, /*key_up=*/true);
    }

    MenuSection KeyRow(const KeyEntry* first, const KeyEntry* last) {
        MenuSection sec;
        for (const KeyEntry* k = first; k != last; ++k) {
            WidgetMenuItem it;
            it.label    = k->label;
            it.on_click = [this, vk = k->vk] { InjectKey(vk); };
            sec.push_back(std::move(it));
        }
        return sec;
    }

    MenuSection BezelSection() {
        MenuSection sec;
        for (const BezelEntry& b : kBezel) {
            WidgetMenuItem it;
            it.label    = b.label;
            it.on_click = [this, y = b.adc_y] {
                emu_.Get<HpJornada700Touch>().TapRawAdc(kBezelAdcX, y);
            };
            sec.push_back(std::move(it));
        }
        return sec;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada700KeyboardHotkeyMenu, KeyboardHotkeyMenu);
