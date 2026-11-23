#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4_centroid_deltas.py (inline `key=value` reports)."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(TOOLS_DIR, "eq4_centroid_deltas.py")

CLASSES = ("white", "red", "green", "blue", "yellow")


def report_text(centroids):
    lines = ["basis=one console, one capture"]
    for name in CLASSES:
        ref_x, ref_y, cap_x, cap_y = centroids[name]
        lines.extend([
            "class_%s_ref_centroid_x=%.4f" % (name, ref_x),
            "class_%s_ref_centroid_y=%.4f" % (name, ref_y),
            "class_%s_capture_centroid_x=%.4f" % (name, cap_x),
            "class_%s_capture_centroid_y=%.4f" % (name, cap_y),
        ])
    lines.append("verdict=PASS")
    return "\n".join(lines) + "\n"


class Eq4CentroidDeltasTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq4-centroid-")
        self.report = os.path.join(self.tmp, "report.txt")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _run(self, *args):
        return subprocess.run([sys.executable, CLI] + list(args),
                              capture_output=True, text=True)

    def test_prints_capture_minus_reference(self):
        centroids = {name: (10.0, 20.0, 9.0, 18.0) for name in CLASSES}
        centroids["red"] = (10.5, 20.5, 12.0, 19.5)
        with open(self.report, "w", encoding="utf-8") as handle:
            handle.write(report_text(centroids))
        result = self._run(self.report)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            self.report,
            "white dx=-1.0000 dy=-2.0000",
            "red dx=1.5000 dy=-1.0000",
            "green dx=-1.0000 dy=-2.0000",
            "blue dx=-1.0000 dy=-2.0000",
            "yellow dx=-1.0000 dy=-2.0000",
        ])

    def test_missing_centroid_key_is_exit_two(self):
        with open(self.report, "w", encoding="utf-8") as handle:
            handle.write("basis=one console, one capture\nverdict=PASS\n")
        result = self._run(self.report)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("class white", result.stderr)

    def test_missing_argument_is_exit_two(self):
        result = self._run()
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("usage", result.stderr)


if __name__ == "__main__":
    unittest.main()
