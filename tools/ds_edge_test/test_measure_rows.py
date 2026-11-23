#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_edge_test/measure_rows.py.

Synthetic native 256x192 frames are rendered directly from quads.json with a
known whole-pixel row shift (0, +1, or +1 for the interior items only), so
the classifier is checked against ground truth.  No emulator numbers are
pinned: the out-of-tree harness PPM is not reproducible from committed files.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_rows.py")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_rows  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

with open(os.path.join(HERE, "quads.json"), "r", encoding="utf-8") as _f:
    DATA = json.load(_f)
with open(os.path.join(HERE, "capture_mapping.json"), "r",
          encoding="utf-8") as _f:
    MAPPING = json.load(_f)

WIDTH = DATA["width"]
HEIGHT = DATA["height"]
INTERIOR = ("sq1", "sq2", "sq3", "sq4", "hline", "vline")


def render_native(shifts=None, default_shift=0):
    """Render the white quads at nominal + per-item whole-pixel row shifts."""
    shifts = shifts or {}
    pixels = [(0, 0, 0)] * (WIDTH * HEIGHT)
    for quad in DATA["quads"]:
        dy = shifts.get(quad["name"], default_shift)
        y0 = max(0, quad["y"] + dy)
        y1 = min(HEIGHT, quad["y"] + quad["h"] + dy)
        x0 = max(0, quad["x"])
        x1 = min(WIDTH, quad["x"] + quad["w"])
        for y in range(y0, y1):
            for x in range(x0, x1):
                pixels[y * WIDTH + x] = (255, 255, 255)
    return pixels


def by_name(result):
    return {entry["name"]: entry for entry in result["items"]}


def write_ppm(path, pixels):
    data = bytearray()
    for r, g, b in pixels:
        data.extend((r, g, b))
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT))
        handle.write(data)


def render_capture(shifts=None, default_shift=0):
    """400x240 capture PNG from the native frame through capture_mapping.json."""
    image = Image.new("RGB", (WIDTH, HEIGHT))
    image.putdata(render_native(shifts, default_shift))
    scaled = image.resize(
        (int(round(WIDTH * MAPPING["scale_x"])),
         int(round(HEIGHT * MAPPING["scale_y"]))), Image.NEAREST)
    canvas = Image.new("RGB", (400, 240), (0, 0, 0))
    canvas.paste(scaled, (int(MAPPING["offset_x"]), int(MAPPING["offset_y"])))
    return canvas


def row_offsets(stdout):
    """item name -> row_offset from the capture-mode output."""
    offsets = {}
    for line in stdout.splitlines():
        if line.startswith("item=") and " row_offset=" in line:
            name, value = line.split(" ", 1)
            offsets[name[len("item="):]] = float(value.split("=", 1)[1])
    return offsets


class NativeClassificationTest(unittest.TestCase):
    def test_geometry_is_inside_the_frame(self):
        for item in DATA["quads"]:
            self.assertGreaterEqual(item["x"], 0, item["name"])
            self.assertGreaterEqual(item["y"], 0, item["name"])
            self.assertLessEqual(item["x"] + item["w"], WIDTH, item["name"])
            self.assertLessEqual(item["y"] + item["h"], HEIGHT, item["name"])

    def test_nominal_frame_classified_none(self):
        result = measure_rows.analyse_native(render_native(), WIDTH, HEIGHT,
                                             DATA)
        self.assertEqual(result["classification"], "none")
        self.assertEqual(result["first_white_row"], 0)
        self.assertEqual(result["last_white_row"], 191)
        self.assertEqual(result["first_white_col"], 0)
        self.assertEqual(result["last_white_col"], 255)
        self.assertEqual(result["frame_vertical_shift"], "none")
        self.assertEqual(result["frame_horizontal_shift"], "none")
        for entry in result["items"]:
            self.assertEqual(entry["row_diff"], 0, entry["name"])
            self.assertEqual(entry["row_end_diff"], 0, entry["name"])
            self.assertEqual(entry["col_diff"], 0, entry["name"])
            self.assertEqual(entry["col_end_diff"], 0, entry["name"])
            self.assertEqual(entry["measured_rows"], entry["nominal_rows"],
                             entry["name"])
        top = by_name(result)["top_band"]
        self.assertEqual(top["nominal_rows"], (0, 15))

    def test_uniform_one_row_shift_classified_uniform(self):
        result = measure_rows.analyse_native(
            render_native(default_shift=1), WIDTH, HEIGHT, DATA)
        self.assertEqual(result["classification"], "uniform+1")
        self.assertEqual(result["first_white_row"], 1)
        self.assertEqual(result["last_white_row"], 191)
        self.assertEqual(result["frame_vertical_shift"], "low-edge-clear")
        items = by_name(result)
        top = items["top_band"]
        self.assertEqual(top["measured_rows"], (1, 16))
        self.assertEqual(top["row_diff"], 1)
        self.assertEqual(top["row_end_diff"], 1)
        bottom = items["bottom_band"]
        self.assertEqual(bottom["measured_rows"], (177, 191))
        self.assertEqual(bottom["row_diff"], 1)
        self.assertEqual(bottom["row_end_diff"], 0)
        self.assertEqual(bottom["row_count"], 15)
        for entry in result["items"]:
            self.assertEqual(entry["row_diff"], 1, entry["name"])

    def test_interior_only_shift_classified_partial(self):
        shifts = {name: 1 for name in INTERIOR}
        result = measure_rows.analyse_native(render_native(shifts), WIDTH,
                                             HEIGHT, DATA)
        self.assertEqual(result["classification"], "partial+1")
        self.assertEqual(result["first_white_row"], 0)
        self.assertEqual(result["frame_vertical_shift"], "none")
        items = by_name(result)
        self.assertEqual(items["top_band"]["row_diff"], 0)
        self.assertEqual(items["bottom_band"]["row_diff"], 0)
        for name in INTERIOR:
            self.assertEqual(items[name]["row_diff"], 1, name)

    def test_negative_control_uniform_is_not_none(self):
        result = measure_rows.analyse_native(
            render_native(default_shift=1), WIDTH, HEIGHT, DATA)
        self.assertNotEqual(result["classification"], "none")
        self.assertNotEqual(result["classification"], "partial+1")

    def test_negative_control_nominal_is_not_shifted(self):
        result = measure_rows.analyse_native(render_native(), WIDTH, HEIGHT,
                                             DATA)
        self.assertNotEqual(result["classification"], "uniform+1")
        self.assertEqual(measure_rows.classify_offsets(
            {"top_band": 1, "bottom_band": 0, "sq1": 0}), "mixed")

    def test_classifier_edge_cases(self):
        self.assertEqual(measure_rows.classify_offsets({}), "no-data")
        self.assertEqual(measure_rows.classify_offsets(
            {"top_band": 0, "bottom_band": 0, "sq1": 0}), "none")
        self.assertEqual(measure_rows.classify_offsets(
            {"top_band": 1, "bottom_band": 1, "sq1": 1}), "uniform+1")
        self.assertEqual(measure_rows.classify_offsets(
            {"top_band": 0, "bottom_band": 0, "sq1": 1}), "partial+1")
        self.assertEqual(measure_rows.classify_offsets(
            {"top_band": 0, "bottom_band": 0, "sq1": 2}), "mixed")


class CliTest(unittest.TestCase):
    def test_cli_deterministic_and_reports_keys(self):
        pixels = render_native()
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "frame.ppm")
            write_ppm(path, pixels)
            first = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
            second = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        for line in ("mode=native", "first_white_row=0", "last_white_row=191",
                     "first_white_col=0", "last_white_col=255",
                     "frame_vertical_shift=none", "frame_horizontal_shift=none",
                     "item=top_band nominal_rows=0-15", "classification=none"):
            self.assertIn(line, first.stdout, line)

    def test_cli_rejects_wrong_native_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tiny.ppm")
            with open(path, "wb") as handle:
                handle.write(b"P6\n2 2\n255\n" + b"\0" * 12)
            completed = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 2)
        self.assertIn("native frame must be 256x192", completed.stderr)

    def test_cli_nominal_frame_has_no_classification_note(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "frame.ppm")
            write_ppm(path, render_native())
            completed = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("classification=none", completed.stdout)
        self.assertNotIn("classification_note", completed.stdout)

    def test_cli_uniform_frame_prints_classification_note(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "frame.ppm")
            write_ppm(path, render_native(default_shift=1))
            completed = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("classification=uniform+1", completed.stdout)
        self.assertIn("classification_note", completed.stdout)


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; capture-PNG tests skipped")
class CaptureCliTest(unittest.TestCase):
    """The capture-PNG CLI path (400x240 crop, no --native)."""

    def run_capture(self, image):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "capture.png")
            image.save(path)
            return subprocess.run(
                [sys.executable, MEASURE, path],
                capture_output=True, text=True)

    def test_capture_nominal_classified_none(self):
        completed = self.run_capture(render_capture())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("mode=capture", completed.stdout)
        self.assertIn("classification=none", completed.stdout)
        offsets = row_offsets(completed.stdout)
        # hline/vline are legitimately skipped by measure_edges (README §5).
        for name in ("top_band", "sq1"):
            self.assertIn(name, offsets, name)
            self.assertLess(abs(offsets[name]), 0.3, name)

    def test_capture_uniform_one_row_shift_classified_uniform(self):
        completed = self.run_capture(render_capture(default_shift=1))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("classification=uniform+1", completed.stdout)
        offsets = row_offsets(completed.stdout)
        for name in ("top_band", "sq1"):
            self.assertIn(name, offsets, name)
            self.assertLess(abs(offsets[name] - 0.8), 0.3, name)
            self.assertEqual(int(round(offsets[name])), 1, name)


if __name__ == "__main__":
    unittest.main()
