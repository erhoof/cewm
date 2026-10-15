from __future__ import annotations

import tkinter as tk

from emulation_speed import THROTTLE_KEY, THROTTLE_PRESETS
from launch_options_multiplier import MultiplierOptionBlock
from rich_text import plain
from settings_card import SettingsColumn, switch

PAGE_EMULATOR = "emulator"
THROTTLE_WARNING = "Changing this setting will render emulation broken. Expect crashes or wrong behaviour."


class EmulatorSettingsPage:
    key = PAGE_EMULATOR
    title = "Emulator Settings"

    def __init__(self, parent: tk.Misc) -> None:
        self.var_full_screen = tk.BooleanVar(value=False)
        self.var_detach_net = tk.BooleanVar(value=False)
        self.var_verbose = tk.BooleanVar(value=False)

        page = SettingsColumn(parent)
        self.frame = page.frame
        speed = page.group()
        speed.note(THROTTLE_WARNING)
        self.throttle = MultiplierOptionBlock(
            speed.row("Throttle multiplier", [
                plain("Set how fast the guest runs: 2 is half speed, and 0 "
                      "is as fast as the host allows.")]),
            THROTTLE_KEY, THROTTLE_PRESETS)
        group = page.group()
        self._switches = []
        for title, description, var in (
                ("Borderless full screen", None, self.var_full_screen),
                ("Detach internet connection", None, self.var_detach_net),
                ("Verbose logs",
                 [plain("Resets when the launcher restarts.")],
                 self.var_verbose)):
            row = group.row(title, description)
            check = switch(row.frame, var)
            row.control(check, wide=False)
            self._switches.append(check)

    def load(self, model: dict) -> None:
        self.var_full_screen.set(bool(model.get("full_screen", False)))
        self.var_detach_net.set(not model.get("network_enabled", True))
        self.var_verbose.set(bool(model.get("verbose_logs", False)))
        self.throttle.load(model)

    def store(self, model: dict) -> None:
        model["full_screen"] = self.var_full_screen.get()
        model["network_enabled"] = not self.var_detach_net.get()
        model["verbose_logs"] = self.var_verbose.get()
        self.throttle.store(model)

    def validate(self) -> bool:
        return True

    def set_enabled(self, enabled: bool) -> None:
        for check in self._switches:
            check.config(state="normal" if enabled else "disabled")
        self.throttle.set_enabled(enabled)
