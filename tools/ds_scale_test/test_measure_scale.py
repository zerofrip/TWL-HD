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
