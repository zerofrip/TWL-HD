#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the hardware 400x240 top-screen rows of the EQ-4o fixtures.

The two committed fixtures are the owner's 400x240 top-left crops of a real
New 3DS running ``arm11/twlhd-arm11.3dsx`` (frame A) and the same frame with
every vertex y moved +3 DS px (frame B).  The arm11 render is 400x240, so the
crop maps 1:1 onto our render and a capture row is a render row.

Inputs
------
One PNG of exactly 400x240 (PIL required).  ``--column N`` may be repeated
(default 100); ``--dy D`` shifts the nominal DS geometry by D rows (0 for
frame A, 3 for frame B).

White test
----------
A pixel is white when its mean ``(R+G+B)/3`` is at least 128 (8-bit levels),
the same rule as ``tools/ds_edge_test/measure_rows.py``.  The clear colour of
these captures is ``(16,16,24)``, far below the threshold.

Edge table
----------
The six horizontal edges are the EQ-4n §3.3 geometry: DS ``y_ds + dy`` maps to
the capture row ``1.25 * y_ds`` under the EQ-4n mapping.  For each edge the
tool reports ``nominal = 1.25 * y_ds``, ``ceil(nominal)``, the measured row,
and a classification: ``ceil`` when the measurement equals the ceiling,
``ceil-1`` / ``ceil+1`` when one capture row is lost or added, ``nominal`` when
a whole-DS-row geometry lands on its exact integer position, else ``other``.

The measured row is the *actual state transition*, not merely the first row in
the target state.  The tool scans ``ceil(nominal) - 3 .. ceil(nominal) + 3``
(clamped to ``0..239``) in ascending order and takes the first row that changes
state: for a ``*.top`` edge the first white row whose row above is dark; for a
``*.bottom`` edge the first dark row whose row above is white.  A run interior
never matches because its state equals the row above, so an edge that sits one
or more rows early is no longer reported as ``ceil``.

Border conventions: row 0 has no row above, so row 0 counts as a transition
when it is already in the target state (white for ``*.top``, dark for
``*.bottom``); a band touching the image top is unobservable above row 0.  If
no transition is found inside the clamped window (e.g. white down to row 239
for a ``*.bottom`` edge, or a shift larger than three rows) the measured row is
``none`` and the classification is ``other``.

Output is deterministic ``key=value`` lines.  Exit 0 on success, 2 on usage or
input errors.
"""

import math
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

WIDTH = 400
HEIGHT = 240
WHITE_THRESHOLD = 128.0
DEFAULT_COLUMN = 100
ROW_PROBE = 120
EDGE_SEARCH_WINDOW = 3
# (name, DS y of the edge, side) from docs/reports/eq4n-arm11-row-shift.md §3.3.
EDGES = (
    ("top_band.bottom", 16, "bottom"),
    ("sq1.top", 32, "top"),
    ("sq1.bottom", 64, "bottom"),
    ("sq3.top", 128, "top"),
    ("sq3.bottom", 160, "bottom"),
    ("bottom_band.top", 176, "top"),
)


def luminance(pixel):
    return (pixel[0] + pixel[1] + pixel[2]) / 3.0


def is_white(pixel):
    return luminance(pixel) >= WHITE_THRESHOLD


def white_runs(flags):
    """Contiguous True runs as (first, last) index pairs."""
    runs = []
    start = None
    for index, flag in enumerate(flags):
        if flag and start is None:
            start = index
        elif not flag and start is not None:
            runs.append((start, index - 1))
            start = None
    if start is not None:
        runs.append((start, len(flags) - 1))
    return runs


def format_runs(runs):
    return ",".join("%d-%d" % run for run in runs) if runs else "none"


def format_rgb(pixel):
    return "%d,%d,%d" % (pixel[0], pixel[1], pixel[2])


def classify(measured, nominal):
    """Classify a measured row against the nominal edge position."""
    ceiling = int(math.ceil(nominal))
    if measured is None:
        return "other"
    if measured == ceiling:
        return "ceil"
    if measured == ceiling + 1:
        return "ceil+1"
    if measured == ceiling - 1:
        return "ceil-1"
    if float(nominal).is_integer() and measured == int(nominal):
        return "nominal"
    return "other"


def edge_rows(pixels, column, dy):
    rows = []
    for name, y_base, side in EDGES:
        y_ds = y_base + dy
        nominal = 1.25 * y_ds
        ceiling = int(math.ceil(nominal))
        measured = None
        first = max(0, ceiling - EDGE_SEARCH_WINDOW)
        last = min(HEIGHT - 1, ceiling + EDGE_SEARCH_WINDOW)
        for row in range(first, last + 1):
            white = is_white(pixels[column, row])
            # Transition rule: row 0 has no row above, so it is a transition
            # when already in the target state (border convention).
            above_white = (row > 0 and is_white(pixels[column, row - 1]))
            if side == "top":
                hit = white and (row == 0 or not above_white)
            else:
                hit = not white and (row == 0 or above_white)
            if hit:
                measured = row
                break
        rows.append((name, y_ds, nominal, ceiling, measured,
                     classify(measured, nominal)))
    return rows


def read_image(path):
    if Image is None:
        raise ValueError("PIL is not available; cannot read PNG fixtures")
    try:
        image = Image.open(path).convert("RGB")
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read image: %s" % exc)
    if image.size != (WIDTH, HEIGHT):
        raise ValueError("image must be %dx%d, got %dx%d"
                         % (WIDTH, HEIGHT, image.size[0], image.size[1]))
    return image.load()


def print_column(pixels, column, dy):
    flags = [is_white(pixels[column, row]) for row in range(HEIGHT)]
    runs = white_runs(flags)
    print("column=%d" % column)
    print("white_runs=%s" % format_runs(runs))
    for row in (0, 1, 2, HEIGHT - 1):
        print("row%d_white=%d" % (row, int(flags[row])))
        print("row%d_rgb=%s" % (row, format_rgb(pixels[column, row])))
    shifted = sum(1 for x in range(WIDTH - 1)
                  if pixels[x, 0] == pixels[x + 1, HEIGHT - 1])
    same = sum(1 for x in range(WIDTH)
               if pixels[x, 0] == pixels[x, HEIGHT - 1])
    print("row0_eq_row239=%d/%d" % (same, WIDTH))
    print("row0_eq_row239_shifted=%d/%d" % (shifted, WIDTH - 1))
    print("row0_wrap=%d" % int(shifted == WIDTH - 1))
    for name, y_ds, nominal, ceiling, measured, match in \
            edge_rows(pixels, column, dy):
        print("edge=%s y_ds=%d nominal=%.2f ceil=%d measured=%s match=%s"
              % (name, y_ds, nominal, ceiling,
                 "none" if measured is None else "%d" % measured, match))


def print_row_probe(pixels):
    flags = [is_white(pixels[x, ROW_PROBE]) for x in range(WIDTH)]
    runs = white_runs(flags)
    first = runs[0][0] if runs else None
    last = runs[-1][1] if runs else None
    print("row120_first_white_col=%s"
          % ("none" if first is None else "%d" % first))
    print("row120_last_white_col=%s"
          % ("none" if last is None else "%d" % last))
    for column in (39, 40, 359, 360):
        print("row120_col%d_rgb=%s"
              % (column, format_rgb(pixels[column, ROW_PROBE])))


def usage():
    sys.stderr.write("usage: measure_rows.py [--column N]... [--dy D] "
                     "<400x240.png>\n")


def main(argv):
    columns = []
    dy = 0
    positional = []
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--column":
            if not args:
                sys.stderr.write("measure_rows: --column needs a value\n")
                return 2
            try:
                columns.append(int(args.pop(0)))
            except ValueError:
                sys.stderr.write("measure_rows: --column must be an integer\n")
                return 2
        elif arg == "--dy":
            if not args:
                sys.stderr.write("measure_rows: --dy needs a value\n")
                return 2
            try:
                dy = int(args.pop(0))
            except ValueError:
                sys.stderr.write("measure_rows: --dy must be an integer\n")
                return 2
        elif arg.startswith("--"):
            sys.stderr.write("measure_rows: unknown option %s\n" % arg)
            return 2
        else:
            positional.append(arg)
    if len(positional) != 1:
        usage()
        return 2
    if not columns:
        columns = [DEFAULT_COLUMN]
    for column in columns:
        if not 0 <= column < WIDTH:
            sys.stderr.write("measure_rows: column %d outside 0..%d\n"
                             % (column, WIDTH - 1))
            return 2

    try:
        pixels = read_image(positional[0])
    except ValueError as exc:
        sys.stderr.write("measure_rows: %s\n" % exc)
        return 2

    print("size=%dx%d" % (WIDTH, HEIGHT))
    print("white_threshold=%.1f" % WHITE_THRESHOLD)
    print("dy=%d" % dy)
    for column in columns:
        print_column(pixels, column, dy)
    print_row_probe(pixels)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
