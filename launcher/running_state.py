from __future__ import annotations

import ctypes
from ctypes import wintypes

from device_state import RunningStatus

_user32 = ctypes.windll.user32

SW_RESTORE = 9


def _belongs_to_pid(hwnd: int, pid: int) -> bool:
    out = wintypes.DWORD(0)
    _user32.GetWindowThreadProcessId(wintypes.HWND(hwnd), ctypes.byref(out))
    return out.value == pid


def show_running_window(status: RunningStatus) -> bool:
    hwnd = status.hwnd
    if not hwnd or not _user32.IsWindow(wintypes.HWND(hwnd)):
        return False
    if status.pid and not _belongs_to_pid(hwnd, status.pid):
        return False
    _user32.ShowWindow(wintypes.HWND(hwnd), SW_RESTORE)
    _user32.SetForegroundWindow(wintypes.HWND(hwnd))
    return True
