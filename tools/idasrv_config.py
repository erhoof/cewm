"""Shared configuration, logging and Hex-Rays availability for the IDA HTTP API server.

Modules read READONLY through this module (idasrv_config.READONLY) rather than
importing the name, so set_readonly() is visible to every caller at runtime.
"""

import logging
import os

import idaapi
import ida_hexrays

READONLY = True

REGISTRY_DIR = os.path.join(os.path.expanduser("~"), ".ida-mcp", "instances")

log = logging.getLogger("ida_api_server")
log.setLevel(logging.INFO)
if not log.handlers:
    _handler = logging.StreamHandler()
    _handler.setFormatter(logging.Formatter("[ida_api] %(levelname)s %(message)s"))
    log.addHandler(_handler)

HAS_HEXRAYS = False
if ida_hexrays.init_hexrays_plugin():
    HAS_HEXRAYS = True
    log.info("Hex-Rays decompiler available")
else:
    log.warning("Hex-Rays decompiler NOT available - /decompile will be limited")


def set_readonly(enabled):
    """Toggle read-only mode at runtime."""
    global READONLY
    READONLY = enabled
    log.info("Read-only mode: %s", READONLY)
    idaapi.msg(f"[ida_api] Read-only mode: {READONLY}\n")


def require_write():
    """Raise PermissionError when the server is in read-only mode."""
    if READONLY:
        raise PermissionError(
            "Server is in read-only mode. Set READONLY = False to enable write operations."
        )
