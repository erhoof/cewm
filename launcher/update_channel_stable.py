from __future__ import annotations

import json
import urllib.request

from available_update import AvailableUpdate
from bundles import BundleError, DEFAULT_TIMEOUT, USER_AGENT


LATEST_RELEASE_API_URL = \
    "https://api.github.com/repos/gweslab/cerf/releases/latest"
ASSET_NAME_MARKER = "Release-Win32"
ASSET_NAME_SUFFIX = ".zip"


def _fetch_json(url: str, timeout: int) -> dict:
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/vnd.github+json",
    })
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except Exception as exc:
        raise BundleError(f"GitHub release API unreachable: {exc}") from exc
    try:
        parsed = json.loads(raw.decode("utf-8"))
    except ValueError as exc:
        raise BundleError(f"GitHub release API returned malformed JSON: {exc}") from exc
    if not isinstance(parsed, dict):
        raise BundleError("GitHub release API returned an unexpected payload")
    return parsed


def _pick_asset(payload: dict) -> dict:
    assets = payload.get("assets")
    if not isinstance(assets, list):
        raise BundleError("GitHub release carries no asset list")
    for asset in assets:
        if not isinstance(asset, dict):
            continue
        name = asset.get("name")
        url = asset.get("browser_download_url")
        if not isinstance(name, str) or not isinstance(url, str):
            continue
        if ASSET_NAME_MARKER in name and name.endswith(ASSET_NAME_SUFFIX):
            return asset
    raise BundleError(
        f"GitHub release has no {ASSET_NAME_MARKER}{ASSET_NAME_SUFFIX} asset")


def fetch_latest_release(timeout: int = DEFAULT_TIMEOUT) -> AvailableUpdate:
    payload = _fetch_json(LATEST_RELEASE_API_URL, timeout)
    tag = payload.get("tag_name")
    if not isinstance(tag, str) or not tag.strip():
        raise BundleError("GitHub release has no tag_name")
    asset = _pick_asset(payload)
    size = asset.get("size")
    title = payload.get("name")
    body = payload.get("body")
    html_url = payload.get("html_url")
    return AvailableUpdate(
        tag=tag.strip(),
        title=title if isinstance(title, str) and title else tag.strip(),
        body=body if isinstance(body, str) else "",
        html_url=html_url if isinstance(html_url, str) and html_url
        else "https://github.com/gweslab/cerf/releases/latest",
        asset_name=asset["name"],
        asset_url=asset["browser_download_url"],
        asset_size=size if isinstance(size, int) and not isinstance(size, bool)
        else None,
    )
