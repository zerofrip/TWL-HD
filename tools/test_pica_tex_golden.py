#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Golden test for the 5B-2d PICA texture swizzle: run the C CLI on generated
RGBA8 images and compare every output byte against tools/pica_tex_oracle.py.

Sizes cover a single tile (8x8), non-square in both directions (16x8 completes
the width on purpose; add 8x16 for the vertical tile-row case), a multi-tile
image (64x32) and the widest small image (1024x8). Corner and edge pixels are
pinned to marker values on top of seeded random content.
"""

import os
import random
import subprocess
import unittest

import pica_tex_oracle


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The mutation runner points this at a scratch build of the same CLI.
CLI = os.environ.get("PICA_TEX_CLI") or os.path.join(
    ROOT, "tests", "host", "build", "pica_tex_cli"
)


def _rng(seed):
    return random.Random(seed)


def _image(width, height, seed):
    rng = _rng(seed)
    data = bytearray(rng.getrandbits(8) for _ in range(width * height * 4))
    # Pin the four corners and the first/last pixel of every edge so wrong
    # tile-row or within-tile ordering is visible even for tiny images.
    corners = [
        0,
        width - 1,
        (height - 1) * width,
        width * height - 1,
    ]
    for k, pixel in enumerate(corners):
        marker = 0x10 + k * 0x30
        for c in range(4):
            data[pixel * 4 + c] = (marker + c) & 0xFF
    for x in range(width):
        data[x * 4] = 0xF0
        data[((height - 1) * width + x) * 4 + 1] = 0x0F
    for y in range(height):
        data[(y * width) * 4 + 2] = 0xAA
        data[(y * width + width - 1) * 4 + 3] = 0x55
    return bytes(data)


def _run_cli(mode, width, height, payload):
    proc = subprocess.run(
        [CLI, mode, str(width), str(height)],
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(
            "cli exit %d: %s" % (proc.returncode, proc.stderr.decode("utf-8", "replace"))
        )
    lines = proc.stdout.decode("ascii").splitlines()
    if not lines or lines[0] != "result=OK":
        raise AssertionError("cli result: %r" % (lines[:1],))
    out = bytearray(len(lines) - 1)
    for i, line in enumerate(lines[1:]):
        parts = line.split()
        if len(parts) != 3 or parts[0] != "byte":
            raise AssertionError("bad line %r" % (line,))
        if int(parts[1]) != i:
            raise AssertionError("out of order byte %r" % (line,))
        out[i] = int(parts[2])
    return out


class PicaTexGoldenTest(unittest.TestCase):
    SIZES = ((8, 8), (16, 8), (8, 16), (64, 32), (1024, 8))

    def test_swizzle_matches_oracle(self):
        for seed, (width, height) in enumerate(self.SIZES):
            with self.subTest(size=(width, height)):
                image = _image(width, height, 100 + seed)
                expected = pica_tex_oracle.swizzle(width, height, image)
                actual = _run_cli("swizzle", width, height, image)
                self.assertEqual(len(actual), len(expected))
                self.assertEqual(actual, expected)

    def test_unswizzle_matches_oracle(self):
        for seed, (width, height) in enumerate(self.SIZES):
            with self.subTest(size=(width, height)):
                image = _image(width, height, 200 + seed)
                tiled = pica_tex_oracle.swizzle(width, height, image)
                actual = _run_cli("unswizzle", width, height, tiled)
                self.assertEqual(actual, image)
                self.assertEqual(
                    pica_tex_oracle.unswizzle(width, height, tiled), image
                )

    def test_roundtrip_c_through_oracle(self):
        for seed, (width, height) in enumerate(self.SIZES):
            with self.subTest(size=(width, height)):
                image = _image(width, height, 300 + seed)
                tiled = _run_cli("swizzle", width, height, image)
                self.assertEqual(
                    pica_tex_oracle.unswizzle(width, height, tiled), image
                )


if __name__ == "__main__":
    unittest.main()
