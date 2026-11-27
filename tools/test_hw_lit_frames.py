# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/hw/gen_lit_frames.py.

Determinism, v2 structural rules, and acceptance by both the Python parser and
the C parser built from tests/host/v2_dump_cli.c.  Offline: no console or
network access.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, os.path.join(TOOLS_DIR, "hw"))

import twlhd_v2_dump as v2  # noqa: E402
import gen_lit_frames as gen  # noqa: E402


class LitFramesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-lit-frames-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)
        cls.first = os.path.join(cls.tmpdir, "first")
        cls.second = os.path.join(cls.tmpdir, "second")
        gen.write_frames(cls.first)
        gen.write_frames(cls.second)

        cc = os.environ.get("CC", "cc")
        if shutil.which(cc) is None:
            raise RuntimeError(
                "host C compiler %r not found; the v2 parser test cannot run"
                % cc)
        cls.binary = os.path.join(cls.tmpdir, "v2_dump_cli")
        command = [
            cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", os.path.join(ROOT, "common"),
            os.path.join(ROOT, "tests", "host", "v2_dump_cli.c"),
            os.path.join(ROOT, "common", "twlhd_v2_parse.c"),
            "-o", cls.binary,
        ]
        completed = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True)
        if completed.returncode != 0:
            raise AssertionError(
                "failed to build v2_dump_cli.c:\n%s" % completed.stderr)

    def read(self, directory, name):
        with open(os.path.join(directory, name), "rb") as handle:
            return handle.read()

    def counts(self, view):
        return v2.count_by_cmd_word(view)

    def test_exactly_three_files(self):
        names = sorted(os.listdir(self.first))
        self.assertEqual(names, sorted(gen.FRAME_NAMES))

    def test_deterministic_bytes(self):
        for name in gen.FRAME_NAMES:
            with self.subTest(name=name):
                self.assertEqual(self.read(self.first, name),
                                 self.read(self.second, name),
                                 "generator is not deterministic")
        # A second write into the same directory must also be identical.
        gen.write_frames(self.first)
        for name in gen.FRAME_NAMES:
            self.assertEqual(self.read(self.first, name),
                             self.read(self.second, name))

    def test_python_parser_accepts(self):
        for name in gen.FRAME_NAMES:
            with self.subTest(name=name):
                data = self.read(self.first, name)
                view = v2.parse(data)
                self.assertEqual(view.flags, 0)
                self.assertEqual(len(view.rsta), 0)

    def test_c_parser_accepts(self):
        for name in gen.FRAME_NAMES:
            with self.subTest(name=name):
                path = os.path.join(self.first, name)
                completed = subprocess.run(
                    [self.binary, path], capture_output=True, text=True)
                self.assertEqual(completed.returncode, 0,
                                 completed.stderr)
                self.assertIn("result=OK", completed.stdout)

    def test_lit_a_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[0]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x32), 1)   # LIGHT_VECTOR
        self.assertEqual(counts.get(0x33), 1)   # LIGHT_COLOR
        self.assertEqual(counts.get(0x30), 1)   # DIF_AMB
        self.assertEqual(counts.get(0x31), None)  # no SPE_EMI
        self.assertEqual(counts.get(0x21), 4)   # NORMAL per quad
        self.assertEqual(counts.get(0x29), 4)   # POLYGON_ATTR per quad
        self.assertEqual(counts.get(0x40), 4)   # BEGIN_VTXS
        self.assertEqual(counts.get(0x41), 4)   # END_VTXS
        self.assertEqual(counts.get(0x23), 32)  # four VTX_16 per quad
        self.assertEqual(counts.get(0x20), None)  # no flat COLOR

    def test_lit_b_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[1]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x30), 4)   # DIF_AMB, one per quad
        self.assertEqual(counts.get(0x31), 4)   # SPE_EMI, one per quad
        self.assertEqual(counts.get(0x32), 2)   # two light vectors
        self.assertEqual(counts.get(0x33), 4)   # red/blue/white/red
        self.assertEqual(counts.get(0x34), 32)  # full shininess table
        self.assertEqual(counts.get(0x21), 4)   # NORMAL per quad
        self.assertEqual(counts.get(0x40), 4)
        self.assertEqual(counts.get(0x41), 4)

    def test_lit_c_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[3]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x30), 4)   # DIF_AMB, one per quad
        self.assertIsNone(counts.get(0x31))     # no SPE_EMI
        self.assertEqual(counts.get(0x32), 1)   # one light vector
        self.assertEqual(counts.get(0x33), 1)   # one white light colour
        self.assertEqual(counts.get(0x21), 4)   # NORMAL per quad
        self.assertEqual(counts.get(0x29), 4)
        self.assertEqual(counts.get(0x40), 4)
        self.assertEqual(counts.get(0x41), 4)

    def test_lit_d_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[4]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x30), 4)   # DIF_AMB, one per quad
        self.assertEqual(counts.get(0x31), 4)   # SPE_EMI, one per quad
        self.assertEqual(counts.get(0x32), 2)   # two light vectors
        self.assertEqual(counts.get(0x33), 2)   # two light colours
        self.assertEqual(counts.get(0x34), 32)  # shininess table
        self.assertEqual(counts.get(0x21), 4)
        self.assertEqual(counts.get(0x29), 4)

    def test_lit_e_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[5]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x31), 4)   # SPE_EMI, one per quad
        self.assertEqual(counts.get(0x33), 4)   # LIGHT_COLOR, one per quad
        self.assertEqual(counts.get(0x34), 32)  # shininess table
        self.assertEqual(counts.get(0x21), 4)
        self.assertEqual(counts.get(0x29), 4)

    def test_lit_f_command_mix(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[6]))
        counts = self.counts(view)
        self.assertEqual(counts.get(0x30), 4)   # DIF_AMB, one per quad
        self.assertIsNone(counts.get(0x31))     # no SPE_EMI
        self.assertEqual(counts.get(0x33), 4)   # LIGHT_COLOR, one per quad
        self.assertEqual(counts.get(0x21), 4)
        self.assertEqual(counts.get(0x29), 4)

    def test_lit_ctl_is_unlit(self):
        view = v2.parse(self.read(self.first, gen.FRAME_NAMES[2]))
        counts = self.counts(view)
        for cmd in (0x21, 0x30, 0x31, 0x32, 0x33, 0x34, 0x29):
            self.assertIsNone(counts.get(cmd),
                              "control frame must not carry command 0x%02X"
                              % cmd)
        self.assertEqual(counts.get(0x20), 4)   # flat COLOR per quad
        self.assertEqual(counts.get(0x40), 4)
        self.assertEqual(counts.get(0x41), 4)

    def test_alpha_is_31_for_lit_frames(self):
        for name in gen.FRAME_NAMES[:2]:
            view = v2.parse(self.read(self.first, name))
            attrs = [value for word, value in view.gxcm if word == 0x29]
            self.assertEqual(len(attrs), 4)
            for value in attrs:
                self.assertEqual((value >> 16) & 0x1F, 31, name)


if __name__ == "__main__":
    unittest.main()
