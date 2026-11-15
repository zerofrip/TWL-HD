#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare a DeSmuME v2 capture of the textured demo scene (SCENE=5).

Usage: compare_texture_capture.py <captured.bin> [angle]

The expectation is built in: this scene draws one 8x8 I4 quad whose texture and
palette arrays are known at build time. The literals below are the ONE place
they live; tools/test_emulator_texture_compare.py cross-checks them against
arm9v2/source/demo_frame.cpp so a changed demo array or key fails the test.

Rules (all must hold):

* file flags are exactly FRAME_COMPLETE. OVERFLOW means the recorder dropped a
  texture key: for this scene that is a FAIL, not a tolerated residue;
* META backend is the register proxy (1) and META coverage is 0x7;
* exactly ONE TXDS record; its key equals the TEXIMAGE_PARAM/PLTT_BASE words
  the demo writes (each checked separately); its sizes equal the format
  formula; TXDT and PLDT bytes equal the known arrays;
* the GXCM stream has the structural shape the scene produces: one
  TEXIMAGE_PARAM with the literal param, one PLTT_BASE with the literal pltt,
  TEXIMAGE_PARAM before PLTT_BASE before one BEGIN_VTXS(GL_QUADS), four
  (TEX_COORD, VTX_16, VTX_16) triples inside the group, one END_VTXS after it,
  and SWAP_BUFFERS last;
* the camera path is the shared demo path, so the perspective/lookat/rotation
  commands are checked analytically by the imported compare_capture helpers.

Justified limitation: the host expected stream does not support SCENE=5
(tests/host/dump_demo_frame.cpp is a non-texture build whose mock has no
texture/VRAM ports), so the command sequence is compared structurally plus
against the two exact known key values instead of entry-for-entry against
expected.txt. A wrongly ordered non-key command outside the checked shape is
therefore not caught here; the cross-checked literals, the matrix checks and
docs/reports/5c3-mutation-report.md bound that gap.

BUILD/UNIT tooling only: this proves nothing about a real emulator or DS
hardware until the owner records a real run (docs/EMULATOR_VALIDATION.md).

Stdlib only, deterministic. Prints PASS or FAIL with one reason per line.
Exit 0 on PASS, 1 on FAIL, 2 on usage errors.
"""

import os
import sys

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMULATOR_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, EMULATOR_DIR)

import twlhd_v2_dump  # noqa: E402
import compare_capture  # noqa: E402

DEFAULT_ANGLE = 61440

# arm9v2/source/demo_frame.cpp: uint32_t param = (3u << 26); uint32_t pltt = 0u;
# Format I4 = 3 (bits 26..28), both size shifts 0 (8x8), texel base = (param &
# 0xFFFF) << 3 = 0, palette base = pltt << 4 = 0.
TEXIMAGE_PARAM = 3 << 26  # 0x0C000000
PLTT_BASE = 0

# twlhd_v2_tex_sizes(0x0C000000): 8x8 * 1/2 byte per I4 texel = 32 texel bytes,
# 32 palette bytes (16 RGB555 entries), no 4x4 index block.
TEXEL_SIZE = 32
PALETTE_SIZE = 32
INDEX_SIZE = 0

# arm9v2/source/demo_frame.cpp scene5_texel / scene5_palette.
TEXEL = bytes((
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
))
PALETTE = bytes((
    0x00, 0x00, 0xFF, 0x0F, 0x00, 0xF8, 0xE0, 0x07,
    0x1F, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
))

CMD_TEX_COORD = 0x22
CMD_VTX_16 = 0x23
CMD_TEXIMAGE_PARAM = 0x2A
CMD_PLTT_BASE = 0x2B
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_SWAP_BUFFERS = 0x50

GL_QUADS = 1
VERTEX_COUNT = 4

BACKEND_REGISTER_PROXY = 1
COVERAGE_ALL = 0x7


def _byte_diff(actual, expected):
    if len(actual) != len(expected):
        return "%d bytes, expected %d" % (len(actual), len(expected))
    for index, (a, b) in enumerate(zip(actual, expected)):
        if a != b:
            return "byte %d is 0x%02X, expected 0x%02X" % (index, a, b)
    return "bytes are identical"


def _check_texture(view, failures):
    records = list(view.tex_records)
    if len(records) != 1:
        failures.add("txds-count", "%d TXDS records, expected 1"
                     % len(records))
    else:
        record = records[0]
        if record.teximage_param != TEXIMAGE_PARAM:
            failures.add("txds-key-param", "0x%08X, expected 0x%08X"
                         % (record.teximage_param, TEXIMAGE_PARAM))
        if record.pltt_base != PLTT_BASE:
            failures.add("txds-key-pltt", "0x%08X, expected 0x%08X"
                         % (record.pltt_base, PLTT_BASE))
        for reason, actual, expected in (
                ("txds-texel-size", record.texel_size, TEXEL_SIZE),
                ("txds-palette-size", record.palette_size, PALETTE_SIZE),
                ("txds-index-size", record.index_size, INDEX_SIZE)):
            if actual != expected:
                failures.add(reason, "%d, expected %d" % (actual, expected))

    if view.txdt != TEXEL:
        failures.add("txdt-bytes", _byte_diff(view.txdt, TEXEL))
    if view.pldt != PALETTE:
        failures.add("pldt-bytes", _byte_diff(view.pldt, PALETTE))


def _positions(entries, cmd_id):
    return [index for index, (word, _value) in enumerate(entries)
            if word == cmd_id]


def _group_ok(group):
    """Exactly four (TEX_COORD, VTX_16, VTX_16) triples, nothing else."""
    if len(group) != VERTEX_COUNT * 3:
        return False
    for vertex in range(VERTEX_COUNT):
        base = vertex * 3
        if group[base][0] != CMD_TEX_COORD:
            return False
        if group[base + 1][0] != CMD_VTX_16:
            return False
        if group[base + 2][0] != CMD_VTX_16:
            return False
    return True


def _check_shape(entries, angle, failures):
    teximage = _positions(entries, CMD_TEXIMAGE_PARAM)
    pltt = _positions(entries, CMD_PLTT_BASE)
    begins = _positions(entries, CMD_BEGIN_VTXS)
    ends = _positions(entries, CMD_END_VTXS)

    if len(teximage) != 1:
        failures.add("teximage-count", "%d TEXIMAGE_PARAM entries, expected 1"
                     % len(teximage))
    elif entries[teximage[0]][1] != TEXIMAGE_PARAM:
        failures.add("teximage-value", "0x%08X, expected 0x%08X"
                     % (entries[teximage[0]][1], TEXIMAGE_PARAM))

    if len(pltt) != 1:
        failures.add("pltt-count", "%d PLTT_BASE entries, expected 1"
                     % len(pltt))
    elif entries[pltt[0]][1] != PLTT_BASE:
        failures.add("pltt-value", "0x%08X, expected 0x%08X"
                     % (entries[pltt[0]][1], PLTT_BASE))

    begin = None
    if len(begins) != 1:
        failures.add("begin-count", "%d BEGIN_VTXS entries, expected 1"
                     % len(begins))
    else:
        begin = begins[0]
        if entries[begin][1] != GL_QUADS:
            failures.add("begin-type", "%d, expected GL_QUADS (1)"
                         % entries[begin][1])

    if len(teximage) == 1 and len(pltt) == 1 and begin is not None:
        if not teximage[0] < pltt[0] < begin:
            failures.add("texture-order",
                         "expected TEXIMAGE_PARAM (%d) before PLTT_BASE (%d) "
                         "before BEGIN_VTXS (%d)"
                         % (teximage[0], pltt[0], begin))

    if begin is not None:
        after = [index for index in ends if index > begin]
        if len(ends) != 1 or not after:
            failures.add("end-count",
                         "%d END_VTXS entries after BEGIN_VTXS, expected 1"
                         % len(after))
        else:
            group = entries[begin + 1:after[0]]
            if not _group_ok(group):
                failures.add("tex-group",
                             "%d entries between BEGIN_VTXS and END_VTXS, "
                             "expected 4 x (TEX_COORD, VTX_16, VTX_16)"
                             % len(group))

    if not entries or entries[-1][0] != CMD_SWAP_BUFFERS:
        failures.add("swap-last", "SWAP_BUFFERS is not the last entry")

    groups = compare_capture.collect_groups(entries, failures)
    if len(groups[0x18]) != 1:
        failures.add("perspective", "%d MTX_MULT_4x4 groups, expected 1"
                     % len(groups[0x18]))
    else:
        compare_capture.check_perspective(groups[0x18][0], failures)
    if len(groups[0x19]) != 1:
        failures.add("lookat", "%d MTX_MULT_4x3 groups, expected 1"
                     % len(groups[0x19]))
    else:
        compare_capture.check_lookat(groups[0x19][0], failures)
    if len(groups[0x1A]) != 2:
        failures.add("rotation", "%d MTX_MULT_3x3 groups, expected 2"
                     % len(groups[0x1A]))
    else:
        compare_capture.check_rotation(groups[0x1A][0], angle,
                                       "rotation X", failures)
        compare_capture.check_rotation(groups[0x1A][1], angle,
                                       "rotation Y", failures)


def compare(captured, angle, failures):
    try:
        with open(captured, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        failures.add("v2-parse", "cannot read %s: %s" % (captured, exc.strerror))
        return

    try:
        view = twlhd_v2_dump.parse(data)
    except twlhd_v2_dump.V2Error as exc:
        failures.add("v2-parse", "%s (%s)" % (exc.message, exc.result))
        return

    if view.flags != twlhd_v2_dump.FLAG_FRAME_COMPLETE:
        if view.flags & twlhd_v2_dump.FLAG_OVERFLOW:
            failures.add("flags",
                         "0x%02X has OVERFLOW: a texture drop is a FAIL for "
                         "this scene (expected FRAME_COMPLETE only)"
                         % view.flags)
        else:
            failures.add("flags", "0x%02X, expected FRAME_COMPLETE only"
                         % view.flags)
    if view.meta_backend_id != BACKEND_REGISTER_PROXY:
        failures.add("backend", "%d, expected %d"
                     % (view.meta_backend_id, BACKEND_REGISTER_PROXY))
    if view.meta_coverage_flags != COVERAGE_ALL:
        failures.add("coverage", "0x%02X, expected 0x%02X"
                     % (view.meta_coverage_flags, COVERAGE_ALL))

    _check_texture(view, failures)
    _check_shape(list(view.gxcm), angle, failures)


def main(argv):
    if len(argv) == 2:
        angle = DEFAULT_ANGLE
    elif len(argv) == 3:
        try:
            angle = int(argv[2], 0)
        except ValueError:
            sys.stderr.write("error: angle must be an integer\n")
            return 2
    else:
        sys.stderr.write(
            "usage: %s <captured.bin> [angle]\n" % argv[0])
        return 2

    failures = compare_capture.Failures()
    compare(argv[1], angle, failures)

    if failures:
        print("FAIL")
        for reason, detail in failures.items:
            print("- %s: %s" % (reason, detail))
        return 1

    print("PASS: SCENE=5 texture 0x%08X/0x%08X, %d texel + %d palette bytes "
          "for angle %d"
          % (TEXIMAGE_PARAM, PLTT_BASE, len(TEXEL), len(PALETTE), angle))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
