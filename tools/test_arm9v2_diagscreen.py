#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Makefile-level validation of the arm9v2 DIAGSCREEN option."""

import os
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class TestArm9v2DiagScreen(unittest.TestCase):
    """Option validation for DIAGSCREEN=1..6 build variants."""

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_diagscreen_bad_values_fail(self):
        for bad in ("abc", "-1", "1.5", "9", "01", "7", "1 2"):
            completed = subprocess.run(
                ["make", "-C", "arm9v2", "-n", "DIAGSCREEN=%s" % bad],
                cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(completed.returncode, 0, bad)
            self.assertIn(
                "DIAGSCREEN must be empty, 0, or a decimal integer in 1..6",
                completed.stderr)

    def test_diagscreen_whitespace_values_fail(self):
        # Leading/trailing spaces are stripped by Make's command-line parser,
        # so whitespace-containing values must be passed through the environment
        # to be detected by the raw-vs-stripped check.
        for bad in (" ", " 5", "5 "):
            env = os.environ.copy()
            env["DIAGSCREEN"] = bad
            completed = subprocess.run(
                ["make", "-C", "arm9v2", "-n"],
                cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertNotEqual(completed.returncode, 0, bad)
            self.assertIn(
                "DIAGSCREEN must be empty, 0, or a decimal integer in 1..6",
                completed.stderr)

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_diagscreen_valid_succeeds(self):
        for valid in ("1", "2", "3", "4", "5", "6"):
            completed = subprocess.run(
                ["make", "-C", "arm9v2", "-n", "DIAGSCREEN=%s" % valid],
                cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn("twlhd-arm9v2-diag%s.nds" % valid,
                          completed.stdout + completed.stderr)

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_diagscreen_unset_is_default_target(self):
        for env_value in (None, "", "0"):
            env = os.environ.copy()
            if env_value is None:
                env.pop("DIAGSCREEN", None)
            else:
                env["DIAGSCREEN"] = env_value
            completed = subprocess.run(
                ["make", "-C", "arm9v2", "-n"],
                cwd=ROOT, env=env, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn("twlhd-arm9v2.nds",
                          completed.stdout + completed.stderr)
            self.assertNotIn("diag", completed.stdout + completed.stderr)

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_diagscreen_composes_with_scene(self):
        completed = subprocess.run(
            ["make", "-C", "arm9v2", "-n", "SCENE=5", "DIAGSCREEN=2"],
            cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("twlhd-arm9v2-s5-diag2.nds",
                      completed.stdout + completed.stderr)


if __name__ == "__main__":
    unittest.main()
