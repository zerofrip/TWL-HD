#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert a 32bpp ZPixmap XWD screenshot to PNG.

Usage: xwd2png.py <in.xwd> <out.png>

Written for `xwd -root` output from an Xvfb screen: 32 bits per pixel,
BGRX byte order, rows padded to the header's bytes-per-line.
"""
import struct
import sys

from PIL import Image

with open(sys.argv[1], 'rb') as f:
    d = f.read()
hs = struct.unpack('>25I', d[:100])
hsize, fv, fmt, depth, w, h = hs[0], hs[1], hs[2], hs[3], hs[4], hs[5]
bpp = hs[11]; bpl = hs[12]; ncol = hs[19]
off = hsize + ncol * 12
px = d[off:off + bpl * h]
img = Image.frombuffer('RGBX', (w, h), px, 'raw', 'BGRX', bpl, 1).convert('RGB')
img.save(sys.argv[2])
print(w, h, bpp)
