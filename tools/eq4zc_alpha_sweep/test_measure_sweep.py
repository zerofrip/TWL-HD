#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4zc_alpha_sweep/measure_sweep.py.

Synthetic 400x240 crops paint the layout rectangles: the positive case dims
every quad below its source value, the negative case leaves every quad at full
source colour and must classify ``not blended``.  Both mapping presets are
tested.  The two committed Azahar crops are sha256-pinned and their per-alpha
values asserted (the EQ-4zc emulator FACTs).
"""

import contextlib
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_sweep as ms  # noqa: E402

try:
    from PIL import Image, ImageDraw
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    ImageDraw = None
    HAVE_PIL = False

FIXTURES = os.path.join(HERE, "fixtures")
GREEN_CROP = os.path.join(FIXTURES, "eq4zc_sweep_green_top400x240.png")
BLUE_CROP = os.path.join(FIXTURES, "eq4zc_sweep_blue_top400x240.png")
GREEN_SHA256 = "5207f2eee3339581a6cc16f31a9f3acd209dd4ede2b5016083519e614ffe5800"
BLUE_SHA256 = "f36134ca69f3ea6f3af58ead5bba3243bd1859a8738b49f8015514c69069c196"

# EQ-4zc emulator FACTs: the measured source channel for both sweeps.
MEASURED = (16, 40, 72, 104, 128, 135, 167, 199, 215, 231, 255)
ALPHAS = (1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31)


def load_layout(name):
    with open(os.path.join(FIXTURES, name), "r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_quads(stdout):
    quads = {}
    for line in stdout.splitlines():
        key, _, value = line.partition("=")
        if key.startswith("quad"):
            index, _, field = key[len("quad"):].partition(".")
            quads.setdefault(int(index), {})[field] = value
    return quads


def run_tool(layout, image, mapping="capture", correction="off"):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        code = ms.main(["--layout", layout, "--mapping", mapping,
                        "--azahar-correction", correction, image])
    return code, output.getvalue()


def render(layout, mapping, colour_for_alpha):
    image = Image.new("RGB", (ms.WIDTH, ms.HEIGHT), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    for quad in layout["quads"]:
        rect = ms.project_rect(quad["rect"], mapping)
        draw.rectangle([int(round(rect[0])), int(round(rect[1])),
                        int(round(rect[2])), int(round(rect[3]))],
                       fill=colour_for_alpha(quad["alpha"]))
    return image


@unittest.skipUnless(HAVE_PIL, "PIL is not available; synthetic tests skipped")
class SyntheticTest(unittest.TestCase):
    def layout_path(self, name="eq4zc_sweep_green_layout.json"):
        return os.path.join(FIXTURES, name)

    def test_positive_blended(self):
        layout = load_layout("eq4zc_sweep_green_layout.json")
        mapping = ms.load_mapping("capture")
        image = render(layout, mapping,
                       lambda alpha: (0, 255 - 5 * alpha, 0))
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "positive.png")
            image.save(path)
            code, stdout = run_tool(self.layout_path(), path)
        self.assertEqual(code, 0)
        quads = parse_quads(stdout)
        for index, alpha in enumerate(ALPHAS):
            self.assertEqual(quads[index]["alpha"], str(alpha))
            self.assertEqual(quads[index]["measured_green"],
                             str(255 - 5 * alpha))
            self.assertEqual(quads[index]["classification"], "blended")

    def test_negative_full_colour_is_not_blended(self):
        layout = load_layout("eq4zc_sweep_green_layout.json")
        mapping = ms.load_mapping("capture")
        image = render(layout, mapping, lambda _alpha: (0, 255, 0))
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "negative.png")
            image.save(path)
            code, stdout = run_tool(self.layout_path(), path)
        self.assertEqual(code, 0)
        quads = parse_quads(stdout)
        for index in range(len(ALPHAS)):
            self.assertEqual(quads[index]["classification"], "not blended")

    def test_mapping_presets_agree(self):
        capture = ms.load_mapping("capture")
        arm11 = ms.load_mapping("arm11")
        for key in ("scale_x", "scale_y", "offset_x", "offset_y"):
            self.assertEqual(capture[key], arm11[key], key)
        interior = ms.interior_rect(ms.project_rect((19, 48, 47, 87), arm11))
        self.assertEqual(interior, (66, 62, 97, 107))

    def test_projected_interiors_separated(self):
        for name in ("eq4zc_sweep_green_layout.json",
                     "eq4zc_sweep_blue_layout.json"):
            layout = load_layout(name)
            mapping = ms.load_mapping("arm11")
            rects = [ms.interior_rect(ms.project_rect(quad["rect"], mapping))
                     for quad in layout["quads"]]
            for x0, y0, x1, y1 in rects:
                self.assertGreaterEqual(x0, 4)
                self.assertGreaterEqual(y0, 4)
                self.assertLessEqual(x1, ms.WIDTH - 5)
                self.assertLessEqual(y1, ms.HEIGHT - 5)
            for i in range(len(rects)):
                for j in range(i + 1, len(rects)):
                    a = rects[i]
                    b = rects[j]
                    separated = (a[2] + 4 <= b[0] or b[2] + 4 <= a[0] or
                                 a[3] + 4 <= b[1] or b[3] + 4 <= a[1])
                    self.assertTrue(separated, (name, a, b))

    def test_cli_both_mappings_same_interiors(self):
        layout = load_layout("eq4zc_sweep_green_layout.json")
        image = render(layout, ms.load_mapping("arm11"),
                       lambda _alpha: (0, 255, 0))
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "negative.png")
            image.save(path)
            outputs = []
            for mapping in ms.MAPPING_NAMES:
                code, stdout = run_tool(self.layout_path(), path, mapping)
                self.assertEqual(code, 0)
                outputs.append(parse_quads(stdout))
        for index in range(len(ALPHAS)):
            self.assertEqual(outputs[0][index]["interior"],
                             outputs[1][index]["interior"])


class FixtureTest(unittest.TestCase):
    def test_fixture_sha256(self):
        for path, expected in ((GREEN_CROP, GREEN_SHA256),
                               (BLUE_CROP, BLUE_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)

    @unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
    def test_emulator_crops(self):
        for crop, layout_name, source_channel in (
                (GREEN_CROP, "eq4zc_sweep_green_layout.json", "green"),
                (BLUE_CROP, "eq4zc_sweep_blue_layout.json", "blue")):
            layout = os.path.join(FIXTURES, layout_name)
            code, stdout = run_tool(layout, crop, "arm11", "on")
            self.assertEqual(code, 0)
            self.assertIn("presentation-row correction: applied", stdout)
            quads = parse_quads(stdout)
            for index, alpha in enumerate(ALPHAS):
                self.assertEqual(quads[index]["alpha"], str(alpha))
                if source_channel == "green":
                    self.assertEqual(quads[index]["measured_green"],
                                     str(MEASURED[index]))
                    self.assertEqual(quads[index]["measured_blue"], "0")
                else:
                    self.assertEqual(quads[index]["measured_green"], "0")
                    self.assertEqual(quads[index]["measured_blue"],
                                     str(MEASURED[index]))
                expected = "not blended" if alpha == 31 else "blended"
                self.assertEqual(quads[index]["classification"], expected)


if __name__ == "__main__":
    unittest.main()
