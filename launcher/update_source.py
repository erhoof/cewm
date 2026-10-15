"""Picks the update channel's source. Both channels yield an AvailableUpdate,
so the offer dialog and the download path are identical for either."""
from __future__ import annotations

from typing import Optional

from app_settings import (CHANNEL_DISABLED, CHANNEL_UNSTABLE,
                          read_update_channel)
from available_update import AvailableUpdate
from bundles import DEFAULT_TIMEOUT
from update_channel_stable import fetch_latest_release
from update_channel_unstable import fetch_latest_ci_build


def fetch_update(timeout: int = DEFAULT_TIMEOUT) -> Optional[AvailableUpdate]:
    channel = read_update_channel()
    if channel == CHANNEL_DISABLED:
        return None
    if channel == CHANNEL_UNSTABLE:
        return fetch_latest_ci_build(timeout)
    return fetch_latest_release(timeout)
