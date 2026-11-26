#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the EQ-4zc arm11 alpha-sweep v2 frames (offline tooling).

Two deterministic frames are written into the output directory:

  eq4zc_sweep_green.bin       11 solid quads, pure (0,255,0), alphas
                              [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
  eq4zc_sweep_blue.bin        the same 11 quads, pure (0,0,255)

Each frame is a scene-4-style quad list: one ``POLYGON_ATTR`` with
``POLY_ALPHA(a) | POLY_CULL_NONE`` (0xC0) per quad, one ``COLOR``, then a
``BEGIN_VTXS`` quad list with four ``VTX_16`` vertices (the scene-4 order
bottom-left, bottom-right, top-right, top-left).  A single ``RSTA`` entry
writes ``0x04000060 = 0x18`` (bit 3 set: blend enabled), so the arm11 blend
gate renders with blending over its black clear.

Next to each frame a ``*_layout.json`` records the quad rectangles in DS
pixels, in alpha order, with no capture mapping or model baked in.

BUILD/EMULATOR tooling only.  Stdlib only.  Nothing here contacts a console or
a network address.  The v2 container helpers are imported from
``tools/test_twlhd_v2.py``; this file does not modify them.
"""

import argparse
import json
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from test_twlhd_v2 import build, entries, rsta  # noqa: E402
import twlhd_v2_dump as v2  # noqa: E402

# v2 command ids (common/twlhd_v2_format.h:94-130).
CMD_COLOR = 0x20
CMD_VTX_16 = 0x23
CMD_POLYGON_ATTR = 0x29
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_SWAP_BUFFERS = 0x50

# POLY_ALPHA(a) | POLY_CULL_NONE: libnds videoGL.h:189, POLY_CULL_NONE = 3<<6.
POLY_CULL_NONE = 0xC0

ALPHAS = (1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31)

# DS viewport used by the arm11 clipfix when the frame carries no viewport
# command (common/twlhd_clipfix.c:12): inclusive 0,0,255,191.
VIEWPORT = (0, 0, 255, 191)

# 6x2 grid inside 256x192: quad 28x39 DS px, gaps 10 px horizontally and 18 px
# vertically, margins 19 px left/right and 48 px top/bottom.  Every y edge is a
# multiple of 3, so the fixed-point mapping is exact: clip_y =
# 4096*(192 - y - 96)/96 = 128*(96 - y)/3, an integer whenever y is a
# multiple of 3, with no 1/96 DS px rounding.
COL_X0 = (19, 57, 95, 133, 171, 209)
QUAD_WIDTH = 28
ROW_Y = ((48, 87), (105, 144))

RECTS = []
for _index, _alpha in enumerate(ALPHAS):
    _col = _index % 6
    _row = _index // 6
    RECTS.append((COL_X0[_col], ROW_Y[_row][0],
                  COL_X0[_col] + QUAD_WIDTH, ROW_Y[_row][1]))
RECTS = tuple(RECTS)

FRAME_NAMES = ("eq4zc_sweep_green.bin", "eq4zc_sweep_blue.bin")
SOURCE_RGB = {"eq4zc_sweep_green.bin": (0, 255, 0),
              "eq4zc_sweep_blue.bin": (0, 0, 255)}
INTERIOR_SHRINK = 2


def _rgb555(r, g, b):
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10)


def _vtx16(x, y):
    """Two GXCM entries for one VTX_16 command (z = 0)."""
    return [(CMD_VTX_16, ((y & 0xFFFF) << 16) | (x & 0xFFFF)),
            (CMD_VTX_16, 0)]


def clip_x(ds_x):
    """DS pixel column -> raw v16 clip x (exact): clip_x = 32*ds_x - 4096."""
    return 32 * ds_x - 4096


def clip_y(ds_y_top):
    """DS top-down row -> raw v16 clip y (exact when 192 - y is a multiple of 3).

    DS y from the bottom is 192 - y_top; the default viewport is 192 rows and
    DS clip y = 4096*(y_from_bottom - 96)/96.
    """
    return 4096 * (192 - ds_y_top - 96) // 96


def _quad(out, alpha, colour, rect):
    """One scene-4-style quad: POLYGON_ATTR, COLOR, quad list (BL/BR/TR/TL)."""
    x0, y0, x1, y1 = rect
    out.append((CMD_POLYGON_ATTR, (alpha << 16) | POLY_CULL_NONE))
    out.append((CMD_COLOR, colour))
    out.append((CMD_BEGIN_VTXS, 1))  # quad list, as glBegin(GL_QUADS)
    for x, y in ((x0, y1), (x1, y1), (x1, y0), (x0, y0)):
        out.extend(_vtx16(clip_x(x), clip_y(y)))
    out.append((CMD_END_VTXS, 0))


def frame_bytes(name):
    if name not in FRAME_NAMES:
        raise ValueError("unknown frame name: %s" % (name,))
    out = []
    colour = _rgb555(*SOURCE_RGB[name])
    for alpha, rect in zip(ALPHAS, RECTS):
        _quad(out, alpha, colour, rect)
    out.append((CMD_SWAP_BUFFERS, 0))
    return build(entries(*out),
                 rsta_payload=rsta((0x04000060, 0x18, 0, 2)),
                 flags=v2.FLAG_FRAME_COMPLETE)


def layout(name):
    if name not in FRAME_NAMES:
        raise ValueError("unknown frame name: %s" % (name,))
    return {
        "frame": name,
        "source_rgb": list(SOURCE_RGB[name]),
        "viewport": list(VIEWPORT),
        "interior_shrink": INTERIOR_SHRINK,
        "alphas": list(ALPHAS),
        "quads": [{"alpha": alpha, "rect": list(rect)}
                  for alpha, rect in zip(ALPHAS, RECTS)],
    }


def layout_name(name):
    return name[:-len(".bin")] + "_layout.json"


def write_frames(outdir):
    os.makedirs(outdir, exist_ok=True)
    paths = []
    for name in FRAME_NAMES:
        path = os.path.join(outdir, name)
        with open(path, "wb") as handle:
            handle.write(frame_bytes(name))
        paths.append(path)
        path = os.path.join(outdir, layout_name(name))
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(layout(name), handle, indent=2, sort_keys=True)
            handle.write("\n")
        paths.append(path)
    return paths


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_sweep_frames.py",
        description="Write the two deterministic EQ-4zc alpha-sweep v2 frames.")
    parser.add_argument("outdir", help="output directory")
    args = parser.parse_args(argv)
    for path in write_frames(args.outdir):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
