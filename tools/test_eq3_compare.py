#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq3_compare.py (EQ-3b).

Drives the committed real fixture pair (harness 256x192 PPM plus our Azahar
top-screen crop) through the CLI for the positive case and exit codes, then
builds synthetic PPM pairs to pin the structural tolerances from both sides.
The mutant tests import the module and loosen TOL_POS or drop the colour check
to show the negative tests depend on the real checks (same pattern as
tools/test_eq2_compare.py).

Runs under `make test` without devkitPro or an emulator. The dedicated
`make test-eq3` target runs this suite; the discover pass in `make test` sets
EQ3_DISCOVER_SKIP so it is not executed twice.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

# Set only by the aggregate `test` recipe, which runs this module first via the
# test-eq3 target. Skips are visible in discover output, not a silent drop.
EQ3_DISCOVER_SKIP = os.environ.get("EQ3_DISCOVER_SKIP") is not None

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import eq3_compare  # noqa: E402

FIXTURES = os.path.join(TOOLS_DIR, "fixtures", "eq3")
HARNESS = os.path.join(FIXTURES, "harness_cube_f240.ppm")
OURS = os.path.join(FIXTURES, "ours_cube_f240_top.ppm")
CLI = os.path.join(TOOLS_DIR, "eq3_compare.py")

HARNESS_BG = (0, 0, 0)
OURS_BG = (16, 16, 24)

# Three visible faces as DS-pixel rectangles with background gaps, enough
# interior area to survive the 3 px erosion.
FACE_RECTS = [
    ("red", (255, 0, 0), 40, 30, 79, 89),
    ("yellow", (255, 255, 0), 150, 30, 189, 89),
    ("cyan", (0, 255, 255), 95, 110, 134, 169),
]


def run_tool(*args):
    return subprocess.run(
        [sys.executable, CLI, *args],
        capture_output=True,
        text=True,
        cwd=TOOLS_DIR,
    )


def write_ppm(path, width, height, pixel):
    data = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            r, g, b = pixel(x, y)
            offset = (y * width + x) * 3
            data[offset] = r
            data[offset + 1] = g
            data[offset + 2] = b
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (width, height))
        handle.write(data)
    return path


def _nearest_sample(pixel, candidates, sampler):
    return min(candidates, key=lambda cell: abs(sampler(cell) - pixel))


def make_harness(path, rects=FACE_RECTS, width=256, height=192):
    def callback(x, y):
        for _name, colour, x0, y0, x1, y1 in rects:
            if x0 <= x <= x1 and y0 <= y <= y1:
                return colour
        return HARNESS_BG

    return write_ppm(path, width, height, callback)


def make_ours(path, rects=FACE_RECTS, width=400, height=240):
    """Draw the DS rectangles through the 1.25x top-screen mapping."""
    ds_x = {}
    for x in range(width):
        if eq3_compare.SCREEN_X0 <= x < eq3_compare.SCREEN_X0 + eq3_compare.SCREEN_W:
            lo = max(0, min(eq3_compare.DS_W - 1,
                            int((x - eq3_compare.SCREEN_X0)
                                * eq3_compare.DS_W / eq3_compare.SCREEN_W)))
            ds_x[x] = _nearest_sample(
                x, range(max(0, lo - 1), min(eq3_compare.DS_W - 1, lo + 1) + 1),
                eq3_compare._sample_x)
        else:
            ds_x[x] = None
    ds_y = {}
    for y in range(min(height, eq3_compare.SCREEN_H)):
        lo = max(0, min(eq3_compare.DS_H - 1,
                        int(y * eq3_compare.DS_H / eq3_compare.SCREEN_H)))
        ds_y[y] = _nearest_sample(
            y, range(max(0, lo - 1), min(eq3_compare.DS_H - 1, lo + 1) + 1),
            eq3_compare._sample_y)

    def callback(x, y):
        dx, dy = ds_x[x], ds_y.get(y)
        if dx is None or dy is None:
            return OURS_BG
        for _name, colour, x0, y0, x1, y1 in rects:
            if x0 <= dx <= x1 and y0 <= dy <= y1:
                return colour
        return OURS_BG

    return write_ppm(path, width, height, callback)


def shifted_rects(face, dx=0, dy=0):
    out = []
    for name, colour, x0, y0, x1, y1 in FACE_RECTS:
        if name == face:
            out.append((name, colour, x0 + dx, y0 + dy, x1 + dx, y1 + dy))
        else:
            out.append((name, colour, x0, y0, x1, y1))
    return out


def recoloured_rects(face, colour):
    return [(name, colour if name == face else old, x0, y0, x1, y1)
            for name, old, x0, y0, x1, y1 in FACE_RECTS]


def without_face(face):
    return [rect for rect in FACE_RECTS if rect[0] != face]


@unittest.skipIf(EQ3_DISCOVER_SKIP,
                 "run once via the test-eq3 make target; discover skips it to "
                 "avoid a double run")
class Eq3CompareCliTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq3-")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_help(self):
        result = run_tool("--help")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_committed_real_pair_passes(self):
        result = run_tool(HARNESS, OURS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)
        self.assertIn("EMULATOR", result.stdout)
        self.assertIn("face red:", result.stdout)
        self.assertIn("not visible", result.stdout)

    def test_400x480_top_screen_input_is_accepted(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        ours = make_ours(os.path.join(self.tmp, "ours480.ppm"), height=480)
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shift_within_tolerance_passes(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        ours = make_ours(os.path.join(self.tmp, "o.ppm"),
                         shifted_rects("red", dx=1))
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_shift_beyond_tolerance_fails_naming_field_and_face(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        ours = make_ours(os.path.join(self.tmp, "o.ppm"),
                         shifted_rects("red", dx=eq3_compare.TOL_POS + 2))
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("MISMATCH", result.stdout)
        self.assertIn("bbox-left", result.stdout)
        self.assertIn("face red", result.stdout)

    def test_colour_change_beyond_tolerance_fails(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        ours = make_ours(os.path.join(self.tmp, "o.ppm"),
                         recoloured_rects("red", (200, 0, 0)))
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("MISMATCH colour-r", result.stdout)
        self.assertIn("face red", result.stdout)

    def test_missing_face_fails(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        ours = make_ours(os.path.join(self.tmp, "o.ppm"),
                         without_face("red"))
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("missing face red", result.stdout)

    def test_extra_face_fails(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"))
        extra = list(FACE_RECTS) + [
            ("magenta", (255, 0, 255), 200, 120, 230, 160)]
        ours = make_ours(os.path.join(self.tmp, "o.ppm"), extra)
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("extra face magenta", result.stdout)

    def test_wrong_size_is_a_usage_error(self):
        harness = make_harness(os.path.join(self.tmp, "h.ppm"), height=191)
        ours = make_ours(os.path.join(self.tmp, "o.ppm"))
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("256x192", result.stderr)

    def test_bad_magic_is_a_usage_error(self):
        path = os.path.join(self.tmp, "bad.ppm")
        with open(path, "wb") as handle:
            handle.write(b"P3\n256 192\n255\n")
        ours = make_ours(os.path.join(self.tmp, "o.ppm"))
        result = run_tool(path, ours)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("expected a P6 PPM", result.stderr)

    def test_truncated_payload_is_a_usage_error(self):
        path = os.path.join(self.tmp, "short.ppm")
        with open(HARNESS, "rb") as handle:
            data = handle.read()
        with open(path, "wb") as handle:
            handle.write(data[:-3])
        ours = make_ours(os.path.join(self.tmp, "o.ppm"))
        result = run_tool(path, ours)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("raster bytes", result.stderr)

    def test_no_expected_face_class_is_a_usage_error(self):
        harness = make_harness(
            os.path.join(self.tmp, "h.ppm"), rects=[])
        ours = make_ours(os.path.join(self.tmp, "o.ppm"), rects=[])
        result = run_tool(harness, ours)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("none of the expected face classes", result.stderr)


@unittest.skipIf(EQ3_DISCOVER_SKIP,
                 "run once via the test-eq3 make target; discover skips it to "
                 "avoid a double run")
class Eq3CompareMutantTest(unittest.TestCase):
    """Show the negative tests depend on the real tolerance and colour checks."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq3-mutant-")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _load(self, harness_rects, ours_rects):
        harness = eq3_compare.load_picture(
            make_harness(os.path.join(self.tmp, "h.ppm"), harness_rects),
            "harness")
        ours = eq3_compare.load_picture(
            make_ours(os.path.join(self.tmp, "o.ppm"), ours_rects), "ours")
        return harness, ours

    def _compare(self, harness, ours):
        with redirect_stdout(io.StringIO()):
            return eq3_compare.compare(harness, ours)[0]

    def test_relaxed_tolerance_would_accept_the_beyond_case(self):
        harness, ours = self._load(
            FACE_RECTS, shifted_rects("red", dx=eq3_compare.TOL_POS + 2))
        self.assertEqual(self._compare(harness, ours), 1)

        original = eq3_compare.TOL_POS
        eq3_compare.TOL_POS = 10
        try:
            code = self._compare(harness, ours)
        finally:
            eq3_compare.TOL_POS = original
        self.assertEqual(code, 0,
                         "loosening TOL_POS must wrongly accept the shifted face")

    def test_dropped_colour_check_would_accept_a_colour_change(self):
        harness, ours = self._load(
            FACE_RECTS, recoloured_rects("red", (200, 0, 0)))
        self.assertEqual(self._compare(harness, ours), 1)

        original = eq3_compare.ENFORCE_COLOUR
        eq3_compare.ENFORCE_COLOUR = False
        try:
            code = self._compare(harness, ours)
        finally:
            eq3_compare.ENFORCE_COLOUR = original
        self.assertEqual(code, 0,
                         "dropping the colour check must wrongly accept it")


if __name__ == "__main__":
    unittest.main()
