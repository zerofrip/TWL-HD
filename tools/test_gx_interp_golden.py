#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden test: run the C GX interpreter CLI on the real DeSmuME capture and
compare against the independent Python oracle.
"""

import os
import subprocess
import sys
import unittest

import gx_interp_oracle


FIXTURE = os.path.join(
    os.path.dirname(__file__), "..", "tests", "host", "fixtures",
    "demo_frame_desmume_v2.bin"
)
CLI = os.path.join(
    os.path.dirname(__file__), "..", "tests", "host", "build", "gx_interp_cli"
)

CLIP_TOL = 0.02
# Tolerance explanation: the oracle uses double-precision Python math, while
# the DS/libnds build the perspective, look-at and rotation matrices from
# 20.12 fixed-point words. The fixed-point -> float conversion of those
# trigonometric and fractional constants introduces small rounding differences.
# The observed maximum clip-space deviation is ~0.0073, so 0.02 is a safe
# upper bound.


class GxInterpGoldenTest(unittest.TestCase):

    def test_cli_exists(self):
        self.assertTrue(
            os.path.isfile(CLI) and os.access(CLI, os.X_OK),
            f"gx_interp_cli binary is missing: {CLI}"
        )

    def test_golden_fixture(self):
        self.assertTrue(os.path.isfile(FIXTURE), f"fixture missing: {FIXTURE}")
        self.assertTrue(os.access(CLI, os.X_OK), f"CLI missing: {CLI}")

        result = subprocess.run(
            [CLI, FIXTURE],
            capture_output=True,
            text=True,
            check=True,
        )

        lines = result.stdout.strip().splitlines()
        self.assertTrue(lines, "CLI produced no output")
        self.assertTrue(lines[0].startswith("result="))
        self.assertEqual(lines[0], "result=OK")

        viewport_line = lines[1]
        self.assertTrue(viewport_line.startswith("viewport "))
        # The demo viewport is x1=0, y1=0, x2=255, y2=191.
        self.assertEqual(viewport_line, "viewport 0 0 255 191 1")

        # Parse CLI vertices.
        cli_verts = []
        stats_line = None
        for line in lines[2:]:
            if line.startswith("stats "):
                stats_line = line
                break
            parts = line.split()
            self.assertEqual(len(parts), 9)
            cli_verts.append([float(x) for x in parts[1:]])

        self.assertIsNotNone(stats_line, "stats line missing")
        self.assertEqual(len(cli_verts), 36,
                         "expected 36 output vertices (6 quads -> 12 triangles)")
        print(f"vertex_count={len(cli_verts)}")

        expected = gx_interp_oracle.expected_vertices()
        self.assertEqual(len(expected), 36)

        max_dev = 0.0
        for i, (cv, ev) in enumerate(zip(cli_verts, expected)):
            # Compare clip coordinates.
            for j in range(4):
                dev = abs(cv[j] - ev["clip"][j])
                max_dev = max(max_dev, dev)
                self.assertLessEqual(
                    dev, CLIP_TOL,
                    f"vertex {i} clip[{j}] deviation {dev}: "
                    f"got {cv[j]}, expected {ev['clip'][j]}"
                )
            # Compare colours exactly (RGB555 / 31).
            for j in range(4):
                self.assertAlmostEqual(
                    cv[4 + j], ev["rgba"][j], places=6,
                    msg=f"vertex {i} rgba[{j}]"
                )

        # Report the observed max deviation in the test output.
        print(f"max_clip_deviation={max_dev:.6f}")
        print("colour_exactness=PASS")

        # Sanity-check stats.
        self.assertIn("triangles_out=12", stats_line)
        self.assertIn("quads_split=6", stats_line)
        self.assertIn("vertices_in=24", stats_line)
        self.assertIn("dropped_incomplete=0", stats_line)
        self.assertIn("stack_errors=0", stats_line)
        self.assertIn("geometry_incomplete=0", stats_line)


if __name__ == "__main__":
    unittest.main()
