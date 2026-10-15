from __future__ import annotations

from typing import Optional, Sequence

from choice_combo import ChoiceCombo
from emulation_speed import Number, model_choice, multiplier_label
from settings_card import SettingsRow


class MultiplierOptionBlock:
    def __init__(self, row: SettingsRow, key: str,
                 presets: Sequence[Optional[Number]]) -> None:
        self.key = key
        self.combo = ChoiceCombo(row.frame)
        row.control(self.combo.widget)
        self.combo.configure(list(presets), multiplier_label)

    def load(self, model: dict) -> None:
        self.combo.set(model_choice(model, self.key))

    def store(self, model: dict) -> None:
        value = self.combo.get()
        if value is None:
            model.pop(self.key, None)
        else:
            model[self.key] = value

    def set_enabled(self, enabled: bool) -> None:
        self.combo.set_enabled(enabled)
