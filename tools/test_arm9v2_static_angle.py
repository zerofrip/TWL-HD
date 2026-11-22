#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the arm9v2 STATIC_ANGLE build option.

Dry-run `make -C arm9v2 -n` only: no ROM is built and no emulator/hardware is
used.  Follows the existing Makefile dry-run pattern in
tools/test_emulator_texture_compare.py.  The cases that need devkitPro are
skipped with a labelled reason when DEVKITARM/DEVKITPRO are absent.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import shutil
import subprocess
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)

DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))
# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-arm9v2-static target.
DISCOVER_SKIP = os.environ.get("ARM9V2_STATIC_DISCOVER_SKIP") is not None

ERROR_TEXT = "STATIC_ANGLE must be empty or a decimal integer in 0..359"


def dry_run(*variables, cwd=ROOT):
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n"] + list(variables),
        cwd=cwd, capture_output=True, text=True, timeout=120)


def without_base(text, base):
    # make echoes absolute paths (e.g. "Entering directory '<base>/arm9v2'"),
    # so a base such as .../work-static5 would itself contain "-st".
    for prefix in (base, os.path.realpath(base)):
        text = text.replace(prefix, "")
    return text


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-arm9v2-static make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class StaticAngleMakefileTest(unittest.TestCase):
    def assertNoStaticSuffix(self, text, base):
        # The only "-st" make may derive is the STATIC_ANGLE suffix "-st<digits>"
        # (arm9v2/Makefile); flags such as -std= or -static do not match.
        self.assertNotRegex(without_base(text, base), r"-st[0-9]")

    def test_default_rom_name_unchanged(self):
        result = dry_run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2.nds", result.stdout + result.stderr)
        self.assertNoStaticSuffix(result.stdout, ROOT)

    def test_empty_value_behaves_like_unset(self):
        result = dry_run("SCENE=5", "AUTODUMP_FRAME=240", "STATIC_ANGLE=")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-autodump-s5.nds",
                      result.stdout + result.stderr)
        self.assertNoStaticSuffix(result.stdout, ROOT)

    def test_path_containing_dash_st_does_not_affect_assertions(self):
        # Pin the path-independence: the OLD assertNotIn("-st", stdout) failed
        # here because the copy's absolute path contains "-st" in "x-static-y".
        copy = tempfile.mkdtemp(prefix="x-static-y-")
        self.addCleanup(shutil.rmtree, copy)
        self.assertIn("-st", copy)
        ignore = shutil.ignore_patterns("build", "build-*")
        for name in ("arm9v2", "common"):
            shutil.copytree(os.path.join(ROOT, name),
                            os.path.join(copy, name), ignore=ignore)
        cases = (
            ((), "twlhd-arm9v2.nds"),
            (("SCENE=5", "AUTODUMP_FRAME=240", "STATIC_ANGLE="),
             "twlhd-arm9v2-autodump-s5.nds"),
        )
        for variables, rom in cases:
            with self.subTest(variables=variables):
                result = dry_run(*variables, cwd=copy)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(rom, result.stdout + result.stderr)
                self.assertNoStaticSuffix(result.stdout, copy)

    def test_scene5_static_angle_30_rom_name(self):
        result = dry_run("SCENE=5", "AUTODUMP_FRAME=240", "STATIC_ANGLE=30")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-autodump-s5-st30.nds",
                      result.stdout + result.stderr)

    def test_static_angle_zero_is_a_variant_not_unset(self):
        result = dry_run("SCENE=5", "STATIC_ANGLE=0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-s5-st0.nds", result.stdout + result.stderr)

    def test_static_angle_boundary_359_accepted(self):
        result = dry_run("SCENE=5", "STATIC_ANGLE=359")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-s5-st359.nds",
                      result.stdout + result.stderr)

    def test_invalid_values_fail_with_a_clear_error(self):
        for value in ("abc", "360", "-5", "3.5", "030"):
            with self.subTest(value=value):
                result = dry_run("STATIC_ANGLE=" + value)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(ERROR_TEXT, result.stderr)


if __name__ == "__main__":
    unittest.main()
