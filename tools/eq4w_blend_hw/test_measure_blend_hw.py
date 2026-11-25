#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4w_blend_hw/measure_blend_hw.py.

Synthetic 400x240 captures paint the derived scene-4 rectangles with chosen
colours: the positive case dims the alpha-16/4 quads, the negative case leaves
the alpha-16 quad at full (0,255,0), which must classify as NOT blended.  The
two committed real-hardware fixtures are measured and their numbers asserted;
they are the EQ-4w FACTs.
"""

import hashlib
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_blend_hw.py")
FIXTURE_A = os.path.join(HERE, "hw_s4bl_a.png")
FIXTURE_B = os.path.join(HERE, "hw_s4bl_b.png")
NONBLEND = os.path.join(HERE, os.pardir, "fixtures", "eq4_hw_scenes",
                        "s4_a_top400x240.png")
FIXTURE_A_SHA256 = \
    "8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01"
FIXTURE_B_SHA256 = \
    "77d537869e0316e2eccfa931e7cb4d2636b19adbe6ab0a5c38e188047c87a9e6"

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_blend_hw  # noqa: E402

try:
    from PIL import Image, ImageDraw
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    ImageDraw = None
    HAVE_PIL = False

BACKGROUND = (0, 0, 0)


def render_capture(colours):
    """Paint each scene-4 derived rectangle with its given RGB colour."""
    image = Image.new("RGB", (measure_blend_hw.WIDTH,
                              measure_blend_hw.HEIGHT), BACKGROUND)
    draw = ImageDraw.Draw(image)
    rects = measure_blend_hw.scene4_rects(measure_blend_hw.load_mapping())
    for rect, colour in zip(rects, colours):
        draw.rectangle(measure_blend_hw.clamp_rect(rect), fill=colour)
    return image


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


@unittest.skipUnless(HAVE_PIL, "PIL is not available; PNG tests skipped")
class SyntheticCliTest(unittest.TestCase):
    def test_dimmed_quads_are_blended(self):
        # Blended captures: alpha 31 red stays 255, alpha 16 green 134,
        # alpha 4 blue 33 (the EQ-4i/eq4u expectation range).
        completed = run_cli(render_capture(
            ((255, 0, 0), (0, 134, 0), (0, 0, 33), (0, 0, 0))))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad0.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad1.measured"), ["134"])
        self.assertEqual(field(completed.stdout, "quad2.measured"), ["33"])

    def test_full_alpha16_quad_is_not_blended(self):
        # Negative case: a full (0,255,0) alpha-16 quad means NOT blended.
        completed = run_cli(render_capture(
            ((255, 0, 0), (0, 255, 0), (0, 0, 33), (0, 0, 0))))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,255,0"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["yes"])

    def test_all_full_quads_are_not_blended(self):
        completed = run_cli(render_capture(
            ((255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0))))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        for quad in ("quad0", "quad1", "quad2"):
            self.assertEqual(field(completed.stdout, quad + ".blended"), ["no"],
                             quad)

    def test_cli_is_deterministic(self):
        image = render_capture(((255, 0, 0), (0, 134, 0), (0, 0, 33),
                                (0, 0, 0)))
        first = run_cli(image)
        second = run_cli(image)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)

    def test_bad_size_and_usage_exit_two(self):
        completed = run_cli(Image.new("RGB", (10, 10), BACKGROUND))
        self.assertEqual(completed.returncode, 2)
        completed = run_cli(render_capture(((0, 0, 0),) * 4), "--nope")
        self.assertEqual(completed.returncode, 2)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class HardwareFixtureTest(unittest.TestCase):
    """The committed real-hardware crops; every number is an EQ-4w FACT."""

    def test_fixture_sha256(self):
        for path, expected in ((FIXTURE_A, FIXTURE_A_SHA256),
                               (FIXTURE_B, FIXTURE_B_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)

    def test_blend_fixture(self):
        completed = run_fixture(FIXTURE_A,
                                "--diff", FIXTURE_B)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad0.majority_rgb"),
                         ["255,0,0"])
        self.assertEqual(field(completed.stdout, "quad0.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,134,0"])
        self.assertEqual(field(completed.stdout, "quad1.measured"), ["134"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad2.majority_rgb"),
                         ["0,0,33"])
        self.assertEqual(field(completed.stdout, "quad2.measured"), ["33"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad3.bright_pixels"),
                         ["130"])
        self.assertEqual(field(completed.stdout, "quad3.bright_bbox"),
                         ["207,80,240,114"])
        self.assertEqual(field(completed.stdout, "quad3.interior_bright_pixels"),
                         ["84"])
        self.assertEqual(field(completed.stdout, "diff_pixels"), ["93"])
        self.assertIn("census=(0, 0, 33) 1348", completed.stdout)
        self.assertIn("census=(0, 134, 0) 468", completed.stdout)
        self.assertIn("census=(255, 0, 0) 892", completed.stdout)

    def test_non_blend_fixture(self):
        completed = run_fixture(NONBLEND)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad0.majority_rgb"),
                         ["255,0,0"])
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,255,0"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad2.majority_rgb"),
                         ["0,0,255"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad3.bright_pixels"),
                         ["138"])
        self.assertEqual(field(completed.stdout, "quad3.bright_bbox"),
                         ["207,80,240,114"])
        self.assertIn("census=(0, 0, 255) 1372", completed.stdout)

    def test_fixture_runs_are_deterministic(self):
        first = run_fixture(FIXTURE_A)
        second = run_fixture(FIXTURE_A)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
