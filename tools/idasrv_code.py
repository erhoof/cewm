"""Code-oriented endpoints: ping, info, bytes, disasm, decompile, function, address, vtable."""

import idaapi
import idc
import ida_bytes
import ida_funcs
import ida_ida
import ida_nalt
import ida_hexrays
import idautils

import idasrv_config
from idasrv_util import (
    callees_of,
    callers_of,
    disasm_at,
    get_bytes_hex,
    get_name,
    get_type_str,
    hex_ea,
    paginate,
    pointer_size,
    read_pointer,
    xrefs_from,
    xrefs_to,
)


def ep_ping():
    return {
        "status": "ok",
        "ida_version": idaapi.get_kernel_version(),
        "hexrays": idasrv_config.HAS_HEXRAYS,
    }


def ep_info():
    ptr_size = pointer_size()
    return {
        "file_path": idaapi.get_input_file_path(),
        "file_md5": ida_nalt.retrieve_input_file_md5().hex() if ida_nalt.retrieve_input_file_md5() else None,
        "imagebase": hex_ea(ida_nalt.get_imagebase()),
        "min_ea": hex_ea(ida_ida.inf_get_min_ea()),
        "max_ea": hex_ea(ida_ida.inf_get_max_ea()),
        "processor": ida_ida.inf_get_procname(),
        "bits": ptr_size * 8,
        "is_be": ida_ida.inf_is_be(),
        "is_dll": ida_ida.inf_is_dll(),
        "pointer_size": ptr_size,
        "hexrays": idasrv_config.HAS_HEXRAYS,
        "readonly": idasrv_config.READONLY,
    }


def ep_bytes(ea, size):
    return {
        "ea": hex_ea(ea),
        "size": size,
        "bytes_hex": get_bytes_hex(ea, size),
    }


_VALUE_READERS = {
    "byte": (1, ida_bytes.get_byte),
    "word": (2, ida_bytes.get_word),
    "dword": (4, ida_bytes.get_dword),
    "qword": (8, ida_bytes.get_qword),
}

_MAX_VALUE_COUNT = 4096


def ep_read_values(ea, value_type, count):
    """
    Read *count* elements of *value_type* at *ea* as decoded integers.

    Values come back already assembled per the database's byte order, so the
    caller never reassembles bytes by hand. A value that lands on a named
    address is resolved, which makes pointer and vector tables readable in one
    call.
    """
    if value_type not in _VALUE_READERS:
        raise ValueError(
            f"value_type must be one of {sorted(_VALUE_READERS)}. Got: {value_type!r}")
    if count <= 0:
        raise ValueError("count must be positive")
    if count > _MAX_VALUE_COUNT:
        raise ValueError(f"count must be <= {_MAX_VALUE_COUNT}")

    size, reader = _VALUE_READERS[value_type]
    low = ida_ida.inf_get_min_ea()
    high = ida_ida.inf_get_max_ea()

    values = []
    for index in range(count):
        cur = ea + index * size
        record = {"ea": hex_ea(cur)}
        if not ida_bytes.is_loaded(cur):
            record["loaded"] = False
            values.append(record)
            continue
        raw = reader(cur)
        record["value"] = raw
        record["hex"] = hex_ea(raw)
        if low <= raw < high:
            name = get_name(raw)
            if name:
                record["points_to_name"] = name
            func = ida_funcs.get_func(raw)
            if func:
                record["points_to_func"] = ida_funcs.get_func_name(func.start_ea)
        values.append(record)

    return {
        "ea": hex_ea(ea),
        "type": value_type,
        "element_size": size,
        "count": len(values),
        "endian": "big" if ida_ida.inf_is_be() else "little",
        "values": values,
    }


def ep_disasm(ea, count):
    lines = disasm_at(ea, count)
    return {
        "start_ea": hex_ea(ea),
        "count": len(lines),
        "disasm": lines,
    }


def _no_function_detail(ea):
    """Explain why *ea* has no function and what to do instead."""
    parts = [f"No function at {hex_ea(ea)}."]

    if not ida_bytes.is_loaded(ea):
        parts.append("That address is not mapped in this IDB"
                     f" (loaded range {hex_ea(ida_ida.inf_get_min_ea())}-"
                     f"{hex_ea(ida_ida.inf_get_max_ea())}).")
        parts.append("Check you are querying the right module.")
        return " ".join(parts)

    seg = idc.get_segm_name(ea)
    flags = ida_bytes.get_flags(ea)
    if ida_bytes.is_code(flags):
        kind = "code that IDA has not attributed to any function"
    elif ida_bytes.is_data(flags):
        kind = "data"
    else:
        kind = "unexplored bytes"
    parts.append(f"Segment {seg or 'none'}; the bytes there are {kind}.")

    prev_func = ida_funcs.get_prev_func(ea)
    if prev_func:
        parts.append(
            f"Nearest preceding function {ida_funcs.get_func_name(prev_func.start_ea)} "
            f"[{hex_ea(prev_func.start_ea)}-{hex_ea(prev_func.end_ea)}).")
    next_func = ida_funcs.get_next_func(ea)
    if next_func:
        parts.append(
            f"Next function {ida_funcs.get_func_name(next_func.start_ea)} "
            f"at {hex_ea(next_func.start_ea)}.")

    parts.append("Decompile a listed function instead, or inspect this address "
                 "with ida_get_disasm / ida_get_address_info.")
    return " ".join(parts)


def ep_decompile(ea):
    """Decompile the function containing *ea*."""
    if not idasrv_config.HAS_HEXRAYS:
        raise RuntimeError("Hex-Rays decompiler is not available")
    func = ida_funcs.get_func(ea)
    if func is None:
        raise ValueError(_no_function_detail(ea))
    cfunc = ida_hexrays.decompile(func)
    if cfunc is None:
        raise RuntimeError(f"Decompilation failed for function at {hex_ea(func.start_ea)}")
    return {
        "ea": hex_ea(ea),
        "function": {
            "name": ida_funcs.get_func_name(func.start_ea),
            "start_ea": hex_ea(func.start_ea),
            "end_ea": hex_ea(func.end_ea),
        },
        "pseudocode": str(cfunc),
    }


FUNCTION_SECTIONS = ("disasm", "pseudocode", "xrefs", "callers", "callees",
                     "comments", "bytes")

DEFAULT_MAX_DISASM = 200


def ep_function(ea, sections, max_disasm):
    """Rich context for the function (or raw area) containing *ea*.

    *sections* selects which parts to build; an empty selection means all of
    them. *max_disasm* caps the disassembly listing, which is the part that
    grows without bound on a large function; 0 lifts the cap.
    """
    unknown = [s for s in (sections or ()) if s not in FUNCTION_SECTIONS]
    if unknown:
        raise ValueError(
            f"unknown section(s) {unknown}. Valid sections: {list(FUNCTION_SECTIONS)}")

    want = set(sections) if sections else set(FUNCTION_SECTIONS)

    head = idc.get_item_head(ea)
    if head != idaapi.BADADDR:
        ea = head

    func = ida_funcs.get_func(ea)
    in_function = func is not None

    if func:
        start, end = func.start_ea, func.end_ea
        fname = ida_funcs.get_func_name(func.start_ea)
    else:
        start = ea
        end = ea + 0x100
        fname = None

    result = {
        "ea": hex_ea(ea),
        "in_function": in_function,
        "function": {
            "name": fname,
            "start_ea": hex_ea(start),
            "end_ea": hex_ea(end),
        } if in_function else None,
        "sections": sorted(want),
    }

    if "bytes" in want:
        result["bytes_at_ea"] = get_bytes_hex(ea, 64)

    if "disasm" in want:
        total = sum(1 for _ in idautils.Heads(start, end))
        limit = max_disasm if max_disasm else total
        result["disasm"] = disasm_at(start, limit, end)
        result["disasm_total"] = total
        result["disasm_truncated"] = len(result["disasm"]) < total
        if result["disasm_truncated"]:
            result["disasm_hint"] = (
                f"showing {len(result['disasm'])} of {total} instructions; raise "
                "max_disasm or use ida_get_disasm from a specific address")

    if "pseudocode" in want:
        pseudo = None
        if in_function and idasrv_config.HAS_HEXRAYS:
            try:
                cfunc = ida_hexrays.decompile(func)
                if cfunc:
                    pseudo = str(cfunc)
            except Exception:
                pass
        result["pseudocode"] = pseudo

    if "xrefs" in want:
        result["xrefs_from"] = xrefs_from(ea)
        result["xrefs_to"] = xrefs_to(ea)

    if "callers" in want:
        result["callers"] = callers_of(func) if func else []

    if "callees" in want:
        result["callees"] = callees_of(func) if func else []

    if "comments" in want:
        result["function_comment"] = idc.get_func_cmt(start, False) if func else None
        result["function_repeatable_comment"] = idc.get_func_cmt(start, True) if func else None
        instr_comments = []
        for item_ea in idautils.Heads(start, end):
            cmt = idc.get_cmt(item_ea, False)
            cmt_rep = idc.get_cmt(item_ea, True)
            if cmt or cmt_rep:
                instr_comments.append({
                    "ea": hex_ea(item_ea),
                    "comment": cmt or "",
                    "repeatable_comment": cmt_rep or "",
                })
        result["instr_comments"] = instr_comments

    return result


def ep_functions(limit, offset, name_filter, mode):
    matched = []
    for fea in idautils.Functions():
        fn = ida_funcs.get_func(fea)
        if fn is None:
            continue
        name = ida_funcs.get_func_name(fn.start_ea)
        if name_filter and name_filter.lower() not in name.lower():
            continue
        matched.append({
            "start_ea": hex_ea(fn.start_ea),
            "end_ea": hex_ea(fn.end_ea),
            "name": name,
            "size": fn.end_ea - fn.start_ea,
        })

    page, total = paginate(matched, offset, limit)

    if mode == "full":
        for entry in page:
            start = int(entry["start_ea"], 16)
            entry["xrefs_to_count"] = len(list(idautils.XrefsTo(start, 0)))
            type_str = get_type_str(start)
            if type_str:
                entry["type"] = type_str

    return {"count": len(page), "total_matched": total, "offset": offset, "functions": page}


def ep_xrefs(ea, direction):
    result = {"ea": hex_ea(ea)}
    if direction in ("from", "both"):
        result["xrefs_from"] = xrefs_from(ea)
    if direction in ("to", "both"):
        result["xrefs_to"] = xrefs_to(ea)
    return result


def ep_vtable(ea, count):
    """
    Read a vtable as an array of pointers at *ea*.
    Resolves each pointer to a function name if possible.
    Stops early if a pointer target is obviously invalid (0, or outside
    any segment).
    """
    ptr_size = pointer_size()
    min_ea = ida_ida.inf_get_min_ea()
    max_ea = ida_ida.inf_get_max_ea()
    entries = []
    for i in range(count):
        slot_ea = ea + i * ptr_size
        target = read_pointer(slot_ea)
        if target == 0 or target < min_ea or target > max_ea:
            break
        fname = get_name(target)
        func = ida_funcs.get_func(target)
        entries.append({
            "index": i,
            "slot_ea": hex_ea(slot_ea),
            "target": hex_ea(target),
            "name": fname,
            "is_function": func is not None,
        })
    return {
        "ea": hex_ea(ea),
        "pointer_size": ptr_size,
        "count": len(entries),
        "entries": entries,
    }


def ep_address(ea):
    """Detailed information about a single address."""
    seg = idaapi.getseg(ea)
    func = ida_funcs.get_func(ea)
    flags = ida_bytes.get_flags(ea)
    return {
        "ea": hex_ea(ea),
        "name": get_name(ea),
        "type": get_type_str(ea),
        "segment": idc.get_segm_name(ea) if seg else None,
        "is_code": ida_bytes.is_code(flags),
        "is_data": ida_bytes.is_data(flags),
        "is_head": ida_bytes.is_head(flags),
        "is_tail": ida_bytes.is_tail(flags),
        "in_function": func is not None,
        "function_name": ida_funcs.get_func_name(func.start_ea) if func else None,
        "function_start": hex_ea(func.start_ea) if func else None,
        "comment": idc.get_cmt(ea, False) or "",
        "repeatable_comment": idc.get_cmt(ea, True) or "",
        "item_size": idc.get_item_size(ea),
        "bytes_hex": get_bytes_hex(ea, idc.get_item_size(ea)),
    }
