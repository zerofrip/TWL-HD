#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4zc_alpha_sweep/gen_sweep_frames.py.

The two frames and layouts are regenerated, compared byte for byte against the
committed fixtures (determinism and fixture provenance), validated with the
Python and C v2 parsers, and checked geometrically: 11 quads in alpha order,
inside the DS 256x192 viewport with a grid gap, and exact fixed-point vertex
coordinates.
"""

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS_DIR)
FIXTURES = os.path.join(HERE, "fixtures")
if HERE not in sys.path:
    sys.path.insert(0, HERE)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import gen_sweep_frames as gen  # noqa: E402
import twlhd_v2_dump as v2  # noqa: E402

LAYOUT_NAMES = tuple(gen.layout_name(name) for name in gen.FRAME_NAMES)
ARTEFACTS = gen.FRAME_NAMES + LAYOUT_NAMES
MAX_FRAME_BYTES = 100 * 1024


def read_fixture(name):
    with open(os.path.join(FIXTURES, name), "rb") as handle:
        return handle.read()


class GenerationTest(unittest.TestCase):
    def test_writes_two_frames_and_two_layouts(self):
        with tempfile.TemporaryDirectory() as outdir:
            paths = gen.write_frames(outdir)
            self.assertEqual(sorted(os.path.basename(path) for path in paths),
                             sorted(ARTEFACTS))
            for name in gen.FRAME_NAMES:
                self.assertLess(os.path.getsize(os.path.join(outdir, name)),
                                MAX_FRAME_BYTES, name)

    def test_rerun_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as first, \
                tempfile.TemporaryDirectory() as second:
            gen.write_frames(first)
            gen.write_frames(second)
            for name in ARTEFACTS:
                with open(os.path.join(first, name), "rb") as handle:
                    left = handle.read()
                with open(os.path.join(second, name), "rb") as handle:
                    right = handle.read()
                self.assertEqual(left, right, name)

    def test_fixtures_match_generator(self):
        with tempfile.TemporaryDirectory() as outdir:
            gen.write_frames(outdir)
            for name in ARTEFACTS:
                with open(os.path.join(outdir, name), "rb") as handle:
                    self.assertEqual(handle.read(), read_fixture(name), name)


class FrameTest(unittest.TestCase):
    def test_python_validator_and_rsta(self):
        for name in gen.FRAME_NAMES:
            view = v2.parse(read_fixture(name))
            self.assertTrue(view.flags & v2.FLAG_FRAME_COMPLETE)
            self.assertEqual(len(view.rsta), 1)
            self.assertEqual(view.rsta[0], (0x04000060, 0x18, 0, 2))
            self.assertEqual(struct.unpack_from("<I", read_fixture(name), 8)[0],
                             len(read_fixture(name)))

    def test_gxcm_stream_is_a_scene4_quad_list(self):
        counts = {}
        for name in gen.FRAME_NAMES:
            view = v2.parse(read_fixture(name))
            counts[name] = {}
            for word, _value in view.gxcm:
                cmd = word & 0xFF
                counts[name][cmd] = counts[name].get(cmd, 0) + 1
            self.assertEqual(counts[name][0x29], len(gen.ALPHAS))
            self.assertEqual(counts[name][0x20], len(gen.ALPHAS))
            self.assertEqual(counts[name][0x40], len(gen.ALPHAS))
            self.assertEqual(counts[name][0x23], 2 * 4 * len(gen.ALPHAS))
            self.assertEqual(counts[name][0x41], len(gen.ALPHAS))
            self.assertEqual(counts[name][0x50], 1)

            alphas = [value >> 16 for word, value in view.gxcm
                      if (word & 0xFF) == 0x29]
            self.assertEqual(tuple(alphas), gen.ALPHAS)
            modes = [value & 3 for word, value in view.gxcm
                     if (word & 0xFF) == 0x40]
            self.assertEqual(modes, [1] * len(gen.ALPHAS))

    def test_source_colours(self):
        expected = {"eq4zc_sweep_green.bin": gen._rgb555(0, 31, 0),
                    "eq4zc_sweep_blue.bin": gen._rgb555(0, 0, 31)}
        for name in gen.FRAME_NAMES:
            view = v2.parse(read_fixture(name))
            colours = [value for word, value in view.gxcm
                       if (word & 0xFF) == 0x20]
            self.assertEqual(colours, [expected[name]] * len(gen.ALPHAS))

    def test_vertex_fixed_point_is_exact(self):
        for rect in gen.RECTS:
            x0, y0, x1, y1 = rect
            for x in (x0, x1):
                self.assertEqual(gen.clip_x(x), 32 * x - 4096)
            for y in (y0, y1):
                self.assertEqual(y % 3, 0)
                clip = gen.clip_y(y)
                # Exact integer clip value: no 1/96 DS px rounding.
                self.assertEqual(clip * 96, 4096 * (96 - y))

    def test_c_validator(self):
        cc = os.environ.get("CC", "cc")
        if shutil.which(cc) is None:
            self.skipTest("host C compiler %r not found" % cc)
        with tempfile.TemporaryDirectory() as tmpdir:
            binary = os.path.join(tmpdir, "v2_dump_cli")
            subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", os.path.join(ROOT, "common"),
                 os.path.join(ROOT, "tests", "host", "v2_dump_cli.c"),
                 os.path.join(ROOT, "common", "twlhd_v2_parse.c"),
                 "-o", binary], check=True, capture_output=True, text=True)
            for name in gen.FRAME_NAMES:
                completed = subprocess.run(
                    [binary, os.path.join(FIXTURES, name)],
                    capture_output=True, text=True)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("result=OK", completed.stdout)
                self.assertIn("rsta=1", completed.stdout)


class LayoutTest(unittest.TestCase):
    def load(self, name):
        return json.loads(read_fixture(gen.layout_name(name)))

    def test_layout_is_alpha_order_in_viewport(self):
        for name in gen.FRAME_NAMES:
            layout = self.load(name)
            self.assertEqual(layout["alphas"], list(gen.ALPHAS))
            self.assertEqual([quad["alpha"] for quad in layout["quads"]],
                             list(gen.ALPHAS))
            self.assertEqual(layout["source_rgb"],
                             list(gen.SOURCE_RGB[name]))
            self.assertEqual(layout["viewport"], list(gen.VIEWPORT))
            for quad in layout["quads"]:
                x0, y0, x1, y1 = quad["rect"]
                self.assertGreaterEqual(x0, 0)
                self.assertGreaterEqual(y0, 0)
                self.assertLessEqual(x1, 256)
                self.assertLessEqual(y1, 192)
                self.assertGreaterEqual(x1 - x0, 16)
                self.assertGreaterEqual(y1 - y0, 16)

    def test_quads_are_separated(self):
        for name in gen.FRAME_NAMES:
            rects = [tuple(quad["rect"])
                     for quad in self.load(name)["quads"]]
            for i in range(len(rects)):
                for j in range(i + 1, len(rects)):
                    a = rects[i]
                    b = rects[j]
                    separated = (a[2] + 8 <= b[0] or b[2] + 8 <= a[0] or
                                 a[3] + 8 <= b[1] or b[3] + 8 <= a[1])
                    self.assertTrue(separated, (name, a, b))

    def test_layout_matches_generator(self):
        for name in gen.FRAME_NAMES:
            self.assertEqual(self.load(name), gen.layout(name))


if __name__ == "__main__":
    unittest.main()
