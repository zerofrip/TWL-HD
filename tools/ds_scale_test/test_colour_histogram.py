#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_scale_test/colour_histogram.py.

A synthetic-PNG ordering case, plus the real 2026-11-23 scene5 capture
`fixtures/s5b_7_top400x240.png`: the six expected colours must appear among
the printed top colours and neither cyan (24,255,255) nor magenta (255,24,255)
may appear with a count >= 20.  Only what the tool prints is asserted.  Skips
with a labelled reason when PIL or the fixture is missing.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "colour_histogram.py")
FIXTURE = os.path.join(HERE, "fixtures", "s5b_7_top400x240.png")
EXPECTED_COLOURS = ((0, 0, 0), (255, 255, 255), (255, 255, 24),
                    (0, 255, 8), (0, 0, 247), (255, 0, 0))
LINE_RE = re.compile(r"^\((\d+), (\d+), (\d+)\) (\d+)$")

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False


def run_script(path, *args):
    return subprocess.run(
        [sys.executable, SCRIPT, path] + list(args),
        capture_output=True, text=True)


def parse(text):
    counts = {}
    for line in text.splitlines():
        match = LINE_RE.match(line)
        if match:
            rgb = tuple(int(group) for group in match.groups()[:3])
            counts[rgb] = int(match.group(4))
    return counts


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; colour histogram tests skipped")
class ColourHistogramTest(unittest.TestCase):
    def test_synthetic_ordering(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "synthetic.png")
            image = Image.new("RGB", (400, 240), (0, 0, 0))
            for x in range(4):
                image.putpixel((x, 0), (255, 0, 0))
            image.putpixel((5, 0), (255, 255, 255))
            image.save(path)
            completed = run_script(path)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            lines = completed.stdout.splitlines()
        self.assertEqual(lines[0], "(0, 0, 0) 95995")
        self.assertEqual(lines[1], "(255, 0, 0) 4")
        self.assertEqual(lines[2], "(255, 255, 255) 1")

    @unittest.skipUnless(
        os.path.exists(FIXTURE), "fixture s5b_7_top400x240.png is missing")
    def test_real_fixture_colours(self):
        """Real 2026-11-23 scene5 capture, top 400x240 crop.

        top_n=40 prints every colour with count >= 20 (35 qualify), so an
        absence from the printed block is an absence from that set.
        """
        completed = run_script(FIXTURE, "40")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        counts = parse(completed.stdout)
        for rgb in EXPECTED_COLOURS:
            self.assertIn(rgb, counts, rgb)
        for rgb in ((24, 255, 255), (255, 24, 255)):
            self.assertLess(counts.get(rgb, 0), 20, rgb)


if __name__ == "__main__":
    unittest.main()
