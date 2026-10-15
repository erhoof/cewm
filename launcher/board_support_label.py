from __future__ import annotations

from tkinter import ttk

from board_info import DANGER, board_support_warning

_STYLES = {DANGER: "Danger.TLabel"}
_DEFAULT_STYLE = "Warn.TLabel"


def apply_support_warning(label: ttk.Label, board_id: str) -> bool:
    warning = board_support_warning(board_id)
    if warning is None:
        label.config(text="")
        return False
    severity, text = warning
    label.config(text=text, style=_STYLES.get(severity, _DEFAULT_STYLE))
    return True
