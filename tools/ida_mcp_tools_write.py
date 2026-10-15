"""Write MCP tools. These return a 403 ToolError while the IDA server is in readonly mode."""

from __future__ import annotations

from typing import Any, Optional

from mcp.server.fastmcp.exceptions import ToolError

from ida_mcp_app import mcp
from ida_mcp_client import ida_post, normalize_ea


@mcp.tool()
def ida_rename(ea: str, name: str, target: str) -> dict[str, Any]:
    """
    Rename an address (function, global, label, etc).

    Args:
        ea: Hex address to rename.
        name: New name. Use "" to clear an existing name.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "name", "success": true}

    Raises:
        ToolError with a 403 message if the IDA server is in readonly mode.
    """
    return ida_post(target, "/api/rename", {"ea": normalize_ea(ea), "name": name})


@mcp.tool()
def ida_set_comment(target: str, ea: str, comment: str, repeatable: bool = False) -> dict[str, Any]:
    """
    Set a comment at an address.

    Args:
        ea: Hex address.
        comment: Comment text. Use "" to clear.
        repeatable: If true, set as a repeatable comment.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "comment", "repeatable", "success": true}
    """
    return ida_post(target, "/api/comment", {
        "ea": normalize_ea(ea),
        "comment": comment,
        "repeatable": repeatable,
    })


@mcp.tool()
def ida_set_func_comment(target: str, ea: str, comment: str, repeatable: bool = False) -> dict[str, Any]:
    """
    Set a comment on the function containing an address.

    Args:
        ea: Hex address within the target function.
        comment: Comment text. Use "" to clear.
        repeatable: If true, set as a repeatable comment.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "comment", "repeatable", "success": true}
    """
    return ida_post(target, "/api/func_comment", {
        "ea": normalize_ea(ea),
        "comment": comment,
        "repeatable": repeatable,
    })


@mcp.tool()
def ida_set_type(ea: str, type_decl: str, target: str) -> dict[str, Any]:
    """
    Apply a C type declaration at an address.

    Args:
        ea: Hex address.
        type_decl: C type string, e.g. "int __fastcall(int a, int b)"
                   or "struct MyStruct *". The trailing semicolon is
                   added automatically if missing.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "type", "success": true}
    """
    if not type_decl:
        raise ToolError("type_decl is required")
    return ida_post(target, "/api/set_type", {
        "ea": normalize_ea(ea),
        "type": type_decl,
    })


@mcp.tool()
def ida_create_function(target: str, start_ea: str, end_ea: Optional[str] = None) -> dict[str, Any]:
    """
    Create a function at the given address range. If end_ea is omitted,
    IDA will try to determine the function boundaries automatically.

    Args:
        start_ea: Hex address of the function start.
        end_ea: Optional hex address of the function end.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"start_ea", "end_ea", "success": true}
    """
    body: dict[str, str] = {"start_ea": normalize_ea(start_ea)}
    if end_ea:
        body["end_ea"] = normalize_ea(end_ea)
    return ida_post(target, "/api/create_function", body)


@mcp.tool()
def ida_delete_function(ea: str, target: str) -> dict[str, Any]:
    """
    Delete the function containing the given address.

    Args:
        ea: Hex address within the function to delete.
        target: Port, full path, or a unique substring/glob of it (e.g. "ddi.dll", "casio*nk.exe"). Ambiguous matches error and list candidates.

    Returns:
        {"ea", "success": true}
    """
    return ida_post(target, "/api/delete_function", {"ea": normalize_ea(ea)})
