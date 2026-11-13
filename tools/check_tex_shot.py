#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate a 5B-2e-2 textured-renderer emulator screenshot.

Reads a P6 PPM (stdlib only), samples four fixed rectangles on the top screen
(upper 400x240 of the 400x480 image), and checks that each region contains the
expected solid colour (control frame) or exactly two colours (textured frame:
the expected base colour plus white).

Usage:
    python3 tools/check_tex_shot.py <ppm> [--frame tex|ctl] [--tolerance N]

Exits 0 when every region matches, non-zero otherwise.
"""

import argparse
import sys

TOLERANCE = 40

# Screen rectangles for the four quads produced by tools/gen_tex_frames.py.
# Coordinates are in the 400x240 top screen, x left-to-right, y top-to-bottom.
# These were tuned against the real Azahar screenshots so that every rectangle
# lies fully inside one quad and never samples the background clear colour.
REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0), (0, 255, 0)),    # green
    ("UR", 270, 340, 75, 110, (255, 0, 0), (128, 128, 128)),  # tex red / ctl grey
    ("LL", 70, 170, 150, 185, (255, 0, 0), (255, 0, 0)),   # red
    ("LR", 270, 340, 150, 185, (0, 0, 255), (0, 0, 255)),  # blue
]

WHITE = (255, 255, 255)


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


def check_region(data, width, name, x0, x1, y0, y1,
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
    print(f"{name} {status}: {len(colours)} distinct colours: {colour_text}")
    return ok


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
    args = parser.parse_args(argv)

    width, height, data = parse_ppm(args.ppm)
    if width != 400 or height != 480:
        print(f"FAIL: expected 400x480 PPM, got {width}x{height}", file=sys.stderr)
        return 2

    all_ok = True
    for name, x0, x1, y0, y1, expected_tex, expected_ctl in REGIONS:
        if not check_region(data, width, name, x0, x1, y0, y1,
                            expected_tex, expected_ctl, args.frame,
                            args.tolerance):
            all_ok = False

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
