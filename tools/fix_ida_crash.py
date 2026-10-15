#!/usr/bin/env python3
"""Dismiss all IDA "did not close properly" recovery dialogs.

Finds all IDA "Please confirm" dialogs that appear when IDA detects an
unpacked database from a previous crash, and clicks "Restore packed base"
on each one.

IDA uses Qt widgets, not native Win32 controls, so we interact
via keyboard input rather than BM_CLICK messages.
"""

import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

try:
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        pass

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32

WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
VK_RETURN = 0x0D

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

IDA_PROCESS_NAMES = ["ida.exe", "ida64.exe"]


def find_ida_pids():
    """Find PIDs of all running IDA processes."""
    pids = {}
    for name in IDA_PROCESS_NAMES:
        result = subprocess.run(
            ["tasklist", "/fi", f"imagename eq {name}", "/fo", "csv", "/nh"],
            capture_output=True, text=True,
        )
        for line in result.stdout.strip().split("\n"):
            line = line.strip()
            if not line or "No tasks" in line or "INFO" in line:
                continue
            parts = line.split(",")
            if len(parts) >= 2:
                try:
                    pids[int(parts[1].strip('"'))] = name
                except ValueError:
                    pass
    return pids


def get_toplevel_windows(pids):
    """Return list of (hwnd, pid, title) for visible top-level windows."""
    windows = []

    def enum_cb(hwnd, _lparam):
        pid = wt.DWORD()
        u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value in pids and u32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(512)
            u32.GetWindowTextW(hwnd, buf, 512)
            if buf.value:
                windows.append((hwnd, pid.value, buf.value))
        return True

    u32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return windows


def find_confirm_dialogs(pids):
    """Find all 'Please confirm' dialogs for given PIDs."""
    dialogs = []
    for hwnd, pid, title in get_toplevel_windows(pids):
        if "Please confirm" in title:
            dialogs.append((hwnd, pid, title))
    return dialogs


def press_enter_on(hwnd):
    """Foreground a window and press Enter to select default button."""
    u32.SetForegroundWindow(hwnd)
    time.sleep(0.15)
    u32.PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0)
    time.sleep(0.05)
    u32.PostMessageW(hwnd, WM_KEYUP, VK_RETURN, 0)


def main():
    pids = find_ida_pids()
    if not pids:
        print("No IDA instances found.")
        return 0

    print(f"Found {len(pids)} IDA instance(s)")

    dialogs = find_confirm_dialogs(pids)
    if not dialogs:
        print("No 'Please confirm' recovery dialogs found.")
        return 0

    print(f"Found {len(dialogs)} recovery dialog(s)")

    for hwnd, pid, title in dialogs:
        print(f"  [{pid}] Pressing 'Restore packed base'...")
        press_enter_on(hwnd)
        time.sleep(0.1)

    # Verify dialogs are gone
    time.sleep(1.0)
    remaining = find_confirm_dialogs(pids)
    if remaining:
        print(f"\n{len(remaining)} dialog(s) still open - retrying...")
        for hwnd, pid, title in remaining:
            print(f"  [{pid}] Retry...")
            press_enter_on(hwnd)
            time.sleep(0.1)

    time.sleep(1.0)
    still_remaining = find_confirm_dialogs(find_ida_pids())
    if still_remaining:
        print(f"\n{len(still_remaining)} dialog(s) still open after retry.")
        return 1
    else:
        print(f"\nAll recovery dialogs dismissed successfully.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
