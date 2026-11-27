#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the DS MODULATE probe (TEXMOD=1) from a top-screen P6 PPM.

Samples a 9x9 mean at the centre of each of the eight quads of
tools/hw/gen_texmod_rom_data.py and compares every channel with two models of
texture x vertex colour (both given the same 5-bit texel and vertex levels):

  pica   8-bit texel (oracle 5->8 table) times vertex/31, rounded: what the
         arm11 renderer's GPU_MODULATE stage computes.
  ds6    DeSmuME six-bit equation: x6 = x ? 2x+1 : 0 for both operands,
         out6 = ((t6+1)*(v6+1)-1) >> 6, shown at 8 bit as out6*255/63.

The capture path keeps only 5 bits of red and blue and 6 bits of green
(every measured red/blue value is a multiple of 255/31, green of 255/63), so the
tool also compares in that domain: the measured value is rounded to its capture
level and each model is reduced to the same level (ds6: out6 >> 1 for red/blue,
out6 for green; pica: the 8-bit value rounded to the level).

Prints measured, both predictions and deviations, the exact capture-level
matches per model, then the maximum deviation per model.  Exit 0 when the PPM was readable and every sample inside the image;
2 on malformed input.  Stdlib only.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
for path in (TOOLS, HERE):
    if path not in sys.path:
        sys.path.insert(0, path)

import gen_texmod_rom_data as rom  # noqa: E402
import gen_tex_format_frames as gen  # noqa: E402
from check_tex_shot import parse_ppm  # noqa: E402
from tex_decode_oracle import FIVE_TO_EIGHT  # noqa: E402

PATCH_RADIUS = 4


def model_pica(texel5, vertex5):
    return int(FIVE_TO_EIGHT[texel5] * vertex5 / 31.0 + 0.5)


def _six(value):
    return value * 2 + 1 if value else 0


def model_ds6(texel5, vertex5):
    out6 = ((_six(texel5) + 1) * (_six(vertex5) + 1) - 1) >> 6
    return int(out6 * 255 / 63.0 + 0.5)


MODELS = (("pica", model_pica), ("ds6", model_ds6))


def capture_top(channel):
    """Highest capture level: 6-bit green, 5-bit red/blue."""
    return 63 if channel == "G" else 31


def capture_level_measured(value, channel):
    return int(value * capture_top(channel) / 255.0 + 0.5)


def capture_level_model(name, texel5, vertex5, channel):
    """A model's prediction reduced to the capture level of the channel."""
    if name == "ds6":
        out6 = ((_six(texel5) + 1) * (_six(vertex5) + 1) - 1) >> 6
        return out6 if channel == "G" else out6 >> 1
    return int(model_pica(texel5, vertex5) * capture_top(channel) / 255.0
               + 0.5)


def capture_value(level, channel):
    """8-bit value a capture shows for a level (inverse of the above)."""
    return int(level * 255.0 / capture_top(channel) + 0.5)


def sample(data, width, height, cx, cy):
    """9x9 mean RGB at (cx, cy)."""
    if (cx - PATCH_RADIUS < 0 or cy - PATCH_RADIUS < 0 or
            cx + PATCH_RADIUS >= width or cy + PATCH_RADIUS >= height):
        raise ValueError("patch outside the image")
    total = [0, 0, 0]
    count = 0
    for y in range(cy - PATCH_RADIUS, cy + PATCH_RADIUS + 1):
        for x in range(cx - PATCH_RADIUS, cx + PATCH_RADIUS + 1):
            off = (y * width + x) * 3
            for c in range(3):
                total[c] += data[off + c]
            count += 1
    return tuple(int(round(t / count)) for t in total)


def measure(data, width, height):
    """Return one row per quad channel: (quad, ch, texel, vertex, measured,
    {model: (prediction, deviation)})."""
    rows = []
    for index, (texel, vertex) in enumerate(rom.PAIRS):
        x0, y0, x1, y1 = gen.QUAD_SPECS[index]["rect"]
        got = sample(data, width, height, (x0 + x1) // 2, (y0 + y1) // 2)
        for ch in range(3):
            preds = {}
            for name, model in MODELS:
                value = model(texel[ch], vertex[ch])
                level = capture_level_model(name, texel[ch], vertex[ch],
                                            "RGB"[ch])
                preds[name] = (value, abs(got[ch] - value), level)
            rows.append((index, "RGB"[ch], texel[ch], vertex[ch], got[ch],
                         preds))
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(prog="measure_texmod.py",
                                     description=__doc__.split("\n")[0])
    parser.add_argument("ppm", help="400x240 (or 400x480) P6 PPM")
    args = parser.parse_args(argv)
    try:
        width, height, data = parse_ppm(args.ppm)
        if width != 400 or height < 240:
            raise ValueError("expected a 400x240 or 400x480 PPM, got %dx%d"
                             % (width, height))
        rows = measure(data, width, 240)
    except (ValueError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    print("quad ch tex vtx measured  pica(dev)  ds6(dev)")
    worst = {name: 0 for name, _ in MODELS}
    exact = {name: 0 for name, _ in MODELS}
    for quad, ch, t, v, got, preds in rows:
        level = capture_level_measured(got, ch)
        print("%4d %2s %3d %3d %8d  %4d(%3d)  %4d(%3d)  level %2d/%2d "
              "pica %2d ds6 %2d"
              % (quad, ch, t, v, got, preds["pica"][0], preds["pica"][1],
                 preds["ds6"][0], preds["ds6"][1], level, capture_top(ch),
                 preds["pica"][2], preds["ds6"][2]))
        for name, _ in MODELS:
            worst[name] = max(worst[name], preds[name][1])
            if preds[name][2] == level:
                exact[name] += 1
    print("capture-level exact matches of %d: " % len(rows) +
          " ".join("%s=%d" % (n, exact[n]) for n, _ in MODELS))
    print("max deviation: " + " ".join("%s=%d" % (n, worst[n])
                                       for n, _ in MODELS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
