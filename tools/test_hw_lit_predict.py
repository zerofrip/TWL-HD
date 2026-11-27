# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/hw/predict_lit.py (offline, host CLI only).

The expected lighting values for lit_a are computed here with the integer
DeSmuME equations from docs/DS_GX_RESEARCH.md:222-258, not by calling the
tool.  The host interpreter CLI (tests/host/build/gx_interp_cli) is built by
`make test`.
"""

import os
import shutil
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import gen_lit_frames as gen  # noqa: E402
import predict_lit as predict  # noqa: E402

CLI = os.path.join(ROOT, "tests", "host", "build", "gx_interp_cli")
FIXTURE = os.path.join(ROOT, "tests", "host", "fixtures",
                       "demo_frame_desmume_v2.bin")


def _dot20_12(a, b):
    """C vec3dot_fixed32: 64-bit sum of products, arithmetic >> 12."""
    return ((a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2])) >> 12


def _diffuse_ambient_rgb8(diffuse, ambient, light_colour, light_dir,
                          normal):
    """Integer equation with emission and specular zero.

    Diffuse and ambient are summed before the single truncation (real DS
    hardware, EQ-4zi lit_c); the earlier DeSmuME-derived form truncated them
    separately.
    """
    fixed_diffuse = max(0, -_dot20_12(light_dir, normal))
    out = []
    for channel in range(3):
        value = min(((diffuse[channel] * light_colour[channel] * fixed_diffuse)
                     + ((ambient[channel] * light_colour[channel]) << 12))
                    >> 17, 31)
        out.append(int(value * 255 / 31 + 0.5))
    return out


class LitPredictTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (os.path.isfile(CLI) and os.access(CLI, os.X_OK)):
            raise AssertionError(
                "gx_interp_cli binary is missing: %s (run `make test`)" % CLI)
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-lit-predict-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)
        gen.write_frames(cls.tmpdir)
        cls.frames = {
            name: os.path.join(cls.tmpdir, name) for name in gen.FRAME_NAMES
        }
        cls.docs = {
            name: predict.predict(path, CLI)
            for name, path in cls.frames.items()
        }

    def doc(self, index):
        return self.docs[gen.FRAME_NAMES[index]]

    def test_document_structure(self):
        for name, document in self.docs.items():
            with self.subTest(name=name):
                self.assertEqual(document["result"], "OK")
                self.assertEqual(document["viewport"]["x1"], 0)
                self.assertEqual(document["viewport"]["x2"], 255)
                self.assertEqual(document["screen"], {"width": 400,
                                                      "height": 240})
                self.assertEqual(document["clear_colour"],
                                 [0, 0, 0, 255])
                self.assertEqual(document["background"]["rgba8"],
                                 [0, 0, 0, 255])
                self.assertEqual(document["background"]["pixel_centre"],
                                 [12.0, 12.0])
                self.assertIn("fb_y", document["mapping"])
                self.assertEqual(len(document["quads"]), 4)
                for index, quad in enumerate(document["quads"]):
                    self.assertEqual(quad["index"], index)
                    self.assertEqual(len(quad["rgba8"]), 4)
                    for channel in quad["rgba8"]:
                        self.assertIsInstance(channel, int)
                        self.assertGreaterEqual(channel, 0)
                        self.assertLessEqual(channel, 255)
                    self.assertEqual(len(quad["clip_centre"]), 4)
                    self.assertEqual(len(quad["pixel_centre"]), 2)
                    self.assertGreaterEqual(quad["pixel_centre"][0], 0)
                    self.assertLessEqual(quad["pixel_centre"][0], 399)
                    self.assertGreaterEqual(quad["pixel_centre"][1], 0)
                    self.assertLessEqual(quad["pixel_centre"][1], 239)

    def test_lit_a_matches_hand_computed_integers(self):
        # LIGHT_VECTOR 0 is (0, 0, -4088) in 20.12; all values below are the
        # 10-bit-field multiples of 8 the generator encodes.
        light_dir = (0, 0, -4088)
        light_colour = (31, 31, 31)
        diffuse = (22, 22, 22)
        ambient = (4, 4, 4)
        normals = [
            (0, 0, 4088),      # facing the light
            (3544, 0, 2048),   # 60 degrees off
            (4088, 0, 0),      # grazing
            (0, 0, -4088),     # facing away
        ]
        expected = [_diffuse_ambient_rgb8(diffuse, ambient, light_colour,
                                          light_dir, normal)
                    for normal in normals]

        quads = self.doc(0)["quads"]
        for index, wanted in enumerate(expected):
            with self.subTest(quad=index):
                self.assertEqual(quads[index]["rgba8"][:3], wanted)

        # Two literals written out so a shared bug in the helper cannot hide.
        self.assertEqual(quads[0]["rgba8"], [206, 206, 206, 255])
        self.assertEqual(quads[1]["rgba8"], [115, 115, 115, 255])
        self.assertEqual(quads[2]["rgba8"], [25, 25, 25, 255])

    def test_lit_c_matches_hardware_levels(self):
        # Real-hardware 5-bit levels (EQ-4zi, three captures): the sum of
        # diffuse and ambient is truncated once.  Separate truncation would
        # give 0, 0, 0, 13 (levels), ceil of the diffuse term 1, 1, 1, 14.
        doc = self.docs[gen.FRAME_NAMES[3]]
        got = [quad["rgba8"][0] for quad in doc["quads"]]
        self.assertEqual(got, [8, 0, 0, 115])

    def test_lit_a_incidence_ordering(self):
        quads = self.doc(0)["quads"]
        brightness = [sum(q["rgba8"][:3]) for q in quads]
        self.assertGreater(brightness[0], brightness[1])
        self.assertGreater(brightness[1], brightness[2])
        # 90 degrees and facing away both see no diffuse term: ambient only.
        self.assertEqual(quads[2]["rgba8"], quads[3]["rgba8"])
        self.assertEqual(quads[3]["rgba8"], [25, 25, 25, 255])

    def test_lit_b_expected_colours_and_spread(self):
        quads = self.doc(1)["quads"]
        self.assertEqual([q["rgba8"] for q in quads], [
            [206, 0, 0, 255],      # diffuse + ambient under the red light
            [33, 230, 33, 255],    # emission only
            [239, 239, 239, 255],  # specular through the shininess table
            [173, 0, 148, 255],    # red light 0 + blue light 1
        ])

        for i in range(4):
            for j in range(i + 1, 4):
                spread = max(abs(quads[i]["rgba8"][c] -
                                 quads[j]["rgba8"][c]) for c in range(3))
                self.assertGreaterEqual(
                    spread, 40, "quads %d and %d are too close" % (i, j))
        for quad in quads:
            self.assertNotEqual(quad["rgba8"][:3], [0, 0, 0])
            self.assertNotEqual(quad["rgba8"][:3], [255, 255, 255])

    def test_lit_ctl_flat_colours(self):
        quads = self.doc(2)["quads"]
        self.assertEqual([q["rgba8"] for q in quads], [
            [255, 0, 0, 255],
            [0, 255, 0, 255],
            [0, 0, 255, 255],
            [255, 255, 0, 255],
        ])

    def test_pixel_mapping_literals(self):
        # Derived mapping for the default full DS viewport:
        #   sx = 200 + 160*x_ndc, sy = 119 - 120*y_ndc
        quads = self.doc(0)["quads"]
        expected = [
            (112.5, 161.1875),
            (287.5, 161.1875),
            (112.5, 76.8125),
            (287.5, 76.8125),
        ]
        for quad, (sx, sy) in zip(quads, expected):
            self.assertAlmostEqual(quad["pixel_centre"][0], sx, places=4)
            self.assertAlmostEqual(quad["pixel_centre"][1], sy, places=4)

    def test_fixture_mapping_matches_run7_orientation(self):
        if not os.path.isfile(FIXTURE):
            self.skipTest("demo fixture missing")
        document = predict.predict(FIXTURE, CLI)
        quads = document["quads"]
        self.assertEqual(len(quads), 6)

        average_x = sum(q["pixel_centre"][0] for q in quads) / len(quads)
        average_y = sum(q["pixel_centre"][1] for q in quads) / len(quads)
        self.assertAlmostEqual(average_x, 200.0, delta=2.0)
        self.assertLess(average_y, 120.0)

        # Run 7: red upper-left, yellow upper-right, cyan lower (not mirrored).
        red, green, blue, yellow, _magenta, cyan = quads
        self.assertEqual(red["rgba8"][:3], [255, 0, 0])
        self.assertEqual(yellow["rgba8"][:3], [255, 255, 0])
        self.assertEqual(cyan["rgba8"][:3], [0, 255, 255])
        self.assertLess(red["pixel_centre"][0], 200.0)
        self.assertGreater(yellow["pixel_centre"][0], 200.0)
        self.assertLess(red["pixel_centre"][1], 120.0)
        self.assertLess(yellow["pixel_centre"][1], 120.0)
        self.assertGreater(cyan["pixel_centre"][1], 120.0)

        # The DS picture stays inside the centred 320-pixel region.
        for quad in quads:
            self.assertGreaterEqual(quad["pixel_centre"][0], 35.0)
            self.assertLessEqual(quad["pixel_centre"][0], 365.0)


if __name__ == "__main__":
    unittest.main()
