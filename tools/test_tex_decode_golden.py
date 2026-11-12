#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden test for the 5B-2c texture decoder: run the C CLI on generated v2 files
and compare every decoded pixel against the independent oracle in
tools/tex_decode_oracle.py.

The v2 files are built here (mirroring tests/host/test_v2_parse.c's chunk
layout) so the parser also has to accept the texture chunks. Pixel buffers are
seeded pseudo-random, which spreads coverage over every index/alpha value; the
4x4 streams pin the palette offsets and block modes explicitly.
"""

import os
import random
import struct
import subprocess
import tempfile
import unittest

import tex_decode_oracle


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "tests", "host", "build", "tex_decode_cli")

GXCM = int.from_bytes(b"GXCM", "little")
META = int.from_bytes(b"META", "little")
TXDS = int.from_bytes(b"TXDS", "little")
TXDT = int.from_bytes(b"TXDT", "little")
PLDT = int.from_bytes(b"PLDT", "little")

CHUNK_FLAG_REQUIRED = 1


def _chunk(ctype, payload, flags=0):
    return struct.pack("<4I", ctype, len(payload), flags, 0) + payload


def _rng(seed):
    return random.Random(seed)


def _bytes(rng, count):
    return bytes(rng.getrandbits(8) for _ in range(count))


def _param(fmt, shift_s=0, shift_t=0, key0=False):
    return ((fmt & 7) << 26) | ((shift_s & 7) << 20) | \
        ((shift_t & 7) << 23) | (0x20000000 if key0 else 0)


def _texture(param, texel, palette=None, index=None, pltt_base=0):
    return {
        "param": param,
        "texel": texel,
        "palette": palette,
        "index": index,
        "pltt_base": pltt_base,
    }


def build_file(textures):
    """A parser-clean v2 file: GXCM + META + TXDS + TXDT (+ PLDT)."""
    txds = bytearray()
    txdt = bytearray()
    pldt = bytearray()
    for tex in textures:
        texel = tex["texel"]
        index = tex["index"] or b""
        palette = tex["palette"] or b""
        texel_off = len(txdt)
        index_off = texel_off + len(texel) if index else 0
        palette_off = len(pldt) if palette else 0
        txds += struct.pack(
            "<10I", tex["param"], tex["pltt_base"], texel_off, len(texel),
            palette_off, len(palette), index_off, len(index), 0, 0)
        txdt += texel + index
        pldt += palette

    body = _chunk(GXCM, struct.pack("<2I", 0x50, 0), CHUNK_FLAG_REQUIRED)
    body += _chunk(META, struct.pack("<4I", 2, 0, 0, 0))
    body += _chunk(TXDS, bytes(txds))
    body += _chunk(TXDT, bytes(txdt))
    count = 4
    if pldt:
        # PLDT may be 2 mod 4 for a 4x4 window; the chunk payload stays aligned.
        body += _chunk(PLDT, bytes(pldt) + b"\0" * (-len(pldt) % 4))
        count = 5

    header = struct.pack("<8I", 2, 32, 32 + len(body), 1, 0, count, 0, 0)
    return header + body


def run_cli(data):
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "tex_v2.bin")
        with open(path, "wb") as handle:
            handle.write(data)
        result = subprocess.run([CLI, path], capture_output=True, text=True,
                                check=True)
    records = []
    current = None
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts[0] == "record":
            current = {
                "index": int(parts[1]),
                "result": parts[2].split("=", 1)[1],
                "width": int(parts[3].split("=", 1)[1]),
                "height": int(parts[4].split("=", 1)[1]),
                "pixels": [],
            }
            records.append(current)
        elif parts[0] == "px":
            current["pixels"].append(tuple(int(v) for v in parts[2:]))
        else:
            raise AssertionError(f"unexpected CLI line: {line!r}")
    return records


class TexDecodeGoldenTest(unittest.TestCase):

    def test_cli_exists(self):
        self.assertTrue(
            os.path.isfile(CLI) and os.access(CLI, os.X_OK),
            f"tex_decode_cli binary is missing: {CLI}"
        )

    def _assert_matches_oracle(self, textures):
        records = run_cli(build_file(textures))
        self.assertEqual(len(records), len(textures))
        for index, (got, tex) in enumerate(zip(records, textures)):
            want_dims = tex_decode_oracle.dims(tex["param"])
            self.assertEqual(got["index"], index)
            self.assertEqual((got["width"], got["height"]), want_dims,
                             f"record {index} dimensions")
            try:
                expected = tex_decode_oracle.decode(
                    tex["param"], tex["texel"], tex["palette"], tex["index"])
            except tex_decode_oracle.TexDecodeError as error:
                self.assertEqual(got["result"], error.name,
                                 f"record {index} error name")
                self.assertEqual(got["pixels"], [])
                continue
            self.assertEqual(got["result"], "OK", f"record {index} result")
            self.assertEqual(len(got["pixels"]), len(expected),
                             f"record {index} pixel count")
            for pixel, (want, have) in enumerate(zip(expected,
                                                     got["pixels"])):
                self.assertEqual(have, want,
                                 f"record {index} pixel {pixel}")

    def test_all_formats_random_payloads(self):
        rng = _rng(0x5B2C)
        textures = [
            # A3I5 8x8, 32-entry palette; random top bits are ignored.
            _texture(_param(1), _bytes(rng, 64), _bytes(rng, 64), None, 0x10),
            # I2 16x8 with KeyColor0 on.
            _texture(_param(2, shift_s=1, key0=True), _bytes(rng, 32),
                     _bytes(rng, 8), None, 0x20),
            # I4 8x8 with KeyColor0 off.
            _texture(_param(3), _bytes(rng, 32), _bytes(rng, 32), None, 0x30),
            # I8 8x8, all 256 indices.
            _texture(_param(4), _bytes(rng, 64), _bytes(rng, 512), None, 0x40),
            # A5I3 8x32.
            _texture(_param(6, shift_t=2), _bytes(rng, 256), _bytes(rng, 16),
                     None, 0x50),
            # 16bpp 8x8 and 16x8, random alpha bit.
            _texture(_param(7), _bytes(rng, 128), None, None, 0x60),
            _texture(_param(7, shift_s=1), _bytes(rng, 256), None, None,
                     0x70),
        ]
        self._assert_matches_oracle(textures)

    def test_4x4_block_modes_and_offsets(self):
        rng = _rng(0x4B4)
        # 8x8: four blocks, one per mode, palette offsets 0/2/4/0 entries.
        words = [0x0000, 0x4002, 0x8004, 0xC000]
        index = b"".join(struct.pack("<H", word) for word in words)
        textures = [
            _texture(_param(5, key0=True), _bytes(rng, 16), _bytes(rng, 32),
                     index, 0x80),
            # 16x8: eight mode-2 blocks with consecutive palette offsets.
            _texture(_param(5, shift_s=1), _bytes(rng, 32), _bytes(rng, 64),
                     b"".join(struct.pack("<H", 0x8000 | block)
                              for block in range(8)), 0x90),
        ]
        self._assert_matches_oracle(textures)

    def test_4x4_channel_position_blocks(self):
        # Same texture as the C test test_4x4_full_pixel_blocks: block 0 is
        # mode 2 (four opaque colours), block 1 is mode 0 (three opaque plus
        # transparent texel 3), and every row byte is a 2-bit permutation.
        colours = {
            0: (5, 10, 20),
            1: (15, 25, 8),
            2: (22, 3, 27),
            3: (12, 19, 2),
            8: (4, 14, 24),
            9: (5, 15, 25),
            10: (6, 16, 26),
        }
        palette = bytearray(24)
        for entry, (red, green, blue) in colours.items():
            struct.pack_into("<H", palette, entry * 2,
                             tex_decode_oracle.pack_rgb555(red, green, blue))
        rows = bytes([0xE4, 0x1B, 0xB1, 0x78])
        texel = rows * 4
        index = struct.pack("<4H", 0x8000, 0x0004, 0x8000, 0x8000)
        textures = [
            _texture(_param(5), texel, bytes(palette), index, 0xC0),
        ]
        self._assert_matches_oracle(textures)

    def test_4x4_transparent_texel_positions(self):
        # Same texture as the C test test_4x4_transparent_positions: the
        # transparent texel (index 3 in modes 0/1) sits at pixel position 1, 2
        # and 3 of every block row in turn, with opaque neighbours.
        palette = b"".join(
            struct.pack("<H", tex_decode_oracle.pack_rgb555(red, green, blue))
            for red, green, blue in ((5, 10, 20), (15, 25, 8), (22, 3, 27))
        )
        row_bytes = [
            (0x9C, 0x2D, 0x4E, 0x8D),  # blocks 0/1
            (0xB4, 0x39, 0x72, 0x78),  # blocks 2/3
            (0xE4, 0xC9, 0xD2, 0xD8),  # blocks 4/5
            (0x2D, 0x2D, 0x2D, 0x2D),  # blocks 6/7
        ]
        texel = b"".join(bytes(row_bytes[block >> 1]) for block in range(8))
        index = struct.pack("<8H", *([0x0000, 0x4000] * 4))
        textures = [
            _texture(_param(5, shift_s=1), texel, palette, index, 0xD0),
        ]
        self._assert_matches_oracle(textures)

    def test_4x4_palette_window_errors(self):
        rng = _rng(0xE44)
        textures = [
            # Mode 2 needs four entries; a six-byte window is one past.
            _texture(_param(5), _bytes(rng, 16), _bytes(rng, 6),
                     b"".join(struct.pack("<H", 0x8000) for _ in range(4)),
                     0xA0),
            # Mode 1/3 need two entries only: a four-byte window is enough.
            _texture(_param(5), _bytes(rng, 16), _bytes(rng, 4),
                     b"".join(struct.pack("<H", word)
                              for word in (0x4000, 0xC000, 0x4000, 0xC000)),
                     0xB0),
        ]
        self._assert_matches_oracle(textures)


if __name__ == "__main__":
    unittest.main()
