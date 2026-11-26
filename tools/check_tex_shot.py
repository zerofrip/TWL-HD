#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate a 5B-2e-2 textured-renderer screenshot.

Reads a P6 PPM (stdlib only): either a 400x240 top-screen-only capture (NTR
hardware) or a 400x480 image whose upper 400x240 rows are the top screen.
Samples four fixed rectangles on the top screen, and checks that each region
contains the expected solid colour (control frame) or exactly two colours
(textured frame: the expected base colour plus white).

For textured frames it also validates the checker STRUCTURE: it measures the
cell period along x and y inside each region, compares it with the period
predicted from the measured quad size and the known texture size, checks that
the non-square 16x8 region has a different x/y cell ratio than the square
regions, and compares the cell patterns of the two 8x8 regions (UR and LL,
which share a texture) cell by cell, tolerating a 1-px bounding-box shift.

--snap replaces every top-screen pixel with the nearest colour of a fixed
palette (squared RGB distance, ties to the earlier entry) before any check, so
JPEG captures from real hardware can be validated by the same checks. Palette:

    (0, 255, 0)      UL green
    (255, 0, 0)      UR/LL red
    (0, 0, 255)      LR blue
    (255, 255, 255)  checker white
    (16, 16, 24)     clear colour
    (132, 132, 132)  control grey (before 128 so hardware grey wins ties)
    (128, 128, 128)  control grey
    (0, 0, 0)        black

Without --snap nothing is changed.

Usage:
    python3 tools/check_tex_shot.py <ppm> [--frame tex|ctl] [--tolerance N] [--snap]

Exits 0 when every region matches, non-zero otherwise.
"""

import argparse
import sys

TOLERANCE = 40
STRUCTURE_TOLERANCE_FRAC = 0.25

# Screen rectangles for the four quads produced by tools/gen_tex_frames.py.
# Coordinates are in the 400x240 top screen, x left-to-right, y top-to-bottom.
# These were tuned against the real Azahar screenshots so that every rectangle
# lies fully inside one quad and never samples the background clear colour.
# Each tuple also records the texture size in texels and the expected base
# colour for the textured frame.
REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0), (0, 255, 0), (16, 8)),   # green
    ("UR", 270, 340, 75, 110, (255, 0, 0), (128, 128, 128), (8, 8)),  # tex red / ctl grey
    ("LL", 70, 170, 150, 185, (255, 0, 0), (255, 0, 0), (8, 8)),   # red
    ("LR", 270, 340, 150, 185, (0, 0, 255), (0, 0, 255), (16, 16)),  # blue
]

WHITE = (255, 255, 255)
# OLD-build clear colour for this synthetic/old-capture validator; kept
# self-consistent rather than flipped for EQ-4z.
CLEAR = (16, 16, 24)

# Fixed palette used by --snap. Order matters: nearest-colour ties go to the
# earlier entry (so 130 grey snaps to 132, not 128).
SNAP_PALETTE = [
    (0, 255, 0),
    (255, 0, 0),
    (0, 0, 255),
    (255, 255, 255),
    CLEAR,
    (132, 132, 132),
    (128, 128, 128),
    (0, 0, 0),
]


def snap_pixels(data, width, top):
    """Replace every top-screen pixel by its nearest palette colour."""
    for y in range(top):
        for x in range(width):
            off = (y * width + x) * 3
            pixel = (data[off], data[off + 1], data[off + 2])
            best = min(SNAP_PALETTE,
                       key=lambda c: sum((pixel[i] - c[i]) ** 2
                                         for i in range(3)))
            data[off], data[off + 1], data[off + 2] = best


def parse_ppm(path):
    """Parse a binary P6 PPM. Returns (width, height, bytearray RGB data)."""
    with open(path, "rb") as f:
        header = f.readline()
        if header.strip() != b"P6":
            raise ValueError(f"{path}: expected P6 PPM")
        while True:
            line = f.readline()
            if not line:
                raise ValueError(f"{path}: truncated header")
            line = line.split(b"#", 1)[0].strip()
            if line:
                break
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"{path}: bad dimension line")
        width, height = int(parts[0]), int(parts[1])
        line = f.readline()
        if not line:
            raise ValueError(f"{path}: missing maxval")
        maxval = int(line.split(b"#", 1)[0].strip())
        if maxval != 255:
            raise ValueError(f"{path}: unsupported maxval {maxval}")
        data = bytearray(f.read())
    expected = width * height * 3
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, got {len(data)}")
    return width, height, data


def pixel_at(data, width, x, y):
    off = (y * width + x) * 3
    return (data[off], data[off + 1], data[off + 2])


def within_tolerance(a, b, tolerance):
    return all(abs(int(a[i]) - int(b[i])) <= tolerance for i in range(3))


def add_colour(colours, colour, tolerance):
    """Add colour to the distinct list if not within tolerance of an existing."""
    for existing in colours:
        if within_tolerance(existing, colour, tolerance):
            return
    colours.append(colour)


def check_region_colours(data, width, name, x0, x1, y0, y1,
                         expected_tex, expected_ctl, frame, tolerance):
    colours = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            add_colour(colours, pixel_at(data, width, x, y), tolerance)

    if frame == "tex":
        expected = [expected_tex, WHITE]
        expected_count = 2
    else:
        expected = [expected_ctl]
        expected_count = 1

    ok = True
    if len(colours) != expected_count:
        ok = False
    else:
        for exp in expected:
            if not any(within_tolerance(c, exp, tolerance) for c in colours):
                ok = False
                break

    colour_text = " ".join(str(c) for c in colours)
    status = "OK" if ok else "FAIL"
    print(f"{name} colour {status}: {len(colours)} distinct colours: {colour_text}")
    return ok


def find_quad_bounds(data, width, height, cx, cy, bg, tolerance):
    """Scan from (cx, cy) to find background pixels on all four sides.

    Returns (x_left, x_right, y_top, y_bottom) where the quad pixels are
    strictly inside those bounds.
    """
    x_left = cx
    while x_left > 0 and not within_tolerance(pixel_at(data, width, x_left, cy), bg, tolerance):
        x_left -= 1

    x_right = cx
    while x_right < width - 1 and not within_tolerance(pixel_at(data, width, x_right, cy), bg, tolerance):
        x_right += 1

    y_top = cy
    while y_top > 0 and not within_tolerance(pixel_at(data, width, cx, y_top), bg, tolerance):
        y_top -= 1

    y_bottom = cy
    while y_bottom < height - 1 and not within_tolerance(pixel_at(data, width, cx, y_bottom), bg, tolerance):
        y_bottom += 1

    return x_left + 1, x_right - 1, y_top + 1, y_bottom - 1


def measure_period_x(data, width, qx0, qx1, y, tolerance):
    """Measure the average distance between colour boundaries on one row.

    The quad edges are treated as implicit boundaries so that a region with
    only one internal transition still yields a period.
    """
    if qx1 <= qx0:
        return None
    boundaries = [qx0]
    prev = pixel_at(data, width, qx0, y)
    for x in range(qx0 + 1, qx1 + 1):
        cur = pixel_at(data, width, x, y)
        if not within_tolerance(cur, prev, tolerance):
            boundaries.append(x)
            prev = cur
    boundaries.append(qx1)
    if len(boundaries) < 3:
        return None
    intervals = [boundaries[i + 1] - boundaries[i]
                 for i in range(len(boundaries) - 1)]
    return sum(intervals) / len(intervals)


def measure_period_y(data, width, x, qy0, qy1, tolerance):
    """Measure the average distance between colour boundaries on one column."""
    if qy1 <= qy0:
        return None
    boundaries = [qy0]
    prev = pixel_at(data, width, x, qy0)
    for y in range(qy0 + 1, qy1 + 1):
        cur = pixel_at(data, width, x, y)
        if not within_tolerance(cur, prev, tolerance):
            boundaries.append(y)
            prev = cur
    boundaries.append(qy1)
    if len(boundaries) < 3:
        return None
    intervals = [boundaries[i + 1] - boundaries[i]
                 for i in range(len(boundaries) - 1)]
    return sum(intervals) / len(intervals)


def _mean5(data, width, top, x, y):
    """5x5 mean centred at (x,y), clipped to the image."""
    total = [0, 0, 0]
    count = 0
    for py in range(max(0, y - 2), min(top, y + 3)):
        for px in range(max(0, x - 2), min(width, x + 3)):
            pixel = pixel_at(data, width, px, py)
            for channel in range(3):
                total[channel] += pixel[channel]
            count += 1
    return tuple(int(round(total[channel] / count)) for channel in range(3))


def check_region_structure(data, width, height, name, x0, x1, y0, y1,
                           expected_colour, tex_size, tolerance):
    """Validate the checker cell period and phase for one textured region."""
    cx = (x0 + x1) // 2
    cy = (y0 + y1) // 2
    qx0, qx1, qy0, qy1 = find_quad_bounds(data, width, height, cx, cy, CLEAR, tolerance)

    quad_w = qx1 - qx0 + 1
    quad_h = qy1 - qy0 + 1
    if quad_w <= 0 or quad_h <= 0:
        print(f"{name} structure FAIL: could not measure quad extents")
        return None

    tex_w, tex_h = tex_size
    cells_x = tex_w // 4
    cells_y = tex_h // 4
    predicted_px_x = quad_w / cells_x
    predicted_px_y = quad_h / cells_y

    # Sample the middle row/column of the measured quad to get the cell
    # period.  The quad edges are treated as implicit boundaries so that a
    # region with only one internal transition still yields a period.
    measured_px_x = measure_period_x(data, width, qx0, qx1, cy, tolerance)
    measured_px_y = measure_period_y(data, width, cx, qy0, qy1, tolerance)

    if measured_px_x is None or measured_px_y is None:
        print(f"{name} structure FAIL: could not measure period")
        return None

    ok_x = abs(measured_px_x - predicted_px_x) <= max(
        1, STRUCTURE_TOLERANCE_FRAC * predicted_px_x)
    ok_y = abs(measured_px_y - predicted_px_y) <= max(
        1, STRUCTURE_TOLERANCE_FRAC * predicted_px_y)

    status = "OK" if (ok_x and ok_y) else "FAIL"
    print(f"{name} structure {status}: "
          f"quad={quad_w}x{quad_h} "
          f"cells={cells_x}x{cells_y} "
          f"predicted_period={predicted_px_x:.1f}x{predicted_px_y:.1f} "
          f"measured_period={measured_px_x:.1f}x{measured_px_y:.1f}")

    if not (ok_x and ok_y):
        return None

    # Phase sample: the top-left checker cell should be the base colour. The
    # check itself is performed in check_structure() after ratio/reuse so that
    # a flipped render shows period/ratio/reuse are otherwise valid.
    cell_w = quad_w / cells_x
    cell_h = quad_h / cells_y
    phase_x = int(qx0 + 0.5 * cell_w)
    phase_y = int(qy0 + 0.5 * cell_h)
    phase_got = _mean5(data, width, height, phase_x, phase_y)

    return {
        "measured_px": (measured_px_x, measured_px_y),
        "predicted_px": (predicted_px_x, predicted_px_y),
        "cells": (cells_x, cells_y),
        "phase": phase_got,
        "expected_colour": expected_colour,
        "bbox": (qx0, qx1, qy0, qy1),
    }


def _is_base_cell(data, width, height, x, y, base):
    """3x3 majority vote: True when the sampled cell is the base colour."""
    base_votes = 0
    for py in range(max(0, y - 1), min(height, y + 2)):
        for px in range(max(0, x - 1), min(width, x + 2)):
            pixel = pixel_at(data, width, px, py)
            base_d = sum((pixel[i] - base[i]) ** 2 for i in range(3))
            white_d = sum((pixel[i] - WHITE[i]) ** 2 for i in range(3))
            if base_d <= white_d:
                base_votes += 1
    return base_votes >= 5


def _cell_pattern(data, width, height, bbox, cells, base, dx=0, dy=0):
    """Cell classes (True = base colour) sampled at texel cell centres."""
    qx0, qx1, qy0, qy1 = bbox
    quad_w = qx1 - qx0 + 1
    quad_h = qy1 - qy0 + 1
    cells_x, cells_y = cells
    pattern = []
    for j in range(cells_y):
        y = int(qy0 + (j + 0.5) * quad_h / cells_y) + dy
        for i in range(cells_x):
            x = int(qx0 + (i + 0.5) * quad_w / cells_x) + dx
            pattern.append(_is_base_cell(data, width, height, x, y, base))
    return pattern


def compare_reuse(data, width, height, ur, ll):
    """Compare UR and LL cell-by-cell, searching +-1 px for bbox jitter.

    Returns (matched_cells, total_cells, (dx, dy)) for the best shift.
    """
    if ur["cells"] != ll["cells"]:
        return 0, 0, (0, 0)
    ll_pattern = _cell_pattern(data, width, height, ll["bbox"], ll["cells"],
                               ll["expected_colour"])
    best = None
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            ur_pattern = _cell_pattern(data, width, height, ur["bbox"],
                                       ur["cells"], ur["expected_colour"],
                                       dx, dy)
            matched = sum(1 for got, want in zip(ur_pattern, ll_pattern)
                          if got == want)
            score = (-matched, abs(dx) + abs(dy))
            if best is None or score < best[0]:
                best = (score, matched, (dx, dy))
    return best[1], len(ll_pattern), best[2]


def check_structure(data, width, height, tolerance):
    """Run the structural checker on all four textured regions."""
    results = {}
    all_ok = True
    for name, x0, x1, y0, y1, expected_tex, _expected_ctl, tex_size in REGIONS:
        info = check_region_structure(data, width, height, name, x0, x1, y0, y1,
                                      expected_tex, tex_size, tolerance)
        if info is None:
            all_ok = False
        else:
            results[name] = info

    if not all_ok or len(results) != len(REGIONS):
        return False

    # Ratio check: the 16x8 region (UL) must have a clearly different x/y cell
    # ratio from the square regions.
    square_ratio_sum = 0.0
    square_count = 0
    for name, *_rest, tex_size in REGIONS:
        if tex_size[0] == tex_size[1]:
            px_x, px_y = results[name]["measured_px"]
            if px_y != 0:
                square_ratio_sum += px_x / px_y
                square_count += 1
    if square_count == 0:
        print("structure FAIL: no square regions for ratio baseline")
        return False
    square_ratio = square_ratio_sum / square_count

    ul_px_x, ul_px_y = results["UL"]["measured_px"]
    if ul_px_y == 0:
        print("structure FAIL: UL vertical period is zero")
        return False
    ul_ratio = ul_px_x / ul_px_y

    ratio_diff = abs(ul_ratio - square_ratio)
    ratio_ok = ratio_diff > 0.15
    status = "OK" if ratio_ok else "FAIL"
    print(f"UL ratio {status}: UL={ul_ratio:.2f} square_avg={square_ratio:.2f} diff={ratio_diff:.2f}")
    if not ratio_ok:
        return False

    # Reuse check: UR and LL share the same 8x8 texture, so their cell pattern
    # must match. Compare cell centres (3x3 majority) and search a +-1 px
    # sample shift to absorb bounding-box jitter from JPEG edges.
    ur = results["UR"]
    ll = results["LL"]
    period_ok = (abs(ur["measured_px"][0] - ll["measured_px"][0]) <=
                 max(1, STRUCTURE_TOLERANCE_FRAC * max(ur["measured_px"][0], 1)) and
                 abs(ur["measured_px"][1] - ll["measured_px"][1]) <=
                 max(1, STRUCTURE_TOLERANCE_FRAC * max(ur["measured_px"][1], 1)))
    matched, total, shift = compare_reuse(data, width, height, ur, ll)
    reuse_ok = period_ok and total > 0 and matched == total
    status = "OK" if reuse_ok else "FAIL"
    print(f"UR==LL reuse {status}: period={period_ok} "
          f"cells={matched}/{total} shift=({shift[0]},{shift[1]})")
    if not reuse_ok:
        return False

    # Phase check: the top-left checker cell of every region must be the base
    # colour. A vertically mirrored render puts white there instead.
    all_phase_ok = True
    for name, *_rest, tex_size in REGIONS:
        expected = results[name]["expected_colour"]
        got = results[name]["phase"]
        phase_ok = within_tolerance(got, expected, tolerance)
        status = "OK" if phase_ok else "FAIL"
        print(f"{name} phase {status}: got={got} want={expected}")
        if not phase_ok:
            all_phase_ok = False

    return all_phase_ok


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Validate a 5B-2e-2 textured-renderer emulator screenshot")
    parser.add_argument("ppm", help="input P6 PPM file")
    parser.add_argument(
        "--frame", choices=["tex", "ctl"], default="tex",
        help="expected frame type (tex = checker, ctl = solid)")
    parser.add_argument(
        "--tolerance", type=int, default=TOLERANCE,
        help="per-channel RGB tolerance")
    parser.add_argument(
        "--snap", action="store_true",
        help="snap every top-screen pixel to the nearest palette colour "
             "(for JPEG hardware captures)")
    args = parser.parse_args(argv)

    width, height, data = parse_ppm(args.ppm)
    if width != 400 or height not in (240, 480):
        print(f"FAIL: expected a 400x240 or 400x480 PPM, got {width}x{height}",
              file=sys.stderr)
        return 2
    top = 240 if height == 480 else height

    if args.snap:
        snap_pixels(data, width, top)

    all_ok = True
    for name, x0, x1, y0, y1, expected_tex, expected_ctl, _tex_size in REGIONS:
        if not check_region_colours(data, width, name, x0, x1, y0, y1,
                                    expected_tex, expected_ctl, args.frame,
                                    args.tolerance):
            all_ok = False

    if args.frame == "tex" and all_ok:
        if not check_structure(data, width, height, args.tolerance):
            all_ok = False

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
