"""Instance discovery tool."""

from __future__ import annotations

from typing import Any

from ida_mcp_app import mcp
from ida_mcp_client import discover_live_instances


@mcp.tool()
def ida_list_instances() -> dict[str, Any]:
    """
    List running IDA instances.

    Every other ida_ tool takes a "target" naming one: its port, its full
    instance_id path, or any substring/glob matching exactly one (e.g.
    "ddi.dll", "casio*nk.exe"). A pattern matching several is rejected with the
    candidates listed, never resolved by guessing - call this to disambiguate.

    Returns:
        {"count": N, "instances": [{"instance_id", "port", "pid", "started_at"}, ...]}
    """
    instances = discover_live_instances(force=True)
    cleaned = []
    for inst in instances:
        cleaned.append({
            "instance_id": inst.get("instance_id"),
            "port": inst.get("port"),
            "pid": inst.get("pid"),
            "started_at": inst.get("started_at"),
            "ida_version": inst.get("ida_version"),
        })
    return {"count": len(cleaned), "instances": cleaned}
