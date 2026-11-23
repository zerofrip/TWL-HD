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

ALPHAS = [4, 8, 12, 16, 20, 24, 28]


def _inside(a, b):
    """True when rectangles a and b overlap (touching edges do not count)."""
    return (a["x"] < b["x"] + b["w"] and b["x"] < a["x"] + a["w"]
            and a["y"] < b["y"] + b["h"] and b["y"] < a["y"] + a["h"])


def _quads_in_front(quads, backdrops):
    """True when every quad is nearer than its own backdrop.

    The DS projection maps a larger source z to a smaller post-transform z,
    which is nearer under the default LESS depth test (report section 5), so a
    quad is in front when its source z is greater than its backdrop's z.
    """
    by_name = {b["name"]: b["z"] for b in backdrops}
    return all(q["z"] > by_name[q["backdrop"]] for q in quads)


class QuadsTest(unittest.TestCase):
    def test_screen_and_clear(self):
        self.assertEqual((DATA["width"], DATA["height"]), (256, 192))
        self.assertEqual(DATA["id"], "ds-alpha-test-1")
        self.assertEqual(DATA["clear5"], [0, 0, 0])
        self.assertEqual(DATA["clear_alpha"], 31)

    def test_two_opaque_backdrops(self):
        backdrops = DATA["backdrops"]
        self.assertEqual([b["name"] for b in backdrops], ["black", "grey"])
        self.assertEqual(backdrops[0]["colour5"], [0, 0, 0])
        self.assertEqual(backdrops[1]["colour5"], [16, 16, 16])
        for bd in backdrops:
            self.assertEqual(bd["alpha"], 31, bd["name"])
            self.assertEqual((bd["x"], bd["y"], bd["w"], bd["h"]),
                             (0, 0, 128, 192) if bd["name"] == "black"
                             else (128, 0, 128, 192), bd["name"])
        self.assertEqual([b["poly_id"] for b in backdrops], [1, 2])
        self.assertTrue(all(b["z"] == 0 for b in backdrops))

    def test_quads_geometry(self):
        quads = DATA["quads"]
        self.assertEqual(len(quads), 14)
        for half, prefix in (("black", "L"), ("grey", "R")):
            members = [q for q in quads if q["backdrop"] == half]
            self.assertEqual([q["alpha"] for q in members], ALPHAS, half)
            self.assertEqual([q["name"] for q in members],
                             ["%s%d" % (prefix, a) for a in ALPHAS], half)
            for q in members:
                self.assertGreaterEqual(q["w"], 16, q["name"])
                self.assertGreaterEqual(q["h"], 16, q["name"])
                self.assertGreaterEqual(q["x"], 0, q["name"])
                self.assertGreaterEqual(q["y"], 0, q["name"])
                self.assertLessEqual(q["x"] + q["w"], 128 if prefix == "L"
                                     else 256, q["name"])
                self.assertLessEqual(q["y"] + q["h"], DATA["height"],
                                     q["name"])
                self.assertEqual(q["colour5"], [31, 31, 31], q["name"])
                self.assertEqual(q["z"], 64, q["name"])

    def test_quads_in_front_of_backdrops(self):
        self.assertTrue(_quads_in_front(DATA["quads"], DATA["backdrops"]))
        for q in DATA["quads"]:
            backdrop = {b["name"]: b for b in DATA["backdrops"]}[q["backdrop"]]
            self.assertGreater(q["z"], backdrop["z"], q["name"])

    def test_depth_relation_negative_control(self):
        # A swapped sign (quad z negated) must fail the same relation check.
        flipped = [dict(q, z=-q["z"]) for q in DATA["quads"]]
        self.assertFalse(_quads_in_front(flipped, DATA["backdrops"]))

    def test_quads_do_not_overlap_and_have_gaps(self):
        quads = DATA["quads"]
        for i, a in enumerate(quads):
            for b in quads[i + 1:]:
                self.assertFalse(_inside(a, b), "%s/%s" % (a["name"],
                                                           b["name"]))
        for half in ("black", "grey"):
            members = sorted((q for q in quads if q["backdrop"] == half),
                             key=lambda q: q["x"])
            for prev, nxt in zip(members, members[1:]):
                self.assertGreaterEqual(nxt["x"] - (prev["x"] + prev["w"]), 1,
                                        "%s->%s" % (prev["name"], nxt["name"]))

    def test_polygon_ids_are_distinct(self):
        ids = [b["poly_id"] for b in DATA["backdrops"]]
        ids += [q["poly_id"] for q in DATA["quads"]]
        self.assertEqual(len(ids), len(set(ids)), ids)
        self.assertEqual(ids, list(range(1, 17)))

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
                      "swiWaitForVBlank", "GL_BLEND"):
            self.assertIn(token, source, token)
        for forbidden in ("fifo_hook", "twlhd_", "arm9v2",
                          "twlhd_gx_interp"):
            self.assertNotIn(forbidden, source, forbidden)

    def test_rom_sets_per_polygon_alpha_and_id(self):
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        body = source[source.index("static void draw_poly"):]
        self.assertIn("glPolyFmt", body)
        self.assertIn("POLY_ALPHA(p->alpha)", body)
        self.assertIn("POLY_ID(p->poly_id)", body)
        self.assertNotIn("POLY_FOG", body)


if __name__ == "__main__":
    unittest.main()
