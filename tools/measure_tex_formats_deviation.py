#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure per-cell colour deviation on a multi-format texture frame capture.

Reads the same P6 PPM the checker accepts (400x240, or 400x480 with the top
screen in the upper 240 rows) and reuses tools/check_tex_formats.py cell logic
(find_bbox / _cell_centre / _mean3, exported through sample_quad) and
tools/gen_tex_format_frames.py expectations, so the numbers describe exactly
what the checker samples.

Prints, per quad, every sampled cell (expected RGB, sampled RGB, per-channel
deviation), the quad worst case, the overall worst case and a histogram of the
deviations, plus the input sha256. It is a measurement tool, not a checker:
deviations never change the exit status (0 for a readable capture, 2 for an
unreadable one or a wrong capture size).

Usage:
    python3 tools/measure_tex_formats_deviation.py <ppm>
"""

import argparse
import hashlib
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from check_tex_shot import parse_ppm  # noqa: E402
import check_tex_formats as check  # noqa: E402
import gen_tex_format_frames as gen  # noqa: E402

# (low, high) deviation buckets; every possible value has a home.
BUCKETS = ((0, 0), (1, 2), (3, 4), (5, 8), (9, 16), (17, 32), (33, 64),
           (65, 255))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def histogram(rows):
    counts = [0] * len(BUCKETS)
    for row in rows:
        for index, (low, high) in enumerate(BUCKETS):
            if low <= row["deviation"] <= high:
                counts[index] += 1
                break
    return counts


def measure(path):
    width, height, data = parse_ppm(path)
    if width != 400 or height not in (240, 480):
        print("error: expected a 400x240 or 400x480 PPM, got %dx%d"
              % (width, height), file=sys.stderr)
        return 2
    top = 240

    print("input: %s" % path)
    print("sha256: %s" % sha256(path))
    print("size: %d bytes" % os.path.getsize(path))
    print("dimensions: %dx%d (top screen rows 0..%d)"
          % (width, height, top))

    all_cells = []
    overall = 0
    quad_worst = []
    for index, spec in enumerate(gen.QUAD_SPECS):
        expected = gen.expected_pixels(spec)
        label = "quad %d fmt=%d %s %dx%d" % (
            index, spec["format"], gen.FORMAT_NAMES[spec["format"]],
            spec["width"], spec["height"])
        result = check.sample_quad(data, width, top, spec, expected,
                                   check.TOLERANCE)
        if result is None:
            print("%s MISSING" % label)
            quad_worst.append(None)
            continue
        bbox, dx, dy, matched, worst, cells = result
        total = spec["width"] * spec["height"]
        print("%s bbox=%s shift=(%d,%d) cells=%d/%d worst=%d"
              % (label, bbox, dx, dy, matched, total, worst))
        for tx, ty, want, got, deviation in cells:
            print("  cell (%2d,%2d) expected=(%3d,%3d,%3d) "
                  "sampled=(%3d,%3d,%3d) deviation=%d"
                  % (tx, ty, want[0], want[1], want[2],
                     got[0], got[1], got[2], deviation))
        all_cells.extend(
            {"quad": index, "tx": tx, "deviation": deviation}
            for tx, ty, want, got, deviation in cells)
        quad_worst.append(worst)
        if worst > overall:
            overall = worst

    print("per-quad worst: %s" % " ".join(
        "MISSING" if worst is None else "quad%d=%d" % (index, worst)
        for index, worst in enumerate(quad_worst)))
    print("overall worst: %d over %d sampled cells" % (overall, len(all_cells)))
    counts = histogram(all_cells)
    print("histogram:")
    for (low, high), count in zip(BUCKETS, counts):
        label = str(low) if low == high else "%d-%d" % (low, high)
        print("  %-7s %d" % (label, count))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="measure_tex_formats_deviation.py",
        description="Measure per-cell colour deviation on a formats frame "
                    "capture.")
    parser.add_argument("ppm", help="input P6 PPM file")
    args = parser.parse_args(argv)
    return measure(args.ppm)


if __name__ == "__main__":
    sys.exit(main())
