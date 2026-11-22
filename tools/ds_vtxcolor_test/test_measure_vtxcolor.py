#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_vtxcolor_test/measure_vtxcolor.py.

Synthesises a 400x240 capture by inverting the measured EQ-4c mapping and
checking the report/classification for three default colours, plus a
geometry-not-found case.  Cross-checks capture_mapping.json against
measure_scale.py on the committed ds_scale_test fixture.  Skips with a labelled
reason when PIL is missing (or the fixture is missing, for that one test).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_vtxcolor.py")
SCALE_DIR = os.path.join(os.path.dirname(HERE), "ds_scale_test")
FIXTURE = os.path.join(SCALE_DIR, "fixtures", "sc1_5_crop400x480.png")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import gen_quads  # noqa: E402
import measure_vtxcolor  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

with open(os.path.join(HERE, "quads.json"), "r", encoding="utf-8") as _f:
    QUADS = json.load(_f)
with open(os.path.join(HERE, "capture_mapping.json"), "r",
          encoding="utf-8") as _f:
    MAPPING = json.load(_f)

DEFAULT_GREY = (200, 200, 200)


def ds_colour(ds_x, ds_y, default_rgb):
    """Colour of a DS pixel in the synthetic pattern."""
    for quad in QUADS["quads"]:
        if (quad["x"] <= ds_x < quad["x"] + quad["w"]
                and quad["y"] <= ds_y < quad["y"] + quad["h"]):
            if quad["colour"] is None:
                return default_rgb
            return gen_quads.unpack_rgb15(gen_quads.colour5(quad["colour"]))
    return gen_quads.unpack_rgb15(QUADS["background5"])


def synthetic_capture(default_rgb):
    """400x240 capture built by inverse-mapping every capture pixel."""
    image = Image.new("RGB", (400, 240), (0, 0, 0))
    pixels = image.load()
    for cy in range(240):
        ds_y = int(round((cy - MAPPING["offset_y"] + 0.5)
                         / MAPPING["scale_y"] - 0.5))
        for cx in range(400):
            ds_x = int(round((cx - MAPPING["offset_x"] + 0.5)
                             / MAPPING["scale_x"] - 0.5))
            if 0 <= ds_x < QUADS["width"] and 0 <= ds_y < QUADS["height"]:
                pixels[cx, cy] = ds_colour(ds_x, ds_y, default_rgb)
    return image


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; measure_vtxcolor capture tests skipped")
class MeasureVtxcolorTest(unittest.TestCase):
    def report_for(self, default_rgb):
        image = synthetic_capture(default_rgb)
        return measure_vtxcolor.measure(image, QUADS, MAPPING)

    def test_default_white_is_same_as_qB(self):
        report, missing = self.report_for((255, 255, 255))
        self.assertEqual(missing, [])
        self.assertEqual(report["geometry_found"], 1)
        self.assertEqual(report["qA_rgb"], (255, 255, 255))
        self.assertEqual(report["qB_rgb"], (255, 255, 255))
        self.assertEqual(report["default_colour_class"], "same_as_qB")

    def test_default_black_is_same_as_qC(self):
        report, missing = self.report_for((0, 0, 0))
        self.assertEqual(missing, [])
        self.assertEqual(report["qA_rgb"], (0, 0, 0))
        self.assertEqual(report["qC_rgb"], (0, 0, 0))
        self.assertEqual(report["default_colour_class"], "same_as_qC")

    def test_default_distinct_grey_is_grey(self):
        report, missing = self.report_for(DEFAULT_GREY)
        self.assertEqual(missing, [])
        self.assertEqual(report["qA_rgb"], DEFAULT_GREY)
        self.assertEqual(report["qD_rgb"], (132, 132, 132))
        self.assertEqual(report["default_colour_class"], "grey")

    def test_reference_quads_and_background(self):
        report, _missing = self.report_for((0, 0, 0))
        self.assertEqual(report["qB_rgb"], (255, 255, 255))
        self.assertEqual(report["qC_rgb"], (0, 0, 0))
        self.assertEqual(report["qD_rgb"], (132, 132, 132))
        self.assertEqual(report["qE_rgb"], (255, 0, 0))
        self.assertEqual(report["bg_rgb"], (66, 66, 66))

    def test_subprocess_report_and_exit_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "capture.png")
            synthetic_capture((255, 255, 255)).save(path)
            completed = subprocess.run(
                [sys.executable, MEASURE, path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        report = dict(
            line.split("=", 1) for line in completed.stdout.splitlines())
        for key in measure_vtxcolor.REPORT_KEYS:
            self.assertIn(key, report)
        self.assertEqual(report["qA_rgb"], "255,255,255")
        self.assertEqual(report["default_colour_class"], "same_as_qB")

    def test_geometry_not_found_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "flat.png")
            Image.new("RGB", (400, 240),
                      gen_quads.unpack_rgb15(QUADS["background5"])).save(path)
            completed = subprocess.run(
                [sys.executable, MEASURE, path],
                capture_output=True, text=True)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("geometry not found", completed.stderr)
        self.assertIn("geometry_found=0", completed.stdout)

    @unittest.skipUnless(
        os.path.exists(FIXTURE), "fixture sc1_5_crop400x480.png is missing")
    def test_mapping_matches_measure_scale_on_fixture(self):
        """capture_mapping.json must equal what measure_scale.py measures."""
        if SCALE_DIR not in sys.path:
            sys.path.insert(0, SCALE_DIR)
        import measure_scale  # noqa: E402

        with open(os.path.join(SCALE_DIR, "pattern.json"), "r",
                  encoding="utf-8") as f:
            pattern = json.load(f)
        with Image.open(FIXTURE) as image:
            result, _box, missing = measure_scale.measure(image, pattern)
        self.assertEqual(missing, [])
        self.assertEqual(result["scale_x"], MAPPING["scale_x"])
        self.assertEqual(result["scale_y"], MAPPING["scale_y"])
        self.assertEqual((result["offset_x"], result["offset_y"]),
                         (MAPPING["offset_x"], MAPPING["offset_y"]))


if __name__ == "__main__":
    unittest.main()
