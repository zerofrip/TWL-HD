#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate tools/ds_scale_test/include/pattern.h from pattern.json.

The header is committed so the unit test can regenerate it into a temp file
and diff it against the committed copy.  `render_rgb()` renders the same
pattern to 8-bit RGB (using the 5-bit round trip the DS would display) for the
synthetic capture tests.

    python3 gen_pattern.py [pattern.json] [output.h]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_JSON = os.path.join(HERE, "pattern.json")
DEFAULT_OUT = os.path.join(HERE, "include", "pattern.h")

KIND_RECT = 0
KIND_FRAME = 1
KIND_ALT_COLUMNS = 2
KIND_ALT_ROWS = 3
KIND_GRADIENT = 4

KINDS = {
    "rect": KIND_RECT,
    "frame": KIND_FRAME,
    "alt_columns": KIND_ALT_COLUMNS,
    "alt_rows": KIND_ALT_ROWS,
    "gradient": KIND_GRADIENT,
}


def v5_min_error(v8):
    """Closest 5-bit value under the display expansion (v5<<3)|(v5>>2)."""
    return round(v8 * 31 / 255)


def rgb_to_bgr555(rgb):
    """8-bit RGB -> opaque 16-bit BGR555 (bit 15 set)."""
    r, g, b = (v5_min_error(int(c)) for c in rgb)
    return 0x8000 | (b << 10) | (g << 5) | r


def unpack_bgr555(value):
    r = value & 31
    g = (value >> 5) & 31
    b = (value >> 10) & 31
    return tuple((v << 3) | (v >> 2) for v in (r, g, b))


def elem_colour(elem):
    """RGB triplet from pattern.json (accepts 'colour' and 'color')."""
    return elem.get("colour", elem.get("color"))


def load(path=DEFAULT_JSON):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def render_rgb(pattern):
    """Render the pattern to a flat RGB byte string (width*height*3)."""
    width = pattern["width"]
    height = pattern["height"]
    background = unpack_bgr555(rgb_to_bgr555(pattern["background"]))
    buf = bytearray(background * (width * height))

    def put(x, y, rgb):
        off = (y * width + x) * 3
        buf[off:off + 3] = bytes(rgb)

    for elem in pattern["elements"]:
        kind = elem["type"]
        x, y, w, h = elem["x"], elem["y"], elem["w"], elem["h"]
        color = unpack_bgr555(rgb_to_bgr555(elem_colour(elem)))
        if kind in ("rect", "frame"):
            for py in range(y, y + h):
                for px in range(x, x + w):
                    if kind == "rect" or py in (y, y + h - 1) \
                            or px in (x, x + w - 1):
                        put(px, py, color)
        elif kind == "alt_columns":
            period = elem.get("period", 2)
            phase = elem.get("phase", 0)
            for py in range(y, y + h):
                for px in range(x, x + w):
                    put(px, py, color if (px - x) % period == phase
                        else (0, 0, 0))
        elif kind == "alt_rows":
            period = elem.get("period", 2)
            phase = elem.get("phase", 0)
            for py in range(y, y + h):
                for px in range(x, x + w):
                    put(px, py, color if (py - y) % period == phase
                        else (0, 0, 0))
        elif kind == "gradient":
            step_px = elem["step_px"]
            step_value = elem["step_value"]
            for py in range(y, y + h):
                for px in range(x, x + w):
                    v5 = v5_min_error(((px - x) // step_px) * step_value)
                    put(px, py, tuple((v5 << 3) | (v5 >> 2) for _ in range(3)))
        else:  # pragma: no cover - pattern.json is validated by the tests
            raise ValueError("unknown element type: %s" % kind)
    return bytes(buf)


def gradient_bgr555(pattern):
    """Opaque BGR555 grey words for the gradient element, one per step."""
    elem = next(e for e in pattern["elements"] if e["type"] == "gradient")
    words = []
    for i in range(elem["w"] // elem["step_px"]):
        v5 = v5_min_error(i * elem["step_value"])
        words.append(0x8000 | (v5 << 10) | (v5 << 5) | v5)
    return words


def header_text(pattern):
    lines = [
        "/* Generated from pattern.json by gen_pattern.py. Do not edit. */",
        "#ifndef DS_SCALE_TEST_PATTERN_H",
        "#define DS_SCALE_TEST_PATTERN_H",
        "",
        "#define PATTERN_WIDTH %d" % pattern["width"],
        "#define PATTERN_HEIGHT %d" % pattern["height"],
        "#define PATTERN_ID \"%s\"" % pattern["id"],
        "",
        "#define PATTERN_OP_RECT %d" % KIND_RECT,
        "#define PATTERN_OP_FRAME %d" % KIND_FRAME,
        "#define PATTERN_OP_ALT_COLUMNS %d" % KIND_ALT_COLUMNS,
        "#define PATTERN_OP_ALT_ROWS %d" % KIND_ALT_ROWS,
        "#define PATTERN_OP_GRADIENT %d" % KIND_GRADIENT,
        "",
        "typedef struct {",
        "    int kind;",
        "    int x, y, w, h;",
        "    unsigned short color;",
        "    int period, phase;",
        "    int step_px, step_value;",
        "} PatternOp;",
        "",
        "static const PatternOp PATTERN_OPS[] = {",
    ]
    for elem in pattern["elements"]:
        kind = KINDS[elem["type"]]
        color = rgb_to_bgr555(elem_colour(elem))
        period = elem.get("period", 0)
        phase = elem.get("phase", 0)
        step_px = elem.get("step_px", 0)
        step_value = elem.get("step_value", 0)
        lines.append(
            "    {%d, %d, %d, %d, %d, 0x%04X, %d, %d, %d, %d}, /* %s */"
            % (kind, elem["x"], elem["y"], elem["w"], elem["h"], color,
               period, phase, step_px, step_value, elem["name"]))
    lines += [
        "};",
        "",
        "#define PATTERN_OP_COUNT "
        "(sizeof(PATTERN_OPS) / sizeof(PATTERN_OPS[0]))",
        "",
        "static const unsigned short PATTERN_GRADIENT_BGR555[] = {",
    ]
    words = gradient_bgr555(pattern)
    for start in range(0, len(words), 8):
        lines.append("    " + ", ".join(
            "0x%04X" % w for w in words[start:start + 8]) + ",")
    lines += [
        "};",
        "",
        "#define PATTERN_GRADIENT_COUNT "
        "(sizeof(PATTERN_GRADIENT_BGR555) / "
        "sizeof(PATTERN_GRADIENT_BGR555[0]))",
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
