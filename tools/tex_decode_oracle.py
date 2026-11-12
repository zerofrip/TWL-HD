#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Independent oracle for the 5B-2c DS texture decoder.

Written separately from DeSmuME desmume/src/texcache.cpp (the same source the C
decoder was written from), not derived from common/twlhd_tex_decode.c. It
models one texture record at a time and returns the RGBA8 pixel list, or raises
TexDecodeError with the decoder error name for the cases the C decoder rejects.

The component tables below are copied from the DeSmuME colour tables
(utils/colorspacehandler/colorspacehandler.cpp) instead of recomputing the
bit-replication formula the C side uses, so the two implementations do not
share a derivation:

  5 -> 8 bit: 00 08 10 18 21 29 31 39 42 4A 52 5A 63 6B 73 7B
              84 8C 94 9C A5 AD B5 BD C6 CE D6 DE E7 EF F7 FF
  3 -> 8 bit: 00 24 49 6D 92 B6 DB FF
"""

FIVE_TO_EIGHT = (
    0x00, 0x08, 0x10, 0x18, 0x21, 0x29, 0x31, 0x39,
    0x42, 0x4A, 0x52, 0x5A, 0x63, 0x6B, 0x73, 0x7B,
    0x84, 0x8C, 0x94, 0x9C, 0xA5, 0xAD, 0xB5, 0xBD,
    0xC6, 0xCE, 0xD6, 0xDE, 0xE7, 0xEF, 0xF7, 0xFF,
)

THREE_TO_EIGHT = (0x00, 0x24, 0x49, 0x6D, 0x92, 0xB6, 0xDB, 0xFF)

# texcache.cpp:414 texSizes[] as eighths of a byte per texel.
_TEXEL_EIGHTHS = {1: 8, 2: 2, 3: 4, 4: 8, 5: 2, 6: 8, 7: 16}
# texcache.cpp:417 paletteSizeList[] in 16-bit entries; format 5 is a window.
_PALETTE_ENTRIES = {1: 32, 2: 4, 3: 16, 4: 256, 6: 8, 7: 0}
_PALETTE_4X4_MIN = 2
_PALETTE_4X4_MAX = 65540


class TexDecodeError(Exception):
    def __init__(self, name):
        super().__init__(name)
        self.name = name


def dims(param):
    """(width, height) or None for format 0 (8 << shift, texcache.cpp:422-423)."""
    fmt = (param >> 26) & 7
    if fmt == 0:
        return None
    return (8 << ((param >> 20) & 7), 8 << ((param >> 23) & 7))


def _rgba(word, alpha):
    word &= 0x7FFF
    return (
        FIVE_TO_EIGHT[word & 0x1F],
        FIVE_TO_EIGHT[(word >> 5) & 0x1F],
        FIVE_TO_EIGHT[(word >> 10) & 0x1F],
        alpha,
    )


def _entry(palette, byte_offset):
    lo = palette[byte_offset]
    hi = palette[byte_offset + 1]
    return lo | (hi << 8)


def _check_sizes(param, texel_size, palette_size, index_size):
    fmt = (param >> 26) & 7
    if fmt == 0:
        raise TexDecodeError("ERR_FORMAT")
    width, height = dims(param)
    texels = width * height
    want_texel = (texels * _TEXEL_EIGHTHS[fmt]) >> 3
    want_index = (texels >> 3) if fmt == 5 else 0
    if texel_size != want_texel or index_size != want_index:
        raise TexDecodeError("ERR_SIZE")
    if fmt == 5:
        if palette_size < _PALETTE_4X4_MIN or palette_size > _PALETTE_4X4_MAX \
                or palette_size % 2 != 0:
            raise TexDecodeError("ERR_PALETTE_SIZE")
    elif palette_size != _PALETTE_ENTRIES[fmt] * 2:
        raise TexDecodeError("ERR_PALETTE_SIZE")
    return fmt, width, height


def _i_formats(param, texel, palette, width, height):
    """I2/I4/I8: texcache.cpp:1057-1112, 1341-1386, 1389-1405."""
    fmt = (param >> 26) & 7
    key0 = (param & 0x20000000) != 0
    per_byte = {2: 4, 3: 2, 4: 1}[fmt]
    bits = {2: 2, 3: 4, 4: 8}[fmt]
    mask = (1 << bits) - 1
    transparent = (0, 0, 0, 0)
    pixels = []
    for byte in texel:
        for j in range(per_byte):
            idx = (byte >> (j * bits)) & mask
            if key0 and idx == 0:
                pixels.append(transparent)
            else:
                pixels.append(_rgba(_entry(palette, idx * 2), 255))
    assert len(pixels) == width * height
    return pixels


def _mode_colours(mode, entries):
    """The four block colours as RGBA8 (texcache.cpp:1795-1860)."""
    if mode == 0:
        return [
            _rgba(entries[0], 255),
            _rgba(entries[1], 255),
            _rgba(entries[2], 255),
            (0, 0, 0, 0),
        ]
    if mode == 1:
        first = _rgba(entries[0], 255)
        second = _rgba(entries[1], 255)
        average = tuple(
            (first[channel] + second[channel]) // 2 for channel in range(3)
        ) + (255,)
        return [first, second, average, (0, 0, 0, 0)]
    if mode == 2:
        return [_rgba(entries[entry], 255) for entry in range(4)]

    first = _rgba(entries[0], 255)
    second = _rgba(entries[1], 255)
    colours = []
    for weights in ((5, 3), (3, 5)):
        word = 0
        for channel in range(3):
            blended = (first[channel] * weights[0] +
                       second[channel] * weights[1]) >> 6
            word |= blended << (5 * channel)
        colours.append(_rgba(word, 255))
    return [first, second] + colours


def _four_by_four(param, texel, palette, palette_size, index, width, height):
    """4x4 compressed blocks: texcache.cpp:1752, 1789-1843."""
    blocks_x = width // 4
    blocks_y = height // 4
    pixels = [None] * (width * height)
    # Pre-scan so a palette overrun is an error before anything is produced.
    for block in range(blocks_x * blocks_y):
        word = _entry(index, block * 2)
        offset = (word & 0x3FFF) * 4
        mode = word >> 14
        needed = {0: 3, 2: 4}.get(mode, 2)
        if offset + needed * 2 > palette_size:
            raise TexDecodeError("ERR_PALETTE_RANGE")

    for block in range(blocks_x * blocks_y):
        block_x = block % blocks_x
        block_y = block // blocks_x
        base = block * 4
        block_data = (texel[base] | (texel[base + 1] << 8) |
                      (texel[base + 2] << 16) | (texel[base + 3] << 24))
        word = _entry(index, block * 2)
        offset = (word & 0x3FFF) * 4
        mode = word >> 14
        needed = {0: 3, 2: 4}.get(mode, 2)
        entries = [_entry(palette, offset + k * 2) for k in range(needed)]
        colours = _mode_colours(mode, entries)
        for sy in range(4):
            row = (block_data >> (8 * sy)) & 0xFF
            y = block_y * 4 + sy
            for sx in range(4):
                value = (row >> (2 * sx)) & 3
                pixels[y * width + block_x * 4 + sx] = colours[value]
    return pixels


def decode(param, texel, palette, index, texel_size=None, palette_size=None,
           index_size=None):
    """Decode one record to [(r, g, b, a), ...] (row-major, top row first)."""
    if texel_size is None:
        texel_size = len(texel)
    if palette_size is None:
        palette_size = len(palette) if palette is not None else 0
    if index_size is None:
        index_size = len(index) if index is not None else 0

    fmt, width, height = _check_sizes(param, texel_size, palette_size,
                                      index_size)
    texel = texel[:texel_size]
    if palette is not None:
        palette = palette[:palette_size]
    if index is not None:
        index = index[:index_size]
    if fmt == 7:
        pixels = []
        for i in range(width * height):
            word = texel[i * 2] | (texel[i * 2 + 1] << 8)
            pixels.append(_rgba(word, 255) if word & 0x8000
                          else (0, 0, 0, 0))
        return pixels

    if palette is None:
        raise TexDecodeError("ERR_NULL")
    if fmt in (2, 3, 4):
        return _i_formats(param, texel, palette, width, height)
    if fmt == 1:
        # A3I5: texcache.cpp:1524-1525, index bits 0-4, alpha bits 5-7.
        return [
            _rgba(_entry(palette, (byte & 0x1F) * 2),
                  THREE_TO_EIGHT[byte >> 5])
            for byte in texel
        ]
    if fmt == 6:
        # A5I3: texcache.cpp:1745-1746, index bits 0-2, alpha bits 3-7.
        return [
            _rgba(_entry(palette, (byte & 0x07) * 2),
                  FIVE_TO_EIGHT[byte >> 3])
            for byte in texel
        ]
    if index is None:
        raise TexDecodeError("ERR_NULL")
    return _four_by_four(param, texel, palette, palette_size, index, width,
                         height)


def pack_rgb555(r, g, b, top=0):
    """Helper for tests: pack a 5-bit-per-channel colour."""
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10) | (top << 15)
