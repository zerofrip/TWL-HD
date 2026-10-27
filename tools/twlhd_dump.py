#!/usr/bin/env python3
"""Analyse TWL-HD capture dumps (Phase 2 host tooling).

The wire layout lives in ``common/twlhd_frame_format.h``: a 12-byte frame
header (version, frame_index, cmd_count) followed by 1024 fixed 72-byte command
slots. Each slot is a 4-byte type, a 1-byte param_count, 3 padding bytes and 16
little-endian parameter words -- the same layout the header locks with
``_Static_assert(sizeof(TwlhdCmd) == 72)``. The explicit ``3x`` pad is required
because struct's ``<`` prefix uses standard sizes and never adds alignment
padding. The type field is stored as a 32-bit word rather than the enum because
devkitARM builds with ``-fshort-enums`` (header lines 44-46).

Only ``cmd_count`` slots are meaningful; the rest of the ring buffer is
zero-fill because the whole struct is written raw.
"""

import argparse
import struct
import sys
from collections import namedtuple

FORMAT_VERSION = 1
RING_CAPACITY = 1024

HEADER = struct.Struct("<III")
CMD = struct.Struct("<IB3x16I")
FRAME_SIZE = HEADER.size + RING_CAPACITY * CMD.size

# Mirrors TwlhdCmdType in common/twlhd_frame_format.h.
CMD_NAMES = {
    0x00: "UNKNOWN",
    0x10: "MTX_MODE",
    0x11: "MTX_PUSH",
    0x12: "MTX_POP",
    0x13: "MTX_STORE",
    0x14: "MTX_RESTORE",
    0x15: "MTX_IDENTITY",
    0x16: "MTX_LOAD_4x4",
    0x17: "MTX_LOAD_4x3",
    0x18: "MTX_MULT_4x4",
    0x19: "MTX_MULT_4x3",
    0x1A: "MTX_MULT_3x3",
    0x20: "COLOR",
    0x21: "NORMAL",
    0x22: "TEX_COORD",
    0x23: "VTX_16",
    0x24: "VTX_10",
    0x25: "VTX_XY",
    0x26: "VTX_XZ",
    0x27: "VTX_YZ",
    0x40: "BEGIN_VTXS",
    0x41: "END_VTXS",
    0x60: "VIEWPORT",
}

Frame = namedtuple("Frame", "version frame_index cmd_count commands size")
Cmd = namedtuple("Cmd", "type param_count params")


def cmd_name(type_id):
    return CMD_NAMES.get(type_id, "UNKNOWN(0x%02X)" % type_id)


def parse_frame(path):
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        sys.exit("error: cannot read %s: %s" % (path, exc.strerror))

    if len(data) < FRAME_SIZE:
        sys.exit("error: %s is truncated: %d bytes, expected %d"
                 % (path, len(data), FRAME_SIZE))

    version, frame_index, cmd_count = HEADER.unpack_from(data)
    if version != FORMAT_VERSION:
        sys.exit("error: %s has format version %d, expected %d "
                 "(re-create the dump with this build)"
                 % (path, version, FORMAT_VERSION))
    if cmd_count > RING_CAPACITY:
        sys.exit("error: %s claims %d commands, maximum is %d"
                 % (path, cmd_count, RING_CAPACITY))

    commands = []
    for index in range(cmd_count):
        type_id, param_count, *params = CMD.unpack_from(
            data, HEADER.size + index * CMD.size)
        if param_count > len(params):
            sys.exit("error: %s command %d claims %d parameters, maximum is %d"
                     % (path, index, param_count, len(params)))
        commands.append(Cmd(type_id, param_count, tuple(params[:param_count])))

    return Frame(version, frame_index, cmd_count, commands, len(data))


def count_by_type(frame):
    counts = {}
    for cmd in frame.commands:
        counts[cmd.type] = counts.get(cmd.type, 0) + 1
    return counts


def print_stats(frame, path):
    print("file:       %s (%d bytes)" % (path, frame.size))
    print("version:    %d" % frame.version)
    print("frame:      %d" % frame.frame_index)
    print("commands:   %d / %d" % (frame.cmd_count, RING_CAPACITY))
    counts = count_by_type(frame)
    for type_id in sorted(counts):
        print("%-13s %d" % (cmd_name(type_id), counts[type_id]))


def describe(cmd):
    words = " ".join("0x%08X" % param for param in cmd.params)
    return "%s (%d params)%s" % (cmd_name(cmd.type), cmd.param_count,
                                 " " + words if words else "")


def print_dump(frame, path):
    print("file:       %s (%d bytes)" % (path, frame.size))
    print("version:    %d" % frame.version)
    print("frame:      %d" % frame.frame_index)
    print("commands:   %d / %d" % (frame.cmd_count, RING_CAPACITY))
    for index, cmd in enumerate(frame.commands):
        print("%4d  %s" % (index, describe(cmd)))


def first_divergence(frame_a, frame_b):
    """Index of the first differing command, or None if the frames match."""
    common = min(frame_a.cmd_count, frame_b.cmd_count)
    for index in range(common):
        if frame_a.commands[index] != frame_b.commands[index]:
            return index
    return None if frame_a.cmd_count == frame_b.cmd_count else common


def count_deltas(frame_a, frame_b):
    counts_a = count_by_type(frame_a)
    counts_b = count_by_type(frame_b)
    return {type_id: counts_a.get(type_id, 0) - counts_b.get(type_id, 0)
            for type_id in set(counts_a) | set(counts_b)
            if counts_a.get(type_id, 0) != counts_b.get(type_id, 0)}


def print_diff(path_a, frame_a, path_b, frame_b):
    print("a: %s (%d commands)" % (path_a, frame_a.cmd_count))
    print("b: %s (%d commands)" % (path_b, frame_b.cmd_count))

    index = first_divergence(frame_a, frame_b)
    if index is None:
        print("frames identical")
    elif index >= len(frame_a.commands) or index >= len(frame_b.commands):
        print("first %d commands identical; lengths differ: %d vs %d"
              % (index, frame_a.cmd_count, frame_b.cmd_count))
    else:
        print("first divergence at command %d" % index)
        print("  a: %s" % describe(frame_a.commands[index]))
        print("  b: %s" % describe(frame_b.commands[index]))

    deltas = count_deltas(frame_a, frame_b)
    if not deltas:
        print("count deltas: none")
    else:
        print("count deltas:")
        for type_id in sorted(deltas):
            print("  %-13s %+d" % (cmd_name(type_id), deltas[type_id]))


def build_parser():
    parser = argparse.ArgumentParser(
        prog="twlhd_dump.py",
        description="Parse and compare TWL-HD capture dumps.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    dump_parser = subparsers.add_parser(
        "dump", help="print every captured command")
    dump_parser.add_argument("path", help="dump file (e.g. /twlhd_frame.bin)")

    stats_parser = subparsers.add_parser(
        "stats", help="print per-command-type counts")
    stats_parser.add_argument("path", help="dump file (e.g. /twlhd_frame.bin)")

    diff_parser = subparsers.add_parser(
        "diff", help="compare two dumps and report the first divergence")
    diff_parser.add_argument("a", help="first dump file")
    diff_parser.add_argument("b", help="second dump file")

    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)

    if args.command == "diff":
        print_diff(args.a, parse_frame(args.a), args.b, parse_frame(args.b))
        return 0

    frame = parse_frame(args.path)
    if args.command == "stats":
        print_stats(frame, args.path)
    else:
        print_dump(frame, args.path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
