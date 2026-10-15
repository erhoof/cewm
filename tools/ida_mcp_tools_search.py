"""Search MCP tools."""

from __future__ import annotations

from typing import Any, Literal, Optional

from mcp.server.fastmcp.exceptions import ToolError

from ida_mcp_app import mcp
from ida_mcp_client import get_or_broadcast, ida_get, normalize_ea


@mcp.tool()
def ida_search_bytes(
    target: str,
    pattern: str,
    start: Optional[str] = None,
    direction: Literal["down", "up"] = "down",
    max_results: int = 100,
) -> dict[str, Any]:
    """
    Search for a byte pattern in the binary.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        pattern: Hex byte pattern with optional '??' wildcards,
                 e.g. "48 8B ?? 10" or "E8 ?? ?? ?? FF".
        start: Hex address to start searching from. Defaults to min_ea (down) or max_ea (up).
        direction: "down" (forward) or "up" (backward). Default "down".
        max_results: Max matches to return (default 100).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"pattern", "count", "results": [{"ea", "name"}, ...]}
    """
    if not pattern:
        raise ToolError("pattern is required")
    if direction not in ("down", "up"):
        raise ToolError("direction must be 'down' or 'up'")
    if max_results <= 0:
        raise ToolError("max_results must be positive")
    params: dict[str, str] = {
        "pattern": pattern,
        "direction": direction,
        "max_results": str(max_results),
    }
    if start:
        params["start"] = normalize_ea(start)
    return get_or_broadcast(target, "/api/search", params)


@mcp.tool()
def ida_search_text(
    target: str,
    text: str,
    encoding: Literal["ascii", "utf16", "both"] = "both",
    start: Optional[str] = None,
    direction: Literal["down", "up"] = "down",
    max_results: int = 100,
) -> dict[str, Any]:
    """
    Search for literal text. The text is encoded to bytes server-side, so never
    hand-encode it for ida_search_bytes. CE ROMs store strings as UTF-16LE,
    hence "both" by default. Matching is CASE-SENSITIVE (raw byte compare).

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        text: Literal text, e.g. "Halting system".
        encoding: "ascii", "utf16", or "both" (default, merges both).
        start: Hex address to start from. Defaults to min_ea (down) / max_ea (up).
        direction: "down" (default) or "up".
        max_results: Max matches (default 100).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"text", "encoding", "count",
         "results": [{"ea", "name", "encoding", "pattern"}, ...]}
        Matches bytes anywhere, including inside a longer string; to enumerate
        defined string literals use ida_get_strings with content_filter.
    """
    if not text:
        raise ToolError("text is required")
    if encoding not in ("ascii", "utf16", "both"):
        raise ToolError("encoding must be 'ascii', 'utf16' or 'both'")
    if direction not in ("down", "up"):
        raise ToolError("direction must be 'down' or 'up'")
    if max_results <= 0:
        raise ToolError("max_results must be positive")
    params: dict[str, str] = {
        "text": text,
        "encoding": encoding,
        "direction": direction,
        "max_results": str(max_results),
    }
    if start:
        params["start"] = normalize_ea(start)
    return get_or_broadcast(target, "/api/search_text", params)


@mcp.tool()
def ida_search_immediate(
    target: str,
    value: str,
    include_halves: bool = False,
    start: Optional[str] = None,
    end: Optional[str] = None,
    max_results: int = 100,
) -> dict[str, Any]:
    """
    Find instructions whose operands carry a constant. Answers "what touches
    MMIO 0x0B000018", which xrefs cannot: that address is not inside the IDB.
    Every hit names its containing function.

    MIPS/ARM build a 32-bit constant from two instructions (lui/ori, lui/addiu),
    so it never appears whole in one operand - if an exact search finds nothing,
    retry with include_halves=true. A 0x0000 half is skipped, so a page-aligned
    base matches on its high half only. Hits rank exact/address above high16
    above low16.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        value: Constant as hex, e.g. "0x0B000018".
        include_halves: Also match either 16-bit half. Default false.
        start, end: Optional hex bounds on the scan. Default: whole image.
        max_results: Max hits (default 100).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"value_hex", "count", "total_matches", "matches_by_kind",
         "scanned_instructions", "truncated",
         "results": [{"ea", "op_index", "matched_as", "func_ea", "func_name", "disasm"}, ...]}
        matched_as is "immediate" | "address" | "high16" | "low16";
        matches_by_kind gives the signal/noise split before you read results.
    """
    if not value:
        raise ToolError("value is required")
    if max_results <= 0:
        raise ToolError("max_results must be positive")
    params: dict[str, str] = {
        "value": normalize_ea(value),
        "include_halves": "1" if include_halves else "0",
        "max_results": str(max_results),
    }
    if start:
        params["start"] = normalize_ea(start)
    if end:
        params["end"] = normalize_ea(end)
    return get_or_broadcast(target, "/api/search_immediate", params)
