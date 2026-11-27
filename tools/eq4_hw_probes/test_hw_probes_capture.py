#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Replay the EQ-4zl/m/n real-hardware captures (texture behaviour, Gouraud,
culling) from the committed fixtures and pin the numbers of
docs/reports/eq4zl-zm-zn-probes-hw-result.md."""

import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, os.pardir)
FIX = os.path.join(TOOLS, "fixtures", "eq4_hw_probes")

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

SHA256 = {
    "hw_texbeh_top400x240.png":
        "c044a14fbc1fc893f9d45985084e41031eeddc215bf028aaddc499673c6ffacb",
    "hw_grad_top400x240.png":
        "4c46a7d259ce3b78a11ac79f186ea42423a0889fb2edbce88b1f3f381bac7c34",
    "hw_cull_top400x240.png":
        "a19f7730ce49ecf0167d96d2db29580950c9b7c47ae58964a6cfd274e1179915",
}

# quad -> (cells matched, total, worst deviation) at tolerance 28.
TEXBEH = {0: (64, 64, 2), 1: (64, 64, 0), 2: (64, 64, 3), 3: (64, 64, 0),
          4: (248, 256, 69), 5: (253, 256, 45), 6: (254, 256, 56),
          7: (256, 256, 8)}
CULL = ["DRAWN", "CULLED", "DRAWN", "CULLED",
        "CULLED", "DRAWN", "DRAWN", "CULLED"]
BEH_LINE = re.compile(r"^quad (\d) role=.* cells=(\d+)/(\d+) worst=(\d+) ")


def _ppm(name, tmp):
    path = os.path.join(tmp, name + ".ppm")
    img = Image.open(os.path.join(FIX, name)).convert("RGB")
    with open(path, "wb") as handle:
        handle.write(b"P6\n400 240\n255\n" + img.tobytes())
    return path


def _run(script, name, *extra):
    with tempfile.TemporaryDirectory() as tmp:
        return subprocess.run(
            [sys.executable, "-B", "-I", script, _ppm(name, tmp)] +
            list(extra), capture_output=True, text=True, check=False)


class FixtureHashes(unittest.TestCase):
    def test_hashes(self):
        for name, want in SHA256.items():
            with open(os.path.join(FIX, name), "rb") as handle:
                self.assertEqual(hashlib.sha256(handle.read()).hexdigest(),
                                 want, name)


@unittest.skipUnless(HAVE_PIL, "Pillow not installed")
class HardwareProbes(unittest.TestCase):
    def test_texture_behaviour_numbers_are_pinned(self):
        proc = _run(os.path.join(TOOLS, "check_tex_behaviour.py"),
                    "hw_texbeh_top400x240.png", "--clear", "0,0,0")
        self.assertEqual(proc.returncode, 1, proc.stdout)  # wrap seams
        got = {}
        for line in proc.stdout.splitlines():
            match = BEH_LINE.match(line)
            if match:
                got[int(match.group(1))] = tuple(
                    int(match.group(i)) for i in (2, 3, 4))
        self.assertEqual(got, TEXBEH)

    def test_texture_behaviour_default_clear_changes_the_numbers(self):
        # Negative test: read with the old (16,16,24) clear colour the
        # translucent quads' expectation moves (worst 24, not 2 / 0).
        proc = _run(os.path.join(TOOLS, "check_tex_behaviour.py"),
                    "hw_texbeh_top400x240.png")
        got = {}
        for line in proc.stdout.splitlines():
            match = BEH_LINE.match(line)
            if match:
                got[int(match.group(1))] = int(match.group(4))
        self.assertEqual((got[0], got[1]), (24, 24))
        self.assertNotEqual((got[0], got[1]), (TEXBEH[0][2], TEXBEH[1][2]))

    def test_gouraud_within_tolerance_12_and_not_4(self):
        script = os.path.join(TOOLS, "hw", "measure_grad.py")
        proc = _run(script, "hw_grad_top400x240.png")
        self.assertEqual(proc.returncode, 0, proc.stdout)
        self.assertIn("max deviation: 5.3 (tolerance 12)", proc.stdout)
        self.assertIn("result=PASS", proc.stdout)
        rows = [line for line in proc.stdout.splitlines()
                if re.match(r"^\s+\d \d,\d,\d", line)]
        self.assertEqual(len(rows), 20)
        # Negative test: tolerance 4 is below the measured 5.3.
        tight = _run(script, "hw_grad_top400x240.png", "--tol", "4")
        self.assertEqual(tight.returncode, 1, tight.stdout)

    def test_culling_classification_is_pinned(self):
        script = os.path.join(TOOLS, "hw", "measure_cull.py")
        proc = _run(script, "hw_cull_top400x240.png")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        states = [line.split()[-1] for line in proc.stdout.splitlines()[1:]]
        self.assertEqual(states, CULL)

    def test_renderer_that_ignores_the_bits_would_differ(self):
        # The arm11 renderer draws every polygon regardless of POLYGON_ATTR
        # bits 6-7 (docs/reports/eq4ze-hw-coverage.md F2): all eight DRAWN.
        # The hardware culls four of the eight, so the two must differ.
        self.assertNotEqual(CULL, ["DRAWN"] * 8)
        self.assertEqual(CULL.count("CULLED"), 4)

    def test_grad_capture_is_not_a_cull_capture(self):
        # Negative test: the Gouraud capture classified as culling is not the
        # pinned pattern.
        script = os.path.join(TOOLS, "hw", "measure_cull.py")
        proc = _run(script, "hw_grad_top400x240.png")
        states = [line.split()[-1] for line in proc.stdout.splitlines()[1:]]
        self.assertNotEqual(states, CULL)


if __name__ == "__main__":
    unittest.main()
