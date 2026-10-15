from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import ttk

from branded_dialog import UNINSTALL_ICON_STEM, BrandedDialog, scaled
import ui_theme as theme


TITLE = "Uninstall CE Runtime Foundation"
HEADING = "Would you like to uninstall CE Runtime Foundation?"
DELETE_DATA_LABEL = "Delete devices and all user data"
KEPT_HINT = ("Your devices, saved states and settings stay in the "
             "installation directory.")
DELETE_HINT = ("Your devices, saved states and settings are deleted with "
               "the rest of the installation. This cannot be undone.")


class UninstallWindow:

    def __init__(self, root: tk.Tk, install_dir: Path) -> None:
        self.accepted = False
        self.delete_user_data = False
        self._root = root

        self._chrome = chrome = BrandedDialog(root, TITLE, HEADING,
                                              icon_stem=UNINSTALL_ICON_STEM)
        body = chrome.body
        gap = scaled(root, 8)
        wrap = chrome.body_width

        ttk.Label(body, text=str(install_dir), style="Hint.TLabel",
                  wraplength=wrap, justify="left").pack(anchor="w")

        self._var = tk.BooleanVar(master=root, value=False)
        ttk.Checkbutton(body, text=DELETE_DATA_LABEL, variable=self._var,
                        command=self._sync_hint).pack(anchor="w",
                                                      pady=(gap, 0))
        self._hint = ttk.Label(body, style="Hint.TLabel", wraplength=wrap,
                               justify="left")
        self._hint.pack(anchor="w", pady=(2, 0))
        self._sync_hint()

        chrome.add_button("Cancel", self._cancel)
        chrome.add_button("Uninstall", self._accept, style="Danger.TButton",
                          default=True)
        root.bind("<Escape>", lambda _e: self._cancel())
        root.protocol("WM_DELETE_WINDOW", self._cancel)

        chrome.present()
        root.deiconify()
        root.lift()
        root.focus_force()

    def _sync_hint(self) -> None:
        deleting = bool(self._var.get())
        self._hint.configure(text=DELETE_HINT if deleting else KEPT_HINT,
                             style="Danger.TLabel" if deleting
                             else "Hint.TLabel")
        self._chrome.refit()

    def _accept(self) -> None:
        self.accepted = True
        self.delete_user_data = bool(self._var.get())
        self._root.quit()

    def _cancel(self) -> None:
        self.accepted = False
        self._root.quit()


class UninstallProgress:

    def __init__(self, root: tk.Tk) -> None:
        self._root = root
        for child in root.winfo_children():
            child.destroy()

        self._chrome = BrandedDialog(root, TITLE,
                                     "Uninstalling CE Runtime Foundation…",
                                     icon_stem=UNINSTALL_ICON_STEM)
        self._text = tk.Text(self._chrome.body, height=9, width=1, wrap="char",
                             relief="flat", bg=theme.BG_FIELD, fg=theme.FG,
                             insertbackground=theme.FG, highlightthickness=1,
                             highlightbackground=theme.BORDER)
        self._text.pack(fill="both", expand=True)
        self._text.configure(state="disabled")
        self._chrome.present()
        root.update_idletasks()

    def log(self, line: str) -> None:
        self._text.configure(state="normal")
        self._text.insert("end", line + "\n")
        self._text.see("end")
        self._text.configure(state="disabled")
        self._root.update()

    def finish(self, heading: str) -> None:
        self._chrome.set_heading(heading)
        self._chrome.add_button("Close", self._root.quit, default=True)
        self._root.protocol("WM_DELETE_WINDOW", self._root.quit)
        self._root.bind("<Escape>", lambda _e: self._root.quit())
        self._chrome.present()
