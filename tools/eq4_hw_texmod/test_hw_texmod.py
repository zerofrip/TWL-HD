#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay the EQ-4zk real-hardware MODULATE capture from the fixture.

Runs tools/hw/measure_texmod.py on the committed capture and pins the
per-channel measured values, the capture-level matches of the two models and
the maximum deviations.
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
MEASURE = os.path.join(TOOLS, "hw", "measure_texmod.py")
FIXTURE = os.path.join(TOOLS, "fixtures", "eq4_hw_texmod",
                       "hw_texmod_top400x240.png")
SHA256 = "67208da71eaf165fa9424b42e3289e4f4c915f63420862e6a9eae824364c77e3"

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

# quad, channel -> measured 8-bit value (24 channels, committed capture).
MEASURED = {
    (0, "R"): 255, (0, "G"): 134, (0, "B"): 66,
    (1, "R"): 74, (1, "G"): 81, (1, "B"): 57,
    (2, "R"): 107, (2, "G"): 93, (2, "B"): 41,
    (3, "R"): 24, (3, "G"): 36, (3, "B"): 214,
    (4, "R"): 24, (4, "G"): 28, (4, "B"): 132,
    (5, "R"): 66, (5, "G"): 85, (5, "B"): 82,
    (6, "R"): 8, (6, "G"): 243, (6, "B"): 0,
    (7, "R"): 41, (7, "G"): 69, (7, "B"): 66,
}
ROW = re.compile(r"^\s*(\d)\s+([RGB])\s+\d+\s+\d+\s+(\d+)\s")


def _run(ppm_bytes):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "top.ppm")
        with open(path, "wb") as handle:
            handle.write(ppm_bytes)
        proc = subprocess.run([sys.executable, "-B", "-I", MEASURE, path],
                              capture_output=True, text=True, check=False)
    return proc.returncode, proc.stdout


def _fixture_ppm():
    img = Image.open(FIXTURE).convert("RGB")
    return b"P6\n400 240\n255\n" + img.tobytes()


class FixtureHash(unittest.TestCase):
    def test_hash(self):
        with open(FIXTURE, "rb") as handle:
            self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                             SHA256)


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class HardwareTexmod(unittest.TestCase):
    def test_measured_values_are_pinned(self):
        code, out = _run(_fixture_ppm())
        self.assertEqual(code, 0, out)
        got = {}
        for line in out.splitlines():
            match = ROW.match(line)
            if match:
                got[(int(match.group(1)), match.group(2))] = \
                    int(match.group(3))
        self.assertEqual(got, MEASURED)

    def test_ds6_matches_every_channel_and_pica_does_not(self):
        code, out = _run(_fixture_ppm())
        self.assertEqual(code, 0, out)
        lines = out.strip().splitlines()
        self.assertEqual(lines[-2],
                         "capture-level exact matches of 24: "
                         "pica=11 ds6=24")
        self.assertEqual(lines[-1], "max deviation: pica=6 ds6=4")

    def test_blank_capture_does_not_match(self):
        # Negative test: an all-black screen matches neither model.
        code, out = _run(b"P6\n400 240\n255\n" + bytes(400 * 240 * 3))
        self.assertEqual(code, 0, out)
        line = out.strip().splitlines()[-2]
        self.assertNotIn("ds6=24", line)


if __name__ == "__main__":
    unittest.main()
