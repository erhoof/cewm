"""Write endpoints, gated behind idasrv_config.READONLY."""

import idc
import ida_funcs
import ida_name

from idasrv_config import require_write
from idasrv_util import get_type_str, hex_ea


def ep_rename(ea, new_name):
    require_write()
    ok = ida_name.set_name(ea, new_name, ida_name.SN_CHECK)
    if not ok:
        raise RuntimeError(f"Failed to rename {hex_ea(ea)} to '{new_name}'")
    return {"ea": hex_ea(ea), "name": new_name, "success": True}


def ep_set_comment(ea, comment, repeatable):
    require_write()
    ok = idc.set_cmt(ea, comment, repeatable)
    if not ok:
        raise RuntimeError(f"Failed to set comment at {hex_ea(ea)}")
    return {"ea": hex_ea(ea), "comment": comment, "repeatable": repeatable, "success": True}


def ep_set_func_comment(ea, comment, repeatable):
    require_write()
    func = ida_funcs.get_func(ea)
    if func is None:
        raise ValueError(f"No function at {hex_ea(ea)}")
    ok = idc.set_func_cmt(func.start_ea, comment, repeatable)
    if not ok:
        raise RuntimeError(f"Failed to set function comment at {hex_ea(ea)}")
    return {"ea": hex_ea(func.start_ea), "comment": comment, "repeatable": repeatable, "success": True}


def ep_set_type(ea, type_str):
    """Apply a C type declaration at *ea* (e.g. 'int __fastcall func(int a, int b)')."""
    require_write()
    decl = type_str.rstrip(";") + ";"
    ok = idc.SetType(ea, decl)
    if not ok:
        raise RuntimeError(f"Failed to apply type '{type_str}' at {hex_ea(ea)}")
    applied = get_type_str(ea)
    return {"ea": hex_ea(ea), "type": applied or type_str, "success": True}


def ep_create_function(start_ea, end_ea):
    require_write()
    if end_ea:
        ok = ida_funcs.add_func(start_ea, end_ea)
    else:
        ok = ida_funcs.add_func(start_ea)
    if not ok:
        raise RuntimeError(f"Failed to create function at {hex_ea(start_ea)}")
    func = ida_funcs.get_func(start_ea)
    return {
        "start_ea": hex_ea(func.start_ea) if func else hex_ea(start_ea),
        "end_ea": hex_ea(func.end_ea) if func else None,
        "success": True,
    }


def ep_delete_function(ea):
    require_write()
    func = ida_funcs.get_func(ea)
    if func is None:
        raise ValueError(f"No function at {hex_ea(ea)}")
    ok = ida_funcs.del_func(func.start_ea)
    if not ok:
        raise RuntimeError(f"Failed to delete function at {hex_ea(func.start_ea)}")
    return {"ea": hex_ea(func.start_ea), "success": True}
