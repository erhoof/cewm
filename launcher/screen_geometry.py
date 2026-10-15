from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from ctypes import wintypes
from typing import Optional, Tuple


class _MonitorInfo(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD),
                ("rcMonitor", wintypes.RECT),
                ("rcWork", wintypes.RECT),
                ("dwFlags", wintypes.DWORD)]


def screen_work_area(widget: tk.Misc) -> Tuple[int, int, int, int]:
    if sys.platform == "win32":
        try:
            user32 = ctypes.windll.user32
            monitor = user32.MonitorFromWindow(widget.winfo_id(), 2)
            info = _MonitorInfo()
            info.cbSize = ctypes.sizeof(_MonitorInfo)
            if monitor and user32.GetMonitorInfoW(monitor, ctypes.byref(info)):
                r = info.rcWork
                return r.left, r.top, r.right - r.left, r.bottom - r.top
        except (OSError, AttributeError):
            pass
    return 0, 0, widget.winfo_screenwidth(), widget.winfo_screenheight()


def fit_geometry(window: tk.Misc, want_w: int, want_h: int,
                 parent: Optional[tk.Misc] = None) -> None:
    window.update_idletasks()
    wa_x, wa_y, wa_w, wa_h = screen_work_area(parent if parent is not None
                                              else window)
    try:
        scale = max(1.0, float(window.winfo_fpixels("1i")) / 96.0)
    except tk.TclError:
        scale = 1.0
    chrome = int(40 * scale)
    w = min(want_w, wa_w)
    h = min(want_h, wa_h - chrome)
    if parent is not None and parent.winfo_width() > 1:
        x = parent.winfo_rootx() + (parent.winfo_width() - w) // 2
        y = parent.winfo_rooty() + (parent.winfo_height() - h) // 2
    else:
        x = wa_x + (wa_w - w) // 2
        y = wa_y + (wa_h - chrome - h) // 2
    x = max(wa_x, min(x, wa_x + wa_w - w))
    y = max(wa_y, min(y, wa_y + wa_h - chrome - h))
    window.geometry(f"{w}x{h}+{x}+{y}")
