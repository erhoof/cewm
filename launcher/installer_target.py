from __future__ import annotations

import os
from pathlib import Path

from branding import PRODUCT_NAME


def default_install_dir() -> Path:
    roaming = os.environ.get("APPDATA")
    if roaming:
        return Path(roaming) / PRODUCT_NAME
    return Path.home() / PRODUCT_NAME


CHANGE_DIRECTORY_WARNING = (
    "Are you sure you want to change the directory?\n\n"
    "If you have another installation on this computer, the target directory "
    "is from now on treated as the only one. When you uninstall, only this "
    "directory is removed.\n\n"
    "Prefer a directory under your user profile. A system-wide path asks for "
    "Administrator rights and can break later upgrades.")
