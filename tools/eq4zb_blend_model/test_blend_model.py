#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the EQ-4zb offline blend-residual model.

The model is pure arithmetic and deterministic; these tests pin the DS
convention spot values (EQ-4i), the source-derived candidates of our PICA
path, and the honest match-table result (no candidate reproduces the console
138/41).  One test cross-checks this module against
``tools/ds_alpha_test/expected.py`` by loading that file, so the two sets of
formulas cannot silently diverge.
"""

import importlib.util
import math
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import blend_model as bm  # noqa: E402


def load_expected():
    path = os.path.join(HERE, os.pardir, "ds_alpha_test", "expected.py")
    spec = importlib.util.spec_from_file_location("eq4zb_expected", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


EXPECTED = load_expected()


class DsConventionTest(unittest.TestCase):
    def test_expansions_match_expected_py(self):
        for v5 in range(32):
            self.assertEqual(bm.expand5(v5), EXPECTED.expand5(v5), v5)
            self.assertEqual(bm.expand6(v5), EXPECTED.expand6(v5), v5)

    def test_round_half_up_matches_expected_py(self):
        for value in (0.0, 0.4999, 0.5, 1.5, 2.5, 36.42857, 141.6667):
            self.assertEqual(bm.round_half_up(value),
                             EXPECTED.round_half_up(value), value)

    def test_weight_matches_expected_py_a2(self):
        for alpha in range(1, 32):
            self.assertAlmostEqual(bm.blend_weight(alpha),
                                   EXPECTED.weight(alpha, "A2"))

    def test_alpha_zero_is_wireframe(self):
        self.assertEqual(bm.blend_weight(0), 0.0)
        self.assertEqual(bm.exact_float_round(0, 255, 0), 0)

    def test_eq4i_spot_values(self):
        # EQ-4i §5.1/§5.2 over a black backdrop, source 5-bit 31.
        self.assertEqual(bm.ds_a2_v6(16), 33)
        self.assertEqual(bm.ds_green(16), 134)
        self.assertEqual(bm.ds_a2_v6(4), 9)
        self.assertEqual(bm.ds_redblue(4), 33)

    def test_expected6_565_agreement(self):
        # expected6_565 returns (R, G, B); the green/red-blue channels agree.
        self.assertEqual(bm.ds_green(16), EXPECTED.expected6_565(16, 0, "A2")[1])
        self.assertEqual(bm.ds_redblue(4), EXPECTED.expected6_565(4, 0, "A2")[2])
        self.assertEqual(bm.ds_redblue(4), EXPECTED.expected6_565(4, 0, "A2")[0])


class OurPathCandidateTest(unittest.TestCase):
    def test_float_weight_values(self):
        self.assertAlmostEqual(bm.blend_weight(16), 17.0 / 32.0)
        self.assertAlmostEqual(bm.blend_weight(4), 5.0 / 32.0)

    def test_exact_float_black(self):
        # w=17/32 -> 135.46875; w=5/32 -> 39.84375.
        self.assertEqual(bm.exact_float_floor(16, 255, 0), 135)
        self.assertEqual(bm.exact_float_round(16, 255, 0), 135)
        self.assertEqual(bm.exact_float_floor(4, 255, 0), 39)
        self.assertEqual(bm.exact_float_round(4, 255, 0), 40)

    def test_exact_float_grey(self):
        # Over (16,16,24): green 142.96875, blue 60.09375.
        self.assertEqual(bm.exact_float_floor(16, 255, 16), 142)
        self.assertEqual(bm.exact_float_round(16, 255, 16), 143)
        self.assertEqual(bm.exact_float_floor(4, 255, 24), 60)
        self.assertEqual(bm.exact_float_round(4, 255, 24), 60)

    def test_alpha8_conversion(self):
        # w*255: 135.46875 trunc=135 round=135; 39.84375 trunc=39 round=40.
        self.assertEqual(bm.alpha8_trunc_floor(16, 255, 0), 135)
        self.assertEqual(bm.alpha8_round_round(16, 255, 0), 135)
        self.assertEqual(bm.alpha8_trunc_floor(4, 255, 0), 39)
        self.assertEqual(bm.alpha8_round_round(4, 255, 0), 40)

    def test_alpha0_blends_to_backdrop(self):
        for _name, fn in bm.OUR_CANDIDATES:
            self.assertEqual(fn(0, 255, 0), 0)


class MatchTableTest(unittest.TestCase):
    def test_table_shape(self):
        rows = bm.match_table()
        self.assertEqual(len(rows), 1 + len(bm.OUR_CANDIDATES))
        self.assertEqual(rows[0][0], "DS convention (EQ-4i)")

    def test_no_candidate_matches_black_138_41(self):
        # eq4za: green 138, blue 41.  Every our-path candidate must be low.
        for name, g_black, b_black, _g_grey, _b_grey in bm.match_table()[1:]:
            self.assertNotEqual((g_black, b_black), (138, 41), name)
            self.assertLessEqual(g_black, 135, name)
            self.assertLessEqual(b_black, 40, name)

    def test_no_candidate_matches_eq4y_blue_57(self):
        # eq4y blue 57 is below every candidate over (16,16,24).
        for name, _g_black, _b_black, _g_grey, b_grey in bm.match_table()[1:]:
            self.assertLess(57, b_grey, name)

    def test_floor_candidate_matches_eq4x_grey(self):
        # eq4x emulator (16,16,24) prediction 142/60; exact floor reproduces it.
        self.assertEqual((bm.exact_float_floor(16, 255, 16),
                          bm.exact_float_floor(4, 255, 24)), (142, 60))

    def test_round_candidate_matches_eq4z_black(self):
        # eq4z emulator black prediction 135/40; exact round reproduces it.
        self.assertEqual((bm.exact_float_round(16, 255, 0),
                          bm.exact_float_round(4, 255, 0)), (135, 40))


class AlphaTableTest(unittest.TestCase):
    def test_full_range(self):
        for channel in ("G", "B"):
            rows = bm.alpha_table(channel)
            self.assertEqual([r[0] for r in rows], list(range(32)))
            for _alpha, _ds, _ours, delta in rows:
                self.assertEqual(delta, _ours - _ds)

    def test_spot_deltas(self):
        green = {a: d for a, _d, _o, d in bm.alpha_table("G")}
        blue = {a: d for a, _d, _o, d in bm.alpha_table("B")}
        self.assertEqual(green[16], 1)   # DS 134 vs ours 135
        self.assertEqual(blue[4], 7)     # DS 33 vs ours 40
        self.assertEqual(green[31], 0)
        self.assertEqual(blue[31], 0)

    def test_monotonic_non_decreasing(self):
        for channel in ("G", "B"):
            ours = [o for _a, _d, o, _delta in bm.alpha_table(channel)]
            self.assertEqual(ours, sorted(ours))


if __name__ == "__main__":
    unittest.main()
