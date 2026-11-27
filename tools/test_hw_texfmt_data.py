# SPDX-License-Identifier: Apache-2.0
"""Tests for the TEXFMT=1 ROM data (tools/hw/gen_texfmt_rom_data.py).

The committed arm9v2/source/texfmt_data.h must equal the generator output, and
every texture read back out of the three VRAM images at the offsets the ROM
programs must decode (through tools/tex_decode_oracle.py) to exactly the pixels
the 5B-2e-2 generator expects.  Dry-run `make -n` tests cover the TEXFMT switch
(no ROM is built).
"""

import os
import subprocess
import sys
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import argparse  # noqa: E402
import check_tex_formats as checker  # noqa: E402
import gen_tex_format_frames as gen  # noqa: E402
import gen_texfmt_rom_data as rom  # noqa: E402
import tex_decode_oracle as oracle  # noqa: E402

HEADER = os.path.join(ROOT, "arm9v2", "source", "texfmt_data.h")
DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))


class TexfmtDataTest(unittest.TestCase):
    def test_header_matches_generator(self):
        with open(HEADER, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), rom.header_text())

    def test_header_has_no_home_path(self):
        with open(HEADER, encoding="utf-8") as handle:
            self.assertNotIn("/home/", handle.read())

    def test_textures_decode_from_the_vram_images(self):
        slot0, slot1, palette, quads = rom.layout()
        self.assertEqual(len(quads), 8)
        for index, spec in enumerate(gen.QUAD_SPECS):
            quad = quads[index]
            t_off = (quad["param"] & 0xFFFF) << 3
            self.assertEqual(t_off, rom.TEXEL_OFFSETS[index])
            texel = slot0[t_off:t_off + len(spec["texel"])]
            if spec["palette"]:
                shift = 3 if spec["format"] == 2 else 4
                p_off = quad["pltt"] << shift
                self.assertEqual(p_off, rom.PALETTE_OFFSETS[index])
                pal = palette[p_off:p_off + len(spec["palette"])]
            else:
                pal = b""
            idx = b""
            if spec["index"]:
                i_off = t_off >> 1
                idx = slot1[i_off:i_off + len(spec["index"])]
            with self.subTest(quad=index, fmt=spec["format"]):
                self.assertEqual(
                    oracle.decode(quad["param"], texel, pal, idx),
                    gen.expected_pixels(spec))

    def test_images_do_not_overlap(self):
        ranges = []
        for index, spec in enumerate(gen.QUAD_SPECS):
            start = rom.TEXEL_OFFSETS[index]
            ranges.append((start, start + len(spec["texel"])))
        ranges.sort()
        for (a0, a1), (b0, _b1) in zip(ranges, ranges[1:]):
            self.assertLessEqual(a1, b0)
        self.assertLessEqual(ranges[-1][1], rom.SLOT0_BYTES)

    def test_wrong_offset_fails_the_decode(self):
        # Negative test: reading one texture one texel row too early must not
        # reproduce the expected pixels.
        slot0, _slot1, palette, quads = rom.layout()
        spec = gen.QUAD_SPECS[3]
        t_off = rom.TEXEL_OFFSETS[3] - 8
        texel = slot0[t_off:t_off + len(spec["texel"])]
        pal = palette[rom.PALETTE_OFFSETS[3]:rom.PALETTE_OFFSETS[3] +
                      len(spec["palette"])]
        self.assertNotEqual(
            oracle.decode(quads[3]["param"], texel, pal, b""),
            gen.expected_pixels(spec))


class ClearOptionTest(unittest.TestCase):
    def test_parse_clear_accepts_rgb(self):
        self.assertEqual(checker._parse_clear("0,0,0"), (0, 0, 0))
        self.assertEqual(checker._parse_clear("16,16,24"), (16, 16, 24))

    def test_parse_clear_rejects_bad_input(self):
        for text in ("1,2", "1,2,3,4", "a,b,c", "256,0,0", "-1,0,0"):
            with self.subTest(text=text):
                with self.assertRaises(argparse.ArgumentTypeError):
                    checker._parse_clear(text)

    def test_default_clear_is_unchanged(self):
        self.assertEqual(checker.CLEAR, (16, 16, 24))


def dry_run(*variables):
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n", "-B"] + list(variables),
        cwd=ROOT, capture_output=True, text=True, timeout=120)


@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class TexfmtMakefileTest(unittest.TestCase):
    def test_texfmt_names_rom_and_defines(self):
        result = dry_run("TEXFMT=1")
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("twlhd-arm9v2-texfmt.nds", out)
        self.assertIn("-DTWLHD_DEMO_TEXFMT=1", out)

    def test_empty_is_like_unset(self):
        result = dry_run("TEXFMT=")
        out = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("-DTWLHD_DEMO_TEXFMT=", out)

    def test_invalid_values_and_combinations_fail(self):
        for variables, text in (
                (("TEXFMT=2",), "TEXFMT must be empty or 1"),
                (("TEXFMT=abc",), "TEXFMT must be empty or 1"),
                (("TEXFMT=1", "LIT=1"), "cannot be combined with LIT"),
                (("TEXFMT=1", "AUTODUMP_FRAME=240"),
                 "cannot be combined with AUTODUMP_FRAME")):
            with self.subTest(variables=variables):
                result = dry_run(*variables)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(text, result.stderr)


if __name__ == "__main__":
    unittest.main()
