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

import hashlib
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


SCENES_DIR = os.path.join(TOOLS_DIR, "fixtures", "eq4_hw_scenes")
SCENE_IDS = ("cube", "s1", "s2", "s3", "s4", "c2")
SCENE_HW = {scene: os.path.join(SCENES_DIR, "%s_a_top400x240.png" % scene)
            for scene in SCENE_IDS}
SCENE_HARNESS = {scene: os.path.join(SCENES_DIR,
                                     "harness_%s_st30_f240.png" % scene)
                 for scene in SCENE_IDS}
SCENE_OURS = {scene: os.path.join(SCENES_DIR,
                                  "ours_%s_st30_top400x240.png" % scene)
              for scene in SCENE_IDS}
SCENE_FIXTURE_SHA256 = {
    "c2_a_top400x240.png":
        "488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6",
    "cube_a_top400x240.png":
        "0f9e7aee19cec41fc00909e894f053717a3cca4088aa18efc5789527d2f6e9c0",
    "harness_c2_st30_f240.png":
        "28979f4ece54f165176dff586e1e0ef673a4f16fc8b5dc494caddae20043852b",
    "harness_cube_st30_f240.png":
        "373701e21dde8593a2cffe10e3afe4e6c622a853adc8bb9ed8b823da2b8f6d74",
    "harness_s1_st30_f240.png":
        "d402948dafcff446c0bb53edbec8d6dfe156255a77b03d310ef21d38303edc25",
    "harness_s2_st30_f240.png":
        "dfc44116d0e354b60a0451bd48ba3f78671def7034ae1a5b1cbe7d44e4cfe965",
    "harness_s3_st30_f240.png":
        "9cf18f061ebf3990939827af3815e91d9564de271f735aefc033064b28702b1c",
    "harness_s4_st30_f240.png":
        "6538dd758346eed149fe1f72ea3d7c8aa4f655a7b3dfed169a13bedb46e6b99b",
    "ours_c2_st30_top400x240.png":
        "b97df11cbae37fe9dc25edaffc22219508a99e607a97ceb4a0a14049aeb92162",
    "ours_cube_st30_top400x240.png":
        "c4a8b5d851c7b8b08adce25cc5617479194ad1c89e733a21fba2a9dbf693bcfe",
    "ours_s1_st30_top400x240.png":
        "f2f806e308ea696c2f42a430889cb08aaf7d5b527365df23dfc070f5612f9a87",
    "ours_s2_st30_top400x240.png":
        "cb73eed07345caae5712a7b79213f9d2ef334400b1ef8b17adb244fb522221fc",
    "ours_s3_st30_top400x240.png":
        "915bdf7cc4f3e969455cc2e9b68508cdbe1b73944d2e5621a03c143f73a7ed9a",
    "ours_s4_st30_top400x240.png":
        "23302ce8371c74ac52822d46d7637f0df06e18616f1698a287b1bb42ae5c0185",
    "s1_a_top400x240.png":
        "8af538df08a00169f9f50785974f8d20388e969fda525151be468db65cfee75a",
    "s2_a_top400x240.png":
        "c01dc0ea69eb2a715af29743ac7e7d48407354bb4a56fff1d3f1a85287f338b9",
    "s3_a_top400x240.png":
        "bb83ba0fef9514d896e659f21f5ad57edf507e92e6e55e8768f0ab71202d9399",
    "s4_a_top400x240.png":
        "ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9",
}
# c2's harness reference is `harness_c2_st30_f240.png` (EQ-4t, produced from
# the plain static ROM `twlhd-arm9v2-c2-st30.nds`, which passes where the
# static autodump ROM exits 1 with `ARM OP_BKPT triggered`).
SCENE_FIXTURES = tuple(SCENE_HW.values()) + tuple(SCENE_OURS.values()) \
    + tuple(SCENE_HARNESS.values())


def shift_capture_up(path, rows, out_path):
    """Move the capture content `rows` capture rows up (2 rows ~ 1.6 DS px)."""
    image = Image.open(path).convert("RGB")
    canvas = Image.new("RGB", image.size, BLACK)
    canvas.paste(image.crop((0, rows, image.width, image.height)), (0, 0))
    canvas.save(out_path)
    return out_path


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-eq4-hw-compare make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(HAVE_PIL, "PIL is not installed, so PNG references "
                               "cannot be converted to PPM (labelled skip)")
@unittest.skipUnless(all(os.path.isfile(path) for path in SCENE_FIXTURES),
                     "the committed static-scene fixtures are not present")
class RealStaticScenesCompareTest(unittest.TestCase):
    """The six EQ-4q static-angle scenes against one hardware capture each.

    Hardware = `tools/fixtures/eq4_hw_scenes/<scene>_a_top400x240.png` (capture
    `a`, the rule fixed before the comparison).  References: the harness PNG
    (converted to PPM here) and our Pica render PNG.  Every assertion is on
    what the tool prints; the numbers are the ones quoted in
    `docs/reports/eq4r-static-scenes-hw-compare.md`.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq4-hw-scenes-")

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
        self.assertIn("presentation-row correction: ", result.stdout)
        for edge in ("left", "top", "right", "bottom"):
            self.assertLessEqual(int(report["bbox_dev_" + edge]), 3,
                                 result.stdout)
        self.assertGreaterEqual(float(report["ds.frac_within_flat"]), 0.95,
                                result.stdout)
        return result, report

    def test_fixture_hashes_and_sizes(self):
        self.assertEqual(len(SCENE_FIXTURE_SHA256), 18)
        for name, digest in sorted(SCENE_FIXTURE_SHA256.items()):
            path = os.path.join(SCENES_DIR, name)
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 digest, name)
            self.assertLess(os.path.getsize(path), 100 * 1024, name)

    def test_cube_capture_matches_harness_reference(self):
        _, report = self._assert_pass(
            SCENE_HW["cube"], self._ppm(SCENE_HARNESS["cube"], "harness_cube"))
        self.assertEqual(report["bbox_dev_top"], "2", report)
        self.assertEqual(report["ds.mean_abs_r"], "2.2378", report)
        self.assertEqual(report["ds.frac_within_flat"], "0.9996", report)

    def test_s2_capture_matches_harness_reference(self):
        _, report = self._assert_pass(
            SCENE_HW["s2"], self._ppm(SCENE_HARNESS["s2"], "harness_s2"))
        self.assertEqual(report["bbox_dev_top"], "1", report)
        self.assertEqual(report["bbox_dev_bottom"], "2", report)
        self.assertEqual(report["ds.mean_abs_g"], "0.0000", report)
        self.assertEqual(report["ds.frac_within_flat"], "0.9987", report)

    def test_c2_capture_matches_ours_reference(self):
        result, report = self._assert_pass(
            SCENE_HW["c2"], self._ppm(SCENE_OURS["c2"], "ours_c2"))
        self.assertIn("presentation-row correction: applied "
                      "(row-0 wrap signature detected)", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "1", report)
        self.assertEqual(report["ds.frac_within_flat"], "1.0000", report)

    def test_s3_capture_matches_ours_reference(self):
        _, report = self._assert_pass(
            SCENE_HW["s3"], self._ppm(SCENE_OURS["s3"], "ours_s3"))
        self.assertEqual(report["bbox_dev_left"], "1", report)
        self.assertEqual(report["bbox_dev_top"], "0", report)
        self.assertEqual(report["ds.frac_within_flat"], "1.0000", report)

    def test_s4_capture_fails_on_the_alpha_zero_quad(self):
        """The hardware draws the alpha-0 quad's outline; both references do
        not, so the capture's top edge sits 10 DS px higher."""
        result = run_tool(SCENE_HW["s4"],
                          self._ppm(SCENE_HARNESS["s4"], "harness_s4"))
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "FAIL", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "10", report)
        self.assertEqual(report["reason"], "bbox-top deviates 10 DS px "
                                           "(tol 3)")

    def test_c2_capture_matches_harness_reference(self):
        """EQ-4t supersedes EQ-4r's NOT-COMPARABLE c2 harness pair: the
        reference is produced from the plain static ROM (not the autodump ROM
        that exits 1 with `ARM OP_BKPT triggered`); see
        `docs/reports/eq4t-c2-reference.md`."""
        result, report = self._assert_pass(
            SCENE_HW["c2"], self._ppm(SCENE_HARNESS["c2"], "harness_c2"))
        self.assertIn("presentation-row correction: not applied "
                      "(no row-0 wrap signature)", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "2", report)
        self.assertEqual(report["ds.mean_abs_r"], "0.4777", report)
        self.assertEqual(report["ds.frac_within_flat"], "1.0000", report)

    def test_c2_shifted_capture_changes_the_number(self):
        """c2's top `bbox_dev` is 2 (one DS px under tol 3), so a 2-capture-row
        shift (1.6 DS px) moves it to 3: the number changes, but the verdict
        stays PASS (the shift stays within tolerance — unlike s1 whose 3 goes
        to 4 and flips FAIL)."""
        reference = self._ppm(SCENE_HARNESS["c2"], "harness_c2")
        self.assertEqual(
            self._assert_pass(SCENE_HW["c2"], reference)[1]["bbox_dev_top"],
            "2")
        shifted = shift_capture_up(
            SCENE_HW["c2"], 2, os.path.join(self.tmp, "c2_shift2.png"))
        result = run_tool(shifted, reference)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "PASS", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "3", report)

    def test_c2_autodump_pair_not_comparable(self):
        """The static autodump c2 ROM still exits 1 in the harness
        (`ARM OP_BKPT triggered`), so that pair stays NOT-COMPARABLE: a
        missing reference makes the tool exit 2 instead of inventing a
        verdict."""
        missing = os.path.join(self.tmp, "harness_c2_autodump_st30_f240.png")
        result = run_tool(SCENE_HW["c2"], missing)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cannot read", result.stderr)
        self.assertNotIn("verdict=", result.stdout)

    def test_shifted_capture_changes_the_verdict(self):
        """Two capture rows (~1.6 DS px) must be enough to break the PASS."""
        reference = self._ppm(SCENE_HARNESS["s1"], "harness_s1")
        self.assertEqual(
            self._assert_pass(SCENE_HW["s1"], reference)[1]["bbox_dev_top"],
            "3")
        shifted = shift_capture_up(
            SCENE_HW["s1"], 2, os.path.join(self.tmp, "s1_shift2.png"))
        result = run_tool(shifted, reference)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = parse_report(result.stdout)
        self.assertEqual(report["verdict"], "FAIL", result.stdout)
        self.assertEqual(report["bbox_dev_top"], "4", report)


if __name__ == "__main__":
    unittest.main()
