"""C <-> Python parser consistency tests.

Builds tests/host/dump_summary.c with the host C compiler and compares its
accept/reject behaviour and per-command-type counts against
tools/twlhd_dump.py.

Run from the repository root:

    PYTHONDONTWRITEBYTECODE=1 python3 -I -m unittest discover -s tools -v
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
sys.path.insert(0, TOOLS_DIR)

import twlhd_dump  # noqa: E402


def build_frame(commands, frame_index=7, version=twlhd_dump.FORMAT_VERSION):
    header = struct.pack("<III", version, frame_index, len(commands))
    body = b"".join(
        struct.pack("<IB3x16I", type_id, len(params),
                    *(list(params) + [0] * (16 - len(params))))
        for type_id, params in commands
    )
    padding = b"\0" * (twlhd_dump.FRAME_SIZE - len(header) - len(body))
    return header + body + padding


class CParserConsistencyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-c-parser-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)

        cc = os.environ.get("CC", "cc")
        if shutil.which(cc) is None:
            raise RuntimeError(
                "host C compiler %r not found; the C/Python consistency "
                "test cannot run (install a C compiler or set CC)" % cc)

        cls.binary = os.path.join(cls.tmpdir, "dump_summary")
        command = [
            cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I", os.path.join(ROOT, "common"),
            os.path.join(ROOT, "tests", "host", "dump_summary.c"),
            os.path.join(ROOT, "common", "twlhd_frame_parse.c"),
            "-o", cls.binary,
        ]
        completed = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True)
        if completed.returncode != 0:
            raise AssertionError(
                "failed to build dump_summary.c:\n%s" % completed.stderr)

    def write_dump(self, name, data):
        path = os.path.join(self.tmpdir, name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    def run_c(self, path):
        completed = subprocess.run(
            [self.binary, path], capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)

        fields = {}
        counts = {}
        for line in completed.stdout.splitlines():
            if line.startswith("count "):
                _, type_id, count = line.split()
                counts[int(type_id, 16)] = int(count)
            else:
                key, value = line.split("=", 1)
                fields[key] = value
        return fields, counts

    def run_python(self, path):
        try:
            return twlhd_dump.parse_frame(path), None
        except SystemExit as exc:
            return None, str(exc)

    def test_valid_frame_agrees(self):
        path = self.write_dump("valid.bin", build_frame([
            (0x10, [1]),
            (0x20, [0x001F]),
            (0x21, [0x1FF00000]),
            (0x23, [0x1000, 0xF000]),
            (0x23, [0x2000, 0x0000]),
            (0x40, [0]),
            (0x99, [1, 2, 3]),  # unknown type: tolerated by both
            (0x41, []),
        ]))

        frame, error = self.run_python(path)
        self.assertIsNone(error)
        fields, counts = self.run_c(path)

        self.assertEqual(fields["result"], "OK")
        self.assertEqual(int(fields["version"]), frame.version)
        self.assertEqual(int(fields["frame"]), frame.frame_index)
        self.assertEqual(int(fields["cmd_count"]), frame.cmd_count)
        self.assertEqual(counts, twlhd_dump.count_by_type(frame))

    def test_truncated_frame_rejected_by_both(self):
        path = self.write_dump(
            "truncated.bin", build_frame([(0x10, [1])])[:-1])

        frame, error = self.run_python(path)
        self.assertIsNone(frame)
        self.assertIn("truncated", error)

        fields, _ = self.run_c(path)
        self.assertEqual(fields["result"], "ERR_SIZE")

    def test_wrong_version_rejected_by_both(self):
        path = self.write_dump(
            "version2.bin", build_frame([(0x10, [1])], version=2))

        frame, error = self.run_python(path)
        self.assertIsNone(frame)
        self.assertIn("format version 2", error)

        fields, _ = self.run_c(path)
        self.assertEqual(fields["result"], "ERR_VERSION")

    def test_cmd_count_over_capacity_rejected_by_both(self):
        data = bytearray(build_frame([(0x10, [1])]))
        struct.pack_into("<I", data, 8, twlhd_dump.RING_CAPACITY + 1)
        path = self.write_dump("over-capacity.bin", bytes(data))

        frame, error = self.run_python(path)
        self.assertIsNone(frame)
        self.assertIn("maximum is %d" % twlhd_dump.RING_CAPACITY, error)

        fields, _ = self.run_c(path)
        self.assertEqual(fields["result"], "ERR_CMD_COUNT")

    def test_c_rejects_known_type_param_mismatch(self):
        """C-only rule: twlhd_dump.py does not validate known-type counts."""
        data = bytearray(build_frame([(0x23, [0x1000, 0xF000])]))
        # Turn the VTX_16 slot into a COLOR command that still claims 2 params.
        struct.pack_into("<I", data, 12, 0x20)
        path = self.write_dump("mismatch.bin", bytes(data))

        frame, error = self.run_python(path)
        self.assertIsNone(error)
        self.assertEqual(frame.commands[0].type, 0x20)

        fields, _ = self.run_c(path)
        self.assertEqual(fields["result"], "ERR_PARAM_COUNT")


if __name__ == "__main__":
    unittest.main()
