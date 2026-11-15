#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the SCENE=5 texture tooling (stage 5C-3).

Covers tools/emulator/compare_texture_capture.py, the TXDS/TXDT/PLDT extension
of tools/twlhd_v2_dump.py, the cross-check of the expected arrays against
arm9v2/source/demo_frame.cpp, the C parser path, and the new Makefile target.

BUILD/UNIT only: a fake/synthetic capture is used; no emulator, DS, console or
hardware is involved.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import contextlib
import io
import os
import re
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
import compare_texture_capture as tex  # noqa: E402
import test_emulator_compare as base  # noqa: E402
import test_twlhd_v2  # noqa: E402

ANGLE = 61440
DEMO_FRAME = os.path.join(ROOT, "arm9v2", "source", "demo_frame.cpp")
DUMP = os.path.join(TOOLS_DIR, "twlhd_v2_dump.py")
MAKEFILE = os.path.join(ROOT, "Makefile")


def chunk(ctype, payload, flags=0, reserved=0):
    return struct.pack("<IIII", ctype, len(payload), flags, reserved) + payload


def gx_payload(entries):
    return b"".join(struct.pack("<II", word, value) for word, value in entries)


def rsta_payload(entries):
    return b"".join(struct.pack("<IIII", *entry) for entry in entries)


def tex_record(teximage_param=None, pltt_base=None, texel_offset=0,
               texel_size=None, palette_offset=0, palette_size=None,
               index_offset=0, index_size=0, reserved0=0, reserved1=0):
    if teximage_param is None:
        teximage_param = tex.TEXIMAGE_PARAM
    if pltt_base is None:
        pltt_base = tex.PLTT_BASE
    if texel_size is None:
        texel_size = tex.TEXEL_SIZE
    if palette_size is None:
        palette_size = tex.PALETTE_SIZE
    return struct.pack("<10I", teximage_param, pltt_base, texel_offset,
                       texel_size, palette_offset, palette_size,
                       index_offset, index_size, reserved0, reserved1)


def build(entries, rsta=None, txds=None, txdt=None, pldt=None,
          flags=v2.FLAG_FRAME_COMPLETE, backend=1, coverage=0x7,
          extra_chunks=()):
    body = chunk(v2.GXCM, gx_payload(entries), v2.CHUNK_FLAG_REQUIRED)
    body += chunk(v2.META, struct.pack("<IIII", backend, coverage, 0, 0))
    count = 2
    if rsta is not None:
        body += chunk(v2.RSTA, rsta_payload(rsta))
        count += 1
    for ctype, payload in ((v2.TXDS, txds), (v2.TXDT, txdt),
                           (v2.PLDT, pldt)):
        if payload is not None:
            body += chunk(ctype, payload)
            count += 1
    for ctype, payload in extra_chunks:
        body += chunk(ctype, payload)
        count += 1
    total = v2.HEADER_SIZE + len(body)
    header = struct.pack("<IIIIIIII", 2, v2.HEADER_SIZE, total,
                         flags, 240, count, 0, 0)
    return header + body


def f32(value):
    return int(round(value * 4096.0)) & 0xFFFFFFFF


def scene5_entries(angle=ANGLE):
    """Structural GXCM stream the SCENE=5 demo produces (BUILD/UNIT model)."""
    entries = [(0x60, 0xBFFF0000), (0x10, 0), (0x15, 0)]
    entries += [(0x18, f32(value)) for value in
                compare_capture.perspective_values()]
    entries += [(0x10, 2), (0x15, 0)]
    entries += [(0x19, f32(value)) for value in
                compare_capture.lookat_values()]
    block0, block1 = compare_capture.rotation_values(angle)
    entries += [(0x1A, f32(value)) for value in block0]
    entries += [(0x1A, f32(value)) for value in block1]
    entries += [(0x1C, 0)] * 3
    entries += [(0x1B, 4096)] * 3
    entries += [(0x11, 0), (0x29, 0x001F0000),
                (0x2A, tex.TEXIMAGE_PARAM), (0x2B, tex.PLTT_BASE)]
    entries += [(0x40, 1)]
    for texcoord in (0x00000000, 0x00008000, 0x80008000, 0x80000000):
        entries += [(0x22, texcoord),
                    (0x23, 0xF000F000), (0x23, 0)]
    entries += [(0x41, 0), (0x12, 1), (0x50, 0)]
    return entries


def texture_file(entries=None, txds=None, txdt=None, pldt=None, **kwargs):
    if entries is None:
        entries = scene5_entries()
    if txds is None:
        txds = tex_record()
    if txdt is None:
        txdt = tex.TEXEL
    if pldt is None:
        pldt = tex.PALETTE
    return build(entries, rsta=[(0x04000060, 0x0010, 0, 2)],
                 txds=txds, txdt=txdt, pldt=pldt, **kwargs)


class TextureCompareTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="twlhd-tex-compare-")
        self.addCleanup(shutil.rmtree, self.tmpdir)

    def write(self, data, name="capture.bin"):
        path = os.path.join(self.tmpdir, name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    def run_texture_compare(self, data, angle=None):
        path = self.write(data)
        argv = ["compare_texture_capture.py", path]
        if angle is not None:
            argv.append(str(angle))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = tex.main(argv)
        return code, output.getvalue()

    def assert_rejected(self, data, reason, detail=None):
        code, output = self.run_texture_compare(data)
        self.assertEqual(code, 1, output)
        self.assertIn("FAIL", output)
        self.assertIn("- %s:" % reason, output)
        if detail is not None:
            self.assertIn(detail, output)

    def test_positive_control_passes(self):
        code, output = self.run_texture_compare(texture_file())
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)
        self.assertIn("SCENE=5", output)

    def test_explicit_angle_argument_passes(self):
        code, output = self.run_texture_compare(texture_file(), ANGLE)
        self.assertEqual(code, 0, output)
        self.assertIn("PASS", output)

    def test_flipped_txdt_byte_fails(self):
        txdt = bytes([tex.TEXEL[0] ^ 0xFF]) + tex.TEXEL[1:]
        self.assert_rejected(texture_file(txdt=txdt), "txdt-bytes",
                             "byte 0")

    def test_flipped_pldt_byte_fails(self):
        pldt = bytes([tex.PALETTE[0] ^ 0xFF]) + tex.PALETTE[1:]
        self.assert_rejected(texture_file(pldt=pldt), "pldt-bytes",
                             "byte 0")

    def test_wrong_teximage_param_fails(self):
        self.assert_rejected(texture_file(txds=tex_record(
            teximage_param=tex.TEXIMAGE_PARAM | 0x8)), "txds-key-param")

    def test_wrong_pltt_base_fails(self):
        self.assert_rejected(texture_file(txds=tex_record(
            pltt_base=tex.PLTT_BASE + 1)), "txds-key-pltt")

    def test_two_txds_records_fail(self):
        self.assert_rejected(
            texture_file(extra_chunks=[(v2.TXDS, tex_record())]),
            "v2-parse", "ERR_DUP_CHUNK")

    def test_two_txds_records_in_one_chunk_fail(self):
        two_records = tex_record() + tex_record(
            teximage_param=tex.TEXIMAGE_PARAM | 1)
        self.assert_rejected(texture_file(txds=two_records), "txds-count",
                             "2 TXDS records, expected 1")

    def test_zero_txds_records_fail(self):
        self.assert_rejected(texture_file(txds=b""), "v2-parse", "ERR_TXDS")

    def test_txdt_wrong_length_fails(self):
        self.assert_rejected(texture_file(txdt=tex.TEXEL + b"\0" * 4),
                             "txdt-bytes", "36 bytes, expected 32")

    def test_pldt_missing_fails(self):
        data = build(scene5_entries(),
                     rsta=[(0x04000060, 0x0010, 0, 2)],
                     txds=tex_record(), txdt=tex.TEXEL, pldt=None)
        self.assert_rejected(data, "v2-parse", "ERR_MISSING_CHUNK")

    def test_overflow_flag_is_a_texture_drop_fail(self):
        self.assert_rejected(
            texture_file(flags=v2.FLAG_FRAME_COMPLETE | v2.FLAG_OVERFLOW),
            "flags", "OVERFLOW")

    def test_truncated_texture_chunk_fails_cleanly(self):
        data = bytearray(texture_file())
        offset = v2.HEADER_SIZE
        while offset < len(data):
            ctype, payload_size = struct.unpack_from("<II", data, offset)
            if ctype == v2.TXDS:
                struct.pack_into("<I", data, offset + 4, 0xFFFF)
                break
            offset += v2.CHUNK_HEADER_SIZE + payload_size
        else:
            self.fail("TXDS chunk not found")
        self.assert_rejected(bytes(data), "v2-parse", "ERR_CHUNK_BOUNDS")

    def test_existing_mode_accepts_extra_valid_texture_chunks(self):
        """compare_capture's rules (flags/meta/GXCM/RSTA) do not inspect
        TXDS/TXDT/PLDT; twlhd_v2_dump.parse decodes valid ones, so the
        existing-mode outcome is unchanged: extra valid texture chunks are
        accepted (exit 0). compare_capture.py is unmodified.
        """
        expected = os.path.join(self.tmpdir, "expected.txt")
        with open(expected, "w", encoding="utf-8") as handle:
            handle.write(base.expected_text(base.base_gxcm(),
                                            base.base_rsta()))
        control = build(base.base_gxcm(), rsta=base.base_rsta())
        textured = build(base.base_gxcm(), rsta=base.base_rsta(),
                         txds=tex_record(), txdt=tex.TEXEL, pldt=tex.PALETTE)
        for name, data in (("control", control), ("textured", textured)):
            with self.subTest(name=name):
                capture = self.write(data, name="capture-%s.bin" % name)
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    code = compare_capture.main(
                        ["compare_capture.py", capture, expected,
                         str(ANGLE)])
                self.assertEqual(code, 0, output.getvalue())
                self.assertIn("PASS", output.getvalue())

    def test_usage_errors_exit_2(self):
        self.assertEqual(tex.main(["compare_texture_capture.py"]), 2)
        self.assertEqual(
            tex.main(["compare_texture_capture.py", "a", "b", "c"]), 2)
        path = self.write(texture_file())
        self.assertEqual(
            tex.main(["compare_texture_capture.py", path, "not-an-int"]), 2)

    def test_untouched_shape_defect_fails(self):
        entries = scene5_entries()
        begin = next(i for i, entry in enumerate(entries) if entry[0] == 0x40)
        entries[begin] = (0x40, 2)  # GL_TRIANGLE_STRIP
        self.assert_rejected(texture_file(entries=entries), "begin-type")

    def test_texture_order_defect_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x2A)
        entry = entries.pop(index)
        entries.insert(-2, entry)  # after END_VTXS, before MTX_POP
        self.assert_rejected(texture_file(entries=entries), "texture-order")

    def test_group_shape_rejects_wrong_command_positions(self):
        good = [(0x22, 0), (0x23, 0), (0x23, 0)] * 4
        self.assertTrue(tex._group_ok(good))
        for offset in (1, 2):
            with self.subTest(offset=offset):
                bad = list(good)
                bad[offset] = (0x29, 0)
                self.assertFalse(tex._group_ok(bad))

    def test_wrong_teximage_value_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x2A)
        entries[index] = (0x2A, tex.TEXIMAGE_PARAM | 0x8)
        self.assert_rejected(texture_file(entries=entries), "teximage-value")

    def test_wrong_pltt_value_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x2B)
        entries[index] = (0x2B, tex.PLTT_BASE + 1)
        self.assert_rejected(texture_file(entries=entries), "pltt-value")

    def test_extra_perspective_group_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x18)
        entries[index:index] = entries[index:index + 16]
        self.assert_rejected(texture_file(entries=entries), "perspective")

    def test_extra_lookat_group_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x19)
        entries[index:index] = entries[index:index + 12]
        self.assert_rejected(texture_file(entries=entries), "lookat")

    def test_missing_texcoord_fails(self):
        entries = scene5_entries()
        index = next(i for i, entry in enumerate(entries) if entry[0] == 0x22)
        del entries[index]
        self.assert_rejected(texture_file(entries=entries), "tex-group")

    def test_swap_not_last_fails(self):
        entries = scene5_entries()
        entries.insert(-1, (0x12, 1))
        # The parser still accepts it (SWAP stays last) only if we move SWAP;
        # instead put a non-SWAP command after SWAP by appending one.
        entries.append((0x12, 1))
        self.assert_rejected(texture_file(entries=entries), "v2-parse")


class DumpTextureTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="twlhd-tex-dump-")
        self.addCleanup(shutil.rmtree, self.tmpdir)

    def write(self, data, name="capture.bin"):
        path = os.path.join(self.tmpdir, name)
        with open(path, "wb") as handle:
            handle.write(data)
        return path

    def run_dump(self, command, path):
        return subprocess.run(
            [sys.executable, "-B", "-I", DUMP, command, path],
            capture_output=True, text=True)

    def test_stats_output_unchanged_for_existing_sample(self):
        path = self.write(test_twlhd_v2.golden())
        completed = self.run_dump("stats", path)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        expected = (
            "file:       %s (176 bytes)\n"
            "version:    2\n"
            "flags:      0x01\n"
            "frame:      1\n"
            "chunks:     3 (0 unknown skipped)\n"
            "entries:    8\n"
            "rsta:       1\n"
            "backend:    2\n"
            "coverage:   0x07\n"
            "0x00000010  1\n"
            "0x00000015  1\n"
            "0x00000020  1\n"
            "0x00000023  2\n"
            "0x00000040  1\n"
            "0x00000041  1\n"
            "0x00000050  1\n" % path)
        self.assertEqual(completed.stdout, expected)

    def test_validate_output_unchanged_for_existing_sample(self):
        path = self.write(test_twlhd_v2.golden())
        completed = self.run_dump("validate", path)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stdout, "result=OK\nentries=8\nrsta=1\n")

    def test_textured_capture_decodes_in_api_and_stats(self):
        data = texture_file()
        view = v2.parse(data)
        self.assertEqual(len(view.tex_records), 1)
        record = view.tex_records[0]
        self.assertEqual(record.teximage_param, tex.TEXIMAGE_PARAM)
        self.assertEqual(record.pltt_base, tex.PLTT_BASE)
        self.assertEqual(record.texel_size, tex.TEXEL_SIZE)
        self.assertEqual(record.palette_size, tex.PALETTE_SIZE)
        self.assertEqual(record.index_size, tex.INDEX_SIZE)
        self.assertEqual(view.txdt, tex.TEXEL)
        self.assertEqual(view.pldt, tex.PALETTE)

        completed = self.run_dump("stats", self.write(data))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("txds:       1 records", completed.stdout)
        self.assertIn("txdt:       32 bytes", completed.stdout)
        self.assertIn("pldt:       32 bytes", completed.stdout)
        self.assertIn("tex 0:     0x0C000000/0x00000000 fmt=3 texel=0+32 "
                      "palette=0+32 index=0+0", completed.stdout)

    def test_malformed_texture_chunks_are_named_errors(self):
        cases = {
            "duplicate": (texture_file(
                extra_chunks=[(v2.TXDS, tex_record())]), "ERR_DUP_CHUNK"),
            "zero-records": (texture_file(txds=b""), "ERR_TXDS"),
            "short-record": (texture_file(txds=tex_record()[:-4]), "ERR_TXDS"),
            "reserved-word": (texture_file(txds=tex_record(reserved1=1)),
                              "ERR_TXDS"),
            "size-formula": (texture_file(txds=tex_record(texel_size=16)),
                             "ERR_TXDS_SIZE"),
            "bounds": (texture_file(txdt=b"\0" * 4), "ERR_TXDS_BOUNDS"),
            "pldt-orphan": (build(scene5_entries(), pldt=tex.PALETTE),
                            "ERR_MISSING_CHUNK"),
            "pldt-missing": (build(scene5_entries(), txds=tex_record(),
                                   txdt=tex.TEXEL), "ERR_MISSING_CHUNK"),
        }
        for name, (data, result) in cases.items():
            with self.subTest(name=name):
                with self.assertRaises(v2.V2Error) as caught:
                    v2.parse(data)
                self.assertEqual(caught.exception.result, result)


class DemoSourceCrossCheckTest(unittest.TestCase):
    def setUp(self):
        with open(DEMO_FRAME, "r", encoding="utf-8") as handle:
            self.source = handle.read()

    def cpp_array(self, name, length):
        match = re.search(
            r"static const uint8_t %s\[%d\][^=]*=\s*\{(.*?)\};"
            % (name, length), self.source, re.S)
        self.assertIsNotNone(match, "array %s not found in demo_frame.cpp"
                             % name)
        values = [int(value, 16) for value in
                  re.findall(r"0x([0-9A-Fa-f]{2})", match.group(1))]
        self.assertEqual(len(values), length, name)
        return bytes(values)

    def test_texel_and_palette_arrays_match(self):
        self.assertEqual(self.cpp_array("scene5_texel", 32), tex.TEXEL)
        self.assertEqual(self.cpp_array("scene5_palette", 32), tex.PALETTE)

    def test_key_literals_match(self):
        match = re.search(
            r"uint32_t\s+param\s*=\s*\((\d+)u\s*<<\s*(\d+)\);", self.source)
        self.assertIsNotNone(match, "param literal not found")
        self.assertEqual(int(match.group(1)) << int(match.group(2)),
                         tex.TEXIMAGE_PARAM)

        match = re.search(r"uint32_t\s+pltt\s*=\s*(\d+)u;", self.source)
        self.assertIsNotNone(match, "pltt literal not found")
        self.assertEqual(int(match.group(1)), tex.PLTT_BASE)

    def test_sizes_match_the_shared_formula(self):
        self.assertEqual(v2.tex_sizes(tex.TEXIMAGE_PARAM),
                         (tex.TEXEL_SIZE, tex.PALETTE_SIZE, tex.INDEX_SIZE))


class ParserPathTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-tex-parser-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)
        cc = os.environ.get("CC", "cc")
        if shutil.which(cc) is None:
            raise RuntimeError(
                "host C compiler %r not found; the parser path test cannot "
                "run (install a C compiler or set CC)" % cc)
        cls.binary = os.path.join(cls.tmpdir, "v2_dump_cli")
        completed = subprocess.run(
            [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", os.path.join(ROOT, "common"),
             os.path.join(ROOT, "tests", "host", "v2_dump_cli.c"),
             os.path.join(ROOT, "common", "twlhd_v2_parse.c"),
             "-o", cls.binary],
            cwd=ROOT, capture_output=True, text=True)
        if completed.returncode != 0:
            raise AssertionError("failed to build v2_dump_cli.c:\n%s"
                                 % completed.stderr)

    def test_c_and_python_agree_on_textured_capture(self):
        path = os.path.join(self.tmpdir, "textured.bin")
        with open(path, "wb") as handle:
            handle.write(texture_file())
        completed = subprocess.run([self.binary, path],
                                   capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        fields = {}
        for line in completed.stdout.splitlines():
            if line.startswith("count "):
                continue
            key, value = line.split("=", 1)
            fields[key] = value
        view = v2.parse(texture_file())
        self.assertEqual(fields["result"], "OK")
        self.assertEqual(int(fields["entries"]), len(view.gxcm))
        self.assertEqual(int(fields["rsta"]), len(view.rsta))
        self.assertEqual(int(fields["chunks"]), view.chunk_count)

    def test_validate_with_parsers_script_on_textured_capture(self):
        if shutil.which("bash") is None:
            self.skipTest("bash not available")
        path = os.path.join(self.tmpdir, "textured.bin")
        with open(path, "wb") as handle:
            handle.write(texture_file())
        completed = subprocess.run(
            ["bash", os.path.join(EMULATOR_DIR, "validate_with_parsers.sh"),
             path], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("parsers: OK", completed.stdout)


class EmuValidateTexTargetTest(unittest.TestCase):
    def setUp(self):
        with open(MAKEFILE, "r", encoding="utf-8") as handle:
            self.makefile = handle.read()

    def recipe(self, target):
        lines = self.makefile.splitlines()
        start = lines.index(target + ":")
        body = []
        for line in lines[start + 1:]:
            if not line.startswith("\t"):
                break
            body.append(line)
        return body

    def test_target_is_not_part_of_all_or_test(self):
        for line in self.makefile.splitlines():
            if line.startswith("all:") or line.startswith("test:"):
                self.assertNotIn("emu-validate-tex", line)

    def test_new_target_has_the_texture_recipe(self):
        body = "\n".join(self.recipe("emu-validate-tex"))
        self.assertIn("$(MAKE) -C arm9v2 SCENE=5 AUTODUMP_FRAME=240", body)
        self.assertIn("arm9v2/twlhd-arm9v2-autodump-s5.nds", body)
        self.assertIn("$(EMU_DIR)/capture-tex", body)
        self.assertIn("validate_with_parsers.sh", body)
        self.assertIn("compare_texture_capture.py", body)

    def test_emu_validate_recipe_is_unchanged(self):
        expected = [
            "\t@if [ -z \"$(DESMUME_CLI)\" ]; then \\",
            "\t\techo \"error: DESMUME_CLI is not set; point it at a "
            "desmume-cli binary (see docs/EMULATOR_VALIDATION.md)\" >&2; \\",
            "\t\texit 1; \\",
            "\tfi",
            "\t@if [ ! -x \"$(DESMUME_CLI)\" ]; then \\",
            "\t\techo \"error: DESMUME_CLI=$(DESMUME_CLI) is not "
            "executable\" >&2; \\",
            "\t\texit 1; \\",
            "\tfi",
            "\t$(MAKE) -C arm9v2 AUTODUMP_FRAME=240",
            "\t$(MAKE) emu-expected",
            "\ttools/emulator/desmume_capture.sh "
            "arm9v2/twlhd-arm9v2-autodump.nds $(EMU_DIR)/capture",
            "\ttools/emulator/validate_with_parsers.sh "
            "$(EMU_DIR)/capture/twlhd_frame_v2.bin",
            "\tpython3 tools/emulator/compare_capture.py \\",
            "\t\t$(EMU_DIR)/capture/twlhd_frame_v2.bin \\",
            "\t\t$(EMU_DIR)/expected_frame.txt $(EMU_ANGLE)",
        ]
        self.assertEqual(self.recipe("emu-validate"), expected)

    def _run_make(self, env_value):
        env = os.environ.copy()
        if env_value is None:
            env.pop("DESMUME_CLI", None)
        else:
            env["DESMUME_CLI"] = env_value
        return subprocess.run(["make", "emu-validate-tex"], cwd=ROOT,
                              env=env, capture_output=True, text=True,
                              timeout=60)

    def test_missing_desmume_cli_fails_loudly(self):
        completed = self._run_make(None)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("DESMUME_CLI is not set", completed.stderr)

    def test_not_executable_desmume_cli_fails_loudly(self):
        completed = self._run_make(os.path.join(ROOT, "no-such-desmume"))
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("is not executable", completed.stderr)

    @unittest.skipUnless(
        os.environ.get("DEVKITARM") and os.environ.get("DEVKITPRO"),
        "devkitPro environment not available")
    def test_scene5_autodump_rom_name(self):
        completed = subprocess.run(
            ["make", "-C", "arm9v2", "-n", "SCENE=5", "AUTODUMP_FRAME=240"],
            cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("twlhd-arm9v2-autodump-s5.nds",
                      completed.stdout + completed.stderr)


if __name__ == "__main__":
    unittest.main()
