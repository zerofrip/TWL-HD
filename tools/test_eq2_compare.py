#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq2_compare.py (EQ-2b).

Drives the committed fixture pair (real DESCube frame from the GPL harness plus
our interpreter's --eq2 output) through the CLI for exit codes, then imports the
module to prove the tolerance and count checks are what make the negative tests
fail (the same in-process mutant pattern as tools/test_emulator_compare.py).

Runs under `make test` without devkitPro or an emulator.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import eq2_compare  # noqa: E402

FIXTURES = os.path.join(TOOLS_DIR, "fixtures", "eq2")
HARNESS = os.path.join(FIXTURES, "harness_cube_f240.txt")
OURS = os.path.join(FIXTURES, "ours_cube_f240.txt")
CLI = os.path.join(TOOLS_DIR, "eq2_compare.py")


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


def drop_vertex_block(lines, vertex=3):
    """Remove one complete vtx block but keep the declared vertex count."""
    out = []
    removed = False
    skipping = False
    for line in lines:
        if not skipping and not removed and line.strip() == "vtx %d" % vertex:
            skipping = True
            removed = True
            continue
        if skipping:
            if line.strip() == "end-poly" or line.startswith("vtx "):
                skipping = False
            else:
                continue
        out.append(line)
    return out


def drop_last_polygon(lines):
    """Remove the final poly..end-poly block but keep the header counts."""
    start = None
    for index, line in enumerate(lines):
        if line.startswith("poly "):
            start = index
    if start is None:
        return lines
    end = start
    while lines[end].strip() != "end-poly":
        end += 1
    return lines[:start] + lines[end + 1:]


class Eq2CompareCliTest(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq2-")
        self.harness_lines = read_lines(HARNESS)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_help(self):
        result = run_tool("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("harness", result.stdout)

    def test_positive_pair_passes(self):
        result = run_tool(HARNESS, OURS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(result.stdout.startswith("PASS"))
        self.assertIn("polygons=6 vertices=24", result.stdout)

    def test_perturbation_beyond_tolerance_fails(self):
        path = write_lines(
            self.tmp, "beyond.txt",
            perturb_first_x(self.harness_lines, eq2_compare.TOL_XY_16_16 + 10000))
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("MISMATCH x", result.stdout)
        self.assertIn("poly 0", result.stdout)
        self.assertIn("vertex 0", result.stdout)

    def test_perturbation_inside_tolerance_passes(self):
        path = write_lines(
            self.tmp, "inside.txt",
            perturb_first_x(self.harness_lines, eq2_compare.TOL_XY_16_16 // 2))
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_vertex_fails(self):
        path = write_lines(self.tmp, "missing-vertex.txt",
                           drop_vertex_block(self.harness_lines))
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("declared 4 vertices, found 3", result.stderr)

    def test_polygon_count_mismatch_fails(self):
        path = write_lines(self.tmp, "missing-poly.txt",
                           drop_last_polygon(self.harness_lines))
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("polygon-count", result.stderr)

    def test_malformed_magic_is_a_format_error(self):
        lines = list(self.harness_lines)
        lines[0] = "not-a-harness-dump\n"
        path = write_lines(self.tmp, "bad-magic.txt", lines)
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("format error", result.stderr)

    def test_malformed_field_count_is_a_format_error(self):
        lines = []
        for line in self.harness_lines:
            if line.startswith("pos-x-16.16 "):
                lines.append("pos-x-16.16\n")
            else:
                lines.append(line)
        path = write_lines(self.tmp, "bad-field.txt", lines)
        result = run_tool(path, OURS)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("format error", result.stderr)

    def test_missing_selected_frame_is_a_usage_error(self):
        result = run_tool("--harness-frame", "999", HARNESS, OURS)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("not present", result.stderr)


class Eq2CompareMutantTest(unittest.TestCase):
    """Show the negative tests depend on the real tolerance and count checks."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="eq2-mutant-")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_relaxed_tolerance_would_accept_the_beyond_case(self):
        path = write_lines(
            self.tmp, "beyond.txt",
            perturb_first_x(read_lines(HARNESS),
                            eq2_compare.TOL_XY_16_16 + 10000))
        harness = eq2_compare.load_harness(path)
        ours = eq2_compare.load_ours(OURS)
        code, _ = eq2_compare.compare(harness, ours)
        self.assertEqual(code, 1)

        original = eq2_compare.TOL_XY_16_16
        eq2_compare.TOL_XY_16_16 = 1 << 40
        try:
            code, _ = eq2_compare.compare(harness, ours)
        finally:
            eq2_compare.TOL_XY_16_16 = original
        self.assertEqual(code, 0,
                         "dropping the x tolerance must wrongly accept it")

    def test_dropped_count_check_would_accept_a_missing_polygon(self):
        path = write_lines(self.tmp, "missing-poly.txt",
                           drop_last_polygon(read_lines(HARNESS)))
        original = eq2_compare.ENFORCE_COUNTS
        eq2_compare.ENFORCE_COUNTS = False
        try:
            harness = eq2_compare.load_harness(path)
        finally:
            eq2_compare.ENFORCE_COUNTS = original
        ours = eq2_compare.load_ours(OURS)
        code, _ = eq2_compare.compare(harness, ours)
        self.assertEqual(code, 1)

        eq2_compare.ENFORCE_COUNTS = False
        try:
            code, _ = eq2_compare.compare(harness, ours)
        finally:
            eq2_compare.ENFORCE_COUNTS = original
        self.assertEqual(code, 0,
                         "dropping the count check must wrongly accept it")


if __name__ == "__main__":
    unittest.main()
