from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Dict, List, Optional

from app_paths import install_root

DB_FILENAME = "db.json"


def _db_path() -> Optional[Path]:
    candidates = [install_root() / DB_FILENAME,
                  Path(os.path.abspath(__file__)).parent.parent / "bundled"
                  / DB_FILENAME]
    for path in candidates:
        if path.is_file():
            return path
    return None


class BoardDatabaseError(RuntimeError):
    pass


def _load() -> dict:
    path = _db_path()
    if path is None:
        return {}
    try:
        with path.open("r", encoding="utf-8-sig") as f:
            text = f.read()
    except OSError as exc:
        raise BoardDatabaseError("{} is unreadable: {}".format(path, exc))
    if not text.strip():
        return {}
    try:
        obj = json.loads(text)
    except json.JSONDecodeError as exc:
        raise BoardDatabaseError("{} is unreadable: {}".format(path, exc))
    if not isinstance(obj, dict):
        raise BoardDatabaseError(
            "{} must hold a JSON object at the top level".format(path))
    return obj


def _objects(table: str) -> List[dict]:
    rows = _DB.get(table)
    if not isinstance(rows, list):
        return []
    return [r for r in rows if isinstance(r, dict)]


def _rows(table: str) -> List[dict]:
    return [r for r in _objects(table) if isinstance(r.get("id"), str)]


def _by_id(table: str) -> Dict[str, dict]:
    return {r["id"]: r for r in _rows(table)}


def _list(row: dict, key: str) -> list:
    value = row.get(key)
    return value if isinstance(value, list) else []


def _merged_features(family: dict, board: dict) -> List[dict]:
    merged: Dict[str, dict] = {}
    for row in (family, board):
        for f in _list(row, "device_features"):
            if isinstance(f, dict) and isinstance(f.get("feature_id"), str):
                merged[f["feature_id"]] = f
    return list(merged.values())


def _effective_board(board: dict) -> dict:
    family = BOARD_FAMILIES_BY_ID.get(board.get("board_family_id", ""), {})
    out = {k: v for k, v in family.items() if k not in ("id", "name")}
    out.update(board)
    out["device_features"] = _merged_features(family, board)
    out["notes"] = _list(family, "notes") + _list(board, "notes")
    return out


_DB = _load()

SOC_FAMILIES = _by_id("soc_families")
SOCS = _by_id("socs")
DEVICE_FEATURES = _rows("device_features")
BOARD_FAMILIES = _rows("board_families")
BOARD_FAMILIES_BY_ID = _by_id("board_families")
BOARDS = [_effective_board(b) for b in _rows("boards")]
BOARDS_BY_ID = {b["id"]: b for b in BOARDS}
ROM_TYPES = _rows("rom_types")
STORAGE_TYPES = _rows("storage_types")
CPU_CONFIG_PARAMS = _rows("cpu_config_params")
CPU_CONFIGS = _objects("cpu_configs")

FEATURE_SPECS = [(f["id"], f.get("icon", f["id"]), f.get("name", f["id"]))
                 for f in DEVICE_FEATURES]


def ga_shared_folder_mount_point() -> str:
    value = _DB.get("ga_shared_folder_mount_point")
    return value if isinstance(value, str) else ""


def sort_text(value: object) -> str:
    if not isinstance(value, str):
        return ""
    return " ".join(value.casefold().split())


def board(board_id: object) -> Optional[dict]:
    if not isinstance(board_id, str):
        return None
    return BOARDS_BY_ID.get(board_id.strip())


def family_of(board_id: object) -> Optional[dict]:
    entry = board(board_id)
    return BOARD_FAMILIES_BY_ID.get(entry.get("board_family_id", "")) if entry else None


def soc_of(board_id: object) -> Optional[dict]:
    entry = board(board_id)
    return SOCS.get(entry.get("soc_id", "")) if entry else None


def soc_family_of(board_id: object) -> Optional[dict]:
    soc = soc_of(board_id)
    return SOC_FAMILIES.get(soc.get("family_id", "")) if soc else None
