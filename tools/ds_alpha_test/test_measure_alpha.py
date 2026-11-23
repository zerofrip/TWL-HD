#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_alpha_test/measure_alpha.py.

Synthesises 400x240 captures (and one native 256x192 frame) by inverting the
DS->capture mapping from quads.json + expected.py, then checks that an A1-built
capture classifies as A1, an A2-built capture as A2, a flat grey capture as
NOT-COMPARABLE, and a too-small image as NOT-COMPARABLE.  Skips with a labelled
reason when PIL is missing.  No PNG fixture is committed; everything is built in
memory.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_alpha.py")
FIXTURE = os.path.join(HERE, "fixtures", "al1_3_top400x240.png")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import expected  # noqa: E402
import measure_alpha  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

with open(os.path.join(HERE, "quads.json"), "r", encoding="utf-8") as _f:
    DATA = json.load(_f)
with open(os.path.join(HERE, "capture_mapping.json"), "r",
          encoding="utf-8") as _f:
    MAPPING = json.load(_f)

BACKDROP_BY_NAME = {b["name"]: b for b in DATA["backdrops"]}


def ds_colour(ds_x, ds_y, convention, variant="8bit_round"):
    """Colour of one DS pixel in a synthetic frame for a convention."""
    for quad in DATA["quads"]:
        if (quad["x"] <= ds_x < quad["x"] + quad["w"]
                and quad["y"] <= ds_y < quad["y"] + quad["h"]):
            b5 = BACKDROP_BY_NAME[quad["backdrop"]]["colour5"][0]
            if variant == expected.VARIANT_565:
                return expected.expected6_565(quad["alpha"], b5, convention)
            v = expected.expected8(quad["alpha"], b5, convention, variant)
            return (v, v, v)
    for bd in DATA["backdrops"]:
        if (bd["x"] <= ds_x < bd["x"] + bd["w"]
                and bd["y"] <= ds_y < bd["y"] + bd["h"]):
            c5 = bd["colour5"][0]
            if variant == expected.VARIANT_565:
                # Opaque surface of colour c5 through the 565 display model.
                return expected.expected6_565(31, c5, "A2", src5=c5)
            v = expected.expand5(c5)
            return (v, v, v)
    c5 = DATA["clear5"][0]
    if variant == expected.VARIANT_565:
        return expected.expected6_565(31, c5, "A2", src5=c5)
    v = expected.expand5(c5)
    return (v, v, v)


def synthetic_frame(convention, mapping, size, variant="8bit_round"):
    """Frame built by inverse-mapping every pixel (nearest neighbour)."""
    image = Image.new("RGB", size, (0, 0, 0))
    pixels = image.load()
    for cy in range(size[1]):
        ds_y = int(round((cy - mapping["offset_y"] + 0.5)
                         / mapping["scale_y"] - 0.5))
        for cx in range(size[0]):
            ds_x = int(round((cx - mapping["offset_x"] + 0.5)
                             / mapping["scale_x"] - 0.5))
            if 0 <= ds_x < DATA["width"] and 0 <= ds_y < DATA["height"]:
                pixels[cx, cy] = ds_colour(ds_x, ds_y, convention, variant)
    return image


def synthetic_capture(convention, variant="8bit_round"):
    return synthetic_frame(convention, MAPPING, (400, 240), variant)


def synthetic_native(convention, variant="8bit_round"):
    return synthetic_frame(convention, measure_alpha.identity_mapping(),
                           (256, 192), variant)


def report_dict(lines):
    return dict(lines)


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; measure_alpha capture tests skipped")
class MeasureAlphaTest(unittest.TestCase):
    def test_a1_capture_verdict_a1(self):
        lines, verdict, comparable = measure_alpha.measure(
            synthetic_capture("A1"), DATA, MAPPING)
        self.assertTrue(comparable)
        self.assertEqual(verdict, "A1")
        self.assertEqual(report_dict(lines)["verdict"], "A1")

    def test_a2_capture_verdict_a2(self):
        lines, verdict, comparable = measure_alpha.measure(
            synthetic_capture("A2"), DATA, MAPPING)
        self.assertTrue(comparable)
        self.assertEqual(verdict, "A2")

    def test_nearest_matches_convention_for_strong_quads(self):
        for convention in ("A1", "A2"):
            lines, _verdict, _c = measure_alpha.measure(
                synthetic_capture(convention), DATA, MAPPING)
            report = report_dict(lines)
            for quad in DATA["quads"]:
                key = "quad_%s_nearest" % quad["name"]
                sep = expected.separations(
                    quad["alpha"],
                    BACKDROP_BY_NAME[quad["backdrop"]]["colour5"][0])
                if sep[measure_alpha.PRIMARY_VARIANT] >= measure_alpha.VOTE_SEP:
                    self.assertEqual(report[key], convention, key)
                else:
                    self.assertEqual(report[key], "ambiguous", key)

    def test_flat_grey_is_not_comparable(self):
        image = Image.new("RGB", (400, 240), (128, 128, 128))
        lines, verdict, comparable = measure_alpha.measure(
            image, DATA, MAPPING)
        self.assertFalse(comparable)
        self.assertEqual(verdict, "NOT-COMPARABLE")
        self.assertEqual(report_dict(lines)["geometry_found"], "0")

    def test_small_image_is_not_comparable(self):
        image = Image.new("RGB", (10, 10), (0, 0, 0))
        _lines, verdict, comparable = measure_alpha.measure(
            image, DATA, MAPPING)
        self.assertFalse(comparable)
        self.assertEqual(verdict, "NOT-COMPARABLE")

    def test_native_path(self):
        lines, verdict, comparable = measure_alpha.measure(
            synthetic_native("A2"), DATA, measure_alpha.identity_mapping())
        self.assertTrue(comparable)
        self.assertEqual(report_dict(lines)["mode"], "native")
        self.assertEqual(verdict, "A2")

    def test_6bit_a2_native_verdict_a2(self):
        lines, verdict, comparable = measure_alpha.measure(
            synthetic_native("A2", "6bit_round"), DATA,
            measure_alpha.identity_mapping())
        self.assertTrue(comparable)
        report = report_dict(lines)
        self.assertEqual(verdict, "A2")
        self.assertEqual(report["best_variant"], "6bit_round")
        self.assertEqual(report["best_convention"], "A2")

    def test_6bit_a1_capture_verdict_a1(self):
        lines, verdict, _c = measure_alpha.measure(
            synthetic_capture("A1", "6bit_round"), DATA, MAPPING)
        report = report_dict(lines)
        self.assertEqual(verdict, "A1")
        self.assertEqual(report["best_variant"], "6bit_round")
        self.assertEqual(report["best_convention"], "A1")

    def test_every_variant_verdict_key_present(self):
        lines, _verdict, _c = measure_alpha.measure(
            synthetic_capture("A1"), DATA, MAPPING)
        report = report_dict(lines)
        for variant in expected.VARIANTS:
            self.assertIn("variant_%s_verdict" % variant, report)

    def test_subprocess_output_is_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "a1.png")
            synthetic_capture("A1").save(path)
            first = subprocess.run([sys.executable, MEASURE, path],
                                   capture_output=True, text=True)
            second = subprocess.run([sys.executable, MEASURE, path],
                                    capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertIn("verdict=A1", first.stdout)

    def test_subprocess_native_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "native.png")
            synthetic_native("A1").save(path)
            completed = subprocess.run(
                [sys.executable, MEASURE, "--native", path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("mode=native", completed.stdout)
        self.assertIn("verdict=A1", completed.stdout)

    def test_subprocess_not_comparable_exit_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "grey.png")
            Image.new("RGB", (400, 240), (128, 128, 128)).save(path)
            completed = subprocess.run([sys.executable, MEASURE, path],
                                       capture_output=True, text=True)
        self.assertEqual(completed.returncode, 1)
        self.assertIn("verdict=NOT-COMPARABLE", completed.stdout)

    def test_565_a2_capture_g_channel_a2(self):
        lines, _verdict, comparable = measure_alpha.measure(
            synthetic_capture("A2", expected.VARIANT_565), DATA, MAPPING)
        report = report_dict(lines)
        self.assertTrue(comparable)
        self.assertEqual(report["g_votes_A1"], "0")
        self.assertGreaterEqual(int(report["g_votes_A2"]), 5)
        self.assertEqual(report["g_channel_verdict"], "A2")

    def test_565_a1_capture_g_channel_a1(self):
        lines, _verdict, comparable = measure_alpha.measure(
            synthetic_capture("A1", expected.VARIANT_565), DATA, MAPPING)
        report = report_dict(lines)
        self.assertTrue(comparable)
        self.assertEqual(report["g_votes_A2"], "0")
        self.assertGreaterEqual(int(report["g_votes_A1"]), 5)
        self.assertEqual(report["g_channel_verdict"], "A1")

    def test_flat_grey_g_channel_not_comparable(self):
        image = Image.new("RGB", (400, 240), (128, 128, 128))
        lines, verdict, comparable = measure_alpha.measure(
            image, DATA, MAPPING)
        report = report_dict(lines)
        self.assertFalse(comparable)
        self.assertEqual(verdict, "NOT-COMPARABLE")
        self.assertEqual(report["geometry_found"], "0")
        self.assertEqual(report["g_channel_verdict"], "NOT-COMPARABLE")

    def test_real_fixture_g_channel_a2(self):
        """Pinned literals from the quoted real-hardware run (report eq4i)."""
        image = Image.open(FIXTURE)
        image.load()
        lines, verdict, comparable = measure_alpha.measure(
            image, DATA, MAPPING)
        report = report_dict(lines)
        self.assertTrue(comparable)
        self.assertEqual(report["g_votes_A1"], "0")
        self.assertEqual(report["g_votes_A2"], "10")
        self.assertEqual(report["g_votes_ambiguous"], "4")
        self.assertEqual(report["g_channel_verdict"], "A2")
        self.assertEqual(verdict, "A1")  # whole-RGB artefact is preserved
        self.assertIn("not decisive", report["verdict_note"])
        self.assertEqual(report["fit_6bit_565_A2_R"], "0")
        self.assertEqual(report["fit_6bit_565_A2_B"], "0")


class VerdictLogicTest(unittest.TestCase):
    """Pure classifier-logic tests that do not need PIL."""

    def test_nearest_tie(self):
        self.assertEqual(measure_alpha._nearest((10, 10, 10), (5, 5, 5),
                                                (15, 15, 15)), "tie")

    def test_variant_verdict_needs_votes(self):
        quad = {"measured": (0, 0, 0), "A1": {"v": 0}, "A2": {"v": 0},
                "sep": {"v": 0}}
        self.assertEqual(measure_alpha._variant_verdict([quad], "v"),
                         "UNDECIDED")

    def test_pc_vote_ambiguity_threshold(self):
        # Equal distances and a difference < 2 are ambiguous.
        self.assertEqual(measure_alpha._pc_vote(10, 5, 15)[0], "ambiguous")
        self.assertEqual(measure_alpha._pc_vote(10, 8, 13)[0], "ambiguous")
        self.assertEqual(measure_alpha._pc_vote(10, 6, 14)[0], "ambiguous")
        # A distance difference of >= 2 votes for the nearer side.
        self.assertEqual(measure_alpha._pc_vote(10, 8, 20)[0], "A1")
        self.assertEqual(measure_alpha._pc_vote(10, 20, 8)[0], "A2")

    def _quad(self, measured, a1, a2):
        return {"measured": measured,
                "A1": {v: a1.get(v, 0) for v in measure_alpha.OVERALL_VARIANTS},
                "A2": {v: a2.get(v, 0) for v in measure_alpha.OVERALL_VARIANTS}}

    def test_best_family_verdict_minimum_error(self):
        # 6bit A2 is exact and every other family is far off.
        quad = self._quad((36, 36, 36), {"6bit_round": 32},
                          {"6bit_round": 36})
        verdict, variant, convention, total = \
            measure_alpha._best_family_verdict([quad])
        self.assertEqual((verdict, variant, convention), ("A2", "6bit_round",
                                                          "A2"))
        self.assertEqual(total, 0)

    def test_best_family_verdict_undecided_outside_tol(self):
        # Best fit is 6bit A2 (36) but measured 56 is 20 levels away: outside
        # TOL=8, so the best family cannot decide the convention.
        quad = self._quad((56, 56, 56), {"6bit_round": 32},
                          {"6bit_round": 36})
        verdict, variant, convention, _total = \
            measure_alpha._best_family_verdict([quad])
        self.assertEqual(verdict, "UNDECIDED")
        self.assertEqual((variant, convention), ("6bit_round", "A2"))

    def test_best_family_verdict_tie_break_is_deterministic(self):
        # All candidates exact: earliest variant in OVERALL_VARIANTS, A1 first.
        quad = self._quad((0, 0, 0), {}, {})
        verdict, variant, convention, total = \
            measure_alpha._best_family_verdict([quad])
        self.assertEqual(
            (verdict, variant, convention, total),
            ("A1", measure_alpha.OVERALL_VARIANTS[0], "A1", 0))


if __name__ == "__main__":
    unittest.main()
