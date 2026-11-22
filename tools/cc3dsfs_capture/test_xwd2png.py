#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/cc3dsfs_capture/xwd2png.py.

Builds a synthetic 32bpp ZPixmap XWD (BGRX, padded rows) in a temp directory,
runs the converter as a subprocess and checks the PNG pixel-exactly. Skips with
a visible reason when PIL is not installed.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover \
        -s tools/cc3dsfs_capture -p 'test_xwd2png.py'
"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CONVERTER = os.path.join(HERE, "xwd2png.py")

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False


def xwd_bytes(width, height, pixels):
    """25 big-endian uint32 XWD header + 32bpp BGRX rows padded to 4*width+4."""
    stride = 4 * width + 4
    hs = [0] * 25
    hs[0] = 100          # header size
    hs[1] = 7            # file version
    hs[2] = 2            # ZPixmap
    hs[3] = 24           # depth
    hs[4] = width
    hs[5] = height
    hs[11] = 32          # bits per pixel
    hs[12] = stride      # bytes per line
    hs[19] = 0           # index the converter reads as ncolours
    data = bytearray(struct.pack(">25I", *hs))
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[y * width + x]
            data += bytes((b, g, r, 255))
        data += b"\x00" * (stride - 4 * width)
    return bytes(data)


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; xwd2png conversion test skipped")
class Xwd2PngTest(unittest.TestCase):
    def test_converts_bgrx_pixel_exact(self):
        width, height = 3, 2
        pixels = [
            (0, 0, 0), (255, 0, 0), (0, 255, 0),
            (0, 0, 255), (255, 255, 255), (17, 34, 51),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "in.xwd")
            dst = os.path.join(tmp, "out.png")
            with open(src, "wb") as f:
                f.write(xwd_bytes(width, height, pixels))
            res = subprocess.run(
                [sys.executable, CONVERTER, src, dst],
                capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout.split(), [str(width), str(height), "32"])
            with Image.open(dst) as img:
                self.assertEqual(img.size, (width, height))
                self.assertEqual(img.mode, "RGB")
                self.assertEqual(
                    img.tobytes(),
                    bytes(v for p in pixels for v in p))


if __name__ == "__main__":
    unittest.main()
