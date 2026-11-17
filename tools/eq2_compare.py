#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EQ-2b: compare our GX interpreter geometry against a DeSmuME geometry dump.

Stage EQ-2b of docs/EQUALITY_CHECK_DESIGN.md section 4 (EQ-2) and section 5.2.
The harness that produces the reference dump is the out-of-tree GPL repository
twlhd-desmume-harness; only its documented text format ("geometry-dump 1") is
read here. No harness or DeSmuME source is used.

Inputs:

* harness geometry dump (text, format "geometry-dump 1"), one frame;
* our interpreter's clip-space output (text, format "twlhd-eq2-clip 1"),
  produced by tests/host/build/gx_interp_cli --eq2 <frame_v2.bin>.

Our clip-space vertices are converted to DS screen space with DeSmuME's own
integer viewport formula (gfx3d.cpp:3287-3324) and compared per polygon and per
vertex under the tolerances of docs/EQUALITY_CHECK_DESIGN.md section 5.2.
docs/EQ2_COMPARE.md documents both formats, the unit conversions, the frame
alignment, and the exact tolerances with line citations.

All results are EMULATOR: the reference dump is DeSmuME data and the our-side
numbers are produced by host arithmetic against it (no console involved).

Exit codes: 0 every compared field within tolerance and counts consumed
exactly; 1 any mismatch (worst deviations with polygon/vertex indices are
printed); 2 usage or format error. Stdlib only, deterministic.
"""

import argparse
import math
import struct
import sys

# --- Tolerances: every number is cited in docs/EQ2_COMPARE.md -------------
#
# x/y: 1 DS pixel in 16.16 units. docs/EQUALITY_CHECK_DESIGN.md section 5.2
#      "Screen space" (lines 610-613): 0.004 clip units = 0.512 px, plus the
#      1/65536 integer-divide truncation; 1 DS px is the safe bound.
TOL_XY_16_16 = 65536
#
# w: 0.004 clip units = 16 quanta of 20.12. section 5.2 "Clip space"
#    (lines 603-609), reused because it is already justified by measurement.
TOL_W_QUANTA_20_12 = 16
#
# z: the same 0.004 clip-component tolerance, propagated through the
#    documented 0.31 conversion per vertex (see _z_tolerance).
TOL_CLIP_Z = 0.004
#
# colour: +-1 5-bit level = +-2 LSB in the harness's 6-bit channels.
#         section 5.2 final bullet (lines 628-630), Run-18 agreement.
TOL_COLOUR_6BIT = 2

DS_SCREEN_HEIGHT = 192

# Count enforcement is a module flag only so the mutant test can drop it and
# show the count negative tests fail. Production always keeps it True.
ENFORCE_COUNTS = True

HARNESS_MAGIC = "twlhd-desmume-harness "
HARNESS_FORMAT = "format geometry-dump 1"
OURS_MAGIC = "twlhd-eq2-clip 1"

HARNESS_HEADER_KEYS = (
    "emulator",
    "label",
    "rom-sha256",
    "requested-frame",
    "captured-frame",
    "polygon-count",
    "vertex-count",
    "raw-polygon-count",
    "raw-vertex-count",
    "viewport-mode",
)

VTX_KEYS = (
    "pos-x-16.16",
    "pos-y-16.16",
    "pos-z-0.31",
    "pos-w-20.12",
    "tex-s-12.4",
    "tex-t-12.4",
    "col-r-6bit",
    "col-g-6bit",
    "col-b-6bit",
    "col-a-u8",
)

FIELD_ORDER = ("x", "y", "z", "w", "r", "g", "b", "tex-s", "tex-t")

FIELD_UNITS = {
    "x": "16.16 px",
    "y": "16.16 px",
    "z": "0.31",
    "w": "20.12",
    "r": "6-bit",
    "g": "6-bit",
    "b": "6-bit",
    "tex-s": "12.4",
    "tex-t": "12.4",
}


class FormatError(Exception):
    """Malformed input or unusable selection: exit 2."""


class CountError(Exception):
    """Missing/extra vertex or polygon, or a count mismatch: exit 1."""


def _trunc_div(numerator, denominator):
    """C integer division (truncates toward zero)."""
    quotient = abs(numerator) // abs(denominator)
    if (numerator < 0) != (denominator < 0):
        return -quotient
    return quotient


def _quantise(value, scale=4096.0):
    """Round a clip float to the nearest 20.12 integer, halves away from zero."""
    scaled = value * scale
    if scaled >= 0.0:
        return int(math.floor(scaled + 0.5))
    return int(math.ceil(scaled - 0.5))


def _bits_to_float(bits):
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def _float_to_screen(clip, viewport):
    """Apply DeSmuME's integer viewport transform to one clip-space vertex.

    Transcription of gfx3d.cpp:3287-3324 with wScalar = hScalar = 1 (native
    soft-rasteriser framebuffer). `viewport` is (x1, y1, width, height).
    Returns (x_16_16, y_16_16, z_0_31, w_20_12).
    """
    x, y, z, w = clip
    x1, y1, width, height = viewport
    xq = _quantise(x)
    yq = _quantise(y)
    zq = _quantise(z)
    wq = _quantise(w)
    if wq != 0:
        sx = _trunc_div((xq + wq) * (width << 16), 2 * wq)
        sy = _trunc_div((yq + wq) * (height << 16), 2 * wq)
        sz = _trunc_div((zq + wq) * (1 << 31), 2 * wq)
    else:
        sx = xq * (width << 16)
        sy = yq * (height << 16)
        sz = zq * (1 << 31)
    sx += x1 << 16
    sy += y1 << 16
    sy = (DS_SCREEN_HEIGHT << 16) - sy
    sz = max(0, min(0x7FFFFFFF, sz))
    return sx, sy, sz, wq


def _z_tolerance(w_clip):
    """0.31 tolerance equivalent of 0.004 clip z at this vertex's w.

    z_screen = ((z + w) / (2w)) * 2^31, so dz_screen/dz = 2^31 / (2w).
    """
    if w_clip == 0.0:
        return 1 << 31
    return int(math.floor(TOL_CLIP_Z * (1 << 31) / (2.0 * abs(w_clip))))


def _rgb5_to_6bit(channel):
    """Expand the interpreter's 0..1 (k/31) channel to the harness's 6 bits."""
    k = int(math.floor(max(0.0, min(1.0, channel)) * 31.0 + 0.5))
    return (k * 63 * 2 + 31) // 62


def _int(field, text):
    try:
        return int(text, 0) if text.lower().startswith("0x") else int(text, 10)
    except ValueError:
        raise FormatError("%s: not an integer: %r" % (field, text))


# --------------------------------------------------------------------------
# Input parsing
# --------------------------------------------------------------------------

def _read_lines(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read().splitlines()
    except OSError as exc:
        raise FormatError("cannot read %s: %s" % (path, exc))


def load_harness(path):
    lines = _read_lines(path)
    while lines and not lines[-1].strip():
        lines.pop()
    pos = [0]

    def take(what):
        if pos[0] >= len(lines):
            raise FormatError("truncated dump: expected %s" % what)
        line = lines[pos[0]]
        pos[0] += 1
        return line

    if not take("magic").startswith(HARNESS_MAGIC):
        raise FormatError("bad harness magic line")
    if take("format line") != HARNESS_FORMAT:
        raise FormatError("unsupported harness format line")

    header = {}
    while True:
        line = take("header or begin")
        fields = line.split(None, 1)
        if fields == ["begin"]:
            break
        if len(fields) != 2:
            raise FormatError("bad header line: %r" % line)
        header[fields[0]] = fields[1]
    for key in HARNESS_HEADER_KEYS:
        if key not in header:
            raise FormatError("missing header field: %s" % key)

    polys = []
    while True:
        line = take("poly or end")
        fields = line.split()
        if fields == ["end"]:
            break
        if not fields or fields[0] != "poly":
            raise FormatError("expected poly or end, got: %r" % line)
        if len(fields) != 10 or fields[2] != "verts" or fields[4] != "prim" \
                or fields[6] != "vtxformat" or fields[8] != "rawpoly":
            raise FormatError("bad poly line: %r" % line)
        poly = {
            "index": _int("poly", fields[1]),
            "verts": _int("verts", fields[3]),
            "prim": fields[5],
            "vtxformat": fields[7],
            "rawpoly": _int("rawpoly", fields[9]),
            "v": [],
        }
        if poly["verts"] not in (3, 4):
            raise CountError(
                "poly %d: %d vertices (clipped polygons are unsupported)"
                % (poly["index"], poly["verts"]))
        if (poly["prim"] == "triangle") != (poly["verts"] == 3):
            raise CountError("poly %d: prim/verts disagree" % poly["index"])

        for name in ("attr", "texparam", "texpalette"):
            line = take(name)
            fields = line.split()
            if len(fields) != 2 or fields[0] != name:
                raise FormatError("bad %s line: %r" % (name, line))
            _int(name, fields[1])

        fields = take("viewport").split()
        if len(fields) != 5 or fields[0] != "viewport":
            raise FormatError("bad viewport line")
        poly["viewport"] = tuple(_int("viewport", f) for f in fields[1:5])
        if poly["viewport"][2] <= 0 or poly["viewport"][3] <= 0:
            raise FormatError("poly %d: empty viewport" % poly["index"])

        fields = take("viewport-word").split()
        if len(fields) != 2 or fields[0] != "viewport-word":
            raise FormatError("bad viewport-word line")
        _int("viewport-word", fields[1])

        fields = take("rawverts").split()
        if len(fields) < 2 or fields[0] != "rawverts":
            raise FormatError("bad rawverts line")
        raw_count = _int("rawverts", fields[1])
        if len(fields) != raw_count + 2:
            raise FormatError("rawverts: field count does not match")
        if raw_count != poly["verts"]:
            raise CountError(
                "poly %d: rawverts count %d != verts %d"
                % (poly["index"], raw_count, poly["verts"]))

        for vertex_index in range(poly["verts"]):
            fields = take("vtx %d" % vertex_index).split()
            if fields == ["end-poly"]:
                raise CountError(
                    "poly %d: declared %d vertices, found %d"
                    % (poly["index"], poly["verts"], vertex_index))
            if len(fields) != 2 or fields[0] != "vtx":
                raise FormatError("bad vtx line: %r" % " ".join(fields))
            if _int("vtx", fields[1]) != vertex_index:
                raise FormatError("poly %d: vtx index out of order" % poly["index"])
            vertex = {}
            for key in VTX_KEYS:
                fields = take(key).split()
                if len(fields) != 2 or fields[0] != key:
                    raise FormatError(
                        "poly %d vtx %d: bad field %r"
                        % (poly["index"], vertex_index, key))
                vertex[key] = _int(key, fields[1])
            poly["v"].append(vertex)

        fields = take("end-poly").split()
        if fields == ["vtx"] or (fields and fields[0] == "vtx"):
            raise CountError(
                "poly %d: more vertices than the declared %d"
                % (poly["index"], poly["verts"]))
        if fields != ["end-poly"]:
            raise FormatError("expected end-poly, got: %r" % " ".join(fields))
        polys.append(poly)

    if pos[0] != len(lines):
        raise FormatError("trailing content after the final end")

    header["polys"] = polys
    header["requested_frame"] = _int("requested-frame", header["requested-frame"])
    header["captured_frame"] = _int("captured-frame", header["captured-frame"])
    header["polygon_count"] = _int("polygon-count", header["polygon-count"])
    header["vertex_count"] = _int("vertex-count", header["vertex-count"])

    if ENFORCE_COUNTS and header["polygon_count"] != len(polys):
        raise CountError(
            "polygon-count header %d != %d polygon blocks"
            % (header["polygon_count"], len(polys)))
    actual_vertices = sum(poly["verts"] for poly in polys)
    if ENFORCE_COUNTS and header["vertex_count"] != actual_vertices:
        raise CountError(
            "vertex-count header %d != %d dumped vertices"
            % (header["vertex_count"], actual_vertices))
    return header


def load_ours(path):
    lines = _read_lines(path)
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines or lines[0] != OURS_MAGIC:
        raise FormatError("bad interpreter dump magic line")
    if len(lines) < 3:
        raise FormatError("truncated interpreter dump")

    fields = lines[1].split()
    if len(fields) != 2 or fields[0] != "frame":
        raise FormatError("bad frame line")
    frame = _int("frame", fields[1])

    fields = lines[2].split()
    if len(fields) != 6 or fields[0] != "viewport":
        raise FormatError("bad viewport line")
    viewport = tuple(_int("viewport", f) for f in fields[1:5])
    present = _int("viewport present", fields[5])
    if present:
        if viewport[2] < viewport[0] or viewport[3] < viewport[1]:
            raise FormatError("interpreter viewport is inverted")
        viewport = (
            viewport[0], viewport[1],
            viewport[2] - viewport[0] + 1, viewport[3] - viewport[1] + 1,
        )
    else:
        viewport = (0, 0, 256, 192)

    vertices = []
    stats = None
    for line in lines[3:]:
        fields = line.split()
        if not fields:
            raise FormatError("blank line in interpreter dump")
        if fields[0] == "stats":
            if stats is not None:
                raise FormatError("duplicate stats line")
            stats = {}
            for item in fields[1:]:
                if "=" not in item:
                    raise FormatError("bad stats item: %r" % item)
                key, value = item.split("=", 1)
                stats[key] = _int("stats " + key, value)
            continue
        if stats is not None:
            raise FormatError("content after the stats line")
        if fields[0] != "v":
            raise FormatError("unexpected line: %r" % line)
        if len(fields) != 14:
            raise FormatError("v line: expected 14 fields, got %d" % len(fields))
        if _int("v index", fields[1]) != len(vertices):
            raise FormatError("v indices are not sequential")
        bits = []
        for token in fields[2:12]:
            if len(token) != 8:
                raise FormatError("v: expected 8 hex digits, got %r" % token)
            bits.append(_int("v bits", "0x" + token))
        vertices.append({
            "clip": tuple(_bits_to_float(b) for b in bits[0:4]),
            "rgba": tuple(_bits_to_float(b) for b in bits[4:8]),
            "s": _bits_to_float(bits[8]),
            "t": _bits_to_float(bits[9]),
            "param": _int("param", fields[12]),
            "pltt": _int("pltt", fields[13]),
        })

    if stats is None:
        raise FormatError("missing stats line")
    for key in ("capacity_dropped", "geometry_incomplete", "dropped_incomplete"):
        if key not in stats:
            raise FormatError("stats line is missing %s" % key)
    return {"frame": frame, "viewport": viewport, "vertices": vertices,
            "stats": stats}


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

def _harness_viewport(poly):
    x, y, width, height = poly["viewport"]
    return (x, y, width, height)


def _corners(ours, start, count):
    """Our-side polygon corners and their global vertex indices.

    A quad is emitted as (v0,v1,v2) + (v0,v2,v3) by twlhd_gx_interp.c, so the
    polygon order is v0,v1,v2,v3 = segment[0], [1], [2], [5].
    """
    if count == 3:
        return [(ours[start + i], start + i) for i in range(3)]
    return [(ours[start + i], start + i) for i in (0, 1, 2, 5)]


def _best_rotation(corners_screen, vertices):
    count = len(corners_screen)
    best_score = None
    best_rotation = 0
    for rotation in range(count):
        score = 0
        for vertex_index in range(count):
            ours = corners_screen[(vertex_index + rotation) % count]
            theirs = vertices[vertex_index]
            score += abs(ours[0] - theirs["pos-x-16.16"])
            score += abs(ours[1] - theirs["pos-y-16.16"])
            score += abs(ours[3] - theirs["pos-w-20.12"])
        if best_score is None or score < best_score:
            best_score = score
            best_rotation = rotation
    return best_rotation


def compare(harness, ours):
    """Return (exit_code, report_lines) for loaded inputs."""
    report = []
    mismatches = []
    worst = dict.fromkeys(FIELD_ORDER)

    def note(field, deviation, tolerance, poly, vertex_index, our_index,
             rawpoly):
        record = {
            "deviation": deviation,
            "tolerance": tolerance,
            "poly": poly,
            "rawpoly": rawpoly,
            "vertex": vertex_index,
            "our": our_index,
        }
        current = worst[field]
        if current is None or deviation > current["deviation"]:
            worst[field] = record
        if tolerance is not None and deviation > tolerance:
            mismatches.append((field, record))

    our_viewport = ours["viewport"]
    polys = sorted(harness["polys"], key=lambda poly: poly["rawpoly"])

    try:
        if ENFORCE_COUNTS:
            raw_indices = [poly["rawpoly"] for poly in polys]
            if raw_indices != list(range(len(polys))):
                raise CountError(
                    "harness rawpoly indices are not 0..%d: %r"
                    % (len(polys) - 1, raw_indices))

        triangle = 0
        for poly in polys:
            expected_viewport = _harness_viewport(poly)
            if our_viewport != expected_viewport:
                raise CountError(
                    "viewport mismatch at poly %d: ours %r harness %r"
                    % (poly["index"], our_viewport, expected_viewport))

            count = poly["verts"]
            corner_count = 3 if count == 3 else 4
            span = 6 if count == 4 else 3
            if triangle + span > len(ours["vertices"]):
                raise CountError(
                    "poly %d: our side ran out of triangle vertices"
                    % poly["index"])
            corners = _corners(ours["vertices"], triangle, corner_count)
            screen = [
                _float_to_screen(vertex["clip"], expected_viewport)
                for vertex, _index in corners
            ]
            rotation = _best_rotation(screen, poly["v"])
            for vertex_index in range(count):
                ours_screen = screen[(vertex_index + rotation) % count]
                source_vertex, our_index = corners[(vertex_index + rotation) % count]
                theirs = poly["v"][vertex_index]
                rawpoly = poly["rawpoly"]

                note("x", abs(ours_screen[0] - theirs["pos-x-16.16"]),
                     TOL_XY_16_16, poly["index"], vertex_index, our_index, rawpoly)
                note("y", abs(ours_screen[1] - theirs["pos-y-16.16"]),
                     TOL_XY_16_16, poly["index"], vertex_index, our_index, rawpoly)
                note("w", abs(ours_screen[3] - theirs["pos-w-20.12"]),
                     TOL_W_QUANTA_20_12, poly["index"], vertex_index,
                     our_index, rawpoly)
                note("z", abs(ours_screen[2] - theirs["pos-z-0.31"]),
                     _z_tolerance(source_vertex["clip"][3]), poly["index"],
                     vertex_index, our_index, rawpoly)

                rgba = source_vertex["rgba"]
                for field, channel, key in (
                    ("r", 0, "col-r-6bit"),
                    ("g", 1, "col-g-6bit"),
                    ("b", 2, "col-b-6bit"),
                ):
                    note(field, abs(_rgb5_to_6bit(rgba[channel]) - theirs[key]),
                         TOL_COLOUR_6BIT, poly["index"], vertex_index,
                         our_index, rawpoly)

                # Texture coordinates are reported, never gated: section 5.2
                # derives no tex tolerance.
                s16 = int(math.floor(source_vertex["s"] * 16.0 + 0.5))
                t16 = int(math.floor(source_vertex["t"] * 16.0 + 0.5))
                note("tex-s", abs(s16 - theirs["tex-s-12.4"]), None,
                     poly["index"], vertex_index, our_index, rawpoly)
                note("tex-t", abs(t16 - theirs["tex-t-12.4"]), None,
                     poly["index"], vertex_index, our_index, rawpoly)

            triangle += span

        if ENFORCE_COUNTS and triangle != len(ours["vertices"]):
            raise CountError(
                "our side has %d triangle vertices, the harness mapping used %d"
                % (len(ours["vertices"]), triangle))

        for key in ("capacity_dropped", "geometry_incomplete",
                    "dropped_incomplete"):
            if ours["stats"][key] != 0:
                raise CountError(
                    "interpreter stats %s=%d (truncated geometry)"
                    % (key, ours["stats"][key]))
    except CountError as exc:
        mismatch = str(exc)
    else:
        mismatch = None

    for field in FIELD_ORDER:
        record = worst[field]
        if record is None:
            continue
        tolerance = record["tolerance"]
        if tolerance is None:
            tolerance_text = "reported, not gated"
        else:
            tolerance_text = "tol %d" % tolerance
        report.append(
            "%s: worst %d %s %s at poly %d (rawpoly %d) vertex %d "
            "(our vertex %d)"
            % (field, record["deviation"], FIELD_UNITS[field], tolerance_text,
               record["poly"], record["rawpoly"], record["vertex"],
               record["our"]))

    if mismatches:
        for field, record in mismatches:
            report.append(
                "MISMATCH %s: %d > %d at poly %d (rawpoly %d) vertex %d "
                "(our vertex %d)"
                % (field, record["deviation"], record["tolerance"],
                   record["poly"], record["rawpoly"], record["vertex"],
                   record["our"]))
    if mismatch is not None:
        report.append("MISMATCH: %s" % mismatch)

    if mismatches or mismatch is not None:
        return 1, report
    report.insert(
        0, "PASS frame=%d polygons=%d vertices=%d"
        % (harness["captured_frame"], len(polys),
           sum(poly["verts"] for poly in polys)))
    return 0, report


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compare our GX interpreter clip output against a DeSmuME "
                    "geometry dump (EQ-2b).")
    parser.add_argument("harness", help="harness geometry-dump 1 text file")
    parser.add_argument("ours", help="gx_interp_cli --eq2 text file")
    parser.add_argument(
        "--harness-frame", type=int, default=242,
        help="expected harness frame (default 242: ROM frame 240 + boot offset)")
    parser.add_argument(
        "--ours-frame", type=int, default=241,
        help="expected our-side frame (default 241: the v2 recorder frame index "
             "that maps to harness frame 242; docs/EQ2_COMPARE.md section 3)")
    args = parser.parse_args(argv)

    try:
        harness = load_harness(args.harness)
        ours = load_ours(args.ours)
        if (args.harness_frame != harness["requested_frame"]
                or args.harness_frame != harness["captured_frame"]):
            raise FormatError(
                "harness frame %d is not present (dump has requested %d, "
                "captured %d)"
                % (args.harness_frame, harness["requested_frame"],
                   harness["captured_frame"]))
        if args.ours_frame != ours["frame"]:
            raise FormatError(
                "ours frame %d is not present (dump has frame %d)"
                % (args.ours_frame, ours["frame"]))
        code, report = compare(harness, ours)
    except FormatError as exc:
        print("eq2: format error: %s" % exc, file=sys.stderr)
        return 2
    except CountError as exc:
        print("eq2: mismatch: %s" % exc, file=sys.stderr)
        return 1

    for line in report:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main())
