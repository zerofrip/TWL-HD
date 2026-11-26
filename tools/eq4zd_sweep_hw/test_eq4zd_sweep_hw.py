#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the EQ-4zd real-hardware alpha-sweep crops.

The two committed crops are the owner's hardware captures of the EQ-4zc sweep
frames.  They are measured with the unmodified
`tools/eq4zc_alpha_sweep/measure_sweep.py` (`--mapping arm11
--azahar-correction off`) and the 11+11 measured values are asserted; they are
the EQ-4zd FACTs (green 138 / blue 140 at alpha 16, where the emulator gave
135/135).  A negative test measures the committed EQ-4zc emulator crops and
asserts the hardware series differs, so the two cannot be silently swapped.
"""

import hashlib
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
MEASURE = os.path.join(TOOLS, "eq4zc_alpha_sweep", "measure_sweep.py")
FIXTURES = os.path.join(TOOLS, "eq4zc_alpha_sweep", "fixtures")
GREEN_CROP = os.path.join(HERE, "hw_eq4zd_sweep_green_a.png")
BLUE_CROP = os.path.join(HERE, "hw_eq4zd_sweep_blue_a.png")
EMU_GREEN = os.path.join(FIXTURES, "eq4zc_sweep_green_top400x240.png")
EMU_BLUE = os.path.join(FIXTURES, "eq4zc_sweep_blue_top400x240.png")
GREEN_LAYOUT = os.path.join(FIXTURES, "eq4zc_sweep_green_layout.json")
BLUE_LAYOUT = os.path.join(FIXTURES, "eq4zc_sweep_blue_layout.json")
GREEN_SHA256 = "6094a04de35a7fb8db97c4766c06a30ff3e2314118df78196a70c16852a56760"
BLUE_SHA256 = "656b9910a2211d14ee89acb3f5ad658310bc857bfb917d7fd1b9343dd86116c5"

ALPHAS = (1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31)
# EQ-4zd hardware FACTs: the measured source channel of each sweep.
HW_GREEN = (16, 40, 73, 105, 130, 138, 170, 203, 219, 235, 255)
HW_BLUE = (16, 41, 74, 107, 132, 140, 173, 206, 222, 239, 255)
# EQ-4zc emulator FACTs (docs/reports/eq4zc-alpha-sweep.md:338-350).
EMU_SERIES = (16, 40, 72, 104, 128, 135, 167, 199, 215, 231, 255)
ALPHA16_GREEN_EMU = 135
ALPHA16_GREEN_HW = 138

try:
    from PIL import Image  # noqa: F401
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_PIL = False


def run_tool(layout, image, correction):
    return subprocess.run(
        [sys.executable, MEASURE, "--layout", layout, "--mapping", "arm11",
         "--azahar-correction", correction, image],
        capture_output=True, text=True)


def parse_quads(stdout):
    quads = {}
    for line in stdout.splitlines():
        key, _, value = line.partition("=")
        if key.startswith("quad"):
            index, _, field = key[len("quad"):].partition(".")
            quads.setdefault(int(index), {})[field] = value
    return quads


class FixtureShaTest(unittest.TestCase):
    def test_fixture_sha256(self):
        for path, expected in ((GREEN_CROP, GREEN_SHA256),
                               (BLUE_CROP, BLUE_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class HardwareSweepTest(unittest.TestCase):
    """The committed real-hardware crops; every number is an EQ-4zd FACT."""

    def measure(self, layout, crop):
        completed = run_tool(layout, crop, "off")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("presentation-row correction: not applied",
                      completed.stdout)
        return parse_quads(completed.stdout)

    def test_green_crop(self):
        quads = self.measure(GREEN_LAYOUT, GREEN_CROP)
        self.assertEqual(len(quads), len(ALPHAS))
        for index, alpha in enumerate(ALPHAS):
            self.assertEqual(quads[index]["alpha"], str(alpha))
            self.assertEqual(quads[index]["measured_green"],
                             str(HW_GREEN[index]))
            self.assertEqual(quads[index]["measured_blue"], "0")
            expected = "not blended" if alpha == 31 else "blended"
            self.assertEqual(quads[index]["classification"], expected)

    def test_blue_crop(self):
        quads = self.measure(BLUE_LAYOUT, BLUE_CROP)
        self.assertEqual(len(quads), len(ALPHAS))
        for index, alpha in enumerate(ALPHAS):
            self.assertEqual(quads[index]["alpha"], str(alpha))
            self.assertEqual(quads[index]["measured_green"], "0")
            self.assertEqual(quads[index]["measured_blue"],
                             str(HW_BLUE[index]))
            expected = "not blended" if alpha == 31 else "blended"
            self.assertEqual(quads[index]["classification"], expected)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class NegativeTest(unittest.TestCase):
    """The EQ-4zc emulator crops must not measure as the hardware series."""

    def test_emulator_green_differs_from_hardware(self):
        completed = run_tool(GREEN_LAYOUT, EMU_GREEN, "on")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        quads = parse_quads(completed.stdout)
        series = tuple(int(quads[i]["measured_green"])
                       for i in range(len(ALPHAS)))
        self.assertEqual(series, EMU_SERIES)
        self.assertNotEqual(series, HW_GREEN)
        self.assertEqual(series[5], ALPHA16_GREEN_EMU)
        self.assertEqual(HW_GREEN[5], ALPHA16_GREEN_HW)
        self.assertNotEqual(ALPHA16_GREEN_EMU, ALPHA16_GREEN_HW)

    def test_emulator_blue_differs_from_hardware(self):
        completed = run_tool(BLUE_LAYOUT, EMU_BLUE, "on")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        quads = parse_quads(completed.stdout)
        series = tuple(int(quads[i]["measured_blue"])
                       for i in range(len(ALPHAS)))
        self.assertNotEqual(series, HW_BLUE)


if __name__ == "__main__":
    unittest.main()
