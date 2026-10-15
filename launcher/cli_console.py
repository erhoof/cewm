"""Bind a frozen windowed launcher.exe's stdio to the caller's console."""
from __future__ import annotations

import os
import sys

ATTACH_PARENT_PROCESS = -1
STD_INPUT_HANDLE = -10
STD_OUTPUT_HANDLE = -11
STD_ERROR_HANDLE = -12
TH32CS_SNAPPROCESS = 0x00000002


def _ancestor_pids(kernel32) -> list:
    import ctypes
    from ctypes import wintypes

    class ProcessEntry32(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("cntUsage", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
            ("th32ModuleID", wintypes.DWORD),
            ("cntThreads", wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", wintypes.DWORD),
            ("szExeFile", ctypes.c_char * 260),
        ]

    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    parent_of: dict = {}
    entry = ProcessEntry32()
    entry.dwSize = ctypes.sizeof(ProcessEntry32)
    if kernel32.Process32First(snapshot, ctypes.byref(entry)):
        while True:
            parent_of[entry.th32ProcessID] = entry.th32ParentProcessID
            if not kernel32.Process32Next(snapshot, ctypes.byref(entry)):
                break
    kernel32.CloseHandle(snapshot)

    chain: list = []
    pid = os.getpid()
    seen: set = set()
    while pid in parent_of and pid not in seen:
        seen.add(pid)
        pid = parent_of[pid]
        chain.append(pid)
    return chain


def attach_parent_console() -> None:
    if not getattr(sys, "frozen", False) or sys.platform != "win32":
        return

    import ctypes
    import msvcrt

    kernel32 = ctypes.windll.kernel32
    kernel32.GetStdHandle.restype = ctypes.c_void_p

    # ATTACH_PARENT_PROCESS reaches the windowed PyInstaller onefile bootloader,
    # which owns no console; the launching console is a further ancestor.
    if not kernel32.AttachConsole(ATTACH_PARENT_PROCESS):
        for pid in _ancestor_pids(kernel32):
            if kernel32.AttachConsole(pid):
                break

    invalid = ctypes.c_void_p(-1).value
    streams = (
        (STD_OUTPUT_HANDLE, "stdout", "w", 0, "CONOUT$"),
        (STD_ERROR_HANDLE, "stderr", "w", 0, "CONOUT$"),
        (STD_INPUT_HANDLE, "stdin", "r", os.O_RDONLY, "CONIN$"),
    )
    for std_id, name, mode, flags, con_dev in streams:
        handle = kernel32.GetStdHandle(std_id)
        try:
            if not handle or handle == invalid:
                stream = open(con_dev, mode, buffering=1, encoding="utf-8")
            else:
                fd = msvcrt.open_osfhandle(handle, flags)
                stream = os.fdopen(fd, mode, buffering=1, encoding="utf-8")
        except OSError:
            continue
        setattr(sys, name, stream)
