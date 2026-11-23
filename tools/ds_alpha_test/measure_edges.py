#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the sub-pixel DS position of every edge of the ds-alpha-test ROM.

The ROM (tools/ds_alpha_test/quads.json) draws two opaque backdrops and 14
translucent white quads at exactly known DS pixel rectangles.  This tool finds
each rectangle edge to sub-pixel precision in a real-hardware capture and/or a
DeSmuME native frame and compares it with the exact nominal DS coordinate.

Method
------
Coordinates are pixel-corner coordinates on both sides: the DS edge at DS
coordinate ``e`` lies at capture coordinate ``offset + scale * e`` (the inverse
of ``measure_alpha.capture_xy``: a DS pixel centre ``d+0.5`` maps to
``offset + (d+0.5)*scale``).  For each rectangle edge a perpendicular profile
of mean ``(R+G+B)/3`` luminance is built from the capture rows/columns inside an
inset parallel span (>=2 DS px from the corners).  The two plateau levels are
measured locally from the image (never from a colour model):

* interior level: median luminance of the DS-pixel band 2..6 DS px inside the
  rectangle (a 4-DS-px slab, always inside a 16-px-wide quad);
* background level: median luminance of the backdrop's clear top strip
  (``y = backdrop.y+4 .. backdrop.y+28``, above every quad) for a quad edge, or
  the neighbouring backdrop's level for the internal black/grey boundary.

The edge position is where the profile crosses the 50 % level
``(left_level + right_level)/2`` in the expected direction, found by linear
interpolation between the two straddling capture pixels
(``x = (i+0.5) + (mid-a)/(b-a)``, the same form as
``tools/ds_scale_test/fit_mapping.py``).  Edges whose interior/background
contrast is below ``MIN_CONTRAST = 10`` 8-bit levels are skipped; every edge of
this ROM clears it (the smallest is R4 over the grey backdrop, about 16).  A
crossing must be unique in the ``BAND = 5`` capture-px search window and each
plateau must actually be observed on its own side, or the edge is skipped with
a reason.  The 2-DS-px gaps between neighbouring quads (2.5 capture px) put the
neighbouring quad's opposite edge inside the band, but the expected
rising/falling direction rejects it.

Screen-boundary edges (DS x = 0/256, y = 0/192) are skipped with a reason: they
lie on the capture crop boundary, so only one plateau is visible.  The x = 128
black/grey boundary is measurable and is measured once, from the black
backdrop's right edge.  Backdrop left/top edges are the screen boundary or the
duplicate side of that internal boundary, so only backdrop right/bottom edges
are considered.

Output
------
Deterministic ``key=value`` lines, quads.json order, edges in the order
left/right/top/bottom and ``%.4f`` formatting.  Exit 0 on success, 2 on usage /
missing PIL / unreadable input, 1 when no edge could be measured.

    measure_edges.py [--mapping M.json] [--native] [--desmume FRAME.ppm] <image>

* plain ``<image>``        the hardware capture, mapped with ``--mapping``
                           (default capture_mapping.json); column ``hw``;
* ``--native <image>``     a native 256x192 frame (identity mapping); column
                           ``desmume``;
* ``--desmume FRAME.ppm``  the hardware capture plus the DeSmuME native frame:
                           prints ``hw``, ``desmume`` and both differences.
"""

import json
import math
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
QUADS_JSON = os.path.join(HERE, "quads.json")
MAPPING_JSON = os.path.join(HERE, "capture_mapping.json")

NATIVE_WIDTH = 256
NATIVE_HEIGHT = 192

# Search half-width around the predicted edge position, in capture px (same as
# tools/ds_scale_test/fit_mapping.py).
BAND = 5.0
# Minimum interior/background contrast (mean R,G,B, 8-bit levels) for an edge
# to carry sub-pixel information.  The smallest this ROM can produce is R4 over
# the grey backdrop at about 16 levels.
MIN_CONTRAST = 10.0
# A plateau level must be observed on its own side within this fraction of the
# contrast, floored at MIN_PLATEAU_TOL levels.  The floor is 12 (not 6) because
# the 2-DS-px inter-quad gap is only about 1 capture px wide and the capture's
# sharpening kernel pushes that gap sample up to ~10 levels off the true
# backdrop level; 12 still rejects a crossing with no plateau on one side.
PLATEAU_TOL_FRACTION = 0.12
MIN_PLATEAU_TOL = 12.0
# DS-pixel slab used to measure the interior plateau (2..6 DS px inside).
INTERIOR_INSET = 2
INTERIOR_WIDTH = 4
# Parallel span is inset this many DS px from the rectangle corners.
CORNER_INSET = 2
# Clean backdrop strip used for the background plateau (above every quad).
BACKDROP_STRIP_TOP = 4
BACKDROP_STRIP_HEIGHT = 24

EDGES = ("left", "right", "top", "bottom")
AXIS_OF = {"left": "x", "right": "x", "top": "y", "bottom": "y"}
STAT_CLASSES = (("x_left", "x", "left"), ("x_right", "x", "right"),
                ("y_top", "y", "top"), ("y_bottom", "y", "bottom"))


def load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def identity_mapping():
    return {"scale_x": 1.0, "scale_y": 1.0, "offset_x": 0.0, "offset_y": 0.0}


def parse_ppm(path):
    """Parse a binary P6 PPM.  Returns (pixels, width, height)."""
    with open(path, "rb") as handle:
        if handle.readline().strip() != b"P6":
            raise ValueError("%s: expected a P6 PPM" % path)
        while True:
            line = handle.readline()
            if not line:
                raise ValueError("%s: truncated PPM header" % path)
            line = line.split(b"#", 1)[0].strip()
            if line:
                break
        parts = line.split()
        if len(parts) != 2:
            raise ValueError("%s: bad PPM dimension line" % path)
        width, height = int(parts[0]), int(parts[1])
        line = handle.readline()
        if not line:
            raise ValueError("%s: missing PPM maxval" % path)
        if int(line.split(b"#", 1)[0].strip()) != 255:
            raise ValueError("%s: unsupported PPM maxval" % path)
        data = handle.read()
    if len(data) != width * height * 3:
        raise ValueError("%s: expected %d bytes, got %d"
                         % (path, width * height * 3, len(data)))
    return ([(data[i], data[i + 1], data[i + 2])
             for i in range(0, len(data), 3)], width, height)


def read_image(path):
    """Read a PNG (via PIL) or a P6 PPM (standard library)."""
    if path.lower().endswith(".ppm"):
        return parse_ppm(path)
    if Image is None:
        raise ValueError("PIL is not installed (pip install Pillow)")
    try:
        image = Image.open(path)
        image.load()
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read %s: %s" % (path, exc))
    image = image.convert("RGB")
    raw = image.tobytes()
    return ([(raw[i], raw[i + 1], raw[i + 2])
             for i in range(0, len(raw), 3)], image.width, image.height)


def _luminance(pixel):
    return (pixel[0] + pixel[1] + pixel[2]) / 3.0


def _median(values):
    ordered = sorted(values)
    count = len(ordered)
    if count % 2:
        return ordered[count // 2]
    return 0.5 * (ordered[count // 2 - 1] + ordered[count // 2])


def _capture_span(ds_lo, ds_hi, offset, scale, limit):
    """Capture indices whose centre falls inside [offset+scale*lo, hi]."""
    first = int(math.ceil(offset + scale * ds_lo - 0.5))
    last = int(math.ceil(offset + scale * ds_hi - 0.5)) - 1
    return max(0, first), min(limit - 1, last)


def _region_median(pixels, width, height, axis, perp, parallel):
    """Median luminance over a capture-space region.

    ``perp`` and ``parallel`` are (lo, hi) integer capture-index ranges; the
    perpendicular axis is x for a vertical edge profile and y for a horizontal
    one.
    """
    perp_lo, perp_hi = perp
    par_lo, par_hi = parallel
    if perp_lo > perp_hi or par_lo > par_hi:
        return None
    values = []
    if axis == "x":
        for x in range(perp_lo, perp_hi + 1):
            for y in range(par_lo, par_hi + 1):
                values.append(_luminance(pixels[y * width + x]))
    else:
        for y in range(perp_lo, perp_hi + 1):
            for x in range(par_lo, par_hi + 1):
                values.append(_luminance(pixels[y * width + x]))
    if not values:
        return None
    return _median(values)


def _profile(pixels, width, height, axis, perp_lo, perp_hi, par_lo, par_hi):
    """Mean luminance along the perpendicular axis over the parallel span."""
    count = par_hi - par_lo + 1
    out = {}
    if axis == "x":
        for x in range(perp_lo, perp_hi + 1):
            total = 0.0
            for y in range(par_lo, par_hi + 1):
                total += _luminance(pixels[y * width + x])
            out[x] = total / count
    else:
        for y in range(perp_lo, perp_hi + 1):
            total = 0.0
            for x in range(par_lo, par_hi + 1):
                total += _luminance(pixels[y * width + x])
            out[y] = total / count
    return out


def _backdrop_levels(pixels, width, height, mapping, data):
    """Median luminance of each backdrop's clean top strip."""
    levels = {}
    for backdrop in data["backdrops"]:
        x0 = backdrop["x"]
        y0 = backdrop["y"]
        y_lo = y0 + BACKDROP_STRIP_TOP
        y_hi = min(y0 + BACKDROP_STRIP_TOP + BACKDROP_STRIP_HEIGHT - 1,
                   y0 + backdrop["h"] - 1)
        values = []
        for ds_y in range(y_lo, y_hi + 1):
            for ds_x in range(x0, x0 + backdrop["w"]):
                inside_quad = any(
                    quad["x"] <= ds_x < quad["x"] + quad["w"]
                    and quad["y"] <= ds_y < quad["y"] + quad["h"]
                    for quad in data["quads"])
                if inside_quad:
                    continue
                cx = int(round(mapping["offset_x"]
                               + (ds_x + 0.5) * mapping["scale_x"] - 0.5))
                cy = int(round(mapping["offset_y"]
                               + (ds_y + 0.5) * mapping["scale_y"] - 0.5))
                if 0 <= cx < width and 0 <= cy < height:
                    values.append(_luminance(pixels[cy * width + cx]))
        if not values:
            raise ValueError("backdrop %r has no measurable pixels"
                             % backdrop["name"])
        levels[backdrop["name"]] = _median(values)
    return levels


def _edge_nominal(rect, side):
    if side == "left":
        return float(rect["x"])
    if side == "right":
        return float(rect["x"] + rect["w"])
    if side == "top":
        return float(rect["y"])
    return float(rect["y"] + rect["h"])


def _on_screen_boundary(axis, nominal):
    if axis == "x":
        return nominal <= 0 or nominal >= NATIVE_WIDTH
    return nominal <= 0 or nominal >= NATIVE_HEIGHT


def _inset_span(start, end):
    return (start + CORNER_INSET, end - CORNER_INSET)


def _interior_band(nominal, side):
    """DS-pixel band 2..6 px inside the rectangle along the perpendicular."""
    if side in ("left", "top"):
        return (nominal + INTERIOR_INSET,
                nominal + INTERIOR_INSET + INTERIOR_WIDTH)
    return (nominal - INTERIOR_INSET - INTERIOR_WIDTH,
            nominal - INTERIOR_INSET)


def _quad_interior_level(pixels, width, height, mapping, rect, side):
    axis = AXIS_OF[side]
    if axis == "x":
        perp = _capture_span(*_interior_band(_edge_nominal(rect, side), side),
                             offset=mapping["offset_x"],
                             scale=mapping["scale_x"], limit=width)
        parallel = _capture_span(*_inset_span(rect["y"], rect["y"] + rect["h"]),
                                 offset=mapping["offset_y"],
                                 scale=mapping["scale_y"], limit=height)
    else:
        perp = _capture_span(*_interior_band(_edge_nominal(rect, side), side),
                             offset=mapping["offset_y"],
                             scale=mapping["scale_y"], limit=height)
        parallel = _capture_span(*_inset_span(rect["x"], rect["x"] + rect["w"]),
                                 offset=mapping["offset_x"],
                                 scale=mapping["scale_x"], limit=width)
    return _region_median(pixels, width, height, axis, perp, parallel)


def _backdrop_strip_span(backdrop):
    return (backdrop["y"] + BACKDROP_STRIP_TOP,
            backdrop["y"] + BACKDROP_STRIP_TOP + BACKDROP_STRIP_HEIGHT - 1)


def _adjacent_backdrop_level(backdrop, side, levels):
    """Level of the backdrop on the far side of an internal backdrop edge."""
    if side == "right":
        edge_x = backdrop["x"] + backdrop["w"]
        for other in backdrop.get("_all", ()):
            if other is not backdrop and other["x"] == edge_x:
                return levels[other["name"]]
    if side == "bottom":
        edge_y = backdrop["y"] + backdrop["h"]
        for other in backdrop.get("_all", ()):
            if other is not backdrop and other["y"] == edge_y:
                return levels[other["name"]]
    return None


def _measure_edge(pixels, width, height, mapping, nominal, axis, parallel,
                  left_level, right_level):
    """Return (capture_coordinate, None) or (None, skip_reason)."""
    offset = mapping["offset_x"] if axis == "x" else mapping["offset_y"]
    scale = mapping["scale_x"] if axis == "x" else mapping["scale_y"]
    perp_limit = width if axis == "x" else height
    predicted = offset + scale * nominal
    if predicted <= 0.0 or predicted >= float(perp_limit):
        return None, "edge lies on the capture crop boundary"
    contrast = abs(right_level - left_level)
    if contrast < MIN_CONTRAST:
        return None, "plateau contrast %.2f below %.2f" % (contrast,
                                                          MIN_CONTRAST)

    perp_lo = max(0, int(math.floor(predicted - BAND)))
    perp_hi = min(perp_limit - 1, int(math.ceil(predicted + BAND)))
    if axis == "x":
        perp = _capture_span(parallel[0], parallel[1], mapping["offset_y"],
                             mapping["scale_y"], height)
    else:
        perp = _capture_span(parallel[0], parallel[1], mapping["offset_x"],
                             mapping["scale_x"], width)
    if perp[0] > perp[1]:
        return None, "parallel span falls outside the capture"
    profile = _profile(pixels, width, height, axis, perp_lo, perp_hi,
                       perp[0], perp[1])
    if len(profile) < 2:
        return None, "too few capture pixels"

    mid = 0.5 * (left_level + right_level)
    rising = right_level > left_level
    candidates = []
    for i in range(perp_lo, perp_hi):
        a = profile[i]
        b = profile[i + 1]
        if (a - mid) * (b - mid) >= 0.0:
            continue
        if (b > a) != rising:
            continue
        t = (mid - a) / (b - a)
        candidates.append((i + 0.5) + t)
    if not candidates:
        return None, "no 50%% crossing in the expected direction"
    if len(candidates) > 1:
        return None, ("multiple 50%% crossings in the search band (%d)"
                      % len(candidates))
    x = candidates[0]

    tolerance = max(MIN_PLATEAU_TOL, PLATEAU_TOL_FRACTION * contrast)
    left_ok = any(abs(profile[i] - left_level) <= tolerance
                  for i in profile if i + 0.5 < x)
    right_ok = any(abs(profile[i] - right_level) <= tolerance
                   for i in profile if i + 0.5 > x)
    if not left_ok or not right_ok:
        return None, ("plateau level not observed on its side "
                      "(left %s, right %s)" % (left_ok, right_ok))
    return x, None


def measure_image(pixels, width, height, mapping, data):
    """Measure every rectangle edge.

    Returns (observations, backdrop_levels) where each observation is a dict
    with keys rect, name, side, axis, nominal, estimate (DS px) or None and
    reason (skip reason or None).
    """
    levels = _backdrop_levels(pixels, width, height, mapping, data)
    backdrops = []
    for backdrop in data["backdrops"]:
        entry = dict(backdrop)
        backdrops.append(entry)
    for backdrop in backdrops:
        backdrop["_all"] = backdrops

    observations = []
    for rect in data["backdrops"]:
        own = next(b for b in backdrops if b["name"] == rect["name"])
        for side in EDGES:
            nominal = _edge_nominal(rect, side)
            axis = AXIS_OF[side]
            obs = {"rect": rect["name"], "kind": "backdrop", "side": side,
                   "axis": axis, "nominal": nominal, "estimate": None,
                   "reason": None}
            if _on_screen_boundary(axis, nominal):
                obs["reason"] = "on the DS screen boundary (capture crop)"
            elif side in ("left", "top"):
                obs["reason"] = ("backdrop left/top edge is the screen "
                                 "boundary or the duplicate side of an "
                                 "internal boundary")
            else:
                other = _adjacent_backdrop_level(own, side, levels)
                if other is None:
                    obs["reason"] = "no adjacent backdrop on this side"
                else:
                    if side in ("left", "top"):
                        left_level, right_level = other, levels[rect["name"]]
                    else:
                        left_level, right_level = levels[rect["name"]], other
                    estimate, reason = _measure_edge(
                        pixels, width, height, mapping, nominal, axis,
                        _backdrop_strip_span(rect), left_level, right_level)
                    if estimate is None:
                        obs["reason"] = reason
                    else:
                        offset = (mapping["offset_x"] if axis == "x"
                                  else mapping["offset_y"])
                        scale = (mapping["scale_x"] if axis == "x"
                                 else mapping["scale_y"])
                        obs["estimate"] = (estimate - offset) / scale
            observations.append(obs)

    for quad in data["quads"]:
        background = levels[quad["backdrop"]]
        for side in EDGES:
            nominal = _edge_nominal(quad, side)
            axis = AXIS_OF[side]
            obs = {"rect": quad["name"], "kind": "quad", "side": side,
                   "axis": axis, "nominal": nominal, "estimate": None,
                   "reason": None}
            if _on_screen_boundary(axis, nominal):
                obs["reason"] = "on the DS screen boundary (capture crop)"
                observations.append(obs)
                continue
            interior = _quad_interior_level(pixels, width, height, mapping,
                                            quad, side)
            if interior is None:
                obs["reason"] = "interior band falls outside the capture"
                observations.append(obs)
                continue
            if side in ("left", "top"):
                left_level, right_level = background, interior
            else:
                left_level, right_level = interior, background
            if axis == "x":
                parallel = _inset_span(quad["y"], quad["y"] + quad["h"])
            else:
                parallel = _inset_span(quad["x"], quad["x"] + quad["w"])
            estimate, reason = _measure_edge(
                pixels, width, height, mapping, nominal, axis, parallel,
                left_level, right_level)
            if estimate is None:
                obs["reason"] = reason
            else:
                offset = (mapping["offset_x"] if axis == "x"
                          else mapping["offset_y"])
                scale = (mapping["scale_x"] if axis == "x"
                         else mapping["scale_y"])
                obs["estimate"] = (estimate - offset) / scale
            observations.append(obs)
    return observations, levels


def _mean_std(values):
    """Mean and population standard deviation (divide by n)."""
    count = len(values)
    if count == 0:
        return None, None, 0
    mean = sum(values) / count
    variance = sum((v - mean) ** 2 for v in values) / count
    return mean, math.sqrt(variance), count


def _by_rect(observations):
    grouped = {}
    for obs in observations:
        grouped.setdefault((obs["kind"], obs["rect"]), {})[obs["side"]] = obs
    return grouped


def _difference_values(observations, sides):
    """hw-desmume per class over samples where both columns are present."""
    values = []
    for obs in observations:
        if obs["kind"] != "quad" or obs["side"] not in sides:
            continue
        hw = obs["estimates"].get("hw")
        dsm = obs["estimates"].get("desmume")
        if hw is not None and dsm is not None:
            values.append(hw - dsm)
    return values


def _size_error_values(observations, axis_label, column):
    values = []
    names = []
    for obs in observations:
        if obs["kind"] == "quad" and obs["rect"] not in names:
            names.append(obs["rect"])
    low_side, high_side = (("left", "right") if axis_label == "width"
                           else ("top", "bottom"))
    grouped = _by_rect(observations)
    for name in names:
        sides = grouped.get(("quad", name))
        if not sides:
            continue
        low, high = sides.get(low_side), sides.get(high_side)
        if low is None or high is None:
            continue
        if (low["estimates"].get(column) is None
                or high["estimates"].get(column) is None):
            continue
        measured = high["estimates"][column] - low["estimates"][column]
        values.append(measured - (high["nominal"] - low["nominal"]))
    return values


def _size_difference_values(observations, axis_label):
    values = []
    names = []
    for obs in observations:
        if obs["kind"] == "quad" and obs["rect"] not in names:
            names.append(obs["rect"])
    low_side, high_side = (("left", "right") if axis_label == "width"
                           else ("top", "bottom"))
    grouped = _by_rect(observations)
    for name in names:
        sides = grouped.get(("quad", name))
        if not sides:
            continue
        low, high = sides.get(low_side), sides.get(high_side)
        if low is None or high is None:
            continue
        if any(low["estimates"].get(column) is None
               or high["estimates"].get(column) is None
               for column in ("hw", "desmume")):
            continue
        hw = high["estimates"]["hw"] - low["estimates"]["hw"]
        dsm = high["estimates"]["desmume"] - low["estimates"]["desmume"]
        values.append(hw - dsm)
    return values


def statistics(observations, which, include_size):
    lines = []
    for label, axis, side in STAT_CLASSES:
        for column in which:
            values = []
            for obs in observations:
                if obs["kind"] != "quad" or obs["side"] != side:
                    continue
                if obs["estimates"].get(column) is None:
                    continue
                values.append(obs["estimates"][column] - obs["nominal"])
            mean, std, count = _mean_std(values)
            if mean is None:
                lines.append("stat %s %s-nominal n=0" % (label, column))
            else:
                lines.append("stat %s %s-nominal mean=%+.4f std=%.4f n=%d"
                             % (label, column, mean, std, count))
        if len(which) == 2:
            mean, std, count = _mean_std(
                _difference_values(observations, (side,)))
            if mean is not None:
                lines.append("stat %s hw-desmume mean=%+.4f std=%.4f n=%d"
                             % (label, mean, std, count))
    if include_size:
        for axis_label in ("width", "height"):
            for column in which:
                values = _size_error_values(observations, axis_label, column)
                mean, std, count = _mean_std(values)
                if mean is None:
                    lines.append("stat %s %s-nominal n=0"
                                 % (axis_label, column))
                else:
                    lines.append("stat %s %s-nominal mean=%+.4f std=%.4f n=%d"
                                 % (axis_label, column, mean, std, count))
            if len(which) == 2:
                mean, std, count = _mean_std(
                    _size_difference_values(observations, axis_label))
                if mean is not None:
                    lines.append("stat %s hw-desmume mean=%+.4f std=%.4f n=%d"
                                 % (axis_label, mean, std, count))
    return lines


def compare(observations, which):
    """Render one line per edge with the requested estimate columns."""
    lines = []
    for obs in observations:
        if obs["estimate"] is None:
            lines.append("edge rect=%s kind=%s side=%s nominal=%.4f skipped=%s"
                         % (obs["rect"], obs["kind"], obs["side"],
                            obs["nominal"], obs["reason"]))
            continue
        fields = ["edge rect=%s kind=%s side=%s nominal=%.4f"
                  % (obs["rect"], obs["kind"], obs["side"], obs["nominal"])]
        for column in which:
            value = obs["estimates"][column]
            if value is None:
                fields.append("%s=skip" % column)
                continue
            fields.append("%s=%.4f" % (column, value))
            fields.append("%s-nominal=%+.4f" % (column, value - obs["nominal"]))
        if (len(which) == 2
                and all(obs["estimates"].get(column) is not None
                        for column in which)):
            fields.append("hw-desmume=%+.4f"
                          % (obs["estimates"]["hw"]
                             - obs["estimates"]["desmume"]))
        lines.append(" ".join(fields))
    return lines


def _merge(primary, secondary, column_primary, column_secondary):
    """Attach estimates from a second image to the primary observations."""
    merged = []
    index = {(o["kind"], o["rect"], o["side"]): o for o in secondary}
    for obs in primary:
        estimates = {column_primary: obs["estimate"]}
        other = index.get((obs["kind"], obs["rect"], obs["side"]))
        estimates[column_secondary] = (other["estimate"]
                                       if other is not None else None)
        entry = dict(obs)
        entry["estimates"] = estimates
        merged.append(entry)
    return merged


def _mapping_line(mapping):
    return ("mapping scale_x=%g scale_y=%g offset_x=%g offset_y=%g"
            % (mapping["scale_x"], mapping["scale_y"],
               mapping["offset_x"], mapping["offset_y"]))


def main(argv):
    mapping_path = MAPPING_JSON
    native = False
    desmume_path = None
    positional = []
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--native":
            native = True
        elif arg == "--mapping":
            if not args:
                sys.stderr.write("measure_edges: --mapping needs a path\n")
                return 2
            mapping_path = args.pop(0)
        elif arg == "--desmume":
            if not args:
                sys.stderr.write("measure_edges: --desmume needs a path\n")
                return 2
            desmume_path = args.pop(0)
        elif arg.startswith("--"):
            sys.stderr.write("measure_edges: unknown option %s\n" % arg)
            return 2
        else:
            positional.append(arg)
    if len(positional) != 1:
        sys.stderr.write(
            "usage: measure_edges.py [--mapping M.json] [--native] "
            "[--desmume FRAME.ppm] <image.png|image.ppm>\n")
        return 2
    if native and desmume_path:
        sys.stderr.write("measure_edges: --native and --desmume are exclusive\n")
        return 2

    try:
        data = load_json(QUADS_JSON)
        if native:
            mapping = identity_mapping()
        else:
            mapping = load_json(mapping_path)
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_edges: cannot read json: %s\n" % exc)
        return 2
    try:
        pixels, width, height = read_image(positional[0])
        if desmume_path:
            dsm_pixels, dsm_width, dsm_height = read_image(desmume_path)
    except ValueError as exc:
        sys.stderr.write("measure_edges: %s\n" % exc)
        return 2

    try:
        observations, levels = measure_image(pixels, width, height, mapping,
                                             data)
        if desmume_path:
            dsm_obs, _ = measure_image(dsm_pixels, dsm_width, dsm_height,
                                       identity_mapping(), data)
    except ValueError as exc:
        sys.stderr.write("measure_edges: %s\n" % exc)
        return 2

    if desmume_path:
        mode = "compare"
        merged = _merge(observations, dsm_obs, "hw", "desmume")
    elif native:
        mode = "native"
        merged = [dict(o, estimates={"desmume": o["estimate"]})
                  for o in observations]
    else:
        mode = "capture"
        merged = [dict(o, estimates={"hw": o["estimate"]})
                  for o in observations]

    measured = sum(1 for o in merged
                   if any(v is not None for v in o["estimates"].values()))
    skipped = len(merged) - measured
    print("mode=%s" % mode)
    print(_mapping_line(identity_mapping() if native else mapping))
    for backdrop in data["backdrops"]:
        print("backdrop %s level=%.4f"
              % (backdrop["name"], levels[backdrop["name"]]))
    print("edges_measured=%d edges_skipped=%d" % (measured, skipped))
    if mode == "compare":
        columns = ["hw", "desmume"]
    elif mode == "native":
        columns = ["desmume"]
    else:
        columns = ["hw"]
    for line in compare(merged, columns):
        print(line)
    for line in statistics(merged, columns, include_size=True):
        print(line)
    if measured == 0:
        sys.stderr.write("measure_edges: no measurable edges\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
