#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_scale_test/measure_scale.py.

Synthesises the ds-scale-test pattern, nearest-neighbour scales it into a
400x240 capture and checks the measured scale/offset within 0.5 capture px.
Skips with a labelled reason when PIL is not installed.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_scale.py")
FIXTURE = os.path.join(
    HERE, "fixtures", "sc1_5_crop400x480_misdecoded.png")
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import gen_pattern  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

with open(os.path.join(HERE, "pattern.json"), "r", encoding="utf-8") as _f:
    PATTERN = json.load(_f)


def pattern_image():
    return Image.frombytes(
        "RGB", (PATTERN["width"], PATTERN["height"]),
        gen_pattern.render_rgb(PATTERN))


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; measure_scale capture tests skipped")
class MeasureScaleTest(unittest.TestCase):
    def assert_geometry(self, result, expect_w, expect_h, offset_x, offset_y):
        self.assertEqual(result["frame_found"], 1)
        self.assertEqual(result["offset_x"], offset_x)
        self.assertEqual(result["offset_y"], offset_y)
        self.assertLessEqual(
            abs(result["scale_x"] * PATTERN["width"] - expect_w), 0.5)
        self.assertLessEqual(
            abs(result["scale_y"] * PATTERN["height"] - expect_h), 0.5)
        for key in ("corner_red", "corner_green", "corner_blue",
                    "corner_yellow"):
            self.assertEqual(result[key], 1, key)

    def test_nearest_neighbour_400x240(self):
        capture = Image.new("RGB", (1280, 720), (0, 0, 0))
        capture.paste(
            pattern_image().resize((400, 240), Image.NEAREST), (0, 0))
        result, _box, missing = measure(PATTERN, capture)
        self.assertEqual(missing, [])
        self.assert_geometry(result, 400, 240, 0, 0)
        self.assertGreater(result["strip_h_contrast"], 64)
        self.assertGreater(result["strip_v_contrast"], 64)

    def test_centred_1x(self):
        capture = Image.new("RGB", (400, 240), (0, 0, 0))
        capture.paste(pattern_image(), (72, 24))
        result, _box, missing = measure(PATTERN, capture)
        self.assertEqual(missing, [])
        self.assert_geometry(result, 256, 192, 72, 24)

    @unittest.skipUnless(
        os.path.exists(FIXTURE),
        "fixture sc1_5_crop400x480_misdecoded.png is missing")
    def test_misdecoded_capture_measures_old_numbers(self):
        """Regression pin for the BUG, not a hardware measurement: this capture
        was decoded as 32-bpp BGRX (see xwd2png.py), so its 0.9375/30 numbers
        are the mis-decoded input, kept here so they cannot silently change."""
        with Image.open(FIXTURE) as image:
            result, _box, missing = measure(PATTERN, image)
        self.assertEqual(missing, [])
        self.assertEqual(result["frame_found"], 1)
        for key in ("corner_red", "corner_green", "corner_blue",
                    "corner_yellow"):
            self.assertEqual(result[key], 1, key)
        self.assertEqual(
            (result["bbox_x0"], result["bbox_y0"],
             result["bbox_x1"], result["bbox_y1"]), (30, 0, 269, 239))
        self.assertEqual(result["scale_x"], 0.9375)
        self.assertEqual(result["scale_y"], 1.25)
        self.assertEqual((result["offset_x"], result["offset_y"]), (30, 0))
        for key in ("scale_x_check", "scale_y_check",
                    "offset_x_check", "offset_y_check"):
            self.assertLess(abs(result[key]), 1.0, key)

    @unittest.skipUnless(
        os.path.exists(FIXTURE),
        "fixture sc1_5_crop400x480_misdecoded.png is missing")
    def test_misdecoded_capture_exits_zero(self):
        """The mis-decoded regression sample still analyses cleanly (old exit
        status), labelled mis-decoded input — it is not a hardware fact."""
        completed = subprocess.run(
            [sys.executable, MEASURE, FIXTURE],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn("not found", completed.stderr)
        report = dict(
            line.split("=", 1) for line in completed.stdout.splitlines())
        self.assertEqual(report["frame_found"], "1")
        self.assertEqual(report["scale_x"], "0.937500")
        self.assertEqual(report["scale_y"], "1.250000")

    def test_missing_markers_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "black.png")
            Image.new("RGB", (400, 240), (0, 0, 0)).save(path)
            completed = subprocess.run(
                [sys.executable, MEASURE, path],
                capture_output=True, text=True)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("not found", completed.stderr)
        self.assertIn("frame_found=0", completed.stdout)


def measure(pattern, capture):
    import measure_scale
    return measure_scale.measure(capture, pattern)


if __name__ == "__main__":
    unittest.main()
