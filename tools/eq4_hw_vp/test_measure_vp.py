#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4_hw_vp/measure_vp.py.

The synthetic captures exercise the derived viewport rectangle: a figure
inside it contributes zero outside pixels, a figure crossing its left edge has
an exact outside count.  The three committed EQ-4zg fixtures are measured and
their hashes and numbers asserted; they are the FACTs of
``docs/reports/eq4zg-partial-viewport.md``.
"""

import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_vp.py")
FIXTURES = os.path.join(HERE, os.pardir, "fixtures", "eq4_hw_vp")
HW_FIXTURE = os.path.join(FIXTURES, "hw_vp_s4_a_top400x240.png")
HARNESS_FIXTURE = os.path.join(FIXTURES, "harness_vp_s4_st30_f240.png")
OURS_FIXTURE = os.path.join(FIXTURES, "ours_vp_s4_st30_top400x240.png")
HW_SHA256 = "8d108cea597d40a8ade862573b039c24892c33d9bd715990519c29ef960a6f33"
HARNESS_SHA256 = \
    "1a21e797c34b4ed1b9920ba21bc5aeae23762fc92028e85deeedffe86ccfec3b"
OURS_SHA256 = \
    "52be0c6fa49f08e13fb3db4327d1f34b1383898a58c75f903d69711b888dd188"

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_vp  # noqa: E402

try:
    from PIL import Image, ImageDraw
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    ImageDraw = None
    HAVE_PIL = False

BACKGROUND = (0, 0, 0)
FILL = (255, 0, 0)


def run_cli(image, *extra):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "capture.png")
        image.save(path)
        return subprocess.run([sys.executable, MEASURE] + list(extra) + [path],
                              capture_output=True, text=True)


def run_fixture(path, *extra):
    return subprocess.run([sys.executable, MEASURE] + list(extra) + [path],
                          capture_output=True, text=True)


def field(stdout, key):
    values = [line.split("=", 1)[1] for line in stdout.splitlines()
              if line.split("=", 1)[0] == key]
    return values


def blank(width, height):
    return Image.new("RGB", (width, height), BACKGROUND)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; PNG tests skipped")
class SyntheticCliTest(unittest.TestCase):
    def test_viewport_rect_derivation(self):
        mapping = measure_vp.load_mapping()
        # VP (32,96)-(159,191), y from bottom: DS top-down rows 0..95.
        self.assertEqual(measure_vp.viewport_rect(mapping, False),
                         (80, 0, 239, 119))
        self.assertEqual(measure_vp.viewport_rect(mapping, True),
                         (32, 0, 159, 95))

    def test_figure_inside_viewport_has_no_outside_pixels(self):
        image = blank(400, 240)
        ImageDraw.Draw(image).rectangle((100, 10, 150, 50), fill=FILL)
        completed = run_cli(image)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["2091"])
        self.assertEqual(field(completed.stdout, "bbox"), ["100,10,150,50"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["0"])
        self.assertEqual(field(completed.stdout, "outside_viewport_bbox"),
                         ["none"])

    def test_figure_crossing_left_edge_has_exact_outside_count(self):
        # Columns 70..79 of rows 0..10 are outside capture x >= 80:
        # 10 * 11 = 110 pixels.
        image = blank(400, 240)
        ImageDraw.Draw(image).rectangle((70, 0, 90, 10), fill=FILL)
        completed = run_cli(image)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["231"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["110"])
        self.assertEqual(field(completed.stdout, "outside_viewport_bbox"),
                         ["70,0,79,10"])

    def test_native_figure_outside_viewport(self):
        image = blank(256, 192)
        ImageDraw.Draw(image).rectangle((0, 0, 31, 191), fill=FILL)
        completed = run_cli(image)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "viewport_rect"),
                         ["32,0,159,95"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["6144"])

    def test_blank_image_has_no_bbox(self):
        completed = run_cli(blank(400, 240))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["0"])
        self.assertEqual(field(completed.stdout, "bbox"), ["none"])

    def test_diff_counts_pixels(self):
        first = blank(400, 240)
        second = blank(400, 240)
        second.putpixel((5, 7), FILL)
        with tempfile.TemporaryDirectory() as tmp:
            second_path = os.path.join(tmp, "second.png")
            second.save(second_path)
            completed = run_cli(first, "--diff", second_path)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "diff_pixels"), ["1"])
        self.assertEqual(field(completed.stdout, "diff_bbox"), ["5,7,5,7"])

    def test_bad_size_and_usage_exit_two(self):
        completed = run_cli(blank(10, 10))
        self.assertEqual(completed.returncode, 2)
        completed = run_cli(blank(400, 240), "--nope")
        self.assertEqual(completed.returncode, 2)
        completed = run_cli(blank(400, 240), "--diff")
        self.assertEqual(completed.returncode, 2)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class FixtureTest(unittest.TestCase):
    """The committed EQ-4zg fixtures; every number is an EQ-4zg FACT."""

    def test_fixture_sha256(self):
        for path, expected in ((HW_FIXTURE, HW_SHA256),
                               (HARNESS_FIXTURE, HARNESS_SHA256),
                               (OURS_FIXTURE, OURS_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)

    def test_hardware_fixture(self):
        completed = run_fixture(HW_FIXTURE)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "viewport_rect"),
                         ["80,0,239,119"])
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["1072"])
        self.assertEqual(field(completed.stdout, "bbox"), ["131,38,179,80"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["0"])

    def test_harness_fixture(self):
        completed = run_fixture(HARNESS_FIXTURE)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "mode"), ["native"])
        self.assertEqual(field(completed.stdout, "viewport_rect"),
                         ["32,0,159,95"])
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["490"])
        self.assertEqual(field(completed.stdout, "bbox"), ["74,36,111,64"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["0"])

    def test_ours_fixture(self):
        completed = run_fixture(OURS_FIXTURE)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "nonbg_pixels"), ["762"])
        self.assertEqual(field(completed.stdout, "bbox"), ["132,46,178,81"])
        self.assertEqual(field(completed.stdout, "outside_viewport_pixels"),
                         ["0"])

    def test_fixture_runs_are_deterministic(self):
        first = run_fixture(HW_FIXTURE)
        second = run_fixture(HW_FIXTURE)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
