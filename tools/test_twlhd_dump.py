"""Regression tests for tools/twlhd_dump.py.

Run from the repository root:

    python3 -B -I -m unittest discover -s tools -v
"""

import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import twlhd_dump


def build_frame(commands, frame_index=7, version=twlhd_dump.FORMAT_VERSION):
    header = struct.pack("<III", version, frame_index, len(commands))
    body = b"".join(
        struct.pack("<IB3x16I", type_id, len(params),
                    *(list(params) + [0] * (16 - len(params))))
        for type_id, params in commands
    )
    padding = b"\0" * (twlhd_dump.FRAME_SIZE - len(header) - len(body))
    return header + body + padding


class TwlhdDumpTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)

    def write_dump(self, name, data):
        path = os.path.join(self.tmpdir.name, name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    def test_wire_layout(self):
        self.assertEqual(twlhd_dump.CMD.size, 72)
        self.assertEqual(twlhd_dump.FRAME_SIZE, 73740)

    def test_parse_and_stats(self):
        path = self.write_dump("frame.bin", build_frame([
            (0x10, [1]),      # MTX_MODE
            (0x23, [0x1234, 0x5678]),  # VTX_16
            (0x41, []),       # END_VTXS
        ]))

        frame = twlhd_dump.parse_frame(path)
        self.assertEqual(frame.version, 1)
        self.assertEqual(frame.frame_index, 7)
        self.assertEqual(frame.cmd_count, 3)
        self.assertEqual(frame.commands[1].params, (0x1234, 0x5678))
        self.assertEqual(twlhd_dump.count_by_type(frame),
                         {0x10: 1, 0x23: 1, 0x41: 1})

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            twlhd_dump.print_stats(frame, path)
        text = out.getvalue()
        self.assertIn("commands:   3 / 1024", text)
        self.assertIn("73740 bytes", text)
        self.assertIn("MTX_MODE      1", text)
        self.assertIn("VTX_16        1", text)

    def test_diff_reports_first_divergence(self):
        base = [(0x10, [1]), (0x23, [0x1234, 0x5678]), (0x41, [])]
        other = [(0x10, [1]), (0x23, [0x1234, 0x9999]), (0x41, [])]
        frame_a = twlhd_dump.parse_frame(self.write_dump("a.bin", build_frame(base)))
        frame_b = twlhd_dump.parse_frame(self.write_dump("b.bin", build_frame(other)))

        self.assertEqual(twlhd_dump.first_divergence(frame_a, frame_b), 1)
        self.assertEqual(twlhd_dump.count_deltas(frame_a, frame_b), {})

    def test_diff_identical_prefix_length_mismatch(self):
        frame_a = twlhd_dump.parse_frame(
            self.write_dump("a.bin", build_frame([(0x10, [1])])))
        frame_b = twlhd_dump.parse_frame(
            self.write_dump("b.bin", build_frame([(0x10, [1]), (0x41, [])])))

        self.assertEqual(twlhd_dump.first_divergence(frame_a, frame_b), 1)

    def test_diff_identical(self):
        frame = twlhd_dump.parse_frame(
            self.write_dump("frame.bin", build_frame([(0x41, [])])))
        self.assertIsNone(twlhd_dump.first_divergence(frame, frame))

    def test_truncated_file_exits(self):
        path = self.write_dump("short.bin", build_frame([])[:-1])
        with self.assertRaises(SystemExit) as caught:
            twlhd_dump.parse_frame(path)
        self.assertIn("truncated", str(caught.exception))

    def test_wrong_version_exits(self):
        path = self.write_dump("v2.bin", build_frame([], version=2))
        with self.assertRaises(SystemExit) as caught:
            twlhd_dump.parse_frame(path)
        self.assertIn("format version 2", str(caught.exception))

    def test_missing_file_exits(self):
        path = os.path.join(self.tmpdir.name, "missing.bin")
        with self.assertRaises(SystemExit) as caught:
            twlhd_dump.parse_frame(path)
        self.assertIn("cannot read", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
