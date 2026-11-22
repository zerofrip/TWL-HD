#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""pattern.json <-> include/pattern.h <-> source/main.c consistency tests."""

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
JSON_PATH = os.path.join(HERE, "pattern.json")
HEADER_PATH = os.path.join(HERE, "include", "pattern.h")
MAIN_C = os.path.join(HERE, "source", "main.c")
GEN = os.path.join(HERE, "gen_pattern.py")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import gen_pattern  # noqa: E402

with open(JSON_PATH, "r", encoding="utf-8") as f:
    PATTERN = json.load(f)

EXPECTED_RECTS = {
    "cross_v": ("rect", 128, 0, 1, 192, [255, 255, 255]),
    "cross_h": ("rect", 0, 96, 256, 1, [255, 255, 255]),
    "block3x3": ("rect", 127, 95, 3, 3, [255, 255, 255]),
    "hstrip": ("alt_columns", 0, 40, 256, 8, [255, 255, 255]),
    "vstrip": ("alt_rows", 40, 0, 8, 192, [255, 255, 255]),
    "gradient": ("gradient", 0, 140, 256, 16, [0, 0, 0]),
    "corner_tl": ("rect", 1, 1, 8, 8, [255, 0, 0]),
    "corner_tr": ("rect", 247, 1, 8, 8, [0, 255, 0]),
    "corner_bl": ("rect", 1, 183, 8, 8, [0, 0, 255]),
    "corner_br": ("rect", 247, 183, 8, 8, [255, 255, 0]),
    "frame": ("frame", 0, 0, 256, 192, [255, 255, 255]),
}


def minimal_error_v5(v8):
    """Brute-force closest v5; round(v8*31/255) breaks the tie at 144."""
    errors = [abs(((v5 << 3) | (v5 >> 2)) - v8) for v5 in range(32)]
    tied = [v5 for v5, err in enumerate(errors) if err == min(errors)]
    chosen = round(v8 * 31 / 255)
    assert chosen in tied, (v8, chosen, tied)
    return chosen


class PatternTest(unittest.TestCase):
    def test_all_elements_in_bounds(self):
        width = PATTERN["width"]
        height = PATTERN["height"]
        self.assertEqual((width, height), (256, 192))
        for elem in PATTERN["elements"]:
            self.assertGreaterEqual(elem["x"], 0, elem["name"])
            self.assertGreaterEqual(elem["y"], 0, elem["name"])
            self.assertGreater(elem["w"], 0, elem["name"])
            self.assertGreater(elem["h"], 0, elem["name"])
            self.assertLessEqual(elem["x"] + elem["w"], width, elem["name"])
            self.assertLessEqual(elem["y"] + elem["h"], height, elem["name"])
            self.assertEqual(len(elem.get("colour", elem.get("color"))), 3,
                             elem["name"])

    def test_required_geometry(self):
        names = {elem["name"]: elem for elem in PATTERN["elements"]}
        self.assertEqual(set(names), set(EXPECTED_RECTS))
        for name, expected in EXPECTED_RECTS.items():
            elem = names[name]
            self.assertEqual(
                (elem["type"], elem["x"], elem["y"], elem["w"], elem["h"],
                 elem.get("colour", elem.get("color"))), expected, name)
        self.assertEqual(names["hstrip"]["period"], 2)
        self.assertEqual(names["hstrip"]["phase"], 0)
        self.assertEqual(names["vstrip"]["period"], 2)
        self.assertEqual(names["vstrip"]["phase"], 0)
        self.assertEqual(names["gradient"]["step_px"], 16)
        self.assertEqual(names["gradient"]["step_value"], 16)
        self.assertEqual(
            [elem["name"] for elem in PATTERN["elements"]],
            ["cross_v", "cross_h", "block3x3", "hstrip", "vstrip",
             "gradient", "corner_tl", "corner_tr", "corner_bl",
             "corner_br", "frame"])

    def test_generated_header_matches_committed(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "pattern.h")
            completed = subprocess.run(
                [sys.executable, GEN, JSON_PATH, out],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            with open(out, "r", encoding="utf-8") as f:
                generated = f.read()
        with open(HEADER_PATH, "r", encoding="utf-8") as f:
            committed = f.read()
        self.assertEqual(generated, committed)

    def test_rom_uses_static_bitmap_bg_and_no_gx(self):
        with open(MAIN_C, "r", encoding="utf-8") as f:
            source = f.read()
        for token in ("MODE_5_2D", "DISPLAY_BG3_ACTIVE", "BgType_Bmp16",
                      "BgSize_B16_256x256", "VRAM_A_MAIN_BG",
                      "consoleDemoInit", "PATTERN_ID",
                      "swiWaitForVBlank"):
            self.assertIn(token, source, token)
        for forbidden in ("glInit", "fifo_hook", "twlhd_", "arm9v2"):
            self.assertNotIn(forbidden, source, forbidden)

    def test_quantisation_is_minimal_error(self):
        """Every 5-bit channel is the closest match under v5<<3|v5>>2."""
        header = gen_pattern.header_text(PATTERN)
        ops = [int(m, 16) for m in re.findall(
            r"^    \{[^}]*?(0x[0-9A-Fa-f]{4})[^}]*\}, /\*", header, re.M)]
        self.assertEqual(len(ops), len(PATTERN["elements"]))
        for elem, word in zip(PATTERN["elements"], ops):
            expected = 0x8000
            for shift, channel in zip((0, 5, 10),
                                      elem.get("colour", elem.get("color"))):
                expected |= minimal_error_v5(channel) << shift
            self.assertEqual(word, expected, elem["name"])

        block = header.split("PATTERN_GRADIENT_BGR555[] = {", 1)[1]
        words = [int(w, 16) for w in
                 re.findall(r"0x[0-9A-Fa-f]{4}", block.split("};", 1)[0])]
        gradient = next(e for e in PATTERN["elements"]
                        if e["type"] == "gradient")
        expected = []
        for step in range(gradient["w"] // gradient["step_px"]):
            v5 = minimal_error_v5(step * gradient["step_value"])
            expected.append(0x8000 | (v5 << 10) | (v5 << 5) | v5)
        self.assertEqual(len(words), 16)
        for step, (word, expected_word) in enumerate(zip(words, expected)):
            with self.subTest(grey=step * gradient["step_value"]):
                self.assertEqual(
                    word, expected_word,
                    "grey %d: header 0x%04X (v5 %d), expected 0x%04X (v5 %d)"
                    % (step * gradient["step_value"], word, word & 31,
                       expected_word, expected_word & 31))


if __name__ == "__main__":
    unittest.main()
