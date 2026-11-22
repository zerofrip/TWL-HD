#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print measured colours of the ds-scale-test pattern inside a capture PNG.

Maps DS pixel centres to capture pixels with the EQ-4c mapping
(`tools/ds_vtxcolor_test/capture_mapping.json`, the same formula as
`measure_vtxcolor.capture_xy`) and prints stable `key=value` lines:

- `corner_*`  measured RGB at the four marker centres DS (4,4), (251,4),
  (4,187), (251,187);
- `gradient_i`  measured RGB at DS (16*i+8, 148) plus the expected step value
  from the ds_scale_test README §3 table and `match=yes|no`.  The ROM writes
  one grey value into R, G and B; the match compares the red channel and the
  full measured tuple is printed next to it;
- `strip_i`  measured RGB of the 1-px alternating `hstrip` row at DS (i, 44),
  i = 0..15, as an observation (no filter is identified).

    python3 fixture_colours.py <capture.png>

Requires PIL; exits 2 with a one-line message when PIL is missing or the PNG
cannot be read.
"""

import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
MAPPING_JSON = os.path.join(
    os.path.dirname(HERE), "ds_vtxcolor_test", "capture_mapping.json")

CORNERS = (
    ("corner_tl", 4, 4),
    ("corner_tr", 251, 4),
    ("corner_bl", 4, 187),
    ("corner_br", 251, 187),
)
GRADIENT_DS_Y = 148
GRADIENT_STEP_PX = 16
GRADIENT_STEPS = 16
# ds_scale_test README §3 table (5-bit expansion v5<<3|v5>>2).
GRADIENT_EXPECTED = (0, 16, 33, 49, 66, 82, 99, 115, 132, 148, 156, 173,
                     189, 206, 222, 239)
STRIP_DS_Y = 44
STRIP_SAMPLES = 16


def capture_xy(ds_x, ds_y, mapping):
    """EQ-4c mapping: DS pixel centre -> capture pixel."""
    cx = mapping["offset_x"] + (ds_x + 0.5) * mapping["scale_x"] - 0.5
    cy = mapping["offset_y"] + (ds_y + 0.5) * mapping["scale_y"] - 0.5
    return int(round(cx)), int(round(cy))


def _rgb(pixels, mapping, ds_x, ds_y):
    cx, cy = capture_xy(ds_x, ds_y, mapping)
    r, g, b = pixels[cx, cy]
    return (r, g, b)


def report(image, mapping):
    """Return the report lines for a PIL image."""
    pixels = image.convert("RGB").load()
    lines = []
    for name, ds_x, ds_y in CORNERS:
        lines.append("%s=%d,%d,%d" % ((name,) + _rgb(pixels, mapping,
                                                     ds_x, ds_y)))
    matches = 0
    for i, expected in enumerate(GRADIENT_EXPECTED):
        rgb = _rgb(pixels, mapping, GRADIENT_STEP_PX * i + 8, GRADIENT_DS_Y)
        match = "yes" if rgb[0] == expected else "no"
        matches += match == "yes"
        lines.append("gradient_i=%d ds_x=%d rgb=%d,%d,%d expected=%d match=%s"
                     % ((i, GRADIENT_STEP_PX * i + 8) + rgb
                        + (expected, match)))
    lines.append("gradient_match_count=%d/%d" % (matches,
                                                 len(GRADIENT_EXPECTED)))
    for i in range(STRIP_SAMPLES):
        lines.append("strip_i=%d rgb=%d,%d,%d"
                     % ((i,) + _rgb(pixels, mapping, i, STRIP_DS_Y)))
    return lines


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: fixture_colours.py <capture.png>\n")
        return 2
    if Image is None:
        sys.stderr.write(
            "fixture_colours: PIL is not installed (pip install Pillow)\n")
        return 2
    try:
        with open(MAPPING_JSON, "r", encoding="utf-8") as f:
            mapping = json.load(f)
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "fixture_colours: cannot read capture_mapping.json: %s\n" % exc)
        return 2
    try:
        image = Image.open(argv[1])
        image.load()
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "fixture_colours: cannot read %s: %s\n" % (argv[1], exc))
        return 2
    for line in report(image, mapping):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
