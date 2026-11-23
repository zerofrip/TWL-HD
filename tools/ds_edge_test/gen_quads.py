#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate tools/ds_edge_test/include/quads.h from quads.json.

quads.json is the single source of truth shared by the ROM generator and the
measurement tool.  The header is committed so the unit test can regenerate it
into a temp file and diff it against the committed copy (same pattern as
tools/ds_alpha_test/gen_quads.py).

    python3 gen_quads.py [quads.json] [output.h]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_JSON = os.path.join(HERE, "quads.json")
DEFAULT_OUT = os.path.join(HERE, "include", "quads.h")


def load(path=DEFAULT_JSON):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _poly_line(poly):
    r5, g5, b5 = poly["colour5"]
    return ('    { "%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d },'
            % (poly["name"], poly["x"], poly["y"], poly["w"], poly["h"],
               poly["alpha"], poly["poly_id"], poly["z"], r5, g5, b5))


def header_text(data):
    lines = [
        "/* Generated from quads.json by gen_quads.py. Do not edit. */",
        "#ifndef DS_EDGE_TEST_QUADS_H",
        "#define DS_EDGE_TEST_QUADS_H",
        "",
        '#define DSEDGE_ID "%s"' % data["id"],
        "#define DSEDGE_CLEAR_R %d" % data["clear5"][0],
        "#define DSEDGE_CLEAR_G %d" % data["clear5"][1],
        "#define DSEDGE_CLEAR_B %d" % data["clear5"][2],
        "",
        "typedef struct {",
        "    const char *name;",
        "    int x, y, w, h;",
        "    unsigned alpha, poly_id;",
        "    int z;",
        "    unsigned r5, g5, b5;",
        "} EdgePoly;",
        "",
        "static const EdgePoly EDGE_BACKDROPS[] = {",
    ]
    for poly in data["backdrops"]:
        lines.append(_poly_line(poly))
    lines += [
        "};",
        "",
        "static const EdgePoly EDGE_QUADS[] = {",
    ]
    for poly in data["quads"]:
        lines.append(_poly_line(poly))
    lines += [
        "};",
        "",
        "#define EDGE_BACKDROP_COUNT "
        "(sizeof(EDGE_BACKDROPS) / sizeof(EDGE_BACKDROPS[0]))",
        "#define EDGE_QUAD_COUNT "
        "(sizeof(EDGE_QUADS) / sizeof(EDGE_QUADS[0]))",
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
