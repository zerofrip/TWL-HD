#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Independent oracle for the 5C-1 texture capture planner.

This is a fresh implementation of the rules in docs/TEXTURE_CAPTURE_DESIGN.md
sections 3.1-3.5 and the 5C-1 task contract: key collection, the seven format
size formulas, the format-5 minimal palette window, the budget/whole-record
drop policy and the TXDS offset layout. It imports nothing from tools/, so a
shared mistake in tools/tex_decode_oracle.py cannot hide here.

Used by tools/test_tex_capture_golden.py; the C side is
tests/host/tex_capture_cli.c.
"""

TEXDT_MAX = 524288
PLDT_MAX = 98304
RECORD_MAX = 256
FULL_WINDOW = 65540

CMD_TEXIMAGE_PARAM = 0x2A
CMD_PLTT_BASE = 0x2B
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_VTX_FIRST = 0x23
CMD_VTX_LAST = 0x28
RAW_FIFO = 0x10000

# Bytes per texel, in eighths: {0, 4, 1, 2, 4, 1, 4, 8} / 4.
TEXEL_EIGHTHS = {1: 8, 2: 2, 3: 4, 4: 8, 5: 2, 6: 8, 7: 16}
# Fixed palette table size in bytes (paletteSizeList * 2).
PALETTE_BYTES = {1: 64, 2: 8, 3: 32, 4: 512, 5: 0, 6: 16, 7: 0}


class ReadError(Exception):
    pass


class FakeMemory:
    """Flat fake source memory with the CLI's per-kind failure injection."""

    def __init__(self, data, fail_mask=0):
        self.data = bytes(data)
        self.fail = fail_mask

    def read(self, kind, offset, size, probe):
        if kind == 0 and (self.fail & 1):
            raise ReadError("texel")
        if kind == 1 and (self.fail & 2):
            raise ReadError("palette")
        if kind == 2:
            if self.fail & 4:
                raise ReadError("index")
            if not probe and size == 2 and (self.fail & 8):
                raise ReadError("index-small")
        if offset + size > len(self.data):
            raise ReadError("range")
        return self.data[offset:offset + size]


def param_of(fmt, shift_s=0, shift_t=0, offset=0):
    return ((fmt & 7) << 26) | ((shift_s & 7) << 20) | \
        ((shift_t & 7) << 23) | (offset & 0xFFFF)


def format_of(param):
    return (param >> 26) & 7


def texel_address(param):
    return (param & 0xFFFF) << 3


def palette_address(param, pltt):
    return (pltt << 3) if format_of(param) == 2 else (pltt << 4)


def index_address(param):
    base = 0x30000 if (param & 0xC000) == 0x8000 else 0x20000
    return base + ((param & 0x3FFF) << 2)


def sizes(param):
    """(texel_bytes, fixed_palette_bytes, index_bytes) from the format rules."""
    fmt = format_of(param)
    if fmt == 0:
        raise ValueError("format 0")
    width = 8 << ((param >> 20) & 7)
    height = 8 << ((param >> 23) & 7)
    texels = width * height
    texel_bytes = texels * TEXEL_EIGHTHS[fmt] // 8
    palette_bytes = PALETTE_BYTES[fmt]
    index_bytes = texels // 8 if fmt == 5 else 0
    return texel_bytes, palette_bytes, index_bytes


def fmt5_window(mem, param, index_bytes):
    """4*max(entry & 0x3FFF) + 8, or the full window if the scan fails."""
    offset = index_address(param)
    max_entry = 0
    try:
        for block in range(index_bytes // 2):
            word = mem.read(2, offset + block * 2, 2, probe=False)
            entry = (word[0] | (word[1] << 8)) & 0x3FFF
            if entry > max_entry:
                max_entry = entry
    except ReadError:
        return FULL_WINDOW
    return max_entry * 4 + 8


def align4(value):
    return (value + 3) & ~3


def plan(max_txdt, max_pldt, max_records, gxcm, mem):
    """Return the plan dict plus the applied TXDT/PLDT bytes."""
    max_txdt = min(max_txdt, TEXDT_MAX)
    max_pldt = min(max_pldt, PLDT_MAX)
    max_records = min(max_records, RECORD_MAX)

    stats = {
        "keys_seen": 0,
        "keys_kept": 0,
        "keys_dropped_budget": 0,
        "keys_dropped_read_error": 0,
        "keys_dropped_unsupported": 0,
        "conflicts_same_key": 0,
        "bytes_txdt": 0,
        "bytes_pldt": 0,
        "overflow": 0,
    }
    keys = []  # dicts in first-use order, with private drop/last_write
    write_seq = 0
    param = 0
    pltt = 0
    in_begin = False

    for cmd_word, value in gxcm:
        if cmd_word == RAW_FIFO:
            continue
        cmd = cmd_word & 0xFF
        if cmd == CMD_TEXIMAGE_PARAM:
            param = value
            write_seq += 1
        elif cmd == CMD_PLTT_BASE:
            pltt = value
            write_seq += 1
        elif cmd == CMD_BEGIN_VTXS:
            in_begin = True
        elif cmd == CMD_END_VTXS:
            in_begin = False
        elif CMD_VTX_FIRST <= cmd <= CMD_VTX_LAST:
            if not in_begin or format_of(param) == 0:
                continue
            found = None
            for key in keys:
                if key["param"] == param and key["pltt"] == pltt:
                    found = key
                    break
            if found is not None:
                if found["last_write"] != write_seq:
                    stats["conflicts_same_key"] += 1
                    found["last_write"] = write_seq
                continue

            stats["keys_seen"] += 1
            if len(keys) >= RECORD_MAX:
                stats["keys_dropped_budget"] += 1
                stats["overflow"] = 1
                continue

            texel_bytes, palette_bytes, index_bytes = sizes(param)
            key = {
                "param": param,
                "pltt": pltt,
                "texel": texel_bytes,
                "palette": palette_bytes,
                "index": index_bytes,
                "drop": None,
                "last_write": write_seq,
            }
            if texel_bytes + index_bytes > TEXDT_MAX or \
                    palette_bytes > PLDT_MAX:
                key["drop"] = "unsupported"
                stats["keys_dropped_unsupported"] += 1
            else:
                try:
                    mem.read(0, texel_address(param), texel_bytes, probe=True)
                except ReadError:
                    key["drop"] = "read"
                if key["drop"] is None and format_of(param) == 5:
                    try:
                        mem.read(2, index_address(param), index_bytes,
                                 probe=True)
                    except ReadError:
                        key["drop"] = "read"
                    else:
                        key["palette"] = fmt5_window(mem, param, index_bytes)
                if key["drop"] is None and key["palette"] != 0:
                    try:
                        mem.read(1, palette_address(param, pltt),
                                 key["palette"], probe=True)
                    except ReadError:
                        key["drop"] = "read"
                if key["drop"] == "read":
                    stats["keys_dropped_read_error"] += 1
            keys.append(key)

    kept = []
    txdt_used = 0
    pldt_used = 0
    for key in keys:
        if key["drop"] is not None:
            continue
        need = key["texel"] + key["index"]
        if len(kept) >= max_records or \
                txdt_used + need > max_txdt or \
                align4(pldt_used + key["palette"]) > max_pldt:
            stats["keys_dropped_budget"] += 1
            stats["overflow"] = 1
            continue
        txdt_used += need
        pldt_used += key["palette"]
        kept.append(key)

    stats["keys_kept"] = len(kept)
    stats["bytes_txdt"] = txdt_used
    stats["bytes_pldt"] = pldt_used

    txdt = bytearray()
    pldt = bytearray()
    records = []
    for key in kept:
        texel_offset = len(txdt)
        txdt += mem.read(0, texel_address(key["param"]), key["texel"],
                         probe=False)
        index_offset = len(txdt) if key["index"] else 0
        if key["index"]:
            txdt += mem.read(2, index_address(key["param"]), key["index"],
                             probe=False)
        palette_offset = len(pldt) if key["palette"] else 0
        if key["palette"]:
            pldt += mem.read(1, palette_address(key["param"], key["pltt"]),
                             key["palette"], probe=False)
        records.append({
            "param": key["param"],
            "pltt": key["pltt"],
            "texel": key["texel"],
            "palette": key["palette"],
            "index": key["index"],
            "texel_offset": texel_offset,
            "palette_offset": palette_offset,
            "index_offset": index_offset,
        })

    return {
        "limits": (max_txdt, max_pldt, max_records),
        "stats": stats,
        "records": records,
        "txdt": bytes(txdt),
        "pldt": bytes(pldt),
    }


def render(result):
    """Exact CLI text for one plan result."""
    txdt, pldt, records = result["txdt"], result["pldt"], result["records"]
    stats = result["stats"]
    limit_txdt, limit_pldt, limit_records = result["limits"]
    lines = [
        "result=OK",
        f"limits txdt={limit_txdt} pldt={limit_pldt} records={limit_records}",
        f"keys_seen={stats['keys_seen']}",
        f"keys_kept={stats['keys_kept']}",
        f"keys_dropped_budget={stats['keys_dropped_budget']}",
        f"keys_dropped_read_error={stats['keys_dropped_read_error']}",
        f"keys_dropped_unsupported={stats['keys_dropped_unsupported']}",
        f"conflicts_same_key={stats['conflicts_same_key']}",
        f"bytes_txdt={stats['bytes_txdt']}",
        f"bytes_pldt={stats['bytes_pldt']}",
        f"overflow={stats['overflow']}",
        f"record_count={len(records)}",
    ]
    for index, rec in enumerate(records):
        lines.append(
            f"record {index} param={rec['param']} pltt={rec['pltt']} "
            f"texel_bytes={rec['texel']} palette_bytes={rec['palette']} "
            f"index_bytes={rec['index']} "
            f"texel_offset={rec['texel_offset']} "
            f"palette_offset={rec['palette_offset']} "
            f"index_offset={rec['index_offset']}")
    lines.append(f"txdt {len(txdt)}")
    lines.append(txdt.hex())
    lines.append(f"pldt {len(pldt)}")
    lines.append(pldt.hex())
    return "\n".join(lines) + "\n"
