#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the opt-in --row-runs probe in tools/ds_edge_test/measure_rows.py.

The synthetic native and capture frames come from the sibling test module so
the ground truth is shared; the committed hardware fixture is only pinned with
values a real run of this probe produced (never predicted here).
"""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_rows.py")
FIXTURE = os.path.join(HERE, "fixtures", "ed1_3_top400x240.png")
DESMUME_FIXTURE = os.path.join(HERE, "fixtures",
                               "ed1_3_desmume_native256x192.png")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_rows  # noqa: E402
import test_measure_rows as base  # noqa: E402


def items_by_name():
    return {item["name"]: item for item in base.DATA["quads"]}


def pil_pixels(image):
    image = image.convert("RGB")
    raw = image.tobytes()
    return ([(raw[i], raw[i + 1], raw[i + 2])
             for i in range(0, len(raw), 3)], image.width, image.height)


def probe(pixels, width, height, name, mapping):
    return measure_rows._probe_item(
        pixels, width, height, items_by_name()[name], mapping)


class NativeRowRunsTest(unittest.TestCase):
    def setUp(self):
        self.mapping = measure_rows.measure_edges.identity_mapping()

    def test_nominal_hline_is_one_row(self):
        result = probe(base.render_native(), base.WIDTH, base.HEIGHT,
                       "hline", self.mapping)
        self.assertEqual(result["covered"], [96])
        self.assertEqual(result["coverage_runs"], [(96, 96)])
        self.assertEqual(result["nominal_capture"], 96.0)
        entry = result["positions"][result["covered"][0] - result["search"][0]]
        self.assertEqual(entry["start"], 64)
        self.assertEqual(entry["length"], 128)
        self.assertEqual(entry["start_ds"], 64.0)
        self.assertEqual(entry["length_ds"], 128.0)
        self.assertEqual(entry["peak"], 255.0)

    def test_nominal_vline_is_one_column(self):
        result = probe(base.render_native(), base.WIDTH, base.HEIGHT,
                       "vline", self.mapping)
        self.assertEqual(result["covered"], [208])
        self.assertEqual(result["coverage_runs"], [(208, 208)])
        entry = result["positions"][result["covered"][0] - result["search"][0]]
        self.assertEqual(entry["start"], 64)
        self.assertEqual(entry["length"], 96)

    def test_one_row_shift_moves_hline_only_in_rows(self):
        result = probe(base.render_native(default_shift=1), base.WIDTH,
                       base.HEIGHT, "hline", self.mapping)
        self.assertEqual(result["covered"], [97])
        self.assertEqual(result["coverage_runs"], [(97, 97)])

    def test_one_row_shift_keeps_vline_column(self):
        result = probe(base.render_native(default_shift=1), base.WIDTH,
                       base.HEIGHT, "vline", self.mapping)
        self.assertEqual(result["covered"], [208])


@unittest.skipUnless(base.HAVE_PIL, "PIL is not available")
class CaptureRowRunsTest(unittest.TestCase):
    def test_nearest_capture_hline_run(self):
        pixels, width, height = pil_pixels(base.render_capture())
        result = probe(pixels, width, height, "hline", base.MAPPING)
        self.assertEqual(result["coverage_runs"], [(120, 120)])
        entry = result["positions"][result["covered"][0]
                                     - result["search"][0]]
        self.assertEqual(entry["start"], 120)
        self.assertEqual(entry["length"], 160)
        self.assertAlmostEqual(entry["start_ds"], 64.0)
        self.assertAlmostEqual(entry["length_ds"], 128.0)

    def test_nearest_capture_vline_run(self):
        pixels, width, height = pil_pixels(base.render_capture())
        result = probe(pixels, width, height, "vline", base.MAPPING)
        self.assertEqual(result["coverage_runs"], [(300, 300)])
        entry = result["positions"][result["covered"][0]
                                     - result["search"][0]]
        self.assertEqual(entry["start"], 80)
        self.assertEqual(entry["length"], 120)
        self.assertAlmostEqual(entry["start_ds"], 64.0)
        self.assertAlmostEqual(entry["length_ds"], 96.0)


@unittest.skipUnless(base.HAVE_PIL, "PIL is not available")
class HardwareFixtureRowRunsTest(unittest.TestCase):
    """Values below were measured from this fixture by a real probe run."""

    def test_fixture_hline_and_vline_are_one_capture_row_or_column(self):
        pixels, width, height = measure_rows.measure_edges.read_image(FIXTURE)
        hline = probe(pixels, width, height, "hline", base.MAPPING)
        self.assertEqual(hline["coverage_runs"], [(120, 120)])
        entry = hline["positions"][hline["covered"][0] - hline["search"][0]]
        self.assertEqual(entry["start"], 120)
        self.assertEqual(entry["length"], 160)
        self.assertAlmostEqual(entry["start_ds"], 64.0)
        self.assertAlmostEqual(entry["length_ds"], 128.0)
        self.assertEqual(entry["peak"], 255.0)

        vline = probe(pixels, width, height, "vline", base.MAPPING)
        self.assertEqual(vline["coverage_runs"], [(300, 300)])
        entry = vline["positions"][vline["covered"][0] - vline["search"][0]]
        self.assertEqual(entry["start"], 80)
        self.assertEqual(entry["length"], 120)
        self.assertAlmostEqual(entry["start_ds"], 64.0)
        self.assertAlmostEqual(entry["length_ds"], 96.0)
        self.assertEqual(entry["peak"], 255.0)


@unittest.skipUnless(base.HAVE_PIL, "PIL is not available")
class DeSmuMeFixtureRowRunsTest(unittest.TestCase):
    """Pins values a real probe run produced on this committed fixture."""

    def test_fixture_hline_covers_two_rows_and_vline_starts_low(self):
        pixels, width, height = measure_rows.measure_edges.read_image(
            DESMUME_FIXTURE)
        mapping = measure_rows.measure_edges.identity_mapping()
        hline = probe(pixels, width, height, "hline", mapping)
        self.assertEqual(hline["coverage_runs"], [(96, 97)])
        vline = probe(pixels, width, height, "vline", mapping)
        entry = vline["positions"][vline["covered"][0] - vline["search"][0]]
        self.assertEqual(entry["start"], 65)
        self.assertEqual(entry["length"], 95)


class RowRunsCliTest(unittest.TestCase):
    def run_native(self, pixels, *flags):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "frame.ppm")
            base.write_ppm(path, pixels)
            return subprocess.run(
                [sys.executable, MEASURE, "--native", *flags, path],
                capture_output=True, text=True)

    def test_row_runs_is_opt_in(self):
        without = self.run_native(base.render_native())
        self.assertEqual(without.returncode, 0, without.stderr)
        self.assertNotIn("row-runs", without.stdout)
        with_flag = self.run_native(base.render_native(), "--row-runs")
        self.assertEqual(with_flag.returncode, 0, with_flag.stderr)
        self.assertIn("row-runs=on", with_flag.stdout)
        self.assertIn("row-runs item=hline rows_covered=1 "
                      "row_run=96-96", with_flag.stdout)
        self.assertIn("row-runs item=vline cols_covered=1 "
                      "col_run=208-208", with_flag.stdout)
        self.assertIn("row-runs item=hline profile_capture=91-101",
                      with_flag.stdout)


if __name__ == "__main__":
    unittest.main()
