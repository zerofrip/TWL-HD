#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the arm9v2 BLEND build option.

Dry-run `make -C arm9v2 -n` only: no ROM is built and no emulator/hardware is
used.  Follows the existing Makefile dry-run pattern in
`tools/test_arm9v2_static_angle.py`.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import subprocess
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)

DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))
# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-arm9v2-blend target.
DISCOVER_SKIP = os.environ.get("ARM9V2_BLEND_DISCOVER_SKIP") is not None

ERROR_TEXT = "BLEND must be empty or 1"


def scan_unguarded_blend_sites(lines):
    """Return every glEnable(GL_BLEND) site and the subset outside the guard.

    `sites` is a list of (lineno, line) for every glEnable(GL_BLEND) call.
    `violations` is the subset that is not inside an #ifdef TWLHD_DEMO_BLEND
    guard.
    """
    guard = None  # None, 'ifdef', or 'endif' for the blend guard
    sites = []
    violations = []
    for lineno, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("#ifdef TWLHD_DEMO_BLEND"):
            guard = "ifdef"
        elif stripped.startswith("#endif") and guard is not None:
            guard = None
        if "glEnable(GL_BLEND)" in line:
            sites.append((lineno, line))
            if guard != "ifdef":
                violations.append((lineno, line))
    return sites, violations


def dry_run(*variables, cwd=ROOT):
    # Use -B so make echoes compile commands even when the build directory is
    # up to date; this lets tests verify that -DTWLHD_DEMO_BLEND=1 is passed.
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n", "-B"] + list(variables),
        cwd=cwd, capture_output=True, text=True, timeout=120)


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-arm9v2-blend make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class BlendMakefileTest(unittest.TestCase):
    def test_blend_one_names_scene4_static_angle_rom(self):
        result = dry_run("SCENE=4", "STATIC_ANGLE=30", "BLEND=1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-s4-bl-st30.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_BLEND=1", result.stdout + result.stderr)

    def test_blend_composes_with_autodump(self):
        result = dry_run("SCENE=4", "STATIC_ANGLE=30", "BLEND=1",
                         "AUTODUMP_FRAME=240")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-autodump-s4-bl-st30.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_BLEND=1", result.stdout + result.stderr)

    def test_blend_composes_after_cubes_before_static_angle(self):
        result = dry_run("CUBES=2", "STATIC_ANGLE=30", "BLEND=1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-c2-bl-st30.nds",
                      result.stdout + result.stderr)

    def test_empty_value_behaves_like_unset(self):
        result = dry_run("SCENE=4", "STATIC_ANGLE=30", "BLEND=")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-s4-st30.nds",
                      result.stdout + result.stderr)
        self.assertNotIn("-DTWLHD_DEMO_BLEND=1",
                         result.stdout + result.stderr)

    def test_invalid_values_fail_with_a_clear_error(self):
        for value in ("0", "2", "abc", "-1"):
            with self.subTest(value=value):
                result = dry_run("SCENE=4", "STATIC_ANGLE=30",
                                 "BLEND=" + value)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(ERROR_TEXT, result.stderr)

    def test_blend_source_sites_are_guarded_by_blend_define(self):
        """Every glEnable(GL_BLEND) site is inside a TWLHD_DEMO_BLEND guard."""
        src_dir = os.path.join(ROOT, "arm9v2", "source")
        all_sites = []
        all_violations = []
        for dirpath, _dirnames, filenames in os.walk(src_dir):
            for name in filenames:
                if not name.endswith((".c", ".cpp", ".h")):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "r", encoding="utf-8") as f:
                    lines = f.read().splitlines()
                sites, violations = scan_unguarded_blend_sites(lines)
                all_sites.extend((path, lineno, line) for lineno, line in sites)
                all_violations.extend(
                    (path, lineno, line) for lineno, line in violations)
        self.assertEqual(
            all_violations, [],
            "found glEnable(GL_BLEND) sites outside a TWLHD_DEMO_BLEND guard")
        self.assertEqual(len(all_sites), 1,
                         "expected exactly one glEnable(GL_BLEND) site")

    def test_scanner_catches_unguarded_blend_site(self):
        """Negative self-check: the scanner flags an unguarded blend call."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        # Insert an unguarded glEnable(GL_BLEND) before the first guarded one.
        insert_idx = None
        for i, line in enumerate(lines):
            if "#ifdef TWLHD_DEMO_BLEND" in line:
                insert_idx = i
                break
        self.assertIsNotNone(insert_idx)
        mutated = list(lines)
        mutated.insert(insert_idx, "    glEnable(GL_BLEND);")
        sites, violations = scan_unguarded_blend_sites(mutated)
        self.assertEqual(len(sites), 2)
        self.assertEqual(len(violations), 1,
                         "mutated copy should produce exactly one violation")
        self.assertIn("glEnable(GL_BLEND);", violations[0][1])

    def test_clean_rule_removes_blend_artifacts(self):
        """The clean rule removes BLEND ROMs with suffixes before -bl."""
        result = subprocess.run(
            ["make", "-C", "arm9v2", "-n", "clean"],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout + result.stderr
        self.assertIn("twlhd-arm9v2*-bl*.elf", output)
        self.assertIn("twlhd-arm9v2*-bl*.nds", output)
        self.assertIn("twlhd-arm9v2*-bl*.map", output)


if __name__ == "__main__":
    unittest.main()
