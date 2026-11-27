# SPDX-License-Identifier: Apache-2.0
"""Tests for the TEXMOD=1 ROM data and measurement tool (EQ-4zk)."""

import os
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import gen_tex_format_frames as gen  # noqa: E402
import gen_texmod_rom_data as rom  # noqa: E402
import measure_texmod as mt  # noqa: E402
import tex_decode_oracle as oracle  # noqa: E402

HEADER = os.path.join(ROOT, "arm9v2", "source", "texmod_data.h")
MEASURE = os.path.join(TOOLS_DIR, "hw", "measure_texmod.py")
DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))


def _synthetic_ppm(name):
    """400x240 PPM as the RGB565 capture path would show model `name`."""
    width, height = 400, 240
    data = bytearray(width * height * 3)
    for index, (texel, vertex) in enumerate(rom.PAIRS):
        x0, y0, x1, y1 = gen.QUAD_SPECS[index]["rect"]
        colour = tuple(
            mt.capture_value(mt.capture_level_model(name, texel[c],
                                                    vertex[c], "RGB"[c]),
                             "RGB"[c]) for c in range(3))
        for y in range(y0, y1):
            for x in range(x0, x1):
                data[(y * width + x) * 3:(y * width + x) * 3 + 3] = \
                    bytes(colour)
    return b"P6\n400 240\n255\n" + bytes(data)


class TexmodDataTest(unittest.TestCase):
    def test_header_matches_generator(self):
        with open(HEADER, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), rom.header_text())
        with open(HEADER, encoding="utf-8") as handle:
            self.assertNotIn("/home/", handle.read())

    def test_pairs_are_valid_five_bit_levels(self):
        self.assertEqual(len(rom.PAIRS), 8)
        for texel, vertex in rom.PAIRS:
            for value in texel + vertex:
                self.assertTrue(0 <= value <= 31)

    def test_solid_textures_decode_to_their_colour(self):
        slot0, quads = rom.layout()
        for index, (texel, _vertex) in enumerate(rom.PAIRS):
            offset = (quads[index]["param"] & 0xFFFF) << 3
            pixels = oracle.decode(quads[index]["param"],
                                   slot0[offset:offset + 128], b"", b"")
            want = tuple(oracle.FIVE_TO_EIGHT[v] for v in texel) + (255,)
            with self.subTest(quad=index):
                self.assertEqual(set(pixels), {want})
                self.assertEqual(len(pixels), 64)

    def test_vertex_colours_match_pairs(self):
        _slot0, quads = rom.layout()
        for index, (_texel, vertex) in enumerate(rom.PAIRS):
            colour = quads[index]["colour"]
            self.assertEqual((colour & 31, (colour >> 5) & 31,
                              (colour >> 10) & 31), vertex)

    def test_models_known_values(self):
        self.assertEqual(mt.model_pica(31, 31), 255)
        self.assertEqual(mt.model_pica(31, 0), 0)
        self.assertEqual(mt.model_ds6(31, 31), 255)
        self.assertEqual(mt.model_ds6(0, 31), 0)
        # 6-bit: t6 = 33, v6 = 17 -> (34 * 18 - 1) >> 6 = 9 -> 36.
        self.assertEqual(mt.model_ds6(16, 8), 36)

    def test_models_differ_somewhere(self):
        # The probe is only informative if the two models disagree on it.
        diffs = [abs(mt.model_pica(t[c], v[c]) - mt.model_ds6(t[c], v[c]))
                 for t, v in rom.PAIRS for c in range(3)]
        self.assertGreater(max(diffs), 0)


class MeasureToolTest(unittest.TestCase):
    def _run(self, payload):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "in.ppm")
            with open(path, "wb") as handle:
                handle.write(payload)
            return subprocess.run([sys.executable, "-B", "-I", MEASURE, path],
                                  capture_output=True, text=True,
                                  check=False)

    def test_synthetic_capture_reports_zero_for_its_own_model(self):
        for name, _model in mt.MODELS:
            with self.subTest(model=name):
                proc = self._run(_synthetic_ppm(name))
                self.assertEqual(proc.returncode, 0, proc.stderr)
                lines = proc.stdout.strip().splitlines()
                self.assertIn("%s=24" % name, lines[-2])

    def test_other_model_does_not_match_everywhere(self):
        # Negative test: a pica-shaped capture is not 24/24 for ds6.
        proc = self._run(_synthetic_ppm("pica"))
        lines = proc.stdout.strip().splitlines()
        self.assertNotIn("ds6=24", lines[-2])

    def test_malformed_input_exits_two(self):
        self.assertEqual(self._run(b"P5\n1 1\n255\nx").returncode, 2)
        self.assertEqual(self._run(b"P6\n10 10\n255\n" + bytes(300))
                         .returncode, 2)


def dry_run(*variables):
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n", "-B"] + list(variables),
        cwd=ROOT, capture_output=True, text=True, timeout=120)


@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class TexmodMakefileTest(unittest.TestCase):
    def test_texmod_names_rom_and_defines(self):
        result = dry_run("TEXMOD=1")
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-texmod.nds", out)
        self.assertIn("-DTWLHD_DEMO_TEXMOD=1", out)

    def test_empty_is_like_unset(self):
        result = dry_run("TEXMOD=")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("-DTWLHD_DEMO_TEXMOD=",
                         result.stdout + result.stderr)

    def test_invalid_values_and_combinations_fail(self):
        for variables, text in (
                (("TEXMOD=2",), "TEXMOD must be empty or 1"),
                (("TEXMOD=1", "LIT=1"), "TEXMOD cannot be combined with LIT"),
                (("TEXMOD=1", "TEXFMT=1"),
                 "TEXMOD cannot be combined with TEXFMT"),
                (("TEXMOD=1", "AUTODUMP_FRAME=240"),
                 "TEXMOD cannot be combined with AUTODUMP_FRAME")):
            with self.subTest(variables=variables):
                result = dry_run(*variables)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(text, result.stderr)


if __name__ == "__main__":
    unittest.main()
