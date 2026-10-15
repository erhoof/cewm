"""Shared device-model helpers: TreeSelection, board/device sort keys, the
table labels, and the search haystack - reused by the card list and the
Download ROMs window."""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import List, Optional

from device_state import DeviceBundle, PackageStatus
from board_database import sort_text
from board_info import (board_device_year, board_display_name, board_soc_label,
                        board_sort_key)


# Board-group row iids contain ':' which is invalid in Windows directory
# names, so they can never collide with a bundle directory name used as a
# device iid.
GROUP_IID_PREFIX    = "board-group::"


@dataclass
class TreeSelection:
    kind: str  # "none" | "group" | "device" | "category" | "package"
    device: Optional[DeviceBundle] = None
    package: Optional[PackageStatus] = None


def _sort_optional_text(value: object) -> tuple[bool, str]:
    text = sort_text(value)
    return (not bool(text), text)


def _sort_optional_int(value: object, *, missing_when_zero: bool = True) -> tuple[bool, int]:
    number = value if isinstance(value, int) and not isinstance(value, bool) else 0
    missing = number == 0 if missing_when_zero else False
    return (missing, number)


def _board_group_key(d: DeviceBundle) -> tuple[int, str]:
    return board_sort_key(board_display_name(d.meta.board_id))


def _device_group_name(d: DeviceBundle) -> str:
    return (d.meta.device_name or board_display_name(d.meta.board_id)
            or d.name)


def _device_group_key(d: DeviceBundle) -> tuple[int, str]:
    """Device-name group order: alphabetical within the board tiers of
    board_sort_key, so the Device Emulator board's device groups stay pinned
    last regardless of what its ROMs' device names are."""
    tier, _ = board_sort_key(board_display_name(d.meta.board_id))
    return (tier, sort_text(_device_group_name(d)))


def _device_sort_key(d: DeviceBundle) -> tuple:
    meta = d.meta
    version_missing = not (meta.os_ver_major or meta.os_ver_minor)
    return (
        sort_text(meta.device_name or d.name),
        _sort_optional_text(meta.os_name),
        _sort_optional_int(meta.os_ver_major, missing_when_zero=version_missing),
        _sort_optional_int(meta.os_ver_minor, missing_when_zero=version_missing),
        _sort_optional_int(_device_year(d)),
        sort_text(d.name),
    )


def _int_metadata(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) else 0


def _os_name_has_version(name: str, major: int, minor: int) -> bool:
    if not name or not (major or minor):
        return False

    version_pattern = (
        rf"(?<![\d.]){major}\.{minor}(?:\.\d+)*(?!\d)"
    )
    if re.search(version_pattern, name):
        return True

    if minor == 0:
        major_pattern = rf"(?<![\d.]){major}(?![\d.])"
        return bool(re.search(major_pattern, name))

    return False


def _table_os_label(d: DeviceBundle) -> str:
    meta = d.meta
    name = meta.os_name.strip() if isinstance(meta.os_name, str) else ""
    major = _int_metadata(meta.os_ver_major)
    minor = _int_metadata(meta.os_ver_minor)
    build = _int_metadata(meta.os_ver_build)
    language = meta.os_language.strip() if isinstance(meta.os_language, str) else ""
    year = _int_metadata(meta.os_year)
    # Parenthetical gathers CE version, then language, then OS year:
    # "(CE 4.2.1234, DE, 2003)", "(CE 4.2, 2003)", "(2003)". The build is
    # appended to the CE version when present; language is omitted when absent.
    # The CE version is dropped when the name already spells it out
    # (e.g. "Windows CE 4.2").
    paren: List[str] = []
    if (major or minor) and not _os_name_has_version(name, major, minor):
        version = f"CE {major}.{minor}"
        if build:
            version += f".{build}"
        paren.append(version)
    if language:
        paren.append(language)
    if year:
        paren.append(str(year))
    base = name or ("Unknown OS" if paren else "")
    return f"{base} ({', '.join(paren)})" if paren else base


def _device_year(d: DeviceBundle) -> int:
    return board_device_year(d.meta.board_id)


def _table_device_label(d: DeviceBundle) -> str:
    display = (d.meta.device_name or board_display_name(d.meta.board_id)
               or d.name)
    year = _device_year(d)
    return f"{display} ({year})" if year else display


def _device_search_haystack(d: DeviceBundle) -> str:
    parts: List[str] = [
        d.meta.name,
        _table_device_label(d),
        _table_os_label(d),
        board_display_name(d.meta.board_id),
        board_soc_label(d.meta.board_id),
        d.state_label,
        d.name,
    ]
    parts.extend(d.meta.os_notes or [])
    parts.extend(ps.remote.name for ps in d.packages)
    return "\n".join(parts).lower()
