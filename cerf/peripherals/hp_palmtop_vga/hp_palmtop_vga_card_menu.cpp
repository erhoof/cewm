#include "hp_palmtop_vga_card_menu.h"

#include "hp_palmtop_vga_card.h"
#include "../../core/cerf_emulator.h"

#include <memory>

REGISTER_SERVICE(HpPalmtopVgaCardMenu);

std::vector<WidgetMenuItem> HpPalmtopVgaCardMenu::BuildInsertMenu(
    PcmciaCardCatalog::CardInserter inserter) {
    std::vector<WidgetMenuItem> items;

    auto note = [&items](const wchar_t* text) {
        WidgetMenuItem it;
        it.label   = text;
        it.enabled = false;   /* shown grayed: inline guidance, not clickable */
        items.push_back(std::move(it));
    };
    WidgetMenuItem insert;
    insert.label    = L"Insert";
    insert.on_click = [this, inserter] {
        inserter(std::make_unique<HpPalmtopVgaCard>(emu_));
    };
    items.push_back(std::move(insert));

    items.push_back({});   /* separator */

    note(L"External VGA monitor output, in its own window.");
    note(L"Works on the HP Jornada series.");
    note(L"Verified on the HP Jornada 720.");

    return items;
}
