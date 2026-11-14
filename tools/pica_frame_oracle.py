#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Independent oracle for the 5B-2e-1 textured frame preparation.

The run grouping, TXDS key lookup, dedupe, arena offsets, wrap modes, entry
status and texcoord maths are implemented here from the v2 format rules and
from docs/DS_GX_RESEARCH.md ("Textured frame preparation (5B-2e-1)"). Only the
DS texture decode and the PICA swizzle are reused from the existing oracles
(tex_decode_oracle.decode / pica_tex_oracle.swizzle), exactly as the C module
reuses twlhd_tex_decode / twlhd_pica_tex_swizzle_rgba8.

The PICA swizzle stores memory tile rows top-down (image row 0 = memory tile
row 0); the v coordinate is kept as v = 1 - t/height so that DS t=0 (top row)
samples memory row 0.

Inputs are the *parsed* view: records with canonical offsets, the TXDT and
PLDT byte blobs, and the interpreter's per-vertex (s, t, teximage_param,
pltt_base) list. Output is the same text the C CLI prints after "# result", so
the golden test can compare it byte for byte.
"""

import struct

import pica_tex_oracle
import tex_decode_oracle


def _f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _f32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _dims(param):
    return (8 << ((param >> 20) & 7), 8 << ((param >> 23) & 7))


def _wrap(param):
    repeat_s = (param >> 16) & 1
    repeat_t = (param >> 17) & 1
    flip_s = (param >> 18) & 1
    flip_t = (param >> 19) & 1

    def axis(repeat, flip):
        if repeat and flip:
            return 3
        if repeat:
            return 2
        return 0

    return axis(repeat_s, flip_s), axis(repeat_t, flip_t)


def _slice(buf, offset, size):
    """Mimic twlhd_v2_tex_*_ptr: NULL when the size is 0 or out of bounds."""
    if size == 0:
        return None
    if offset + size > len(buf):
        return None
    return buf[offset:offset + size]


def _find_record(records, param, pltt):
    for rec in records:
        if rec["param"] == param and rec["pltt"] == pltt:
            return rec
    return None


def _decode_run(rec, param, txdt, pldt):
    """Return (status, width, height, rgba_bytes) for one textured run."""
    fmt = (param >> 26) & 7
    texel = _slice(txdt, rec["texel_off"], rec["texel_size"])
    palette = _slice(pldt, rec["palette_off"], rec["palette_size"])
    index = _slice(txdt, rec["index_off"], rec["index_size"])

    if texel is None:
        return "NO_DATA", 0, 0, None
    if fmt != 7 and palette is None:
        return "NO_DATA", 0, 0, None
    if fmt == 5 and index is None:
        return "NO_DATA", 0, 0, None

    width, height = _dims(param)
    try:
        pixels = tex_decode_oracle.decode(
            param, texel, palette, index,
            texel_size=rec["texel_size"],
            palette_size=rec["palette_size"],
            index_size=rec["index_size"],
        )
    except tex_decode_oracle.TexDecodeError:
        return "UNSUPPORTED", 0, 0, None

    rgba = bytearray()
    for pixel in pixels:
        rgba.extend(bytes(pixel))
    return "OK", width, height, bytes(rgba)


def prepare(vertices, records, txdt, pldt):
    """Run the preparation and return the result structure."""
    runs = []
    first = 0
    while first < len(vertices):
        param = vertices[first][2]
        pltt = vertices[first][3]
        nxt = first + 3
        while (nxt < len(vertices) and vertices[nxt][2] == param
               and vertices[nxt][3] == pltt):
            nxt += 3
        runs.append((first, nxt - first, param, pltt))
        first = nxt

    textures = []
    key_to_index = {}
    arena = bytearray()
    entries = []
    texcoords = [None] * len(vertices)

    for index, (start, count, param, pltt) in enumerate(runs):
        fmt = (param >> 26) & 7
        untextured = fmt == 0
        status = "OK"
        tex_index = -1
        width = 0
        height = 0

        if not untextured:
            rec = _find_record(records, param, pltt)
            if rec is None:
                status = "NO_DATA"
            else:
                status, width, height, rgba = _decode_run(
                    rec, param, txdt, pldt)
                if status == "OK":
                    key = (param, pltt)
                    if key not in key_to_index:
                        offset = len(arena)
                        tex_index = len(textures)
                        wrap_s, wrap_t = _wrap(param)
                        textures.append({
                            "offset": offset,
                            "width": width,
                            "height": height,
                            "wrap_s": wrap_s,
                            "wrap_t": wrap_t,
                        })
                        key_to_index[key] = tex_index
                        arena.extend(
                            pica_tex_oracle.swizzle(
                                width, height, bytearray(rgba)))
                    else:
                        tex_index = key_to_index[key]

        entries.append({
            "first": start,
            "count": count,
            "param": param,
            "pltt": pltt,
            "untextured": 1 if untextured else 0,
            "status": status,
            "tex": tex_index,
        })

        for v in range(start, start + count):
            if status == "OK" and not untextured and tex_index >= 0:
                s, t = vertices[v][0], vertices[v][1]
                u = _f32(s / _f32(width))
                vv = _f32(1.0 - _f32(t / _f32(height)))
            else:
                u = 0.0
                vv = 0.0
            texcoords[v] = (_f32_bits(u), _f32_bits(vv))

    return {
        "result": "OK",
        "entry_count": len(entries),
        "texture_count": len(textures),
        "arena_used": len(arena),
        "entries": entries,
        "textures": textures,
        "texcoords": texcoords,
        "arena": bytes(arena),
    }


def render_result(vertices, records, txdt, pldt):
    """Return the exact text the C CLI prints after "# result"."""
    out = prepare(vertices, records, txdt, pldt)
    lines = [
        "result=%s" % out["result"],
        "entry_count=%d" % out["entry_count"],
        "texture_count=%d" % out["texture_count"],
        "arena_used=%d" % out["arena_used"],
    ]
    for i, entry in enumerate(out["entries"]):
        lines.append(
            "entry %d first=%d count=%d param=%d pltt=%d untextured=%d "
            "status=%s tex=%d"
            % (i, entry["first"], entry["count"], entry["param"],
               entry["pltt"], entry["untextured"], entry["status"],
               entry["tex"]))
    for i, texture in enumerate(out["textures"]):
        lines.append(
            "texture %d offset=%d width=%d height=%d wrap_s=%d wrap_t=%d"
            % (i, texture["offset"], texture["width"], texture["height"],
               texture["wrap_s"], texture["wrap_t"]))
    for i, (u_bits, v_bits) in enumerate(out["texcoords"]):
        lines.append("texcoord %d u=%08x v=%08x" % (i, u_bits, v_bits))
    lines.append("arena %d" % out["arena_used"])
    lines.append(out["arena"].hex())
    return "\n".join(lines) + "\n"
