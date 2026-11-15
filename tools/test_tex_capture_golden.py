#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden suite for the 5C-1 texture capture planner: 36 deterministic random
scenes are rendered by tests/host/tex_capture_cli and by the independent
oracle in tools/tex_capture_oracle.py. The two texts must match byte for byte.

Scenes cover budget pressure (small TXDT/PLDT/record caps), duplicate keys,
same-key register rewrites, a format mix, format-5 windows and read failures.
"""

import os
import random
import struct
import subprocess
import unittest

import tex_capture_oracle as oracle


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.environ.get(
    "TEX_CAPTURE_CLI",
    os.path.join(ROOT, "tests", "host", "build", "tex_capture_cli"))

SCENE_COUNT = 36


def _rng(seed):
    return random.Random(0x5C10000 + seed)


def _build_scene(seed):
    rng = _rng(seed)
    txdt_cap = rng.choice([131072, 131072, 131072, 4096, 512, 64, 32768])
    pldt_cap = rng.choice([32768, 32768, 32768, 4096, 512, 64, 98304])
    max_records = rng.choice([256, 256, 16, 4, 1])

    fail = 0
    if seed % 7 == 3:
        fail |= 1
    if seed % 11 == 4:
        fail |= 2
    if seed % 13 == 5:
        fail |= 4
    if seed % 17 == 6:
        fail |= 8

    mem_size = 0x40000
    mem = bytearray(rng.getrandbits(8) for _ in range(mem_size))
    gxcm = []

    pltt = rng.choice([0, 0, 1, 2, 0x20])
    gxcm.append((oracle.CMD_PLTT_BASE, pltt))

    for _draw in range(rng.randint(2, 7)):
        fmt = rng.choice([0, 1, 2, 3, 4, 5, 6, 7])
        shift_s = rng.choice([0, 0, 0, 1, 2, 5])
        shift_t = rng.choice([0, 0, 0, 1, 2, 5])
        offset = rng.randrange(0, 0x100)
        param = oracle.param_of(fmt, shift_s, shift_t, offset)

        if fmt == 5:
            blocks = (8 << shift_s) * (8 << shift_t) // 16
            edge = rng.choice([0, 1, 2, 3, 0x3FFF])
            index_base = oracle.index_address(param)
            for block in range(blocks):
                entry = edge if rng.random() < 0.35 else 0
                mem[index_base + block * 2] = entry & 0xFF
                mem[index_base + block * 2 + 1] = (entry >> 8) & 0xFF

        if rng.random() < 0.5:
            pltt = rng.choice([0, 1, 2, 0x20])
            gxcm.append((oracle.CMD_PLTT_BASE, pltt))
        gxcm.append((oracle.CMD_TEXIMAGE_PARAM, param))

        # Some keys are set but never drawn.
        if rng.random() < 0.15:
            continue

        gxcm.append((oracle.CMD_BEGIN_VTXS, 1))
        for _vertex in range(rng.choice([1, 1, 2])):
            gxcm.append((oracle.CMD_VTX_FIRST, 0))
        gxcm.append((oracle.CMD_END_VTXS, 0))

        # A duplicate draw: sometimes without a rewrite, sometimes with one.
        if rng.random() < 0.4:
            if rng.random() < 0.5:
                gxcm.append((oracle.CMD_TEXIMAGE_PARAM, param))
                gxcm.append((oracle.CMD_PLTT_BASE, pltt))
            gxcm.append((oracle.CMD_BEGIN_VTXS, 1))
            gxcm.append((oracle.CMD_VTX_FIRST, 0))
            gxcm.append((oracle.CMD_END_VTXS, 0))

    if rng.random() < 0.3:
        gxcm.append((oracle.RAW_FIFO, 0))

    # Targeted extras: an unsupported key, a format-5 fallback and the format-5
    # window edge. They are appended after the random draws on purpose, so the
    # budget pressure from the random part is preserved.
    if seed % 4 == 0:
        # 1024x1024 16bpp = 2,097,152 texel bytes: statically unsupported.
        param = oracle.param_of(7, 7, 7, 0x20)
        gxcm += [(oracle.CMD_TEXIMAGE_PARAM, param),
                 (oracle.CMD_BEGIN_VTXS, 1),
                 (oracle.CMD_VTX_FIRST, 0),
                 (oracle.CMD_END_VTXS, 0)]
    if seed % 6 == 1:
        # Format-5 fallback: the 2-byte index scan fails, so 65,540 B.
        param = oracle.param_of(5, 0, 0, 0x40)
        index_base = oracle.index_address(param)
        for block in range(4):
            mem[index_base + block * 2] = 0
            mem[index_base + block * 2 + 1] = 0
        fail |= 8
        pldt_cap = 98304
        gxcm += [(oracle.CMD_TEXIMAGE_PARAM, param),
                 (oracle.CMD_PLTT_BASE, 0),
                 (oracle.CMD_BEGIN_VTXS, 1),
                 (oracle.CMD_VTX_FIRST, 0),
                 (oracle.CMD_END_VTXS, 0)]
    if seed % 6 == 2:
        # Format-5 edge: max entry 0x3FFF gives 4*16383 + 8 = 65,540 B.
        param = oracle.param_of(5, 0, 0, 0x80)
        index_base = oracle.index_address(param)
        for block in range(4):
            mem[index_base + block * 2] = 0xFF
            mem[index_base + block * 2 + 1] = 0x3F
        pldt_cap = 98304
        gxcm += [(oracle.CMD_TEXIMAGE_PARAM, param),
                 (oracle.CMD_PLTT_BASE, 0),
                 (oracle.CMD_BEGIN_VTXS, 1),
                 (oracle.CMD_VTX_FIRST, 0),
                 (oracle.CMD_END_VTXS, 0)]

    return {
        "txdt": txdt_cap,
        "pldt": pldt_cap,
        "records": max_records,
        "gxcm": gxcm,
        "mem": bytes(mem),
        "fail": fail,
    }


def _cli_input(scene):
    out = bytearray()
    out += struct.pack("<4I", scene["txdt"], scene["pldt"], scene["records"],
                       len(scene["gxcm"]))
    for cmd_word, value in scene["gxcm"]:
        out += struct.pack("<2I", cmd_word, value)
    out += struct.pack("<I", len(scene["mem"]))
    out += scene["mem"]
    out += struct.pack("<I", scene["fail"])
    return bytes(out)


def _expected(scene):
    result = oracle.plan(scene["txdt"], scene["pldt"], scene["records"],
                         scene["gxcm"],
                         oracle.FakeMemory(scene["mem"], scene["fail"]))
    return oracle.render(result)


def _run_cli(scene):
    proc = subprocess.run([CLI], input=_cli_input(scene),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        raise AssertionError(
            f"{CLI} exited {proc.returncode}: {proc.stderr.decode(errors='replace')}")
    return proc.stdout.decode()


class TexCaptureGoldenTest(unittest.TestCase):

    def test_cli_exists(self):
        self.assertTrue(
            os.path.isfile(CLI) and os.access(CLI, os.X_OK),
            f"tex_capture_cli binary is missing: {CLI}")

    def test_scenes_match_oracle(self):
        self.assertGreaterEqual(SCENE_COUNT, 30)
        for seed in range(SCENE_COUNT):
            with self.subTest(seed=seed):
                scene = _build_scene(seed)
                self.assertEqual(_run_cli(scene), _expected(scene),
                                 f"scene seed {seed}")


if __name__ == "__main__":
    unittest.main()
