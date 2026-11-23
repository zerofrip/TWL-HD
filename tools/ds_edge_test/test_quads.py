#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""quads.json <-> include/quads.h <-> source/main.c consistency tests."""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "quads.json")
HEADER_PATH = os.path.join(HERE, "include", "quads.h")
MAIN_C = os.path.join(HERE, "source", "main.c")
GEN = os.path.join(HERE, "gen_quads.py")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import gen_quads  # noqa: E402

with open(JSON_PATH, "r", encoding="utf-8") as f:
    DATA = json.load(f)

WIDTH = 256
HEIGHT = 192

# Nominal half-open vertex rectangles [x, x+w) x [y, y+h) in DS pixels.
EXPECTED = {
    "top_band": (0, 0, 256, 16),
    "bottom_band": (0, 176, 256, 16),
    "left_band": (0, 80, 16, 32),
    "right_band": (240, 80, 16, 32),
    "sq1": (32, 32, 32, 32),
    "sq2": (160, 32, 32, 32),
    "sq3": (32, 128, 32, 32),
    "sq4": (160, 128, 32, 32),
    "hline": (64, 96, 128, 1),
    "vline": (208, 64, 1, 96),
}


def _inside(a, b):
    """True when rectangles a and b overlap (touching edges do not count)."""
    return (a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"]
            and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"])


class QuadsTest(unittest.TestCase):
    def test_screen_and_clear(self):
        self.assertEqual((DATA["width"], DATA["height"]), (WIDTH, HEIGHT))
        self.assertEqual(DATA["id"], "ds-edge-test-1")
        self.assertEqual(DATA["clear5"], [0, 0, 0])
        self.assertEqual(DATA["clear_alpha"], 31)

    def test_single_opaque_full_screen_backdrop(self):
        backdrops = DATA["backdrops"]
        self.assertEqual([b["name"] for b in backdrops], ["black"])
        backdrop = backdrops[0]
        self.assertEqual(backdrop["colour5"], [0, 0, 0])
        self.assertEqual((backdrop["x"], backdrop["y"], backdrop["w"],
                          backdrop["h"]), (0, 0, WIDTH, HEIGHT))
        self.assertEqual(backdrop["alpha"], 31)
        self.assertEqual(backdrop["poly_id"], 1)
        self.assertEqual(backdrop["z"], 0)

    def test_geometry_items_exact_and_in_bounds(self):
        quads = {q["name"]: q for q in DATA["quads"]}
        self.assertEqual(sorted(quads), sorted(EXPECTED))
        for name, (x, y, w, h) in EXPECTED.items():
            quad = quads[name]
            self.assertEqual((quad["x"], quad["y"], quad["w"], quad["h"]),
                             (x, y, w, h), name)
            self.assertGreaterEqual(quad["x"], 0, name)
            self.assertGreaterEqual(quad["y"], 0, name)
            self.assertLessEqual(quad["x"] + quad["w"], WIDTH, name)
            self.assertLessEqual(quad["y"] + quad["h"], HEIGHT, name)
            self.assertEqual(quad["colour5"], [31, 31, 31], name)
            self.assertEqual(quad["alpha"], 31, name)
            self.assertEqual(quad["z"], 64, name)
            self.assertEqual(quad["backdrop"], "black", name)

    def test_bands_touch_the_four_screen_edges(self):
        quads = {q["name"]: q for q in DATA["quads"]}
        top = quads["top_band"]
        bottom = quads["bottom_band"]
        left = quads["left_band"]
        right = quads["right_band"]
        self.assertEqual(top["y"], 0, "top band must start on scanline 0")
        self.assertEqual(bottom["y"] + bottom["h"], HEIGHT,
                         "bottom band must end on scanline 191")
        self.assertEqual(left["x"], 0, "left band must start on column 0")
        self.assertEqual(right["x"] + right["w"], WIDTH,
                         "right band must end on column 255")
        for band in (top, bottom):
            self.assertEqual(band["w"], WIDTH, band["name"])
        for band in (left, right):
            self.assertEqual(band["h"], 32, band["name"])

    def test_four_isolated_interior_squares(self):
        squares = [q for q in DATA["quads"] if q["name"].startswith("sq")]
        self.assertEqual(len(squares), 4)
        for square in squares:
            self.assertEqual((square["w"], square["h"]), (32, 32),
                             square["name"])
            self.assertGreater(square["x"], 0, square["name"])
            self.assertGreater(square["y"], 0, square["name"])
            self.assertLess(square["x"] + square["w"], WIDTH, square["name"])
            self.assertLess(square["y"] + square["h"], HEIGHT,
                             square["name"])

    def test_one_pixel_lines(self):
        quads = {q["name"]: q for q in DATA["quads"]}
        hline = quads["hline"]
        vline = quads["vline"]
        self.assertEqual(hline["h"], 1)
        self.assertGreater(hline["w"], 1)
        self.assertGreater(hline["y"], 0)
        self.assertLess(hline["y"], HEIGHT - 1)
        self.assertEqual(vline["w"], 1)
        self.assertGreater(vline["h"], 1)
        self.assertGreater(vline["x"], 0)
        self.assertLess(vline["x"], WIDTH - 1)

    def test_quads_in_front_of_backdrop_and_depth_sign(self):
        backdrop_z = DATA["backdrops"][0]["z"]
        for quad in DATA["quads"]:
            self.assertGreater(quad["z"], backdrop_z, quad["name"])
        # Negative control: a swapped z sign must fail the relation.
        flipped = [dict(q, z=-q["z"]) for q in DATA["quads"]]
        self.assertFalse(all(q["z"] > backdrop_z for q in flipped))

    def test_quads_do_not_overlap(self):
        quads = DATA["quads"]
        for i, a in enumerate(quads):
            for b in quads[i + 1:]:
                self.assertFalse(_inside(a, b),
                                 "%s/%s" % (a["name"], b["name"]))

    def test_polygon_ids_are_distinct(self):
        ids = [b["poly_id"] for b in DATA["backdrops"]]
        ids += [q["poly_id"] for q in DATA["quads"]]
        self.assertEqual(len(ids), len(set(ids)), ids)
        self.assertEqual(ids, list(range(1, 12)))

    def test_generated_header_matches_committed(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "quads.h")
            completed = subprocess.run(
                [sys.executable, GEN, JSON_PATH, out],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            with open(out, "r", encoding="utf-8") as f:
                generated = f.read()
        with open(HEADER_PATH, "r", encoding="utf-8") as f:
            committed = f.read()
        self.assertEqual(generated, committed)

    def test_rom_uses_3d_path_and_no_twlhd(self):
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        for token in ("MODE_0_3D", "glInit", "glBegin", "glColor",
                      "glVertex3v16", "glOrthof32", "consoleDemoInit",
                      "swiWaitForVBlank", "glDisable(GL_BLEND"):
            self.assertIn(token, source, token)
        self.assertNotIn("glEnable(GL_BLEND", source)
        for forbidden in ("fifo_hook", "twlhd_", "arm9v2",
                          "twlhd_gx_interp"):
            self.assertNotIn(forbidden, source, forbidden)

    def test_rom_sets_opaque_polygon_id_without_fog(self):
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        body = source[source.index("static void draw_poly"):]
        self.assertIn("glPolyFmt", body)
        self.assertIn("POLY_ALPHA(p->alpha)", body)
        self.assertIn("POLY_ID(p->poly_id)", body)
        self.assertNotIn("POLY_FOG", body)


if __name__ == "__main__":
    unittest.main()
