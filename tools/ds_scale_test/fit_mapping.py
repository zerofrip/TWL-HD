#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Sub-pixel least-squares fit of the DS-pixel -> capture mapping.

Reads a cc3dsfs top-screen capture (the 400x240 crop the EQ-4 work uses) and
the ds-scale-test `pattern.json`, measures straight pattern edges to sub-pixel
precision and fits, per axis,

    capture_edge = a * ds_edge + b

with `a` in capture px per DS px and `b` in capture px.

Method (stated here and in docs/reports/eq4g-capture-offset.md):

* Coordinates are pixel-corner coordinates on both sides: the boundary between
  DS pixels d-1 and d is at DS coordinate d, and the boundary between capture
  pixels p-1 and p is at capture coordinate p.  A capture pixel p has its
  centre at p+0.5.
* For every candidate straight edge of the pattern (both vertical edges for
  the x fit, both horizontal edges for the y fit) a perpendicular profile is
  built from the mean R,G,B of each capture pixel averaged over the edge's
  parallel span.  The two plateau levels are the pattern's own colours of the
  DS pixels on either side (read back from `pattern.json`; the edge is only
  used when those two colours are constant along the whole span, and DS
  coordinates outside the 256x192 pattern count as the pattern background).
  Edges whose two levels differ by less than MIN_CONTRAST are skipped.
* The edge position is the capture coordinate where the profile crosses the
  50 % level (high+low)/2 in the expected direction, found by linear
  interpolation between the two capture pixels that straddle the crossing.
  The crossing must be unique in the search window and each plateau level must
  actually be observed on its own side (within PLATEAU_TOL_FRACTION of the
  level difference, floored at 6 luminance levels).  The 1-px striping probes
  therefore show several crossings per window and are rejected; the 1-px
  frame, cross and corner edges stay usable because their expected level is
  still sampled.  Every
  rejected edge is listed on stderr with its reason.
* Least squares per axis over all observations:
  a = sum((d-dbar)(c-cbar)) / sum((d-dbar)^2), b = cbar - a*dbar; the RMS of
  the residuals is printed, with the standard errors of a and b derived from
  it (se_a = rms/sqrt(sxx), se_b = rms*sqrt(1/n + dbar^2/sxx)).

The output is deterministic: observations are sorted by (axis, ds, name).

    python3 tools/ds_scale_test/fit_mapping.py <capture.png> [--pattern P]

Exit 0 on success, 2 on usage/PIL/input errors with a one-line message.
Requires PIL for the capture PNG; everything else is standard library.
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
PATTERN_JSON = os.path.join(HERE, "pattern.json")
MAPPING_JSON = os.path.join(HERE, os.pardir, "ds_vtxcolor_test",
                            "capture_mapping.json")

# Search half-width around the nominal edge position, in capture px.  It must
# cover the offset being investigated (about 1-2 DS px = 1.25-2.5 capture px)
# without pulling in the next pattern feature.
BAND = 5.0
# Minimum plateau contrast (mean R,G,B) for an edge to carry sub-pixel
# information.  The gradient steps differ by 16, so this must stay below 16.
MIN_CONTRAST = 6.0
# Minimum jump between the two straddling pixels for a crossing to count.
MIN_STEP = 3.0
# Maximum distance of a straddling pixel from the plateau colour it should
# represent.  Catches resampled 1-px lines (their neighbours are blends).
PLATEAU_TOL_FRACTION = 0.12

AXES = ("x", "y")


def load_json(path, what):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read %s: %s" % (what, exc))


def _elem(pattern, name):
    for elem in pattern["elements"]:
        if elem["name"] == name:
            return elem
    raise KeyError("pattern.json has no element named %r" % name)


def _luminance(rgb):
    return (rgb[0] + rgb[1] + rgb[2]) / 3.0


def _render(pattern):
    """Render the pattern to 256x192 RGB pixel tuples (same rules as ROM)."""
    import gen_pattern
    raw = gen_pattern.render_rgb(pattern)
    width = pattern["width"]
    return [(raw[i], raw[i + 1], raw[i + 2])
            for i in range(0, len(raw), 3)], width


def _capture_span(ds_start, ds_end, offset, scale, limit):
    """Capture indices whose centre lies in [offset+scale*ds_start, ...)."""
    lo = offset + scale * ds_start
    hi = offset + scale * ds_end
    first = int(math.ceil(lo - 0.5))
    last = int(math.ceil(hi - 0.5)) - 1
    return max(0, first), min(limit - 1, last)


def _span_edge_list(pattern):
    """Yield (axis, name, ds, other_start, other_end) for straight edges."""
    edges = []
    width = pattern["width"]
    height = pattern["height"]

    def add(axis, name, ds, start, end):
        edges.append((axis, name, float(ds), float(start), float(end)))

    # 1-px frame: outer left/right (measurable against the pillar bars) and the
    # inner boundary of the 1-px border.  The outer top/bottom coincide with
    # the 400x240 crop edge and are rejected later (no sample on the far side).
    add("x", "frame_left_outer", 0, 55, 85)
    add("x", "frame_left_inner", 1, 55, 85)
    add("x", "frame_right_inner", width - 1, 55, 85)
    add("x", "frame_right_outer", width, 55, 85)
    add("y", "frame_top_outer", 0, 55, 85)
    add("y", "frame_top_inner", 1, 55, 85)
    add("y", "frame_bottom_inner", height - 1, 55, 85)
    add("y", "frame_bottom_outer", height, 55, 85)

    # 1-px centre cross: both sides of each line.
    add("x", "cross_v_left", 128, 55, 85)
    add("x", "cross_v_right", 129, 55, 85)
    add("y", "cross_h_top", 96, 55, 85)
    add("y", "cross_h_bottom", 97, 55, 85)

    # 1-px alternating strip probes: every column/row boundary.  Most are
    # rejected by the plateau test; each rejection is listed.
    for d in range(1, width):
        add("x", "hstrip_%d" % d, d, 41, 47)
    for d in range(1, height):
        add("y", "vstrip_%d" % d, d, 41, 47)

    # 16-step gradient: the 15 boundaries between neighbouring steps.
    grad = _elem(pattern, "gradient")
    step = grad["step_px"]
    for k in range(1, grad["w"] // step):
        add("x", "gradient_%d" % (k * step), k * step, 142, 153)

    # Corner rects: both vertical and both horizontal sides of each square.
    for name in ("corner_tl", "corner_tr", "corner_bl", "corner_br"):
        elem = _elem(pattern, name)
        x0, y0, w, h = elem["x"], elem["y"], elem["w"], elem["h"]
        x1, y1 = x0 + w, y0 + h
        span_y = (y0 + 1, y1 - 1)
        span_x = (x0 + 1, x1 - 1)
        add("x", name + "_left", x0, span_y[0], span_y[1])
        add("x", name + "_right", x1, span_y[0], span_y[1])
        add("y", name + "_top", y0, span_x[0], span_x[1])
        add("y", name + "_bottom", y1, span_x[0], span_x[1])
    return edges


def _expected_levels(render, width, height, axis, ds, span, background):
    """Luminance of the two DS pixels flanking the edge, or None if not flat.

    DS coordinates outside the 256x192 pattern are the pattern background (the
    pillar bars of the 400x240 capture), which makes the outer frame edges
    usable.
    """
    first, last = span
    left = set()
    right = set()

    def sample(x, y):
        if 0 <= x < width and 0 <= y < height:
            return render[y * width + x]
        return background

    if axis == "x":
        xl = int(ds) - 1
        xr = int(ds)
        for y in range(int(math.ceil(first)), int(math.floor(last))):
            left.add(sample(xl, y))
            right.add(sample(xr, y))
    else:
        yt = int(ds) - 1
        yb = int(ds)
        for x in range(int(math.ceil(first)), int(math.floor(last))):
            left.add(sample(x, yt))
            right.add(sample(x, yb))
    if len(left) != 1 or len(right) != 1:
        return None
    return _luminance(next(iter(left))), _luminance(next(iter(right)))


def _profile(pixels, width, height, axis, perp_lo, perp_hi, par_lo, par_hi):
    """Mean luminance along the perpendicular axis over the parallel span."""
    out = {}
    if par_lo > par_hi:
        return out
    count = par_hi - par_lo + 1
    if axis == "x":
        for x in range(perp_lo, perp_hi + 1):
            total = 0.0
            for y in range(par_lo, par_hi + 1):
                r, g, b = pixels[y * width + x]
                total += (r + g + b) / 3.0
            out[x] = total / count
    else:
        for y in range(perp_lo, perp_hi + 1):
            total = 0.0
            for x in range(par_lo, par_hi + 1):
                r, g, b = pixels[y * width + x]
                total += (r + g + b) / 3.0
            out[y] = total / count
    return out


def _edge_observation(pixels, width, height, render, native, mapping, edge,
                      background):
    """Return an observation dict, or (None, reason) when the edge is skipped."""
    axis, name, ds, span_start, span_end = edge
    if axis == "x":
        perp_limit = width
        offset = mapping["offset_x"]
        scale = mapping["scale_x"]
        par_lo, par_hi = _capture_span(span_start, span_end,
                                       mapping["offset_y"],
                                       mapping["scale_y"], height)
    else:
        perp_limit = height
        offset = mapping["offset_y"]
        scale = mapping["scale_y"]
        par_lo, par_hi = _capture_span(span_start, span_end,
                                       mapping["offset_x"],
                                       mapping["scale_x"], width)
    if par_lo > par_hi:
        return None, "parallel span falls outside the capture"

    levels = _expected_levels(render, native[0], native[1], axis, ds,
                              (span_start, span_end), background)
    if levels is None:
        return None, "pattern colour is not constant along the span"
    left_expected, right_expected = levels
    contrast = abs(left_expected - right_expected)
    if contrast < MIN_CONTRAST:
        return None, "plateau contrast %.1f below %.1f" % (contrast,
                                                           MIN_CONTRAST)

    predicted = offset + scale * ds
    if predicted <= 0.0 or predicted >= float(perp_limit):
        return None, "edge lies on the capture crop boundary"
    perp_lo = max(0, int(math.floor(predicted - BAND)))
    perp_hi = min(perp_limit - 1, int(math.ceil(predicted + BAND)))
    profile = _profile(pixels, width, height, axis, perp_lo, perp_hi,
                       par_lo, par_hi)
    if len(profile) < 2:
        return None, "too few capture pixels"

    mid = (left_expected + right_expected) / 2.0
    rising = right_expected > left_expected
    candidates = []
    for i in range(perp_lo, perp_hi):
        a = profile[i]
        b = profile[i + 1]
        if abs(a - b) < MIN_STEP:
            continue
        if (a - mid) * (b - mid) >= 0.0:
            continue
        if (b > a) != rising:
            continue
        t = (mid - a) / (b - a)
        x = (i + 0.5) + t
        candidates.append((abs(x - predicted), x))
    if not candidates:
        return None, "no 50% crossing in the expected direction"
    if len(candidates) > 1:
        return None, ("multiple 50%% crossings (periodic or 1-px feature): "
                      "%d candidates" % len(candidates))
    x = candidates[0][1]

    # The plateau levels must actually appear on their own side of the
    # crossing within the search window, or the crossing is noise.
    tolerance = max(6.0, PLATEAU_TOL_FRACTION * contrast)
    left_ok = any(abs(profile[i] - left_expected) <= tolerance
                  for i in profile if i + 0.5 < x)
    right_ok = any(abs(profile[i] - right_expected) <= tolerance
                   for i in profile if i + 0.5 > x)
    if not left_ok or not right_ok:
        return None, ("a plateau level is not observed on its side "
                      "(expected %.1f/%.1f)" % (left_expected, right_expected))
    return {
        "axis": axis,
        "name": name,
        "ds": ds,
        "capture": x,
    }, None


def _fit(observations):
    if not observations:
        return None
    n = float(len(observations))
    dbar = sum(o["ds"] for o in observations) / n
    cbar = sum(o["capture"] for o in observations) / n
    sxx = sum((o["ds"] - dbar) ** 2 for o in observations)
    sxy = sum((o["ds"] - dbar) * (o["capture"] - cbar)
              for o in observations)
    a = sxy / sxx if sxx else 0.0
    b = cbar - a * dbar
    for o in observations:
        o["residual"] = o["capture"] - (a * o["ds"] + b)
    rms = math.sqrt(sum(o["residual"] ** 2 for o in observations) / n)
    # Standard errors from the residuals; zero when the slope is undetermined.
    if sxx:
        se_a = rms / math.sqrt(sxx)
        se_b = rms * math.sqrt(1.0 / n + dbar * dbar / sxx)
    else:
        se_a = se_b = 0.0
    return {"a": a, "b": b, "rms": rms, "n": len(observations),
            "se_a": se_a, "se_b": se_b}


def fit_capture(pixels, width, height, pattern, mapping):
    """Measure every usable edge and fit both axes.

    `pixels` is a flat list of (r,g,b), row-major, `width` x `height`.
    Returns (observations, fits, skipped) where fits maps 'x'/'y' to the fit
    dict and skipped is a list of (axis, name, ds, reason).
    """
    render, native_w = _render(pattern)
    native = (native_w, pattern["height"])
    background = tuple(pattern["background"])
    observations = []
    skipped = []
    for edge in _span_edge_list(pattern):
        observation, reason = _edge_observation(
            pixels, width, height, render, native, mapping, edge, background)
        if observation is None:
            skipped.append((edge[0], edge[1], edge[2], reason))
        else:
            observations.append(observation)
    observations.sort(key=lambda o: (o["axis"], o["ds"], o["name"]))
    fits = {}
    for axis in AXES:
        subset = [o for o in observations if o["axis"] == axis]
        fits[axis] = _fit(subset)
    return observations, fits, skipped


def _delta_ds(a, b, nominal_scale, nominal_offset, pos):
    """Offset vs the nominal mapping at DS position `pos`, in DS px."""
    return ((a - nominal_scale) * pos + (b - nominal_offset)) / a


def format_report(observations, fits, mapping):
    lines = []
    for o in observations:
        lines.append("axis=%s edge=%s ds=%.4f capture=%.4f residual=%.4f"
                     % (o["axis"], o["name"], o["ds"], o["capture"],
                        o["residual"]))
    for axis in AXES:
        fit = fits[axis]
        if fit is None:
            lines.append("n_%s=0 %s_fit=unavailable" % (axis, axis))
            continue
        lines.append("n_%s=%d a_%s=%.6f b_%s=%.6f rms_%s=%.6f "
                     "se_a_%s=%.6f se_b_%s=%.6f"
                     % (axis, fit["n"], axis, fit["a"], axis, fit["b"],
                        axis, fit["rms"], axis, fit["se_a"], axis,
                        fit["se_b"]))
    fx = fits["x"]
    fy = fits["y"]
    if fx is not None and fy is not None:
        nominal_scale_x = mapping["scale_x"]
        nominal_scale_y = mapping["scale_y"]
        for label, px, py in (("centre", 128.0, 96.0),
                              ("corner_0_0", 0.0, 0.0),
                              ("corner_256_192", 256.0, 192.0)):
            dx = _delta_ds(fx["a"], fx["b"], nominal_scale_x,
                           mapping["offset_x"], px)
            dy = _delta_ds(fy["a"], fy["b"], nominal_scale_y,
                           mapping["offset_y"], py)
            lines.append("delta_ds_%s dx=%.4f dy=%.4f" % (label, dx, dy))
    lines.append("mapping_current scale_x=%g scale_y=%g offset_x=%g "
                 "offset_y=%g"
                 % (mapping["scale_x"], mapping["scale_y"],
                    mapping["offset_x"], mapping["offset_y"]))
    return lines


def read_capture(path):
    if Image is None:
        raise ValueError("PIL is not installed (pip install Pillow)")
    try:
        image = Image.open(path)
        image.load()
    except (OSError, ValueError) as exc:
        raise ValueError("cannot read %s: %s" % (path, exc))
    image = image.convert("RGB")
    raw = image.tobytes()
    pixels = [(raw[i], raw[i + 1], raw[i + 2])
              for i in range(0, len(raw), 3)]
    return pixels, image.width, image.height


def main(argv):
    pattern_path = PATTERN_JSON
    capture_path = None
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg == "--pattern":
            if not args:
                sys.stderr.write("usage: fit_mapping.py <capture.png> "
                                 "[--pattern pattern.json]\n")
                return 2
            pattern_path = args.pop(0)
        elif arg.startswith("--pattern="):
            pattern_path = arg.split("=", 1)[1]
        elif capture_path is None and not arg.startswith("-"):
            capture_path = arg
        else:
            sys.stderr.write("usage: fit_mapping.py <capture.png> "
                             "[--pattern pattern.json]\n")
            return 2
    if capture_path is None:
        sys.stderr.write("usage: fit_mapping.py <capture.png> "
                         "[--pattern pattern.json]\n")
        return 2
    try:
        pattern = load_json(pattern_path, "pattern.json")
        mapping = load_json(MAPPING_JSON, "capture mapping")
        pixels, width, height = read_capture(capture_path)
    except ValueError as exc:
        sys.stderr.write("fit_mapping: %s\n" % exc)
        return 2
    observations, fits, skipped = fit_capture(pixels, width, height,
                                               pattern, mapping)
    for axis, name, ds, reason in skipped:
        sys.stderr.write("fit_mapping: skip axis=%s edge=%s ds=%.0f: %s\n"
                         % (axis, name, ds, reason))
    for line in format_report(observations, fits, mapping):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
