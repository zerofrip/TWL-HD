#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the texture BEHAVIOUR frame for 5B-2e-2 validation.

Writes ONE deterministic v2 frame, ``twlhd_frame_v2_behaviour.bin``, with eight
non-overlapping quads on a 4x2 grid of the 400x240 top screen (white vertex
colours, alpha 1.0, polygon alpha 31, so MODULATE shows the texels unchanged):
the three texture behaviours not yet shown on screen:

    quad 0  A3I5, alpha 7/5/3/0 in column pairs, rows identical
    quad 1  A5I3, alpha 31/20/10/0 in column pairs, rows identical
    quad 2  I4, TEXIMAGE_PARAM bit 29 (KeyColor0) set -> index 0 transparent
    quad 3  the SAME I4 texture/palette, bit 29 clear -> index 0 visible
    quad 4  REPEAT in S and T (bits 16,17), texcoords 0..2 both axes
    quad 5  CLAMP (bits 16-19 clear), texcoords -0.5..1.5
    quad 6  REPEAT+FLIP = MIRRORED_REPEAT (bits 16-19), texcoords 0..2
    quad 7  FLIP without REPEAT (bits 18,19) = clamp per DS, texcoords 0..2

Q4-Q7 share ONE asymmetric opaque 8x8 16bpp texture: R = x*4, G = y*4, a
unique marker B=31 at (0,0) and B=16 at (7,7), so a wrong wrap/flip is visible.

The expected colours are computed independently by tools/check_tex_behaviour.py
from tools/tex_decode_oracle.py plus its own wrap function. The renderer facts
(blend ADD/SRC_ALPHA/ONE_MINUS_SRC_ALPHA, TexEnv MODULATE) are recorded in that
checker and in docs/EMULATOR_VALIDATION.md.

BUILD/UNIT/EMULATOR tooling only. Stdlib only. Nothing here contacts a console
or a network address.
"""

import argparse
import os
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from test_twlhd_v2 import entries  # noqa: E402

# The multi-format generator already pins the v2 container builder, the clip
# mapping and the palette helpers; reuse them instead of copying.
import gen_tex_format_frames as base  # noqa: E402

FRAME_NAME = "twlhd_frame_v2_behaviour.bin"

CMD_COLOR = base.CMD_COLOR
CMD_TEX_COORD = base.CMD_TEX_COORD
CMD_VTX_16 = base.CMD_VTX_16
CMD_TEXIMAGE_PARAM = base.CMD_TEXIMAGE_PARAM
CMD_PLTT_BASE = base.CMD_PLTT_BASE
CMD_BEGIN_VTXS = base.CMD_BEGIN_VTXS
CMD_END_VTXS = base.CMD_END_VTXS
CMD_SWAP_BUFFERS = base.CMD_SWAP_BUFFERS

WHITE_555 = base.WHITE_555
QUAD_TEXCOORDS = base.QUAD_TEXCOORDS

GXCM = base.GXCM
META = base.META
TXDS = base.TXDS
TXDT = base.TXDT
PLDT = base.PLDT
CHUNK_FLAG_REQUIRED = base.CHUNK_FLAG_REQUIRED

KEY_COLOR0 = 1 << 29
WRAP_REPEAT_S = 1 << 16
WRAP_REPEAT_T = 1 << 17
WRAP_FLIP_S = 1 << 18
WRAP_FLIP_T = 1 << 19

# A3I5 / A5I3 alpha levels per column pair (columns 0-1, 2-3, 4-5, 6-7).
A3I5_ALPHAS = (7, 5, 3, 0)
A5I3_ALPHAS = (31, 20, 10, 0)

# Palette for Q0 (8 colours) and Q1 (same first 8 entries).
TEX_PALETTE = base.PALETTE_8

# I4 palette for Q2/Q3: entry 0 is the bright colour that KeyColor0 turns
# transparent on Q2 and leaves visible on Q3.
I4_PALETTE = (
    (31, 31, 31),  # 0 bright white
    (0, 31, 31),   # 1 cyan
) + base.PALETTE_15[:14]


def _texture(fmt, width, height, texel, palette=b"", index=b"", bits=0):
    """One texture record plus its TEXIMAGE_PARAM word.

    ``bits`` carries the TEXIMAGE_PARAM extras: KeyColor0 (bit 29) and the DS
    wrap/flip bits 16-19. Size bits 20-25 encode 8 << shift, format is 26-28.
    """
    param = ((fmt & 7) << 26) | \
        (((width // 8).bit_length() - 1) << 20) | \
        (((height // 8).bit_length() - 1) << 23) | bits
    return {
        "format": fmt,
        "width": width,
        "height": height,
        "param": param,
        "texel": texel,
        "palette": palette,
        "index": index,
    }


def _a3i5_texel():
    """Alpha 7/5/3/0 in column pairs, rows identical, index 3 (yellow)."""
    return bytes(
        (3 | (A3I5_ALPHAS[x // 2] << 5))
        for _y in range(8) for x in range(8))


def _a5i3_texel():
    """Alpha 31/20/10/0 in column pairs, rows identical, index 4 (magenta)."""
    return bytes(
        (4 | (A5I3_ALPHAS[x // 2] << 3))
        for _y in range(8) for x in range(8))


def _i4_texel():
    """I4 rows alternating index 0 / index 1."""
    rows = [[(x + y) % 2 for x in range(8)] for y in range(8)]
    return base._pack_indices(rows, 4)


def _asymmetric_texel():
    """Opaque 16bpp ramp: every row/column distinct, markers at (0,0)/(7,7)."""
    out = bytearray()
    for y in range(8):
        for x in range(8):
            blue = 31 if (x, y) == (0, 0) else (16 if (x, y) == (7, 7) else 0)
            out += struct.pack("<H", base._rgb555(x * 4, y * 4, blue))
    return bytes(out)


def _span_texcoords(s0, s1, t0, t1):
    """Six texcoords in the QUAD_TEXCOORDS triangle order (TL,TR,BR,TL,BR,BL)."""
    return [(s0, t0), (s1, t0), (s1, t1),
            (s0, t0), (s1, t1), (s0, t1)]


def _build_textures():
    a3i5 = _a3i5_texel()
    a5i3 = _a5i3_texel()
    i4 = _i4_texel()
    i4_palette = base._words_from_zero(I4_PALETTE, 16)
    ramp = _asymmetric_texel()

    return (
        _texture(1, 8, 8, a3i5, palette=base._words_from_zero(TEX_PALETTE, 32)),
        _texture(6, 8, 8, a5i3, palette=base._words_from_zero(TEX_PALETTE, 8)),
        _texture(3, 8, 8, i4, palette=i4_palette, bits=KEY_COLOR0),
        _texture(3, 8, 8, i4, palette=i4_palette),
        _texture(7, 8, 8, ramp, bits=WRAP_REPEAT_S | WRAP_REPEAT_T),
        _texture(7, 8, 8, ramp),
        _texture(7, 8, 8, ramp,
                 bits=WRAP_REPEAT_S | WRAP_REPEAT_T |
                 WRAP_FLIP_S | WRAP_FLIP_T),
        _texture(7, 8, 8, ramp, bits=WRAP_FLIP_S | WRAP_FLIP_T),
    )


ROLES = (
    "A3I5 alpha",
    "A5I3 alpha",
    "I4 key0 on",
    "I4 key0 off",
    "repeat ST",
    "clamp ST",
    "mirror ST",
    "flip-only clamp",
)

KINDS = ("translucent",) * 4 + ("wrap",) * 4
GRIDS = (8, 8, 8, 8, 16, 16, 16, 16)
# (s0, t0, s1, t1) in texture-coordinate units for the wrap quads.
SPANS = (None, None, None, None,
         (0.0, 0.0, 2.0, 2.0),
         (-0.5, -0.5, 1.5, 1.5),
         (0.0, 0.0, 2.0, 2.0),
         (0.0, 0.0, 2.0, 2.0))


def _quad_texcoords(index):
    span = SPANS[index]
    if span is None:
        return QUAD_TEXCOORDS
    s0, t0, s1, t1 = span
    return _span_texcoords(s0, s1, t0, t1)


def _quad_specs():
    specs = []
    for index, texture in enumerate(_build_textures()):
        column = index % 4
        row = index // 4
        cell_x0, cell_x1 = base.COLUMN_EDGES[column], base.COLUMN_EDGES[column + 1]
        cell_y0, cell_y1 = base.ROW_EDGES[row], base.ROW_EDGES[row + 1]
        rect = (cell_x0 + base.QUAD_MARGIN_X, cell_y0 + base.QUAD_MARGIN_Y,
                cell_x1 - base.QUAD_MARGIN_X, cell_y1 - base.QUAD_MARGIN_Y)
        spec = dict(texture)
        spec["cell"] = (cell_x0, cell_y0, cell_x1, cell_y1)
        spec["rect"] = rect
        spec["corners"] = base._rect_quad(rect)
        spec["texcoords"] = _quad_texcoords(index)
        spec["role"] = ROLES[index]
        spec["kind"] = KINDS[index]
        spec["grid"] = GRIDS[index]
        spec["span"] = SPANS[index]
        specs.append(spec)
    return tuple(specs)


QUAD_SPECS = _quad_specs()

FORMAT_NAMES = base.FORMAT_NAMES


def expected_pixels(spec):
    """Expected RGBA8 texels (row-major, top row first) from the oracle."""
    import tex_decode_oracle
    return tex_decode_oracle.decode(
        spec["param"], spec["texel"], spec["palette"], spec["index"])


def _build_frame():
    out = []
    txdt = bytearray()
    pldt = bytearray()
    records = []
    for spec in QUAD_SPECS:
        texel_offset = len(txdt)
        txdt += spec["texel"]
        index_offset = 0
        if spec["index"]:
            index_offset = len(txdt)
            txdt += spec["index"]
        palette_offset = 0
        if spec["palette"]:
            palette_offset = len(pldt)
            pldt += spec["palette"]
        records.append({
            "teximage_param": spec["param"],
            "pltt_base": 0,
            "texel_offset": texel_offset,
            "texel_size": len(spec["texel"]),
            "palette_offset": palette_offset,
            "palette_size": len(spec["palette"]),
            "index_offset": index_offset,
            "index_size": len(spec["index"]),
        })
        base._tex_run(out, spec, base._quad_triangles(spec["corners"]),
                      spec["texcoords"])
    out.append((CMD_SWAP_BUFFERS, 0))

    meta_payload = struct.pack("<IIII", 2, 0x7, 0, 0)
    body = base._chunk(GXCM, entries(*out), flags=CHUNK_FLAG_REQUIRED)
    body += base._chunk(META, meta_payload)
    body += base._chunk(TXDS, base._txds_payload(records))
    body += base._chunk(TXDT, bytes(txdt))
    body += base._chunk(PLDT, bytes(pldt))

    total = 32 + len(body)
    header = struct.pack("<IIIIIIII", 2, 32, total, 1, 1, 5, 0, 0)
    return header + body


def frame_bytes():
    return _build_frame()


def write_frame(outdir):
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, FRAME_NAME)
    with open(path, "wb") as handle:
        handle.write(frame_bytes())
    return path


def describe():
    """Print quad -> role -> expected result description."""
    print("frame: %s (one v2 frame, 8 quads, white vertex colour, alpha 255)"
          % FRAME_NAME)
    print("blend: fragment = texel_rgb*a + clear*(1-a), clear=(16,16,24), "
          "a on the 0..255 scale")
    print("orientation: decoded row 0 is the TOP screen edge of the quad")
    for index, spec in enumerate(QUAD_SPECS):
        fmt = spec["format"]
        print("quad %d: role=%s fmt %d %s %dx%d param=0x%08X "
              "screen=(%d,%d)-(%d,%d)"
              % (index, spec["role"], fmt, FORMAT_NAMES[fmt], spec["width"],
                 spec["height"], spec["param"], spec["rect"][0],
                 spec["rect"][1], spec["rect"][2], spec["rect"][3]))
        if spec["kind"] == "translucent":
            if index == 0:
                print("  A3I5 alpha 7/5/3/0 in column pairs -> blended with "
                      "clear; alpha 0 pair shows clear exactly")
            elif index == 1:
                print("  A5I3 alpha 31/20/10/0 in column pairs -> blended "
                      "with clear; alpha 0 pair shows clear exactly")
            elif index == 2:
                print("  I4 KeyColor0 set: index-0 texels show clear, "
                      "index-1 texels show cyan")
            else:
                print("  I4 KeyColor0 clear: index-0 texels show white, "
                      "index-1 texels show cyan")
        else:
            s0, t0, s1, t1 = spec["span"]
            print("  wrap texcoords s %g..%g t %g..%g; expected texel = "
                  "asymmetric ramp at the DS-wrapped coordinate"
                  % (s0, s1, t0, t1))


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_tex_behaviour_frames.py",
        description="Write the deterministic texture-behaviour v2 frame.")
    parser.add_argument("outdir", nargs="?", help="output directory")
    parser.add_argument("--describe", action="store_true",
                        help="print quad -> role -> expected description")
    args = parser.parse_args(argv)

    if args.describe:
        describe()
    if args.outdir:
        path = write_frame(args.outdir)
        if not args.describe:
            print(path)
    elif not args.describe:
        parser.error("outdir is required unless --describe is used")
    return 0


if __name__ == "__main__":
    sys.exit(main())
