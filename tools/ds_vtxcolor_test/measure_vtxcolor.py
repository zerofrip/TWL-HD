#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the DS hardware's default current vertex colour from a capture PNG.

Reads the top-left 400x240 region of a cc3dsfs capture (the EQ-4b layout),
maps DS pixel coordinates to capture pixels with capture_mapping.json (the
EQ-4c measured mapping), takes the median RGB of the centre 16x16 DS-pixel
patch of quads A..E and of a clear background spot, and classifies quad A
(no glColor before it in the ROM) against the explicit reference quads.

    python3 measure_vtxcolor.py <capture.png>

Report: one `key=value` line per key.  Exit 0 when the geometry (background and
all five quads) is found, 1 when it is not, 2 for usage/PIL/unreadable input.

Classification rule (tolerance TOLERANCE = 12 per channel, applied to the
measured medians, checked in this order):

    1. same_as_qB / same_as_qC / same_as_qD / same_as_qE  (|qA - qX| <= 12)
    2. black  (qA within 12 of (0,0,0))
    3. white  (qA within 12 of (255,255,255))
    4. grey   (max(qA) - min(qA) <= 12)
    5. other

The relative classes use the measured reference quads because the capture
colour path is not identity (EQ-4c: DS white shows (255,251,255), black shows
0..8).  The absolute black/white thresholds necessarily use ideal 8-bit values.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
QUADS_JSON = os.path.join(HERE, "quads.json")
MAPPING_JSON = os.path.join(HERE, "capture_mapping.json")

REGION_WIDTH = 400
REGION_HEIGHT = 240
PATCH = 16          # centre DS-pixel patch, per axis
TOLERANCE = 12      # per-channel colour tolerance
FOUND_DELTA = 16    # quad/background channel delta needed to count as found
BG_DS = (128, 8)    # clear spot: above every quad, inside the 256x192 screen

QUAD_NAMES = ("qA", "qB", "qC", "qD", "qE")
REPORT_KEYS = (
    "geometry_found",
    "qA_rgb", "qB_rgb", "qC_rgb", "qD_rgb", "qE_rgb",
    "bg_rgb",
    "default_colour_class",
)


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def capture_xy(ds_x, ds_y, mapping):
    """EQ-4c mapping: DS pixel centre -> capture pixel (see module docstring)."""
    cx = mapping["offset_x"] + (ds_x + 0.5) * mapping["scale_x"] - 0.5
    cy = mapping["offset_y"] + (ds_y + 0.5) * mapping["scale_y"] - 0.5
    return int(round(cx)), int(round(cy))


def patch_box(ds_x, ds_y, mapping):
    """Inclusive capture rect covering the PATCH x PATCH DS-pixel patch."""
    half = PATCH // 2
    x0 = ds_x - half
    y0 = ds_y - half
    xs = [capture_xy(x, y0, mapping)[0] for x in range(x0, x0 + PATCH)]
    ys = [capture_xy(x0, y, mapping)[1] for y in range(y0, y0 + PATCH)]
    return min(xs), min(ys), max(xs), max(ys)


def quad_box(quad, mapping):
    return patch_box(quad["x"] + quad["w"] // 2,
                     quad["y"] + quad["h"] // 2, mapping)


def _median(values):
    ordered = sorted(values)
    n = len(ordered)
    mid = n // 2
    if n % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) // 2


def _in_region(box, width, height):
    x0, y0, x1, y1 = box
    return x0 >= 0 and y0 >= 0 and x1 < width and y1 < height


def _median_patch(pixels, width, height, box):
    x0, y0, x1, y1 = box
    x0 = max(0, x0)
    y0 = max(0, y0)
    x1 = min(width - 1, x1)
    y1 = min(height - 1, y1)
    if x0 > x1 or y0 > y1:
        return None
    rs, gs, bs = [], [], []
    for y in range(y0, y1 + 1):
        base = y * width
        for x in range(x0, x1 + 1):
            r, g, b = pixels[base + x]
            rs.append(r)
            gs.append(g)
            bs.append(b)
    return (_median(rs), _median(gs), _median(bs))


def _differs(a, b):
    return any(abs(x - y) >= FOUND_DELTA for x, y in zip(a, b))


def classify(qA, qB, qC, qD, qE, tolerance=TOLERANCE):
    def same(a, b):
        return all(abs(x - y) <= tolerance for x, y in zip(a, b))

    for name, other in (("same_as_qB", qB), ("same_as_qC", qC),
                        ("same_as_qD", qD), ("same_as_qE", qE)):
        if same(qA, other):
            return name
    if same(qA, (0, 0, 0)):
        return "black"
    if same(qA, (255, 255, 255)):
        return "white"
    if max(qA) - min(qA) <= tolerance:
        return "grey"
    return "other"


def measure(image, quads, mapping):
    """Measure a PIL image; returns (report dict, missing names list)."""
    region = image.convert("RGB").crop(
        (0, 0, min(REGION_WIDTH, image.width),
         min(REGION_HEIGHT, image.height)))
    width, height = region.size
    raw = region.tobytes()
    pixels = [(raw[i], raw[i + 1], raw[i + 2])
              for i in range(0, len(raw), 3)]

    bg_box = patch_box(BG_DS[0], BG_DS[1], mapping)
    bg = None
    if _in_region(bg_box, width, height):
        bg = _median_patch(pixels, width, height, bg_box)

    report = {"bg_rgb": bg or (0, 0, 0)}
    missing = []

    for quad in quads["quads"]:
        box = quad_box(quad, mapping)
        rgb = None
        if _in_region(box, width, height):
            rgb = _median_patch(pixels, width, height, box)
        report[quad["name"] + "_rgb"] = rgb or (0, 0, 0)
        if bg is None or rgb is None or not _differs(rgb, bg):
            missing.append(quad["name"])

    if bg is None:
        missing.insert(0, "background")

    report["geometry_found"] = 0 if missing else 1
    report["default_colour_class"] = classify(
        report["qA_rgb"], report["qB_rgb"], report["qC_rgb"],
        report["qD_rgb"], report["qE_rgb"])
    return report, missing


def _format(key, value):
    if key.endswith("_rgb"):
        return "%d,%d,%d" % value
    if key == "default_colour_class":
        return value
    return "%d" % value


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: measure_vtxcolor.py <capture.png>\n")
        return 2
    if Image is None:
        sys.stderr.write(
            "measure_vtxcolor: PIL is not installed (pip install Pillow)\n")
        return 2
    try:
        quads = load_json(QUADS_JSON)
        mapping = load_json(MAPPING_JSON)
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "measure_vtxcolor: cannot read quads/mapping json: %s\n" % exc)
        return 2
    try:
        image = Image.open(argv[1])
        image.load()
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "measure_vtxcolor: cannot read %s: %s\n" % (argv[1], exc))
        return 2

    report, missing = measure(image, quads, mapping)
    for key in REPORT_KEYS:
        print("%s=%s" % (key, _format(key, report[key])))

    if missing:
        sys.stderr.write(
            "measure_vtxcolor: geometry not found in top-left %dx%d region: "
            "%s\n" % (REGION_WIDTH, REGION_HEIGHT, " ".join(missing)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
