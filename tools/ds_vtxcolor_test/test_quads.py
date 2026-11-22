#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""quads.json <-> include/quads.h <-> source/main.c consistency tests."""

import json
import os
import re
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
    QUADS = json.load(f)

EXPECTED = [
    ("qA", 16, 32, 64, 64, None),
    ("qB", 96, 32, 64, 64, [255, 255, 255]),
    ("qC", 176, 32, 64, 64, [0, 0, 0]),
    ("qD", 16, 112, 64, 64, [128, 128, 128]),
    ("qE", 96, 112, 64, 64, [255, 0, 0]),
]


def minimal_error_v5(v8):
    """Brute-force closest v5; round(v8*31/255) breaks the tie at 144."""
    errors = [abs(((v5 << 3) | (v5 >> 2)) - v8) for v5 in range(32)]
    tied = [v5 for v5, err in enumerate(errors) if err == min(errors)]
    chosen = round(v8 * 31 / 255)
    assert chosen in tied, (v8, chosen, tied)
    return chosen


class QuadsTest(unittest.TestCase):
    def test_screen_and_background(self):
        self.assertEqual((QUADS["width"], QUADS["height"]), (256, 192))
        self.assertEqual(QUADS["background5"], [8, 8, 8])
        self.assertEqual(QUADS["id"], "ds-vtxcolor-test-1")

    def test_quads_in_bounds_and_order(self):
        got = []
        for quad in QUADS["quads"]:
            self.assertGreaterEqual(quad["x"], 0, quad["name"])
            self.assertGreaterEqual(quad["y"], 0, quad["name"])
            self.assertGreater(quad["w"], 0, quad["name"])
            self.assertGreater(quad["h"], 0, quad["name"])
            self.assertLessEqual(quad["x"] + quad["w"], QUADS["width"],
                                 quad["name"])
            self.assertLessEqual(quad["y"] + quad["h"], QUADS["height"],
                                 quad["name"])
            got.append((quad["name"], quad["x"], quad["y"], quad["w"],
                        quad["h"], quad.get("colour")))
        self.assertEqual(got, EXPECTED)

    def test_qA_is_the_default_colour_probe(self):
        self.assertIsNone(QUADS["quads"][0]["colour"])

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

    def test_header_colours_are_minimal_error(self):
        header = gen_quads.header_text(QUADS)
        rows = re.findall(r"^    \{ \"(q[A-E])\",([^}]*)\},$", header, re.M)
        self.assertEqual([name for name, _ in rows],
                         [q["name"] for q in QUADS["quads"]])
        for (name, body), quad in zip(rows, QUADS["quads"]):
            fields = [int(v) for v in body.split(",")]
            self.assertEqual(fields[:4], [quad["x"], quad["y"], quad["w"],
                                          quad["h"]], name)
            has_colour = fields[4]
            if quad["colour"] is None:
                self.assertEqual(has_colour, 0, name)
                continue
            self.assertEqual(has_colour, 1, name)
            expected = [minimal_error_v5(c) for c in quad["colour"]]
            self.assertEqual(fields[5:8], expected, name)

    def test_rom_uses_3d_path_and_no_twlhd(self):
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        for token in ("MODE_0_3D", "glInit", "glBegin", "glColor",
                      "glVertex3v16", "glOrthof32", "consoleDemoInit",
                      "DSVTX_ID", "swiWaitForVBlank"):
            self.assertIn(token, source, token)
        for forbidden in ("fifo_hook", "twlhd_", "arm9v2",
                          "twlhd_gx_interp"):
            self.assertNotIn(forbidden, source, forbidden)

    def test_rom_draws_qA_without_a_colour_write(self):
        """Colour writes are guarded, so qA's first vertex sees the default."""
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        self.assertIn("if (q->has_colour)", source)
        body = source[source.index("static void draw_quads"):]
        self.assertLess(body.index("glBegin"), body.index("glColor"))
        self.assertLess(body.index("if (q->has_colour)"),
                        body.index("vertex_px("))


if __name__ == "__main__":
    unittest.main()
