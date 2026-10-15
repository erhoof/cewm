"""The unstable channel: every CI build, published to R2 as latest.json plus
the build zip beside it."""
from __future__ import annotations

import json
import urllib.request

from available_update import AvailableUpdate
from bundles import BundleError, DEFAULT_TIMEOUT, USER_AGENT

CI_BASE_URL = "https://cerf-bundles.dz3n.net/cerf_ci"
CI_LATEST_NAME = "latest.json"
CI_RUNS_URL = "https://github.com/gweslab/cerf/actions"


def latest_url() -> str:
    return CI_BASE_URL + "/" + CI_LATEST_NAME


def _require_int(payload: dict, key: str) -> int:
    value = payload.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        raise BundleError(f"{CI_LATEST_NAME} has no integer {key}")
    return value


def _object_name(payload: dict) -> str:
    name = payload.get("object_name")
    if not isinstance(name, str) or not name:
        raise BundleError(f"{CI_LATEST_NAME} has no object_name")
    if "/" in name or "\\" in name or name.startswith("."):
        raise BundleError(f"{CI_LATEST_NAME} object_name is not a plain name")
    return name


def fetch_latest_ci_build(timeout: int = DEFAULT_TIMEOUT) -> AvailableUpdate:
    request = urllib.request.Request(latest_url(),
                                     headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except Exception as exc:
        raise BundleError(f"CI build feed unreachable: {exc}") from exc
    try:
        payload = json.loads(raw.decode("utf-8"))
    except ValueError as exc:
        raise BundleError(f"{CI_LATEST_NAME} is malformed: {exc}") from exc
    if not isinstance(payload, dict):
        raise BundleError(f"{CI_LATEST_NAME} is not an object")

    tag = "{}.{}.{}.{}".format(_require_int(payload, "ver_major"),
                               _require_int(payload, "ver_minor"),
                               _require_int(payload, "ver_patch"),
                               _require_int(payload, "ver_build"))
    name = _object_name(payload)
    size = payload.get("object_size")
    body = payload.get("changelog")
    return AvailableUpdate(
        tag=tag,
        title=tag,
        body=body if isinstance(body, str) else "",
        html_url=CI_RUNS_URL,
        asset_name=name,
        asset_url=CI_BASE_URL + "/" + name,
        asset_size=size if isinstance(size, int) and not isinstance(size, bool)
        else None,
    )
