"""Data-oriented MCP tools: names, strings, segments, imports, exports, structs, enums."""

from __future__ import annotations

from typing import Any, Optional

from mcp.server.fastmcp.exceptions import ToolError

from ida_mcp_app import mcp
from ida_mcp_client import get_or_broadcast, ida_get


@mcp.tool()
def ida_get_names(
    target: str,
    limit: int = 0,
    offset: int = 0,
    name_filter: Optional[str] = None,
) -> dict[str, Any]:
    """
    List named addresses in the IDB.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        limit: Max entries to return. 0 = no limit.
        offset: Skip this many matches before returning. Use with limit to page
                through more results than one response can carry.
        name_filter: Case-insensitive substring filter.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "total_matched", "offset", "names": [{"ea", "name"}, ...]}
        count is the size of this page; total_matched is how many entries matched
        overall, so count < total_matched means more pages remain.
    """
    if limit < 0:
        raise ToolError("limit must be >= 0")
    if offset < 0:
        raise ToolError("offset must be >= 0")
    params: dict[str, str] = {"limit": str(limit), "offset": str(offset)}
    if name_filter:
        params["filter"] = name_filter
    return get_or_broadcast(target, "/api/names", params,
                        narrowed=bool(name_filter) or limit > 0,
                        narrowing_hint="Pass name_filter, or a limit.")


@mcp.tool()
def ida_get_strings(
    target: str,
    limit: int = 0,
    offset: int = 0,
    min_length: int = 4,
    content_filter: Optional[str] = None,
) -> dict[str, Any]:
    """
    List string literals found in the binary.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        limit: Max strings to return. 0 = no limit.
        offset: Skip this many matches before returning. Use with limit to page.
        min_length: Minimum string length to include (default 4).
        content_filter: Case-insensitive substring match on the string's text.
                        Prefer this over dumping every string in a large module.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "total_matched", "offset",
         "strings": [{"ea", "length", "type", "type_name", "value"}, ...]}
        type_name is the readable form of the numeric type, e.g. "C_16" for a
        UTF-16 literal. count < total_matched means more pages remain.
    """
    if limit < 0:
        raise ToolError("limit must be >= 0")
    if offset < 0:
        raise ToolError("offset must be >= 0")
    params: dict[str, str] = {
        "limit": str(limit),
        "offset": str(offset),
        "min_length": str(min_length),
    }
    if content_filter:
        params["filter"] = content_filter
    return get_or_broadcast(target, "/api/strings", params,
                        narrowed=bool(content_filter) or limit > 0,
                        narrowing_hint="Pass content_filter, or a limit.")


@mcp.tool()
def ida_get_segments(target: str) -> dict[str, Any]:
    """
    List all segments (sections) in the binary.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "segments": [{"start_ea", "end_ea", "name", "class", "size", "perm", "bitness"}, ...]}
    """
    return get_or_broadcast(target, "/api/segments")


@mcp.tool()
def ida_get_imports(target: str) -> dict[str, Any]:
    """
    List all imported modules and their functions.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "modules": {"dll_name": [{"ea", "name", "ordinal"}, ...], ...}}
    """
    return get_or_broadcast(target, "/api/imports")


@mcp.tool()
def ida_get_exports(target: str) -> dict[str, Any]:
    """
    List all exported entry points.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "exports": [{"index", "ordinal", "ea", "name"}, ...]}
    """
    return get_or_broadcast(target, "/api/exports")


@mcp.tool()
def ida_list_structs(target: str, name_filter: Optional[str] = None) -> dict[str, Any]:
    """
    List structure types defined in the IDB.

    Args:
        name_filter: Case-insensitive substring filter.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "structs": [{"index", "id", "name", "size", "is_union"}, ...]}
    """
    params: dict[str, str] = {}
    if name_filter:
        params["filter"] = name_filter
    return ida_get(target, "/api/structs", params)


@mcp.tool()
def ida_get_struct(name: str, target: str) -> dict[str, Any]:
    """
    Get full details of a struct by name, including all members.

    Args:
        name: Exact struct name.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"name", "id", "size", "is_union",
         "members": [{"offset", "name", "size", "type", "comment"}, ...]}
    """
    if not name:
        raise ToolError("name is required")
    return ida_get(target, "/api/struct", {"name": name})


@mcp.tool()
def ida_list_enums(target: str, name_filter: Optional[str] = None) -> dict[str, Any]:
    """
    List enum types defined in the IDB.

    Args:
        name_filter: Case-insensitive substring filter.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "enums": [{"id", "name", "is_bitfield", "member_count"}, ...]}
    """
    params: dict[str, str] = {}
    if name_filter:
        params["filter"] = name_filter
    return ida_get(target, "/api/enums", params)


@mcp.tool()
def ida_get_enum(name: str, target: str) -> dict[str, Any]:
    """
    Get full details of an enum by name, including all members.

    Args:
        name: Exact enum name.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"name", "id", "is_bitfield",
         "members": [{"name", "value", "value_hex"}, ...]}
    """
    if not name:
        raise ToolError("name is required")
    return ida_get(target, "/api/enum", {"name": name})
