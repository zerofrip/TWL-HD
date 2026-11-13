#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a deterministic textured v2 frame for 5B-2e-2 emulator validation.

Two frames are written into the output directory:

  twlhd_frame_v2_tex.bin     four quads exercising:
                               - 8x8 two-colour checker (red/white)
                               - 16x16 two-colour checker (blue/white)
                               - 16x8 non-square two-colour checker (green/white)
                               - 8x8 checker reused from the first run
  twlhd_frame_v2_tex_ctl.bin the same four quads with no texture state
                               (untextured clip-path control)

BUILD/UNIT/EMULATOR tooling only. Stdlib only. Nothing here contacts a console
or a network address. The frame layout follows the scene4_alpha 2x2 quad grid
(tools/hw/gen_lit_frames.py:53-61), scaled to use most of the DS screen.

Texture format: DS 16bpp (format 7, TEXIMAGE_PARAM bits 26-28 = 7). Each texel
is a little-endian RGB555 word; bit 15 = 1 means opaque, 0 means fully
transparent. No palette is required, so the generated frame has TXDS and TXDT
chunks but no PLDT chunk.
"""

import argparse
import os
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

# v2 container helpers from the parser agreement suite.
from test_twlhd_v2 import build, entries  # noqa: E402

# v2 command ids (common/twlhd_v2_format.h:94-130).
CMD_COLOR = 0x20
CMD_TEX_COORD = 0x22
CMD_VTX_16 = 0x23
CMD_TEXIMAGE_PARAM = 0x2A
CMD_PLTT_BASE = 0x2B
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41

FRAME_NAMES = (
    "twlhd_frame_v2_tex.bin",
    "twlhd_frame_v2_tex_ctl.bin",
)

# 2x2 quad grid scaled to fill most of the DS clip box. Values are raw clip
# * 4096 (same convention as tools/hw/gen_lit_frames.py).
QUADS = (
    ((-3840, -2560), (-640, -2560), (-640, -320), (-3840, -320)),
    ((640, -2560), (3840, -2560), (3840, -320), (640, -320)),
    ((-3840, 320), (-640, 320), (-640, 2560), (-3840, 2560)),
    ((640, 320), (3840, 320), (3840, 2560), (640, 2560)),
)

# Texture descriptors used by the four runs. The first and last run reuse
# PARAM_RED_8 so the 8x8 red/white texture is referenced twice.
# Format 7 (16bpp) is at bits 26-28; size shifts are at bits 20-22 (S) and
# 23-25 (T).
PARAM_RED_8 = (7 << 26) | (0 << 20) | (0 << 23)  # 8x8, format 7
PARAM_BLUE_16 = (7 << 26) | (1 << 20) | (1 << 23)  # 16x16, format 7
PARAM_GREEN_16_8 = (7 << 26) | (1 << 20) | (0 << 23)  # 16x8, format 7

TEXTURE_SPECS = (
    (PARAM_RED_8, 0),      # run 0
    (PARAM_BLUE_16, 1),    # run 1
    (PARAM_GREEN_16_8, 2), # run 2
    (PARAM_RED_8, 0),      # run 3 (reuse)
)

# Colours as RGB555 opaque (bit 15 set).
RGB555_RED = (31 << 0) | (0 << 5) | (0 << 10) | 0x8000
RGB555_GREEN = (0 << 0) | (31 << 5) | (0 << 10) | 0x8000
RGB555_BLUE = (0 << 0) | (0 << 5) | (31 << 10) | 0x8000
RGB555_WHITE = (31 << 0) | (31 << 5) | (31 << 10) | 0x8000
RGB555_GREY = (16 << 0) | (16 << 5) | (16 << 10) | 0x8000


def _rgb555(r, g, b):
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10) | 0x8000


def _vtx16(x, y):
    """Two GXCM entries for one VTX_16 command (z = 0)."""
    return [(CMD_VTX_16, ((y & 0xFFFF) << 16) | (x & 0xFFFF)),
            (CMD_VTX_16, 0)]


def _tex_coord(s, t):
    """One GXCM entry for TEX_COORD; s/t are 16-bit fixed 1.11.4 values."""
    return (CMD_TEX_COORD, ((t & 0xFFFF) << 16) | (s & 0xFFFF))


def _checkerboard(width, height, color_a, color_b):
    """Return DS 16bpp texel bytes for a checker texture."""
    out = bytearray()
    for y in range(height):
        for x in range(width):
            if ((x // 4) + (y // 4)) % 2 == 0:
                out += struct.pack("<H", color_a)
            else:
                out += struct.pack("<H", color_b)
    return bytes(out)


def _build_texture_table():
    """Return (txds_records, txdt_payload) for the unique textures.

    The reuse of the 8x8 checker is expressed in the GXCM stream, not by a
    duplicate TXDS record; the parser rejects duplicate
    (teximage_param, pltt_base) keys.
    """
    # Each record: teximage_param, pltt_base, texel_offset, texel_size,
    # palette_offset, palette_size, index_offset, index_size, reserved0, reserved1.
    txdt = bytearray()
    records = []
    seen = set()

    specs = (
        (PARAM_RED_8, 0, _checkerboard(8, 8, RGB555_RED, RGB555_WHITE)),
        (PARAM_BLUE_16, 0, _checkerboard(16, 16, RGB555_BLUE, RGB555_WHITE)),
        (PARAM_GREEN_16_8, 0,
         _checkerboard(16, 8, RGB555_GREEN, RGB555_WHITE)),
    )

    for teximage_param, pltt_base, texel_bytes in specs:
        key = (teximage_param, pltt_base)
        if key in seen:
            continue
        seen.add(key)
        offset = len(txdt)
        records.append({
            "teximage_param": teximage_param,
            "pltt_base": pltt_base,
            "texel_offset": offset,
            "texel_size": len(texel_bytes),
            "palette_offset": 0,
            "palette_size": 0,
            "index_offset": 0,
            "index_size": 0,
        })
        txdt += texel_bytes

    return records, bytes(txdt)


def _txds_payload(records):
    out = bytearray()
    for rec in records:
        out += struct.pack("<IIIIIIIIII",
                           rec["teximage_param"], rec["pltt_base"],
                           rec["texel_offset"], rec["texel_size"],
                           rec["palette_offset"], rec["palette_size"],
                           rec["index_offset"], rec["index_size"],
                           0, 0)
    return bytes(out)


def _quad_triangles(corners):
    """Two triangles covering the quad, counter-clockwise."""
    return [corners[0], corners[1], corners[2],
            corners[0], corners[2], corners[3]]


def _tex_run(out, teximage_param, pltt_base, width, height,
             triangle_corners, texcoords, white=False):
    """Append commands for one textured run (two triangles).

    triangle_corners and texcoords are parallel lists of six entries each
    (two triangles). Texcoords are normalised (u,v) in [0,1].
    """
    out.append((CMD_TEXIMAGE_PARAM, teximage_param))
    out.append((CMD_PLTT_BASE, pltt_base))
    if white:
        out.append((CMD_COLOR, RGB555_WHITE))
    out.append((CMD_BEGIN_VTXS, 0))  # triangle list
    for corner, (u, v) in zip(triangle_corners, texcoords):
        # Convert normalised texcoord to DS 1.11.4 fixed point.
        s = int(u * width * 16.0) & 0xFFFF
        t = int(v * height * 16.0) & 0xFFFF
        out.append(_tex_coord(s, t))
        out.extend(_vtx16(corner[0], corner[1]))
    out.append((CMD_END_VTXS, 0))


def _untextured_run(out, color_rgb555, triangle_corners):
    """Append commands for one untextured run (two triangles)."""
    out.append((CMD_COLOR, color_rgb555))
    out.append((CMD_BEGIN_VTXS, 0))
    for corner in triangle_corners:
        out.extend(_vtx16(corner[0], corner[1]))
    out.append((CMD_END_VTXS, 0))


CMD_SWAP_BUFFERS = 0x50


def _build_with_textures():
    out = []
    records, txdt = _build_texture_table()
    # Standard [0,1]^2 texcoords for the two triangles of each quad.
    quad_texcoords = [
        (0.0, 0.0), (1.0, 0.0), (1.0, 1.0),
        (0.0, 0.0), (1.0, 1.0), (0.0, 1.0),
    ]
    for run_index, (param, tex_index) in enumerate(TEXTURE_SPECS):
        rec = records[tex_index]
        width = 8 << ((param >> 20) & 7)
        height = 8 << ((param >> 23) & 7)
        _tex_run(out, param, rec["pltt_base"], width, height,
                 _quad_triangles(QUADS[run_index]), quad_texcoords,
                 white=True)
    out.append((CMD_SWAP_BUFFERS, 0))

    gxcm = entries(*out)
    txds = _txds_payload(records)
    return gxcm, txds, txdt


def _build_control():
    out = []
    colors = (RGB555_RED, RGB555_BLUE, RGB555_GREEN, RGB555_GREY)
    for color, corners in zip(colors, QUADS):
        _untextured_run(out, color, _quad_triangles(corners))
    out.append((CMD_SWAP_BUFFERS, 0))
    return entries(*out), None, None


def _chunk(ctype, payload, flags=0, reserved=0):
    size = len(payload)
    return struct.pack("<IIII", ctype, size, flags, reserved) + payload


def _fourcc(text):
    raw = text.encode("ascii")
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24)


GXCM = _fourcc("GXCM")
META = _fourcc("META")
TXDS = _fourcc("TXDS")
TXDT = _fourcc("TXDT")


# Chunk flag constants from the v2 format.
CHUNK_FLAG_REQUIRED = 0x00000001


def _build_frame(gxcm, txds, txdt):
    meta_payload = struct.pack("<IIII", 2, 0x7, 0, 0)
    body = _chunk(GXCM, gxcm, flags=CHUNK_FLAG_REQUIRED)
    body += _chunk(META, meta_payload)
    chunk_count = 2
    if txds is not None:
        body += _chunk(TXDS, txds)
        body += _chunk(TXDT, txdt)
        chunk_count += 2

    total = 32 + len(body)
    header = struct.pack("<IIIIIIII", 2, 32, total, 1, 1, chunk_count,
                         0, 0)
    return header + body


def frame_bytes(name):
    if name == FRAME_NAMES[0]:
        gxcm, txds, txdt = _build_with_textures()
        return _build_frame(gxcm, txds, txdt)
    if name == FRAME_NAMES[1]:
        gxcm, _txds, _txdt = _build_control()
        return _build_frame(gxcm, None, None)
    raise ValueError(f"unknown frame name: {name}")


def write_frames(outdir):
    os.makedirs(outdir, exist_ok=True)
    paths = []
    for name in FRAME_NAMES:
        path = os.path.join(outdir, name)
        with open(path, "wb") as handle:
            handle.write(frame_bytes(name))
        paths.append(path)
    return paths


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_tex_frames.py",
        description="Write deterministic 5B-2e-2 textured v2 frames.")
    parser.add_argument("outdir", help="output directory")
    parser.add_argument("--name", choices=FRAME_NAMES,
                        help="write only one frame")
    args = parser.parse_args(argv)

    if args.name:
        path = os.path.join(args.outdir, args.name)
        os.makedirs(args.outdir, exist_ok=True)
        with open(path, "wb") as handle:
            handle.write(frame_bytes(args.name))
        print(path)
    else:
        for path in write_frames(args.outdir):
            print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
