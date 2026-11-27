# SPDX-License-Identifier: Apache-2.0
"""Tests for the TEXBEH / GRAD / CULL ROM data and measurement tools (EQ-4zl/m/n)."""

import argparse
import os
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import check_tex_behaviour as behaviour_checker  # noqa: E402
import gen_gradcull_rom_data as gc  # noqa: E402
import gen_tex_behaviour_frames as beh  # noqa: E402
import gen_tex_format_frames as fmt  # noqa: E402
import gen_texbeh_rom_data as tb  # noqa: E402
import measure_cull as mcull  # noqa: E402
import measure_grad as mgrad  # noqa: E402
import tex_decode_oracle as oracle  # noqa: E402

SRC = os.path.join(ROOT, "arm9v2", "source")
DEVKIT = bool(os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"))
BLANK = b"P6\n400 240\n255\n" + bytes(400 * 240 * 3)


def _read(name):
    with open(os.path.join(SRC, name), encoding="utf-8") as handle:
        return handle.read()


def _paint(width, height, painter):
    data = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            colour = painter(x, y)
            if colour is not None:
                data[(y * width + x) * 3:(y * width + x) * 3 + 3] = \
                    bytes(colour)
    return b"P6\n%d %d\n255\n" % (width, height) + bytes(data)


def _run_tool(script, ppm_bytes, *extra):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "in.ppm")
        with open(path, "wb") as handle:
            handle.write(ppm_bytes)
        return subprocess.run(
            [sys.executable, "-B", "-I",
             os.path.join(TOOLS_DIR, "hw", script), path] + list(extra),
            capture_output=True, text=True, check=False)


class TexbehDataTest(unittest.TestCase):
    def test_header_matches_generator(self):
        text = _read("texbeh_data.h")
        self.assertEqual(text, tb.header_text())
        self.assertNotIn("/home/", text)

    def test_textures_decode_from_the_vram_images(self):
        slot0, palette, quads = tb.layout()
        for index, spec in enumerate(beh.QUAD_SPECS):
            quad = quads[index]
            t_off = (quad["param"] & 0xFFFF) << 3
            texel = slot0[t_off:t_off + len(spec["texel"])]
            pal = b""
            if spec["palette"]:
                p_off = quad["pltt"] << 4
                pal = palette[p_off:p_off + len(spec["palette"])]
            with self.subTest(quad=index):
                self.assertEqual(
                    oracle.decode(quad["param"], texel, pal, b""),
                    beh.expected_pixels(spec))

    def test_wrap_bits_are_kept(self):
        _slot0, _palette, quads = tb.layout()
        wrap = [(q["param"] >> 16) & 0xF for q in quads[4:]]
        self.assertEqual(wrap, [0x3, 0x0, 0xF, 0xC])
        self.assertTrue(quads[2]["param"] & (1 << 29))   # KeyColor0
        self.assertFalse(quads[3]["param"] & (1 << 29))

    def test_wrong_palette_offset_fails_the_decode(self):
        slot0, palette, quads = tb.layout()
        spec = beh.QUAD_SPECS[1]
        t_off = (quads[1]["param"] & 0xFFFF) << 3
        pal = palette[0x50:0x50 + len(spec["palette"])]
        self.assertNotEqual(
            oracle.decode(quads[1]["param"],
                          slot0[t_off:t_off + len(spec["texel"])], pal, b""),
            beh.expected_pixels(spec))

    def test_check_tex_behaviour_clear_option(self):
        self.assertEqual(behaviour_checker._parse_clear("0,0,0"), (0, 0, 0))
        for text in ("1,2", "a,b,c", "256,0,0"):
            with self.assertRaises(argparse.ArgumentTypeError):
                behaviour_checker._parse_clear(text)
        self.assertEqual(behaviour_checker.CLEAR, (16, 16, 24))


class GradCullDataTest(unittest.TestCase):
    def test_header_matches_generator(self):
        text = _read("gradcull_data.h")
        self.assertEqual(text, gc.header_text())
        self.assertNotIn("/home/", text)

    def test_cull_windings_are_opposite(self):
        def area(corners):
            total = 0
            for i in range(4):
                x0, y0 = corners[i]
                x1, y1 = corners[(i + 1) % 4]
                total += x0 * y1 - x1 * y0
            return total

        for index in range(4):
            first = area(gc.cull_corners(index))
            second = area(gc.cull_corners(index + 4))
            self.assertNotEqual(first, 0)
            self.assertLess(first * second, 0)         # opposite winding
            self.assertEqual(abs(first), abs(second))  # same quad size
            self.assertEqual(gc.CULL_CASES[index][1],
                             gc.CULL_CASES[index + 4][1])

    def test_cull_covers_every_attribute_value(self):
        self.assertEqual(sorted(b for _w, b in gc.CULL_CASES[:4]),
                         [0, 1, 2, 3])

    def test_triangles_inside_the_visible_clip_range(self):
        for tri in gc.GRAD_TRIANGLES:
            for x, y in tri["vertices"]:
                self.assertLessEqual(abs(x), 3994)
                self.assertLessEqual(abs(y), 3994)


def _grad_painter(shift=0):
    triangles = []
    for tri in gc.GRAD_TRIANGLES:
        triangles.append(([mgrad.to_capture(v) for v in tri["vertices"]],
                          tri["colours"]))

    def painter(x, y):
        for corners, colours in triangles:
            (x0, y0), (x1, y1), (x2, y2) = corners
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            l0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den
            l1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den
            l2 = 1.0 - l0 - l1
            if l0 >= 0 and l1 >= 0 and l2 >= 0:
                weights = (l0, l1, l2)
                return tuple(min(255, max(0, int(
                    mgrad.expected_colour(colours, weights)[c] + 0.5) + shift))
                    for c in range(3))
        return None
    return painter


class MeasureGradTest(unittest.TestCase):
    def test_sample_points_are_interior_and_sum_to_one(self):
        points = mgrad.sample_points()
        self.assertEqual(len(points), 10)
        for w in points:
            self.assertAlmostEqual(sum(w), 1.0)
            self.assertTrue(all(v > 0 for v in w))

    def test_exact_gradient_passes(self):
        proc = _run_tool("measure_grad.py", _paint(400, 240, _grad_painter()))
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("result=PASS", proc.stdout)

    def test_offset_gradient_fails(self):
        # Negative test: every channel 20 levels brighter is outside tol 12.
        proc = _run_tool("measure_grad.py",
                         _paint(400, 240, _grad_painter(shift=20)))
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn("result=FAIL", proc.stdout)

    def test_blank_capture_fails_and_garbage_exits_two(self):
        self.assertEqual(_run_tool("measure_grad.py", BLANK).returncode, 1)
        self.assertEqual(_run_tool("measure_grad.py", b"P5\n1 1\n255\nx")
                         .returncode, 2)


class MeasureCullTest(unittest.TestCase):
    def _capture(self, drawn):
        rects = [fmt.QUAD_SPECS[i]["rect"] for i in drawn]

        def painter(x, y):
            for x0, y0, x1, y1 in rects:
                if x0 <= x < x1 and y0 <= y < y1:
                    return (255, 255, 255)
            return None
        return _paint(400, 240, painter)

    def test_classification_follows_the_drawn_quads(self):
        proc = _run_tool("measure_cull.py", self._capture([0, 2, 5]))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        states = [line.split()[-1] for line in proc.stdout.splitlines()[1:]]
        self.assertEqual(states, ["DRAWN", "CULLED", "DRAWN", "CULLED",
                                  "CULLED", "DRAWN", "CULLED", "CULLED"])

    def test_blank_capture_is_all_culled(self):
        proc = _run_tool("measure_cull.py", BLANK)
        states = [line.split()[-1] for line in proc.stdout.splitlines()[1:]]
        self.assertEqual(states, ["CULLED"] * 8)

    def test_classify_boundaries(self):
        self.assertEqual(mcull.classify(0.95), "DRAWN")
        self.assertEqual(mcull.classify(0.0), "CULLED")
        self.assertEqual(mcull.classify(0.5), "MIXED")

    def test_garbage_exits_two(self):
        self.assertEqual(_run_tool("measure_cull.py", b"P5\n1 1\n255\nx")
                         .returncode, 2)


def dry_run(*variables):
    return subprocess.run(
        ["make", "-C", "arm9v2", "-n", "-B"] + list(variables),
        cwd=ROOT, capture_output=True, text=True, timeout=120)


@unittest.skipUnless(DEVKIT, "devkitPro environment not available (labelled "
                             "skip)")
class ProbeMakefileTest(unittest.TestCase):
    def test_each_switch_names_its_rom_and_define(self):
        for name in ("TEXBEH", "GRAD", "CULL"):
            with self.subTest(name=name):
                result = dry_run("%s=1" % name)
                out = result.stdout + result.stderr
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("twlhd-arm9v2-%s.nds" % name.lower(), out)
                self.assertIn("-DTWLHD_DEMO_%s=1" % name, out)

    def test_empty_is_like_unset(self):
        for name in ("TEXBEH", "GRAD", "CULL"):
            with self.subTest(name=name):
                result = dry_run("%s=" % name)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("-DTWLHD_DEMO_%s=" % name,
                                 result.stdout + result.stderr)

    def test_invalid_values_and_combinations_fail(self):
        cases = (
            (("TEXBEH=2",), "TEXBEH must be empty or 1"),
            (("GRAD=x",), "GRAD must be empty or 1"),
            (("CULL=0",), "CULL must be empty or 1"),
            (("TEXBEH=1", "LIT=1"), "TEXBEH cannot be combined with LIT"),
            (("TEXBEH=1", "TEXMOD=1"),
             "TEXBEH cannot be combined with TEXMOD"),
            (("GRAD=1", "TEXBEH=1"), "GRAD cannot be combined with TEXBEH"),
            (("CULL=1", "GRAD=1"), "CULL cannot be combined with GRAD"),
            (("CULL=1", "AUTODUMP_FRAME=240"),
             "CULL cannot be combined with AUTODUMP_FRAME"),
        )
        for variables, text in cases:
            with self.subTest(variables=variables):
                result = dry_run(*variables)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(text, result.stderr)


if __name__ == "__main__":
    unittest.main()
