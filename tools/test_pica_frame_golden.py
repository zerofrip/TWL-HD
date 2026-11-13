#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden test for the 5B-2e-1 textured frame preparation: run the C CLI on
generated frames and compare its full result section against
tools/pica_frame_oracle.py.

The frames cover repeated textures (non-consecutive runs), paletted formats,
4x4 compressed textures, non-square sizes, an untextured run and a run whose
(param, pltt) key has no TXDS record (per-entry NO_DATA). Every frame is built
from the real parsed-view records the CLI echoes, so both sides decode the same
byte ranges.
"""

import os
import random
import struct
import subprocess
import unittest

import pica_frame_oracle


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The mutation runner points this at a scratch build of the same CLI.
CLI = os.environ.get("PICA_FRAME_CLI") or os.path.join(
    ROOT, "tests", "host", "build", "pica_frame_cli"
)

_EIGHTHS = {1: 8, 2: 2, 3: 4, 4: 8, 5: 2, 6: 8, 7: 16}
_PALETTE_ENTRIES = {1: 32, 2: 4, 3: 16, 4: 256, 6: 8, 7: 0}


def _param(fmt, ss=0, st=0, repeat=0, flip=0, key0=0):
    return ((fmt << 26) | (ss << 20) | (st << 23) | (repeat << 16) |
            (flip << 18) | (key0 << 29))


def _dims(param):
    return (8 << ((param >> 20) & 7), 8 << ((param >> 23) & 7))


def _make_entry(rng, param, pltt):
    fmt = (param >> 26) & 7
    width, height = _dims(param)
    pixels = width * height
    texel_size = (pixels * _EIGHTHS[fmt]) // 8
    index_size = (pixels >> 3) if fmt == 5 else 0

    texel = bytes(rng.getrandbits(8) for _ in range(texel_size))
    if fmt == 5:
        palette_size = 8
        palette = bytes(rng.getrandbits(8) for _ in range(palette_size))
        # Every block: mode 1, palette offset 0 -> needs 4 of the 8 bytes.
        index = b"".join(struct.pack("<H", 0x4000)
                         for _ in range(pixels // 16))
    else:
        palette_size = _PALETTE_ENTRIES[fmt] * 2
        palette = bytes(rng.getrandbits(8)
                        for _ in range(palette_size))
        index = b""

    return {
        "param": param,
        "pltt": pltt,
        "texel": texel,
        "palette": palette,
        "index": index,
    }


def _build_frame(rng, runs):
    """runs: list of None (untextured), (param, pltt) or ('missing', p, pltt)."""
    order = []
    entries = []
    seen = set()
    vertices = []

    for run in runs:
        if run is None:
            key = 0
            pltt = 0
        elif run[0] == "missing":
            key = run[1]
            pltt = run[2]
        else:
            key, pltt = run
            if (key, pltt) not in seen:
                seen.add((key, pltt))
                order.append((key, pltt))
                entries.append(_make_entry(rng, key, pltt))

        width, height = _dims(key) if key else (8, 8)
        for _ in range(3):
            s = rng.randrange(0, max(2 * width, 16)) / 2.0
            t = rng.randrange(0, max(2 * height, 16)) / 2.0
            vertices.append((s, t, key, pltt))

    return vertices, entries


def _pack_description(vertices, entries):
    blob = bytearray()
    blob += struct.pack("<I", len(vertices))
    for (s, t, param, pltt) in vertices:
        blob += struct.pack("<ffII", s, t, param, pltt)
    blob += struct.pack("<I", len(entries))
    for entry in entries:
        blob += struct.pack(
            "<IIIII", entry["param"], entry["pltt"], len(entry["texel"]),
            len(entry["palette"]), len(entry["index"]))
        blob += entry["texel"] + entry["palette"] + entry["index"]
    return bytes(blob)


def _run_cli(payload):
    proc = subprocess.run(
        [CLI],
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(
            "cli exit %d: %s" % (proc.returncode,
                                  proc.stderr.decode("utf-8", "replace")))
    return proc.stdout.decode("ascii")


def _parse_input(text):
    lines = text.splitlines()
    records = []
    txdt = b""
    pldt = b""
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("record "):
            fields = {}
            for item in line.split()[2:]:
                key, value = item.split("=")
                fields[key] = int(value)
            records.append(fields)
            i += 1
        elif line.startswith("txdt "):
            size = int(line.split()[1])
            txdt = bytes.fromhex(lines[i + 1])
            if len(txdt) != size:
                raise AssertionError("txdt length %d != %d" %
                                     (len(txdt), size))
            i += 2
        elif line.startswith("pldt "):
            size = int(line.split()[1])
            pldt = bytes.fromhex(lines[i + 1])
            if len(pldt) != size:
                raise AssertionError("pldt length %d != %d" %
                                     (len(pldt), size))
            i += 2
        else:
            i += 1
    return records, txdt, pldt


def _frame_specs():
    a = _param(7, repeat=1)          # 8x8, repeat S+T
    b = _param(3, ss=1)              # 16x8 I4
    c = _param(2)                    # 8x8 I2
    d = _param(4, ss=2, st=1)        # 32x8 I8
    e = _param(1, ss=1, st=1)        # 16x16 A3I5
    f = _param(6, st=2)              # 8x32 A5I3
    g = _param(5, ss=1, st=1)        # 16x16 4x4
    h = _param(7, ss=3, st=1)        # 64x8 16bpp
    i = _param(4, st=3)              # 8x64 I8
    missing = _param(7)              # no record with pltt 0 in this frame
    m2 = _param(7, st=1)             # 8x16 key that no record carries
    big_w = _param(7, ss=7)          # 1024x8, the largest width
    big_h = _param(7, st=7)          # 8x1024, the largest height

    return [
        [(a, 0), (b, 1), None, (a, 0)],
        [(c, 2), (c, 2), (b, 1), None, (c, 2)],
        [(e, 5), (g, 7), (f, 6), (g, 7)],
        [(d, 4), ("missing", missing, 0), (i, 9), None],
        [(h, 8), (h, 8), (i, 9), (d, 4), (a, 0)],
        # Same pltt with a different parameter, and the same parameter with a
        # different pltt: both halves of the dedupe key must be compared.
        [(a, 0), (b, 0), (a, 3), None, (a, 0)],
        # Largest DS width/height keep the defensive PICA size check honest.
        [(big_w, 11), (big_h, 12), (big_w, 11)],
        # An untextured run before any textured run: it must not consume a
        # texture slot (the CLI sizes the texture buffer to the entry count).
        [None, (a, 0), (b, 1)],
        # A NO_DATA run before any textured run, untextured last.
        [("missing", m2, 0), (c, 2), None],
    ]


class PicaFrameGoldenTest(unittest.TestCase):
    def test_matches_oracle(self):
        for seed, runs in enumerate(_frame_specs()):
            with self.subTest(seed=seed):
                rng = random.Random(1000 + seed)
                vertices, entries = _build_frame(rng, runs)
                output = _run_cli(_pack_description(vertices, entries))
                if "# result\n" not in output:
                    raise AssertionError("missing result section")
                input_text, result_text = output.split("# result\n", 1)
                records, txdt, pldt = _parse_input(input_text)
                expected = pica_frame_oracle.render_result(
                    vertices, records, txdt, pldt)
                self.assertEqual(result_text, expected)


if __name__ == "__main__":
    unittest.main()
