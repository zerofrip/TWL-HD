#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate a texture-behaviour frame screenshot (5B-2e-2 follow-up).

Reads a P6 PPM (400x240 top screen, or 400x480 with the top screen in the
upper 240 rows). For every quad it samples a fixed cell grid and compares the
measured screen colours with expectations computed independently here from
tools/tex_decode_oracle.py (never from the C decoder):

  - translucent quads q0-q3: expected = texel_rgb*a + clear*(1-a), with
    clear = (16,16,24) and a the oracle RGBA alpha on the 0..255 scale. This
    also covers colour-0 transparency: a KeyColor0 texel decodes to alpha 0,
    so it blends to clear exactly.
  - wrap quads q4-q7: expected = the oracle texel at the DS-wrapped texel
    coordinate, sampled at the centres of 16 steps across the quad's texcoord
    span (never exactly on a texel boundary).

Why the cell grid is mapped from the generator's known rect: alpha-0 / colour-0
texels render exactly the clear colour, so a bounding-box search over non-clear
pixels would shrink the quad and corrupt the texel->pixel mapping. The emulator
mapping of the rect is pixel-exact (verified by the multi-format frame run), so
the rect is the right anchor; a whole-quad +-1 px shift search absorbs small
offsets.

Wrap rule (DS side): TEXIMAGE_PARAM bits 16/17 = repeat S/T, bits 18/19 = flip
S/T. common/twlhd_pica_tex.c twlhd_pica_wrap_from_teximage maps repeat+flip ->
MIRRORED_REPEAT, repeat -> REPEAT, otherwise CLAMP_TO_EDGE (flip alone =
clamp). The expected row/column below is computed directly on the DS t/s texel
coordinate: repeat = mod, mirrored repeat = triangle wave about the height
(t in [h,2h] -> 2h-t), clamp = min/max. This is an exact equivalent of the C
path: twlhd_pica_texcoords_from_gx computes u = s/w and v = 1 - t/h; working
the PICA wrap through that v mapping (v=1 samples memory row 0 = image top row)
reduces to the triangle about t=h, including t=1.5h -> v<0 -> clamped to the
image bottom row h-1. So the checker does not ignore the v-flip.

Decoded row 0 is pinned at the TOP screen edge of the quad (post-orientation-fix
rule); there is no flip search.

Usage:
    python3 tools/check_tex_behaviour.py <ppm> [--tolerance N]

Exits 0 only when every sampled cell of every quad matches.
"""

import argparse
import math
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from check_tex_shot import parse_ppm  # noqa: E402
import gen_tex_behaviour_frames as gen  # noqa: E402

TOLERANCE = 28
# OLD-build clear colour for this synthetic validator; kept self-consistent.
CLEAR = (16, 16, 24)
SHIFTS = (-1, 0, 1)

WRAP_REPEAT = "repeat"
WRAP_MIRROR = "mirror"
WRAP_CLAMP = "clamp"


def _parse_clear(text):
    """Parse --clear R,G,B (0..255 each)."""
    parts = text.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected R,G,B")
    try:
        values = tuple(int(part) for part in parts)
    except ValueError:
        raise argparse.ArgumentTypeError("expected integers R,G,B")
    if any(v < 0 or v > 255 for v in values):
        raise argparse.ArgumentTypeError("values must be 0..255")
    return values


def _is_clear(pixel, tolerance):
    return all(abs(pixel[i] - CLEAR[i]) <= tolerance for i in range(3))


def _pixel_at(data, width, x, y):
    off = (y * width + x) * 3
    return (data[off], data[off + 1], data[off + 2])


def find_bbox(data, width, top, cell, tolerance):
    """Bounding box of the non-clear pixels inside one grid cell."""
    x0, y0, x1, y1 = cell
    x1 = min(x1, width)
    y1 = min(y1, top)
    left = top_y = right = bottom = None
    for y in range(y0, y1):
        for x in range(x0, x1):
            if _is_clear(_pixel_at(data, width, x, y), tolerance):
                continue
            if left is None or x < left:
                left = x
            if right is None or x > right:
                right = x
            if top_y is None or y < top_y:
                top_y = y
            if bottom is None or y > bottom:
                bottom = y
    if left is None:
        return None
    return left, top_y, right, bottom


def _mean3(data, width, top, x, y):
    total = [0, 0, 0]
    count = 0
    for py in range(max(0, y - 1), min(top, y + 2)):
        for px in range(max(0, x - 1), min(width, x + 2)):
            pixel = _pixel_at(data, width, px, py)
            for channel in range(3):
                total[channel] += pixel[channel]
            count += 1
    return tuple(int(round(total[channel] / count)) for channel in range(3))


def blend(rgba):
    """Texel over clear with SRC_ALPHA / ONE_MINUS_SRC_ALPHA (0..255 alpha)."""
    alpha = rgba[3]
    return tuple((rgba[c] * alpha + CLEAR[c] * (255 - alpha)) // 255
                 for c in range(3))


def wrap_axis(param, axis):
    """DS wrap mode for axis 's' or 't' from TEXIMAGE_PARAM bits 16-19."""
    if axis == "s":
        repeat, flip = (param >> 16) & 1, (param >> 18) & 1
    else:
        repeat, flip = (param >> 17) & 1, (param >> 19) & 1
    if repeat and flip:
        return WRAP_MIRROR
    if repeat:
        return WRAP_REPEAT
    return WRAP_CLAMP


def wrap_coord(x, size, mode):
    """Wrapped texel index (floor of the wrapped continuous coordinate)."""
    if mode == WRAP_REPEAT:
        return math.floor(x % size)
    if mode == WRAP_MIRROR:
        period = 2 * size
        m = x % period
        value = m if m < size else period - m
        return min(math.floor(value), size - 1)
    return min(max(math.floor(x), 0), size - 1)


def cell_positions(spec):
    """Screen pixel at the centre of every sample cell (row-major)."""
    grid = spec["grid"]
    x0, y0, x1, y1 = spec["rect"]
    return [(int(x0 + (i + 0.5) * (x1 - x0) / grid),
             int(y0 + (j + 0.5) * (y1 - y0) / grid))
            for j in range(grid) for i in range(grid)]


def _expected_translucent(spec, pixels):
    return [blend(pixels[ty * 8 + tx])
            for ty in range(8) for tx in range(8)]


def _expected_wrap(spec, pixels):
    s0, t0, s1, t1 = spec["span"]
    mode_s = wrap_axis(spec["param"], "s")
    mode_t = wrap_axis(spec["param"], "t")
    width, height = spec["width"], spec["height"]
    out = []
    for j in range(16):
        t = (t0 + (j + 0.5) * (t1 - t0) / 16.0) * height
        row = wrap_coord(t, height, mode_t)
        for i in range(16):
            s = (s0 + (i + 0.5) * (s1 - s0) / 16.0) * width
            col = wrap_coord(s, width, mode_s)
            out.append(pixels[row * width + col][:3])
    return out


def expected_cells(spec):
    """Expected screen RGB for every sample cell, computed from the oracle."""
    pixels = gen.expected_pixels(spec)
    if spec["kind"] == "translucent":
        return _expected_translucent(spec, pixels)
    return _expected_wrap(spec, pixels)


def check_quad(data, width, top, spec, tolerance):
    """Return (bbox, dx, dy, matched, total, worst, worst_cell) or None."""
    bbox = find_bbox(data, width, top, spec["cell"], tolerance)
    if bbox is None:
        return None
    positions = cell_positions(spec)
    expected = expected_cells(spec)
    total = len(expected)

    best = None
    for dx in SHIFTS:
        for dy in SHIFTS:
            matched = 0
            worst = 0
            worst_cell = -1
            for cell, (px, py) in enumerate(positions):
                got = _mean3(data, width, top, px + dx, py + dy)
                want = expected[cell]
                deviation = max(abs(got[i] - want[i]) for i in range(3))
                if deviation > worst:
                    worst = deviation
                    worst_cell = cell
                if deviation <= tolerance:
                    matched += 1
            score = (-matched, worst, abs(dx) + abs(dy))
            if best is None or score < best[0]:
                best = (score, dx, dy, matched, worst, worst_cell)
    return (bbox,) + best[1:]


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="check_tex_behaviour.py",
        description="Validate a texture-behaviour frame screenshot.")
    parser.add_argument("ppm", help="input P6 PPM file")
    parser.add_argument("--tolerance", type=int, default=TOLERANCE,
                        help="per-channel RGB tolerance (default 28)")
    parser.add_argument("--clear", type=_parse_clear, default=None,
                        help="clear colour R,G,B of the capture (default "
                             "16,16,24; a black-clear DS ROM needs 0,0,0)")
    args = parser.parse_args(argv)
    if args.clear is not None:
        global CLEAR
        CLEAR = args.clear

    width, height, data = parse_ppm(args.ppm)
    if width != 400 or height not in (240, 480):
        print("FAIL: expected a 400x240 or 400x480 PPM, got %dx%d"
              % (width, height), file=sys.stderr)
        return 2
    top = 240

    all_ok = True
    for index, spec in enumerate(gen.QUAD_SPECS):
        label = "quad %d role=%s" % (index, spec["role"])
        result = check_quad(data, width, top, spec, args.tolerance)
        if result is None:
            print("%s MISSING: no non-clear pixels in cell %s FAIL"
                  % (label, spec["cell"]))
            all_ok = False
            continue
        bbox, dx, dy, matched, worst, worst_cell = result
        total = len(expected_cells(spec))
        ok = matched == total
        print("%s cells=%d/%d worst=%d shift=(%d,%d) bbox=%s %s"
              % (label, matched, total, worst, dx, dy, bbox,
                 "OK" if ok else "FAIL"))
        if not ok:
            all_ok = False
            # Cell dump for the docs/FINDING record (bounded).
            grid = spec["grid"]
            expected = expected_cells(spec)
            shown = 0
            for cell, (px, py) in enumerate(cell_positions(spec)):
                got = _mean3(data, width, top, px + dx, py + dy)
                want = expected[cell]
                if max(abs(got[i] - want[i]) for i in range(3)) > args.tolerance:
                    print("  cell(%d,%d) expected=%s measured=%s"
                          % (cell % grid, cell // grid, want, got))
                    shown += 1
                    if shown >= 8:
                        print("  ... more mismatches omitted")
                        break

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
