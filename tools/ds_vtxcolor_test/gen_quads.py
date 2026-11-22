#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate tools/ds_vtxcolor_test/include/quads.h from quads.json.

The header is committed so the unit test can regenerate it into a temp file
and diff it against the committed copy.  `colour5()` and `unpack_rgb15()` are
the 8-bit <-> 5-bit helpers used by the tests and mirror gen_pattern.py's
minimal-error rule.

    python3 gen_quads.py [quads.json] [output.h]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_JSON = os.path.join(HERE, "quads.json")
DEFAULT_OUT = os.path.join(HERE, "include", "quads.h")


def v5_min_error(v8):
    """Closest 5-bit value under the display expansion (v5<<3)|(v5>>2)."""
    return round(v8 * 31 / 255)


def colour5(rgb):
    """8-bit RGB -> 5-bit RGB tuple, minimal error under the expansion."""
    return tuple(v5_min_error(int(c)) for c in rgb)


def unpack_rgb15(rgb5):
    """5-bit RGB -> expected 8-bit display tuple (v5<<3)|(v5>>2)."""
    return tuple((v << 3) | (v >> 2) for v in rgb5)


def load(path=DEFAULT_JSON):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def header_text(quads):
    lines = [
        "/* Generated from quads.json by gen_quads.py. Do not edit. */",
        "#ifndef DS_VTXCOLOR_TEST_QUADS_H",
        "#define DS_VTXCOLOR_TEST_QUADS_H",
        "",
        "#define DSVTX_ID \"%s\"" % quads["id"],
        "#define DSVTX_CLEAR_R %d" % quads["background5"][0],
        "#define DSVTX_CLEAR_G %d" % quads["background5"][1],
        "#define DSVTX_CLEAR_B %d" % quads["background5"][2],
        "",
        "typedef struct {",
        "    const char *name;",
        "    int x, y, w, h;",
        "    int has_colour;",
        "    unsigned r5, g5, b5;",
        "} VtxQuad;",
        "",
        "static const VtxQuad VTX_QUADS[] = {",
    ]
    for quad in quads["quads"]:
        rgb = quad.get("colour")
        if rgb is None:
            r5, g5, b5 = 0, 0, 0
            has_colour = 0
        else:
            r5, g5, b5 = colour5(rgb)
            has_colour = 1
        lines.append(
            "    { \"%s\", %d, %d, %d, %d, %d, %d, %d, %d },"
            % (quad["name"], quad["x"], quad["y"], quad["w"], quad["h"],
               has_colour, r5, g5, b5))
    lines += [
        "};",
        "",
        "#define VTX_QUAD_COUNT "
        "(sizeof(VTX_QUADS) / sizeof(VTX_QUADS[0]))",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def main(argv):
    json_path = argv[1] if len(argv) > 1 else DEFAULT_JSON
    out_path = argv[2] if len(argv) > 2 else DEFAULT_OUT
    text = header_text(load(json_path))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
