#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay the EQ-4zj real-hardware texture-format capture from the fixture.

The fixture is the top 400x240 crop of one capture of the DS ROM built with
TEXFMT=1 (see docs/reports/eq4zj-texfmt-hw-result.md).  The tests run the
repository checker tools/check_tex_formats.py on it (clear colour 0,0,0) and
pin the per-quad numbers; the 16x16 I4 quad has one cell above the default
tolerance, which is pinned together with a nearest-colour classification of
all 256 cells.
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, os.pardir)
CHECKER = os.path.join(TOOLS, "check_tex_formats.py")
FIXTURE = os.path.join(TOOLS, "fixtures", "eq4_hw_texfmt",
                       "hw_texfmt_top400x240.png")
SHA256 = "0f02cc8b8125dfd177633209050c52c111f13054bf68041d9c5080c97c1ad39f"
sys.path.insert(0, os.path.abspath(TOOLS))

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

# quad -> (format, matched cells, worst deviation), from the committed fixture.
EXPECTED = {
    0: ("A3I5", 64, 3), 1: ("I2", 64, 3), 2: ("I4", 64, 3),
    3: ("I8", 64, 2), 4: ("4x4", 64, 7), 5: ("A5I3", 64, 3),
    6: ("16bpp", 64, 3), 7: ("I4", 255, 35),
}
LINE = re.compile(r"^quad (\d) fmt=\d (\S+) \d+x\d+ cells=(\d+)/(\d+) "
                  r"worst=(\d+) ")


def _ppm(tmp):
    path = os.path.join(tmp, "top.ppm")
    img = Image.open(FIXTURE).convert("RGB")
    with open(path, "wb") as handle:
        handle.write(b"P6\n400 240\n255\n" + img.tobytes())
    return path


def _run(*extra):
    with tempfile.TemporaryDirectory() as tmp:
        proc = subprocess.run(
            [sys.executable, "-B", "-I", CHECKER, _ppm(tmp), "--clear",
             "0,0,0"] + list(extra),
            capture_output=True, text=True, check=False)
    return proc.returncode, proc.stdout


def _rows(out):
    rows = {}
    for line in out.splitlines():
        match = LINE.match(line)
        if match:
            rows[int(match.group(1))] = (match.group(2),
                                         int(match.group(3)),
                                         int(match.group(5)))
    return rows


class FixtureHash(unittest.TestCase):
    def test_hash(self):
        with open(FIXTURE, "rb") as handle:
            self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                             SHA256)


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class HardwareTexfmt(unittest.TestCase):
    def test_default_tolerance_numbers_are_pinned(self):
        code, out = _run()
        self.assertEqual(code, 1, out)  # quad 7 has one cell above 28
        self.assertEqual(_rows(out), EXPECTED)

    def test_seven_quads_pass_at_default_tolerance(self):
        code, out = _run()
        ok = [line for line in out.splitlines() if line.endswith(" OK")]
        self.assertEqual(len(ok), 7, out)

    def test_all_quads_pass_at_tolerance_36(self):
        code, out = _run("--tolerance", "36")
        self.assertEqual(code, 0, out)

    def test_tight_tolerance_fails(self):
        # Negative test: tolerance 2 is below the measured worst of 3.
        code, out = _run("--tolerance", "2")
        self.assertEqual(code, 1, out)

    def test_default_clear_colour_rejects_the_capture(self):
        # Negative test: the black-clear capture read as a (16,16,24)-clear
        # one makes the quad bounding boxes wrong, so it must not pass.
        with tempfile.TemporaryDirectory() as tmp:
            proc = subprocess.run(
                [sys.executable, "-B", "-I", CHECKER, _ppm(tmp)],
                capture_output=True, text=True, check=False)
        self.assertEqual(proc.returncode, 1, proc.stdout)

    def test_wrong_quad_expectation_fails(self):
        # Negative test: quad 2 (I4) measured against quad 3's (I8) pixels.
        import check_tex_formats as checker
        import gen_tex_format_frames as gen
        from check_tex_shot import parse_ppm
        checker.CLEAR = (0, 0, 0)
        with tempfile.TemporaryDirectory() as tmp:
            width, _height, data = parse_ppm(_ppm(tmp))
        wrong = gen.expected_pixels(gen.QUAD_SPECS[3])
        result = checker.sample_quad(data, width, 240, gen.QUAD_SPECS[2],
                                     wrong, 28)
        self.assertLess(result[3], 64)

    def test_i4_16x16_cells_all_classify_to_their_colour(self):
        # The one cell above the tolerance is a sampling-position artefact:
        # every one of the 256 cells is nearest to its own expected colour.
        import check_tex_formats as checker
        import gen_tex_format_frames as gen
        from check_tex_shot import parse_ppm
        checker.CLEAR = (0, 0, 0)
        with tempfile.TemporaryDirectory() as tmp:
            width, _height, data = parse_ppm(_ppm(tmp))
        spec = gen.QUAD_SPECS[7]
        expected = gen.expected_pixels(spec)
        palette = sorted(set(p[:3] for p in expected))
        cells = checker.sample_quad(data, width, 240, spec, expected, 28)[5]
        self.assertEqual(len(cells), 256)
        self.assertEqual(len(palette), 15)
        for tx, ty, want, got, _dev in cells:
            near = min(palette, key=lambda p: max(abs(p[i] - got[i])
                                                  for i in range(3)))
            self.assertEqual(near, tuple(want), (tx, ty))


if __name__ == "__main__":
    unittest.main()
