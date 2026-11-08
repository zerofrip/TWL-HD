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
CMD_MTX_MODE = 0x10
CMD_MTX_PUSH = 0x11
CMD_MTX_POP = 0x12
CMD_MTX_IDENTITY = 0x15
CMD_MTX_LOAD_4x4 = 0x16
CMD_MTX_MULT_4x4 = 0x18
CMD_MTX_SCALE = 0x1B
CMD_MTX_TRANS = 0x1C
CMD_COLOR = 0x20
CMD_NORMAL = 0x21
CMD_VTX_16 = 0x23
CMD_POLYGON_ATTR = 0x29
CMD_DIF_AMB = 0x30
CMD_SPE_EMI = 0x31
CMD_LIGHT_VECTOR = 0x32
CMD_LIGHT_COLOR = 0x33
CMD_SHININESS = 0x34
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


def _u32(value):
    return value & 0xFFFFFFFF


def _rgb555(r, g, b):
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10)


def _component10(value_20_12):
    """20.12 component -> 10-bit field (9 fractional bits, i.e. /8)."""
    return (value_20_12 // 8) & 0x3FF


def _vector_word(x_20_12, y_20_12, z_20_12, light=0):
    return (
        _component10(x_20_12)
        | (_component10(y_20_12) << 10)
        | (_component10(z_20_12) << 20)
        | ((light & 3) << 30)
    )


def _normal_word(x_20_12, y_20_12, z_20_12):
    return _vector_word(x_20_12, y_20_12, z_20_12, 0)


def _light_colour(r, g, b, light=0):
    return _rgb555(r, g, b) | ((light & 3) << 30)


def _add_load_4x4(entries, words):
    for word in words:
        entries.append((CMD_MTX_LOAD_4x4, _u32(word)))


def _add_mult_4x4(entries, words):
    for word in words:
        entries.append((CMD_MTX_MULT_4x4, _u32(word)))


def _diag(sx, sy, sz):
    return [sx, 0, 0, 0,
            0, sy, 0, 0,
            0, 0, sz, 0,
            0, 0, 0, 4096]


def _rotate_z_90():
    # Rotation by +90 degrees about z: (1,0,0) -> (0,1,0).
    return [0, 4096, 0, 0,
            -4096, 0, 0, 0,
            0, 0, 4096, 0,
            0, 0, 0, 4096]


def _lit_entries(pre_entries, normal, mask=None, normal_inside=True):
    """pre_entries, then (mask), then BEGIN + NORMAL + 3 vertices + END."""
    entries = list(pre_entries)
    if mask is not None:
        entries.append((CMD_POLYGON_ATTR, (31 << 16) | (mask & 0xF)))
    if not normal_inside:
        entries.append((CMD_NORMAL, _u32(normal)))
    entries.append((CMD_BEGIN_VTXS, 0))
    if normal_inside:
        entries.append((CMD_NORMAL, _u32(normal)))
    for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
        entries.extend(_vtx16(x, y))
    entries.append((CMD_END_VTXS, 0))
    entries.append((CMD_SWAP_BUFFERS, 0))
    return entries


def _shininess_entries(entries, words):
    for word in words:
        entries.append((CMD_SHININESS, _u32(word)))


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

    def _assert_lighting(self, entries):
        vertices = self._run_cli(build_v2(entries))
        expected = gx_interp_oracle.LightingOracle().process(entries)
        self.assertEqual(len(vertices), 3)
        for vertex in vertices:
            for channel in range(3):
                self.assertAlmostEqual(
                    vertex[4 + channel], expected[channel], places=6,
                    msg=f"lit colour channel {channel}"
                )
            self.assertEqual(vertex[7], 1.0)
        return vertices, expected

    def test_light_ambient_only(self):
        entries = [
            (CMD_DIF_AMB, _rgb555(0, 0, 0) | (_rgb555(16, 8, 4) << 16)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, 4088)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        self._assert_lighting(entries)

    def test_light_diffuse_only(self):
        entries = [
            (CMD_DIF_AMB, _rgb555(31, 16, 8)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            # Light points -z, normal points +z: fixed_diffuse = 1.0.
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        self._assert_lighting(entries)

    def test_light_emission_only(self):
        # No BEGIN before NORMAL: the applied light mask is still 0.
        entries = [(CMD_SPE_EMI, _rgb555(10, 20, 30) << 16)]
        entries = _lit_entries(
            entries, _normal_word(0, 0, 4088), mask=None,
            normal_inside=False
        )
        vertices, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (10 / 31.0, 20 / 31.0, 30 / 31.0))

    def test_light_specular_without_table(self):
        entries = [
            (CMD_SPE_EMI, _rgb555(31, 31, 31)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        self._assert_lighting(entries)

    def test_light_specular_with_table(self):
        words = [0] * 32
        words[31] = 200 << 24  # table entry 127 = 200
        entries = [(CMD_SPE_EMI, _rgb555(31, 31, 31) | 0x8000)]
        _shininess_entries(entries, words)
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        self._assert_lighting(entries)

    def test_light_two_lights(self):
        entries = [
            (CMD_DIF_AMB, _rgb555(31, 31, 31)),
            (CMD_LIGHT_COLOR, _light_colour(31, 0, 0, light=0)),
            (CMD_LIGHT_COLOR, _light_colour(0, 31, 0, light=1)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=0)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=1)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x3)
        self._assert_lighting(entries)

    def test_light_each_index_alone(self):
        for light in range(4):
            entries = [
                (CMD_DIF_AMB, _rgb555(31, 31, 31)),
                (CMD_LIGHT_COLOR,
                 _light_colour(8 + 4 * light, 20 - 4 * light, 4 + 8 * light,
                               light=light)),
                (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=light)),
            ]
            entries = _lit_entries(entries, _normal_word(0, 0, 4088),
                                   mask=1 << light)
            self._assert_lighting(entries)

    def test_light_saturation_at_31(self):
        entries = [
            (CMD_DIF_AMB, _rgb555(31, 31, 31) | (_rgb555(31, 31, 31) << 16)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        vertices, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (1.0, 1.0, 1.0))

    def test_light_rotated_vec_normal(self):
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _rotate_z_90())
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        self._assert_lighting(entries)

    def test_light_mode2_vs_mode1(self):
        results = {}
        for mode in (1, 2):
            entries = [(CMD_MTX_MODE, mode)]
            _add_load_4x4(entries, _diag(8192, 4096, 4096))
            entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
            entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
            entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
            entries = _lit_entries(entries, _normal_word(4088, 0, 0),
                                   mask=0x1)
            _, expected = self._assert_lighting(entries)
            results[mode] = expected
        self.assertNotEqual(results[1], results[2])

    def test_light_scale_in_mode2_scales_position_only(self):
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(2048, 4096, 4096))
        entries.append((CMD_MTX_SCALE, 2048))
        entries.append((CMD_MTX_SCALE, 4096))
        entries.append((CMD_MTX_SCALE, 4096))
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        self._assert_lighting(entries)

    def test_light_push_pop_restores_vec(self):
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(2048, 4096, 4096))
        entries.append((CMD_MTX_PUSH, 0))
        _add_load_4x4(entries, _diag(1024, 4096, 4096))
        entries.append((CMD_MTX_POP, 1))
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        self._assert_lighting(entries)

    def test_light_dif_amb_bit15_sets_colour(self):
        diffuse = _rgb555(10, 20, 30)
        entries = [(CMD_DIF_AMB, diffuse | 0x8000), (CMD_BEGIN_VTXS, 0)]
        for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
            entries.extend(_vtx16(x, y))
        entries.append((CMD_END_VTXS, 0))
        entries.append((CMD_SWAP_BUFFERS, 0))
        vertices = self._run_cli(build_v2(entries))
        expected = [10 / 31.0, 20 / 31.0, 30 / 31.0]
        self.assertEqual(len(vertices), 3)
        for vertex in vertices:
            for channel in range(3):
                self.assertAlmostEqual(vertex[4 + channel], expected[channel], places=6)

    def test_light_dif_amb_bit15_clear_keeps_colour(self):
        entries = [
            (CMD_COLOR, _rgb555(4, 8, 16)),
            (CMD_DIF_AMB, _rgb555(10, 20, 30)),
            (CMD_BEGIN_VTXS, 0),
        ]
        for x, y in ((0, 0), (0x1000, 0), (0, 0x1000)):
            entries.extend(_vtx16(x, y))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        vertices = self._run_cli(build_v2(entries))
        expected = [4 / 31.0, 8 / 31.0, 16 / 31.0]
        for vertex in vertices:
            for channel in range(3):
                self.assertAlmostEqual(vertex[4 + channel], expected[channel], places=6)

    def test_light_diffuse_negative_dot_clamped(self):
        entries = [
            (CMD_DIF_AMB, _rgb555(31, 31, 31)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, -4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_shininess_clamp_at_4095(self):
        # A 2x vector matrix makes dot_h exceed 4096, so shininess saturates.
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(4096, 4096, 8192))
        entries.append((CMD_SPE_EMI, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (30 / 31.0, 30 / 31.0, 30 / 31.0))

    def test_light_direction_transformed_at_command_time(self):
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(2048, 4096, 4096))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        # Changing the vector matrix afterwards must not move the light.
        _add_load_4x4(entries, _diag(8192, 4096, 4096))
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        self._assert_lighting(entries)

    def test_light_shininess_applied_after_32_words(self):
        zeros = [0] * 32
        table = [0] * 32
        table[31] = 200 << 24
        entries = [(CMD_SPE_EMI, _rgb555(31, 31, 31) | 0x8000)]
        _shininess_entries(entries, zeros)
        _shininess_entries(entries, table)
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        self._assert_lighting(entries)

    def test_oracle_partial_shininess_not_applied(self):
        oracle = gx_interp_oracle.LightingOracle()
        table = [0] * 31
        table[30] = 0xFF << 24
        oracle.set_shininess(table)
        self.assertEqual(oracle.shininess_applied, [0] * 128)
        self.assertEqual(oracle.shininess_index, 124)

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
