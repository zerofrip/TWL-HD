#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/emulator/compare_capture.py.

Builds synthetic v2 captures (the same chunk layout the parsers accept) and
checks that the comparator accepts a matching frame, rejects each class of
mismatch with its intended reason, and tolerates only the documented
first-CLEAR_COLOR value residue. Also syntax-checks the emulator scripts.

Runs under `make test` without devkitPro or an emulator.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import contextlib
import io
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
EMULATOR_DIR = os.path.join(TOOLS_DIR, "emulator")
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, EMULATOR_DIR)

import twlhd_v2_dump as v2  # noqa: E402
import compare_capture  # noqa: E402

ANGLE = 61440
CLEAR_COLOR = 0x04000350


def chunk(ctype, payload, flags=0, reserved=0):
    return struct.pack("<IIII", ctype, len(payload), flags, reserved) + payload


def build(gx_payload, rsta_payload, flags=v2.FLAG_FRAME_COMPLETE,
          backend=1, coverage=0x7):
    meta = struct.pack("<IIII", backend, coverage, 0, 0)
    body = chunk(v2.GXCM, gx_payload, v2.CHUNK_FLAG_REQUIRED)
    body += chunk(v2.META, meta)
    body += chunk(v2.RSTA, rsta_payload)
    total = v2.HEADER_SIZE + len(body)
    header = struct.pack("<IIIIIIII", 2, v2.HEADER_SIZE, total,
                         flags, 240, 3, 0, 0)
    return header + body


def f32(value):
    return int(round(value * 4096.0)) & 0xFFFFFFFF


def perspective():
    near = 0.1
    far = 100.0
    top = near * math.tan(math.radians(70.0) / 2.0)
    right = top * (256.0 / 192.0)
    return [
        2.0 * near / (2.0 * right), 0.0, 0.0, 0.0,
        0.0, 2.0 * near / (2.0 * top), 0.0, 0.0,
        0.0, 0.0, -(far + near) / (far - near), -1.0,
        0.0, 0.0, -2.0 * far * near / (far - near), 0.0,
    ]


def rotation(axis, angle):
    theta = 2.0 * math.pi * angle / 32768.0
    cosine = math.cos(theta)
    sine = math.sin(theta)
    if axis == "x":
        return [1.0, 0.0, 0.0,
                0.0, cosine, sine,
                0.0, -sine, cosine]
    return [cosine, 0.0, -sine,
            0.0, 1.0, 0.0,
            sine, 0.0, cosine]


def base_gxcm():
    entries = [(0x60, 0xBFFF0000), (0x10, 0), (0x15, 0)]
    entries += [(0x18, f32(value)) for value in perspective()]
    entries += [(0x10, 2), (0x15, 0)]
    entries += [(0x19, f32(value)) for value in
                [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -4]]
    entries += [(0x1A, f32(value)) for value in rotation("x", ANGLE)]
    entries += [(0x1A, f32(value)) for value in rotation("y", ANGLE)]
    entries += [(0x1C, 0)] * 3
    entries += [(0x1B, 4096)] * 3
    # POLYGON_ATTR = POLY_ALPHA(31) | POLY_CULL_NONE
    #                = (31 << 16) | (3 << 6) = 0x001F0000 | 0xC0 = 0x001F00C0
    entries += [(0x11, 0), (0x29, 0x001F00C0), (0x40, 1),
                (0x21, 0x1FF00000), (0x20, 0x1F),
                (0x23, 0xF000F000), (0x23, 0x00001000),
                (0x41, 0), (0x12, 1), (0x50, 0)]
    return entries


def base_rsta():
    return [
        (0x04000060, 0x0010, 0, 2),
        (CLEAR_COLOR, 0x001F0000, 0, 4),
        (CLEAR_COLOR, 0x3F1F0000, 0, 4),
        (0x04000354, 0x7FFF, 0, 2),
    ]


def gx_payload(entries):
    return b"".join(struct.pack("<II", word, value) for word, value in entries)


def rsta_payload(entries):
    return b"".join(struct.pack("<IIII", *entry) for entry in entries)


def expected_text(gxcm, rsta):
    lines = []
    for index, (word, value) in enumerate(gxcm):
        lines.append("G %d 0x%08X 0x%08X" % (index, word, value))
    for index, (addr, value, gx_index, width) in enumerate(rsta):
        lines.append("R %d 0x%08X 0x%08X %d %d"
                     % (index, addr, value, gx_index, width))
    return "\n".join(lines) + "\n"


class CompareCaptureTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="twlhd-emu-compare-")
        self.addCleanup(shutil.rmtree, self.tmpdir)
        self.expected_path = os.path.join(self.tmpdir, "expected.txt")
        with open(self.expected_path, "w", encoding="utf-8") as handle:
            handle.write(expected_text(base_gxcm(), base_rsta()))

    def write_capture(self, gxcm, rsta, name="capture.bin"):
        path = os.path.join(self.tmpdir, name)
        with open(path, "wb") as handle:
            handle.write(build(gx_payload(gxcm), rsta_payload(rsta)))
        return path

    def run_compare(self, capture):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = compare_capture.main(
                ["compare_capture.py", capture, self.expected_path,
                 str(ANGLE)])
        return code, output.getvalue()

    def assert_rejected(self, gxcm, rsta, reason):
        code, output = self.run_compare(self.write_capture(gxcm, rsta))
        self.assertEqual(code, 1, output)
        self.assertIn("FAIL", output)
        self.assertIn("- %s:" % reason, output)

    def test_matching_capture_passes(self):
        code, output = self.run_compare(
            self.write_capture(base_gxcm(), base_rsta()))
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)

    def test_polygon_attr_cull_bits_zero_fails(self):
        """Regression: cull mode 0 renders nothing on DS hardware."""
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x29)
        gxcm[index] = (0x29, 0x001F0000)  # POLY_ALPHA(31), cull bits 0
        self.assert_rejected(gxcm, base_rsta(), "gxcm-value")

    def test_non_math_value_mismatch_fails(self):
        gxcm = base_gxcm()
        gxcm[0] = (gxcm[0][0], gxcm[0][1] ^ 1)
        self.assert_rejected(gxcm, base_rsta(), "gxcm-value")

    def test_command_sequence_mismatch_fails(self):
        gxcm = base_gxcm()
        gxcm[0], gxcm[1] = (gxcm[1][0], gxcm[1][1]), (gxcm[0][0], gxcm[0][1])
        self.assert_rejected(gxcm, base_rsta(), "gxcm-cmd")

    def test_perspective_mismatch_fails(self):
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x18)
        gxcm[index] = (0x18, (gxcm[index][1] + 50) & 0xFFFFFFFF)
        self.assert_rejected(gxcm, base_rsta(), "perspective")

    def test_rotation_mismatch_fails(self):
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x1A)
        gxcm[index] = (0x1A, f32(1.5))
        self.assert_rejected(gxcm, base_rsta(), "rotation")

    def test_rsta_value_mismatch_fails(self):
        rsta = base_rsta()
        rsta[3] = (rsta[3][0], rsta[3][1] - 1, rsta[3][2], rsta[3][3])
        self.assert_rejected(base_gxcm(), rsta, "rsta-value")

    def test_first_clear_color_polygon_id_only_is_tolerated(self):
        rsta = base_rsta()
        # polygon id bits 24..29 differ (0x00 -> 0x3F); color/alpha unchanged
        rsta[1] = (rsta[1][0], 0x3F1F0000, rsta[1][2], rsta[1][3])
        code, output = self.run_compare(self.write_capture(base_gxcm(), rsta))
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)

    def test_first_clear_color_value_diff_fails(self):
        rsta = base_rsta()
        # 0x001F3F00 changes color/alpha bits, which are not polygon id
        rsta[1] = (rsta[1][0], 0x001F3F00, rsta[1][2], rsta[1][3])
        self.assert_rejected(base_gxcm(), rsta, "rsta-value")

    def test_second_clear_color_polygon_id_only_diff_fails(self):
        rsta = base_rsta()
        # The polygon-id tolerance applies ONLY to the first CLEAR_COLOR write;
        # a polygon-id-only difference on the second write must be rejected.
        rsta[2] = (rsta[2][0], rsta[2][1] ^ 0x01000000, rsta[2][2], rsta[2][3])
        self.assert_rejected(base_gxcm(), rsta, "rsta-value")

    def test_first_clear_color_bit30_diff_fails(self):
        rsta = base_rsta()
        # Bit 30 is outside the polygon-id mask and must fail even on the
        # first CLEAR_COLOR write.
        rsta[1] = (rsta[1][0], rsta[1][1] ^ 0x40000000, rsta[1][2], rsta[1][3])
        self.assert_rejected(base_gxcm(), rsta, "rsta-value")

    def _mutant_tolerates_all_clear_color(self, gxcm, rsta):
        """Run compare_capture with a mutant that tolerates every CLEAR_COLOR
        write (not just the first). Returns the mutant's exit code."""
        tmpdir = tempfile.mkdtemp(prefix="twlhd-emu-mutant-")
        self.addCleanup(shutil.rmtree, tmpdir)
        # Preserve the module<->dependency layout: emulator/compare_capture.py
        # imports twlhd_v2_dump from its grandparent directory.
        emu_dir = os.path.join(tmpdir, "emulator")
        os.makedirs(emu_dir)
        shutil.copy2(os.path.join(EMULATOR_DIR, "compare_capture.py"),
                     os.path.join(emu_dir, "compare_capture.py"))
        shutil.copy2(os.path.join(TOOLS_DIR, "twlhd_v2_dump.py"),
                     os.path.join(tmpdir, "twlhd_v2_dump.py"))
        mutant_path = os.path.join(emu_dir, "compare_capture.py")
        with open(mutant_path, "r", encoding="utf-8") as handle:
            source = handle.read()
        source = source.replace(
            "index == first_clear",
            "expected[0] == CLEAR_COLOR_ADDR")
        with open(mutant_path, "w", encoding="utf-8") as handle:
            handle.write(source)
        capture = os.path.join(tmpdir, "capture.bin")
        with open(capture, "wb") as handle:
            handle.write(build(gx_payload(gxcm), rsta_payload(rsta)))
        expected_path = os.path.join(tmpdir, "expected.txt")
        with open(expected_path, "w", encoding="utf-8") as handle:
            handle.write(expected_text(base_gxcm(), base_rsta()))
        completed = subprocess.run(
            [sys.executable, "-B", "-I", mutant_path,
             capture, expected_path, str(ANGLE)],
            capture_output=True, text=True)
        return completed.returncode

    def test_mutant_tolerating_all_clear_writes_accepts_second_write_diff(self):
        rsta = base_rsta()
        # The mutant tolerates the polygon-id difference on EVERY CLEAR_COLOR
        # write, so the second-write diff is accepted (exit 0).  That proves
        # the expected data discriminates the two rules: the real comparator
        # rejects this same diff (see test_second_clear_color_polygon_id_only_diff_fails).
        rsta[2] = (rsta[2][0], rsta[2][1] ^ 0x01000000, rsta[2][2], rsta[2][3])
        code = self._mutant_tolerates_all_clear_color(base_gxcm(), rsta)
        self.assertEqual(
            code, 0,
            "mutant tolerating every CLEAR_COLOR write should make the "
            "second-write polygon-id diff pass, proving the tolerance data "
            "discriminates the first write from the second")

    def test_rotation_block0_negative_angle_fails(self):
        gxcm = base_gxcm()
        start = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x1A)
        for j, value in enumerate(rotation("x", -ANGLE)):
            gxcm[start + j] = (0x1A, f32(value))
        code, output = self.run_compare(self.write_capture(gxcm, base_rsta()))
        self.assertEqual(code, 1, output)
        self.assertIn("- rotation:", output)
        self.assertIn("block 0", output)

    def test_rotation_blocks_swapped_fails(self):
        gxcm = base_gxcm()
        starts = [i for i, entry in enumerate(gxcm) if entry[0] == 0x1A]
        x_vals = [gxcm[starts[0] + j][1] for j in range(9)]
        y_vals = [gxcm[starts[1] + j][1] for j in range(9)]
        for j in range(9):
            gxcm[starts[0] + j] = (0x1A, y_vals[j])
            gxcm[starts[1] + j] = (0x1A, x_vals[j])
        code, output = self.run_compare(self.write_capture(gxcm, base_rsta()))
        self.assertEqual(code, 1, output)
        self.assertIn("- rotation:", output)
        self.assertIn("block 0", output)

    def test_header_flags_wrong_fails(self):
        gxcm = base_gxcm()
        rsta = base_rsta()
        path = self.write_capture(gxcm, rsta)
        with open(path, "rb") as handle:
            data = handle.read()
        bad = build(gx_payload(gxcm), rsta_payload(rsta),
                    flags=v2.FLAG_FRAME_COMPLETE | v2.FLAG_OVERFLOW)
        with open(path, "wb") as handle:
            handle.write(bad)
        code, output = self.run_compare(path)
        self.assertEqual(code, 1, output)
        self.assertIn("- flags:", output)

    def test_meta_backend_wrong_fails(self):
        gxcm = base_gxcm()
        rsta = base_rsta()
        path = self.write_capture(gxcm, rsta)
        bad = build(gx_payload(gxcm), rsta_payload(rsta), backend=2)
        with open(path, "wb") as handle:
            handle.write(bad)
        code, output = self.run_compare(path)
        self.assertEqual(code, 1, output)
        self.assertIn("- backend:", output)

    def test_meta_coverage_wrong_fails(self):
        gxcm = base_gxcm()
        rsta = base_rsta()
        path = self.write_capture(gxcm, rsta)
        bad = build(gx_payload(gxcm), rsta_payload(rsta), coverage=0x3)
        with open(path, "wb") as handle:
            handle.write(bad)
        code, output = self.run_compare(path)
        self.assertEqual(code, 1, output)
        self.assertIn("- coverage:", output)

    def test_gxcm_entry_count_mismatch_fails(self):
        gxcm = base_gxcm()
        # Insert an extra entry before SWAP_BUFFERS so the file remains
        # FRAME_COMPLETE with SWAP last and the parser accepts it.
        gxcm.insert(-1, (0x10, 0))
        self.assert_rejected(gxcm, base_rsta(), "gxcm-count")

    def test_rsta_entry_count_mismatch_fails(self):
        rsta = base_rsta()
        rsta.append((0x04000060, 0, 0, 2))
        self.assert_rejected(base_gxcm(), rsta, "rsta-count")

    def test_rsta_field_mismatch_fails(self):
        rsta = base_rsta()
        # Width 1 is whitelisted for DISP3DCNT, so the parser accepts it, but
        # it differs from the expected width and must be reported as rsta-field.
        rsta[0] = (rsta[0][0], rsta[0][1], rsta[0][2], 1)
        self.assert_rejected(base_gxcm(), rsta, "rsta-field")

    def test_lookat_element_off_fails(self):
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x19)
        gxcm[index] = (0x19, f32(1.5))
        self.assert_rejected(gxcm, base_rsta(), "lookat")

    def test_truncated_capture_fails(self):
        path = self.write_capture(base_gxcm(), base_rsta())
        with open(path, "rb") as handle:
            data = handle.read()
        with open(path, "wb") as handle:
            handle.write(data[:20])
        code, output = self.run_compare(path)
        self.assertEqual(code, 1, output)
        self.assertIn("- v2-parse:", output)

    def test_perspective_tolerance_pinning(self):
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x18)
        expected = compare_capture.perspective_values()[0]
        gxcm[index] = (0x18, f32(expected + 0.003))
        code, output = self.run_compare(self.write_capture(gxcm, base_rsta()))
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)
        gxcm[index] = (0x18, f32(expected + 0.005))
        self.assert_rejected(gxcm, base_rsta(), "perspective")

    def test_lookat_tolerance_pinning(self):
        gxcm = base_gxcm()
        index = next(i for i, entry in enumerate(gxcm) if entry[0] == 0x19)
        expected = compare_capture.lookat_values()[0]
        gxcm[index] = (0x19, f32(expected + 0.0009))
        code, output = self.run_compare(self.write_capture(gxcm, base_rsta()))
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)
        gxcm[index] = (0x19, f32(expected + 0.0011))
        self.assert_rejected(gxcm, base_rsta(), "lookat")

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_autodump_frame_bad_values_fail(self):
        for bad in ("abc", "-1", "1.5", "99999"):
            completed = subprocess.run(
                ["make", "-C", "arm9v2", "-n",
                 "AUTODUMP_FRAME=%s" % bad],
                capture_output=True, text=True)
            self.assertNotEqual(completed.returncode, 0, bad)
            self.assertIn(
                "AUTODUMP_FRAME must be empty, 0, or a positive decimal "
                "integer <= 65535", completed.stderr)

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_autodump_frame_valid_succeeds(self):
        completed = subprocess.run(
            ["make", "-C", "arm9v2", "-n", "AUTODUMP_FRAME=240"],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_scripts_are_syntactically_valid(self):
        if shutil.which("bash") is None:
            self.skipTest("bash not available")
        for name in ("desmume_capture.sh", "validate_with_parsers.sh"):
            path = os.path.join(EMULATOR_DIR, name)
            completed = subprocess.run(["bash", "-n", path],
                                       capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0,
                             "%s: %s" % (name, completed.stderr))


if __name__ == "__main__":
    unittest.main()
