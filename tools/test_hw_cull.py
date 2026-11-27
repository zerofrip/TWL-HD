# SPDX-License-Identifier: Apache-2.0
"""The host interpreter's culling rule against the EQ-4zn hardware capture.

The CULL probe frame (tools/hw/gen_cull_frames.py) is run through
tests/host/build/gx_interp_cli with and without --cull.  With --cull the set of
quads that produce triangles must equal the hardware result pinned in
tools/eq4_hw_probes/test_hw_probes_capture.py (and, when Pillow is available,
the classification of the committed capture); without it all eight quads are
emitted as before.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import gen_cull_frames as gen  # noqa: E402
import gen_gradcull_rom_data as rom  # noqa: E402

CLI = os.path.join(ROOT, "tests", "host", "build", "gx_interp_cli")
HARDWARE = ["DRAWN", "CULLED", "DRAWN", "CULLED",
            "CULLED", "DRAWN", "DRAWN", "CULLED"]

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False


def _run_cli(frame, *flags):
    proc = subprocess.run([CLI] + list(flags) + [frame],
                          capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stderr
    vertices = []
    stats = ""
    for line in proc.stdout.splitlines():
        parts = line.split()
        if line.startswith("stats "):
            stats = line
        elif len(parts) == 9 and parts[0].isdigit():
            vertices.append((float(parts[1]), float(parts[2])))
    return vertices, stats


def _states(vertices):
    """Which of the eight quads have triangles (by centroid)."""
    drawn = set()
    for start in range(0, len(vertices), 3):
        tri = vertices[start:start + 3]
        cx = sum(v[0] for v in tri) / 3.0
        cy = sum(v[1] for v in tri) / 3.0
        for index in range(8):
            corners = rom.cull_corners(index)
            xs = [c[0] / 4096.0 for c in corners]
            ys = [c[1] / 4096.0 for c in corners]
            if min(xs) <= cx <= max(xs) and min(ys) <= cy <= max(ys):
                drawn.add(index)
    return ["DRAWN" if i in drawn else "CULLED" for i in range(8)]


class CullAgainstHardware(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (os.path.isfile(CLI) and os.access(CLI, os.X_OK)):
            raise AssertionError(
                "gx_interp_cli binary is missing: %s (run `make test`)" % CLI)
        cls.tmp = tempfile.mkdtemp(prefix="twlhd-cull-")
        cls.addClassCleanup(shutil.rmtree, cls.tmp)
        cls.frame = os.path.join(cls.tmp, gen.FRAME_NAME)
        with open(cls.frame, "wb") as handle:
            handle.write(gen.frame_bytes())

    def test_interpreter_with_cull_equals_the_hardware_result(self):
        vertices, stats = _run_cli(self.frame, "--cull")
        self.assertEqual(_states(vertices), HARDWARE)
        self.assertEqual(len(vertices), 24)              # 4 quads x 6
        self.assertIn("culled_polygons=4 cull_undecided=0", stats)

    def test_interpreter_without_cull_draws_all_eight(self):
        # Negative test: the default build of the interpreter does not cull.
        vertices, stats = _run_cli(self.frame)
        self.assertEqual(_states(vertices), ["DRAWN"] * 8)
        self.assertNotEqual(_states(vertices), HARDWARE)
        self.assertEqual(len(vertices), 48)

    def test_swapped_windings_do_not_match_the_hardware(self):
        # Negative test: the hardware result with the windings exchanged.
        swapped = HARDWARE[4:] + HARDWARE[:4]
        vertices, _stats = _run_cli(self.frame, "--cull")
        self.assertNotEqual(_states(vertices), swapped)

    @unittest.skipUnless(HAVE_PIL, "Pillow not installed")
    def test_capture_classification_equals_the_interpreter(self):
        import measure_cull
        from check_tex_shot import parse_ppm
        fixture = os.path.join(TOOLS_DIR, "fixtures", "eq4_hw_probes",
                               "hw_cull_top400x240.png")
        ppm = os.path.join(self.tmp, "cull.ppm")
        img = Image.open(fixture).convert("RGB")
        with open(ppm, "wb") as handle:
            handle.write(b"P6\n400 240\n255\n" + img.tobytes())
        width, _height, data = parse_ppm(ppm)
        captured = [row[4] for row in measure_cull.measure(data, width)]
        vertices, _stats = _run_cli(self.frame, "--cull")
        self.assertEqual(captured, _states(vertices))


if __name__ == "__main__":
    unittest.main()
