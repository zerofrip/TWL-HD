#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_scale_test/fit_mapping.py.

Synthetic captures are made from the rendered ds-scale-test pattern with a
known mapping (bilinear upsample via tools/eq4_hw_compare.py, pasted onto a
black 400x240 canvas) and the fit must recover that mapping within 0.05 px.
A capture whose content is shifted by one DS pixel is the negative control:
the fit must detect the shift (>= 0.9 DS px).  The real fixture
fixtures/sc4_3_top400x240.png (sha256 f7ad03e17a82...d198d5) is pinned with
the tolerance measured in docs/reports/eq4g-capture-offset.md:
a within +/-0.001 and b within +/-0.05 capture px (about 2x the larger
intercept standard error of the fit).  References the fixture at ds_scale_test/fixtures/sc4_3_top400x240.png.
Skips with a labelled reason when PIL is absent or the fixture is missing.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools \
        -p 'test_fit_mapping.py'
"""

import json
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
DS = os.path.join(HERE, "ds_scale_test")
CLI = os.path.join(DS, "fit_mapping.py")
FIXTURE = os.path.join(DS, "fixtures", "sc4_3_top400x240.png")
MAPPING_JSON = os.path.join(HERE, "ds_vtxcolor_test", "capture_mapping.json")
if DS not in sys.path:
    sys.path.insert(0, DS)
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import fit_mapping  # noqa: E402
import gen_pattern  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

with open(os.path.join(DS, "pattern.json"), "r", encoding="utf-8") as _f:
    PATTERN = json.load(_f)

# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-fit-mapping target.
DISCOVER_SKIP = os.environ.get("FIT_MAPPING_DISCOVER_SKIP") is not None

# The mapping fit_mapping.py compares against and uses to place its search
# windows.  The synthetic captures stay close to it so the windows still find
# the edges.
NOMINAL = {"scale_x": 1.25, "scale_y": 1.25,
           "offset_x": 40.0, "offset_y": 0.0}

def reference_pixels():
    raw = gen_pattern.render_rgb(PATTERN)
    return [(raw[i], raw[i + 1], raw[i + 2])
            for i in range(0, len(raw), 3)]


def synthetic_capture(reference, scale, paste_x, paste_y, size=(400, 240)):
    """Bilinear-upsample `reference` and paste it at (paste_x, paste_y)."""
    import eq4_hw_compare
    upsampled, out_w, out_h = eq4_hw_compare.reference_to_capture(
        reference, {"scale_x": scale[0], "scale_y": scale[1]})
    width, height = size
    canvas = [[(0, 0, 0)] * width for _ in range(height)]
    for y in range(out_h):
        for x in range(out_w):
            px = paste_x + x
            py = paste_y + y
            if 0 <= px < width and 0 <= py < height:
                canvas[py][px] = upsampled[y * out_w + x]
    flat = [canvas[y][x] for y in range(height) for x in range(width)]
    return flat, width, height


def fit(pixels, width, height):
    _observations, fits, _skipped = fit_mapping.fit_capture(
        pixels, width, height, PATTERN, NOMINAL)
    return fits


def shifted_reference(reference):
    """Shift the DS content right by one DS pixel (one black column)."""
    width = PATTERN["width"]
    height = PATTERN["height"]
    out = []
    for y in range(height):
        row = reference[y * width:(y + 1) * width]
        out.append((0, 0, 0))
        out.extend(row[:width - 1])
    return out


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-fit-mapping make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; fit_mapping capture tests skipped")
class FitMappingTest(unittest.TestCase):
    def assert_recovered(self, fits, scale, offset_x, offset_y):
        self.assertLessEqual(abs(fits["x"]["a"] - scale[0]), 0.05,
                             "a_x %.5f" % fits["x"]["a"])
        self.assertLessEqual(abs(fits["y"]["a"] - scale[1]), 0.05,
                             "a_y %.5f" % fits["y"]["a"])
        self.assertLessEqual(abs(fits["x"]["b"] - offset_x), 0.05,
                             "b_x %.5f" % fits["x"]["b"])
        self.assertLessEqual(abs(fits["y"]["b"] - offset_y), 0.05,
                             "b_y %.5f" % fits["y"]["b"])

    def test_synthetic_nominal_mapping_recovered(self):
        pixels, width, height = synthetic_capture(
            reference_pixels(), (1.25, 1.25), 40, 0)
        fits = fit(pixels, width, height)
        self.assert_recovered(fits, (1.25, 1.25), 40.0, 0.0)

    def test_synthetic_scale_decoupled_from_offset(self):
        pixels, width, height = synthetic_capture(
            reference_pixels(), (1.245, 1.25), 40, 0)
        fits = fit(pixels, width, height)
        self.assert_recovered(fits, (1.245, 1.25), 40.0, 0.0)

    def test_synthetic_offset_decoupled_from_scale(self):
        pixels, width, height = synthetic_capture(
            reference_pixels(), (1.25, 1.25), 41, 0)
        fits = fit(pixels, width, height)
        self.assert_recovered(fits, (1.25, 1.25), 41.0, 0.0)

    def test_one_ds_pixel_shift_is_detected(self):
        pixels, width, height = synthetic_capture(
            shifted_reference(reference_pixels()), (1.25, 1.25), 40, 0)
        fits = fit(pixels, width, height)
        shift_ds = (fits["x"]["b"] - NOMINAL["offset_x"]) / fits["x"]["a"]
        self.assertGreaterEqual(abs(shift_ds), 0.9,
                                "shift %.3f DS px not detected" % shift_ds)
        self.assertGreater(abs(shift_ds), 0.05)

    @unittest.skipUnless(
        os.path.exists(FIXTURE),
        "fixture sc4_3_top400x240.png is missing")
    def test_real_fixture_fit_pinned(self):
        with Image.open(FIXTURE) as image:
            image = image.convert("RGB")
            raw = image.tobytes()
            pixels = [(raw[i], raw[i + 1], raw[i + 2])
                      for i in range(0, len(raw), 3)]
            fits = fit(pixels, image.width, image.height)
        # Measured in docs/reports/eq4g-capture-offset.md on the committed
        # fixture; tolerance +/-0.001 (a) and +/-0.05 capture px (b).
        self.assertLessEqual(abs(fits["x"]["a"] - 1.250383), 0.001)
        self.assertLessEqual(abs(fits["x"]["b"] - 39.933172), 0.05)
        self.assertLessEqual(abs(fits["y"]["a"] - 1.250790), 0.001)
        self.assertLessEqual(abs(fits["y"]["b"] - (-0.168453)), 0.05)

    @unittest.skipUnless(
        os.path.exists(FIXTURE),
        "fixture sc4_3_top400x240.png is missing")
    def test_cli_is_deterministic(self):
        first = subprocess.run([sys.executable, CLI, FIXTURE],
                               capture_output=True, text=True)
        second = subprocess.run([sys.executable, CLI, FIXTURE],
                                capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        report = {}
        for line in first.stdout.splitlines():
            for token in line.split():
                if "=" in token:
                    key, value = token.split("=", 1)
                    report[key] = value
        self.assertIn("a_x", report)
        self.assertIn("b_x", report)
        self.assertIn("rms_x", report)

    def test_usage_exits_two(self):
        completed = subprocess.run([sys.executable, CLI],
                                   capture_output=True, text=True)
        self.assertEqual(completed.returncode, 2)
        self.assertIn("usage:", completed.stderr)


if __name__ == "__main__":
    unittest.main()
