# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/hw/compare_ppm.py.

Builds synthetic top-screen PPMs from the predictor JSON and checks the pass
and fail paths (off-by-tolerance, swapped channel, background, truncated PPM,
patch outside the image, malformed PPM).  Offline.
"""

import json
import os
import shutil
import subprocess
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
COMPARE = os.path.join(TOOLS_DIR, "hw", "compare_ppm.py")

WIDTH = 400
HEIGHT = 240
RADIUS = 4


def _paint(pixels, centre, colour):
    cx = int(round(centre[0]))
    cy = int(round(centre[1]))
    for y in range(cy - RADIUS, cy + RADIUS + 1):
        for x in range(cx - RADIUS, cx + RADIUS + 1):
            base = (y * WIDTH + x) * 3
            pixels[base:base + 3] = bytes(colour)


def _clamp(value):
    return max(0, min(255, int(value)))


def build_ppm(path, document, quad_offsets=None, background_offset=(0, 0, 0)):
    pixels = bytearray()
    clear = document["clear_colour"][:3]
    for _ in range(WIDTH * HEIGHT):
        pixels.extend(clear)

    quad_offsets = quad_offsets or {}
    for quad in document["quads"]:
        offset = quad_offsets.get(quad["index"], (0, 0, 0))
        colour = [_clamp(quad["rgba8"][c] + offset[c]) for c in range(3)]
        _paint(pixels, quad["pixel_centre"], colour)

    if any(background_offset):
        colour = [_clamp(clear[c] + background_offset[c]) for c in range(3)]
        _paint(pixels, document["background"]["pixel_centre"], colour)

    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT))
        handle.write(pixels)
    return path


class LitCompareTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (os.path.isfile(CLI) and os.access(CLI, os.X_OK)):
            raise AssertionError(
                "gx_interp_cli binary is missing: %s (run `make test`)" % CLI)
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-lit-compare-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)
        frames = gen.write_frames(cls.tmpdir)

        cls.docs = {}
        cls.json_paths = {}
        for name, frame in zip(gen.FRAME_NAMES, frames):
            document = predict.predict(frame, CLI)
            path = os.path.join(cls.tmpdir, name + ".json")
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(document, handle)
            cls.docs[name] = document
            cls.json_paths[name] = path

    def run_compare(self, ppm, json_path, *extra):
        return subprocess.run(
            [sys.executable, "-B", COMPARE, ppm, json_path] + list(extra),
            capture_output=True, text=True)

    def test_exact_prediction_passes(self):
        for name, json_path in self.json_paths.items():
            with self.subTest(name=name):
                ppm = build_ppm(
                    os.path.join(self.tmpdir, name + ".exact.ppm"),
                    self.docs[name])
                completed = self.run_compare(ppm, json_path)
                self.assertEqual(completed.returncode, 0, completed.stdout +
                                 completed.stderr)
                self.assertIn("result=PASS", completed.stdout)

    def test_noise_within_tolerance_passes(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        offsets = {q["index"]: (3, -3, 2) for q in doc["quads"]}
        ppm = build_ppm(os.path.join(self.tmpdir, "noise.ppm"), doc,
                        offsets, background_offset=(4, -4, 1))
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 0, completed.stdout +
                         completed.stderr)
        self.assertIn("result=PASS", completed.stdout)

    def test_one_quad_off_by_tolerance_plus_one_fails(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        offsets = {0: (13, 0, 0)}
        ppm = build_ppm(os.path.join(self.tmpdir, "off.ppm"), doc, offsets)
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("result=FAIL", completed.stdout)

    def test_tolerance_option_rescues_deviation(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        offsets = {0: (13, 0, 0)}
        ppm = build_ppm(os.path.join(self.tmpdir, "rescue.ppm"), doc, offsets)
        completed = self.run_compare(
            ppm, self.json_paths[gen.FRAME_NAMES[0]], "--tol", "20")
        self.assertEqual(completed.returncode, 0, completed.stdout)

    def test_negative_deviation_beyond_tolerance_fails(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        offsets = {0: (-13, 0, 0)}
        ppm = build_ppm(os.path.join(self.tmpdir, "negative.ppm"), doc,
                        offsets)
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("result=FAIL", completed.stdout)

    def test_blue_only_deviation_fails(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        offsets = {1: (0, 0, 13)}
        ppm = build_ppm(os.path.join(self.tmpdir, "blue.ppm"), doc, offsets)
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("result=FAIL", completed.stdout)

    def test_swapped_channel_fails(self):
        name = gen.FRAME_NAMES[1]
        doc = self.docs[name]
        clear = doc["clear_colour"][:3]
        pixels = bytearray()
        for _ in range(WIDTH * HEIGHT):
            pixels.extend(clear)
        for quad in doc["quads"]:
            colour = quad["rgba8"][:3]
            if quad["index"] == 0:
                colour = [colour[1], colour[0], colour[2]]  # swap R and G
            _paint(pixels, quad["pixel_centre"], colour)
        ppm = os.path.join(self.tmpdir, "swapped.ppm")
        with open(ppm, "wb") as handle:
            handle.write(b"P6\n400 240\n255\n")
            handle.write(pixels)
        completed = self.run_compare(ppm, self.json_paths[name])
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("result=FAIL", completed.stdout)

    def test_background_out_of_tolerance_fails(self):
        doc = self.docs[gen.FRAME_NAMES[0]]
        ppm = build_ppm(os.path.join(self.tmpdir, "bg.ppm"), doc,
                        background_offset=(30, 30, 30))
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 1, completed.stdout)
        self.assertIn("result=FAIL", completed.stdout)

    def test_truncated_ppm_is_input_error(self):
        ppm = os.path.join(self.tmpdir, "short.ppm")
        with open(ppm, "wb") as handle:
            handle.write(b"P6\n400 240\n255\n" + b"\0" * 100)
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 2, completed.stdout)
        self.assertIn("truncated", completed.stderr)

    def test_patch_outside_image_is_input_error(self):
        name = gen.FRAME_NAMES[0]
        document = json.loads(json.dumps(self.docs[name]))
        document["quads"][0]["pixel_centre"] = [2.0, 2.0]
        json_path = os.path.join(self.tmpdir, "outside.json")
        with open(json_path, "w", encoding="utf-8") as handle:
            json.dump(document, handle)
        ppm = build_ppm(os.path.join(self.tmpdir, "outside.ppm"),
                        self.docs[name])
        completed = self.run_compare(ppm, json_path)
        self.assertEqual(completed.returncode, 2, completed.stdout)
        self.assertIn("outside", completed.stderr)

    def test_malformed_ppm_is_input_error(self):
        ppm = os.path.join(self.tmpdir, "bad.ppm")
        with open(ppm, "wb") as handle:
            handle.write(b"P3\n400 240\n255\n")
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 2)
        self.assertIn("P6", completed.stderr)

    def test_short_header_is_input_error(self):
        ppm = os.path.join(self.tmpdir, "header.ppm")
        with open(ppm, "wb") as handle:
            handle.write(b"P6\n400")
        completed = self.run_compare(ppm, self.json_paths[gen.FRAME_NAMES[0]])
        self.assertEqual(completed.returncode, 2)


if __name__ == "__main__":
    unittest.main()
