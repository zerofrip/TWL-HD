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

# Full quad rectangles on the top screen.  The sampling rectangles below lie
# fully inside these quads, and a margin of background separates the quads.
QUADS = [
    ("UL", 50, 190, 55, 125, (0, 255, 0), (16, 8)),
    ("UR", 230, 370, 55, 125, (255, 0, 0), (8, 8)),
    ("LL", 50, 190, 135, 205, (255, 0, 0), (8, 8)),
    ("LR", 230, 370, 135, 205, (0, 0, 255), (16, 16)),
]

# Sampling rectangles used by the checker.
TEX_REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0)),
    ("UR", 270, 340, 75, 110, (255, 0, 0)),
    ("LL", 70, 170, 150, 185, (255, 0, 0)),
    ("LR", 270, 340, 150, 185, (0, 0, 255)),
]

# Control-frame solid colours.
CTL_REGIONS = [
    ("UL", 70, 170, 75, 110, (0, 255, 0)),
    ("UR", 270, 340, 75, 110, (128, 128, 128)),
    ("LL", 70, 170, 150, 185, (255, 0, 0)),
    ("LR", 270, 340, 150, 185, (0, 0, 255)),
]

BG = (16, 16, 24)
WHITE = (255, 255, 255)


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
    return BG


def checker_callback(x, y):
    for _name, qx0, qx1, qy0, qy1, colour, tex_size in QUADS:
        if qx0 <= x < qx1 and qy0 <= y < qy1:
            tex_w, tex_h = tex_size
            cells_x = tex_w // 4
            cells_y = tex_h // 4
            quad_w = qx1 - qx0
            quad_h = qy1 - qy0
            cell_w = quad_w / cells_x
            cell_h = quad_h / cells_y
            cx = int((x - qx0) / cell_w)
            cy = int((y - qy0) / cell_h)
            if (cx + cy) & 1:
                return WHITE
            return colour
    return BG


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
            self.assertIn("UL colour OK", out)
            self.assertIn("UR colour OK", out)
            self.assertIn("LL colour OK", out)
            self.assertIn("LR colour OK", out)

    def test_textured_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tex.ppm")
            make_ppm(path, checker_callback)
            rc, out, err = self.run_script(path, ["--frame", "tex"])
            self.assertEqual(rc, 0, f"stdout:\n{out}\nstderr:\n{err}")
            self.assertIn("UL colour OK", out)
            self.assertIn("UR colour OK", out)
            self.assertIn("LL colour OK", out)
            self.assertIn("LR colour OK", out)
            self.assertIn("UL structure OK", out)
            self.assertIn("UR structure OK", out)
            self.assertIn("LL structure OK", out)
            self.assertIn("LR structure OK", out)
            self.assertIn("UL ratio OK", out)
            self.assertIn("UR==LL reuse OK", out)

    def test_wrong_solid_colour_fails(self):
        def wrong(x, y):
            for _, x0, x1, y0, y1, _ in CTL_REGIONS:
                if x0 <= x < x1 and y0 <= y < y1:
                    return (0, 0, 0)
            return BG

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

    def test_wrong_cell_period_fails(self):
        """All regions drawn with an 8x8 cell layout: UL/LR period is wrong."""
        def wrong_period(x, y):
            for _name, qx0, qx1, qy0, qy1, colour, _tex_size in QUADS:
                if qx0 <= x < qx1 and qy0 <= y < qy1:
                    # Force an 8x8 checker everywhere.
                    cell_w = (qx1 - qx0) / 2
                    cell_h = (qy1 - qy0) / 2
                    cx = int((x - qx0) / cell_w)
                    cy = int((y - qy0) / cell_h)
                    if (cx + cy) & 1:
                        return WHITE
                    return colour
            return BG

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "wrong_period.ppm")
            make_ppm(path, wrong_period)
            rc, out, _ = self.run_script(path, ["--frame", "tex"])
            self.assertNotEqual(rc, 0)
            self.assertIn("structure FAIL", out)

    def test_square_where_nonsquare_expected_fails(self):
        """UL drawn with a square cell ratio instead of the 16x8 ratio."""
        def square_ul(x, y):
            for name, qx0, qx1, qy0, qy1, colour, tex_size in QUADS:
                if qx0 <= x < qx1 and qy0 <= y < qy1:
                    tex_w, tex_h = tex_size
                    cells_x = tex_w // 4
                    cells_y = tex_h // 4
                    if name == "UL":
                        # Force square cells: 2x2 instead of 4x2.
                        cells_x = 2
                        cells_y = 2
                    quad_w = qx1 - qx0
                    quad_h = qy1 - qy0
                    cell_w = quad_w / cells_x
                    cell_h = quad_h / cells_y
                    cx = int((x - qx0) / cell_w)
                    cy = int((y - qy0) / cell_h)
                    if (cx + cy) & 1:
                        return WHITE
                    return colour
            return BG

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "square_ul.ppm")
            make_ppm(path, square_ul)
            rc, out, _ = self.run_script(path, ["--frame", "tex"])
            self.assertNotEqual(rc, 0)
            # Either the UL structure period is wrong or the ratio check fails.
            self.assertTrue(
                "UL ratio FAIL" in out or "UL structure FAIL" in out,
                f"unexpected output:\n{out}")

    def test_ur_differs_from_ll_fails(self):
        """UR and LL reuse the texture, so a shifted phase in UR breaks reuse."""
        def shifted_ur(x, y):
            for name, qx0, qx1, qy0, qy1, colour, tex_size in QUADS:
                if qx0 <= x < qx1 and qy0 <= y < qy1:
                    tex_w, tex_h = tex_size
                    cells_x = tex_w // 4
                    cells_y = tex_h // 4
                    quad_w = qx1 - qx0
                    quad_h = qy1 - qy0
                    cell_w = quad_w / cells_x
                    cell_h = quad_h / cells_y
                    cx = int((x - qx0) / cell_w)
                    cy = int((y - qy0) / cell_h)
                    if name == "UR":
                        # Shift the phase so it no longer matches LL.
                        cx += 1
                    if (cx + cy) & 1:
                        return WHITE
                    return colour
            return BG

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "shifted_ur.ppm")
            make_ppm(path, shifted_ur)
            rc, out, _ = self.run_script(path, ["--frame", "tex"])
            self.assertNotEqual(rc, 0)
            self.assertIn("UR==LL reuse FAIL", out)

    def test_modulate_with_base_colour_fails(self):
        """MODULATE (texel x vertex colour) with a base-colour vertex colour
        collapses the checker to the base colour; the checker must reject it
        as a textured frame. White vertex colour is tested by
        test_textured_passes."""
        ctl_map = {
            "UL": (0, 255, 0),
            "UR": (128, 128, 128),
            "LL": (255, 0, 0),
            "LR": (0, 0, 255),
        }

        def modulated_callback(x, y):
            for name, qx0, qx1, qy0, qy1, colour, tex_size in QUADS:
                if qx0 <= x < qx1 and qy0 <= y < qy1:
                    tex_w, tex_h = tex_size
                    cells_x = tex_w // 4
                    cells_y = tex_h // 4
                    quad_w = qx1 - qx0
                    quad_h = qy1 - qy0
                    cell_w = quad_w / cells_x
                    cell_h = quad_h / cells_y
                    cx = int((x - qx0) / cell_w)
                    cy = int((y - qy0) / cell_h)
                    texel = WHITE if (cx + cy) & 1 else colour
                    vertex = ctl_map[name]
                    return tuple(
                        (texel[i] * vertex[i]) // 255 for i in range(3)
                    )
            return BG

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "modulated.ppm")
            make_ppm(path, modulated_callback)
            rc, out, _ = self.run_script(path, ["--frame", "tex"])
            self.assertNotEqual(rc, 0)
            self.assertIn("FAIL", out)


if __name__ == "__main__":
    unittest.main()
