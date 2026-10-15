#pragma once

#include "../core/service.h"
#include "host_widget.h"

#include <vector>

/* Optional board contribution of hardware-hotkey menu sections for
   KeyboardWidget - the board buttons that are not part of the key mapping
   (Jornada app-launch row, bezel buttons, Fn-symbol shortcuts). A board with
   none registers nothing and the widget shows only "See keyboard mapping". */
class KeyboardHotkeyMenu : public Service {
public:
    using Service::Service;
    ~KeyboardHotkeyMenu() override = default;

    using MenuSection = std::vector<WidgetMenuItem>;

    /* Sections appended (separator-joined) below "See keyboard mapping". */
    virtual std::vector<MenuSection> HotkeySections() = 0;
};
