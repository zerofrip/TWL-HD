#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_alpha_test/measure_edges.py.

Synthetic captures are rendered from quads.json with a known sub-pixel shift by
**area coverage in capture space**: each capture pixel's box is intersected with
the shifted (fractional) DS rectangle, so the edge produces a linear coverage
ramp and the interpolated 50 % crossing recovers the shift exactly.  A plain
nearest-neighbour resample of an integer DS frame cannot encode a sub-pixel
shift and would not meet the 0.1 DS px target.

Coverage:
  * +0.5 and -1.0 DS px shifts are recovered within 0.1 DS px per axis;
  * a negative control fails when the +0.5 result is compared with -1.0 or 0;
  * an unshifted native 256x192 frame recovers the nominal edges within 0.1;
  * the committed real-hardware fixture is pinned as literals (mean/std per
    axis) with a stated 0.01 DS px tolerance taken from the quoted run;
  * a missing fixture fails (explicit existence assert).

The synthetic and PPM tests use only the standard library.  The fixture is a
PNG, so the pinned-literal test uses the repo's labelled ``skipUnless``
convention when PIL is missing; the existence assert itself never skips, so a
missing fixture fails the suite even without PIL.
"""

import json
import math
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_edges.py")
FIXTURE = os.path.join(HERE, "fixtures", "al1_3_top400x240.png")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import expected  # noqa: E402
import measure_edges  # noqa: E402

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
SIDES = ("left", "right", "top", "bottom")


def _coverage(cx, cy, rx, ry, rw, rh, mapping):
    """Fraction of capture pixel (cx, cy) covered by the DS rectangle."""
    scale_x = mapping["scale_x"]
    scale_y = mapping["scale_y"]
    lo_x = (cx - mapping["offset_x"]) / scale_x
    hi_x = (cx + 1 - mapping["offset_x"]) / scale_x
    lo_y = (cy - mapping["offset_y"]) / scale_y
    hi_y = (cy + 1 - mapping["offset_y"]) / scale_y
    overlap_x = max(0.0, min(hi_x, rx + rw) - max(lo_x, rx))
    overlap_y = max(0.0, min(hi_y, ry + rh) - max(lo_y, ry))
    return (overlap_x / (hi_x - lo_x)) * (overlap_y / (hi_y - lo_y))


def render_scene(mapping, size, shift_x=0.0, shift_y=0.0):
    """Render quads.json in capture space with a fractional DS shift."""
    width, height = size
    image = [[0.0, 0.0, 0.0] for _ in range(width * height)]
    layers = []
    for backdrop in DATA["backdrops"]:
        value = expected.expand5(backdrop["colour5"][0])
        layers.append((backdrop["x"], backdrop["y"], backdrop["w"],
                       backdrop["h"], (value, value, value)))
    for quad in DATA["quads"]:
        b5 = BACKDROP_BY_NAME[quad["backdrop"]]["colour5"][0]
        value = expected.expected8(quad["alpha"], b5, "A2", "8bit_round")
        layers.append((quad["x"], quad["y"], quad["w"], quad["h"],
                       (value, value, value)))
    for rx0, ry0, rw, rh, colour in layers:
        rx = rx0 + shift_x
        ry = ry0 + shift_y
        cx_lo = max(0, int(math.floor(mapping["offset_x"]
                                      + mapping["scale_x"] * rx)))
        cx_hi = min(width - 1, int(math.ceil(mapping["offset_x"]
                                             + mapping["scale_x"] * (rx + rw)))
                    - 1)
        cy_lo = max(0, int(math.floor(mapping["offset_y"]
                                      + mapping["scale_y"] * ry)))
        cy_hi = min(height - 1, int(math.ceil(mapping["offset_y"]
                                              + mapping["scale_y"]
                                              * (ry + rh))) - 1)
        for cy in range(cy_lo, cy_hi + 1):
            for cx in range(cx_lo, cx_hi + 1):
                fraction = _coverage(cx, cy, rx, ry, rw, rh, mapping)
                if fraction <= 0.0:
                    continue
                base = image[cy * width + cx]
                image[cy * width + cx] = [
                    base[i] * (1.0 - fraction) + colour[i] * fraction
                    for i in range(3)]
    pixels = [tuple(int(round(channel)) for channel in pixel)
              for pixel in image]
    return pixels, width, height


def side_errors(observations, side, column="hw"):
    values = []
    for obs in observations:
        if obs["kind"] != "quad" or obs["side"] != side:
            continue
        if obs["estimates"].get(column) is None:
            continue
        values.append(obs["estimates"][column] - obs["nominal"])
    return values


def mean_and_std(values):
    count = len(values)
    mean = sum(values) / count
    variance = sum((v - mean) ** 2 for v in values) / count
    return mean, math.sqrt(variance), count


def measure(pixels, width, height, mapping):
    observations, _levels = measure_edges.measure_image(pixels, width, height,
                                                        mapping, DATA)
    return [dict(obs, estimates={"hw": obs["estimate"]})
            for obs in observations]


def write_ppm(path, pixels, width, height):
    data = bytearray()
    for r, g, b in pixels:
        data.extend((r, g, b))
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (width, height))
        handle.write(data)


class SyntheticRecoveryTest(unittest.TestCase):
    """Area-coverage synthetic captures; standard library only."""

    def _capture_means(self, shift_x, shift_y):
        pixels, width, height = render_scene(MAPPING, (400, 240),
                                             shift_x, shift_y)
        observations = measure(pixels, width, height, MAPPING)
        return {side: mean_and_std(side_errors(observations, side))[0]
                for side in SIDES}

    def test_half_pixel_shift_recovered(self):
        means = self._capture_means(0.5, 0.5)
        for side in SIDES:
            self.assertAlmostEqual(means[side], 0.5, delta=0.1,
                                   msg="%s: %r" % (side, means[side]))

    def test_minus_one_pixel_shift_recovered(self):
        means = self._capture_means(-1.0, -1.0)
        for side in SIDES:
            self.assertAlmostEqual(means[side], -1.0, delta=0.1,
                                   msg="%s: %r" % (side, means[side]))

    def test_negative_control_detects_wrong_shift(self):
        means = self._capture_means(0.5, 0.5)
        for side in SIDES:
            # The +0.5 build must not look like a -1.0 build or an unshifted
            # build: a wrong shift result is detected as outside this window.
            self.assertAlmostEqual(means[side], 0.5, delta=0.1)
            self.assertGreater(abs(means[side] - (-1.0)), 0.25)
            self.assertGreater(abs(means[side] - 0.0), 0.25)

    def test_native_unshifted_recovers_nominal(self):
        identity = measure_edges.identity_mapping()
        pixels, width, height = render_scene(identity, (256, 192))
        observations = measure(pixels, width, height, identity)
        for side in SIDES:
            mean, _std, count = mean_and_std(side_errors(observations, side))
            self.assertAlmostEqual(mean, 0.0, delta=0.1,
                                   msg="%s: %r" % (side, mean))
            self.assertGreaterEqual(count, 14)

    def test_native_half_pixel_shift_recovered(self):
        identity = measure_edges.identity_mapping()
        pixels, width, height = render_scene(identity, (256, 192), 0.5, 0.5)
        observations = measure(pixels, width, height, identity)
        for side in SIDES:
            mean, _std, _count = mean_and_std(
                side_errors(observations, side))
            self.assertAlmostEqual(mean, 0.5, delta=0.1,
                                   msg="%s: %r" % (side, mean))

    def test_backdrop_boundary_measured(self):
        identity = measure_edges.identity_mapping()
        pixels, width, height = render_scene(identity, (256, 192))
        observations = measure(pixels, width, height, identity)
        boundary = [obs for obs in observations
                    if obs["kind"] == "backdrop" and obs["side"] == "right"
                    and obs["estimate"] is not None]
        self.assertEqual(len(boundary), 1)
        self.assertIsNotNone(boundary[0]["estimate"])
        self.assertAlmostEqual(boundary[0]["estimate"], 128.0, delta=0.1)


class PpmReaderTest(unittest.TestCase):
    def test_parse_ppm_round_trip(self):
        pixels = [(1, 2, 3), (4, 5, 6), (7, 8, 9), (10, 11, 12)]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tiny.ppm")
            write_ppm(path, pixels, 2, 2)
            parsed, width, height = measure_edges.parse_ppm(path)
        self.assertEqual((width, height), (2, 2))
        self.assertEqual(parsed, pixels)

    def test_parse_ppm_rejects_bad_magic(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "bad.ppm")
            with open(path, "wb") as handle:
                handle.write(b"P3\n1 1\n255\n0 0 0\n")
            with self.assertRaises(ValueError):
                measure_edges.parse_ppm(path)


class CliDeterminismTest(unittest.TestCase):
    def test_two_runs_are_byte_identical(self):
        identity = measure_edges.identity_mapping()
        pixels, width, height = render_scene(identity, (256, 192), 0.5, 0.5)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "frame.ppm")
            write_ppm(path, pixels, width, height)
            first = subprocess.run([sys.executable, MEASURE, "--native", path],
                                   capture_output=True, text=True)
            second = subprocess.run([sys.executable, MEASURE, "--native", path],
                                    capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertIn("mode=native", first.stdout)


class FixturePresenceTest(unittest.TestCase):
    """A missing fixture must fail, not skip."""

    def test_fixture_present(self):
        self.assertTrue(os.path.isfile(FIXTURE),
                        "missing fixture: %s" % FIXTURE)


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; measure_edges fixture test skipped")
class RealFixturePinTest(unittest.TestCase):
    """Pinned literals from the quoted real-hardware run (report EQ-4j).

    Values are the per-axis mean and population standard deviation of
    hardware-minus-nominal for the committed mapping (scale 1.25, offset 40,0);
    tolerance 0.01 DS px against the 4-decimal quoted output.
    """

    TOL = 0.01

    @classmethod
    def setUpClass(cls):
        pixels, width, height = measure_edges.read_image(FIXTURE)
        cls.observations = measure(pixels, width, height, MAPPING)

    def _pin(self, side, mean, std, count):
        measured = side_errors(self.observations, side)
        got_mean, got_std, got_count = mean_and_std(measured)
        self.assertAlmostEqual(got_mean, mean, delta=self.TOL,
                               msg="%s mean" % side)
        self.assertAlmostEqual(got_std, std, delta=self.TOL,
                               msg="%s std" % side)
        self.assertEqual(got_count, count, side)

    def test_x_left(self):
        self._pin("left", -0.1069, 0.0553, 13)

    def test_x_right(self):
        self._pin("right", -0.0879, 0.0348, 13)

    def test_y_top(self):
        self._pin("top", -0.0734, 0.0250, 14)

    def test_y_bottom(self):
        self._pin("bottom", -0.0772, 0.0229, 14)

    def test_width(self):
        values = []
        for quad in DATA["quads"]:
            obs = {o["side"]: o for o in self.observations
                   if o["kind"] == "quad" and o["rect"] == quad["name"]}
            left, right = obs.get("left"), obs.get("right")
            if (left is None or right is None
                    or left["estimates"]["hw"] is None
                    or right["estimates"]["hw"] is None):
                continue
            width = right["estimates"]["hw"] - left["estimates"]["hw"]
            values.append(width - (right["nominal"] - left["nominal"]))
        mean, std, count = mean_and_std(values)
        self.assertAlmostEqual(mean, 0.0179, delta=self.TOL)
        self.assertAlmostEqual(std, 0.0575, delta=self.TOL)
        self.assertEqual(count, 12)

    def test_height(self):
        values = []
        for quad in DATA["quads"]:
            obs = {o["side"]: o for o in self.observations
                   if o["kind"] == "quad" and o["rect"] == quad["name"]}
            top, bottom = obs.get("top"), obs.get("bottom")
            if (top is None or bottom is None
                    or top["estimates"]["hw"] is None
                    or bottom["estimates"]["hw"] is None):
                continue
            height = bottom["estimates"]["hw"] - top["estimates"]["hw"]
            values.append(height - (bottom["nominal"] - top["nominal"]))
        mean, std, count = mean_and_std(values)
        self.assertAlmostEqual(mean, -0.0038, delta=self.TOL)
        self.assertAlmostEqual(std, 0.0460, delta=self.TOL)
        self.assertEqual(count, 14)


if __name__ == "__main__":
    unittest.main()
