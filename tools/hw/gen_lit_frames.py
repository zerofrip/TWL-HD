#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the deterministic v2 frames for the Phase 5B-1 lighting test.

Four frames are written into the output directory:

  twlhd_frame_v2_lit_a.bin   four flat quads, one white directional light,
                             four different normals (incidence-angle sweep)
  twlhd_frame_v2_lit_b.bin   four flat quads exercising red diffuse+ambient,
                             emission only, specular with a shininess table,
                             and two lights
  twlhd_frame_v2_lit_ctl.bin the same four quads with flat COLOR only
  twlhd_frame_v2_lit_c.bin   diffuse+ambient rounding probe (four materials)

BUILD/UNIT tooling only.  Stdlib only.  Nothing here contacts a console or a
network address.  The frame layout follows the scene4_alpha 2x2 quad grid
(tests/host/test_demo_frame.cpp:920-939), scaled up to use more of the DS
screen.
"""

import argparse
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

# Attribution: the v2 container helpers (chunk/build/entries) are the ones
# tools/test_twlhd_v2.py uses for the C<->Python parser agreement suite.
# Importing them (rather than duplicating them) keeps the byte layout
# identical; that file is not modified.
from test_twlhd_v2 import build, entries  # noqa: E402

# v2 command ids (common/twlhd_v2_format.h:94-130).
CMD_COLOR = 0x20
CMD_NORMAL = 0x21
CMD_VTX_16 = 0x23
CMD_POLYGON_ATTR = 0x29
CMD_DIF_AMB = 0x30
CMD_SPE_EMI = 0x31
CMD_LIGHT_VECTOR = 0x32
CMD_LIGHT_COLOR = 0x33
CMD_SHININESS = 0x34
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41

FRAME_NAMES = (
    "twlhd_frame_v2_lit_a.bin",
    "twlhd_frame_v2_lit_b.bin",
    "twlhd_frame_v2_lit_ctl.bin",
    "twlhd_frame_v2_lit_c.bin",
)

# scene4_alpha 2x2 grid (tests/host/test_demo_frame.cpp:925-930) scaled by
# 1.6: max |x| = 3840/4096 = 0.9375, max |y| = 2560/4096 = 0.625, inside the
# +/-0.98 DS clip range.  VTX_16 words are raw clip * 4096.
QUADS = (
    ((-3840, -2560), (-640, -2560), (-640, -320), (-3840, -320)),
    ((640, -2560), (3840, -2560), (3840, -320), (640, -320)),
    ((-3840, 320), (-640, 320), (-640, 2560), (-3840, 2560)),
    ((640, 320), (3840, 320), (3840, 2560), (640, 2560)),
)

# 10-bit vector fields are 20.12 fixed point shifted right by 3 (a sign + 9
# fractional bits, common/twlhd_gx_interp.c: `sign_extend10(param) * 8`).
# Every value below is a multiple of 8 so the quantisation is exact.
LIGHT_Z = -4088          # light 0 direction: (0, 0, -4088) ~ -1.0
NORMAL_FACING = 511 << 20          # (0, 0, +4088) towards the light
NORMAL_60 = 443 | (256 << 20)      # (3544, 0, 2048) ~ 60 deg off the light
NORMAL_GRAZING = 511               # (4088, 0, 0) ~ 90 deg
NORMAL_AWAY = 513 << 20            # (0, 0, -4088) facing away
NORMAL_75 = 495 | (128 << 20)      # (3960, 0, 1024): fixed_diffuse 1022


def _rgb555(r, g, b):
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10)


def _component10(value_20_12):
    """20.12 fixed-point component -> signed 10-bit field (floor /8)."""
    return (value_20_12 // 8) & 0x3FF


def _vector_word(x, y, z, index=0):
    return (_component10(x) | (_component10(y) << 10) |
            (_component10(z) << 20) | ((index & 3) << 30))


def _light1_word():
    # (0.5, 0, -0.866) * 4096, rounded to multiples of 8: (2048, 0, -3544).
    # The y term stays zero; index 1 selects light slot 1.
    return _component10(2048) | (_component10(0) << 10) | \
        (_component10(-3544) << 20) | (1 << 30)


def _dif_amb(diffuse_rgb555, ambient_rgb555):
    return ((ambient_rgb555 & 0x7FFF) << 16) | (diffuse_rgb555 & 0x7FFF)


def _vtx16(x, y):
    """Two GXCM entries for one VTX_16 command (z = 0)."""
    return [(CMD_VTX_16, ((y & 0xFFFF) << 16) | (x & 0xFFFF)),
            (CMD_VTX_16, 0)]


def _quad(out, alpha, mask, normal_word, corners):
    out.append((CMD_POLYGON_ATTR, (alpha << 16) | (mask & 0xF)))
    out.append((CMD_BEGIN_VTXS, 1))
    if normal_word is not None:
        out.append((CMD_NORMAL, normal_word))
    for x, y in corners:
        out.extend(_vtx16(x, y))
    out.append((CMD_END_VTXS, 0))


def _flat_quad(out, colour_rgb555, corners):
    # No lighting state at all: the default POLYGON_ATTR alpha is already 31.
    out.append((CMD_COLOR, colour_rgb555))
    out.append((CMD_BEGIN_VTXS, 1))
    for x, y in corners:
        out.extend(_vtx16(x, y))
    out.append((CMD_END_VTXS, 0))


def _shininess_words():
    """128-entry ramp 1,3,5,...,255 packed four bytes per 32-bit word."""
    table = bytes((2 * i + 1) & 0xFF for i in range(128))
    return [int.from_bytes(table[k * 4:k * 4 + 4], "little")
            for k in range(32)]


def lit_a_entries():
    out = [
        (CMD_LIGHT_VECTOR, _vector_word(0, 0, LIGHT_Z)),
        (CMD_LIGHT_COLOR, _rgb555(31, 31, 31)),
        (CMD_DIF_AMB, _dif_amb(_rgb555(22, 22, 22), _rgb555(4, 4, 4))),
    ]
    normals = (NORMAL_FACING, NORMAL_60, NORMAL_GRAZING, NORMAL_AWAY)
    for corners, normal in zip(QUADS, normals):
        _quad(out, 31, 0x1, normal, corners)
    return out


def lit_b_entries():
    out = [
        (CMD_LIGHT_VECTOR, _vector_word(0, 0, LIGHT_Z)),
        (CMD_LIGHT_COLOR, _rgb555(31, 0, 0)),          # light 0 red
        (CMD_LIGHT_VECTOR, _light1_word()),
        (CMD_LIGHT_COLOR, _rgb555(0, 0, 31) | (1 << 30)),  # light 1 blue
    ]

    # quad 0: diffuse + ambient lit by the red light
    out.append((CMD_DIF_AMB,
                _dif_amb(_rgb555(22, 22, 22), _rgb555(4, 4, 4))))
    out.append((CMD_SPE_EMI, 0))
    _quad(out, 31, 0x1, NORMAL_FACING, QUADS[0])

    # quad 1: emission only (normal faces away, diffuse/ambient zero)
    out.append((CMD_DIF_AMB, 0))
    out.append((CMD_SPE_EMI, _rgb555(4, 28, 4) << 16))
    _quad(out, 31, 0x1, NORMAL_AWAY, QUADS[1])

    # quad 2: specular through the shininess table, white light
    out.append((CMD_DIF_AMB, 0))
    out.append((CMD_SPE_EMI, 0x7FFF | 0x8000))  # white specular + table bit
    out.append((CMD_LIGHT_COLOR, _rgb555(31, 31, 31)))
    for word in _shininess_words():
        out.append((CMD_SHININESS, word))
    _quad(out, 31, 0x1, NORMAL_FACING, QUADS[2])

    # quad 3: two lights from different directions
    out.append((CMD_DIF_AMB, _dif_amb(_rgb555(22, 22, 22), 0)))
    out.append((CMD_SPE_EMI, 0))
    out.append((CMD_LIGHT_COLOR, _rgb555(31, 0, 0)))  # light 0 back to red
    _quad(out, 31, 0x3, NORMAL_FACING, QUADS[3])
    return out


def lit_c_entries():
    """Diffuse+ambient rounding probe (EQ-4zi follow-up).

    One white light, four materials/normals chosen so that the candidate
    rounding rules (separate floor, floor of the sum, ceil, round) give
    different 5-bit results.
    """
    out = [
        (CMD_LIGHT_VECTOR, _vector_word(0, 0, LIGHT_Z)),
        (CMD_LIGHT_COLOR, _rgb555(31, 31, 31)),
    ]
    probes = (
        (1, 1, NORMAL_FACING),
        (1, 0, NORMAL_FACING),
        (1, 0, NORMAL_60),
        (28, 8, NORMAL_75),
    )
    for corners, (diff, amb, normal) in zip(QUADS, probes):
        out.append((CMD_DIF_AMB,
                    _dif_amb(_rgb555(diff, diff, diff),
                             _rgb555(amb, amb, amb))))
        _quad(out, 31, 0x1, normal, corners)
    return out


def lit_ctl_entries():
    colours = (_rgb555(31, 0, 0), _rgb555(0, 31, 0),
               _rgb555(0, 0, 31), _rgb555(31, 31, 0))
    out = []
    for corners, colour in zip(QUADS, colours):
        _flat_quad(out, colour, corners)
    return out


_BUILDERS = {
    FRAME_NAMES[0]: lit_a_entries,
    FRAME_NAMES[1]: lit_b_entries,
    FRAME_NAMES[2]: lit_ctl_entries,
    FRAME_NAMES[3]: lit_c_entries,
}


def frame_bytes(name):
    return build(entries(*_BUILDERS[name]()))


def write_frames(outdir):
    os.makedirs(outdir, exist_ok=True)
    paths = []
    for name in FRAME_NAMES:
        path = os.path.join(outdir, name)
        with open(path, "wb") as handle:
            handle.write(frame_bytes(name))
        paths.append(path)
    return paths


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_lit_frames.py",
        description="Write the three deterministic 5B-1 lighting v2 frames.")
    parser.add_argument("outdir", help="output directory")
    args = parser.parse_args(argv)
    for path in write_frames(args.outdir):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
