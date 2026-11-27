#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the arm9v2 LIT build option.

Dry-run `make -C arm9v2 -n` only: no ROM is built and no emulator/hardware is
used.  Follows the existing Makefile dry-run pattern in
`tools/test_arm9v2_blend.py`.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import re
import subprocess
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)

DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))
# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-arm9v2-lit target.
DISCOVER_SKIP = os.environ.get("ARM9V2_LIT_DISCOVER_SKIP") is not None

ERROR_TEXT = "LIT must be empty or 1..6"

LIT_TOKENS = (
    "glLight",
    "glMaterialf",
    "glMaterialShinyness",
    "glShininess",
    "GFX_LIGHT_VECTOR",
    "GFX_LIGHT_COLOR",
    "GFX_DIFFUSE_AMBIENT",
    "GFX_SPECULAR_EMISSION",
    "GFX_SHININESS",
    "POLY_FORMAT_LIGHT0",
    "POLY_FORMAT_LIGHT1",
    "POLY_FORMAT_LIGHT2",
    "POLY_FORMAT_LIGHT3",
)

GLNORMAL_ALLOWLIST = (
    (os.path.join(ROOT, "arm9v2/source/demo_frame.cpp"), 104),
    (os.path.join(ROOT, "arm9v2/source/demo_frame.cpp"), 392),
)


def _is_lit_guard(line):
    """Return True if this preprocessor line starts/continues a LIT guard."""
    s = line.strip()
    # #ifndef and negated/zero forms are NOT guards.
    if s.startswith("#ifndef"):
        return False
    if re.search(r'^#if\s+!\s*TWLHD_DEMO_LIT\b', s):
        return False
    if re.search(r'^#if\s+TWLHD_DEMO_LIT\s*==\s*0\b', s):
        return False
    # #ifdef / #if containing TWLHD_DEMO_LIT (including == 1 / == 2 / != 0).
    return "TWLHD_DEMO_LIT" in s


def _strip_comments(text):
    """Remove C/C++ comments from `text`; keep newlines for line alignment."""
    result = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == '/' and i + 1 < n:
            if text[i + 1] == '/':
                # Skip to end of line.
                while i < n and text[i] != '\n':
                    i += 1
                continue
            elif text[i + 1] == '*':
                # Skip C-style comment, preserving newlines.
                i += 2
                while i < n - 1 and not (text[i] == '*' and text[i + 1] == '/'):
                    if text[i] == '\n':
                        result.append('\n')
                    i += 1
                i += 2
                continue
        result.append(text[i])
        i += 1
    return ''.join(result)


def scan_unguarded_lit_sites(lines):
    """Return every lighting-port site and the subset outside the LIT guard.

    `sites` is a list of (lineno, line, token) for every use of a LIT lighting
    port.  `violations` is the subset that is not inside a #if / #ifdef
    TWLHD_DEMO_LIT guard (including nested #if TWLHD_DEMO_LIT == 1/2).
    """
    guard_stack = []  # tuples: (is this block a LIT guard?, in #else branch?)
    sites = []
    violations = []

    # Strip comments so documentation mentions of tokens are not counted.
    code_only = _strip_comments('\n'.join(lines)).split('\n')

    for lineno, line in enumerate(code_only, 1):
        stripped = line.strip()
        if stripped.startswith("#if") or stripped.startswith("#ifdef"):
            guard_stack.append([_is_lit_guard(stripped), False])
        elif stripped.startswith("#elif"):
            if guard_stack:
                guard_stack.pop()
            guard_stack.append([_is_lit_guard(stripped), False])
        elif stripped.startswith("#else"):
            if guard_stack:
                guard_stack[-1][1] = True
        elif stripped.startswith("#endif"):
            if guard_stack:
                guard_stack.pop()

        inside_lit_guard = any(is_lit and not in_else
                               for is_lit, in_else in guard_stack)
        for token in LIT_TOKENS:
            if token in line:
                sites.append((lineno, line, token))
                if not inside_lit_guard:
                    violations.append((lineno, line, token))
                break

    return sites, violations


def dry_run(*variables, cwd=ROOT):
    # Use -B so make echoes compile commands even when the build directory is
    # up to date; this lets tests verify that -DTWLHD_DEMO_LIT=n is passed.
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n", "-B"] + list(variables),
        cwd=cwd, capture_output=True, text=True, timeout=120)


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-arm9v2-lit make target; discover "
                 "skips it to avoid a double run")
@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class LitMakefileTest(unittest.TestCase):
    def test_lit_one_names_lit1_rom(self):
        result = dry_run("LIT=1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit1.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=1", result.stdout + result.stderr)

    def test_lit_two_names_lit2_rom(self):
        result = dry_run("LIT=2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit2.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=2", result.stdout + result.stderr)

    def test_lit_three_names_lit3_rom(self):
        result = dry_run("LIT=3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit3.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=3", result.stdout + result.stderr)

    def test_lit_four_names_lit4_rom(self):
        result = dry_run("LIT=4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit4.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=4", result.stdout + result.stderr)

    def test_lit_five_names_lit5_rom(self):
        result = dry_run("LIT=5")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit5.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=5", result.stdout + result.stderr)

    def test_lit_six_names_lit6_rom(self):
        result = dry_run("LIT=6")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-lit6.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=6", result.stdout + result.stderr)

    def test_lit_composes_with_autodump(self):
        result = dry_run("LIT=1", "AUTODUMP_FRAME=240")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-autodump-lit1.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=1", result.stdout + result.stderr)

    def test_lit_composes_with_scene_and_static_angle(self):
        result = dry_run("SCENE=4", "LIT=1", "STATIC_ANGLE=30")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-s4-lit1-st30.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_SCENE=4", result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=1", result.stdout + result.stderr)

    def test_lit_composes_after_blend_before_static_angle(self):
        result = dry_run("BLEND=1", "STATIC_ANGLE=30", "LIT=2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-bl-lit2-st30.nds",
                      result.stdout + result.stderr)
        self.assertIn("-DTWLHD_DEMO_LIT=2", result.stdout + result.stderr)

    def test_empty_value_behaves_like_unset(self):
        result = dry_run("LIT=")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2.nds",
                      result.stdout + result.stderr)
        self.assertNotIn("-DTWLHD_DEMO_LIT=",
                         result.stdout + result.stderr)

    def test_invalid_values_fail_with_a_clear_error(self):
        for value in ("0", "7", "abc", "-1"):
            with self.subTest(value=value):
                result = dry_run("LIT=" + value)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(ERROR_TEXT, result.stderr)

    def test_lit_source_sites_are_guarded_by_lit_define(self):
        """Every lighting-port site is inside a TWLHD_DEMO_LIT guard."""
        src_dir = os.path.join(ROOT, "arm9v2", "source")
        all_sites = []
        all_violations = []
        glnormal_sites = []
        for dirpath, _dirnames, filenames in os.walk(src_dir):
            for name in filenames:
                path = os.path.join(dirpath, name)
                try:
                    with open(path, "r", encoding="utf-8") as f:
                        lines = f.read().splitlines()
                except UnicodeDecodeError:
                    continue
                sites, violations = scan_unguarded_lit_sites(lines)
                all_sites.extend(
                    (path, lineno, line, token)
                    for lineno, line, token in sites)
                all_violations.extend(
                    (path, lineno, line, token)
                    for lineno, line, token in violations)
                code_only = _strip_comments('\n'.join(lines)).split('\n')
                for lineno, line in enumerate(code_only, 1):
                    if "glNormal" in line:
                        glnormal_sites.append((path, lineno, line))
        self.assertEqual(
            all_violations, [],
            "found lighting-port sites outside a TWLHD_DEMO_LIT guard")
        # lit helper: 2 direct material writes
        # draw_lit_a: 1 glLight + 4 POLY
        # draw_lit_b: 4 glLight + 1 GFX_SHININESS + 4 POLY
        # draw_lit_c: 1 glLight + 4 POLY
        # draw_lit_d: 2 glLight + 1 GFX_SHININESS + 4 POLY
        # draw_lit_e: 1 GFX_SHININESS + 1 glLight + 1 POLY (loop body; 3 sites)
        # draw_lit_f: 1 glLight + 1 POLY (loop body; 2 sites)
        self.assertEqual(len(all_sites), 33,
                         "expected exactly 33 guarded lighting-port sites")
        # glNormal is allowed only for the cube and the lit helper.
        self.assertEqual(sorted((p, l) for p, l, _ in glnormal_sites),
                         sorted(GLNORMAL_ALLOWLIST),
                         "glNormal sites changed")

    def test_scanner_catches_unguarded_lit_site(self):
        """Negative self-check: the scanner flags an unguarded port write."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        # Insert an unguarded glLight site before the first guarded one.
        insert_idx = None
        for i, line in enumerate(lines):
            if "#if TWLHD_DEMO_LIT" in line:
                insert_idx = i
                break
        self.assertIsNotNone(insert_idx)
        mutated = list(lines)
        mutated.insert(insert_idx, "    glLight(0, 0, 0, 0, 0);")
        sites, violations = scan_unguarded_lit_sites(mutated)
        self.assertEqual(len(sites), 34)
        self.assertEqual(len(violations), 1,
                         "mutated copy should produce exactly one violation")
        self.assertIn("glLight", violations[0][2])

    def test_scanner_else_branch_is_not_guarded(self):
        """Negative self-check: a site in the #else of a LIT guard is flagged."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        mutated = [
            "#if TWLHD_DEMO_LIT",
            "#else",
            "    glLight(0, 0, 0, 0, 0);",
            "#endif",
        ] + lines
        sites, violations = scan_unguarded_lit_sites(mutated)
        self.assertGreaterEqual(len(violations), 1,
                                "#else branch of LIT guard must be flagged")
        self.assertIn("glLight", violations[0][2])

    def test_scanner_negated_lit_is_not_guarded(self):
        """Negative self-check: #ifndef TWLHD_DEMO_LIT is not a guard."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        mutated = [
            "#ifndef TWLHD_DEMO_LIT",
            "    glLight(0, 0, 0, 0, 0);",
            "#endif",
        ] + lines
        sites, violations = scan_unguarded_lit_sites(mutated)
        self.assertGreaterEqual(len(violations), 1,
                                "#ifndef TWLHD_DEMO_LIT must not be a guard")
        self.assertIn("glLight", violations[0][2])

    def test_scanner_bang_lit_is_not_guarded(self):
        """Negative self-check: #if !TWLHD_DEMO_LIT is not a guard."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        mutated = [
            "#if !TWLHD_DEMO_LIT",
            "    glLight(0, 0, 0, 0, 0);",
            "#endif",
        ] + lines
        sites, violations = scan_unguarded_lit_sites(mutated)
        self.assertGreaterEqual(len(violations), 1,
                                "#if !TWLHD_DEMO_LIT must not be a guard")
        self.assertIn("glLight", violations[0][2])

    def test_scanner_lit_equals_zero_is_not_guarded(self):
        """Negative self-check: #if TWLHD_DEMO_LIT == 0 is not a guard."""
        src = os.path.join(ROOT, "arm9v2/source/demo_frame.cpp")
        with open(src, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        mutated = [
            "#if TWLHD_DEMO_LIT == 0",
            "    glLight(0, 0, 0, 0, 0);",
            "#endif",
        ] + lines
        sites, violations = scan_unguarded_lit_sites(mutated)
        self.assertGreaterEqual(len(violations), 1,
                                "#if TWLHD_DEMO_LIT == 0 must not be a guard")
        self.assertIn("glLight", violations[0][2])

    def test_clean_rule_removes_lit_artifacts(self):
        """The clean rule removes LIT ROMs with suffixes before -litN."""
        result = subprocess.run(
            ["make", "-C", "arm9v2", "-n", "clean"],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout + result.stderr
        self.assertIn("twlhd-arm9v2*-lit*.elf", output)
        self.assertIn("twlhd-arm9v2*-lit*.nds", output)
        self.assertIn("twlhd-arm9v2*-lit*.map", output)


if __name__ == "__main__":
    unittest.main()
