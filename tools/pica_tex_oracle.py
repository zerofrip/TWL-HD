#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Independent oracle for the 5B-2d PICA200 RGBA8 texture swizzle.

Written from the reference sources, not from common/twlhd_pica_tex.c:

  * Texel byte order is A,B,G,R (Azahar src/common/color.h:195-199 writes
    bytes[3]=r, [2]=g, [1]=b, [0]=a; Panda3DS renderer_gl/textures.cpp:175-182
    reads data[offset]=alpha, +1=b, +2=g, +3=r).
  * Images are cut into 8x8 tiles; inside a tile the texel offset is the
    Z-order (Morton) interleave (Azahar video_core/utils.h:12-16,
    texture_codec.h:554-559; Panda3DS textures.cpp:94-107).
  * Memory tile rows are stored top-down (image row 0 is memory tile row 0).
    Azahar's linear buffer is bottom-up for OpenGL (texture_codec.h:892-897:
    "we read/write the linear buffer from the bottom up"), so its tile walk
    starts tile row 0 at linear row height-8 and DecodeTile (:577-604) maps
    in-tile row y to linear row 7-y within the tile. Because the tile base is
    at linear row height-8, tile row 0 lands on linear row height-1, i.e. the
    image top row. Panda3DS indexes memory by v directly (textures.cpp:94-107)
    and uploads row 0 first; with GL's bottom-up data layout that row is the
    image top.

So image row y is stored at tile row y//8 and in-tile row y%8.
"""

X_OFFSETS = (0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15)
Y_OFFSETS = (0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A)


class PicaTexError(Exception):
    pass


def _check_size(width, height):
    for v in (width, height):
        if v < 8 or v > 1024 or (v & (v - 1)) != 0:
            raise PicaTexError("ERR_SIZE")


def _tile_texel(width, x, y):
    """Texel index of source/image row y, column x."""
    tiles_across = width // 8
    tile_number = (y // 8) * tiles_across + (x // 8)
    inside = X_OFFSETS[x % 8] + Y_OFFSETS[y % 8]
    return tile_number * 64 + inside


def swizzle(width, height, rgba):
    """Row-major top-down RGBA8 -> PICA RGBA8 texture memory."""
    _check_size(width, height)
    if len(rgba) != width * height * 4:
        raise PicaTexError("ERR_SIZE")
    out = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            src = (y * width + x) * 4
            dest = _tile_texel(width, x, y) * 4
            # R,G,B,A -> A,B,G,R
            out[dest : dest + 4] = bytes(
                (rgba[src + 3], rgba[src + 2], rgba[src + 1], rgba[src + 0])
            )
    return out


def unswizzle(width, height, tiled):
    """PICA RGBA8 texture memory -> row-major top-down RGBA8."""
    _check_size(width, height)
    if len(tiled) != width * height * 4:
        raise PicaTexError("ERR_SIZE")
    out = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            src = _tile_texel(width, x, y) * 4
            dest = (y * width + x) * 4
            # A,B,G,R -> R,G,B,A
            out[dest : dest + 4] = bytes(
                (tiled[src + 3], tiled[src + 2], tiled[src + 1], tiled[src + 0])
            )
    return out
