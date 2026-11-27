#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay the EQ-4zi real-hardware lighting measurement from committed fixtures.

Each fixture is the top 400x240 crop of one capture of the DS ROM built with
LIT=1 or LIT=2 (see docs/reports/eq4zh-lit-rom.md).  The expected values are
the host interpreter's prediction (tools/hw/predict_lit.py) for the matching
frame.  The tests run tools/hw/compare_ppm.py on the fixtures: they must pass
at the documented tolerance 12, fail at tolerance 5 (the diffuse quads are
brighter on hardware by one 5-bit level), and the measured values are pinned.
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
COMPARE = os.path.join(HERE, os.pardir, "hw", "compare_ppm.py")
FIX = os.path.join(HERE, os.pardir, "fixtures", "eq4_hw_lit")

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

SHA256 = {
    "hw_lit1_a_top400x240.png":
        "e84b3f1bb4d4dcac3497ef7d7936fe25d217bb773940ba9b2da3f3f6c4a2ddc7",
    "hw_lit2_a_top400x240.png":
        "dcd5a85e84ef5b28f8b0a9b7f240e91dfc515181748c1d93fc62d33a3c4a596c",
    "expected_lit_a.json":
        "5d28bc4b8f9fd6e0e6a1684cee19e35390564258684ad8a5b45fdfb4429476d7",
    "expected_lit_b.json":
        "ac25bf665bc5c57ad3d1b2776c6f366344c0b3fa365481eebb08a01c82613601",
}

# patch -> (measured RGB, expected RGB), from the committed fixtures.
LIT1 = {
    "quad 0": ((206, 207, 206), (197, 197, 197)),
    "quad 1": ((115, 117, 115), (107, 107, 107)),
    "quad 2": ((24, 28, 24), (25, 25, 25)),
    "quad 3": ((24, 28, 24), (25, 25, 25)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
LIT2 = {
    "quad 0": ((206, 0, 0), (197, 0, 0)),
    "quad 1": ((33, 231, 33), (33, 230, 33)),
    "quad 2": ((239, 239, 239), (239, 239, 239)),
    "quad 3": ((173, 0, 148), (173, 0, 148)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
ROW = re.compile(r"^(quad \d|background)\s+\((\d+)\s*,(\d+)\s*,(\d+)\s*\)"
                 r"\s+\((\d+)\s*,(\d+)\s*,(\d+)\s*\)")


def _fix(name):
    return os.path.join(FIX, name)


def _compare(png, expected, tol):
    """Run compare_ppm.py; return (exit code, stdout)."""
    with tempfile.TemporaryDirectory() as tmp:
        ppm = os.path.join(tmp, "top.ppm")
        img = Image.open(_fix(png)).convert("RGB")
        with open(ppm, "wb") as handle:
            handle.write(b"P6\n400 240\n255\n" + img.tobytes())
        proc = subprocess.run(
            [sys.executable, "-B", "-I", COMPARE, ppm, _fix(expected),
             "--tol", str(tol)],
            capture_output=True, text=True, check=False)
    return proc.returncode, proc.stdout


def _rows(out):
    rows = {}
    for line in out.splitlines():
        match = ROW.match(line)
        if match:
            vals = tuple(int(v) for v in match.groups()[1:])
            rows[match.group(1)] = (vals[:3], vals[3:])
    return rows


class FixtureHashes(unittest.TestCase):
    def test_hashes(self):
        for name, want in SHA256.items():
            with open(_fix(name), "rb") as handle:
                got = hashlib.sha256(handle.read()).hexdigest()
            self.assertEqual(got, want, name)

    def test_no_home_path(self):
        for name in ("expected_lit_a.json", "expected_lit_b.json"):
            with open(_fix(name), encoding="utf-8") as handle:
                self.assertNotIn("/home/", handle.read(), name)


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class HardwareLit(unittest.TestCase):
    def test_lit1_within_tolerance_12(self):
        code, out = _compare("hw_lit1_a_top400x240.png",
                             "expected_lit_a.json", 12)
        self.assertEqual(code, 0, out)
        self.assertEqual(_rows(out), LIT1)

    def test_lit2_within_tolerance_12(self):
        code, out = _compare("hw_lit2_a_top400x240.png",
                             "expected_lit_b.json", 12)
        self.assertEqual(code, 0, out)
        self.assertEqual(_rows(out), LIT2)

    def test_tolerance_5_fails_on_diffuse_quads(self):
        # Negative test: the diffuse quads are 8..10 levels brighter on
        # hardware, so a tight tolerance must be rejected (exit 1).
        for png, exp in (("hw_lit1_a_top400x240.png", "expected_lit_a.json"),
                         ("hw_lit2_a_top400x240.png", "expected_lit_b.json")):
            code, out = _compare(png, exp, 5)
            self.assertEqual(code, 1, out)

    def test_swapped_expectation_fails(self):
        # Negative test: lit1 capture against the lit2 expectation.
        code, out = _compare("hw_lit1_a_top400x240.png",
                             "expected_lit_b.json", 12)
        self.assertEqual(code, 1, out)

    def test_diffuse_offset_is_one_five_bit_level(self):
        # Measured minus expected on the diffuse-lit red channel is 8..10
        # levels, i.e. one 5-bit step (8.2 levels), never zero or two steps.
        for rows, quads in ((LIT1, ("quad 0", "quad 1")), (LIT2, ("quad 0",))):
            for quad in quads:
                meas, exp = rows[quad]
                self.assertIn(meas[0] - exp[0], (8, 9, 10), quad)


if __name__ == "__main__":
    unittest.main()
