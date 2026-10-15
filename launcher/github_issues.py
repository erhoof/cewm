from __future__ import annotations

import json
import urllib.request
from dataclasses import dataclass
from typing import List, Optional

from bundles import BundleError, DEFAULT_TIMEOUT, USER_AGENT


ISSUES_API_URL = ("https://api.github.com/search/issues"
                  "?q=repo:gweslab/cerf+is:issue+is:open"
                  "&sort=reactions&order=desc&per_page=100")


@dataclass(frozen=True)
class GithubIssue:
    number: int
    title: str
    html_url: str
    reactions: int
    comments: int


def _fetch_json(url: str, timeout: int) -> object:
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/vnd.github+json",
    })
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
    except Exception as exc:
        raise BundleError(f"GitHub issue API unreachable: {exc}") from exc
    try:
        parsed = json.loads(raw.decode("utf-8"))
    except ValueError as exc:
        raise BundleError(
            f"GitHub issue API returned malformed JSON: {exc}") from exc
    return parsed


def _items(payload: object) -> list:
    if not isinstance(payload, dict):
        raise BundleError("GitHub issue API returned an unexpected payload")
    items = payload.get("items")
    if not isinstance(items, list):
        raise BundleError("GitHub issue API returned no item list")
    return items


def _parse_issue(entry: object) -> Optional[GithubIssue]:
    if not isinstance(entry, dict) or "pull_request" in entry:
        return None
    number, title = entry.get("number"), entry.get("title")
    html_url = entry.get("html_url")
    if not isinstance(number, int) or not isinstance(title, str):
        return None
    if not isinstance(html_url, str) or not html_url:
        return None
    reactions = entry.get("reactions")
    total = reactions.get("total_count") if isinstance(reactions, dict) else 0
    comments = entry.get("comments")
    return GithubIssue(
        number=number,
        title=title.strip() or f"Issue #{number}",
        html_url=html_url,
        reactions=total if isinstance(total, int) else 0,
        comments=comments if isinstance(comments, int) else 0,
    )


def fetch_open_issues(timeout: int = DEFAULT_TIMEOUT) -> List[GithubIssue]:
    parsed = [_parse_issue(entry)
              for entry in _items(_fetch_json(ISSUES_API_URL, timeout))]
    issues = [issue for issue in parsed if issue is not None]
    issues.sort(key=lambda i: (-i.reactions, -i.comments, -i.number))
    return issues
