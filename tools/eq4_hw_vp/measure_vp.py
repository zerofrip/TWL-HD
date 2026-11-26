#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the EQ-4zg partial-viewport fixtures.

The ROM was built with the DS viewport bytes ``VP=(32,96)-(159,191)`` (y from
the bottom, ``docs/PHASE5A_V2_RENDERER_DESIGN.md:39``).  This tool derives the
matching viewport rectangle in capture coordinates from the shared mapping
(``tools/ds_vtxcolor_test/capture_mapping.json``: scale 1.25, offset x=40):
``capture_x = offset_x + scale_x * ds_x`` and
``capture_y = offset_y + scale_y * (192 - ds_y_from_bottom)``.  It then reports
the non-background bounding box and a census of non-background pixels outside
that rectangle, so bleed outside the viewport is localised.  A 256x192
harness image is measured in native DS coordinates (identity mapping).

Background rule: the same as ``tools/eq4_hw_compare.py`` (the clear colour is
black; a pixel is background when every channel is <= 24).  The same-file
constant is imported, not duplicated.

Optionally ``--diff OTHER.png`` counts differing pixels between two images of
the same size (the EQ-4r a/b capture-noise check).

    python3 tools/eq4_hw_vp/measure_vp.py [--diff OTHER.png] IMAGE

Output is deterministic ``key=value`` lines.  Exit 0 on success, 2 on usage,
PIL or input errors.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
sys.path.insert(0, TOOLS_DIR)

from eq4_hw_compare import BG_MAX_CHANNEL  # noqa: E402

DS_WIDTH = 256
DS_HEIGHT = 192
CAPTURE_WIDTH = 400
CAPTURE_HEIGHT = 240

# EQ-4zg ROM switches: `make -C arm9v2 SCENE=4 VP_X1=32 VP_Y1=96 VP_X2=159
# VP_Y2=191 STATIC_ANGLE=30`.  y1/y2 are measured from the bottom.
VIEWPORT = (32, 96, 159, 191)


def load_mapping():
    path = os.path.join(TOOLS_DIR, "ds_vtxcolor_test", "capture_mapping.json")
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def is_background(rgb):
    return max(abs(channel) for channel in rgb) <= BG_MAX_CHANNEL


def viewport_rect(mapping, native):
    """Viewport rectangle in image pixels, inclusive bounds.

    ``x1..x2`` and ``y1..y2`` are inclusive viewport bytes; y is measured from
    the bottom.  In DS top-down rows the viewport covers
    ``192-(y2+1) .. 192-(y1+1)``.  A capture pixel is
    ``offset + scale * DS coordinate``; the upper bound is the last pixel
    before ``scale * (last + 1)``.
    """
    x1, y1, x2, y2 = VIEWPORT
    top_ds = DS_HEIGHT - (y2 + 1)
    bottom_ds = DS_HEIGHT - (y1 + 1)
    if native:
        scale_x = scale_y = 1
        offset_x = offset_y = 0
    else:
        scale_x = mapping["scale_x"]
        scale_y = mapping["scale_y"]
        offset_x = mapping["offset_x"]
        offset_y = mapping["offset_y"]
    left = int(round(offset_x + scale_x * x1))
    right = int(round(offset_x + scale_x * (x2 + 1))) - 1
    top = int(round(offset_y + scale_y * top_ds))
    bottom = int(round(offset_y + scale_y * (bottom_ds + 1))) - 1
    return (left, top, right, bottom)


def nonbg_census(pixels, width, height):
    bbox = None
    count = 0
    for y in range(height):
        for x in range(width):
            if is_background(pixels[x, y]):
                continue
            count += 1
            if bbox is None:
                bbox = [x, y, x, y]
            else:
                bbox[0] = min(bbox[0], x)
                bbox[1] = min(bbox[1], y)
                bbox[2] = max(bbox[2], x)
                bbox[3] = max(bbox[3], y)
    return count, (None if bbox is None else tuple(bbox))


def outside_census(pixels, width, height, rect):
    left, top, right, bottom = rect
    bbox = None
    count = 0
    for y in range(height):
        for x in range(width):
            if left <= x <= right and top <= y <= bottom:
                continue
            if is_background(pixels[x, y]):
                continue
            count += 1
            if bbox is None:
                bbox = [x, y, x, y]
            else:
                bbox[0] = min(bbox[0], x)
                bbox[1] = min(bbox[1], y)
                bbox[2] = max(bbox[2], x)
                bbox[3] = max(bbox[3], y)
    return count, (None if bbox is None else tuple(bbox))


def diff_census(pixels, other, width, height):
    bbox = None
    count = 0
    for y in range(height):
        for x in range(width):
            if pixels[x, y] == other[x, y]:
                continue
            count += 1
            if bbox is None:
                bbox = [x, y, x, y]
            else:
                bbox[0] = min(bbox[0], x)
                bbox[1] = min(bbox[1], y)
                bbox[2] = max(bbox[2], x)
                bbox[3] = max(bbox[3], y)
    return count, (None if bbox is None else tuple(bbox))


def format_rect(rect):
    return "none" if rect is None else ",".join(str(value) for value in rect)


def read_image(path):
    if Image is None:
        raise ValueError("PIL is not installed (pip install Pillow)")
    try:
        image = Image.open(path).convert("RGB")
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read image: %s" % exc)
    return image.size, image.load()


def usage():
    sys.stderr.write("usage: measure_vp.py [--diff OTHER.png] <image>\n")


def main(argv):
    args = list(argv[1:])
    diff_path = None
    positional = []
    while args:
        arg = args.pop(0)
        if arg == "--diff":
            if not args:
                sys.stderr.write("measure_vp: --diff needs a path\n")
                return 2
            diff_path = args.pop(0)
        elif arg.startswith("--"):
            sys.stderr.write("measure_vp: unknown option %s\n" % arg)
            return 2
        else:
            positional.append(arg)
    if len(positional) != 1:
        usage()
        return 2
    try:
        mapping = load_mapping()
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_vp: cannot read capture mapping: %s\n" % exc)
        return 2
    try:
        size, pixels = read_image(positional[0])
        other = None
        if diff_path:
            other_size, other = read_image(diff_path)
            if other_size != size:
                raise ValueError("--diff image must be %dx%d, got %dx%d"
                                 % (size[0], size[1], other_size[0],
                                    other_size[1]))
    except ValueError as exc:
        sys.stderr.write("measure_vp: %s\n" % exc)
        return 2
    width, height = size
    if (width, height) == (DS_WIDTH, DS_HEIGHT):
        native = True
    elif (width, height) == (CAPTURE_WIDTH, CAPTURE_HEIGHT):
        native = False
    else:
        sys.stderr.write("measure_vp: image must be %dx%d or %dx%d, got %dx%d\n"
                         % (DS_WIDTH, DS_HEIGHT, CAPTURE_WIDTH, CAPTURE_HEIGHT,
                            width, height))
        return 2

    x1, y1, x2, y2 = VIEWPORT
    rect = viewport_rect(mapping, native)
    count, bbox = nonbg_census(pixels, width, height)
    outside, outside_bbox = outside_census(pixels, width, height, rect)

    print("tool=measure_vp")
    print("viewport_ds=x1=%d y1=%d x2=%d y2=%d (inclusive bytes; y from bottom)"
          % (x1, y1, x2, y2))
    print("background_rule=max_channel<=%d" % BG_MAX_CHANNEL)
    if native:
        print("mode=native")
        print("image=%dx%d" % (width, height))
        print("mapping=identity (256x192 native DS framebuffer)")
        print("derivation=ds_top=%d-(y2+1)=%d..%d-(y1+1)=%d; "
              "native rect=x1..x2, ds_top..ds_bottom"
              % (DS_HEIGHT, DS_HEIGHT - (y2 + 1), DS_HEIGHT,
                 DS_HEIGHT - (y1 + 1)))
    else:
        print("mode=capture")
        print("image=%dx%d" % (width, height))
        print("capture_mapping=scale_x=%s scale_y=%s offset_x=%s offset_y=%s"
              % (mapping["scale_x"], mapping["scale_y"],
                 mapping["offset_x"], mapping["offset_y"]))
        print("derivation=ds_top=%d-(y2+1)=%d..%d-(y1+1)=%d; "
              "capture_x=%s+%s*ds_x; capture_y=%s+%s*(%d-ds_y_from_bottom)"
              % (DS_HEIGHT, DS_HEIGHT - (y2 + 1), DS_HEIGHT,
                 DS_HEIGHT - (y1 + 1), mapping["offset_x"],
                 mapping["scale_x"], mapping["offset_y"], mapping["scale_y"],
                 DS_HEIGHT))
    print("viewport_rect=%s" % format_rect(rect))
    print("nonbg_pixels=%d" % count)
    print("bbox=%s" % format_rect(bbox))
    print("outside_viewport_pixels=%d" % outside)
    print("outside_viewport_bbox=%s" % format_rect(outside_bbox))
    if other is not None:
        diff, diff_bbox = diff_census(pixels, other, width, height)
        print("diff_other=%s" % diff_path)
        print("diff_pixels=%d" % diff)
        print("diff_bbox=%s" % format_rect(diff_bbox))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
