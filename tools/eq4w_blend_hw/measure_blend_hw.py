#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the EQ-4w blend-enabled scene-4 hardware capture.

The fixture is the owner's top-left 400x240 crop of a 1280x720 passive capture
of the scene-4 ROM with ``GL_BLEND`` enabled (EQ-4v
``twlhd-arm9v2-s4-bl-st30.nds``).  This tool derives the four scene-4 quad
rectangles from the source vertices and the demo's own transform chain
(``arm9v2/source/demo_frame.cpp:246-259`` and ``:312-383``), places them on the
capture grid with the shared mapping (``tools/ds_vtxcolor_test/
capture_mapping.json``: scale 1.25, offset x=40), and reports the majority
colour per channel of the interior (projected bounding box shrunk by 2 capture
pixels per side).  No model of the DS blend equation is assumed: a quad is
``blended`` when its source channel's majority value is below the source
value 255, otherwise ``not-blended``.

The alpha-0 quad (white source, outline on hardware) carries no blend
classification; its region is censused with the EQ-4r bright rule
``max(r,g,b) > 60`` and the bright-pixel bounding box is printed next to the
region's pixel count.

Optionally ``--diff OTHER.png`` counts differing pixels between the capture and
a second capture (the EQ-4w a/b noise check).

    python3 tools/eq4w_blend_hw/measure_blend_hw.py [--diff b.png] cap.png

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

from gx_interp_oracle import (  # noqa: E402
    gl_look_at, gl_perspective, gl_rotate_x, gl_rotate_y, mat_mult, mat_vec,
)

WIDTH = 400
HEIGHT = 240
BRIGHT_RULE = 60
INTERIOR_SHRINK = 2

# Scene 4 source: arm9v2/source/demo_frame.cpp:250-259.  The v16 units are
# 20.12 fixed point, so a source coordinate is raw / 4096 (world units).
# Quad order and source colours match scene_alpha_vertices / scene_colors.
QUADS = (
    ((-2400, -1600), (-400, -1600), (-400, -200), (-2400, -200)),
    ((400, -1600), (2400, -1600), (2400, -200), (400, -200)),
    ((-2400, 200), (-400, 200), (-400, 1600), (-2400, 1600)),
    ((400, 200), (2400, 200), (2400, 1600), (400, 1600)),
)
ALPHAS = (31, 16, 4, 0)
COLOURS = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))
# Source channel that carries the quad's colour (quad 3 has no blend test).
SOURCE_CHANNEL = (0, 1, 2, None)

# arm9v2/source/demo_frame.cpp:350-383, STATIC_ANGLE=30 -> 2730 binary units.
FOVY = 70.0
ASPECT = 256.0 / 192.0
NEAR = 0.1
FAR = 100.0
STATIC_ANGLE_BIN = 2730
VIEWPORT = (0, 0, 255, 191)
SCALE = 2.0


def load_mapping():
    path = os.path.join(TOOLS_DIR, "ds_vtxcolor_test", "capture_mapping.json")
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def scene4_rects(mapping):
    """Projected scene-4 quad bounding boxes in capture pixels.

    Chain (column-major, same helpers as tools/gx_interp_oracle.py):
    clip = P(70,256/192,0.1,100) * LookAt(0,0,4) * Rx(2730) * Ry(2730) * S(2).
    NDC = clip.xy/clip.w; DS x = (ndc_x+1)/2*256, DS y from bottom =
    (ndc_y+1)/2*192; the capture is top-down, so row = offset_y +
    scale_y*(192 - y_from_bottom), column = offset_x + scale_x*DS x.
    """
    proj = gl_perspective(FOVY, ASPECT, NEAR, FAR)
    look = gl_look_at((0.0, 0.0, 4.0), (0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    roty = gl_rotate_y(STATIC_ANGLE_BIN)
    rotx = gl_rotate_x(STATIC_ANGLE_BIN)
    scale = [0.0] * 16
    scale[0] = scale[5] = scale[10] = SCALE
    scale[15] = 1.0
    modelview = mat_mult(look, mat_mult(rotx, mat_mult(roty, scale)))
    clip = mat_mult(proj, modelview)

    x1, y1, x2, y2 = VIEWPORT
    ds_w = x2 - x1 + 1
    ds_h = y2 - y1 + 1
    scale_x = mapping["scale_x"]
    scale_y = mapping["scale_y"]
    offset_x = mapping["offset_x"]
    offset_y = mapping["offset_y"]
    rects = []
    for quad in QUADS:
        points = []
        for raw_x, raw_y in quad:
            v = [raw_x / 4096.0, raw_y / 4096.0, 0.0, 1.0]
            cv = mat_vec(clip, v)
            ndc_x = cv[0] / cv[3]
            ndc_y = cv[1] / cv[3]
            ds_x = x1 + (ndc_x + 1.0) * 0.5 * ds_w
            ds_y_from_bottom = y1 + (ndc_y + 1.0) * 0.5 * ds_h
            capture_x = offset_x + scale_x * ds_x
            capture_y = offset_y + scale_y * (ds_h - ds_y_from_bottom)
            points.append((capture_x, capture_y))
        rects.append((
            min(p[0] for p in points), min(p[1] for p in points),
            max(p[0] for p in points), max(p[1] for p in points)))
    return rects


def clamp_rect(rect):
    x0 = max(0, int(round(rect[0])))
    y0 = max(0, int(round(rect[1])))
    x1 = min(WIDTH - 1, int(round(rect[2])))
    y1 = min(HEIGHT - 1, int(round(rect[3])))
    return x0, y0, x1, y1


def interior_rect(rect):
    x0, y0, x1, y1 = clamp_rect(rect)
    return (x0 + INTERIOR_SHRINK, y0 + INTERIOR_SHRINK,
            x1 - INTERIOR_SHRINK, y1 - INTERIOR_SHRINK)


def channel_mode(pixels, rect, channel):
    counts = {}
    x0, y0, x1, y1 = rect
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            value = pixels[x, y][channel]
            counts[value] = counts.get(value, 0) + 1
    return sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]


def bright_census(pixels, rect):
    x0, y0, x1, y1 = clamp_rect(rect)
    points = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
              if max(pixels[x, y]) > BRIGHT_RULE]
    bbox = None
    if points:
        bbox = (min(p[0] for p in points), min(p[1] for p in points),
                max(p[0] for p in points), max(p[1] for p in points))
    return len(points), bbox, (x1 - x0 + 1) * (y1 - y0 + 1)


def colour_census(pixels, top_n=16):
    counts = {}
    for y in range(HEIGHT):
        for x in range(WIDTH):
            rgb = pixels[x, y]
            counts[rgb] = counts.get(rgb, 0) + 1
    return sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:top_n]


def diff_census(pixels, other):
    points = [(x, y) for y in range(HEIGHT) for x in range(WIDTH)
              if pixels[x, y] != other[x, y]]
    bbox = None
    if points:
        bbox = (min(p[0] for p in points), min(p[1] for p in points),
                max(p[0] for p in points), max(p[1] for p in points))
    return len(points), bbox


def format_rect(rect):
    return "%d,%d,%d,%d" % rect


def format_float_rect(rect):
    return "%.3f,%.3f,%.3f,%.3f" % rect


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
    return image.load()


def usage():
    sys.stderr.write("usage: measure_blend_hw.py [--diff OTHER.png] "
                     "<400x240.png>\n")


def main(argv):
    args = list(argv[1:])
    diff_path = None
    positional = []
    while args:
        arg = args.pop(0)
        if arg == "--diff":
            if not args:
                sys.stderr.write("measure_blend_hw: --diff needs a path\n")
                return 2
            diff_path = args.pop(0)
        elif arg.startswith("--"):
            sys.stderr.write("measure_blend_hw: unknown option %s\n" % arg)
            return 2
        else:
            positional.append(arg)
    if len(positional) != 1:
        usage()
        return 2
    try:
        mapping = load_mapping()
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_blend_hw: cannot read capture mapping: %s\n"
                         % exc)
        return 2
    try:
        pixels = read_image(positional[0])
        other = read_image(diff_path) if diff_path else None
    except ValueError as exc:
        sys.stderr.write("measure_blend_hw: %s\n" % exc)
        return 2

    rects = scene4_rects(mapping)
    print("tool=measure_blend_hw")
    print("basis=one console, one capture session; scene=4 static_angle=30")
    print("image=%dx%d" % (WIDTH, HEIGHT))
    print("capture_mapping=scale_x=%s scale_y=%s offset_x=%s offset_y=%s"
          % (mapping["scale_x"], mapping["scale_y"],
             mapping["offset_x"], mapping["offset_y"]))
    print("projection=perspective(%.0f,%.6f,%.1f,%.0f) look_at=0,0,4 "
          "rotate_x=%d rotate_y=%d scale=%.0f viewport=%d,%d,%d,%d"
          % (FOVY, ASPECT, NEAR, FAR, STATIC_ANGLE_BIN, STATIC_ANGLE_BIN,
             SCALE, VIEWPORT[0], VIEWPORT[1], VIEWPORT[2], VIEWPORT[3]))
    print("derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; "
          "ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; "
          "capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top")
    for index, rect in enumerate(rects):
        interior = interior_rect(rect)
        modes = [channel_mode(pixels, interior, channel) for channel in range(3)]
        print("quad%d.source_rgb=%d,%d,%d alpha=%d"
              % (index, COLOURS[index][0], COLOURS[index][1],
                 COLOURS[index][2], ALPHAS[index]))
        print("quad%d.rect=%s" % (index, format_float_rect(rect)))
        print("quad%d.interior=%s" % (index, format_rect(interior)))
        print("quad%d.majority_rgb=%d,%d,%d"
              % (index, modes[0], modes[1], modes[2]))
        source_channel = SOURCE_CHANNEL[index]
        if source_channel is None:
            print("quad%d.classification=none" % index)
            continue
        source_value = COLOURS[index][source_channel]
        blended = modes[source_channel] < source_value
        print("quad%d.source_channel=%d" % (index, source_channel))
        print("quad%d.source_value=%d" % (index, source_value))
        print("quad%d.measured=%d" % (index, modes[source_channel]))
        print("quad%d.blended=%s" % (index, "yes" if blended else "no"))

    index = 3
    bright, bbox, region_pixels = bright_census(pixels, rects[index])
    interior = interior_rect(rects[index])
    x0, y0, x1, y1 = interior
    interior_pixels = (x1 - x0 + 1) * (y1 - y0 + 1)
    interior_bright = sum(
        1 for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
        if max(pixels[x, y]) > BRIGHT_RULE)
    print("quad%d.bright_rule=max_rgb_gt_%d" % (index, BRIGHT_RULE))
    print("quad%d.region=%s region_pixels=%d"
          % (index, format_rect(clamp_rect(rects[index])), region_pixels))
    print("quad%d.bright_pixels=%d" % (index, bright))
    print("quad%d.bright_bbox=%s"
          % (index, "none" if bbox is None else format_rect(bbox)))
    print("quad%d.interior=%s" % (index, format_rect(interior)))
    print("quad%d.interior_pixels=%d" % (index, interior_pixels))
    print("quad%d.interior_bright_pixels=%d" % (index, interior_bright))

    if other is not None:
        count, bbox = diff_census(pixels, other)
        print("diff_other=%s" % diff_path)
        print("diff_pixels=%d" % count)
        print("diff_bbox=%s" % ("none" if bbox is None else format_rect(bbox)))

    print("census_top=%d" % 16)
    for (r, g, b), count in colour_census(pixels):
        print("census=(%d, %d, %d) %d" % (r, g, b, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
