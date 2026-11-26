#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the EQ-4za black-clear arm11 hardware fixtures.

The two committed real-hardware crops are measured with the unmodified
`tools/eq4w_blend_hw/measure_blend_hw.py` and their numbers asserted; they are
the EQ-4za FACTs: the background clears to `(0,0,0)`, the non-blend capture
paints the alpha-16 and alpha-4 quads opaque, and the blend capture dims them
(green 138, blue 41).  A negative test checks that the older EQ-4y crop, which
clears to `(16,16,24)`, is not classified as a black background.
"""

import hashlib
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, os.pardir, "eq4w_blend_hw", "measure_blend_hw.py")
NB = os.path.join(HERE, "hw_eq4za_nb_a.png")
BL = os.path.join(HERE, "hw_eq4za_bl_a.png")
OLD_NB = os.path.join(HERE, os.pardir, "eq4y_gate_hw", "hw_eq4x_nb_a.png")
NB_SHA256 = "1b6c9aaa99265601d1f35f27d9025f33d663f75bcff40eef6b31ce7fb68568f8"
BL_SHA256 = "17245fa9ab9ff5ef129cf082af9a899e186b3a30dd23ebb4c148d78a145bcf3c"

try:
    from PIL import Image  # noqa: F401
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_PIL = False


def run_fixture(path):
    return subprocess.run([sys.executable, MEASURE, path],
                          capture_output=True, text=True)


def field(stdout, key):
    return [line.split("=", 1)[1] for line in stdout.splitlines()
            if line.split("=", 1)[0] == key]


class FixtureShaTest(unittest.TestCase):
    def test_fixture_sha256(self):
        for path, expected in ((NB, NB_SHA256), (BL, BL_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class HardwareFixtureTest(unittest.TestCase):
    """The committed real-hardware crops; every number is an EQ-4za FACT."""

    def test_non_blend_fixture(self):
        completed = run_fixture(NB)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,255,0"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad2.majority_rgb"),
                         ["0,0,255"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["no"])
        self.assertEqual(field(completed.stdout, "quad3.bright_pixels"),
                         ["12"])
        self.assertEqual(field(completed.stdout,
                               "quad3.interior_bright_pixels"), ["1"])
        self.assertIn("census=(0, 0, 0) 92968", completed.stdout)
        self.assertIn("census=(0, 0, 255) 1407", completed.stdout)
        self.assertIn("census=(255, 0, 0) 982", completed.stdout)
        self.assertIn("census=(0, 255, 0) 642", completed.stdout)
        self.assertIn("census=(16, 0, 0) 1", completed.stdout)

    def test_blend_fixture(self):
        completed = run_fixture(BL)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,138,0"])
        self.assertEqual(field(completed.stdout, "quad1.measured"), ["138"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad2.majority_rgb"),
                         ["0,0,41"])
        self.assertEqual(field(completed.stdout, "quad2.measured"), ["41"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad3.bright_pixels"),
                         ["12"])
        self.assertEqual(field(completed.stdout,
                               "quad3.interior_bright_pixels"), ["1"])
        self.assertIn("census=(0, 0, 0) 92968", completed.stdout)
        self.assertIn("census=(0, 0, 41) 1407", completed.stdout)
        self.assertIn("census=(255, 0, 0) 982", completed.stdout)
        self.assertIn("census=(0, 138, 0) 642", completed.stdout)
        self.assertIn("census=(16, 0, 0) 1", completed.stdout)

    def test_old_crop_is_not_black_background(self):
        completed = run_fixture(OLD_NB)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn("census=(0, 0, 0)", completed.stdout)
        self.assertIn("census=(16, 16, 24) 92969", completed.stdout)

    def test_fixture_runs_are_deterministic(self):
        first = run_fixture(NB)
        second = run_fixture(NB)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
