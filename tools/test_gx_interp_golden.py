#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden test: run the C GX interpreter CLI on the real DeSmuME capture and
compare against the independent Python oracle.
"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest

import gx_interp_oracle


FIXTURE = os.path.join(
    os.path.dirname(__file__), "..", "tests", "host", "fixtures",
    "demo_frame_desmume_v2.bin"
)
CLI = os.path.join(
    os.path.dirname(__file__), "..", "tests", "host", "build", "gx_interp_cli"
)

# v2 command ids (common/twlhd_v2_format.h) used by the alpha streams.
CMD_VTX_16 = 0x23
CMD_POLYGON_ATTR = 0x29
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_SWAP_BUFFERS = 0x50

GXCM = int.from_bytes(b"GXCM", "little")
META = int.from_bytes(b"META", "little")

CLIP_TOL = 0.02
# Tolerance explanation: the oracle uses double-precision Python math, while
# the DS/libnds build the perspective, look-at and rotation matrices from
# 20.12 fixed-point words. The fixed-point -> float conversion of those
# trigonometric and fractional constants introduces small rounding differences.
# The observed maximum clip-space deviation is ~0.0073, so 0.02 is a safe
# upper bound.


def _vtx16(x, y):
    """Two GXCM entries for one VTX_16 command (x | y << 16, z = 0)."""
    return [
        (CMD_VTX_16, ((y & 0xFFFF) << 16) | (x & 0xFFFF)),
        (CMD_VTX_16, 0),
    ]


def _triangle_group(alpha=None):
    """One BEGIN..END triangle list carrying 3 VTX_16 vertices."""
    entries = []
    if alpha is not None:
        entries.append((CMD_POLYGON_ATTR, alpha << 16))
    entries.append((CMD_BEGIN_VTXS, 0))
    for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
        entries.extend(_vtx16(x, y))
    entries.append((CMD_END_VTXS, 0))
    return entries


def build_v2(entries):
    """Minimal parser-clean v2 file: 32-byte header, GXCM, META(16 bytes)."""
    gxcm = b"".join(struct.pack("<II", cmd, value) for cmd, value in entries)
    total = 32 + 16 + len(gxcm) + 16 + 16
    header = struct.pack("<8I", 2, 32, total, 1, 1, 2, 0, 0)
    gxcm_chunk = struct.pack("<4I", GXCM, len(gxcm), 1, 0) + gxcm
    meta_chunk = struct.pack("<4I", META, 16, 0, 0) + struct.pack(
        "<4I", 2, 0, 0, 0
    )
    return header + gxcm_chunk + meta_chunk


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

    def _run_cli(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "alpha_stream_v2.bin")
            with open(path, "wb") as handle:
                handle.write(data)
            result = subprocess.run(
                [CLI, path], capture_output=True, text=True, check=True
            )
        lines = result.stdout.strip().splitlines()
        self.assertEqual(lines[0], "result=OK")
        vertices = []
        for line in lines[2:]:
            if line.startswith("stats "):
                break
            parts = line.split()
            self.assertEqual(len(parts), 9)
            vertices.append([float(x) for x in parts[1:]])
        return vertices

    def test_oracle_alpha_table(self):
        # Hand-written table, independent of the oracle implementation: 1..31
        # is (alpha + 1) / 32 and 0 is the DS wireframe value.
        expected = {
            0: 0.0,
            1: 0.0625,
            4: 0.15625,
            16: 0.53125,
            31: 1.0,
        }
        for alpha, value in expected.items():
            self.assertEqual(gx_interp_oracle.alpha_to_float(alpha), value)

    def test_alpha_streams(self):
        for alpha in (31, 16, 4, 0):
            entries = _triangle_group(alpha)
            entries.append((CMD_SWAP_BUFFERS, 0))
            vertices = self._run_cli(build_v2(entries))
            self.assertEqual(len(vertices), 3)
            expected = gx_interp_oracle.alpha_to_float(alpha)
            for vertex in vertices:
                self.assertEqual(vertex[7], expected)

    def test_alpha_pending_until_begin(self):
        entries = [(CMD_POLYGON_ATTR, 31 << 16), (CMD_BEGIN_VTXS, 0)]
        for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
            entries.extend(_vtx16(x, y))
        # Inside BEGIN..END: applies only at the NEXT BEGIN.
        entries.append((CMD_POLYGON_ATTR, 4 << 16))
        for x, y in ((0x1000, 0x1000), (0x800, 0x800), (0x400, 0x400)):
            entries.extend(_vtx16(x, y))
        entries.append((CMD_END_VTXS, 0))
        entries.append((CMD_BEGIN_VTXS, 0))
        for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
            entries.extend(_vtx16(x, y))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])

        vertices = self._run_cli(build_v2(entries))
        self.assertEqual(len(vertices), 9)
        for vertex in vertices[:6]:
            self.assertEqual(vertex[7], 1.0)
        for vertex in vertices[6:]:
            self.assertEqual(vertex[7], gx_interp_oracle.alpha_to_float(4))

    def test_alpha_no_attr_stream(self):
        entries = _triangle_group()
        entries.append((CMD_SWAP_BUFFERS, 0))
        vertices = self._run_cli(build_v2(entries))
        self.assertEqual(len(vertices), 3)
        for vertex in vertices:
            self.assertEqual(vertex[7], 1.0)


if __name__ == "__main__":
    unittest.main()
