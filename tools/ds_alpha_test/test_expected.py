#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Expected-table tests for tools/ds_alpha_test/expected.py.

Pins the expansion, the two weights, both blend families, round-half-up (not
banker's rounding), the hand-computed a=16 values and the weak-discriminator
classification.
"""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import expected  # noqa: E402


class ExpectedTest(unittest.TestCase):
    def test_expand5(self):
        self.assertEqual([expected.expand5(v) for v in (0, 16, 31)],
                         [0, 132, 255])

    def test_expand6(self):
        # DeSmuME-style 5->6 expansion: (v<<1)|(v>>4).
        self.assertEqual([expected.expand6(v) for v in (0, 16, 31)],
                         [0, 33, 63])

    def test_round_half_up_is_not_bankers(self):
        self.assertEqual(expected.round_half_up(0.5), 1)
        self.assertEqual(expected.round_half_up(2.5), 3)
        self.assertEqual(expected.round_half_up(16 / 31 * 255), 132)

    def test_weights(self):
        self.assertAlmostEqual(expected.weight(16, "A1"), 16 / 31)
        self.assertAlmostEqual(expected.weight(16, "A2"), 17 / 32)

    def test_a16_over_black_hand_computed(self):
        # 8-bit blend: A1 = 16/31*255 = 131.61 -> 132; A2 = 17/32*255 = 135.47 -> 135.
        self.assertEqual(
            expected.expected8(16, 0, "A1", "8bit_round"), 132)
        self.assertEqual(
            expected.expected8(16, 0, "A2", "8bit_round"), 135)
        # 5-bit blend then expand: both round to b5=16 -> expand5(16) = 132.
        self.assertEqual(
            expected.expected8(16, 0, "A1", "5bit_round"), 132)
        self.assertEqual(
            expected.expected8(16, 0, "A2", "5bit_round"), 132)
        # floor variant.
        self.assertEqual(
            expected.expected8(16, 0, "A1", "8bit_floor"), 131)
        self.assertEqual(
            expected.expected8(16, 0, "A2", "8bit_floor"), 135)

    def test_a4_over_black_separations(self):
        sep = expected.separations(4, 0)
        self.assertEqual(sep["8bit_round"], 7)
        self.assertEqual(sep["5bit_round"], 8)
        self.assertEqual(sep["5bit_floor"], 0)
        self.assertEqual(sep["6bit_round"], 4)

    def test_a4_over_black_6bit_a2_pinned(self):
        # DeSmuME-style 6-bit pipeline.  v6 = ((4+1)*63 + (32-5)*0) >> 5 = 9;
        # out = floor(9*255/63 + 0.5) = 36.
        v6 = ((4 + 1) * expected.expand6(31)
              + (32 - 5) * expected.expand6(0)) >> 5
        self.assertEqual(v6, 9)
        self.assertEqual(expected.round_half_up(v6 * 255 / 63), 36)
        # The literal 36 and the formula agree, and expected8() returns it.
        self.assertEqual(expected.expected8(4, 0, "A2", "6bit_round"), 36)

    def test_a4_over_grey_6bit_a2_pinned(self):
        # v6 = (5*63 + 27*33) >> 5 = 37; out = floor(37*255/63 + 0.5) = 150.
        self.assertEqual(expected.expected8(4, 16, "A2", "6bit_round"), 150)

    def test_a8_over_black_6bit_a2_pinned(self):
        # v6 = (9*63) >> 5 = 17; out = floor(17*255/63 + 0.5) = 69.
        self.assertEqual(expected.expected8(8, 0, "A2", "6bit_round"), 69)

    def test_a12_over_black_6bit_a2_pinned(self):
        # v6 = (13*63) >> 5 = 25; out = floor(25*255/63 + 0.5) = 101.
        self.assertEqual(expected.expected8(12, 0, "A2", "6bit_round"), 101)

    def test_a4_over_black_6bit_a1_pinned(self):
        # A1 direction guard: v6 = floor((4/31)*63) = 8; out = 32.
        self.assertEqual(expected.expected8(4, 0, "A1", "6bit_round"), 32)

    def test_a24_over_black_6bit_a2_pinned(self):
        # v6 = (25*63) >> 5 = 49; prescribed round gives 198.  The harness
        # measured 199: the 1-level residual of round() vs DeSmuME's
        # (v6<<2)|(v6>>4) output stage, documented in the report.
        self.assertEqual(expected.expected8(24, 0, "A2", "6bit_round"), 198)

    def test_min_separation_a4_is_zero(self):
        # Pinned source: over black the A2 5bit_floor value (weight 5/32) floors
        # back to the A1 value for every alpha >= 1, so the smallest separation
        # is 0; the same holds over grey (e.g. grey a=4 differs by 8).
        self.assertEqual(expected.min_separation(4), 0)
        self.assertEqual(expected.min_separation(16), 0)

    def test_weak_discriminators(self):
        self.assertEqual(expected.weak_alphas(), [16, 20, 24, 28])
        for a in (4, 8, 12):
            self.assertGreaterEqual(expected.best_separation(a),
                                    expected.WEAK_SEPARATION)
        for a in (16, 20, 24, 28):
            self.assertLess(expected.best_separation(a),
                            expected.WEAK_SEPARATION)

    def test_expected_table_covers_every_variant_and_convention(self):
        for alpha in expected.ALPHAS:
            for backdrop5 in (0, 16):
                exp = expected.expectations(alpha, backdrop5)
                self.assertEqual(len(exp), len(expected.VARIANTS) * 2)
                for value in exp.values():
                    self.assertGreaterEqual(value, 0)
                    self.assertLessEqual(value, 255)

    def test_cli_prints_table_and_weak_note(self):
        import subprocess
        completed = subprocess.run(
            [sys.executable, os.path.join(HERE, "expected.py")],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        out = completed.stdout
        self.assertIn("weak discriminators", out)
        self.assertIn("a=16", out)
        self.assertIn("8bit_round", out)
        self.assertIn("6bit", out)
        self.assertIn("min=", out)


if __name__ == "__main__":
    unittest.main()
