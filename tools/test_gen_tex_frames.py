#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/gen_tex_frames.py.

Runs under `make test` via `python3 -m unittest discover -s tools`.
"""

import os
import struct
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import gen_tex_frames

TWLHD_V2_FILE_MAX = 1 << 20
TWLHD_V2_TXDT_MAX_BYTES = 512 << 10
TWLHD_PICA_FRAME_ARENA_PRACTICAL_BYTES = 8 * 1024 * 1024


def _read_le32(data, offset):
    return struct.unpack("<I", data[offset:offset + 4])[0]


class TestGenTexFrames(unittest.TestCase):
    def test_default_frames_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = gen_tex_frames.write_frames(tmp)
            self.assertEqual(len(paths), 3)
            for path in paths:
                self.assertTrue(os.path.exists(path))

    def test_over_budget_frame_properties(self):
        n = 3
        gxcm, txds, txdt, pldt = gen_tex_frames._build_over_budget(n)
        frame = gen_tex_frames._build_frame(gxcm, txds, txdt, pldt)

        self.assertLessEqual(len(frame), TWLHD_V2_FILE_MAX)
        self.assertLessEqual(len(txdt), TWLHD_V2_TXDT_MAX_BYTES)

        # Parse chunk headers and inspect TXDS records.
        total_size = _read_le32(frame, 8)
        self.assertEqual(total_size, len(frame))
        chunk_count = _read_le32(frame, 20)
        body_off = 32
        chunks = {}
        for _ in range(chunk_count):
            ctype = _read_le32(frame, body_off)
            size = _read_le32(frame, body_off + 4)
            chunks[ctype] = frame[body_off + 16:body_off + 16 + size]
            body_off += 16 + size

        self.assertIn(gen_tex_frames.TXDS, chunks)
        self.assertIn(gen_tex_frames.TXDT, chunks)
        self.assertIn(gen_tex_frames.PLDT, chunks)
        self.assertEqual(len(chunks[gen_tex_frames.TXDT]), len(txdt))

        txds_data = chunks[gen_tex_frames.TXDS]
        record_size = 40
        self.assertEqual(len(txds_data) % record_size, 0)
        self.assertEqual(len(txds_data) // record_size, n)

        pltt_bases = set()
        for i in range(n):
            off = i * record_size
            teximage_param = _read_le32(txds_data, off)
            pltt_base = _read_le32(txds_data, off + 4)
            texel_offset = _read_le32(txds_data, off + 8)
            texel_size = _read_le32(txds_data, off + 12)
            palette_offset = _read_le32(txds_data, off + 16)
            palette_size = _read_le32(txds_data, off + 20)
            index_offset = _read_le32(txds_data, off + 24)
            index_size = _read_le32(txds_data, off + 28)

            self.assertEqual((teximage_param >> 26) & 7, 2)  # I2
            self.assertEqual((teximage_param >> 20) & 7, 7)  # 1024 (S)
            self.assertEqual((teximage_param >> 23) & 7, 7)  # 1024 (T)
            self.assertEqual(texel_offset, 0)
            self.assertEqual(texel_size, 1024 * 1024 // 4)
            self.assertEqual(index_offset, 0)
            self.assertEqual(index_size, 0)
            self.assertEqual(palette_size, 8)
            self.assertIn(pltt_base, range(n))
            pltt_bases.add(pltt_base)

        self.assertEqual(len(pltt_bases), n)
        self.assertGreater(n * 4 * 1024 * 1024,
                           TWLHD_PICA_FRAME_ARENA_PRACTICAL_BYTES)

    def test_over_budget_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            gen_tex_frames.main([tmp, "--over-budget", "4"])
            path = os.path.join(tmp, "twlhd_frame_v2_tex_over.bin")
            self.assertTrue(os.path.exists(path))
            with open(path, "rb") as handle:
                frame = handle.read()
            self.assertLessEqual(len(frame), TWLHD_V2_FILE_MAX)


if __name__ == "__main__":
    unittest.main()
