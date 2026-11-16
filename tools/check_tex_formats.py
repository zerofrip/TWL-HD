#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate a multi-format texture frame screenshot (5B-2e-2 follow-up).

Reads a P6 PPM (400x240 top screen, or 400x480 with the top screen in the
upper 240 rows), locates each quad's bounding box inside its 4x2 grid cell,
samples the centre of every texel cell with a 3x3 mean and compares it with the
expected colours from tools/tex_decode_oracle.py, computed from the same
generator data (tools/gen_tex_format_frames.py). The expectation therefore
does not depend on the C decoder.

Each quad is checked with whole-quad shifts dx, dy in {-1, 0, 1}; the best
shift is reported. The texel-row orientation is pinned: decoded row 0 is the
TOP screen edge of the quad, matching the corrected PICA memory layout
(memory tile row 0 = image top row). There is no flip search.

Palettes are rich, so the checker does not snap colours; it uses a per-channel
tolerance (default 28, JPEG-capture friendly). The default is bounded by
measurement: the worst cell on the Run 20 hardware capture is 26 (16x16 I4
quad), while the closest two distinct expected colours of any quad are 66
levels apart (4x4 blend colours), so a totally substituted texel still fails
with a margin of 38, and a 50/50 edge blend against that pair deviates by 33
and so still fails at 28. The 3x3 centre sampling rule is unchanged. See
docs/reports/tex-formats-tolerance.md for the measured table and the
separation-margin arithmetic.

Usage:
    python3 tools/check_tex_formats.py <ppm> [--tolerance N]

Exits 0 only when every texel cell of every quad matches.
"""

import argparse
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from check_tex_shot import parse_ppm  # noqa: E402
import gen_tex_format_frames as gen  # noqa: E402

TOLERANCE = 28
CLEAR = (16, 16, 24)
SHIFTS = (-1, 0, 1)


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
    left = top_y = None
    right = bottom = None
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


def _cell_centre(left, size, texels, cell):
    """Pixel index at the centre of texel cell `cell`."""
    return int(left + (cell + 0.5) * size / texels)


def sample_quad(data, width, top, spec, expected, tolerance):
    """Return (bbox, dx, dy, matched, worst, cells) or None.

    `cells` lists one tuple per sampled texel, (tx, ty, expected RGB, sampled
    RGB, per-channel deviation), in the best-shift order. Shared with
    tools/measure_tex_formats_deviation.py so both use the same cell logic.
    """
    bbox = find_bbox(data, width, top, spec["cell"], tolerance)
    if bbox is None:
        return None
    left, upper, right, bottom = bbox
    box_w = right - left + 1
    box_h = bottom - upper + 1
    tex_w, tex_h = spec["width"], spec["height"]

    best = None
    for dx in SHIFTS:
        for dy in SHIFTS:
            matched = 0
            worst = 0
            cells = []
            for ty in range(tex_h):
                y = _cell_centre(upper, box_h, tex_h, ty) + dy
                for tx in range(tex_w):
                    x = _cell_centre(left, box_w, tex_w, tx) + dx
                    got = _mean3(data, width, top, x, y)
                    # Decoded row 0 is the top screen edge (corrected pin).
                    want = expected[ty * tex_w + tx]
                    deviation = max(abs(got[i] - want[i]) for i in range(3))
                    cells.append((tx, ty, want[:3], got, deviation))
                    if deviation > worst:
                        worst = deviation
                    if deviation <= tolerance:
                        matched += 1
            score = (-matched, worst, abs(dx) + abs(dy))
            if best is None or score < best[0]:
                best = (score, dx, dy, matched, worst, cells)
    return (bbox,) + best[1:]


def check_quad(data, width, top, spec, expected, tolerance):
    """Return (bbox, dx, dy, matched, worst) or None when missing."""
    result = sample_quad(data, width, top, spec, expected, tolerance)
    if result is None:
        return None
    bbox, dx, dy, matched, _worst, _cells = result
    return bbox, dx, dy, matched, _worst


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="check_tex_formats.py",
        description="Validate a multi-format texture frame screenshot.")
    parser.add_argument("ppm", help="input P6 PPM file")
    parser.add_argument("--tolerance", type=int, default=TOLERANCE,
                        help="per-channel RGB tolerance (default 28)")
    parser.add_argument("--emulator-expected", action="store_true",
                        help="accepted for CLI compatibility; no effect")
    args = parser.parse_args(argv)

    width, height, data = parse_ppm(args.ppm)
    if width != 400 or height not in (240, 480):
        print("FAIL: expected a 400x240 or 400x480 PPM, got %dx%d"
              % (width, height), file=sys.stderr)
        return 2
    top = 240

    all_ok = True
    for index, spec in enumerate(gen.QUAD_SPECS):
        expected = gen.expected_pixels(spec)
        fmt = spec["format"]
        label = "quad %d fmt=%d %s %dx%d" % (
            index, fmt, gen.FORMAT_NAMES[fmt], spec["width"], spec["height"])
        result = check_quad(data, width, top, spec, expected, args.tolerance)
        if result is None:
            print("%s MISSING: no non-clear pixels in cell %s FAIL"
                  % (label, spec["cell"]))
            all_ok = False
            continue
        bbox, dx, dy, matched, worst = result
        total = spec["width"] * spec["height"]
        ok = matched == total
        print("%s cells=%d/%d worst=%d shift=(%d,%d) bbox=%s %s"
              % (label, matched, total, worst, dx, dy, bbox,
                 "OK" if ok else "FAIL"))
        if not ok:
            all_ok = False

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
