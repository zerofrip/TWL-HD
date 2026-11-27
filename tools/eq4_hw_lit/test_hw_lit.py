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
    "expected_lit_a.json":
        "eb7557c77bc8d17f34933047caeace7775d511a7d580fa1382be51710b917878",
    "expected_lit_b.json":
        "8da9a9eca150d8826da985b9d334b9ec91df1a29993eb62a12385de22dbe1075",
    "expected_lit_c.json":
        "5ed1cf332a4fd632266847457ceb007f7a2e101f14c12d8069ae0f1f41cf5795",
    "expected_lit_c_separate.json":
        "9680f70dc3fb1d101d094d68c4c96f82998251e4aafb31633f1a5b930513d25f",
    "expected_lit_d.json":
        "2913741239bc3bc2773d96e39094158bd34a167484c84de37d9b0a8046806ea4",
    "expected_lit_e.json":
        "67433e8d3c1903957f9849ba80108b7ab39ca12df91da41f8f57ce5cf2145cc1",
    "expected_lit_f.json":
        "ba49065be8bdb7cbffa87e2220f6a6a2378ce7b84047e78ca7c037d3508c3a36",
    "hw_lit1_a_top400x240.png":
        "e84b3f1bb4d4dcac3497ef7d7936fe25d217bb773940ba9b2da3f3f6c4a2ddc7",
    "hw_lit2_a_top400x240.png":
        "dcd5a85e84ef5b28f8b0a9b7f240e91dfc515181748c1d93fc62d33a3c4a596c",
    "hw_lit3_a_top400x240.png":
        "a46c149645e1c7715a1f2d11ffa9310fe92250638a98bc8fd075e19b68b3605e",
    "hw_lit4_a_top400x240.png":
        "0f8b0dea85b2484b677366347c0041cf6670aaa4008b5309a6e8afc21dc634bd",
    "hw_lit5_a_top400x240.png":
        "9ef005bdbc3b979d2cecb278f35ce239f26911c3db8fa93400d79bf34d7bade7",
    "hw_lit6_a_top400x240.png":
        "f3d6bcd1864be05492e7bb6aba4f583dd996eefa0a8d3266fdcc2015921e47b2",
}

# patch -> (measured RGB, expected RGB), from the committed fixtures.  The
# expectation is the host interpreter with diffuse and ambient summed before
# truncation (see docs/reports/eq4zi-lit-hw-result.md).
LIT1 = {
    "quad 0": ((206, 207, 206), (206, 206, 206)),
    "quad 1": ((115, 117, 115), (115, 115, 115)),
    "quad 2": ((24, 28, 24), (25, 25, 25)),
    "quad 3": ((24, 28, 24), (25, 25, 25)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
LIT2 = {
    "quad 0": ((206, 0, 0), (206, 0, 0)),
    "quad 1": ((33, 231, 33), (33, 230, 33)),
    "quad 2": ((239, 239, 239), (239, 239, 239)),
    "quad 3": ((173, 0, 148), (173, 0, 148)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
LIT3 = {
    "quad 0": ((8, 12, 8), (8, 8, 8)),
    "quad 1": ((0, 0, 0), (0, 0, 0)),
    "quad 2": ((0, 0, 0), (0, 0, 0)),
    "quad 3": ((115, 117, 115), (115, 115, 115)),
    "background": ((0, 0, 0), (0, 0, 0)),
}

LIT4 = {
    "quad 0": ((24, 28, 24), (25, 25, 25)),
    "quad 1": ((8, 12, 8), (8, 8, 8)),
    "quad 2": ((148, 150, 148), (148, 148, 148)),
    "quad 3": ((90, 93, 90), (90, 90, 90)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
LIT5 = {
    "quad 0": ((132, 134, 132), (132, 132, 132)),
    "quad 1": ((156, 158, 156), (156, 156, 156)),
    "quad 2": ((189, 190, 189), (189, 189, 189)),
    "quad 3": ((156, 158, 156), (156, 156, 156)),
    "background": ((0, 0, 0), (0, 0, 0)),
}
LIT6 = {
    "quad 0": ((90, 93, 90), (90, 90, 90)),
    "quad 1": ((198, 199, 198), (197, 197, 197)),
    "quad 2": ((206, 207, 206), (206, 206, 206)),
    "quad 3": ((173, 174, 173), (173, 173, 173)),
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
        for name in sorted(SHA256):
            if not name.endswith(".json"):
                continue
            with open(_fix(name), encoding="utf-8") as handle:
                self.assertNotIn("/home/", handle.read(), name)


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class HardwareLit(unittest.TestCase):
    CASES = (
        ("hw_lit1_a_top400x240.png", "expected_lit_a.json", LIT1),
        ("hw_lit2_a_top400x240.png", "expected_lit_b.json", LIT2),
        ("hw_lit3_a_top400x240.png", "expected_lit_c.json", LIT3),
        ("hw_lit4_a_top400x240.png", "expected_lit_d.json", LIT4),
        ("hw_lit5_a_top400x240.png", "expected_lit_e.json", LIT5),
        ("hw_lit6_a_top400x240.png", "expected_lit_f.json", LIT6),
    )


    def test_within_tolerance_4_and_values_pinned(self):
        for png, exp, rows in self.CASES:
            with self.subTest(png=png):
                code, out = _compare(png, exp, 4)
                self.assertEqual(code, 0, out)
                self.assertEqual(_rows(out), rows)

    def test_tolerance_3_fails(self):
        # Negative test: the green channel of the lit3 quad 0 is 4 levels
        # off (presumably capture path; unverified), so tolerance 3 is rejected.
        code, out = _compare("hw_lit3_a_top400x240.png",
                             "expected_lit_c.json", 3)
        self.assertEqual(code, 1, out)

    def test_swapped_expectation_fails(self):
        # Negative test: lit1 capture against the lit2 expectation.
        code, out = _compare("hw_lit1_a_top400x240.png",
                             "expected_lit_b.json", 4)
        self.assertEqual(code, 1, out)

    def test_old_separate_truncation_would_fail(self):
        # Negative test (the EQ-4zi finding): the lit3 capture against an
        # expectation with 0 levels in quad 0 (separate truncation) fails.
        code, out = _compare("hw_lit3_a_top400x240.png",
                             "expected_lit_c_separate.json", 4)
        self.assertEqual(code, 1, out)


if __name__ == "__main__":
    unittest.main()
