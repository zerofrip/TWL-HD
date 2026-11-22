#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the DS 256x192 top screen inside a cc3dsfs capture PNG.

Reads the top-left 400x240 region (the New 3DS capture layout recorded in
docs/reports/eq4b-cc3dsfs-ds-capture.md), finds the white frame and the four
corner colour squares of the ds-scale-test pattern, and prints a stable
`key=value` report: displayed bounding box, capture-px-per-DS-px scale, offset
and a 1-px-alternation contrast hint.

    python3 measure_scale.py <capture.png>

Exit status is non-zero with a one-line message when the frame or a corner
marker is missing. Requires PIL; the unit tests skip with a labelled reason
when PIL is absent.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
PATTERN_JSON = os.path.join(HERE, "pattern.json")

REGION_WIDTH = 400
REGION_HEIGHT = 240
TOLERANCE = 24

REPORT_KEYS = (
    "frame_found",
    "corner_red",
    "corner_green",
    "corner_blue",
    "corner_yellow",
    "bbox_x0",
    "bbox_y0",
    "bbox_x1",
    "bbox_y1",
    "scale_x",
    "scale_y",
    "offset_x",
    "offset_y",
    "strip_h_contrast",
    "strip_v_contrast",
)

CORNER_NAMES = (
    ("corner_red", "corner_tl"),
    ("corner_green", "corner_tr"),
    ("corner_blue", "corner_bl"),
    ("corner_yellow", "corner_br"),
)


def _near(pixel, rgb):
    """Match an 8-bit RGB marker colour with TOLERANCE per channel."""
    for got, want in zip(pixel, rgb):
        target = 255 if want >= 128 else 0
        if abs(got - target) > TOLERANCE:
            return False
    return True


def _white(pixel):
    return _near(pixel, (255, 255, 255))


def _element(pattern, name):
    for elem in pattern["elements"]:
        if elem["name"] == name:
            return elem
    raise KeyError("pattern.json has no element named %r" % name)


def _bbox(points):
    if not points:
        return None
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), min(ys), max(xs), max(ys)


def _border_white_ratio(pixels, width, height, box):
    x0, y0, x1, y1 = box
    total = 0
    white = 0

    def check(x, y):
        nonlocal total, white
        total += 1
        if _white(pixels[y * width + x]):
            white += 1

    for x in range(x0, x1 + 1):
        check(x, y0)
        check(x, y1)
    for y in range(y0 + 1, y1):
        check(x0, y)
        check(x1, y)
    return (white / total) if total else 0.0


def _corner_ok(box, frame, name):
    """Require the colour blob in the expected quadrant of the frame."""
    if box is None:
        return False
    x0, y0, x1, y1 = box
    if (x1 - x0 + 1) < 2 or (y1 - y0 + 1) < 2:
        return False
    fx0, fy0, fx1, fy1 = frame
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    left = cx <= (fx0 + fx1) / 2.0
    top = cy <= (fy0 + fy1) / 2.0
    return {
        "corner_tl": left and top,
        "corner_tr": (not left) and top,
        "corner_bl": left and (not top),
        "corner_br": (not left) and (not top),
    }[name]


def _sample(pixels, width, height, x, y):
    if x < 0:
        x = 0
    elif x >= width:
        x = width - 1
    if y < 0:
        y = 0
    elif y >= height:
        y = height - 1
    r, g, b = pixels[y * width + x]
    return (r + g + b) / 3.0


def measure(image, pattern):
    """Measure a PIL image; returns the report dict."""
    region = image.convert("RGB").crop(
        (0, 0, min(REGION_WIDTH, image.width),
         min(REGION_HEIGHT, image.height)))
    width, height = region.size
    raw = region.tobytes()
    pixels = [(raw[i], raw[i + 1], raw[i + 2])
              for i in range(0, len(raw), 3)]

    white_points = [(x, y) for y in range(height) for x in range(width)
                    if _white(pixels[y * width + x])]
    frame_box = _bbox(white_points)
    frame_found = (
        frame_box is not None
        and (frame_box[2] - frame_box[0] + 1) >= 64
        and (frame_box[3] - frame_box[1] + 1) >= 48
        and _border_white_ratio(pixels, width, height, frame_box) >= 0.5
    )

    result = {
        "frame_found": 1 if frame_found else 0,
        "bbox_x0": 0, "bbox_y0": 0, "bbox_x1": 0, "bbox_y1": 0,
        "scale_x": 0.0, "scale_y": 0.0, "offset_x": 0, "offset_y": 0,
        "strip_h_contrast": 0.0, "strip_v_contrast": 0.0,
    }

    for key, name in CORNER_NAMES:
        elem = _element(pattern, name)
        color = elem.get("colour", elem.get("color"))
        points = [(x, y) for y in range(height) for x in range(width)
                  if _near(pixels[y * width + x], color)]
        box = _bbox(points)
        result[key] = 1 if (frame_found and _corner_ok(
            box, frame_box, name)) else 0

    if not frame_found:
        return result, frame_box, [key for key, _ in CORNER_NAMES
                                   if not result[key]]

    x0, y0, x1, y1 = frame_box
    result["bbox_x0"] = x0
    result["bbox_y0"] = y0
    result["bbox_x1"] = x1
    result["bbox_y1"] = y1
    result["offset_x"] = x0
    result["offset_y"] = y0
    scale_x = (x1 - x0 + 1) / float(pattern["width"])
    scale_y = (y1 - y0 + 1) / float(pattern["height"])
    result["scale_x"] = scale_x
    result["scale_y"] = scale_y

    def capture_xy(ds_x, ds_y):
        cx = int(round(x0 + (ds_x + 0.5) * scale_x - 0.5))
        cy = int(round(y0 + (ds_y + 0.5) * scale_y - 0.5))
        return cx, cy

    def strip_contrast(elem, columns):
        period = elem.get("period", 2)
        phase = elem.get("phase", 0)
        whites = []
        blacks = []
        for y in range(elem["y"], elem["y"] + elem["h"]):
            for x in range(elem["x"], elem["x"] + elem["w"]):
                expected_white = (
                    ((x - elem["x"]) if columns else (y - elem["y"]))
                    % period == phase)
                cx, cy = capture_xy(x, y)
                value = _sample(pixels, width, height, cx, cy)
                (whites if expected_white else blacks).append(value)
        if not whites or not blacks:
            return 0.0
        return sum(whites) / len(whites) - sum(blacks) / len(blacks)

    result["strip_h_contrast"] = strip_contrast(
        _element(pattern, "hstrip"), columns=True)
    result["strip_v_contrast"] = strip_contrast(
        _element(pattern, "vstrip"), columns=False)

    missing = [key for key, _ in CORNER_NAMES if not result[key]]
    return result, frame_box, missing


def _format(key, value):
    if key.startswith("scale_") or key.endswith("_contrast"):
        return "%.6f" % value
    return "%d" % value


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: measure_scale.py <capture.png>\n")
        return 2
    if Image is None:
        sys.stderr.write(
            "measure_scale: PIL is not installed (pip install Pillow)\n")
        return 2
    try:
        with open(PATTERN_JSON, "r", encoding="utf-8") as f:
            pattern = json.load(f)
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_scale: cannot read pattern.json: %s\n" % exc)
        return 2
    try:
        image = Image.open(argv[1])
        image.load()
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "measure_scale: cannot read %s: %s\n" % (argv[1], exc))
        return 2

    result, _frame_box, missing = measure(image, pattern)
    for key in REPORT_KEYS:
        print("%s=%s" % (key, _format(key, result[key])))

    if not result["frame_found"]:
        sys.stderr.write(
            "measure_scale: white frame not found in top-left %dx%d region\n"
            % (REGION_WIDTH, REGION_HEIGHT))
        return 1
    if missing:
        sys.stderr.write(
            "measure_scale: corner marker(s) not found: %s\n"
            % " ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
