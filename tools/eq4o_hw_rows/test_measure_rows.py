#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/eq4o_hw_rows/measure_rows.py.

Synthetic 400x240 captures are rendered from the EQ-4n §3.3 geometry with a
known row placement: nominal ``ceil(1.25*y_ds)``, the same scene shifted one
extra capture row (the negative case that must be detected), and the
``+3`` DS-row frame B.  The two committed real-hardware fixtures are measured
and their numbers asserted; they are the EQ-4o FACTs.
"""

import hashlib
import math
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_rows.py")
FRAME_A = os.path.join(HERE, "hw_frameA_top400x240.png")
FRAME_B = os.path.join(HERE, "hw_frameB_top400x240.png")
FRAME_A_SHA256 = \
    "68912b01d4a7e8164f41b8547910a0a533d4ca399cbf16e3a8bf729551856352"
FRAME_B_SHA256 = \
    "fc2b6534d1c50bfabdb11c21735ad8b4ab2eb5309a035116dce5f9328a3dbb3b"

if HERE not in sys.path:
    sys.path.insert(0, HERE)

import measure_rows  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

# The ds_edge_test scene in DS px: (x, y, w, h) of every white rectangle.
RECTS = (
    (0, 0, 256, 16),      # top band
    (0, 176, 256, 16),    # bottom band
    (0, 80, 16, 32),      # left band
    (240, 80, 16, 32),    # right band
    (32, 32, 32, 32),     # sq1
    (32, 128, 32, 32),    # sq3
    (64, 96, 128, 1),     # hline
    (208, 64, 1, 96),     # vline
)
CLEAR = (16, 16, 24)  # OLD-build clear colour; keep for historical captures.


def render_capture(dy=0, extra_shift=0):
    """Render the scene at ceil(1.25*y) + extra_shift capture rows."""
    image = Image.new("RGB", (400, 240), CLEAR)
    pixels = image.load()
    for x, y, w, h in RECTS:
        row0 = math.ceil(1.25 * (y + dy)) + extra_shift
        row1 = math.ceil(1.25 * (y + h + dy)) + extra_shift
        col0 = math.ceil(40 + 1.25 * x)
        col1 = math.ceil(40 + 1.25 * (x + w))
        for row in range(max(0, row0), min(240, row1)):
            for col in range(max(0, col0), min(400, col1)):
                pixels[col, row] = (255, 255, 255)
    return image


def render_capture_one_edge_early():
    """Frame A (dy=0) with only sq1's top edge moved one capture row early.

    Shifting a whole rect moves both its edges; moving only ``row0`` isolates
    the ``sq1.top`` edge (col-100 profile becomes dark 20-38, white 39-79).
    """
    image = Image.new("RGB", (400, 240), CLEAR)
    pixels = image.load()
    for x, y, w, h in RECTS:
        row0 = math.ceil(1.25 * y)
        if (x, y, w, h) == (32, 32, 32, 32):
            row0 -= 1
        row1 = math.ceil(1.25 * (y + h))
        col0 = math.ceil(40 + 1.25 * x)
        col1 = math.ceil(40 + 1.25 * (x + w))
        for row in range(max(0, row0), min(240, row1)):
            for col in range(max(0, col0), min(400, col1)):
                pixels[col, row] = (255, 255, 255)
    return image


def run_cli(image, *extra):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "capture.png")
        image.save(path)
        return subprocess.run([sys.executable, MEASURE] + list(extra) + [path],
                              capture_output=True, text=True)


def run_fixture(path, *extra):
    return subprocess.run([sys.executable, MEASURE] + list(extra) + [path],
                          capture_output=True, text=True)


def field(stdout, key):
    values = [line.split("=", 1)[1] for line in stdout.splitlines()
              if line.split("=", 1)[0] == key]
    return values


def edges(stdout):
    table = {}
    for line in stdout.splitlines():
        if not line.startswith("edge="):
            continue
        parts = dict(item.split("=", 1) for item in line.split(" "))
        table[parts.pop("edge")] = parts
    return table


class HelpersTest(unittest.TestCase):
    def test_white_runs_and_format(self):
        flags = [False, True, True, False, True]
        self.assertEqual(measure_rows.white_runs(flags), [(1, 2), (4, 4)])
        self.assertEqual(measure_rows.format_runs([]), "none")

    def test_classify(self):
        self.assertEqual(measure_rows.classify(20, 20.0), "ceil")
        self.assertEqual(measure_rows.classify(21, 20.0), "ceil+1")
        self.assertEqual(measure_rows.classify(24, 23.75), "ceil")
        self.assertEqual(measure_rows.classify(25, 23.75), "ceil+1")
        self.assertEqual(measure_rows.classify(19, 20.0), "ceil-1")
        self.assertEqual(measure_rows.classify(23, 23.75), "ceil-1")
        self.assertEqual(measure_rows.classify(None, 20.0), "other")


@unittest.skipUnless(HAVE_PIL, "PIL is not available; PNG tests skipped")
class SyntheticCliTest(unittest.TestCase):
    def test_nominal_lands_on_ceil(self):
        completed = run_cli(render_capture())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "white_runs"),
                         ["0-19,40-79,160-199,220-239"])
        self.assertEqual(field(completed.stdout, "row0_white"), ["1"])
        self.assertEqual(field(completed.stdout, "row0_rgb"),
                         ["255,255,255"])
        self.assertEqual(field(completed.stdout,
                               "row120_first_white_col"), ["40"])
        self.assertEqual(field(completed.stdout,
                               "row120_last_white_col"), ["359"])
        for name, entry in edges(completed.stdout).items():
            self.assertEqual(entry["match"], "ceil", name)

    def test_one_extra_row_is_detected(self):
        completed = run_cli(render_capture(extra_shift=1))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        table = edges(completed.stdout)
        self.assertEqual(len(table), 6)
        for name, entry in table.items():
            self.assertNotEqual(entry["match"], "ceil", name)
            self.assertEqual(entry["match"], "ceil+1", name)
            self.assertEqual(int(entry["measured"]),
                             int(entry["ceil"]) + 1, name)
        self.assertEqual(field(completed.stdout, "row0_white"), ["0"])

    def test_whole_scene_one_row_early_is_ceil_minus_one(self):
        # Negative case the old first-hit search missed: every edge sits one
        # capture row before ceil(1.25*y) and must be reported ceil-1.
        completed = run_cli(render_capture(extra_shift=-1))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        table = edges(completed.stdout)
        self.assertEqual(len(table), 6)
        for name, entry in table.items():
            self.assertEqual(entry["match"], "ceil-1", name)
            self.assertEqual(int(entry["measured"]),
                             int(entry["ceil"]) - 1, name)

    def test_one_edge_one_row_early_is_not_ceil(self):
        # Shifting only sq1's row0 moves just its top edge one row early; the
        # transition-based search must report ceil-1 there and ceil elsewhere.
        completed = run_cli(render_capture_one_edge_early())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        table = edges(completed.stdout)
        self.assertEqual(len(table), 6)
        self.assertEqual(table["sq1.top"]["match"], "ceil-1")
        self.assertEqual(table["sq1.top"]["measured"], "39")
        for name in ("top_band.bottom", "sq1.bottom", "sq3.top",
                     "sq3.bottom", "bottom_band.top"):
            self.assertEqual(table[name]["match"], "ceil", name)

    def test_frame_b_like_rows_zero_to_two_are_clear(self):
        completed = run_cli(render_capture(dy=3), "--dy", "3")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "white_runs"),
                         ["4-23,44-83,164-203,224-239"])
        for row in ("0", "1", "2"):
            self.assertEqual(field(completed.stdout, "row%s_white" % row),
                             ["0"])
            self.assertEqual(field(completed.stdout, "row%s_rgb" % row),
                             ["16,16,24"])
        for name, entry in edges(completed.stdout).items():
            self.assertEqual(entry["match"], "ceil", name)

    def test_cli_is_deterministic(self):
        image = render_capture()
        first = run_cli(image)
        second = run_cli(image)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)

    def test_bad_size_and_usage_exit_two(self):
        completed = run_cli(Image.new("RGB", (10, 10), CLEAR))
        self.assertEqual(completed.returncode, 2)
        completed = run_cli(render_capture(), "--nope")
        self.assertEqual(completed.returncode, 2)
        completed = run_cli(render_capture(), "--column", "400")
        self.assertEqual(completed.returncode, 2)


@unittest.skipUnless(HAVE_PIL, "PIL is not available; fixture tests skipped")
class HardwareFixtureTest(unittest.TestCase):
    """The committed real-hardware crops; every number is an EQ-4o FACT."""

    def test_fixture_sha256(self):
        for path, expected in ((FRAME_A, FRAME_A_SHA256),
                               (FRAME_B, FRAME_B_SHA256)):
            with open(path, "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 expected, path)

    def test_frame_a(self):
        completed = run_fixture(FRAME_A)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "white_runs"),
                         ["0-19,40-79,160-199,220-239"])
        self.assertEqual(field(completed.stdout, "row0_white"), ["1"])
        for row in ("0", "1", "2"):
            self.assertEqual(field(completed.stdout, "row%s_rgb" % row),
                             ["255,255,255"], row)
        self.assertEqual(field(completed.stdout, "row239_white"), ["1"])
        self.assertEqual(field(completed.stdout, "row0_wrap"), ["0"])
        self.assertEqual(field(completed.stdout,
                               "row120_first_white_col"), ["40"])
        self.assertEqual(field(completed.stdout,
                               "row120_last_white_col"), ["359"])
        self.assertEqual(field(completed.stdout, "row120_col39_rgb"),
                         ["16,16,24"])
        self.assertEqual(field(completed.stdout, "row120_col40_rgb"),
                         ["255,255,255"])
        self.assertEqual(field(completed.stdout, "row120_col359_rgb"),
                         ["255,255,255"])
        self.assertEqual(field(completed.stdout, "row120_col360_rgb"),
                         ["16,16,24"])
        expected = {
            "top_band.bottom": ("16", "20", "20"),
            "sq1.top": ("32", "40", "40"),
            "sq1.bottom": ("64", "80", "80"),
            "sq3.top": ("128", "160", "160"),
            "sq3.bottom": ("160", "200", "200"),
            "bottom_band.top": ("176", "220", "220"),
        }
        table = edges(completed.stdout)
        self.assertEqual(set(table), set(expected))
        for name, (y_ds, ceiling, measured) in expected.items():
            self.assertEqual(table[name]["y_ds"], y_ds, name)
            self.assertEqual(table[name]["ceil"], ceiling, name)
            self.assertEqual(table[name]["measured"], measured, name)
            self.assertEqual(table[name]["match"], "ceil", name)

    def test_frame_b_with_dy_three(self):
        completed = run_fixture(FRAME_B, "--dy", "3")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(field(completed.stdout, "white_runs"),
                         ["4-23,44-83,164-203,224-239"])
        self.assertEqual(field(completed.stdout, "row0_white"), ["0"])
        for row in ("0", "1", "2"):
            self.assertEqual(field(completed.stdout, "row%s_rgb" % row),
                             ["16,16,24"], row)
        self.assertEqual(field(completed.stdout, "row239_white"), ["1"])
        self.assertEqual(field(completed.stdout, "row0_wrap"), ["0"])
        self.assertEqual(field(completed.stdout,
                               "row120_first_white_col"), ["40"])
        self.assertEqual(field(completed.stdout,
                               "row120_last_white_col"), ["359"])
        expected = {
            "top_band.bottom": ("19", "24", "24"),
            "sq1.top": ("35", "44", "44"),
            "sq1.bottom": ("67", "84", "84"),
            "sq3.top": ("131", "164", "164"),
            "sq3.bottom": ("163", "204", "204"),
            "bottom_band.top": ("179", "224", "224"),
        }
        table = edges(completed.stdout)
        self.assertEqual(set(table), set(expected))
        for name, (y_ds, ceiling, measured) in expected.items():
            self.assertEqual(table[name]["y_ds"], y_ds, name)
            self.assertEqual(table[name]["ceil"], ceiling, name)
            self.assertEqual(table[name]["measured"], measured, name)
            self.assertEqual(table[name]["match"], "ceil", name)

    def test_fixture_runs_are_deterministic(self):
        first = run_fixture(FRAME_A)
        second = run_fixture(FRAME_A)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)


if __name__ == "__main__":
    unittest.main()
