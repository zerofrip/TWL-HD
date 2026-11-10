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
CMD_MTX_STORE = 0x13
CMD_MTX_RESTORE = 0x14
CMD_MTX_IDENTITY = 0x15
CMD_MTX_LOAD_4x4 = 0x16
CMD_MTX_LOAD_4x3 = 0x17
CMD_MTX_MULT_4x4 = 0x18
CMD_MTX_MULT_4x3 = 0x19
CMD_MTX_MULT_3x3 = 0x1A
CMD_MTX_SCALE = 0x1B
CMD_MTX_TRANS = 0x1C
CMD_COLOR = 0x20
CMD_NORMAL = 0x21
CMD_TEX_COORD = 0x22
CMD_VTX_16 = 0x23
CMD_TEXIMAGE_PARAM = 0x2A
CMD_PLTT_BASE = 0x2B
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


# MTX_TRANS on vec cannot be observed through affine-only lighting streams:
# the translation column is never read by the 3x3 transform. A later non-affine
# MTX_MULT_4x4 with a non-zero row 3 in columns 0-2 copies the translation into
# the 3x3 part lighting reads; the paired mode-2/mode-1 streams below kill the
# TRANS-on-vec removal and the `mode >= 1u` mutant.

# 20.12 cos/sin pairs. The rounding of these constants fixes the exact
# fixed-point diffuse channel the literals below assert.
_ROT_Y_WORDS = {45: (2896, 2896), 60: (2048, 3547), 90: (0, 4096)}


def _rot_y(deg):
    """Column-major Ry(deg) as 16 words: maps (0,0,n) into the xz plane."""
    c, s = _ROT_Y_WORDS[deg]
    return [c, 0, -s, 0,
            0, 4096, 0, 0,
            s, 0, c, 0,
            0, 0, 0, 4096]


def _rot_y_columns(deg, columns):
    """First `columns` columns of _rot_y(deg), three rows each (4x3/3x3)."""
    m = _rot_y(deg)
    return [m[col * 4 + row] for col in range(columns) for row in range(3)]


def _vec_light_prefix(mode):
    """White diffuse (ambient 0), white light 0 pointing -z, in `mode`."""
    return [
        (CMD_MTX_MODE, mode),
        (CMD_DIF_AMB, _rgb555(31, 31, 31)),
        (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
        (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
    ]


def _add_load_4x3(entries, words):
    for word in words:
        entries.append((CMD_MTX_LOAD_4x3, _u32(word)))


def _add_mult_4x3(entries, words):
    for word in words:
        entries.append((CMD_MTX_MULT_4x3, _u32(word)))


def _add_mult_3x3(entries, words):
    for word in words:
        entries.append((CMD_MTX_MULT_3x3, _u32(word)))


def _add_identity(entries):
    entries.append((CMD_MTX_IDENTITY, 0))


def _add_store(entries, index):
    entries.append((CMD_MTX_STORE, index))


def _add_restore(entries, index):
    entries.append((CMD_MTX_RESTORE, index))


def _add_tex_coord(entries, s, t):
    entries.append((CMD_TEX_COORD, ((t & 0xFFFF) << 16) | (s & 0xFFFF)))


def _add_teximage(entries, word):
    entries.append((CMD_TEXIMAGE_PARAM, _u32(word)))


def _add_pltt(entries, word):
    entries.append((CMD_PLTT_BASE, _u32(word)))


def _tex_triangle(entries, coordinates):
    """BEGIN triangle list with one TEX_COORD/VTX pair per (s, t)."""
    entries.append((CMD_BEGIN_VTXS, 0))
    for s, t in coordinates:
        _add_tex_coord(entries, s, t)
        entries.extend(_vtx16(0, 0))
    entries.append((CMD_END_VTXS, 0))


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

    def _run_cli_tex(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "tex_stream_v2.bin")
            with open(path, "wb") as handle:
                handle.write(data)
            result = subprocess.run(
                [CLI, "--tex", path], capture_output=True, text=True,
                check=True,
            )
        lines = result.stdout.strip().splitlines()
        self.assertEqual(lines[0], "result=OK")
        vertices = []
        tex = []
        stats_line = None
        for line in lines[2:]:
            if line.startswith("stats "):
                stats_line = line
                break
            parts = line.split()
            if parts[0] == "tex":
                self.assertEqual(len(parts), 6)
                tex.append({
                    "s": float(parts[2]),
                    "t": float(parts[3]),
                    "param": int(parts[4]),
                    "pltt": int(parts[5]),
                })
            else:
                self.assertEqual(len(parts), 9)
                vertices.append([float(x) for x in parts[1:]])
        self.assertIsNotNone(stats_line, "stats line missing")
        return vertices, tex, stats_line

    TEX_FIELD_TOL = 1e-6

    def _assert_texture(self, entries):
        vertices, tex, stats_line = self._run_cli_tex(build_v2(entries))
        expected, unsupported = gx_interp_oracle.TextureOracle().process(entries)
        self.assertEqual(len(tex), len(expected),
                         f"tex count {len(tex)} != oracle {len(expected)}")
        self.assertEqual(len(vertices), len(expected))
        for index, (got, want) in enumerate(zip(tex, expected)):
            self.assertAlmostEqual(got["s"], want[0], delta=self.TEX_FIELD_TOL,
                                   msg=f"tex[{index}].s")
            self.assertAlmostEqual(got["t"], want[1], delta=self.TEX_FIELD_TOL,
                                   msg=f"tex[{index}].t")
            self.assertEqual(got["param"], want[2],
                             f"tex[{index}].teximage_param")
            self.assertEqual(got["pltt"], want[3], f"tex[{index}].pltt_base")
        self.assertIn(f"tex_transform_unsupported={unsupported}", stats_line)
        return tex, vertices, stats_line

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

    # BUILD/UNIT only: vec-matrix lighting streams. The matrix op is issued
    # between LIGHT_VECTOR (transformed by identity vec) and NORMAL (rotated
    # by the new vec), so only the normal moves and the diffuse term changes.
    # A 90 deg rotation makes the fixed light and the rotated normal
    # perpendicular (0), while an untouched identity vec gives 29.

    def test_light_load_4x3_mode2_rotates_vec(self):
        # Guards MTX_LOAD_4x3 writing g->vec (line 724): removal leaves 29.
        entries = _vec_light_prefix(2)
        _add_load_4x3(entries, _rot_y_columns(90, 4))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_load_4x3_mode1_keeps_vec(self):
        # Guards `g->mode >= 1u` on MTX_LOAD_4x3 (line 724): vec must stay
        # identity in mode 1, so the diffuse result is 29, not 0.
        entries = _vec_light_prefix(1)
        _add_load_4x3(entries, _rot_y_columns(90, 4))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_mult_4x4_mode2_rotates_vec(self):
        # Guards MULT_4x4 writing g->vec (line 736): removal leaves 29.
        entries = _vec_light_prefix(2)
        _add_mult_4x4(entries, _rot_y(90))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_mult_4x4_mode1_keeps_vec(self):
        # Guards `g->mode >= 1u` on MULT_4x4 (line 736): vec stays identity.
        entries = _vec_light_prefix(1)
        _add_mult_4x4(entries, _rot_y(90))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_mult_4x3_mode2_rotates_vec(self):
        # Guards MULT_4x3 writing g->vec (line 748): removal leaves 29.
        entries = _vec_light_prefix(2)
        _add_mult_4x3(entries, _rot_y_columns(90, 4))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_mult_4x3_mode1_keeps_vec(self):
        # Guards `g->mode >= 1u` on MULT_4x3 (line 748): vec stays identity.
        entries = _vec_light_prefix(1)
        _add_mult_4x3(entries, _rot_y_columns(90, 4))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_mult_3x3_mode2_rotates_vec(self):
        # Guards MULT_3x3 writing g->vec (line 760): removal leaves 29.
        entries = _vec_light_prefix(2)
        _add_mult_3x3(entries, _rot_y_columns(90, 3))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_mult_3x3_mode1_keeps_vec(self):
        # Guards `g->mode >= 1u` on MULT_3x3 (line 760): vec stays identity.
        entries = _vec_light_prefix(1)
        _add_mult_3x3(entries, _rot_y_columns(90, 3))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_identity_mode2_resets_vec(self):
        # Guards MTX_IDENTITY resetting g->vec (line 704): removal leaves the
        # 90 deg rotation in place and gives 0, not 29.
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(90))
        _add_identity(entries)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_identity_mode1_keeps_vec(self):
        # Guards `g->mode >= 1u` on MTX_IDENTITY (line 704): in mode 1 vec
        # must stay rotated, so the result is 0, not 29.
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(90))
        entries.append((CMD_MTX_MODE, 1))
        _add_identity(entries)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_store_restore_mode2_slot3(self):
        # Guards stack_store saving g->vec (line 312): removal restores the
        # zero matrix (0) instead of rot_y45 (21); a dropped RESTORE (line
        # 332) leaves identity (29).
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        _add_store(entries, 3)
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        _add_restore(entries, 3)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (21 / 31.0, 21 / 31.0, 21 / 31.0))

    def test_light_store_restore_mode2_slots_distinct(self):
        # Guards slot indexing: slot 7 holds rot_y60 (14), slot 3 rot_y45
        # (21). A mixed-up index gives 21 and an un-stored slot gives 0.
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        _add_store(entries, 3)
        _add_load_4x4(entries, _rot_y(60))
        _add_store(entries, 7)
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        _add_restore(entries, 7)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (14 / 31.0, 14 / 31.0, 14 / 31.0))

    def test_light_store_restore_mode1_store_saves_vec(self):
        # Guards mode-1 MTX_STORE saving g->vec (line 312): removal restores
        # the zero matrix (0) instead of rot_y45 (21).
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        entries.append((CMD_MTX_MODE, 1))
        _add_store(entries, 3)
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        _add_restore(entries, 3)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (21 / 31.0, 21 / 31.0, 21 / 31.0))

    def test_light_store_restore_mode1_restore_slot7(self):
        # Guards mode-1 MTX_RESTORE of g->vec (line 332) plus slot
        # distinctness: slot 7 is rot_y60 (14), slot 3 rot_y45 (21).
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        _add_store(entries, 3)
        _add_load_4x4(entries, _rot_y(60))
        _add_store(entries, 7)
        entries.append((CMD_MTX_MODE, 1))
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        _add_restore(entries, 7)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (14 / 31.0, 14 / 31.0, 14 / 31.0))

    def test_light_store_restore_mode0_leaves_vec(self):
        # Mode-0 stack ops touch only the projection matrix: vec stays
        # rot_y45, so the diffuse result is 21.
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        entries.append((CMD_MTX_MODE, 0))
        _add_store(entries, 0)
        _add_restore(entries, 0)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (21 / 31.0, 21 / 31.0, 21 / 31.0))

    # --- 5B-2b texture streams (BUILD/UNIT only) -------------------------

    def test_tex_latch_order(self):
        entries = []
        _add_tex_coord(entries, 16, 32)  # before BEGIN: latched immediately
        entries.append((CMD_BEGIN_VTXS, 0))
        entries.extend(_vtx16(0, 0))
        _add_tex_coord(entries, 48, 64)  # between vertices of one polygon
        entries.extend(_vtx16(0x1000, 0))
        _add_tex_coord(entries, -16, -32)  # after the previous VTX
        entries.extend(_vtx16(0, 0x1000))
        entries.append((CMD_END_VTXS, 0))
        entries.append((CMD_SWAP_BUFFERS, 0))
        tex, _, _ = self._assert_texture(entries)
        self.assertEqual([(t["s"], t["t"]) for t in tex],
                         [(1.0, 2.0), (3.0, 4.0), (-1.0, -2.0)])
        self.assertEqual([t["param"] for t in tex], [0, 0, 0])

    def test_tex_s16_sign_edges(self):
        entries = []
        _add_tex_coord(entries, 0x8000, 0x7FFF)
        entries.append((CMD_BEGIN_VTXS, 0))
        entries.extend(_vtx16(0, 0))
        _add_tex_coord(entries, -1, -16)
        entries.extend(_vtx16(0x1000, 0))
        entries.extend(_vtx16(0, 0x1000))
        entries.append((CMD_END_VTXS, 0))
        entries.append((CMD_SWAP_BUFFERS, 0))
        tex, _, _ = self._assert_texture(entries)
        self.assertEqual((tex[0]["s"], tex[0]["t"]), (-2048.0, 2047.9375))
        self.assertEqual((tex[1]["s"], tex[1]["t"]), (-0.0625, -1.0))

    def test_tex_mode0_vs_mode1(self):
        matrix = _diag(8192, 2048, 4096)
        matrix[12] = 4096
        matrix[13] = -1024
        results = {}
        for param in (0x00001234, 0x40001234):
            entries = [(CMD_MTX_MODE, 3)]
            _add_load_4x4(entries, matrix)
            _add_tex_coord(entries, 16, 32)
            _add_teximage(entries, param)
            _add_pltt(entries, 0x5678)
            entries.append((CMD_BEGIN_VTXS, 0))
            for xy in ((0, 0), (0x1000, 0), (0, 0x1000)):
                entries.extend(_vtx16(*xy))
            entries.append((CMD_END_VTXS, 0))
            entries.append((CMD_SWAP_BUFFERS, 0))
            tex, _, _ = self._assert_texture(entries)
            results[param] = (tex[0]["s"], tex[0]["t"])
            for item in tex:
                self.assertEqual(item["param"], param)
                self.assertEqual(item["pltt"], 0x5678)
        self.assertEqual(results[0x00001234], (1.0, 2.0))
        self.assertAlmostEqual(results[0x40001234][0], 3.0, places=6)
        self.assertAlmostEqual(results[0x40001234][1], 0.75, places=6)

    def test_tex_modes_2_3_raw_and_counter(self):
        for mode in (2, 3):
            entries = [(CMD_MTX_MODE, 3)]
            _add_load_4x4(entries, _diag(8192, 8192, 4096))
            _add_tex_coord(entries, 16, 32)
            _add_teximage(entries, mode << 30)
            entries.append((CMD_BEGIN_VTXS, 0))
            for xy in ((0, 0), (0x1000, 0), (0, 0x1000)):
                entries.extend(_vtx16(*xy))
            entries.append((CMD_END_VTXS, 0))
            entries.extend(_vtx16(0, 0))  # outside BEGIN: still counted
            entries.append((CMD_SWAP_BUFFERS, 0))
            tex, _, stats_line = self._assert_texture(entries)
            self.assertEqual([(t["s"], t["t"]) for t in tex],
                             [(1.0, 2.0)] * 3)
            self.assertIn("tex_transform_unsupported=4", stats_line)

    def test_tex_quad_list_alignment(self):
        entries = [(CMD_BEGIN_VTXS, 1)]
        for s in (16, 32, 48, 64):
            _add_tex_coord(entries, s, 0)
            entries.extend(_vtx16(0, 0))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        tex, _, _ = self._assert_texture(entries)
        self.assertEqual([t["s"] for t in tex],
                         [1.0, 2.0, 3.0, 1.0, 3.0, 4.0])

    def test_tex_quad_strip_alignment(self):
        entries = [(CMD_BEGIN_VTXS, 3)]
        for s in (16, 32, 48, 64):
            _add_tex_coord(entries, s, 0)
            entries.extend(_vtx16(0, 0))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        tex, _, _ = self._assert_texture(entries)
        self.assertEqual([t["s"] for t in tex],
                         [1.0, 2.0, 4.0, 1.0, 4.0, 3.0])

    def test_tex_param_pltt_mid_polygon(self):
        entries = [(CMD_BEGIN_VTXS, 0)]
        for param, pltt in ((1, 0x10), (2, 0x20), (3, 0x30)):
            _add_teximage(entries, param)
            _add_pltt(entries, pltt)
            entries.extend(_vtx16(0, 0))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        tex, _, _ = self._assert_texture(entries)
        self.assertEqual([t["param"] for t in tex], [1, 2, 3])
        self.assertEqual([t["pltt"] for t in tex], [0x10, 0x20, 0x30])

    def test_tex_matrix_stack_ops(self):
        entries = [(CMD_MTX_MODE, 3)]
        for value in (8192, 4096, 4096):
            entries.append((CMD_MTX_SCALE, value))
        for value in (4096, 0, 0):
            entries.append((CMD_MTX_TRANS, value))
        _add_store(entries, 0)
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        _add_restore(entries, 0)
        entries.append((CMD_MTX_PUSH, 0))
        _add_load_4x4(entries, _diag(4096, 4096, 4096))
        entries.append((CMD_MTX_POP, 1))
        _add_tex_coord(entries, 16, 32)
        _add_teximage(entries, 0x40000000)
        entries.append((CMD_BEGIN_VTXS, 0))
        for xy in ((0, 0), (0x1000, 0), (0, 0x1000)):
            entries.extend(_vtx16(*xy))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        tex, _, _ = self._assert_texture(entries)
        self.assertAlmostEqual(tex[0]["s"], 4.0, places=6)
        self.assertAlmostEqual(tex[0]["t"], 2.0, places=6)

    def test_tex_matrix_load_4x3_and_mult_3x3(self):
        load_4x3 = [8192, 0, 0,
                    0, 2048, 0,
                    0, 0, 4096,
                    0, 0, 0]
        rot_z = [0, 4096, 0,
                 -4096, 0, 0,
                 0, 0, 4096]
        for mult, expected in ((False, (2.0, 1.0)), (True, (-4.0, 0.5))):
            entries = [(CMD_MTX_MODE, 3)]
            _add_load_4x3(entries, load_4x3)
            if mult:
                _add_mult_3x3(entries, rot_z)
            _add_tex_coord(entries, 16, 32)
            _add_teximage(entries, 0x40000000)
            entries.append((CMD_BEGIN_VTXS, 0))
            for xy in ((0, 0), (0x1000, 0), (0, 0x1000)):
                entries.extend(_vtx16(*xy))
            entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
            tex, _, _ = self._assert_texture(entries)
            self.assertAlmostEqual(tex[0]["s"], expected[0], places=6)
            self.assertAlmostEqual(tex[0]["t"], expected[1], places=6)

    def test_tex_flag_keeps_vertex_lines(self):
        entries = [(CMD_BEGIN_VTXS, 1)]
        for s in (16, 32, 48, 64):
            _add_tex_coord(entries, s, 0)
            entries.extend(_vtx16(0, 0))
        entries.extend([(CMD_END_VTXS, 0), (CMD_SWAP_BUFFERS, 0)])
        plain = self._run_cli(build_v2(entries))
        tex_vertices, _, _ = self._run_cli_tex(build_v2(entries))
        self.assertEqual(plain, tex_vertices)

    def test_light_store_restore_mode3_leaves_vec(self):
        # Mode-3 STORE/RESTORE use the texture stack (slot 0 only): vec stays
        # rot_y45 (21).
        entries = _vec_light_prefix(2)
        _add_load_4x4(entries, _rot_y(45))
        entries.append((CMD_MTX_MODE, 3))
        _add_store(entries, 0)
        _add_restore(entries, 0)
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (21 / 31.0, 21 / 31.0, 21 / 31.0))

    def test_light_trans_mode2_reaches_vec_via_nonaffine_mult(self):
        # Guards MTX_TRANS writing g->vec (line 786): without the TRANS the
        # later non-affine MULT_4x4 cannot fold the translation into the 3x3
        # block, so the doubled normal (31) drops to 15.
        entries = [(CMD_MTX_MODE, 2)]
        for value in (4096, 0, 0):
            entries.append((CMD_MTX_TRANS, value))
        op = _diag(4096, 4096, 4096)
        op[3] = 4096  # row 3 of column 0 folds the col-3 translation in
        _add_mult_4x4(entries, op)
        entries.append((CMD_DIF_AMB, _rgb555(16, 16, 16)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (31 / 31.0, 31 / 31.0, 31 / 31.0))

    def test_light_trans_mode1_skips_vec(self):
        # Guards `g->mode >= 1u` on MTX_TRANS (line 786): mode 1 must leave
        # vec alone, so the later MULT_4x4 sees identity and yields 15.
        entries = [(CMD_MTX_MODE, 1)]
        for value in (4096, 0, 0):
            entries.append((CMD_MTX_TRANS, value))
        entries.append((CMD_MTX_MODE, 2))
        op = _diag(4096, 4096, 4096)
        op[3] = 4096  # row 3 of column 0 folds the col-3 translation in
        _add_mult_4x4(entries, op)
        entries.append((CMD_DIF_AMB, _rgb555(16, 16, 16)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (15 / 31.0, 15 / 31.0, 15 / 31.0))

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

    def test_light_mask_zero_ignores_configured_lights(self):
        # POLYGON_ATTR mask 0 must skip every light even though all four are
        # configured: only emission survives (guards the per-light mask test).
        entries = [
            (CMD_LIGHT_COLOR, _light_colour(31, 0, 0, light=0)),
            (CMD_LIGHT_COLOR, _light_colour(0, 31, 0, light=1)),
            (CMD_LIGHT_COLOR, _light_colour(0, 0, 31, light=2)),
            (CMD_LIGHT_COLOR, _light_colour(8, 16, 24, light=3)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=0)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=1)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=2)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=3)),
            (CMD_DIF_AMB, _rgb555(3, 4, 5)),
            (CMD_SPE_EMI, _rgb555(5, 7, 9) << 16),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x0)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(
            expected, (5 / 31.0, 7 / 31.0, 9 / 31.0)
        )

    def test_light_mask_one_skips_other_configured_lights(self):
        # mask 0x1 enables light 0 only; lights 1..3 are configured and must
        # not contribute (guards the "mask != 0" scatter-gun mutant).
        entries = [
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31, light=0)),
            (CMD_LIGHT_COLOR, _light_colour(31, 0, 0, light=1)),
            (CMD_LIGHT_COLOR, _light_colour(0, 31, 0, light=2)),
            (CMD_LIGHT_COLOR, _light_colour(0, 0, 31, light=3)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=0)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=1)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=2)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096, light=3)),
            (CMD_DIF_AMB, _rgb555(9, 9, 9)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        # (9 * 31 * 4088) >> 17 == 8 on every channel.
        self.assertEqual(expected, (8 / 31.0, 8 / 31.0, 8 / 31.0))

    def test_light_mask_each_index_with_others_configured(self):
        # One light enabled at a time, but all four are configured with
        # distinct colours: removing the per-index mask test changes the sum.
        colours = [(31, 0, 0), (0, 31, 0), (0, 0, 31), (8, 16, 24)]
        expected_rgb = [(29, 0, 0), (0, 29, 0), (0, 0, 29), (7, 15, 23)]
        for light in range(4):
            entries = [(CMD_DIF_AMB, _rgb555(31, 31, 31))]
            for index in range(4):
                entries.append(
                    (CMD_LIGHT_COLOR, _light_colour(*colours[index],
                                                    light=index))
                )
                entries.append(
                    (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096,
                                                    light=index))
                )
            entries = _lit_entries(entries, _normal_word(0, 0, 4088),
                                   mask=1 << light)
            _, expected = self._assert_lighting(entries)
            self.assertEqual(
                expected, tuple(c / 31.0 for c in expected_rgb[light])
            )

    def test_light_shininess_clamp_table_entry_127(self):
        # dot_h = 8176 -> raw shininess 28544 must clamp to 4095 so the table
        # index is 127 (200). The off-by-one 4096 clamp is already killed by
        # test_light_shininess_clamp_uses_4095 ((32*4095)>>17 = 0 vs
        # (32*4096)>>17 = 1 with no shininess table).
        words = [0] * 32
        words[31] = 200 << 24  # applied entry 127 = 200
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(4096, 4096, 8192))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        entries.append((CMD_SPE_EMI, _rgb555(31, 31, 31) | 0x8000))
        _shininess_entries(entries, words)
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        # (31 * 31 * (200 << 4)) >> 17 == 23.
        self.assertEqual(expected, (23 / 31.0, 23 / 31.0, 23 / 31.0))

    def test_light_shininess_clamp_uses_4095(self):
        # Saturated shininess without a table: red has specular 16 * light 2.
        # (32 * 4095) >> 17 == 0 while (32 * 4096) >> 17 == 1.
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(4096, 4096, 8192))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(2, 31, 31)))
        entries.append((CMD_SPE_EMI, _rgb555(16, 31, 31)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(
            expected, (0 / 31.0, 30 / 31.0, 30 / 31.0)
        )

    def test_light_shininess_4096_constant_boundary(self):
        # k = 3180 was brute-forced: n_z = (4088 * k) >> 12 = 3173 and
        # raw shininess j = 818. (961 * 818) >> 17 == 5 but
        # (961 * 819) >> 17 == 6; an off-by-one 4096 constant moves j.
        entries = [(CMD_MTX_MODE, 2)]
        entries.append((CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)))
        _add_load_4x4(entries, _diag(4096, 4096, 3180))
        entries.append((CMD_SPE_EMI, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries = _lit_entries(entries, _normal_word(0, 0, 4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (5 / 31.0, 5 / 31.0, 5 / 31.0))

    def test_light_dot_zero_uses_table_entry_zero(self):
        # EQUIVALENT MUTANT DECLARATION — mutant 3b: `if (dot >= 0)` vs `if (dot > 0)`
        # common/twlhd_gx_interp.c lines 462–468: shininess is initialized to 0 (line 462)
        # before the guard (line 463). At dot==0, the mutant path computes
        # 2*mul_fixed32(0,0)-4096 = -4096, which the `if (shininess < 0)` clamp (line 468)
        # resets to 0 — identical to the initialized value from the correct path.
        # The table lookup (line 471) and specular accumulation (line 475) are outside the guard
        # and therefore see shininess=0 in both paths. No test can distinguish the two guards
        # without modifying the C code, which is prohibited. This test verifies correct behavior
        # at dot_h==0 but does NOT kill mutant 3b. EQUIVALENT MUTANT: cannot be killed.
        # NORMAL is perpendicular to the half vector: dot_h == 0, so the
        # shininess stays 0 and table[0] (255) is used.
        words = [0] * 32
        words[0] = 255  # applied entry 0 = 255
        entries = [
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
            (CMD_SPE_EMI, _rgb555(31, 31, 31) | 0x8000),
        ]
        _shininess_entries(entries, words)
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        _, expected = self._assert_lighting(entries)
        # (31 * 31 * (255 << 4)) >> 17 == 29.
        self.assertEqual(expected, (29 / 31.0, 29 / 31.0, 29 / 31.0))

    def test_light_negative_dot_specular_ignored(self):
        # dot_h = -4088: only dot > 0 may enter the specular path. A mutant
        # that accepts dot >= -100000 turns (5,5,5) into (31,31,31).
        entries = [
            (CMD_SPE_EMI, _rgb555(31, 31, 31) | (_rgb555(5, 5, 5) << 16)),
            (CMD_LIGHT_COLOR, _light_colour(31, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, -4088), mask=0x1)
        _, expected = self._assert_lighting(entries)
        self.assertEqual(expected, (5 / 31.0, 5 / 31.0, 5 / 31.0))

    def test_light_negative_shininess_clamped_to_zero(self):
        # dot_h = 8 gives raw shininess 2*0 - 4096 = -4096. The < 0 clamp must
        # keep the specular term at zero (removing it makes red 9 not 10).
        entries = [
            (CMD_DIF_AMB, _rgb555(16, 16, 16) << 16),
            (CMD_SPE_EMI, _rgb555(16, 31, 31) | (_rgb555(9, 0, 0) << 16)),
            (CMD_LIGHT_COLOR, _light_colour(2, 31, 31)),
            (CMD_LIGHT_VECTOR, _vector_word(0, 0, -4096)),
        ]
        entries = _lit_entries(entries, _normal_word(0, 0, 8), mask=0x1)
        _, expected = self._assert_lighting(entries)
        # Ambient adds (1,15,15); emission is (9,0,0).
        self.assertEqual(
            expected, (10 / 31.0, 15 / 31.0, 15 / 31.0)
        )

    def test_light_saturating_transform_clamps_both_ends(self):
        # Row 0 of the vector matrix overflows the 64-bit accumulator in both
        # directions; sat_shiftdown_accum must return INT32_MAX / INT32_MIN.
        positive = [(CMD_MTX_MODE, 2)]
        positive.append((CMD_LIGHT_VECTOR, _vector_word(-8, 0, -4096)))
        _add_load_4x4(
            positive,
            [0x7FFFFFFF, 0, 0, 0,
             0x7FFFFFFF, 0, 0, 0,
             0, 0, 0, 0,
             0, 0, 0, 4096],
        )
        positive.append((CMD_SPE_EMI, _rgb555(31, 31, 31)))
        positive.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        positive = _lit_entries(positive, _normal_word(4088, 4088, 0),
                                mask=0x1)
        _, expected = self._assert_lighting(positive)
        self.assertEqual(expected, (30 / 31.0, 30 / 31.0, 30 / 31.0))

        negative = [(CMD_MTX_MODE, 2)]
        negative.append((CMD_LIGHT_VECTOR, _vector_word(-8, 0, -4096)))
        _add_load_4x4(
            negative,
            [0x80000001, 0, 0, 0,
             0x80000001, 0, 0, 0,
             0, 0, 0, 0,
             0, 0, 0, 4096],
        )
        negative.append((CMD_SPE_EMI, _rgb555(31, 31, 31)))
        negative.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        negative = _lit_entries(negative, _normal_word(4088, 4088, 0),
                                mask=0x1)
        _, expected = self._assert_lighting(negative)
        self.assertEqual(expected, (0.0, 0.0, 0.0))

    def test_light_mode3_stack_commands_ignored(self):
        # In mode 3 PUSH/POP and matrix loads do not touch the vector matrix;
        # this golden stream contains them between two mode-2 operations.
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(8192, 4096, 4096))
        entries.append((CMD_MTX_PUSH, 0))
        _add_load_4x4(entries, _diag(1024, 4096, 4096))
        entries.extend([(CMD_MTX_MODE, 3), (CMD_MTX_PUSH, 0)])
        _add_load_4x4(entries, [0] * 16)
        entries.append((CMD_MTX_MODE, 2))
        entries.append((CMD_MTX_POP, 1))
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        entries = _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)
        self._assert_lighting(entries)

    def _mode3_lighting_stream(self, with_mode3):
        entries = [(CMD_MTX_MODE, 2)]
        _add_load_4x4(entries, _diag(8192, 4096, 4096))
        entries.append((CMD_MTX_PUSH, 0))
        _add_load_4x4(entries, _diag(1024, 4096, 4096))
        if with_mode3:
            entries.extend([
                (CMD_MTX_MODE, 3),
                (CMD_MTX_PUSH, 0),
                (CMD_MTX_MODE, 2),
            ])
        entries.append((CMD_MTX_POP, 1))
        entries.append((CMD_DIF_AMB, _rgb555(31, 31, 31)))
        entries.append((CMD_LIGHT_COLOR, _light_colour(31, 31, 31)))
        entries.append((CMD_LIGHT_VECTOR, _vector_word(-4096, 0, 0)))
        return _lit_entries(entries, _normal_word(4088, 0, 0), mask=0x1)

    def test_oracle_mode3_does_not_touch_vec(self):
        # The C interpreter ignores stack commands in mode 3. The oracle must
        # match: inserting a mode-3 PUSH must not change the final colour.
        plain = gx_interp_oracle.LightingOracle().process(
            self._mode3_lighting_stream(False)
        )
        with_mode3 = gx_interp_oracle.LightingOracle().process(
            self._mode3_lighting_stream(True)
        )
        self.assertEqual(with_mode3, plain)

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
