#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the row/column placement of the ds-edge-test ROM geometry.

The ROM (``tools/ds_edge_test/quads.json``) draws one opaque black full-screen
backdrop and ten opaque white polygons whose nominal coverage is the half-open
vertex rectangle ``[x, x+w) x [y, y+h)`` in DS pixels: bands touching the top
and bottom scanlines and the left and right columns, four isolated 32x32
squares, one 1-pixel-high line and one 1-pixel-wide line.  This tool answers
where those polygons actually land.

Two inputs
----------
* ``--native <frame.ppm>``  a native 256x192 P6 PPM such as the DeSmuME
  harness writes (``--ppm``).  Rows and columns are DS rows/columns (identity
  mapping) and the answer is whole pixels: the first/last white row and column,
  the white row/column runs, and per geometry item the measured rows/columns
  against the nominal rectangle.
* ``<capture.png>``  a 400x240 cc3dsfs crop, mapped with the committed
  ``capture_mapping.json`` (``scale 1.25, offset 40,0``).  The sub-pixel edge
  positions come from ``tools/ds_alpha_test/measure_edges.py`` **by import**
  (``measure_image``); that file is not modified.

White test
----------
A pixel is white when its mean ``(R+G+B)/3`` is at least
``WHITE_THRESHOLD = 128`` (8-bit levels).  DeSmuME expands the 6-bit white
``63`` to ``255`` and the black clear to ``0``, so the threshold is far from
both plateaus.

Classification
--------------
Per-item row offsets are classified as:

* ``none``     every measured item is on its nominal first row;
* ``uniform+1`` every measured item is exactly one row lower.  A single 3D
                frame cannot separate this from a whole-frame shift in
                DeSmuME/the harness *and* a per-primitive rasteriser offset;
* ``partial+1`` the top/bottom frame-anchor bands are nominal while other
                items are one row lower (per-primitive-like);
* ``mixed``    anything else (or missing anchors).

Output is deterministic ``key=value`` lines.  Exit 0 on success, 2 on usage or
input errors.
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ALPHA_DIR = os.path.join(os.path.dirname(HERE), "ds_alpha_test")
if ALPHA_DIR not in sys.path:
    # Append, not insert: the sibling directory also holds a test_quads.py, and
    # putting it ahead of this directory would shadow this suite's own module.
    sys.path.append(ALPHA_DIR)

import measure_edges  # noqa: E402

QUADS_JSON = os.path.join(HERE, "quads.json")
MAPPING_JSON = os.path.join(HERE, "capture_mapping.json")

NATIVE_WIDTH = 256
NATIVE_HEIGHT = 192
# A white polygon expands 5-bit 31 to 255; the black clear is 0.  The midpoint
# is a safe split that survives the 1.25x capture resampling smear as well.
WHITE_THRESHOLD = 128.0
# Rows/columns searched around a nominal rectangle when reading back its
# actual extent.  Two DS pixels absorb a one-row/one-column placement offset.
SEARCH_MARGIN = 2
# The two bands that touch the top and bottom framebuffer scanlines anchor the
# frame origin; a per-primitive offset leaves them at nominal.
ANCHORS = ("top_band", "bottom_band")


def load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def luminance(pixel):
    return (pixel[0] + pixel[1] + pixel[2]) / 3.0


def is_white(pixel):
    return luminance(pixel) >= WHITE_THRESHOLD


def white_runs(flags):
    """Contiguous True runs as (first, last) index pairs."""
    runs = []
    start = None
    for index, flag in enumerate(flags):
        if flag and start is None:
            start = index
        elif not flag and start is not None:
            runs.append((start, index - 1))
            start = None
    if start is not None:
        runs.append((start, len(flags) - 1))
    return runs


def format_runs(runs):
    return ",".join("%d-%d" % run for run in runs) if runs else "none"


def _search_box(item, width, height):
    x0 = max(0, item["x"] - SEARCH_MARGIN)
    y0 = max(0, item["y"] - SEARCH_MARGIN)
    x1 = min(width - 1, item["x"] + item["w"] - 1 + SEARCH_MARGIN)
    y1 = min(height - 1, item["y"] + item["h"] - 1 + SEARCH_MARGIN)
    return x0, y0, x1, y1


def _item_extent(pixels, width, height, item, axis):
    """(first, last) white row or column of one item, or None."""
    x0, y0, x1, y1 = _search_box(item, width, height)
    found = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if is_white(pixels[y * width + x]):
                found.append(x if axis == "cols" else y)
    if not found:
        return None
    return min(found), max(found)


def classify_offsets(offsets):
    """Classify per-item whole-pixel row offsets (see the module docstring)."""
    if not offsets:
        return "no-data"
    values = set(offsets.values())
    if values == {0}:
        return "none"
    if values == {1}:
        return "uniform+1"
    anchors_nominal = all(name in offsets and offsets[name] == 0
                          for name in ANCHORS)
    if anchors_nominal and any(value == 1 for value in values) \
            and values <= {0, 1}:
        return "partial+1"
    return "mixed"


def _cell(value):
    return "none" if value is None else "%d" % value


def _range_label(extent):
    if extent is None:
        return "none"
    return "%d-%d" % extent


def _shift_label(low_white, high_white):
    """Whole-frame shift implied by whether both screen edges are painted."""
    if low_white and high_white:
        return "none"
    if high_white:
        return "low-edge-clear"
    if low_white:
        return "high-edge-clear"
    return "both-edges-clear"


def analyse_native(pixels, width, height, data):
    """Whole-frame and per-item white extents on a native frame."""
    row_flags = [False] * height
    col_flags = [False] * width
    for y in range(height):
        for x in range(width):
            if is_white(pixels[y * width + x]):
                row_flags[y] = True
                col_flags[x] = True

    rows = white_runs(row_flags)
    cols = white_runs(col_flags)
    top_white = bool(row_flags[0])
    bottom_white = bool(row_flags[height - 1])
    left_white = bool(col_flags[0])
    right_white = bool(col_flags[width - 1])
    result = {
        "width": width,
        "height": height,
        "row_runs": rows,
        "col_runs": cols,
        "first_white_row": rows[0][0] if rows else None,
        "last_white_row": rows[-1][1] if rows else None,
        "first_white_col": cols[0][0] if cols else None,
        "last_white_col": cols[-1][1] if cols else None,
        "frame_top_row_white": top_white,
        "frame_bottom_row_white": bottom_white,
        "frame_left_col_white": left_white,
        "frame_right_col_white": right_white,
        "frame_vertical_shift": _shift_label(top_white, bottom_white),
        "frame_horizontal_shift": _shift_label(left_white, right_white),
        "items": [],
    }

    for item in data["quads"]:
        nominal_rows = (item["y"], item["y"] + item["h"] - 1)
        nominal_cols = (item["x"], item["x"] + item["w"] - 1)
        measured_rows = _item_extent(pixels, width, height, item, "rows")
        measured_cols = _item_extent(pixels, width, height, item, "cols")
        result["items"].append({
            "name": item["name"],
            "nominal_rows": nominal_rows,
            "nominal_cols": nominal_cols,
            "measured_rows": measured_rows,
            "measured_cols": measured_cols,
            "row_diff": (None if measured_rows is None
                         else measured_rows[0] - nominal_rows[0]),
            "row_end_diff": (None if measured_rows is None
                             else measured_rows[1] - nominal_rows[1]),
            "col_diff": (None if measured_cols is None
                         else measured_cols[0] - nominal_cols[0]),
            "col_end_diff": (None if measured_cols is None
                             else measured_cols[1] - nominal_cols[1]),
            "row_count": (None if measured_rows is None
                          else measured_rows[1] - measured_rows[0] + 1),
        })
    result["row_offsets"] = {
        entry["name"]: entry["row_diff"] for entry in result["items"]
        if entry["row_diff"] is not None}
    result["classification"] = classify_offsets(result["row_offsets"])
    return result


def print_native(result):
    print("mode=native")
    print("size=%dx%d" % (result["width"], result["height"]))
    print("white_threshold=%.1f" % WHITE_THRESHOLD)
    print("first_white_row=%s" % _cell(result["first_white_row"]))
    print("last_white_row=%s" % _cell(result["last_white_row"]))
    print("first_white_col=%s" % _cell(result["first_white_col"]))
    print("last_white_col=%s" % _cell(result["last_white_col"]))
    print("white_row_runs=%s" % format_runs(result["row_runs"]))
    print("white_col_runs=%s" % format_runs(result["col_runs"]))
    print("frame_top_row_white=%d" % result["frame_top_row_white"])
    print("frame_bottom_row_white=%d" % result["frame_bottom_row_white"])
    print("frame_left_col_white=%d" % result["frame_left_col_white"])
    print("frame_right_col_white=%d" % result["frame_right_col_white"])
    print("frame_vertical_shift=%s" % result["frame_vertical_shift"])
    print("frame_horizontal_shift=%s" % result["frame_horizontal_shift"])
    for entry in result["items"]:
        print("item=%s nominal_rows=%d-%d nominal_cols=%d-%d "
              "measured_rows=%s measured_cols=%s row_diff=%s row_end_diff=%s "
              "col_diff=%s col_end_diff=%s row_count=%s"
              % (entry["name"], entry["nominal_rows"][0],
                 entry["nominal_rows"][1], entry["nominal_cols"][0],
                 entry["nominal_cols"][1],
                 _range_label(entry["measured_rows"]),
                 _range_label(entry["measured_cols"]),
                 _cell(entry["row_diff"]), _cell(entry["row_end_diff"]),
                 _cell(entry["col_diff"]), _cell(entry["col_end_diff"]),
                 _cell(entry["row_count"])))
    print("classification=%s" % result["classification"])
    if result["classification"] == "uniform+1":
        print("classification_note=uniform shifts of every measured item are "
              "consistent with both a whole-frame shift and a per-primitive "
              "rasteriser offset; source read decides")


def _capture_row_offsets(observations, data):
    """Per-item row offset in DS px from the top (else bottom) measured edge."""
    grouped = {}
    for obs in observations:
        grouped.setdefault(obs["rect"], {})[obs["side"]] = obs
    offsets = {}
    for item in data["quads"]:
        sides = grouped.get(item["name"], {})
        top = sides.get("top")
        bottom = sides.get("bottom")
        if top is not None and top["estimate"] is not None:
            offsets[item["name"]] = top["estimate"] - top["nominal"]
        elif bottom is not None and bottom["estimate"] is not None:
            offsets[item["name"]] = bottom["estimate"] - bottom["nominal"]
    return offsets


def print_capture(observations, levels, mapping, data):
    merged = [dict(obs, estimates={"hw": obs["estimate"]})
              for obs in observations]
    print("mode=capture")
    print("mapping scale_x=%g scale_y=%g offset_x=%g offset_y=%g"
          % (mapping["scale_x"], mapping["scale_y"],
             mapping["offset_x"], mapping["offset_y"]))
    print("white_threshold=%.1f" % WHITE_THRESHOLD)
    for backdrop in data["backdrops"]:
        print("backdrop %s level=%.4f"
              % (backdrop["name"], levels[backdrop["name"]]))
    for line in measure_edges.compare(merged, ["hw"]):
        print(line)
    offsets = _capture_row_offsets(observations, data)
    for name in [item["name"] for item in data["quads"]]:
        if name in offsets:
            print("item=%s row_offset=%+.4f" % (name, offsets[name]))
    whole = {name: int(round(value)) for name, value in offsets.items()}
    print("classification=%s" % classify_offsets(whole))


def usage():
    sys.stderr.write(
        "usage: measure_rows.py [--native] [--mapping M.json] "
        "<frame.ppm|capture.png>\n")


def main(argv):
    native = False
    mapping_path = MAPPING_JSON
    positional = []
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--native":
            native = True
        elif arg == "--mapping":
            if not args:
                sys.stderr.write("measure_rows: --mapping needs a path\n")
                return 2
            mapping_path = args.pop(0)
        elif arg.startswith("--"):
            sys.stderr.write("measure_rows: unknown option %s\n" % arg)
            return 2
        else:
            positional.append(arg)
    if len(positional) != 1:
        usage()
        return 2

    try:
        data = load_json(QUADS_JSON)
        mapping = measure_edges.identity_mapping() if native \
            else load_json(mapping_path)
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_rows: cannot read json: %s\n" % exc)
        return 2
    try:
        pixels, width, height = measure_edges.read_image(positional[0])
    except ValueError as exc:
        sys.stderr.write("measure_rows: %s\n" % exc)
        return 2

    if native:
        if (width, height) != (NATIVE_WIDTH, NATIVE_HEIGHT):
            sys.stderr.write("measure_rows: native frame must be %dx%d, got "
                             "%dx%d\n" % (NATIVE_WIDTH, NATIVE_HEIGHT,
                                          width, height))
            return 2
        print_native(analyse_native(pixels, width, height, data))
        return 0

    try:
        observations, levels = measure_edges.measure_image(
            pixels, width, height, mapping, data)
    except ValueError as exc:
        sys.stderr.write("measure_rows: %s\n" % exc)
        return 2
    print_capture(observations, levels, mapping, data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
