# SPDX-License-Identifier: Apache-2.0
"""C <-> Python v2 parser agreement tests.

Builds tests/host/v2_dump_cli.c with the host C compiler and compares it
against the independent implementation in tools/twlhd_v2_dump.py over a
fixed-seed corpus: accept/reject, the reported result name, and (for accepted
files) per-command counts, RSTA count and flags.

Run from the repository root:

    PYTHONDONTWRITEBYTECODE=1 python3 -I -m unittest discover -s tools
"""

import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)

import twlhd_v2_dump as v2  # noqa: E402


def chunk(ctype, payload, flags=0, reserved=0, declared_size=None):
    size = len(payload) if declared_size is None else declared_size
    return struct.pack("<IIII", ctype, size, flags, reserved) + payload


def build(gx_payload, meta_payload=None, rsta_payload=None, flags=0, frame=0,
          gx_flags=v2.CHUNK_FLAG_REQUIRED, meta_flags=0, rsta_flags=0):
    if meta_payload is None:
        meta_payload = struct.pack("<IIII", 2, 0x7, 0, 0)

    body = chunk(v2.GXCM, gx_payload, gx_flags)
    body += chunk(v2.META, meta_payload, meta_flags)
    count = 2
    if rsta_payload is not None:
        body += chunk(v2.RSTA, rsta_payload, rsta_flags)
        count = 3

    total = v2.HEADER_SIZE + len(body)
    header = struct.pack("<IIIIIIII", 2, 32, total, flags, frame, count, 0, 0)
    return header + body


def entries(*pairs):
    return b"".join(struct.pack("<II", word, value) for word, value in pairs)


def rsta(*quads):
    return b"".join(struct.pack("<IIII", *quad) for quad in quads)


GOLDEN_ENTRIES = entries(
    (0x10, 2), (0x15, 0), (0x40, 1), (0x20, 0x1F),
    (0x23, 0x1000), (0x23, 0x2000), (0x41, 0), (0x50, 0))
GOLDEN_RSTA = rsta((0x04000350, 0x001F3FFF, 3, 4))


def golden():
    return build(GOLDEN_ENTRIES, rsta_payload=GOLDEN_RSTA,
                 flags=v2.FLAG_FRAME_COMPLETE, frame=1)


def error_cases():
    """(name, bytes, expected result) for every parser rejection."""
    base = golden()
    cases = []

    cases.append(("truncated", base[:31], "ERR_TRUNCATED"))
    cases.append(("version", b"\x01\x00\x00\x00" + base[4:], "ERR_VERSION"))

    data = bytearray(base)
    struct.pack_into("<I", data, 4, 33)
    cases.append(("header-size", bytes(data), "ERR_HEADER"))

    data = bytearray(base)
    struct.pack_into("<I", data, 24, 1)
    cases.append(("header-reserved", bytes(data), "ERR_HEADER"))

    data = bytearray(base)
    struct.pack_into("<I", data, 12, 4)
    cases.append(("flags", bytes(data), "ERR_FLAGS"))

    data = bytearray(base)
    struct.pack_into("<I", data, 20, 0)
    cases.append(("chunk-count-zero", bytes(data), "ERR_CHUNK_COUNT"))

    data = bytearray(base)
    struct.pack_into("<I", data, 20, 17)
    cases.append(("chunk-count-high", bytes(data), "ERR_CHUNK_COUNT"))

    data = bytearray(base)
    struct.pack_into("<I", data, 8, len(base) + 4)
    cases.append(("size-mismatch", bytes(data), "ERR_SIZE"))

    data = bytearray(base)
    struct.pack_into("<I", data, 8, v2.FILE_MAX + 4)
    cases.append(("size-huge", bytes(data), "ERR_SIZE"))

    # Chunk payload extends past the file.
    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 4, 0xFFFFFFFF)
    cases.append(("chunk-bounds", bytes(data), "ERR_CHUNK_BOUNDS"))

    # Trailing bytes after the last chunk.
    data = bytearray(base) + b"\xAA\xBB\xCC\xDD"
    struct.pack_into("<I", data, 8, len(data))
    cases.append(("chunk-trailing", bytes(data), "ERR_CHUNK_BOUNDS"))

    # Not a multiple of 4.
    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 4, 62)
    cases.append(("chunk-align", bytes(data), "ERR_CHUNK_ALIGN"))

    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 8, 2)
    cases.append(("chunk-flags", bytes(data), "ERR_CHUNK_FLAGS"))

    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 12, 1)
    cases.append(("chunk-reserved", bytes(data), "ERR_CHUNK_FLAGS"))

    # Duplicate META.
    duplicate = bytearray(base[:32]) + bytearray(base[32:])
    dup_meta = chunk(v2.META, struct.pack("<IIII", 2, 0, 0, 0))
    duplicate += dup_meta
    struct.pack_into("<I", duplicate, 8, len(duplicate))
    struct.pack_into("<I", duplicate, 20, 4)
    cases.append(("duplicate", bytes(duplicate), "ERR_DUP_CHUNK"))

    # Missing GXCM (rename it and clear the REQUIRED bit).
    missing = bytearray(base)
    struct.pack_into("<I", missing, 32, v2.fourcc("PAD0"))
    struct.pack_into("<I", missing, 32 + 8, 0)
    cases.append(("missing-gxcm", bytes(missing), "ERR_MISSING_CHUNK"))

    # Unknown REQUIRED chunk.
    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 0, v2.fourcc("PAD0"))
    struct.pack_into("<I", data, 32 + 8, 1)
    cases.append(("unsupported-required", bytes(data),
                  "ERR_UNSUPPORTED_REQUIRED"))

    # Required-bit expectations.
    data = bytearray(base)
    struct.pack_into("<I", data, 32 + 8, 0)
    cases.append(("gxcm-not-required", bytes(data), "ERR_CHUNK_FLAGS"))

    # META payload size wrong (keep GXCM valid: SWAP last + FRAME_COMPLETE).
    bad_meta = build(GOLDEN_ENTRIES, meta_payload=b"\0" * 12,
                     flags=v2.FLAG_FRAME_COMPLETE)
    cases.append(("meta-size", bad_meta, "ERR_META"))

    # Empty GXCM.
    cases.append(("gxcm-empty", build(b""), "ERR_GXCM"))
    cases.append(("gxcm-not-multiple", build(entries((0x40, 1))[:-1] + b"\0" * 5),
                  "ERR_GXCM"))

    # Bad cmd_word.
    for name, word in (("gxcm-nop", 0x00), ("gxcm-id", 0x42),
                       ("gxcm-bits", 0x100), ("gxcm-raw-bits", 0x00010005)):
        cases.append((name, build(entries((word, 0))), "ERR_GXCM_CMD"))

    # Incomplete group.
    cases.append(("gxcm-group", build(entries((0x23, 1))), "ERR_GXCM_GROUP"))

    # FRAME_COMPLETE without a trailing SWAP.
    cases.append(("gxcm-frame", build(entries((0x40, 1), (0x41, 0)),
                                      flags=v2.FLAG_FRAME_COMPLETE),
                  "ERR_GXCM_FRAME"))

    # Bad RSTA width.
    cases.append(("rsta-width",
                  build(entries((0x40, 1)),
                        rsta_payload=rsta((0x04000350, 0, 0, 3))),
                  "ERR_RSTA"))

    # RSTA value wider than the port width.
    cases.append(("rsta-value-width",
                  build(entries((0x40, 1)),
                        rsta_payload=rsta((0x04000360, 0x1FF, 0, 1))),
                  "ERR_RSTA"))

    return cases


def valid_cases():
    out = [("golden", golden())]
    out.append(("no-rsta", build(entries((0x40, 1)))))
    out.append(("raw", build(entries((v2.RAW_FIFO_BIT, 0xDEADBEEF),
                                     (0x40, 1)))))
    out.append(("overflow-trailing",
                build(entries((0x23, 0x1000)), flags=v2.FLAG_OVERFLOW)))
    out.append(("unknown-optional",
                golden() + chunk(v2.fourcc("PAD0"), b"\0" * 8)))
    # Fix the header of the unknown-optional case.
    data = bytearray(out[-1][1])
    struct.pack_into("<I", data, 8, len(data))
    struct.pack_into("<I", data, 20, 4)
    out[-1] = ("unknown-optional", bytes(data))

    # Same structure as golden but one entry VALUE differs; digest must change.
    diff = bytearray(golden())
    struct.pack_into("<I", diff, 76, 0x12345678)  # value of the COLOR entry
    out.append(("golden-diff-value", bytes(diff)))
    return out


def mutations(base, rng, count):
    out = []
    for index in range(count):
        data = bytearray(base)
        kind = index % 5
        if kind == 0:
            for _ in range(rng.randint(1, 3)):
                pos = rng.randrange(len(data))
                data[pos] ^= 0xFF
        elif kind == 1:
            pos = rng.randrange(len(data))
            data[pos] = rng.randrange(256)
        elif kind == 2:
            data = data[:rng.randrange(len(data))]
        elif kind == 3:
            data.extend(bytes(rng.randrange(256) for _ in range(4)))
            if rng.random() < 0.5:
                struct.pack_into("<I", data, 8, len(data))
        else:
            pos = rng.randrange(0, max(1, len(data) // 4)) * 4
            if pos + 4 <= len(data):
                struct.pack_into("<I", data, pos, rng.getrandbits(32))
        out.append(bytes(data))
    return out


class V2AgreementTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.mkdtemp(prefix="twlhd-v2-")
        cls.addClassCleanup(shutil.rmtree, cls.tmpdir)

        cc = os.environ.get("CC", "cc")
        if shutil.which(cc) is None:
            raise RuntimeError(
                "host C compiler %r not found; the v2 agreement test cannot "
                "run (install a C compiler or set CC)" % cc)

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

    def write(self, name, data):
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
                _, word, count = line.split()
                counts[int(word, 16)] = int(count)
            else:
                key, value = line.split("=", 1)
                fields[key] = value
        return fields, counts

    def run_python(self, data):
        try:
            return v2.parse(data), None
        except v2.V2Error as exc:
            return None, exc.result

    def assert_agree(self, name, data):
        view, error = self.run_python(data)
        path = self.write(name + ".bin", data)
        fields, counts = self.run_c(path)

        if view is None:
            self.assertEqual(fields["result"], error,
                             "%s: C and Python disagree" % name)
            return

        self.assertEqual(fields["result"], "OK", "%s: C rejected a valid file" % name)
        self.assertEqual(int(fields["flags"]), view.flags, name)
        self.assertEqual(int(fields["frame"]), view.frame_index, name)
        self.assertEqual(int(fields["chunks"]), view.chunk_count, name)
        self.assertEqual(int(fields["unknown"]), view.unknown_chunks_skipped, name)
        self.assertEqual(int(fields["entries"]), len(view.gxcm), name)
        self.assertEqual(int(fields["rsta"]), len(view.rsta), name)
        self.assertEqual(int(fields["meta_backend"]), view.meta_backend_id, name)
        self.assertEqual(int(fields["meta_coverage"]),
                         view.meta_coverage_flags, name)
        self.assertEqual(counts, v2.count_by_cmd_word(view), name)
        self.assertEqual(int(fields["digest"], 16), v2.digest(data, view), name)

    def test_every_error_class(self):
        for name, data, expected in error_cases():
            with self.subTest(name=name):
                view, error = self.run_python(data)
                self.assertIsNone(view, name)
                self.assertEqual(error, expected, name)
                self.assert_agree("error-" + name, data)

    def test_valid_cases(self):
        for name, data in valid_cases():
            with self.subTest(name=name):
                view, error = self.run_python(data)
                self.assertIsNone(error, name)
                self.assert_agree("valid-" + name, data)

    def test_digest_catches_value_change(self):
        base = golden()
        view, error = self.run_python(base)
        self.assertIsNone(error)
        base_digest = v2.digest(base, view)

        diff = bytearray(base)
        struct.pack_into("<I", diff, 76, 0x12345678)
        diff = bytes(diff)
        view, error = self.run_python(diff)
        self.assertIsNone(error)
        self.assertNotEqual(v2.digest(diff, view), base_digest,
                            "digest must change when a GXCM value changes")

    def test_fixed_seed_mutations(self):
        rng = random.Random(0x4D0F)
        seeds = [golden(), build(entries((0x40, 1), (0x50, 0)),
                                 flags=v2.FLAG_FRAME_COMPLETE)]
        corpus = []
        for index, seed in enumerate(seeds):
            corpus.extend(mutations(seed, rng, 200))

        for index, data in enumerate(corpus):
            self.assert_agree("mutation-%03d" % index, data)

    def test_v1_tool_rejects_v2(self):
        base = golden()
        # Pad to the v1 size with an unknown optional chunk so the v1 tool's
        # size check passes and its version check is what fires.
        pad = 73740 - len(base) - 16
        data = bytearray(base)
        data += chunk(v2.fourcc("PAD0"), b"\0" * pad)
        struct.pack_into("<I", data, 8, len(data))
        struct.pack_into("<I", data, 20, 4)
        self.assertEqual(len(data), 73740)

        path = self.write("v1-sized-v2.bin", bytes(data))
        completed = subprocess.run(
            [sys.executable, "-B", "-I",
             os.path.join(TOOLS_DIR, "twlhd_dump.py"), "stats", path],
            cwd=ROOT, capture_output=True, text=True)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("format version 2", completed.stderr)

    def test_valid_v1_still_parses(self):
        import twlhd_dump

        frame = bytearray(73740)
        struct.pack_into("<III", frame, 0, 1, 0, 0)
        path = self.write("v1.bin", bytes(frame))
        parsed = twlhd_dump.parse_frame(path)
        self.assertEqual(parsed.version, 1)
        self.assertEqual(parsed.cmd_count, 0)


if __name__ == "__main__":
    unittest.main()
