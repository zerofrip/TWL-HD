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


def fourcc(text):
    raw = text.encode("ascii")
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24)


GXCM = fourcc("GXCM")
META = fourcc("META")
RSTA = fourcc("RSTA")
KNOWN_CHUNKS = {GXCM, META, RSTA}

# Reserved, not implemented: treated as unknown chunks.
RESERVED_CHUNKS = {fourcc("CLPM"), fourcc("TXDS"),
                   fourcc("TXDT"), fourcc("PLDT")}

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
    "gxcm rsta meta_backend_id meta_coverage_flags")


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

    if not chunks[GXCM][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "GXCM must be marked REQUIRED")
    if chunks[META][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "META must not be marked REQUIRED")
    if RSTA in chunks and chunks[RSTA][0] & CHUNK_FLAG_REQUIRED:
        raise V2Error("ERR_CHUNK_FLAGS", "RSTA must not be marked REQUIRED")

    gxcm = _validate_gxcm(chunks[GXCM][1], flags)
    rsta = (_validate_rsta(chunks[RSTA][1], len(gxcm))
            if RSTA in chunks else [])
    backend, coverage = _validate_meta(chunks[META][1])

    return View(total_size, flags, frame_index, chunk_count, unknown,
                gxcm, rsta, backend, coverage)


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
