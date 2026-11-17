#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq_scenes.py (EQ-2/EQ-3 scene coverage).

The suite drives the driver in manifest mode over the committed small text
fixtures: one per-scene pair from the real EMULATOR run, the existing cube
pair, and the harness dumps for the scenes whose capture cannot be produced.
No harness binary, DeSmuME or Azahar run is needed here.

Runs under `make test` without devkitPro or an emulator. The dedicated
`make test-eq-scenes` target runs this suite; the discover pass in `make test`
sets EQ_SCENES_DISCOVER_SKIP so it is not executed twice.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

# Set only by the aggregate `test` recipe, which runs this module first via the
# test-eq-scenes target. Skips are visible in discover output, not a silent
# drop.
EQ_SCENES_DISCOVER_SKIP = os.environ.get("EQ_SCENES_DISCOVER_SKIP") is not None

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import eq2_compare  # noqa: E402
import eq_scenes  # noqa: E402

FIXTURES = os.path.join(TOOLS_DIR, "fixtures", "eq_scenes")
MANIFEST = os.path.join(FIXTURES, "manifest.json")
PASS_MANIFEST = os.path.join(FIXTURES, "manifest_pass.json")
CLI = os.path.join(TOOLS_DIR, "eq_scenes.py")

HARNESS_S1 = os.path.join(FIXTURES, "harness_s1_f240.txt")
OURS_S1 = os.path.join(FIXTURES, "ours_s1_f240.txt")


def run_tool(*args):
    return subprocess.run(
        [sys.executable, CLI, *args],
        capture_output=True,
        text=True,
        cwd=TOOLS_DIR,
    )


def read_lines(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read().splitlines(keepends=True)


def write_lines(directory, name, lines):
    path = os.path.join(directory, name)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("".join(lines))
    return path


def perturb_first_x(lines, delta):
    """Add delta to the first pos-x-16.16 value, keeping the format valid."""
    out = []
    changed = False
    for line in lines:
        if not changed and line.startswith("pos-x-16.16 "):
            parts = line.split()
            parts[1] = str(int(parts[1]) + delta)
            out.append(" ".join(parts) + "\n")
            changed = True
        else:
            out.append(line)
    return out


def write_manifest(directory, rows):
    path = os.path.join(directory, "manifest.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"rows": rows}, handle)
    return path


class EqScenesManifestTest(unittest.TestCase):
    """Table, verdict and exit codes over the committed fixtures."""

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_help(self):
        result = run_tool("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--manifest", result.stdout)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_table_has_one_row_per_scene_and_reports_the_real_fail(self):
        result = run_tool("--manifest", MANIFEST)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        for scene in ("cube", "scene1", "scene2", "scene3", "scene4",
                      "scene5", "cubes"):
            self.assertIn("%-8s" % scene, result.stdout)
        self.assertIn("polys/verts", result.stdout)
        # The real scene-5 colour finding must not be hidden.
        self.assertIn("MISMATCH r: 63 > 2", result.stdout)
        self.assertIn("63", result.stdout)
        self.assertIn("summary: 7 rows", result.stdout)
        self.assertIn("1 FAIL", result.stdout)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_pass_and_not_comparable_rows_exit_zero(self):
        result = run_tool("--manifest", PASS_MANIFEST)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("NOT-COMPARABLE", result.stdout)
        self.assertIn("summary: 4 rows, 3 PASS, 0 FAIL, 1 NOT-COMPARABLE",
                      result.stdout)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_perturbed_fixture_is_a_fail_row_and_exit_one(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-perturb-")
        try:
            perturbed = write_lines(
                tmp, "harness_s1.txt",
                perturb_first_x(read_lines(HARNESS_S1),
                                eq2_compare.TOL_XY_16_16 + 10000))
            manifest = write_manifest(tmp, [{
                "scene": "scene1",
                "build": "SCENE=1 AUTODUMP_FRAME=240",
                "harness": perturbed,
                "ours": OURS_S1,
                "expected_polys": 8,
                "expected_verts": 24,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 1,
                             result.stdout + result.stderr)
            self.assertIn("scene1", result.stdout)
            self.assertIn("FAIL", result.stdout)
            self.assertIn("MISMATCH x", result.stdout)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_harness_frame_mismatch_is_not_comparable_not_a_fail(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-hframe-")
        try:
            harness = write_lines(
                tmp, "harness_s1_f243.txt",
                [line.replace("requested-frame 242", "requested-frame 243")
                      .replace("captured-frame 242", "captured-frame 243")
                 for line in read_lines(HARNESS_S1)])
            manifest = write_manifest(tmp, [{
                "scene": "scene1",
                "build": "SCENE=1 AUTODUMP_FRAME=240",
                "harness": harness,
                "ours": OURS_S1,
                "expected_polys": 8,
                "expected_verts": 24,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)
            self.assertIn("NOT-COMPARABLE", result.stdout)
            self.assertIn("frame alignment: harness has requested 243 "
                          "captured 243, expected 242", result.stdout)
            self.assertIn("0 compared (reference 8/24)", result.stdout)
            self.assertIn("1 NOT-COMPARABLE", result.stdout)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_ours_frame_mismatch_is_not_comparable_not_a_fail(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-oframe-")
        try:
            lines = []
            changed = False
            for line in read_lines(OURS_S1):
                if not changed and line.startswith("frame 241"):
                    line = line.replace("frame 241", "frame 240")
                    changed = True
                lines.append(line)
            self.assertTrue(changed, "fixture has no 'frame 241' line")
            ours = write_lines(tmp, "ours_s1_f240line.txt", lines)
            manifest = write_manifest(tmp, [{
                "scene": "scene1",
                "build": "SCENE=1 AUTODUMP_FRAME=240",
                "harness": HARNESS_S1,
                "ours": ours,
                "expected_polys": 8,
                "expected_verts": 24,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)
            self.assertIn("NOT-COMPARABLE", result.stdout)
            self.assertIn("frame alignment: our recorder frame is 240, "
                          "expected 241", result.stdout)
            self.assertIn("1 NOT-COMPARABLE", result.stdout)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_unloadable_harness_dump_is_not_comparable(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-trunc-")
        try:
            harness = write_lines(tmp, "harness_truncated.txt",
                                  read_lines(HARNESS_S1)[:4])
            manifest = write_manifest(tmp, [{
                "scene": "scene1",
                "build": "SCENE=1 AUTODUMP_FRAME=240",
                "harness": harness,
                "ours": OURS_S1,
                "expected_polys": 8,
                "expected_verts": 24,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)
            self.assertIn("NOT-COMPARABLE", result.stdout)
            self.assertIn("harness dump is unusable", result.stdout)
            self.assertIn("1 NOT-COMPARABLE", result.stdout)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_polygon_count_mismatch_is_a_fail_and_exit_one(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-count-")
        try:
            manifest = write_manifest(tmp, [{
                "scene": "scene1",
                "build": "SCENE=1 AUTODUMP_FRAME=240",
                "harness": HARNESS_S1,
                "ours": OURS_S1,
                "expected_polys": 9,
                "expected_verts": 24,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 1,
                             result.stdout + result.stderr)
            self.assertIn("FAIL", result.stdout)
            self.assertIn("harness dumped 8 polygons, expected 9",
                          result.stdout)
            self.assertIn("1 FAIL", result.stdout)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_missing_prerequisite_is_exit_two_before_any_run(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-missing-")
        try:
            result = run_tool(
                "--harness-bin", os.path.join(tmp, "no-harness"),
                "--desmume-cli", os.path.join(tmp, "no-desmume"),
                "--rom-dir", os.path.join(tmp, "no-roms"),
                "--gx-interp-cli", os.path.join(tmp, "no-cli"))
            self.assertEqual(result.returncode, 2,
                             result.stdout + result.stderr)
            self.assertIn("harness binary", result.stderr)
            self.assertIn("ROM directory", result.stderr)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_missing_manifest_input_is_exit_two(self):
        tmp = tempfile.mkdtemp(prefix="eq-scenes-input-")
        try:
            manifest = write_manifest(tmp, [{
                "scene": "cube",
                "harness": os.path.join(tmp, "missing.txt"),
                "ours": OURS_S1,
            }])
            result = run_tool("--manifest", manifest)
            self.assertEqual(result.returncode, 2,
                             result.stdout + result.stderr)
            self.assertIn("does not exist", result.stderr)
        finally:
            shutil.rmtree(tmp)

    @unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                     "run once via the test-eq-scenes make target; discover "
                     "skips it to avoid a double run")
    def test_missing_manifest_path_is_exit_two(self):
        result = run_tool("--manifest", "/nonexistent/eq-scenes.json")
        self.assertEqual(result.returncode, 2,
                         result.stdout + result.stderr)
        self.assertIn("manifest does not exist", result.stderr)


@unittest.skipIf(EQ_SCENES_DISCOVER_SKIP,
                 "run once via the test-eq-scenes make target; discover "
                 "skips it to avoid a double run")
class EqScenesMutantTest(unittest.TestCase):
    """Show the suite depends on the real verdict rule and per-scene loop."""

    def _main(self, manifest):
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            return eq_scenes.main(["--manifest", manifest])

    def test_verdict_aggregation_mutant_would_accept_a_fail(self):
        self.assertEqual(self._main(MANIFEST), 1)
        original = eq_scenes.aggregate
        eq_scenes.aggregate = lambda results: 0
        try:
            self.assertEqual(
                self._main(MANIFEST), 0,
                "a broken verdict rule must wrongly accept the FAIL row")
        finally:
            eq_scenes.aggregate = original

    def test_per_scene_loop_mutant_would_hide_the_fail_row(self):
        self.assertEqual(self._main(MANIFEST), 1)
        original = eq_scenes.select_rows
        eq_scenes.select_rows = lambda rows: rows[:1]
        try:
            self.assertEqual(
                self._main(MANIFEST), 0,
                "processing only the first row must wrongly hide the FAIL")
        finally:
            eq_scenes.select_rows = original


if __name__ == "__main__":
    unittest.main()
