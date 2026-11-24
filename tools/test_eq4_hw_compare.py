#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4_hw_compare.py.

Synthetic PIL data only: a 256x192 reference PPM with one block per colour
class, and a 400x240 capture PNG built by the documented 1.25x bilinear
upsample at (40,0).  Skips with a labelled reason when PIL is missing.  The
real scene5 capture fixture (tools/ds_scale_test/fixtures/) is used only as a
rotating-phase mismatch regression: a rotating frame must never match a static
reference.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(TOOLS_DIR, "eq4_hw_compare.py")
MAPPING = os.path.join(TOOLS_DIR, "ds_vtxcolor_test", "capture_mapping.json")
REAL_FIXTURE = os.path.join(TOOLS_DIR, "ds_scale_test", "fixtures",
                            "s5b_7_top400x240.png")

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-eq4-hw-compare target.
DISCOVER_SKIP = os.environ.get("EQ4_HW_COMPARE_DISCOVER_SKIP") is not None

DS_W, DS_H = 256, 192
CAPTURE_W, CAPTURE_H = 400, 240
SCALE = 1.25
OFFSET_X = 40

WHITE = (255, 255, 255)
RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
YELLOW = (255, 255, 0)
BLACK = (0, 0, 0)

BLOCKS = (
    (WHITE, 32, 32, 64, 48),
    (RED, 128, 32, 64, 48),
    (GREEN, 32, 96, 64, 48),
    (BLUE, 128, 96, 64, 48),
    (YELLOW, 192, 96, 48, 48),
)


def synthetic_reference():
    pixels = [BLACK] * (DS_W * DS_H)
    for colour, x0, y0, width, height in BLOCKS:
        for y in range(y0, y0 + height):
            for x in range(x0, x0 + width):
                pixels[y * DS_W + x] = colour
    return pixels


def write_ppm(path, pixels, width=DS_W, height=DS_H):
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (width, height))
        handle.write(bytes(channel for rgb in pixels for channel in rgb))


def shift(pixels, dx, dy):
    out = [BLACK] * len(pixels)
    for y in range(DS_H):
        for x in range(DS_W):
            sx, sy = x - dx, y - dy
            if 0 <= sx < DS_W and 0 <= sy < DS_H:
                out[y * DS_W + x] = pixels[sy * DS_W + sx]
    return out


def recolour(pixels, old, new):
    return [new if rgb == old else rgb for rgb in pixels]


def write_capture(path, reference):
    """Independent documented upsample: PIL bilinear 1.25x into (40,0)."""
    image = Image.frombytes("RGB", (DS_W, DS_H),
                            bytes(channel for rgb in reference
                                  for channel in rgb))
    image = image.resize((int(DS_W * SCALE), int(DS_H * SCALE)),
                         Image.BILINEAR)
    canvas = Image.new("RGB", (CAPTURE_W, CAPTURE_H), BLACK)
    canvas.paste(image, (OFFSET_X, 0))
    canvas.save(path)


def run_tool(capture, reference, mapping=MAPPING):
    return subprocess.run(
        [sys.executable, CLI, "--capture", capture,
         "--reference", reference, "--mapping", mapping],
        capture_output=True, text=True)


def run_tool_correction(capture, reference, mode):
    return subprocess.run(
        [sys.executable, CLI, "--capture", capture,
         "--reference", reference, "--azahar-correction", mode],
        capture_output=True, text=True)


def parse_report(stdout):
    return dict(line.split("=", 1) for line in stdout.splitlines() if "=" in line)


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-eq4-hw-compare make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(HAVE_PIL, "PIL is not installed, so capture PNGs cannot "
                               "be read or synthesised (labelled skip)")
class Eq4HwCompareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq4-hw-compare-")
        self.reference_ppm = os.path.join(self.tmp, "reference.ppm")
        write_ppm(self.reference_ppm, synthetic_reference())
        self.reference = synthetic_reference()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _capture(self, pixels, name="capture.png"):
        path = os.path.join(self.tmp, name)
        write_capture(path, pixels)
        return path

    def test_identity_upsample_passes(self):
        capture = self._capture(self.reference)
        result = run_tool(capture, self.reference_ppm)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertEqual(report["basis"], "one console, one capture")
        self.assertIn("ds.frac_within", report)
        self.assertIn("capture.frac_within", report)
        self.assertGreaterEqual(float(report["ds.frac_within"]), 0.95)

    def test_one_ds_pixel_shift_passes(self):
        capture = self._capture(shift(self.reference, 1, 1), "shift1.png")
        result = run_tool(capture, self.reference_ppm)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertLessEqual(int(report["bbox_dev_left"]), 2)

    def test_four_ds_pixel_shift_fails(self):
        capture = self._capture(shift(self.reference, 4, 4), "shift4.png")
        result = run_tool(capture, self.reference_ppm)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "FAIL", result.stdout)
        self.assertGreater(int(report["bbox_dev_left"]), 2)

    def test_wrong_colour_fails(self):
        capture = self._capture(recolour(self.reference, RED, GREEN),
                                "wrong-colour.png")
        result = run_tool(capture, self.reference_ppm)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "FAIL", result.stdout)
        self.assertIn("class red", report["reason"])

    def test_empty_capture_is_not_comparable(self):
        capture = self._capture([BLACK] * (DS_W * DS_H), "empty.png")
        result = run_tool(capture, self.reference_ppm)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "NOT-COMPARABLE", result.stdout)
        self.assertIn("no non-background region", report["reason"])

    def test_missing_mapping_is_exit_two(self):
        capture = self._capture(self.reference)
        result = run_tool(capture, self.reference_ppm,
                          mapping=os.path.join(self.tmp, "no-such.json"))
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cannot read mapping", result.stderr)

    @unittest.skipUnless(os.path.isfile(REAL_FIXTURE),
                         "real scene5 capture fixture is not present")
    def test_rotating_phase_capture_never_matches_static_reference(self):
        """The real scene5 capture is one rotating phase; against a static
        reference it must be FAIL or NOT-COMPARABLE, never PASS."""
        result = run_tool(REAL_FIXTURE, self.reference_ppm)
        report = parse_report(result.stdout)
        self.assertIn(report["verdict"], ("FAIL", "NOT-COMPARABLE"),
                      result.stdout)
        self.assertEqual(report["basis"], "one console, one capture")
        expected_exit = 1 if report["verdict"] == "FAIL" else 0
        self.assertEqual(result.returncode, expected_exit, result.stdout)


HARNESS_DIR = os.path.join(TOOLS_DIR, "fixtures", "eq_scenes")
HW_DIR = os.path.join(TOOLS_DIR, "fixtures", "eq4_hw")
HARNESS_REFS = {
    "st0": os.path.join(HARNESS_DIR, "harness_s5_st0_f240.png"),
    "st30": os.path.join(HARNESS_DIR, "harness_s5_st30_f240.png"),
}
HW_CAPTURES = {
    "st0": os.path.join(HW_DIR, "st0_3_top400x240.png"),
    "st30": os.path.join(HW_DIR, "st30_3_top400x240.png"),
}
OURS_REFS = {
    "st0": os.path.join(HW_DIR, "ours_s5_st0_top400x240.png"),
    "st30": os.path.join(HW_DIR, "ours_s5_st30_top400x240.png"),
}
REAL_FIXTURES = tuple(HARNESS_REFS.values()) + tuple(HW_CAPTURES.values()) \
    + tuple(OURS_REFS.values())


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-eq4-hw-compare make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(HAVE_PIL, "PIL is not installed, so PNG references "
                               "cannot be converted to PPM (labelled skip)")
@unittest.skipUnless(all(os.path.isfile(path) for path in REAL_FIXTURES),
                     "the committed real scene5 fixtures are not present")
class RealScene5StaticCompareTest(unittest.TestCase):
    """The first real hardware-vs-reference comparison (2026-11-23).

    Hardware = cc3dsfs top-screen captures of the static scene5 ROMs
    (`tools/fixtures/eq4_hw/st{0,30}_3_top400x240.png`).  References: the
    DeSmuME harness PPM and our Pica render (both committed as PNG and
    converted to PPM here).  Every assertion is on what the tool prints.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq4-hw-real-")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def _ppm(self, png, name):
        path = os.path.join(self.tmp, name + ".ppm")
        Image.open(png).convert("RGB").save(path)
        return path

    def _assert_pass(self, capture, reference):
        result = run_tool(capture, reference)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertEqual(report["basis"], "one console, one capture")
        for edge in ("left", "top", "right", "bottom"):
            self.assertLessEqual(int(report["bbox_dev_" + edge]), 3,
                                 result.stdout)
        self.assertGreaterEqual(float(report["ds.frac_within_flat"]), 0.95,
                                result.stdout)
        return report

    def _assert_not_pass(self, capture, reference):
        result = run_tool(capture, reference)
        report = parse_report(result.stdout)
        self.assertIn(report["verdict"], ("FAIL", "NOT-COMPARABLE"),
                      result.stdout)
        expected_exit = 1 if report["verdict"] == "FAIL" else 0
        self.assertEqual(result.returncode, expected_exit, result.stdout)
        return report

    def test_st0_capture_matches_harness_reference(self):
        self._assert_pass(HW_CAPTURES["st0"],
                          self._ppm(HARNESS_REFS["st0"], "harness_st0"))

    def test_st30_capture_matches_harness_reference(self):
        self._assert_pass(HW_CAPTURES["st30"],
                          self._ppm(HARNESS_REFS["st30"], "harness_st30"))

    def test_st0_capture_matches_ours_reference(self):
        self._assert_pass(HW_CAPTURES["st0"],
                          self._ppm(OURS_REFS["st0"], "ours_st0"))

    def test_st30_capture_matches_ours_reference(self):
        self._assert_pass(HW_CAPTURES["st30"],
                          self._ppm(OURS_REFS["st30"], "ours_st30"))

    def test_st0_capture_never_matches_st30_reference(self):
        self._assert_not_pass(HW_CAPTURES["st0"],
                              self._ppm(HARNESS_REFS["st30"], "harness_st30"))

    def test_st30_capture_never_matches_st0_reference(self):
        self._assert_not_pass(HW_CAPTURES["st30"],
                              self._ppm(HARNESS_REFS["st0"], "harness_st0"))

    def test_presentation_correction_off_reproduces_legacy_numbers(self):
        reference = self._ppm(OURS_REFS["st0"], "ours_st0")
        result = run_tool_correction(HW_CAPTURES["st0"], reference, "off")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertIn("presentation-row correction: not applied "
                      "(--azahar-correction off)", result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertEqual(report["ref_bbox_top"], "28", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "2", result.stdout)

    def test_presentation_correction_auto_shifts_the_azahar_reference(self):
        reference = self._ppm(OURS_REFS["st0"], "ours_st0")
        result = run_tool_correction(HW_CAPTURES["st0"], reference, "auto")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertIn("presentation-row correction: applied "
                      "(row-0 wrap signature detected)", result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertEqual(report["ref_bbox_top"], "27", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "1", result.stdout)
        self.assertEqual(report["bbox_dev_bottom"], "0", result.stdout)


if __name__ == "__main__":
    unittest.main()
