#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the DS Gouraud probe (GRAD=1) from a top-screen P6 PPM.

Samples a 5x5 mean at ten interior barycentric points of each of the two
triangles of tools/hw/gen_gradcull_rom_data.py and compares them with the
barycentric blend of the three corner colours (5-bit level * 255 / 31).  The
clip-to-capture mapping is the one used by tools/hw/predict_lit.py
(sx = 200 + 160 x, sy = 119 - 120 y with x, y in clip units).

Exit codes: 0 all samples within --tol (default 12 per channel), 1 a sample is
outside, 2 malformed input.  Stdlib only.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
for path in (TOOLS, HERE):
    if path not in sys.path:
        sys.path.insert(0, path)

import gen_gradcull_rom_data as rom  # noqa: E402
from check_tex_shot import parse_ppm  # noqa: E402

PATCH_RADIUS = 2
DIVISIONS = 6


def to_capture(vertex):
    return (200.0 + 160.0 * vertex[0] / 4096.0,
            119.0 - 120.0 * vertex[1] / 4096.0)


def sample_points():
    """Interior barycentric weights (i, j, k) / DIVISIONS with i, j, k >= 1."""
    points = []
    for i in range(1, DIVISIONS - 1):
        for j in range(1, DIVISIONS - i):
            k = DIVISIONS - i - j
            if k >= 1:
                points.append((i / DIVISIONS, j / DIVISIONS, k / DIVISIONS))
    return points


def expected_colour(colours, weights):
    return tuple(sum(w * c[ch] for w, c in zip(weights, colours)) * 255.0 /
                 31.0 for ch in range(3))


def sample(data, width, height, cx, cy):
    if (cx - PATCH_RADIUS < 0 or cy - PATCH_RADIUS < 0 or
            cx + PATCH_RADIUS >= width or cy + PATCH_RADIUS >= height):
        raise ValueError("patch outside the image")
    total = [0, 0, 0]
    count = 0
    for y in range(cy - PATCH_RADIUS, cy + PATCH_RADIUS + 1):
        for x in range(cx - PATCH_RADIUS, cx + PATCH_RADIUS + 1):
            off = (y * width + x) * 3
            for c in range(3):
                total[c] += data[off + c]
            count += 1
    return tuple(t / count for t in total)


def measure(data, width, height):
    """Rows: (triangle, weights, measured RGB, expected RGB, max deviation)."""
    rows = []
    for index, tri in enumerate(rom.GRAD_TRIANGLES):
        corners = [to_capture(v) for v in tri["vertices"]]
        for weights in sample_points():
            cx = sum(w * p[0] for w, p in zip(weights, corners))
            cy = sum(w * p[1] for w, p in zip(weights, corners))
            got = sample(data, width, height, int(round(cx)), int(round(cy)))
            want = expected_colour(tri["colours"], weights)
            dev = max(abs(got[ch] - want[ch]) for ch in range(3))
            rows.append((index, weights, got, want, dev))
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(prog="measure_grad.py",
                                     description=__doc__.split("\n")[0])
    parser.add_argument("ppm", help="400x240 (or 400x480) P6 PPM")
    parser.add_argument("--tol", type=int, default=12)
    args = parser.parse_args(argv)
    try:
        width, height, data = parse_ppm(args.ppm)
        if width != 400 or height < 240:
            raise ValueError("expected a 400x240 or 400x480 PPM, got %dx%d"
                             % (width, height))
        rows = measure(data, width, 240)
    except (ValueError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    print("tri weights(i,j,k)/%d  measured          expected          dev"
          % DIVISIONS)
    worst = 0.0
    for index, weights, got, want, dev in rows:
        print("%3d %d,%d,%d             (%3.0f,%3.0f,%3.0f)    "
              "(%3.0f,%3.0f,%3.0f)    %4.1f"
              % ((index,) + tuple(int(round(w * DIVISIONS)) for w in weights) +
                 got + want + (dev,)))
        worst = max(worst, dev)
    print("max deviation: %.1f (tolerance %d)" % (worst, args.tol))
    ok = worst <= args.tol
    print("result=%s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
