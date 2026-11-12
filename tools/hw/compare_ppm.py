#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare a top-screen P6 PPM against predict_lit.py expectations.

Samples a 9x9 patch at every predicted quad centre and at the background
patch, prints measured vs expected and per-channel deviation, and exits 0 only
when every channel is within --tol (default 12 levels).  Pure stdlib.

Exit codes:
  0  all patches within tolerance
  1  a patch is outside tolerance
  2  malformed/short PPM, malformed JSON, or a patch outside the image
"""

import argparse
import json
import statistics
import sys

PATCH_RADIUS = 4  # 9x9


class PPMError(Exception):
    """Malformed or unusable PPM."""


class PatchError(Exception):
    """A sampled patch does not fit inside the image."""


def _tokens(data):
    """Yield (token, end offset) header tokens, skipping comments."""
    pos = 0
    length = len(data)
    while True:
        while pos < length and data[pos:pos + 1].isspace():
            pos += 1
        if pos < length and data[pos:pos + 1] == b"#":
            while pos < length and data[pos:pos + 1] not in b"\r\n":
                pos += 1
            continue
        start = pos
        while (pos < length and not data[pos:pos + 1].isspace()
               and data[pos:pos + 1] != b"#"):
            pos += 1
        if start == pos:
            return
        yield data[start:pos], pos


def read_ppm(path):
    """Read a binary P6 PPM. Returns (width, height, pixel bytes)."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        raise PPMError("cannot read %s: %s" % (path, exc))

    tokens = _tokens(data)
    try:
        magic, _ = next(tokens)
        width_token, _ = next(tokens)
        height_token, _ = next(tokens)
        maxval_token, header_end = next(tokens)
    except StopIteration:
        raise PPMError("truncated PPM header")

    if magic != b"P6":
        raise PPMError("not a binary P6 PPM (magic %r)" % magic)
    try:
        width = int(width_token)
        height = int(height_token)
        maxval = int(maxval_token)
    except ValueError:
        raise PPMError("non-numeric PPM header field")
    if width <= 0 or height <= 0:
        raise PPMError("invalid PPM dimensions %dx%d" % (width, height))
    if maxval != 255:
        raise PPMError("only 8-bit PPMs are supported (maxval %d)" % maxval)

    # Exactly one whitespace byte separates maxval from the raster; accept a
    # CRLF pair because some writers emit one.
    offset = header_end
    if data[offset:offset + 2] == b"\r\n":
        offset += 2
    elif offset < len(data) and data[offset:offset + 1].isspace():
        offset += 1
    else:
        raise PPMError("missing whitespace after the PPM header")

    expected = width * height * 3
    pixels = data[offset:offset + expected]
    if len(pixels) < expected:
        raise PPMError("truncated pixel data: expected %d bytes, got %d"
                       % (expected, len(pixels)))
    return width, height, pixels


def sample(pixels, width, height, centre):
    """Mean RGB over a 9x9 patch centred (rounded) on `centre`."""
    cx = int(round(centre[0]))
    cy = int(round(centre[1]))
    if (cx - PATCH_RADIUS < 0 or cx + PATCH_RADIUS >= width or
            cy - PATCH_RADIUS < 0 or cy + PATCH_RADIUS >= height):
        raise PatchError(
            "patch at (%d,%d) lies outside the %dx%d image"
            % (cx, cy, width, height))

    channels = ([], [], [])
    for y in range(cy - PATCH_RADIUS, cy + PATCH_RADIUS + 1):
        row = y * width * 3
        for x in range(cx - PATCH_RADIUS, cx + PATCH_RADIUS + 1):
            base = row + x * 3
            for channel in range(3):
                channels[channel].append(pixels[base + channel])
    return tuple(int(round(statistics.fmean(values))) for values in channels)


def _load_expectation(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            document = json.load(handle)
    except OSError as exc:
        raise PPMError("cannot read %s: %s" % (path, exc))
    except ValueError as exc:
        raise PPMError("invalid JSON in %s: %s" % (path, exc))
    if not isinstance(document, dict) or not document.get("quads"):
        raise PPMError("prediction JSON has no quads")
    return document


def _deviation(expected, measured):
    return [abs(measured[i] - expected[i]) for i in range(3)]


def compare(ppm_path, json_path, tol=12.0):
    """Return (ok, table rows). Raises PPMError/PatchError."""
    width, height, pixels = read_ppm(ppm_path)
    document = _load_expectation(json_path)

    rows = []
    ok = True
    for entry in document["quads"]:
        expected = entry["rgba8"][:3]
        measured = sample(pixels, width, height, entry["pixel_centre"])
        deviation = _deviation(expected, measured)
        if max(deviation) > tol:
            ok = False
        rows.append({
            "name": "quad %d" % entry["index"],
            "measured": measured,
            "expected": tuple(expected),
            "deviation": deviation,
        })

    background = document.get("background")
    if background:
        expected = background["rgba8"][:3]
        measured = sample(pixels, width, height, background["pixel_centre"])
        deviation = _deviation(expected, measured)
        if max(deviation) > tol:
            ok = False
        rows.append({
            "name": "background",
            "measured": measured,
            "expected": tuple(expected),
            "deviation": deviation,
        })
    return ok, rows


def _print_table(rows, tol):
    print("%-11s %-15s %-15s %-11s" %
          ("patch", "measured", "expected", "dev R/G/B"))
    for row in rows:
        print("%-11s (%-3d,%-3d,%-3d) (%-3d,%-3d,%-3d) %d/%d/%d" % (
            row["name"],
            row["measured"][0], row["measured"][1], row["measured"][2],
            row["expected"][0], row["expected"][1], row["expected"][2],
            row["deviation"][0], row["deviation"][1], row["deviation"][2]))
    print("tolerance: %g level(s) per channel" % tol)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="compare_ppm.py",
        description="Compare a top-screen P6 PPM with predict_lit.py JSON.")
    parser.add_argument("ppm", help="top-screen PPM (400x240, P6)")
    parser.add_argument("json", help="prediction JSON from predict_lit.py")
    parser.add_argument("--tol", type=float, default=12.0,
                        help="per-channel tolerance (default: %(default)s)")
    args = parser.parse_args(argv)

    try:
        ok, rows = compare(args.ppm, args.json, args.tol)
    except (PPMError, PatchError, KeyError, TypeError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    _print_table(rows, args.tol)
    if ok:
        print("result=PASS")
        return 0
    print("result=FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
