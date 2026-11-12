#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Predict the on-screen result of a v2 frame from the host GX interpreter.

Runs tests/host/build/gx_interp_cli on a frame and writes JSON with the
expected 8-bit RGBA for every quad plus the expected top-screen pixel centre.
BUILD/UNIT tooling only: the CLI is a host binary, nothing contacts a console
or a network address.

The pixel mapping is derived from the arm11 sources:

  1. The interpreter emits DS clip space (common/twlhd_gx_interp.c:546-562).
  2. clipfix (common/twlhd_clipfix.c:12-43, applied by
     arm11/source/renderer.c:849-850 and the clip vertex shader
     arm11/shaders/twlhd_clip_vshader.v.pica:22-28):
        x' =  hh*y + cy
        y' = -0.8*(hw*x + cx)
  3. The render target is 240x400 (arm11/source/renderer.c:671-672):
        fb_x = 120*(x' + 1)
        fb_y = 200*(1 - y')
  4. The top screen is presented as 400x240; tools/ntr_capture rotates the
     portrait framebuffer 90 degrees counter-clockwise
     (tools/ntr_capture/README.md:151-155):
        sx = fb_y
        sy = 239 - fb_x

The 0.8 factor narrows the 256-wide DS picture into the centred 320-pixel
region of the 400-pixel screen (40-pixel bars on each side); Run 7 confirms
the bars and the non-mirrored orientation (docs/HARDWARE_TEST_PROCEDURE_3DS.md:
285-287, 320-321).
"""

import argparse
import json
import os
import subprocess
import sys

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS_DIR)
DEFAULT_CLI = os.path.join(ROOT, "tests", "host", "build", "gx_interp_cli")

SCREEN_WIDTH = 400
SCREEN_HEIGHT = 240
FB_WIDTH = 240
FB_HEIGHT = 400

# arm11/source/renderer.c:38 RENDER_CLEAR_COLOR 0x101018FF = (16,16,24,255);
# Run 17 measured (16,16,24) over the top screen.
CLEAR_RGBA8 = (16, 16, 24, 255)
# A corner patch that the generated 2x2 quad layout never covers.
BACKGROUND_CENTRE = (12.0, 12.0)

MAPPING_TEXT = (
    "clipfix x'=hh*y+cy, y'=-0.8*(hw*x+cx); "
    "fb 240x400: fb_x=120*(x'+1), fb_y=200*(1-y'); "
    "top-screen PPM 400x240: sx=fb_y, sy=239-fb_x")


class PredictError(Exception):
    """A clear, user-facing prediction failure."""


def _round8(value):
    # Half-up rounding matches "rgb in 0..1 -> 0..255 rounded".
    return int(value * 255.0 + 0.5)


def viewport_params(viewport):
    """Return (hw, hh, cx, cy) for the VIEWPORT word (clipfix derivation)."""
    x1, y1, x2, y2, present = viewport
    if not present:
        x1, y1, x2, y2 = 0, 0, 255, 191
    width = ((x2 - x1) & 0xFF) + 1
    height = ((y2 - y1) & 0xFF) + 1
    y1v = y1 - 0x00FF if y1 > 191 else y1
    hw = width / 256.0
    hh = height / 192.0
    cx = (2 * x1 + width) / 256.0 - 1.0
    cy = (2 * y1v + height) / 192.0 - 1.0
    return hw, hh, cx, cy


def map_ndc(x, y, viewport):
    """DS NDC -> expected top-screen (sx, sy) pixel coordinate."""
    hw, hh, cx, cy = viewport_params(viewport)
    x_clip = hh * y + cy
    y_clip = -0.8 * (hw * x + cx)
    fb_x = (x_clip + 1.0) / 2.0 * FB_WIDTH
    fb_y = (1.0 - y_clip) / 2.0 * FB_HEIGHT
    return fb_y, (SCREEN_HEIGHT - 1) - fb_x


def parse_cli_output(text):
    """Parse gx_interp_cli output (tests/host/gx_interp_cli.c:113-155)."""
    result = None
    viewport = None
    stats = None
    vertices = []

    for line in text.splitlines():
        if line.startswith("result="):
            result = line.split("=", 1)[1].strip()
        elif line.startswith("viewport "):
            parts = line.split()
            if len(parts) != 6:
                raise PredictError("malformed viewport line: %r" % line)
            viewport = (int(parts[1]), int(parts[2]), int(parts[3]),
                        int(parts[4]), bool(int(parts[5])))
        elif line.startswith("stats "):
            stats = {}
            for token in line.split()[1:]:
                key, value = token.split("=", 1)
                stats[key] = int(value)
        elif line and line[0].isdigit():
            parts = line.split()
            if len(parts) != 9:
                raise PredictError("malformed vertex line: %r" % line)
            vertices.append((float(parts[1]), float(parts[2]),
                             float(parts[3]), float(parts[4]),
                             float(parts[5]), float(parts[6]),
                             float(parts[7]), float(parts[8])))

    if result is None or viewport is None or stats is None:
        raise PredictError("incomplete gx_interp_cli output")
    if result != "OK":
        raise PredictError("interpreter result=%s" % result)
    return viewport, stats, vertices


def _clip_centre(corners):
    count = float(len(corners))
    return tuple(sum(c[i] for c in corners) / count for i in range(4))


def build_document(frame_path, cli_path, viewport, stats, vertices):
    quads_split = stats.get("quads_split", 0)
    if quads_split <= 0:
        raise PredictError("frame produced no split quads")
    per_quad = 6  # two triangles: 0,1,2 / 0,2,3 (twlhd_gx_interp.c:630-645)
    if len(vertices) != quads_split * per_quad:
        raise PredictError(
            "unexpected output: %d vertices for %d quads"
            % (len(vertices), quads_split))

    quads = []
    for index in range(quads_split):
        group = vertices[index * per_quad:(index + 1) * per_quad]
        rgba = group[0][4:8]
        for vertex in group[1:]:
            if any(abs(vertex[4 + c] - rgba[c]) > 1e-6 for c in range(4)):
                raise PredictError(
                    "quad %d has inconsistent vertex colours" % index)
        corners = [group[i] for i in (0, 1, 2, 5)]
        clip = _clip_centre(corners)
        ndc_x = sum(c[0] / c[3] for c in corners) / 4.0
        ndc_y = sum(c[1] / c[3] for c in corners) / 4.0
        pixel = map_ndc(ndc_x, ndc_y, viewport)
        quads.append({
            "index": index,
            "rgba8": [_round8(rgba[0]), _round8(rgba[1]),
                      _round8(rgba[2]), _round8(rgba[3])],
            "clip_centre": list(clip),
            "ndc_centre": [ndc_x, ndc_y],
            "pixel_centre": [pixel[0], pixel[1]],
        })

    x1, y1, x2, y2, present = viewport
    if not present:
        # The interpreter leaves the struct zeroed; the renderer's clipfix
        # uses the full DS viewport, so record that effective rectangle.
        x1, y1, x2, y2 = 0, 0, 255, 191

    return {
        "frame": os.path.abspath(frame_path),
        "tool": os.path.abspath(cli_path),
        "result": "OK",
        "viewport": {
            "x1": x1, "y1": y1,
            "x2": x2, "y2": y2,
            "present": present,
        },
        "screen": {"width": SCREEN_WIDTH, "height": SCREEN_HEIGHT},
        "mapping": MAPPING_TEXT,
        "clear_colour": list(CLEAR_RGBA8),
        "background": {
            "pixel_centre": list(BACKGROUND_CENTRE),
            "rgba8": list(CLEAR_RGBA8),
        },
        "stats": stats,
        "quads": quads,
    }


def predict(frame_path, cli_path=DEFAULT_CLI):
    """Run the interpreter CLI and return the prediction document."""
    if not os.path.isfile(cli_path):
        raise PredictError(
            "gx_interp_cli not found at %s (run `make test` to build it)"
            % cli_path)
    try:
        completed = subprocess.run(
            [cli_path, frame_path], capture_output=True, text=True)
    except OSError as exc:
        raise PredictError("cannot run %s: %s" % (cli_path, exc))
    if completed.returncode != 0:
        raise PredictError("gx_interp_cli failed: %s"
                           % (completed.stderr.strip() or "no stderr"))
    viewport, stats, vertices = parse_cli_output(completed.stdout)
    return build_document(frame_path, cli_path, viewport, stats, vertices)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="predict_lit.py",
        description="Predict per-quad RGBA and top-screen centres from a v2 "
                    "frame.")
    parser.add_argument("frame", help="v2 frame (.bin)")
    parser.add_argument("--cli", default=DEFAULT_CLI,
                        help="gx_interp_cli binary (default: %(default)s)")
    parser.add_argument("--out", default=None,
                        help="write JSON here instead of stdout")
    args = parser.parse_args(argv)

    try:
        document = predict(args.frame, args.cli)
    except PredictError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    text = json.dumps(document, indent=2) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
