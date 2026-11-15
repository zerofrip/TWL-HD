#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent Python implementation of the TWL-HD v2 capture format.

The normative description is docs/TWLHD_V2_FORMAT.md. This module deliberately
shares no code with the C implementation; the agreement suite
(tools/test_twlhd_v2.py) compares the two over a fixed-seed corpus.

Stdlib only. Everything here is UNIT VERIFIED (host analysis); no hardware is
involved.
"""

import argparse
import struct
import sys
from collections import namedtuple

VERSION = 2
HEADER_SIZE = 32
CHUNK_HEADER_SIZE = 16
FILE_MAX = 1048576
GXCM_MAX_ENTRIES = 65536
RSTA_MAX_ENTRIES = 4096
CHUNK_COUNT_MAX = 16
META_PAYLOAD_SIZE = 16
RAW_FIFO_BIT = 0x00010000

FLAG_FRAME_COMPLETE = 0x00000001
FLAG_OVERFLOW = 0x00000002
FLAG_RESERVED_MASK = 0xFFFFFFFC

CHUNK_FLAG_REQUIRED = 0x00000001
CHUNK_FLAG_RESERVED_MASK = 0xFFFFFFFE

COVERAGE_RESERVED_MASK = 0xFFFFFFF8

_FNV1A_64_BASIS = 0xcbf29ce484222325
_FNV1A_64_PRIME = 0x100000001b3


def _fnv1a_64(data):
    hash_ = _FNV1A_64_BASIS
    for byte in data:
        hash_ ^= byte
        hash_ = (hash_ * _FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return hash_


def _fnv1a_64_u32(hash_, value):
    hash_ ^= value & 0xFF
    hash_ = (hash_ * _FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    hash_ ^= (value >> 8) & 0xFF
    hash_ = (hash_ * _FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    hash_ ^= (value >> 16) & 0xFF
    hash_ = (hash_ * _FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    hash_ ^= (value >> 24) & 0xFF
    hash_ = (hash_ * _FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return hash_


def digest(data, view):
    """64-bit FNV-1a over header bytes then every GXCM/RSTA field."""
    hash_ = _fnv1a_64(data[:HEADER_SIZE])
    for word, value in view.gxcm:
        hash_ = _fnv1a_64_u32(hash_, word)
        hash_ = _fnv1a_64_u32(hash_, value)
    for addr, value, gx_index, width in view.rsta:
        hash_ = _fnv1a_64_u32(hash_, addr)
        hash_ = _fnv1a_64_u32(hash_, value)
        hash_ = _fnv1a_64_u32(hash_, gx_index)
        hash_ = _fnv1a_64_u32(hash_, width)
    return hash_


def fourcc(text):
    raw = text.encode("ascii")
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24)


GXCM = fourcc("GXCM")
META = fourcc("META")
RSTA = fourcc("RSTA")
TXDS = fourcc("TXDS")
TXDT = fourcc("TXDT")
PLDT = fourcc("PLDT")
KNOWN_CHUNKS = {GXCM, META, RSTA, TXDS, TXDT, PLDT}

# Reserved, not implemented: treated as unknown chunks.
RESERVED_CHUNKS = {fourcc("CLPM")}

TXDS_RECORD_SIZE = 40
TXDS_MAX_RECORDS = 256
TXDT_MAX_BYTES = 524288
PLDT_MAX_BYTES = 98304
TEX_PALETTE_4X4_MIN_BYTES = 2
TEX_PALETTE_4X4_MAX_BYTES = 65540

# Command id -> entries per command (one entry per port write).
# A zero count still consumes exactly one entry.
CMD_ENTRY_COUNTS = {
    0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0,
    0x16: 16, 0x17: 12, 0x18: 16, 0x19: 12, 0x1A: 9, 0x1B: 3, 0x1C: 3,
    0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1, 0x26: 1,
    0x27: 1, 0x28: 1, 0x29: 1, 0x2A: 1, 0x2B: 1,
    0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1, 0x34: 32,
    0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1,
}

RSTA_RANGES = (
    (0x04000060, 0x04000062),  # DISP3DCNT
    (0x04000330, 0x04000340),  # edge colours + clear poly id
    (0x04000340, 0x04000342),  # alpha test
    (0x04000350, 0x04000358),  # clear colour / depth / image offset
    (0x04000358, 0x04000360),  # fog colour / offset
    (0x04000360, 0x04000380),  # fog table
    (0x04000380, 0x040003C0),  # toon table
    (0x04000610, 0x04000612),  # 1-dot depth
)

View = namedtuple(
    "View",
    "total_size flags frame_index chunk_count unknown_chunks_skipped "
    "gxcm rsta meta_backend_id meta_coverage_flags "
    "tex_records txdt pldt")

# One TXDS descriptor: the 8 documented u32 fields.
TexRecord = namedtuple(
    "TexRecord",
    "teximage_param pltt_base texel_offset texel_size "
    "palette_offset palette_size index_offset index_size")


class V2Error(Exception):
    """A rejection with the same result name the C parser reports."""

    def __init__(self, result, message):
        super().__init__("%s: %s" % (result, message))
        self.result = result
        self.message = message


def _u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _entry_count(cmd_id):
    count = CMD_ENTRY_COUNTS[cmd_id]
    return count if count else 1


def _rsta_whitelisted(addr, width):
    if width not in (1, 2, 4):
        return False
    if addr % width:
        return False
    end = addr + width
    return any(start <= addr and end <= stop for start, stop in RSTA_RANGES)


def _is_raw(word):
    return word == RAW_FIFO_BIT


def _validate_gxcm(payload, flags):
    if len(payload) % 8:
        raise V2Error("ERR_GXCM", "GXCM payload is not a multiple of 8")

    count = len(payload) // 8
    if count == 0 or count > GXCM_MAX_ENTRIES:
        raise V2Error("ERR_GXCM", "GXCM entry count out of range")

    words = [struct.unpack_from("<II", payload, i * 8) for i in range(count)]

    # Pass 1: cmd_word layout and command id validity.
    for word, _value in words:
        if word == RAW_FIFO_BIT:
            continue
        if word & RAW_FIFO_BIT:
            raise V2Error("ERR_GXCM_CMD", "RAW_FIFO bit with other bits")
        if word & 0xFFFFFF00:
            raise V2Error("ERR_GXCM_CMD", "reserved cmd_word bits set")
        if (word & 0xFF) not in CMD_ENTRY_COUNTS:
            raise V2Error("ERR_GXCM_CMD",
                          "invalid command id 0x%02X" % (word & 0xFF))

    # Pass 2: strict grouping.
    i = 0
    while i < count:
        word = words[i][0]
        if _is_raw(word):
            i += 1
            continue

        cmd_id = word & 0xFF
        expected = _entry_count(cmd_id)
        j = i
        while j < count and j - i < expected:
            nxt = words[j][0]
            if _is_raw(nxt) or (nxt & 0xFF) != cmd_id:
                break
            j += 1

        if j - i == expected:
            i = j
            continue

        if j == count and flags & FLAG_OVERFLOW:
            break
        raise V2Error("ERR_GXCM_GROUP", "incomplete command group")

    # Pass 3: SWAP_BUFFERS last, iff FRAME_COMPLETE.
    for i, (word, _value) in enumerate(words):
        if not _is_raw(word) and (word & 0xFF) == 0x50 and i + 1 != count:
            raise V2Error("ERR_GXCM_FRAME", "SWAP_BUFFERS is not the last entry")

    last_swap = (not _is_raw(words[-1][0]) and
                 (words[-1][0] & 0xFF) == 0x50)
    complete = bool(flags & FLAG_FRAME_COMPLETE)
    if complete != last_swap:
        raise V2Error("ERR_GXCM_FRAME",
                      "FRAME_COMPLETE must be set iff the last entry is SWAP")

    return words


def _validate_rsta(payload, gxcm_count):
    if len(payload) % 16:
        raise V2Error("ERR_RSTA", "RSTA payload is not a multiple of 16")

    count = len(payload) // 16
    if count > RSTA_MAX_ENTRIES:
        raise V2Error("ERR_RSTA", "RSTA entry count out of range")

    entries = []
    previous_index = 0
    for i in range(count):
        addr, value, gx_index, width = struct.unpack_from(
            "<IIII", payload, i * 16)
        if not _rsta_whitelisted(addr, width):
            raise V2Error("ERR_RSTA",
                          "RSTA entry %d is outside the whitelist" % i)
        if width == 1 and value & ~0xFF:
            raise V2Error("ERR_RSTA",
                          "RSTA 1-byte value has high bits set")
        if width == 2 and value & ~0xFFFF:
            raise V2Error("ERR_RSTA",
                          "RSTA 2-byte value has high bits set")
        if gx_index > gxcm_count:
            raise V2Error("ERR_RSTA", "RSTA gx_index past the GXCM end")
        if i and gx_index < previous_index:
            raise V2Error("ERR_RSTA", "RSTA gx_index is not monotonic")
        previous_index = gx_index
        entries.append((addr, value, gx_index, width))

    return entries


def _validate_meta(payload):
    if len(payload) != META_PAYLOAD_SIZE:
        raise V2Error("ERR_META", "META payload must be exactly 16 bytes")

    backend, coverage, reserved0, reserved1 = struct.unpack("<IIII", payload)
    if coverage & COVERAGE_RESERVED_MASK:
        raise V2Error("ERR_META", "META coverage reserved bits set")
    if reserved0 or reserved1:
        raise V2Error("ERR_META", "META reserved words are not zero")
    return backend, coverage


def tex_format(teximage_param):
    """Packed format from bits 26..28 (0 none, 1 A3I5, 2 I2, 3 I4, ...)."""
    return (teximage_param >> 26) & 7


def tex_sizes(teximage_param):
    """Mirror of twlhd_v2_tex_sizes(): (texel, palette, index) byte counts.

    Returns None for format 0 (no texture, no record).
    """
    texel_eighths = (0, 8, 2, 4, 8, 2, 8, 16)
    palette_by_format = (0, 64, 8, 32, 512, 0, 16, 0)
    fmt = (teximage_param >> 26) & 7
    if fmt == 0:
        return None

    width = 8 << ((teximage_param >> 20) & 7)
    height = 8 << ((teximage_param >> 23) & 7)
    texels = width * height
    texel_bytes = texels * texel_eighths[fmt] >> 3
    palette_bytes = palette_by_format[fmt]
    index_bytes = texels >> 3 if fmt == 5 else 0
    return texel_bytes, palette_bytes, index_bytes


def _tex_palette_size_ok(teximage_param, palette_bytes):
    sizes = tex_sizes(teximage_param)
    if sizes is None:
        return False
    if (teximage_param >> 26) & 7 == 5:
        return (TEX_PALETTE_4X4_MIN_BYTES <= palette_bytes <=
                TEX_PALETTE_4X4_MAX_BYTES and palette_bytes % 2 == 0)
    return palette_bytes == sizes[1]


def _validate_tex(txds, txdt, pldt):
    """Mirror of validate_tex(): the C result order is transcribed exactly."""
    if len(txds) % TXDS_RECORD_SIZE:
        raise V2Error("ERR_TXDS", "TXDS payload is not a multiple of 40")

    count = len(txds) // TXDS_RECORD_SIZE
    if count == 0 or count > TXDS_MAX_RECORDS:
        raise V2Error("ERR_TXDS", "TXDS record count out of range")

    records = []
    for i in range(count):
        fields = struct.unpack_from("<10I", txds, i * TXDS_RECORD_SIZE)
        if fields[8] or fields[9]:
            raise V2Error("ERR_TXDS", "TXDS reserved words are not zero")
        if tex_sizes(fields[0]) is None:
            raise V2Error("ERR_TXDS", "TXDS record has texture format 0")
        records.append(TexRecord(*fields[:8]))

    for i, a in enumerate(records):
        for b in records[:i]:
            if a.teximage_param == b.teximage_param and \
                    a.pltt_base == b.pltt_base:
                raise V2Error("ERR_TXDS_KEY",
                              "duplicate (teximage_param, pltt_base) key")

    need_pldt = False
    for rec in records:
        texel_bytes, _palette, index_bytes = tex_sizes(rec.teximage_param)
        if rec.texel_size != texel_bytes or \
                not _tex_palette_size_ok(rec.teximage_param,
                                         rec.palette_size) or \
                rec.index_size != index_bytes:
            raise V2Error("ERR_TXDS_SIZE",
                          "TXDS size does not match the formula")
        if rec.texel_size == 0 and rec.texel_offset != 0:
            raise V2Error("ERR_TXDS_SIZE", "TXDS zero texel size with offset")
        if rec.palette_size == 0 and rec.palette_offset != 0:
            raise V2Error("ERR_TXDS_SIZE", "TXDS zero palette size with offset")
        if rec.index_size == 0 and rec.index_offset != 0:
            raise V2Error("ERR_TXDS_SIZE", "TXDS zero index size with offset")
        if rec.palette_size != 0:
            need_pldt = True

    if txdt is None:
        raise V2Error("ERR_MISSING_CHUNK", "TXDS is present without TXDT")
    if need_pldt and pldt is None:
        raise V2Error("ERR_MISSING_CHUNK", "TXDS needs a PLDT chunk")

    txdt_size = len(txdt)
    pldt_size = len(pldt) if pldt is not None else 0
    for rec in records:
        if rec.texel_offset + rec.texel_size > txdt_size:
            raise V2Error("ERR_TXDS_BOUNDS", "texel block past the TXDT end")
        if rec.index_offset + rec.index_size > txdt_size:
            raise V2Error("ERR_TXDS_BOUNDS", "index block past the TXDT end")
        if rec.palette_offset + rec.palette_size > pldt_size:
            raise V2Error("ERR_TXDS_BOUNDS", "palette block past the PLDT end")

    if txdt_size > TXDT_MAX_BYTES:
        raise V2Error("ERR_TXDT", "TXDT payload over 524288 bytes")
    if pldt is not None and pldt_size > PLDT_MAX_BYTES:
        raise V2Error("ERR_PLDT", "PLDT payload over 98304 bytes")

    return records


def parse(data):
    """Parse a v2 file. Returns View or raises V2Error."""

    if len(data) >= 4 and _u32(data, 0) != VERSION:
        raise V2Error("ERR_VERSION", "not a version 2 file")
    if len(data) < HEADER_SIZE:
        raise V2Error("ERR_TRUNCATED", "file shorter than the 32-byte header")

    (version, header_size, total_size, flags, frame_index, chunk_count,
     reserved0, reserved1) = struct.unpack_from("<IIIIIIII", data, 0)

    if version != VERSION:
        raise V2Error("ERR_VERSION", "not a version 2 file")
    if header_size != HEADER_SIZE:
        raise V2Error("ERR_HEADER", "header_size is not 32")
    if reserved0 or reserved1:
        raise V2Error("ERR_HEADER", "header reserved words are not zero")
    if flags & FLAG_RESERVED_MASK:
        raise V2Error("ERR_FLAGS", "reserved file flag bits set")
    if chunk_count == 0 or chunk_count > CHUNK_COUNT_MAX:
        raise V2Error("ERR_CHUNK_COUNT", "chunk_count out of range")
    if total_size != len(data) or total_size > FILE_MAX:
        raise V2Error("ERR_SIZE", "total_size does not match the file size")

    offset = HEADER_SIZE
    unknown = 0
    chunks = {}

    for _ in range(chunk_count):
        if offset + CHUNK_HEADER_SIZE > total_size:
            raise V2Error("ERR_CHUNK_BOUNDS", "truncated chunk header")

        ctype, payload_size, chunk_flags, reserved = struct.unpack_from(
            "<IIII", data, offset)
        payload_offset = offset + CHUNK_HEADER_SIZE
        payload_end = payload_offset + payload_size

        if payload_end > total_size:
            raise V2Error("ERR_CHUNK_BOUNDS", "chunk payload past the file end")
        if payload_size % 4:
            raise V2Error("ERR_CHUNK_ALIGN",
                          "chunk payload is not a multiple of 4")
        if chunk_flags & CHUNK_FLAG_RESERVED_MASK:
            raise V2Error("ERR_CHUNK_FLAGS", "reserved chunk flag bits set")
        if reserved:
            raise V2Error("ERR_CHUNK_FLAGS", "chunk reserved word is not zero")

        payload = data[payload_offset:payload_end]

        if ctype in KNOWN_CHUNKS:
            if ctype in chunks:
                raise V2Error("ERR_DUP_CHUNK", "duplicate known chunk")
            chunks[ctype] = (chunk_flags, payload)
        else:
            if chunk_flags & CHUNK_FLAG_REQUIRED:
                raise V2Error("ERR_UNSUPPORTED_REQUIRED",
                              "unknown chunk marked REQUIRED")
            unknown += 1

        offset = payload_end

    if offset != total_size:
        raise V2Error("ERR_CHUNK_BOUNDS", "trailing bytes after the last chunk")
    if GXCM not in chunks or META not in chunks:
        raise V2Error("ERR_MISSING_CHUNK", "GXCM and META are mandatory")

    # Orphan texture payloads, before the payload-size caps (format s.8 step 10).
    if (TXDT in chunks or PLDT in chunks) and TXDS not in chunks:
        raise V2Error("ERR_MISSING_CHUNK", "TXDT/PLDT without a TXDS chunk")

    if not chunks[GXCM][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "GXCM must be marked REQUIRED")
    if chunks[META][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "META must not be marked REQUIRED")
    if RSTA in chunks and chunks[RSTA][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "RSTA must not be marked REQUIRED")
    for ctype, name in ((TXDS, "TXDS"), (TXDT, "TXDT"), (PLDT, "PLDT")):
        if ctype in chunks and chunks[ctype][0] & CHUNK_FLAG_REQUIRED:
            raise V2Error("ERR_CHUNK_FLAGS",
                          "%s must not be marked REQUIRED" % name)

    gxcm = _validate_gxcm(chunks[GXCM][1], flags)
    rsta = (_validate_rsta(chunks[RSTA][1], len(gxcm))
            if RSTA in chunks else [])

    tex_records = []
    txdt = chunks[TXDT][1] if TXDT in chunks else None
    pldt = chunks[PLDT][1] if PLDT in chunks else None
    if TXDS in chunks:
        tex_records = _validate_tex(chunks[TXDS][1], txdt, pldt)

    backend, coverage = _validate_meta(chunks[META][1])

    return View(total_size, flags, frame_index, chunk_count, unknown,
                gxcm, rsta, backend, coverage,
                tex_records, txdt if txdt is not None else b"",
                pldt if pldt is not None else b"")


def detect_format(data):
    """Return 'v1', 'v2' or 'unknown' without fully validating the file."""
    if len(data) < 4:
        return "unknown"
    version = _u32(data, 0)
    if version == 1:
        return "v1" if len(data) == 73740 else "unknown"
    if version == VERSION:
        return "v2" if len(data) >= HEADER_SIZE else "unknown"
    return "unknown"


def count_by_cmd_word(view):
    counts = {}
    for word, _value in view.gxcm:
        counts[word] = counts.get(word, 0) + 1
    return counts


def _load(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as exc:
        sys.exit("error: cannot read %s: %s" % (path, exc.strerror))


def _print_stats(view, path, data):
    print("file:       %s (%d bytes)" % (path, len(data)))
    print("version:    2")
    print("flags:      0x%02X" % view.flags)
    print("frame:      %d" % view.frame_index)
    print("chunks:     %d (%d unknown skipped)"
          % (view.chunk_count, view.unknown_chunks_skipped))
    print("entries:    %d" % len(view.gxcm))
    print("rsta:       %d" % len(view.rsta))
    print("backend:    %d" % view.meta_backend_id)
    print("coverage:   0x%02X" % view.meta_coverage_flags)
    for word in sorted(count_by_cmd_word(view)):
        print("0x%08X  %d" % (word, count_by_cmd_word(view)[word]))
    if view.tex_records:
        print("txds:       %d records" % len(view.tex_records))
        print("txdt:       %d bytes" % len(view.txdt))
        print("pldt:       %d bytes" % len(view.pldt))
        for index, rec in enumerate(view.tex_records):
            print("tex %d:     0x%08X/0x%08X fmt=%d texel=%d+%d palette=%d+%d "
                  "index=%d+%d"
                  % (index, rec.teximage_param, rec.pltt_base,
                     tex_format(rec.teximage_param),
                     rec.texel_offset, rec.texel_size,
                     rec.palette_offset, rec.palette_size,
                     rec.index_offset, rec.index_size))


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="twlhd_v2_dump.py",
        description="Validate and summarise TWL-HD v2 capture dumps.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser(
        "validate", help="validate a v2 file and print the result")
    validate_parser.add_argument("path", help="dump file (e.g. /twlhd_frame_v2.bin)")

    stats_parser = subparsers.add_parser(
        "stats", help="print a validated file summary")
    stats_parser.add_argument("path", help="dump file (e.g. /twlhd_frame_v2.bin)")

    args = parser.parse_args(argv)
    data = _load(args.path)

    try:
        view = parse(data)
    except V2Error as exc:
        print("result=%s" % exc.result)
        print("error: %s" % exc.message, file=sys.stderr)
        return 1

    if args.command == "validate":
        print("result=OK")
        print("entries=%d" % len(view.gxcm))
        print("rsta=%d" % len(view.rsta))
        return 0

    _print_stats(view, args.path, data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
