#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/cc3dsfs_capture/xwd2png.py.

Builds synthetic ZPixmap XWDs (32bpp BGRX and 24bpp BGR, padded rows) in a
temp directory, runs the converter as a subprocess and checks the PNG
pixel-exactly. Also pins the old 4-byte decoding of a 24-bpp file (it must not
equal the correct image) and checks that an unsupported bpp exits non-zero.
Skips with a visible reason when PIL is not installed.

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


def xwd_bytes(width, height, pixels, bpp=32):
    """25 big-endian uint32 XWD header + padded pixel rows.

    32 bpp: 4-byte BGRX rows padded to 4*width+4.
    24 bpp: 3-byte BGR rows padded to 3*width+8 (stride > 3*width).
    """
    if bpp == 32:
        stride = 4 * width + 4
    elif bpp == 24:
        stride = 3 * width + 8
    else:
        raise ValueError("xwd_bytes: unsupported bpp %r" % (bpp,))
    hs = [0] * 25
    hs[0] = 100          # header size
    hs[1] = 7            # file version
    hs[2] = 2            # ZPixmap
    hs[3] = 24           # depth
    hs[4] = width
    hs[5] = height
    hs[11] = bpp         # bits per pixel
    hs[12] = stride      # bytes per line
    hs[19] = 0           # index the converter reads as ncolours
    data = bytearray(struct.pack(">25I", *hs))
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[y * width + x]
            if bpp == 32:
                data += bytes((b, g, r, 255))
            else:
                data += bytes((b, g, r))
        data += b"\x00" * (stride - (bpp // 8) * width)
    return bytes(data)


def old_bgrx_decode(width, height, data, stride):
    """The pre-fix decoder body: always 4-byte BGRX, never 3-byte BGR."""
    off = 100
    px = data[off:off + stride * height]
    return Image.frombuffer(
        'RGBX', (width, height), px, 'raw', 'BGRX', stride, 1).convert('RGB')


def build_xwd(path, width, height, pixels, bpp=32):
    with open(path, "wb") as f:
        f.write(xwd_bytes(width, height, pixels, bpp))


def convert(src, dst):
    return subprocess.run(
        [sys.executable, CONVERTER, src, dst], capture_output=True, text=True)


PIXELS = [
    (0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255),
    (255, 255, 255), (17, 34, 51), (66, 69, 66), (255, 0, 255),
]


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; xwd2png conversion test skipped")
class Xwd2PngTest(unittest.TestCase):
    def test_converts_bgrx_pixel_exact(self):
        width, height = 4, 2
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "in.xwd")
            dst = os.path.join(tmp, "out.png")
            build_xwd(src, width, height, PIXELS, bpp=32)
            res = convert(src, dst)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout.split(), [str(width), str(height), "32"])
            with Image.open(dst) as img:
                self.assertEqual(img.size, (width, height))
                self.assertEqual(img.mode, "RGB")
                self.assertEqual(img.tobytes(),
                                 bytes(v for p in PIXELS for v in p))

    def test_converts_24bpp_bgr_pixel_exact(self):
        width, height = 4, 2
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "in.xwd")
            dst = os.path.join(tmp, "out.png")
            build_xwd(src, width, height, PIXELS, bpp=24)
            res = convert(src, dst)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout.split(), [str(width), str(height), "24"])
            with Image.open(dst) as img:
                self.assertEqual(img.size, (width, height))
                self.assertEqual(img.mode, "RGB")
                self.assertEqual(img.tobytes(),
                                 bytes(v for p in PIXELS for v in p))

    def test_old_bgrx_decode_of_24bpp_is_wrong(self):
        """Pin the bug: 4-byte BGRX on 24-bpp data is not the correct image."""
        width, height = 4, 2
        data = xwd_bytes(width, height, PIXELS, bpp=24)
        stride = 3 * width + 8
        old = old_bgrx_decode(width, height, data, stride)
        correct = bytes(v for p in PIXELS for v in p)
        self.assertNotEqual(old.tobytes(), correct)

    def test_unsupported_bpp_exits_nonzero(self):
        width, height = 2, 1
        data = bytearray(xwd_bytes(width, height, [(0, 0, 0)] * 2, bpp=32))
        struct.pack_into(">I", data, 11 * 4, 16)  # bits_per_pixel = 16
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "in.xwd")
            dst = os.path.join(tmp, "out.png")
            with open(src, "wb") as f:
                f.write(data)
            res = convert(src, dst)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unsupported bits_per_pixel 16", res.stderr)


if __name__ == "__main__":
    unittest.main()
