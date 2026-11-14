#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the multi-format texture frame tooling (5B-2e-2 follow-up).

Covers tools/gen_tex_format_frames.py and tools/check_tex_formats.py:

  - the generator is deterministic and the frame parses with the repo parser
  - the C decoder (tests/host/tex_decode_cli) matches the Python oracle
  - twlhd_pica_frame_prepare() accepts the frame's records
  - the checker accepts a perfect/noisy/shifted synthetic image and rejects
    permuted, mirrored, wrong-format and missing quads
  - hand-computed literal colours for one texel of every format

Runs under `make test` via `python3 -m unittest discover -s tools`.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import gen_tex_format_frames as gen  # noqa: E402
import tex_decode_oracle as oracle  # noqa: E402

SCRIPT = os.path.join(TOOLS_DIR, "check_tex_formats.py")
VALIDATE = os.path.join(TOOLS_DIR, "twlhd_v2_dump.py")

WIDTH = 400
HEIGHT = 480
CLEAR = (16, 16, 24)

EXPECTED = [gen.expected_pixels(spec) for spec in gen.QUAD_SPECS]


def _ensure_cli(target):
    """Return the CLI path, building it with make when a compiler exists."""
    path = os.path.join(ROOT, target)
    if os.access(path, os.X_OK):
        return path
    if shutil.which(os.environ.get("CC", "cc")) is None:
        return None
    if shutil.which("make") is None:
        return None
    proc = subprocess.run(["make", target], cwd=ROOT, capture_output=True,
                          text=True)
    if proc.returncode != 0 or not os.access(path, os.X_OK):
        return None
    return path


def _expected_colour(index, tx, ty):
    """Expected screen RGB for screen cell (tx, ty) of one quad.

    Decoded row 0 is the bottom screen edge (emulator-pinned orientation).
    """
    spec = gen.QUAD_SPECS[index]
    return EXPECTED[index][(spec["height"] - 1 - ty) * spec["width"] + tx][:3]


def image_bytes(colour_at, dx=0, dy=0, skip=(), height=HEIGHT):
    """Render the quads into a PPM body using colour_at(index, tx, ty)."""
    data = bytearray()
    for y in range(height):
        for x in range(WIDTH):
            colour = CLEAR
            for index, spec in enumerate(gen.QUAD_SPECS):
                if index in skip:
                    continue
                x0, y0, x1, y1 = spec["rect"]
                if x0 <= x - dx < x1 and y0 <= y - dy < y1:
                    tex_w, tex_h = spec["width"], spec["height"]
                    tx = min(tex_w - 1,
                             int((x - dx - x0) * tex_w // (x1 - x0)))
                    ty = min(tex_h - 1,
                             int((y - dy - y0) * tex_h // (y1 - y0)))
                    colour = colour_at(index, tx, ty)
                    break
            data += bytes(colour)
    return b"P6\n400 %d\n255\n" % height + bytes(data)


def write_image(directory, name, *args, **kwargs):
    path = os.path.join(directory, name)
    with open(path, "wb") as handle:
        handle.write(image_bytes(*args, **kwargs))
    return path


def run_checker(path, extra=()):
    proc = subprocess.run(
        [sys.executable, "-B", "-I", SCRIPT, path] + list(extra),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout, proc.stderr


class GeneratorTest(unittest.TestCase):
    def test_deterministic_and_fixed_size(self):
        first = gen.frame_bytes()
        second = gen.frame_bytes()
        self.assertEqual(first, second)
        self.assertEqual(len(first), 3136)

    def test_validate_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = gen.write_frame(tmp)
            proc = subprocess.run(
                [sys.executable, "-B", VALIDATE, "validate", path],
                capture_output=True, text=True, check=True)
            self.assertIn("result=OK", proc.stdout)
            self.assertIn("entries=185", proc.stdout)

    def test_texture_records(self):
        frame = gen.frame_bytes()
        total_size = struct.unpack_from("<I", frame, 8)[0]
        self.assertEqual(total_size, len(frame))
        chunk_count = struct.unpack_from("<I", frame, 20)[0]
        chunks = {}
        offset = 32
        for _ in range(chunk_count):
            ctype, size = struct.unpack_from("<II", frame, offset)
            chunks[ctype] = frame[offset + 16:offset + 16 + size]
            offset += 16 + size

        txds = chunks[gen.TXDS]
        self.assertEqual(len(txds) // 40, 8)
        keys = set()
        formats = []
        index_sizes = []
        for i in range(8):
            record = struct.unpack_from("<10I", txds, i * 40)
            param, pltt = record[0], record[1]
            keys.add((param, pltt))
            formats.append((param >> 26) & 7)
            index_sizes.append(record[7])
        self.assertEqual(len(keys), 8)
        self.assertEqual(formats, [1, 2, 3, 4, 5, 6, 7, 3])
        # Only the 4x4 quad carries an index block (8 bytes for 8x8).
        self.assertEqual(index_sizes, [0, 0, 0, 0, 8, 0, 0, 0])

    def test_describe_prints_quads(self):
        proc = subprocess.run(
            [sys.executable, "-B", os.path.join(
                TOOLS_DIR, "gen_tex_format_frames.py"), "--describe"],
            capture_output=True, text=True, check=True)
        for index in range(8):
            self.assertIn("quad %d:" % index, proc.stdout)
        self.assertIn("fmt 5 4x4", proc.stdout)


class DecoderCrossCheckTest(unittest.TestCase):
    def test_c_decoder_matches_oracle(self):
        cli = _ensure_cli("tests/host/build/tex_decode_cli")
        if cli is None:
            self.skipTest("tex_decode_cli not buildable (no C compiler/make)")

        with tempfile.TemporaryDirectory() as tmp:
            path = gen.write_frame(tmp)
            proc = subprocess.run([cli, path], capture_output=True, text=True,
                                  check=True)

        records = []
        current = None
        for line in proc.stdout.splitlines():
            parts = line.split()
            if parts[0] == "record":
                current = {"result": parts[2].split("=", 1)[1], "pixels": []}
                records.append(current)
            elif parts[0] == "px":
                current["pixels"].append(tuple(int(v) for v in parts[2:]))
            else:
                raise AssertionError("unexpected CLI line: %r" % line)

        self.assertEqual(len(records), len(gen.QUAD_SPECS))
        for index, (got, spec) in enumerate(zip(records, gen.QUAD_SPECS)):
            self.assertEqual(got["result"], "OK", "record %d" % index)
            self.assertEqual(got["pixels"], EXPECTED[index], "record %d" % index)

    def test_prepare_accepts_frame_records(self):
        cli = _ensure_cli("tests/host/build/pica_frame_cli")
        if cli is None:
            self.skipTest("pica_frame_cli not buildable (no C compiler/make)")

        # pica_frame_cli cannot read a v2 frame file: it builds its own
        # synthetic frame from this stdin description. We feed the exact
        # records the generator writes plus 3 vertices per quad, so the
        # prepare path sees the same textures the frame carries.
        blob = bytearray()
        blob += struct.pack("<I", 3 * len(gen.QUAD_SPECS))
        for spec in gen.QUAD_SPECS:
            width, height = spec["width"], spec["height"]
            for s, t in ((0.0, 0.0), (float(width), 0.0),
                         (0.0, float(height))):
                blob += struct.pack("<ffII", s, t, spec["param"], 0)
        blob += struct.pack("<I", len(gen.QUAD_SPECS))
        for spec in gen.QUAD_SPECS:
            blob += struct.pack("<IIIII", spec["param"], 0,
                                len(spec["texel"]), len(spec["palette"]),
                                len(spec["index"]))
            blob += spec["texel"] + spec["palette"] + spec["index"]

        proc = subprocess.run([cli], input=bytes(blob), capture_output=True,
                              text=False)
        self.assertEqual(proc.returncode, 0,
                         proc.stderr.decode("utf-8", "replace"))
        out = proc.stdout.decode("utf-8", "replace")
        self.assertIn("result=OK", out)
        self.assertIn("entry_count=8", out)
        self.assertIn("texture_count=8", out)


class HandComputedColoursTest(unittest.TestCase):
    """Literals derived from docs/DS_GX_RESEARCH.md:460-476 by hand.

    5-bit -> 8-bit: (c<<3)|(c>>2); 3-bit -> 8-bit: (a<<5)|(a<<2)|(a>>1);
    RGB555 word = r | g<<5 | b<<10 | 0x8000 (bit 15 is ignored).
    """

    def test_format_bytes(self):
        # A3I5 (3, 0): index (3+0)%8 = 3, alpha 7 -> 3 | 7<<5 = 0xE3.
        spec = gen.QUAD_SPECS[0]
        self.assertEqual(spec["texel"][3], 0xE3)
        # I2 (2, 0): indices (1, 2, 3, 1); x=2 is bits 4-5 -> 3.
        self.assertEqual(gen.QUAD_SPECS[1]["texel"][0], 0x79)
        # I4 (3, 0): low nibble texel 2, high nibble texel 3 -> entry 4.
        self.assertEqual(gen.QUAD_SPECS[2]["texel"][1], 0x43)
        # I8 (0, 1): one byte per texel, index 1 + (0+1)%15 = 2.
        self.assertEqual(gen.QUAD_SPECS[3]["texel"][8], 2)
        # A5I3 (2, 0): index (4+0)%8 = 4, alpha 31 -> 4 | 31<<3 = 0xFC.
        self.assertEqual(gen.QUAD_SPECS[5]["texel"][2], 0xFC)
        # 4x4 block 0 row 0 = indices 0,1,2,0 -> 0 | 1<<2 | 2<<4 = 0x24.
        self.assertEqual(gen.QUAD_SPECS[4]["texel"][0], 0x24)
        # 4x4 index words: modes 0/1/2/3 at palette words 0/2/3/5.
        self.assertEqual(gen.QUAD_SPECS[4]["index"],
                         struct.pack("<4H", 0x0000, 0x4002, 0x8003, 0xC005))
        # 16x16 I4 (2, 2): index 1 + (2+2)%15 = 5, low nibble of byte 17.
        self.assertEqual(gen.QUAD_SPECS[7]["texel"][17] & 0x0F, 5)

    def test_literal_colours(self):
        cases = [
            # (quad, x, y, (r, g, b, a), derivation)
            (0, 3, 0, (255, 255, 0, 255), "A3I5 idx 3 -> yellow, alpha 7"),
            (1, 2, 0, (0, 0, 255, 255), "I2 idx 3 -> blue, opaque"),
            (2, 3, 0, (255, 255, 0, 255), "I4 idx 4 -> yellow, opaque"),
            (3, 0, 1, (0, 255, 0, 255), "I8 idx 2 -> green, opaque"),
            (4, 1, 0, (0, 255, 0, 255), "4x4 mode 0 idx 1 -> green"),
            # mode 3 blend (r0*5+r1*3)>>6 on 8-bit, folded to 5 bits:
            # green (0,255,0) + red (255,0,0) -> r 11, g 19 -> (90, 156, 0).
            (4, 6, 4, (90, 156, 0, 255), "4x4 mode 3 5:3 blend"),
            (5, 2, 0, (255, 0, 255, 255), "A5I3 idx 4 -> magenta, alpha 31"),
            (6, 2, 1, (132, 0, 255, 255), "16bpp 0xFC10 -> (132, 0, 255)"),
            (7, 2, 2, (255, 0, 255, 255), "16x16 I4 idx 5 -> magenta"),
        ]
        for quad, x, y, want, why in cases:
            spec = gen.QUAD_SPECS[quad]
            got = EXPECTED[quad][y * spec["width"] + x]
            self.assertEqual(got, want, "%s: %s" % (spec["format"], why))


class CheckerTest(unittest.TestCase):
    def assert_passes(self, path, extra=()):
        rc, out, err = run_checker(path, extra)
        self.assertEqual(rc, 0, "stdout:\n%s\nstderr:\n%s" % (out, err))
        self.assertNotIn("FAIL", out)
        for index in range(8):
            self.assertIn("quad %d fmt=" % index, out)

    def assert_fails(self, path):
        rc, out, err = run_checker(path)
        self.assertNotEqual(rc, 0, "stdout:\n%s\nstderr:\n%s" % (out, err))
        self.assertIn("FAIL", out)

    def test_perfect_image_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "perfect.ppm", _expected_colour)
            self.assert_passes(path)
            # --emulator-expected is accepted and inert.
            self.assert_passes(path, ["--emulator-expected"])
            _, out, _ = run_checker(path)
            self.assertIn("quad 7 fmt=3 I4 16x16 cells=256/256", out)

    def test_top_screen_only_image_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "top.ppm", _expected_colour,
                               height=240)
            self.assert_passes(path)

    def test_noise_and_shift_pass(self):
        def noisy(index, tx, ty):
            base = _expected_colour(index, tx, ty)
            return tuple(
                max(0, min(255, base[channel] +
                           ((index * 7 + tx * 37 + ty * 91 + channel * 53)
                            % 31) - 15))
                for channel in range(3))

        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "noise.ppm", noisy)
            self.assert_passes(path)
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "shift.ppm", _expected_colour,
                               dx=1, dy=1)
            self.assert_passes(path)

    def test_tolerance_option(self):
        def off_by_30(index, tx, ty):
            base = _expected_colour(index, tx, ty)
            return (min(255, base[0] + 30), min(255, base[1] + 30),
                    min(255, base[2] + 30))

        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "off30.ppm", off_by_30)
            rc, out, _ = run_checker(path)
            self.assertNotEqual(rc, 0)
            self.assertIn("FAIL", out)
            self.assert_passes(path, ["--tolerance", "30"])

    def test_permuted_texels_fail(self):
        def permuted(index, tx, ty):
            if index == 3 and (tx, ty) in ((0, 0), (1, 1)):
                return _expected_colour(index, 1 - tx, 1 - ty)
            return _expected_colour(index, tx, ty)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "permuted.ppm", permuted))

    def test_mirrored_quad_fails(self):
        def mirrored(index, tx, ty):
            if index == 0:
                return _expected_colour(index, 7 - tx, ty)
            return _expected_colour(index, tx, ty)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "mirrored.ppm", mirrored))

    def test_wrong_format_fails(self):
        spec = gen.QUAD_SPECS[3]
        i4_param = (3 << 26)
        as_i4 = oracle.decode(i4_param, spec["texel"][:32],
                              spec["palette"][:32], b"")

        def wrong(index, tx, ty):
            if index == 3:
                # I4 decoding of the I8 bytes, still in screen orientation.
                return as_i4[(7 - ty) * 8 + tx][:3]
            return _expected_colour(index, tx, ty)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "wrongfmt.ppm", wrong))

    def test_missing_quad_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "missing.ppm", _expected_colour,
                               skip=(7,))
            rc, out, _ = run_checker(path)
            self.assertNotEqual(rc, 0)
            self.assertIn("quad 7 fmt=3 I4 16x16 MISSING", out)


if __name__ == "__main__":
    unittest.main()
