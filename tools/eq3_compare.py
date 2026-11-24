#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EQ-3b: structural picture comparison, our PICA200 path vs DeSmuME.

Stage EQ-3b of docs/EQUALITY_CHECK_DESIGN.md section 4 (EQ-3) and section 5.1.
The reference is the 256x192 P6 PPM written by the out-of-tree GPL harness
twlhd-desmume-harness (DeSmuME software rasteriser, EMULATOR); the our-side
picture is the top screen of an Azahar libretro host run of the same frame
(also EMULATOR). No harness or DeSmuME source is used here.

The comparison is STRUCTURAL, not per pixel. The DS 256x192 viewport sits in
the centred 320x240 region of the 400x240 top screen (40 px pillar bars), so
our picture is sampled back onto the DS grid at the mapped cell centres
(1.25 screen px per DS px). On both sides every DS cell is classified into one
of the six cube face colours (or background), edge pixels are eroded by
EROSION DS px, and per class bounding boxes, centroids and interior colours
are compared under TOL_POS and TOL_COLOUR.

Colour expansion differs on the two sides: DeSmuME passes 5-bit through a
6-bit intermediate and the harness writes (v<<2)|(v>>4); our path expands
5-bit as (v<<3)|(v>>2). For a shared 5-bit code the two 8-bit values differ by
at most 2, which TOL_COLOUR = 8 absorbs with margin. docs/EQ3_COMPARE.md shows
the arithmetic.

Exit codes: 0 every compared field within tolerance; 1 structural mismatch
(worst deviation, face and field are named); 2 usage or format error (bad PPM,
wrong dimensions, an input with none of the expected face classes). A face
that is invisible on both sides (fully occluded) is reported and skipped; a
face visible on exactly one side, or a differing visible-face count, is a
mismatch (exit 1), never a silent pass.

All results are EMULATOR. Stdlib only, deterministic.
"""

import argparse
import sys

# --- Tolerances: derivation in docs/EQ3_COMPARE.md -----------------------
#
# 2 DS px: 1.25 screen px/DS px sampled at a cell centre leaves <= 0.5 screen
# px = 0.4 DS px of rounding, plus the observed ~1 px rasteriser/capture
# shift; <= 1.4 DS px, rounded up to 2. section 5.1(vi).
TOL_POS = 2

# 8 per channel: one full 5-bit level; the 6-bit intermediate of the DeSmuME
# colour chain adds up to one more level (<= 8). section 5.1(i).
TOL_COLOUR = 8

# 3 DS px > TOL_POS, so no interior sample can cross an edge even at the
# maximum combined shift.
EROSION = 3

# Interior colour sample grid per face (n x n cell centres).
COLOUR_GRID = 5

# Module flags only so the mutant tests can drop the checks and show the
# negative tests fail. Production always keeps them True.
ENFORCE_POS = True
ENFORCE_COLOUR = True

# Face colour classes of the default-cube frame. Sources are the 6-bit
# col-*-6bit values of tools/fixtures/eq2/harness_cube_f240.txt (63/0 from the
# 5-bit source), expanded to 8-bit; background is per side below.
FACE_CLASSES = (
    ("magenta", (255, 0, 255)),
    ("red", (255, 0, 0)),
    ("yellow", (255, 255, 0)),
    ("green", (0, 255, 0)),
    ("blue", (0, 0, 255)),
    ("cyan", (0, 255, 255)),
)

# DeSmuME's software buffer is cleared to black; our renderer clears to the
# RENDER_CLEAR_COLOR 0x101018FF bytes (16, 16, 24). Both are background.
BACKGROUND_COLOURS = ((0, 0, 0), (16, 16, 24))

# Our top screen: the DS 256x192 viewport is the centred 320x240 region with
# 40 px pillar bars (common/twlhd_clipfix.c, docs/PHASE5A_V2_RENDERER_DESIGN.md
# lines 48-49). 0.8 = 320/400 is the clipfix factor; 1.25 = 320/256 is its
# inverse, used here to sample the screen back onto the DS grid.
SCREEN_X0 = 40
SCREEN_W = 320
SCREEN_H = 240
DS_W = 256
DS_H = 192


class FormatError(Exception):
    """Malformed input or unusable picture: exit 2."""


def parse_ppm(path):
    """Parse a binary P6 PPM. Returns (width, height, bytearray RGB data)."""
    with open(path, "rb") as handle:
        header = handle.readline()
        if header.strip() != b"P6":
            raise FormatError("%s: expected a P6 PPM" % path)
        while True:
            line = handle.readline()
            if not line:
                raise FormatError("%s: truncated header" % path)
            line = line.split(b"#", 1)[0].strip()
            if line:
                break
        parts = line.split()
        if len(parts) != 2:
            raise FormatError("%s: bad dimension line" % path)
        try:
            width, height = int(parts[0]), int(parts[1])
        except ValueError:
            raise FormatError("%s: bad dimension line" % path)
        line = handle.readline()
        if not line:
            raise FormatError("%s: missing maxval" % path)
        try:
            maxval = int(line.split(b"#", 1)[0].strip())
        except ValueError:
            raise FormatError("%s: bad maxval" % path)
        if maxval != 255:
            raise FormatError("%s: unsupported maxval %d" % (path, maxval))
        data = bytearray(handle.read())
    expected = width * height * 3
    if len(data) != expected:
        raise FormatError(
            "%s: expected %d raster bytes, got %d" % (path, expected, len(data)))
    return width, height, data


def pixel_at(data, width, x, y):
    offset = (y * width + x) * 3
    return (data[offset], data[offset + 1], data[offset + 2])


def classify(rgb):
    """Nearest colour class by squared RGB distance (ties to the earlier)."""
    best = None
    candidates = list(FACE_CLASSES) + [
        ("background", colour) for colour in BACKGROUND_COLOURS]
    for name, colour in candidates:
        distance = sum((rgb[i] - colour[i]) ** 2 for i in range(3))
        if best is None or distance < best[0]:
            best = (distance, name)
    return best[1]


def _sample_x(dx):
    """Screen x of the centre of DS column dx (1.25 px per DS px)."""
    return int(SCREEN_X0 + (SCREEN_W / DS_W) * (dx + 0.5) + 0.5)


def _sample_y(dy):
    return int((SCREEN_H / DS_H) * (dy + 0.5) + 0.5)


def _classify_cache(data, width):
    cache = {}

    def at(x, y):
        rgb = pixel_at(data, width, x, y)
        if rgb not in cache:
            cache[rgb] = classify(rgb)
        return cache[rgb]

    return at


def load_picture(path, side, correction="off"):
    """Load a PPM and return the DS-grid colour classes plus sampling helpers.

    side is "harness" (256x192, read directly) or "ours" (400x240, or 400x480
    whose upper 400x240 rows are the top screen).  correction is the EQ-4p
    presentation-row mode for an "ours" picture; the default "off" keeps the
    historical numbers for direct callers, while the CLI defaults to auto.
    """
    width, height, data = parse_ppm(path)
    correction_line = None
    if side == "ours":
        import azahar_presentation
        if correction == "off":
            correction_line = azahar_presentation.format_line(
                False, "--azahar-correction off")
        else:
            pixels = [(data[i], data[i + 1], data[i + 2])
                      for i in range(0, len(data), 3)]
            pixels, applied, reason = azahar_presentation.apply_correction(
                correction, pixels, width, height)
            correction_line = azahar_presentation.format_line(applied, reason)
            if applied:
                data = bytearray(channel for rgb in pixels for channel in rgb)
    if side == "harness":
        if (width, height) != (DS_W, DS_H):
            raise FormatError(
                "%s: harness picture must be %dx%d, got %dx%d"
                % (path, DS_W, DS_H, width, height))
        top = height
    else:
        if width != 400 or height not in (SCREEN_H, 480):
            raise FormatError(
                "%s: our picture must be 400x240 or 400x480, got %dx%d"
                % (path, width, height))
        top = SCREEN_H if height == 480 else height

    at = _classify_cache(data, width)
    grid = []
    if side == "harness":
        for dy in range(DS_H):
            grid.append([at(dx, dy) for dx in range(DS_W)])
    else:
        for dy in range(DS_H):
            sy = _sample_y(dy)
            row = []
            for dx in range(DS_W):
                row.append(at(_sample_x(dx), sy))
            grid.append(row)

    return {
        "side": side,
        "path": path,
        "width": width,
        "height": height,
        "top": top,
        "data": data,
        "grid": grid,
        "at": at,
        "correction_line": correction_line,
    }


def mean_colour(picture, dx, dy):
    """3x3 mean of the sampled colour at DS cell (dx, dy), clipped."""
    if picture["side"] == "harness":
        cx, cy = dx, dy
        x0, x1 = max(0, cx - 1), min(DS_W - 1, cx + 1)
        y0, y1 = max(0, cy - 1), min(DS_H - 1, cy + 1)
    else:
        cx, cy = _sample_x(dx), _sample_y(dy)
        x0, x1 = max(0, cx - 1), min(picture["width"] - 1, cx + 1)
        y0, y1 = max(0, cy - 1), min(picture["top"] - 1, cy + 1)
    total = [0, 0, 0]
    count = 0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            rgb = pixel_at(picture["data"], picture["width"], x, y)
            for channel in range(3):
                total[channel] += rgb[channel]
            count += 1
    return tuple(int(round(total[channel] / count)) for channel in range(3))


def class_points(grid, name):
    points = []
    for y, row in enumerate(grid):
        for x, cell in enumerate(row):
            if cell == name:
                points.append((x, y))
    return points


def erode(points, grid, radius=EROSION):
    """Keep pixels whose full (2*radius+1) square is the same class."""
    if radius <= 0:
        return list(points)
    height = len(grid)
    width = len(grid[0])
    kept = []
    for x, y in points:
        x0, x1 = max(0, x - radius), min(width - 1, x + radius)
        y0, y1 = max(0, y - radius), min(height - 1, y + radius)
        ok = True
        for ny in range(y0, y1 + 1):
            for nx in range(x0, x1 + 1):
                if grid[ny][nx] != grid[y][x]:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            kept.append((x, y))
    return kept


def bounds(points):
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    return (min(xs), min(ys), max(xs), max(ys))


def centroid(points):
    return (sum(point[0] for point in points) / len(points),
            sum(point[1] for point in points) / len(points))


def sample_cells(bbox, points):
    """Up to COLOUR_GRID x COLOUR_GRID interior cell centres inside bbox.

    A face is a rotated quad, so its bounding-box corners are not face pixels;
    every sample is snapped to the nearest point of the eroded face mask
    (already EROSION px from any non-class pixel, so the 3x3 mean is interior).
    """
    remaining = set(points)
    cells = []
    for j in range(COLOUR_GRID):
        y = int(bbox[1] + (j + 0.5) * (bbox[3] - bbox[1] + 1) / COLOUR_GRID)
        for i in range(COLOUR_GRID):
            x = int(bbox[0] + (i + 0.5) * (bbox[2] - bbox[0] + 1) / COLOUR_GRID)
            nearest = min(remaining, key=lambda p: (p[0] - x) ** 2 + (p[1] - y) ** 2,
                          default=None)
            if nearest is not None:
                cells.append(nearest)
                remaining.discard(nearest)
    return cells


def _fmt_point(point):
    return "(%.2f,%.2f)" % point


def compare(harness, ours):
    """Return (exit_code, report_lines) for loaded pictures."""
    report = []
    mismatches = []
    worst = {}

    def note(field, deviation, tolerance, face):
        current = worst.get(field)
        if current is None or deviation > current[0]:
            worst[field] = (deviation, face, tolerance)
        if tolerance is not None and deviation > tolerance:
            mismatches.append((field, deviation, tolerance, face))

    hgrid, ogrid = harness["grid"], ours["grid"]
    face_names = [name for name, _colour in FACE_CLASSES]

    present = {}
    for name in face_names:
        present[name] = (class_points(hgrid, name), class_points(ogrid, name))

    h_visible = [name for name in face_names if present[name][0]]
    o_visible = [name for name in face_names if present[name][1]]
    if not h_visible:
        raise FormatError(
            "%s: contains none of the expected face classes" % harness["path"])
    if not o_visible:
        raise FormatError(
            "%s: contains none of the expected face classes" % ours["path"])

    if set(h_visible) != set(o_visible):
        report.append("MISMATCH face set: desmume=%s ours=%s"
                      % (",".join(h_visible) or "-", ",".join(o_visible) or "-"))
        mismatches.append(("face-set", len(set(h_visible) ^ set(o_visible)),
                           0, "visible classes"))

    for name in face_names:
        hpoints, opoints = present[name]
        if not hpoints and not opoints:
            report.append("face %s: not visible (fully occluded on both sides)"
                          % name)
            continue
        if not hpoints:
            report.append("MISMATCH extra face %s: present on ours only" % name)
            mismatches.append(("face-set", 1, 0, name))
            continue
        if not opoints:
            report.append("MISMATCH missing face %s: present on desmume only"
                          % name)
            mismatches.append(("face-set", 1, 0, name))
            continue

        hkeep = erode(hpoints, hgrid)
        okeep = erode(opoints, ogrid)
        if not hkeep or not okeep:
            side = "desmume" if not hkeep else "ours"
            report.append("MISMATCH face %s: eroded to nothing on %s"
                          % (name, side))
            mismatches.append(("eroded", 0, 0, name))
            continue

        hbbox, obbox = bounds(hkeep), bounds(okeep)
        hcentroid, ocentroid = centroid(hkeep), centroid(okeep)
        bounds_dev = tuple(abs(hbbox[i] - obbox[i]) for i in range(4))
        centroid_dev = (abs(hcentroid[0] - ocentroid[0]),
                        abs(hcentroid[1] - ocentroid[1]))
        for field, deviation in zip(
                ("bbox-left", "bbox-top", "bbox-right", "bbox-bottom"),
                bounds_dev):
            note(field, deviation, TOL_POS if ENFORCE_POS else None, name)
        note("centroid-x", centroid_dev[0], TOL_POS if ENFORCE_POS else None,
             name)
        note("centroid-y", centroid_dev[1], TOL_POS if ENFORCE_POS else None,
             name)

        report.append(
            "face %s: desmume bbox=(%d,%d)-(%d,%d) centroid=%s pixels=%d; "
            "ours bbox=(%d,%d)-(%d,%d) centroid=%s pixels=%d; "
            "dev bbox=(%d,%d,%d,%d) centroid=%s"
            % (name, hbbox[0], hbbox[1], hbbox[2], hbbox[3],
               _fmt_point(hcentroid), len(hkeep),
               obbox[0], obbox[1], obbox[2], obbox[3],
               _fmt_point(ocentroid), len(okeep),
               bounds_dev[0], bounds_dev[1], bounds_dev[2], bounds_dev[3],
               _fmt_point(centroid_dev)))

        nominal = dict(FACE_CLASSES)[name]
        # Sample only cells that are interior on BOTH sides, so a 1-2 px
        # structural shift cannot move a colour sample onto an edge.
        shared = sorted(set(hkeep) & set(okeep))
        cells = sample_cells(hbbox, shared)
        if not cells:
            report.append("MISMATCH face %s: no shared interior colour cell" % name)
            mismatches.append(("colour-sample", 0, 0, name))
            cells = []
        hsum = [0, 0, 0]
        osum = [0, 0, 0]
        h_nominal = [0, 0, 0]
        o_nominal = [0, 0, 0]
        diff_sum = [0, 0, 0]
        count = 0
        for dx, dy in cells:
            hmean = mean_colour(harness, dx, dy)
            omean = mean_colour(ours, dx, dy)
            for channel in range(3):
                diff_sum[channel] += abs(hmean[channel] - omean[channel])
                h_nominal[channel] += abs(hmean[channel] - nominal[channel])
                o_nominal[channel] += abs(omean[channel] - nominal[channel])
                hsum[channel] += hmean[channel]
                osum[channel] += omean[channel]
            count += 1
        if count:
            diff = tuple(int(round(diff_sum[channel] / count))
                         for channel in range(3))
            for channel, field in enumerate(("colour-r", "colour-g", "colour-b")):
                tolerance = TOL_COLOUR if ENFORCE_COLOUR else None
                note(field, diff[channel], tolerance, name)
                # A shared wrong rule must not pass: each side is also checked
                # against the declared face colour.
                note(field + "-nominal",
                     max(int(round(h_nominal[channel] / count)),
                         int(round(o_nominal[channel] / count))),
                     tolerance, name)
            report.append(
                "face %s colour: desmume~%s ours~%s diff=%s tol %d"
                % (name,
                   tuple(int(round(hsum[c] / count)) for c in range(3)),
                   tuple(int(round(osum[c] / count)) for c in range(3)),
                   diff, TOL_COLOUR))

    if mismatches:
        for field, deviation, tolerance, face in mismatches:
            report.append("MISMATCH %s: %s > %s at face %s"
                          % (field, deviation, tolerance, face))
        report.append("FAIL faces desmume=%s ours=%s"
                      % (",".join(h_visible), ",".join(o_visible)))
        return 1, report

    worst_field, (worst_deviation, worst_face, _tol) = max(
        worst.items(), key=lambda item: item[1][0])
    report.append(
        "PASS faces=%d worst %s=%s (tol %s) at face %s"
        % (len(h_visible), worst_field, worst_deviation,
           TOL_POS if worst_field.startswith(("bbox", "centroid"))
           else TOL_COLOUR, worst_face))
    return 0, report


def main(argv=None):
    import azahar_presentation
    parser = argparse.ArgumentParser(
        description="Structural EQ-3b picture comparison: DeSmuME software "
                    "rasteriser picture vs our Azahar host picture.")
    parser.add_argument("harness", help="DeSmuME harness P6 PPM (256x192)")
    parser.add_argument("ours", help="our top-screen P6 PPM (400x240 or 400x480)")
    parser.add_argument("--azahar-correction", choices=azahar_presentation.MODES,
                        default="auto",
                        help="EQ-4p presentation-row correction for our "
                             "Azahar picture: auto (row-0 wrap guard, default), "
                             "on (forced), off (never)")
    args = parser.parse_args(argv)

    try:
        harness = load_picture(args.harness, "harness")
        ours = load_picture(args.ours, "ours", args.azahar_correction)
        code, report = compare(harness, ours)
    except (FormatError, OSError) as exc:
        print("eq3: format error: %s" % exc, file=sys.stderr)
        return 2

    print("EQ-3b structural picture comparison (EMULATOR)")
    print(ours["correction_line"])
    for line in report:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main())
