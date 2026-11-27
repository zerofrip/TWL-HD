#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the DS culling probe (CULL=1) from a top-screen P6 PPM.

For each of the eight white quads of tools/hw/gen_gradcull_rom_data.py reports
the fraction of bright pixels (all channels > 128) inside the quad rectangle
shrunk by 4 px, and classifies it DRAWN (>= 0.9), CULLED (<= 0.02) or MIXED.
Prints a table with the winding and POLYGON_ATTR bits 6-7 of every quad.
The renderer's interpreter ignores those attribute bits, so it draws all eight.

Exit codes: 0 readable input (the classification is the result), 2 malformed.
Stdlib only.
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
import gen_tex_format_frames as gen  # noqa: E402
from check_tex_shot import parse_ppm  # noqa: E402

SHRINK = 4


def bright_fraction(data, width, rect):
    x0, y0, x1, y1 = rect
    total = 0
    bright = 0
    for y in range(y0 + SHRINK, y1 - SHRINK):
        for x in range(x0 + SHRINK, x1 - SHRINK):
            off = (y * width + x) * 3
            total += 1
            if data[off] > 128 and data[off + 1] > 128 and \
                    data[off + 2] > 128:
                bright += 1
    return bright / total if total else 0.0


def classify(fraction):
    if fraction >= 0.9:
        return "DRAWN"
    if fraction <= 0.02:
        return "CULLED"
    return "MIXED"


def measure(data, width):
    rows = []
    for index, (winding, bits) in enumerate(rom.CULL_CASES):
        fraction = bright_fraction(data, width,
                                   gen.QUAD_SPECS[index]["rect"])
        rows.append((index, winding, bits, fraction, classify(fraction)))
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(prog="measure_cull.py",
                                     description=__doc__.split("\n")[0])
    parser.add_argument("ppm", help="400x240 (or 400x480) P6 PPM")
    args = parser.parse_args(argv)
    try:
        width, height, data = parse_ppm(args.ppm)
        if width != 400 or height < 240:
            raise ValueError("expected a 400x240 or 400x480 PPM, got %dx%d"
                             % (width, height))
        rows = measure(data, width)
    except (ValueError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    print("quad winding      attr bits 6-7  bright   result")
    for index, winding, bits, fraction, state in rows:
        print("%4d %-13s %13d  %6.3f   %s" % (index, winding, bits, fraction,
                                              state))
    return 0


if __name__ == "__main__":
    sys.exit(main())
