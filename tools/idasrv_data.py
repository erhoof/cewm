"""Data-oriented endpoints: names, strings, segments, imports, exports, structs, enums."""

import idaapi
import idc
import ida_bytes
import ida_nalt
import ida_typeinf
import idautils

from idasrv_util import hex_ea, paginate

_STRTYPE_NAMES = {
    0: "C", 1: "C_16", 2: "C_32",
    4: "PASCAL", 5: "PASCAL_16", 6: "PASCAL_32",
    8: "LEN2", 9: "LEN2_16", 10: "LEN2_32",
    12: "LEN4", 13: "LEN4_16", 14: "LEN4_32",
}


def _strtype_name(strtype):
    return _STRTYPE_NAMES.get(ida_nalt.get_str_type_code(strtype), "UNKNOWN")


def ep_names(limit, offset, name_filter):
    matched = []
    for ea, name in idautils.Names():
        if name_filter and name_filter.lower() not in name.lower():
            continue
        matched.append({"ea": hex_ea(ea), "name": name})
    page, total = paginate(matched, offset, limit)
    return {"count": len(page), "total_matched": total, "offset": offset, "names": page}


def _decode_string(ea, length, strtype):
    raw = ida_bytes.get_strlit_contents(ea, length, strtype)
    return raw.decode("utf-8", errors="replace") if raw else ""


def ep_strings(limit, offset, min_length, content_filter):
    strings = idautils.Strings()
    strings.setup(
        strtypes=[ida_nalt.STRTYPE_C, ida_nalt.STRTYPE_C_16],
        minlen=min_length,
        only_7bit=False,
        display_only_existing_strings=True,
    )
    items = [(s.ea, s.length, s.strtype) for s in strings]

    if content_filter:
        needle = content_filter.lower()
        matched = [(ea, ln, st, text)
                   for ea, ln, st in items
                   for text in (_decode_string(ea, ln, st),)
                   if needle in text.lower()]
    else:
        matched = [(ea, ln, st, None) for ea, ln, st in items]

    page, total = paginate(matched, offset, limit)
    out = [{
        "ea": hex_ea(ea),
        "length": ln,
        "type": st,
        "type_name": _strtype_name(st),
        "value": text if text is not None else _decode_string(ea, ln, st),
    } for ea, ln, st, text in page]
    return {"count": len(out), "total_matched": total, "offset": offset, "strings": out}


def ep_segments():
    segs = []
    for seg_ea in idautils.Segments():
        seg = idaapi.getseg(seg_ea)
        if seg is None:
            continue
        segs.append({
            "start_ea": hex_ea(seg.start_ea),
            "end_ea": hex_ea(seg.end_ea),
            "name": idc.get_segm_name(seg.start_ea),
            "class": idaapi.get_segm_class(seg) or "",
            "size": seg.end_ea - seg.start_ea,
            "perm": seg.perm,
            "bitness": seg.bitness,
        })
    return {"count": len(segs), "segments": segs}


def ep_imports():
    modules = {}
    nimps = idaapi.get_import_module_qty()
    for i in range(nimps):
        mod_name = idaapi.get_import_module_name(i)
        if not mod_name:
            continue
        entries = []

        def _cb(ea, name, ordinal):
            entries.append({
                "ea": hex_ea(ea),
                "name": name or "",
                "ordinal": ordinal,
            })
            return True

        idaapi.enum_import_names(i, _cb)
        modules[mod_name] = entries
    return {"count": nimps, "modules": modules}


def ep_exports():
    exports = []
    for idx, ordinal, ea, name in idautils.Entries():
        exports.append({
            "index": idx,
            "ordinal": ordinal,
            "ea": hex_ea(ea),
            "name": name or "",
        })
    return {"count": len(exports), "exports": exports}


def ep_structs(name_filter):
    """List structure/union definitions in the IDB (IDA 9.0+, uses ida_typeinf)."""
    results = []
    for ordinal, sid, sname in idautils.Structs():
        if name_filter and name_filter.lower() not in sname.lower():
            continue
        tif = ida_typeinf.tinfo_t()
        tif.get_type_by_tid(sid)
        results.append({
            "ordinal": ordinal,
            "id": sid,
            "name": sname,
            "size": tif.get_size() if tif.present() else 0,
            "is_union": tif.is_union(),
        })
    return {"count": len(results), "structs": results}


def ep_struct(name):
    """Get full details of a struct by name (IDA 9.0+, uses ida_typeinf)."""
    til = ida_typeinf.get_idati()
    tif = ida_typeinf.tinfo_t()
    if not tif.get_named_type(til, name, ida_typeinf.BTF_STRUCT, True, False):
        if not tif.get_named_type(til, name, ida_typeinf.BTF_UNION, True, False):
            raise ValueError(f"Struct/union '{name}' not found")

    udt = ida_typeinf.udt_type_data_t()
    if not tif.get_udt_details(udt):
        raise ValueError(f"Could not read UDT details for '{name}'")

    members = []
    for udm in udt:
        if udm.is_gap():
            continue
        type_str = str(udm.type) if udm.type.present() else None
        cmt = udm.cmt or ""
        members.append({
            "offset": udm.offset // 8,
            "name": udm.name or "",
            "size": udm.size // 8,
            "type": type_str,
            "comment": cmt,
        })
    return {
        "name": name,
        "id": tif.get_tid(),
        "size": tif.get_size(),
        "is_union": tif.is_union(),
        "members": members,
    }


def ep_enums(name_filter):
    """List enum definitions (IDA 9.0+, uses ida_typeinf)."""
    results = []
    limit = ida_typeinf.get_ordinal_limit()
    for ordinal in range(1, limit):
        tif = ida_typeinf.tinfo_t()
        tif.get_numbered_type(None, ordinal)
        if not tif.is_enum():
            continue
        ename = tif.get_type_name()
        if not ename:
            continue
        if name_filter and name_filter.lower() not in ename.lower():
            continue
        edt = ida_typeinf.enum_type_data_t()
        member_count = 0
        is_bf = False
        if tif.get_enum_details(edt):
            member_count = edt.size()
            is_bf = edt.is_bf()
        results.append({
            "id": tif.get_tid(),
            "name": ename,
            "is_bitfield": is_bf,
            "member_count": member_count,
        })
    return {"count": len(results), "enums": results}


def ep_enum(name):
    """Get full details of an enum by name (IDA 9.0+, uses ida_typeinf)."""
    til = ida_typeinf.get_idati()
    tif = ida_typeinf.tinfo_t()
    if not tif.get_named_type(til, name, ida_typeinf.BTF_ENUM, True, False):
        raise ValueError(f"Enum '{name}' not found")

    edt = ida_typeinf.enum_type_data_t()
    if not tif.get_enum_details(edt):
        raise ValueError(f"Could not read enum details for '{name}'")

    members = []
    for edm in edt:
        val = edm.value
        members.append({
            "name": edm.name or f"unk_{val:X}",
            "value": val,
            "value_hex": hex_ea(val),
        })
    return {
        "name": name,
        "id": tif.get_tid(),
        "is_bitfield": edt.is_bf(),
        "members": members,
    }
