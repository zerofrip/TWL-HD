#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert a ZPixmap XWD screenshot to PNG.

Usage: xwd2png.py <in.xwd> <out.png>

Written for `xwd -root` output from an Xvfb screen. The header's
bits_per_pixel selects the byte layout: 24 -> 3-byte BGR, 32 -> 4-byte BGRX
(as before); rows are padded to the header's bytes_per_line. Any other depth
exits non-zero, as does a file shorter than the header claims.
"""
import struct
import sys

from PIL import Image

# bits per pixel -> (frombuffer mode, raw byte order)
RAW_MODES = {24: ("RGB", "BGR"), 32: ("RGBX", "BGRX")}


def fail(message):
    sys.stderr.write("xwd2png: %s\n" % message)
    sys.exit(2)


def main(argv):
    if len(argv) != 3:
        fail("usage: xwd2png.py <in.xwd> <out.png>")
    with open(argv[1], 'rb') as f:
        d = f.read()
    if len(d) < 100:
        fail("file too short for a 100-byte XWD header (%d bytes)" % len(d))
    hs = struct.unpack('>25I', d[:100])
    hsize, w, h = hs[0], hs[4], hs[5]
    bpp = hs[11]; bpl = hs[12]; ncol = hs[19]
    off = hsize + ncol * 12
    if bpp not in RAW_MODES:
        fail("unsupported bits_per_pixel %d (only 24 or 32)" % bpp)
    need = off + bpl * h
    if bpl < 1 or len(d) < need:
        fail("file too short: %dx%d at %d bpp needs %d bytes, have %d"
             % (w, h, bpp, need, len(d)))
    mode, rawmode = RAW_MODES[bpp]
    px = d[off:off + bpl * h]
    img = Image.frombuffer(mode, (w, h), px, 'raw', rawmode, bpl, 1)
    img.convert('RGB').save(argv[2])
    print(w, h, bpp)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
