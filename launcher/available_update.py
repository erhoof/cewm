from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class AvailableUpdate:
    tag: str
    title: str
    body: str
    html_url: str
    asset_name: str
    asset_url: str
    asset_size: Optional[int]
