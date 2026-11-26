#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the EQ-4zc alpha-sweep capture crops.

The input is a 400x240 top-left crop of an arm11 render (or of a 1280x720
capture board screenshot).  The quad rectangles come from the layout JSON the
generator wrote next to the frame, in DS pixels; this tool applies a mapping
preset and reports the majority colour of each quad interior (projected
rectangle shrunk by 2 capture pixels per side, the method of
``tools/eq4w_blend_hw/measure_blend_hw.py``).  No blend model is assumed: a
quad is ``blended`` when its source channel's majority value is below the
source value (255), otherwise ``not blended``.

Mapping presets:

  ``capture``  the shared ``tools/ds_vtxcolor_test/capture_mapping.json``
               (scale 1.25, offset x=40) used for 1280x720 capture-board crops.
  ``arm11``    the 1:1 crop of the 400x240 arm11 render (EQ-4o: "the arm11
               render is 400x240, so the crop maps 1:1").  The DS content inside
               that render is itself scaled 1.25x with offset x=40 by the
               clipfix matrix (``common/twlhd_clipfix.c:40``, ``m[1][0] =
               -0.8f * hw``), so the effective DS-to-crop transform is the same
               1.25/40 as ``capture``.

Azahar renders need ``--azahar-correction on`` (rows 0 and 239 are uniform
black on the black clear, so ``auto`` is undetermined); hardware crops need
``off``.

    python3 tools/eq4zc_alpha_sweep/measure_sweep.py --layout L.json \\
        [--mapping capture|arm11] [--azahar-correction on|off|auto] cap.png

Output is deterministic ``key=value`` lines.  Exit 0 on success, 2 on usage,
PIL, layout or input errors.
"""

import argparse
import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
SMALL_DIR = os.path.join(TOOLS_DIR, "eq4w_blend_hw")
if SMALL_DIR not in sys.path:
    sys.path.insert(0, SMALL_DIR)

from azahar_presentation import apply_correction, format_line  # noqa: E402
from measure_blend_hw import (  # noqa: E402
    HEIGHT, WIDTH, channel_mode, clamp_rect, interior_rect,
)

CAPTURE_MAPPING_PATH = os.path.join(
    TOOLS_DIR, "ds_vtxcolor_test", "capture_mapping.json")
DEFAULT_LAYOUT = os.path.join(HERE, "fixtures", "eq4zc_sweep_green_layout.json")

# The arm11 render is a 400x240 top-screen output; its DS content is scaled
# 1.25x with offset x=40 by the clipfix (common/twlhd_clipfix.c:40) and the
# crop maps 1:1 (docs/reports/eq4o-hw-row-shift.md:27-29).
ARM11_MAPPING = {
    "scale_x": 1.25,
    "scale_y": 1.25,
    "offset_x": 40,
    "offset_y": 0,
}

MAPPING_NAMES = ("capture", "arm11")


def load_mapping(name):
    if name == "arm11":
        return dict(ARM11_MAPPING)
    with open(CAPTURE_MAPPING_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def project_rect(rect, mapping):
    """DS pixel edges -> capture pixel edges under one mapping preset."""
    x0, y0, x1, y1 = rect
    return (mapping["offset_x"] + mapping["scale_x"] * x0,
            mapping["offset_y"] + mapping["scale_y"] * y0,
            mapping["offset_x"] + mapping["scale_x"] * x1,
            mapping["offset_y"] + mapping["scale_y"] * y1)


def read_image(path):
    if Image is None:
        raise ValueError("PIL is not installed (pip install Pillow)")
    try:
        image = Image.open(path).convert("RGB")
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read image: %s" % exc)
    if image.size != (WIDTH, HEIGHT):
        raise ValueError("image must be %dx%d, got %dx%d"
                         % (WIDTH, HEIGHT, image.size[0], image.size[1]))
    return image


def load_layout(path):
    with open(path, "r", encoding="utf-8") as handle:
        layout = json.load(handle)
    for key in ("alphas", "source_rgb", "quads"):
        if key not in layout:
            raise ValueError("layout is missing %r" % (key,))
    if [quad["alpha"] for quad in layout["quads"]] != layout["alphas"]:
        raise ValueError("layout quads are not in alpha order")
    return layout


def source_channel(layout):
    rgb = layout["source_rgb"]
    return max(range(3), key=lambda index: rgb[index])


def read_pixels(image, mode):
    """Return (PixelAccess, applied, reason) after the presentation correction."""
    raw = image.tobytes()
    pixels = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    pixels, applied, reason = apply_correction(mode, pixels, WIDTH, HEIGHT)
    corrected = Image.new("RGB", (WIDTH, HEIGHT))
    corrected.putdata(pixels)
    return corrected.load(), applied, reason


def measure(pixels, layout, mapping):
    """Yield per-quad ``(alpha, rect_ds, interior, rgb, classification)``."""
    channel = source_channel(layout)
    source_value = layout["source_rgb"][channel]
    for quad in layout["quads"]:
        rect = quad["rect"]
        interior = interior_rect(project_rect(rect, mapping))
        rgb = tuple(channel_mode(pixels, interior, i) for i in range(3))
        blended = rgb[channel] < source_value
        yield (quad["alpha"], rect, interior, rgb,
               "blended" if blended else "not blended")


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="measure_sweep.py",
        description="Measure EQ-4zc alpha-sweep quad interiors.")
    parser.add_argument("--layout", default=DEFAULT_LAYOUT,
                        help="layout JSON written next to the frame")
    parser.add_argument("--mapping", choices=MAPPING_NAMES, default="capture",
                        help="capture-board (1.25/40) or arm11 (1:1 crop)")
    parser.add_argument("--azahar-correction", choices=("on", "off", "auto"),
                        default="auto", help="presentation-row correction mode")
    parser.add_argument("image", help="400x240 crop (PNG/PPM)")
    args = parser.parse_args(argv)

    try:
        layout = load_layout(args.layout)
        mapping = load_mapping(args.mapping)
        image = read_image(args.image)
    except (OSError, ValueError, KeyError) as exc:
        sys.stderr.write("measure_sweep: %s\n" % exc)
        return 2

    pixels, applied, reason = read_pixels(image, args.azahar_correction)

    print("tool=measure_sweep")
    print("basis=one 400x240 crop; quad interiors from the layout JSON; no model")
    print("image=%dx%d" % (WIDTH, HEIGHT))
    print("layout=%s" % args.layout)
    print("mapping=%s scale_x=%s scale_y=%s offset_x=%s offset_y=%s"
          % (args.mapping, mapping["scale_x"], mapping["scale_y"],
             mapping["offset_x"], mapping["offset_y"]))
    print(format_line(applied, reason))
    print("source_rgb=%d,%d,%d source_channel=%d interior_shrink=%d"
          % (layout["source_rgb"][0], layout["source_rgb"][1],
             layout["source_rgb"][2], source_channel(layout),
             layout.get("interior_shrink", 2)))
    for index, (alpha, rect, interior, rgb, classification) in enumerate(
            measure(pixels, layout, mapping)):
        print("quad%d.alpha=%d" % (index, alpha))
        print("quad%d.rect_ds=%d,%d,%d,%d" % ((index,) + tuple(rect)))
        print("quad%d.interior=%d,%d,%d,%d" % ((index,) + tuple(interior)))
        print("quad%d.majority_rgb=%d,%d,%d" % ((index,) + tuple(rgb)))
        print("quad%d.measured_green=%d" % (index, rgb[1]))
        print("quad%d.measured_blue=%d" % (index, rgb[2]))
        print("quad%d.classification=%s" % (index, classification))
    return 0


if __name__ == "__main__":
    sys.exit(main())
