"""Shared FastMCP application object and configuration for the IDA MCP server.

Tool modules import `mcp` from here and register with @mcp.tool(); claude_ida.py
imports those modules so the registrations happen before mcp.run().
"""

from __future__ import annotations

import logging
import os

from mcp.server.fastmcp import FastMCP

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("ida-mcp")

mcp = FastMCP("ida-tools")

IDA_TIMEOUT = float(os.getenv("IDA_HTTP_TIMEOUT", "30.0"))
REGISTRY_DIR = os.path.join(os.path.expanduser("~"), ".ida-mcp", "instances")
