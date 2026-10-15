"""Code-oriented MCP tools: ping, info, bytes, disasm, decompile, function context,
function listing, xrefs, vtable, address info."""

from __future__ import annotations

from typing import Any, Literal, Optional

from mcp.server.fastmcp.exceptions import ToolError

from ida_mcp_app import mcp
from ida_mcp_client import get_or_broadcast, ida_get, normalize_ea


@mcp.tool()
def ida_ping(target: str) -> dict[str, Any]:
    """
    Health check. Returns IDA version, Hex-Rays availability, and readonly status.

    Args:
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.
    """
    return ida_get(target, "/api/ping")


@mcp.tool()
def ida_info(target: str) -> dict[str, Any]:
    """
    Get metadata about the loaded IDB: file path, imagebase, architecture,
    pointer size, segment bounds, Hex-Rays availability, readonly mode.

    Args:
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.
    """
    return ida_get(target, "/api/info")


@mcp.tool()
def ida_get_bytes(target: str, ea: str, size: int = 256) -> dict[str, Any]:
    """
    Read raw bytes at an address.

    Args:
        ea: Hex address string (e.g. "0x401000").
        size: Number of bytes to read (default 256).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "size", "bytes_hex"} where bytes_hex is a lowercase hex string.
    """
    if size <= 0:
        raise ToolError("size must be positive")
    return ida_get(target, "/api/bytes", {"ea": normalize_ea(ea), "size": str(size)})


@mcp.tool()
def ida_read_values(
    target: str,
    ea: str,
    value_type: Literal["byte", "word", "dword", "qword"] = "dword",
    count: int = 1,
) -> dict[str, Any]:
    """
    Read memory as decoded integers rather than a hex blob - pointer tables,
    vector tables, struct fields. Prefer over ida_get_bytes whenever you care
    about values: elements come assembled per the IDB's byte order in decimal
    and hex, so nothing needs byte-swapping by hand.

    Args:
        ea: Hex address to start reading at.
        value_type: "byte", "word", "dword" (default) or "qword".
        count: Elements to read (default 1, max 4096).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "type", "element_size", "count", "endian",
         "values": [{"ea", "value", "hex", "points_to_name"?, "points_to_func"?}, ...]}
        Values landing on a named address resolve to points_to_name /
        points_to_func. Unmapped elements report {"ea", "loaded": false}.
    """
    if count <= 0:
        raise ToolError("count must be positive")
    if value_type not in ("byte", "word", "dword", "qword"):
        raise ToolError("value_type must be 'byte', 'word', 'dword' or 'qword'")
    return ida_get(target, "/api/values", {
        "ea": normalize_ea(ea),
        "type": value_type,
        "count": str(count),
    })


@mcp.tool()
def ida_get_disasm(target: str, ea: str, count: int = 50) -> dict[str, Any]:
    """
    Get disassembly lines starting at an address.

    Args:
        ea: Hex address string.
        count: Max number of instructions (default 50).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"start_ea", "count", "disasm": [{"ea", "text"}, ...]}
    """
    if count <= 0:
        raise ToolError("count must be positive")
    return ida_get(target, "/api/disasm", {"ea": normalize_ea(ea), "count": str(count)})


@mcp.tool()
def ida_decompile(ea: str, target: str) -> dict[str, Any]:
    """
    Decompile the function containing the given address using Hex-Rays.

    Args:
        ea: Hex address string.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "function": {"name", "start_ea", "end_ea"}, "pseudocode"}
    """
    return ida_get(target, "/api/decompile", {"ea": normalize_ea(ea)})


@mcp.tool()
def ida_get_function_context(
    ea: str,
    target: str,
    sections: Optional[str] = None,
    max_disasm: int = 200,
) -> dict[str, Any]:
    """
    Disassembly, pseudocode, callers, callees, xrefs and comments for the
    function containing an address. Widest response of any tool - narrow it
    with sections, e.g. sections="pseudocode,callees".

    Args:
        ea: Hex address string.
        sections: Comma-separated subset of
                  "disasm,pseudocode,xrefs,callers,callees,comments,bytes".
                  Omit for all; only requested keys are returned.
        max_disasm: Cap on disasm lines (default 200, 0 = no cap).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "in_function", "sections", "function", "bytes_at_ea",
         "disasm", "disasm_total", "disasm_truncated", "pseudocode",
         "xrefs_from", "xrefs_to", "callers", "callees", "function_comment",
         "function_repeatable_comment", "instr_comments"}
        callers/callees are [{"ea", "name"}, ...], already resolved.
    """
    if max_disasm < 0:
        raise ToolError("max_disasm must be >= 0")
    params: dict[str, str] = {"ea": normalize_ea(ea), "max_disasm": str(max_disasm)}
    if sections:
        params["sections"] = sections
    return ida_get(target, "/api/function", params)


@mcp.tool()
def ida_list_functions(
    target: str,
    limit: int = 0,
    offset: int = 0,
    name_filter: Optional[str] = None,
    mode: Literal["fast", "full"] = "fast",
) -> dict[str, Any]:
    """
    List functions known to IDA.

    Broadcast: target="*" or "all:<pattern>" queries every instance in parallel;
    modules with no matches are omitted.

    Args:
        limit: Max functions to return. 0 = no limit.
        offset: Skip this many matches before returning. Use with limit to page
                through more functions than one response can carry.
        name_filter: Case-insensitive substring filter on function names.
        mode: "fast" for basic info, "full" to also count xrefs_to and include type.
              The extra work in "full" is done only for the returned page.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"count", "total_matched", "offset",
         "functions": [{"start_ea", "end_ea", "name", "size", ...}, ...]}
        count < total_matched means more pages remain.
    """
    if limit < 0:
        raise ToolError("limit must be >= 0")
    if offset < 0:
        raise ToolError("offset must be >= 0")
    if mode not in ("fast", "full"):
        raise ToolError("mode must be 'fast' or 'full'")
    params: dict[str, str] = {"limit": str(limit), "offset": str(offset), "mode": mode}
    if name_filter:
        params["filter"] = name_filter
    return get_or_broadcast(target, "/api/functions", params,
                        narrowed=bool(name_filter) or limit > 0,
                        narrowing_hint="Pass name_filter, or a limit.")


@mcp.tool()
def ida_get_xrefs(
    target: str,
    ea: str,
    direction: Literal["from", "to", "both"] = "both",
) -> dict[str, Any]:
    """
    Get cross-references for an address.

    Args:
        ea: Hex address string.
        direction: "from" (outgoing), "to" (incoming), or "both".
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "xrefs_from": [...], "xrefs_to": [...]}
        Each xref: {from, to, type, type_name} plus, where available,
        from_func_ea / from_func_name, to_name, and disasm. Read from_func_name
        to see who references an address: on RISC targets the raw "from" is
        often a literal pool slot, not the using code.
    """
    if direction not in ("from", "to", "both"):
        raise ToolError("direction must be 'from', 'to', or 'both'")
    return ida_get(target, "/api/xrefs", {"ea": normalize_ea(ea), "direction": direction})


@mcp.tool()
def ida_get_vtable(target: str, ea: str, count: int = 64) -> dict[str, Any]:
    """
    Read a vtable as an array of pointers at a given address.
    Each pointer is resolved to a function name where possible.
    Stops early if a pointer target looks invalid (null, out of bounds).

    Args:
        ea: Hex address of the vtable start.
        count: Max number of slots to read (default 64).
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "pointer_size", "count",
         "entries": [{"index", "slot_ea", "target", "name", "is_function"}, ...]}
    """
    if count <= 0:
        raise ToolError("count must be positive")
    return ida_get(target, "/api/vtable", {"ea": normalize_ea(ea), "count": str(count)})


@mcp.tool()
def ida_get_address_info(ea: str, target: str) -> dict[str, Any]:
    """
    Get detailed information about a single address: name, type, segment,
    flags (code/data/head/tail), containing function, comments, raw bytes.

    Args:
        ea: Hex address string.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "name", "type", "segment", "is_code", "is_data", "is_head",
         "is_tail", "in_function", "function_name", "function_start",
         "comment", "repeatable_comment", "item_size", "bytes_hex"}
    """
    return ida_get(target, "/api/address", {"ea": normalize_ea(ea)})
