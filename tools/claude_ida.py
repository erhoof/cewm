"""
IDA MCP Server (Unified)
------------------------
Single MCP server that discovers and routes to ALL running IDA instances.

IDA instances register themselves via ida_server.py into ~/.ida-mcp/instances/.
This server discovers them automatically - no hardcoded ports needed.

Tools live in the ida_mcp_* sibling modules:
    ida_mcp_app              FastMCP application object and configuration
    ida_mcp_client           instance discovery, target resolution, HTTP transport
    ida_mcp_tools_instances  ida_list_instances
    ida_mcp_tools_code       ping/info/bytes/disasm/decompile/function/xrefs/vtable/address
    ida_mcp_tools_data       names/strings/segments/imports/exports/structs/enums
    ida_mcp_tools_search     byte search
    ida_mcp_tools_write      rename/comment/type/function edits

Configure with environment variables:
    IDA_HTTP_TIMEOUT   Per-request timeout in seconds (default: 30.0)

Run:
    python claude_ida.py
"""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from ida_mcp_app import IDA_TIMEOUT, REGISTRY_DIR, logger, mcp

import ida_mcp_tools_instances  # noqa: F401
import ida_mcp_tools_code       # noqa: F401
import ida_mcp_tools_data       # noqa: F401
import ida_mcp_tools_search     # noqa: F401
import ida_mcp_tools_write      # noqa: F401


def main() -> None:
    """
    Run the MCP server over stdio.

    Environment variables:
        IDA_HTTP_TIMEOUT  Request timeout in seconds (default 30.0)
    """
    logger.info(
        "Starting unified IDA MCP server, registry=%s, timeout=%.1fs",
        REGISTRY_DIR,
        IDA_TIMEOUT,
    )
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
