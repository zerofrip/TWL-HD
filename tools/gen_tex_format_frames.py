#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the multi-format texture frame for 5B-2e-2 validation.

Writes ONE deterministic v2 frame, ``twlhd_frame_v2_formats.bin``, with eight
non-overlapping quads on a 4x2 grid of the 400x240 top screen (white vertex
colours, so MODULATE shows the decoded texels unchanged). Each quad shows one
8x8 texture, except the last one (16x16):

    quad 0  format 1  A3I5
    quad 1  format 2  I2
    quad 2  format 3  I4 (8x8)
    quad 3  format 4  I8
    quad 4  format 5  compressed 4x4, one block per mode 0/1/2/3
    quad 5  format 6  A5I3
    quad 6  format 7  16bpp (reference)
    quad 7  format 3  I4 (16x16, 15-colour palette)

Alpha choice: every texel is fully opaque (A3I5 alpha 7, A5I3 alpha 31,
palette entries carry the opaque bit 15 where the format has one). Bit 15 is
masked off by the decoder for formats 1-6, so it does not change the colour.

Colour-0 choice: TEXIMAGE_PARAM bit 29 (KeyColor0) is left clear AND palette
index 0 is never used as a visible colour in formats 2/3/4 (their index 0
translates to (0, 0, 0), which would punch background holes). Both belt and
braces; quad 5 (format 6) and quad 0 (format 1) do use palette entry 0 for
some texels because those formats ignore KeyColor0.

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

# v2 container helpers from the parser agreement suite.
from test_twlhd_v2 import entries  # noqa: E402

# v2 command ids (common/twlhd_v2_format.h:94-130).
CMD_COLOR = 0x20
CMD_TEX_COORD = 0x22
CMD_VTX_16 = 0x23
CMD_TEXIMAGE_PARAM = 0x2A
CMD_PLTT_BASE = 0x2B
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_SWAP_BUFFERS = 0x50

FRAME_NAME = "twlhd_frame_v2_formats.bin"

# 4x2 grid of the 400x240 top screen. The DS viewport is pillar-boxed into the
# central 320 pixels (twlhd_clipfix.h: the 0.8 factor is 320/400), so quads must
# stay inside screen x 40..360 or PICA clips them at x_clip = +-4096. The last
# column is wider so the 16x16 texture keeps >= 6 px per texel cell.
COLUMN_EDGES = (40, 112, 184, 256, 360)
ROW_EDGES = (0, 120, 240)
QUAD_MARGIN_X = 4
QUAD_MARGIN_Y = 10

WHITE_555 = (31 << 0) | (31 << 5) | (31 << 10) | 0x8000

# Strongly separated 5-bit colours. Index i here is palette entry i+1 in the
# formats that reserve palette index 0 (formats 2/3/4).
PALETTE_15 = (
    (31, 0, 0),    # 1  red
    (0, 31, 0),    # 2  green
    (0, 0, 31),    # 3  blue
    (31, 31, 0),   # 4  yellow
    (31, 0, 31),   # 5  magenta
    (0, 31, 31),   # 6  cyan
    (31, 16, 0),   # 7  orange
    (16, 0, 31),   # 8  purple
    (0, 31, 16),   # 9  spring green
    (31, 0, 16),   # 10 rose
    (16, 31, 0),   # 11 lime
    (0, 16, 31),   # 12 azure
    (31, 31, 16),  # 13 pale yellow
    (16, 31, 31),  # 14 pale cyan
    (31, 16, 31),  # 15 pink
)
PALETTE_8 = PALETTE_15[:8]


def _rgb555(r, g, b):
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10) | 0x8000


def _words(colours, entry_count):
    """Little-endian 16-bit palette entries; palette entry 0 is empty."""
    out = bytearray()
    for i in range(entry_count):
        if i == 0 or i > len(colours):
            word = 0
        else:
            word = _rgb555(*colours[i - 1])
        out += struct.pack("<H", word)
    return bytes(out)


def _words_from_zero(colours, entry_count):
    """Palette where entry 0 is the first colour (formats 1/6/7)."""
    out = bytearray()
    for i in range(entry_count):
        word = _rgb555(*colours[i]) if i < len(colours) else 0
        out += struct.pack("<H", word)
    return bytes(out)


def _pack_indices(rows, bits):
    """Pack index rows low bits first (I2: 4/byte, I4: low nibble first)."""
    per_byte = 8 // bits
    mask = (1 << bits) - 1
    out = bytearray()
    for row in rows:
        for start in range(0, len(row), per_byte):
            byte = 0
            for j in range(per_byte):
                if start + j < len(row):
                    byte |= (row[start + j] & mask) << (bits * j)
            out.append(byte)
    return bytes(out)


def _grid(width, height, index_at):
    return [[index_at(x, y) for x in range(width)] for y in range(height)]


def _texel_16bpp(width, height, index_at):
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out += struct.pack("<H", _rgb555(*PALETTE_8[index_at(x, y)]))
    return bytes(out)


# 4x4 compressed blocks, one per mode. The palette byte offset of each block
# is chosen so blocks never alias: 0/8/12/20 (word values 0/2/3/5).
FMT5_BLOCKS = (
    {   # mode 0: three opaque colours, index 3 (transparent) unused
        "mode": 0, "palette_offset": 0,
        "colours": [(31, 0, 0), (0, 31, 0), (0, 0, 31)],
        "rows": ((0, 1, 2, 0), (1, 2, 0, 1), (2, 0, 1, 2), (0, 1, 2, 0)),
    },
    {   # mode 1: two colours plus their average, index 3 unused
        "mode": 1, "palette_offset": 8,
        "colours": [(31, 31, 0), (0, 31, 31)],
        "rows": ((2, 1, 0, 2), (1, 0, 2, 1), (0, 2, 1, 0), (2, 1, 0, 2)),
    },
    {   # mode 2: four opaque colours
        "mode": 2, "palette_offset": 12,
        "colours": [(31, 0, 31), (31, 16, 0), (16, 0, 31), (16, 31, 0)],
        "rows": ((3, 2, 1, 0), (2, 1, 0, 3), (1, 0, 3, 2), (0, 3, 2, 1)),
    },
    {   # mode 3: two colours plus the 5:3 / 3:5 blends
        "mode": 3, "palette_offset": 20,
        "colours": [(0, 31, 0), (31, 0, 0)],
        "rows": ((0, 1, 2, 3), (1, 2, 3, 0), (2, 3, 0, 1), (3, 0, 1, 2)),
    },
)


def _fmt5_data():
    palette = bytearray(FMT5_BLOCKS[-1]["palette_offset"] + 4)
    texel = bytearray()
    index = bytearray()
    for block in FMT5_BLOCKS:
        offset = block["palette_offset"]
        for k, color in enumerate(block["colours"]):
            struct.pack_into("<H", palette, offset + k * 2, _rgb555(*color))
        for row in block["rows"]:
            texel.append(row[0] | (row[1] << 2) | (row[2] << 4) |
                         (row[3] << 6))
        index += struct.pack("<H",
                             (block["mode"] << 14) | (offset // 4))
    return bytes(palette), bytes(texel), bytes(index)


def _texture(fmt, width, height, texel, palette=b"", index=b""):
    """One texture record plus its TEXIMAGE_PARAM word.

    Size bits 20-22 (S) and 23-25 (T) encode 8 << shift; format is at 26-28.
    Bit 29 (KeyColor0) is left clear for every quad.
    """
    param = ((fmt & 7) << 26) | \
        (((width // 8).bit_length() - 1) << 20) | \
        (((height // 8).bit_length() - 1) << 23)
    return {
        "format": fmt,
        "width": width,
        "height": height,
        "param": param,
        "texel": texel,
        "palette": palette,
        "index": index,
    }


def _build_textures():
    fmt5_palette, fmt5_texel, fmt5_index = _fmt5_data()

    a3i5 = bytes(
        (index | (7 << 5)) for row in
        _grid(8, 8, lambda x, y: (x + 2 * y) % 8) for index in row)
    i2 = _pack_indices(
        _grid(8, 8, lambda x, y: 1 + (x + y) % 3), 2)
    i4_8 = _pack_indices(
        _grid(8, 8, lambda x, y: 1 + (x + 2 * y) % 15), 4)
    i8 = bytes(
        index for row in
        _grid(8, 8, lambda x, y: 1 + (3 * x + y) % 15) for index in row)
    a5i3 = bytes(
        (index | (31 << 3)) for row in
        _grid(8, 8, lambda x, y: (2 * x + y) % 8) for index in row)
    tex16 = _texel_16bpp(8, 8, lambda x, y: (3 * x + y) % 8)
    i4_16 = _pack_indices(
        _grid(16, 16, lambda x, y: 1 + (x + y) % 15), 4)

    return (
        _texture(1, 8, 8, a3i5,
                 palette=_words_from_zero(PALETTE_8, 32)),
        _texture(2, 8, 8, i2, palette=_words(PALETTE_15[:3], 4)),
        _texture(3, 8, 8, i4_8, palette=_words(PALETTE_15, 16)),
        _texture(4, 8, 8, i8, palette=_words(PALETTE_15, 256)),
        _texture(5, 8, 8, fmt5_texel, palette=fmt5_palette,
                 index=fmt5_index),
        _texture(6, 8, 8, a5i3,
                 palette=_words_from_zero(PALETTE_8, 8)),
        _texture(7, 8, 8, tex16),
        _texture(3, 16, 16, i4_16, palette=_words(PALETTE_15, 16)),
    )


def _clip_x(px):
    """Screen x to clip*4096.

    Emulator-observed mapping: x_screen = 200 + 160 * (x_clip / 4096), i.e.
    the 320-wide pillar-boxed DS viewport inside the 400-pixel top screen.
    The inverse is x_clip = (x_screen - 200) * 4096 / 160.
    """
    return int(round((px - 200.0) * 4096.0 / 160.0))


def _clip_y(py):
    """Screen y to clip*4096: screen_y = (1 - y_clip/4096) * 120."""
    return int(round((1.0 - py / 120.0) * 4096.0))


def _rect_quad(rect):
    x0, y0, x1, y1 = rect
    left, right = _clip_x(x0), _clip_x(x1)
    top, bottom = _clip_y(y0), _clip_y(y1)
    return ((left, top), (right, top), (right, bottom), (left, bottom))


def _quad_specs():
    specs = []
    for index, texture in enumerate(_build_textures()):
        column = index % 4
        row = index // 4
        cell_x0, cell_x1 = COLUMN_EDGES[column], COLUMN_EDGES[column + 1]
        cell_y0, cell_y1 = ROW_EDGES[row], ROW_EDGES[row + 1]
        rect = (cell_x0 + QUAD_MARGIN_X, cell_y0 + QUAD_MARGIN_Y,
                cell_x1 - QUAD_MARGIN_X, cell_y1 - QUAD_MARGIN_Y)
        spec = dict(texture)
        spec["cell"] = (cell_x0, cell_y0, cell_x1, cell_y1)
        spec["rect"] = rect
        spec["corners"] = _rect_quad(rect)
        specs.append(spec)
    return tuple(specs)


QUAD_SPECS = _quad_specs()

FORMAT_NAMES = {
    1: "A3I5", 2: "I2", 3: "I4", 4: "I8", 5: "4x4", 6: "A5I3", 7: "16bpp",
}


def expected_pixels(spec):
    """Expected RGBA8 pixels (row-major, top row first) from the oracle."""
    import tex_decode_oracle
    return tex_decode_oracle.decode(
        spec["param"], spec["texel"], spec["palette"], spec["index"])


def _vtx16(x, y):
    return [(CMD_VTX_16, ((y & 0xFFFF) << 16) | (x & 0xFFFF)),
            (CMD_VTX_16, 0)]


def _tex_coord(s, t):
    return (CMD_TEX_COORD, ((t & 0xFFFF) << 16) | (s & 0xFFFF))


def _quad_triangles(corners):
    return [corners[0], corners[1], corners[2],
            corners[0], corners[2], corners[3]]


def _tex_run(out, spec, triangle_corners, texcoords):
    out.append((CMD_TEXIMAGE_PARAM, spec["param"]))
    out.append((CMD_PLTT_BASE, 0))
    out.append((CMD_COLOR, WHITE_555))
    out.append((CMD_BEGIN_VTXS, 0))
    for corner, (u, v) in zip(triangle_corners, texcoords):
        s = int(u * spec["width"] * 16.0) & 0xFFFF
        t = int(v * spec["height"] * 16.0) & 0xFFFF
        out.append(_tex_coord(s, t))
        out.extend(_vtx16(corner[0], corner[1]))
    out.append((CMD_END_VTXS, 0))


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


def _chunk(ctype, payload, flags=0, reserved=0):
    return struct.pack("<IIII", ctype, len(payload), flags, reserved) + payload


def _fourcc(text):
    raw = text.encode("ascii")
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24)


GXCM = _fourcc("GXCM")
META = _fourcc("META")
TXDS = _fourcc("TXDS")
TXDT = _fourcc("TXDT")
PLDT = _fourcc("PLDT")

CHUNK_FLAG_REQUIRED = 0x00000001

QUAD_TEXCOORDS = [
    (0.0, 0.0), (1.0, 0.0), (1.0, 1.0),
    (0.0, 0.0), (1.0, 1.0), (0.0, 1.0),
]


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
        _tex_run(out, spec, _quad_triangles(spec["corners"]), QUAD_TEXCOORDS)
    out.append((CMD_SWAP_BUFFERS, 0))

    meta_payload = struct.pack("<IIII", 2, 0x7, 0, 0)
    body = _chunk(GXCM, entries(*out), flags=CHUNK_FLAG_REQUIRED)
    body += _chunk(META, meta_payload)
    body += _chunk(TXDS, _txds_payload(records))
    body += _chunk(TXDT, bytes(txdt))
    body += _chunk(PLDT, bytes(pldt))

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
    """Print quad -> format -> expected texel grid."""
    print("frame: %s (one v2 frame, 8 quads, white vertex colour, alpha 255)"
          % FRAME_NAME)
    print("colour-0: TEXIMAGE_PARAM bit 29 clear; formats 2/3/4 never use "
          "palette index 0 as a visible colour")
    print("orientation: corrected; decoded row 0 is the TOP screen edge of "
          "the quad (memory tile row 0 = image top row)")
    for index, spec in enumerate(QUAD_SPECS):
        fmt = spec["format"]
        print("quad %d: fmt %d %s %dx%d param=0x%08X screen=(%d,%d)-(%d,%d)"
              % (index, fmt, FORMAT_NAMES[fmt], spec["width"], spec["height"],
                 spec["param"], spec["rect"][0], spec["rect"][1],
                 spec["rect"][2], spec["rect"][3]))
        pixels = expected_pixels(spec)
        for y in range(spec["height"]):
            row = []
            for x in range(spec["width"]):
                r, g, b = pixels[y * spec["width"] + x][:3]
                row.append("%02X%02X%02X" % (r, g, b))
            print("  " + " ".join(row))


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_tex_format_frames.py",
        description="Write the deterministic multi-format v2 texture frame.")
    parser.add_argument("outdir", nargs="?", help="output directory")
    parser.add_argument("--describe", action="store_true",
                        help="print quad -> format -> expected texel grid")
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
