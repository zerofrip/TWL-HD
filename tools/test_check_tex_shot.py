#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/check_tex_shot.py.

Builds synthetic 400x480 PPMs and verifies that the checker accepts a matching
frame and rejects wrong-colour / wrong-pattern cases.

Runs under `make test` via `python3 -m unittest discover -s tools`.
"""

import os
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(TOOLS_DIR, "check_tex_shot.py")

WIDTH = 400
HEIGHT = 480

# Textured-frame base colours: UL green, UR red, LL red, LR blue.
# These rectangles must match tools/check_tex_shot.py REGIONS.
TEX_REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0)),
    ("UR", 270, 340, 75, 110, (255, 0, 0)),
    ("LL", 70, 170, 150, 185, (255, 0, 0)),
    ("LR", 270, 340, 150, 185, (0, 0, 255)),
]

# Control-frame solid colours: UL green, UR grey, LL red, LR blue.
CTL_REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0)),
    ("UR", 270, 340, 75, 110, (128, 128, 128)),
    ("LL", 70, 170, 150, 185, (255, 0, 0)),
    ("LR", 270, 340, 150, 185, (0, 0, 255)),
]


def make_ppm(path, fill_callback):
    data = bytearray(WIDTH * HEIGHT * 3)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = fill_callback(x, y)
            off = (y * WIDTH + x) * 3
            data[off] = r
            data[off + 1] = g
            data[off + 2] = b
    with open(path, "wb") as f:
        f.write(b"P6\n400 480\n255\n")
        f.write(data)


def solid_callback(x, y):
    for _, x0, x1, y0, y1, colour in CTL_REGIONS:
        if x0 <= x < x1 and y0 <= y < y1:
            return colour
    return (16, 16, 24)


def checker_callback(x, y):
    for _, x0, x1, y0, y1, colour in TEX_REGIONS:
        if x0 <= x < x1 and y0 <= y < y1:
            # 8x8 checker squares, alternating with white.
            cx = (x - x0) // 8
            cy = (y - y0) // 8
            if (cx + cy) & 1:
                return (255, 255, 255)
            return colour
    return (16, 16, 24)


class TestCheckTexShot(unittest.TestCase):
    def run_script(self, path, args):
        cmd = [sys.executable, "-B", "-I", SCRIPT, path] + args
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return proc.returncode, proc.stdout, proc.stderr

    def test_solid_control_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "ctl.ppm")
            make_ppm(path, solid_callback)
            rc, out, err = self.run_script(path, ["--frame", "ctl"])
            self.assertEqual(rc, 0, f"stdout:\n{out}\nstderr:\n{err}")
            self.assertIn("UL OK", out)
            self.assertIn("UR OK", out)
            self.assertIn("LL OK", out)
            self.assertIn("LR OK", out)

    def test_textured_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tex.ppm")
            make_ppm(path, checker_callback)
            rc, out, err = self.run_script(path, ["--frame", "tex"])
            self.assertEqual(rc, 0, f"stdout:\n{out}\nstderr:\n{err}")
            self.assertIn("UL OK", out)
            self.assertIn("UR OK", out)
            self.assertIn("LL OK", out)
            self.assertIn("LR OK", out)

    def test_wrong_solid_colour_fails(self):
        def wrong(x, y):
            for _, x0, x1, y0, y1, _ in CTL_REGIONS:
                if x0 <= x < x1 and y0 <= y < y1:
                    return (0, 0, 0)
            return (16, 16, 24)

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "wrong.ppm")
            make_ppm(path, wrong)
            rc, out, _ = self.run_script(path, ["--frame", "ctl"])
            self.assertNotEqual(rc, 0)
            self.assertIn("FAIL", out)

    def test_solid_as_textured_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "solid_as_tex.ppm")
            make_ppm(path, solid_callback)
            rc, out, _ = self.run_script(path, ["--frame", "tex"])
            self.assertNotEqual(rc, 0)
            self.assertIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
