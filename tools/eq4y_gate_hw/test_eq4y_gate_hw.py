#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the EQ-4y arm11 blend-gate hardware fixtures.

The two committed real-hardware crops are measured with the unmodified
`tools/eq4w_blend_hw/measure_blend_hw.py` and their numbers asserted; they are
the EQ-4y FACTs: the non-blend capture paints the alpha-16 and alpha-4 quads
opaque, the blend capture dims them (green 142, blue 57), and both alpha-0
quads are invisible in the interior.
"""

import hashlib
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, os.pardir, "eq4w_blend_hw", "measure_blend_hw.py")
NB = os.path.join(HERE, "hw_eq4x_nb_a.png")
BL = os.path.join(HERE, "hw_eq4x_bl_a.png")
NB_SHA256 = "deb38c18a5bb996062666032f028f85285082e1784f2fc6d1b2b430253ee4994"
BL_SHA256 = "b69fe759b2ea71e5478c161e62ab91e66e43cc753083ded41f687ee33cbc48de"

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
    """The committed real-hardware crops; every number is an EQ-4y FACT."""

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
        self.assertIn("census=(16, 16, 24) 92969", completed.stdout)
        self.assertIn("census=(0, 0, 255) 1407", completed.stdout)
        self.assertIn("census=(255, 0, 0) 982", completed.stdout)
        self.assertIn("census=(0, 255, 0) 642", completed.stdout)

    def test_blend_fixture(self):
        completed = run_fixture(BL)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "quad1.majority_rgb"),
                         ["0,142,8"])
        self.assertEqual(field(completed.stdout, "quad1.measured"), ["142"])
        self.assertEqual(field(completed.stdout, "quad1.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad2.majority_rgb"),
                         ["8,12,57"])
        self.assertEqual(field(completed.stdout, "quad2.measured"), ["57"])
        self.assertEqual(field(completed.stdout, "quad2.blended"), ["yes"])
        self.assertEqual(field(completed.stdout, "quad3.bright_pixels"),
                         ["12"])
        self.assertEqual(field(completed.stdout,
                               "quad3.interior_bright_pixels"), ["1"])
        self.assertIn("census=(16, 16, 24) 92969", completed.stdout)
        self.assertIn("census=(8, 12, 57) 1407", completed.stdout)
        self.assertIn("census=(255, 0, 0) 982", completed.stdout)
        self.assertIn("census=(0, 142, 8) 642", completed.stdout)

    def test_fixture_runs_are_deterministic(self):
        first = run_fixture(NB)
        second = run_fixture(NB)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
