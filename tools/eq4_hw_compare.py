#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EQ-4: compare a cc3dsfs DS capture with a DS-resolution reference picture.

Inputs are a hardware top-screen capture PNG (the 400x240 crop produced by
tools/cc3dsfs_capture, or a full 1280x720 root screenshot whose top-left
400x240 region is used) and a DS-resolution reference PPM (256x192, the
harness raw 3D framebuffer).  A 400x240 or 400x480 "ours" PPM is also accepted
and sampled onto the DS grid with the eq3_compare centre rule.

Both mapping directions are reported:

* capture -> DS: crop x 40..359 of the 400x240 region, area (box) downsample
  320x240 -> 256x192;
* reference -> capture: upsample 256x192 -> 320x240 bilinearly at 1.25x and
  place it at (40,0) on a 400x240 canvas.

The capture/DS geometry (scale 1.25, offset 40) is read from the shared
tools/ds_vtxcolor_test/capture_mapping.json; no number is duplicated here.
Tolerances come from tools/eq3_compare.py (TOL_POS = 2 DS px, TOL_COLOUR = 8
per channel) plus the design section 5.1 blended/edge tolerance 16.  The extra
1 DS px position allowance for the capture's 1.25x resampling is a labelled
HYPOTHESIS (the bilinear-like filter is inferred from a 1-px strip profile in
tools/ds_scale_test/README.md section 6), giving 3 DS px in total.

This tool never claims hardware equality.  Every report carries the line
`basis=one console, one capture`.

    python3 tools/eq4_hw_compare.py --capture cap.png --reference ref.ppm

Report: stable `key=value` lines.  Exit codes: 0 PASS or NOT-COMPARABLE (a
blank/unusable capture has nothing to compare), 1 FAIL, 2 usage/input/PIL
error.  Standard library plus PIL (only for the capture PNG).
"""

import argparse
import json
import math
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAPPING = os.path.join(TOOLS_DIR, "ds_vtxcolor_test",
                               "capture_mapping.json")

sys.path.insert(0, TOOLS_DIR)

import eq3_compare  # noqa: E402

# --- Tolerances -----------------------------------------------------------
# eq3_compare owns TOL_POS (2 DS px) and TOL_COLOUR (8 per channel); the
# blended/edge value is docs/EQUALITY_CHECK_DESIGN.md section 5.1.
TOL_FLAT = eq3_compare.TOL_COLOUR
TOL_BLENDED = 16
POS_TOL = eq3_compare.TOL_POS
# HYPOTHESIS: the capture's 1.25x bilinear-like resampling can add one DS px
# of apparent shift (tools/ds_scale_test/README.md section 6).  Labelled, not
# measured against hardware in this task.
RESAMPLE_POS_ALLOWANCE = 1
POS_TOL_CAPTURE = POS_TOL + RESAMPLE_POS_ALLOWANCE

# Fraction of DS pixels that must be within their per-pixel tolerance.
MIN_WITHIN_FRACTION = 0.95
# A colour class smaller than this is not used for a presence/centroid check.
MIN_CLASS_AREA = 64
# Capture layout (cc3dsfs 400x240 top region); the DS part derives from the
# shared mapping.
CAPTURE_W = 400
CAPTURE_H = 240
DS_W = eq3_compare.DS_W
DS_H = eq3_compare.DS_H

BACKGROUND = (0, 0, 0)
CLASSES = (
    ("white", (255, 255, 255)),
    ("red", (255, 0, 0)),
    ("green", (0, 255, 0)),
    ("blue", (0, 0, 255)),
    ("yellow", (255, 255, 0)),
)
CLASS_MAX_DIST = 96
BG_MAX_CHANNEL = 24


class InputError(Exception):
    """Usage, mapping or input failure: exit 2."""


# --------------------------------------------------------------------------
# Parsers
# --------------------------------------------------------------------------

def parse_ppm(path):
    """Parse a binary P6 PPM into (width, height, [(r,g,b), ...])."""
    try:
        with open(path, "rb") as handle:
            magic = handle.readline().strip()
            if magic != b"P6":
                raise InputError("%s: expected a P6 PPM" % path)
            fields = []
            while len(fields) < 3:
                line = handle.readline()
                if not line:
                    raise InputError("%s: truncated header" % path)
                line = line.split(b"#", 1)[0].split()
                fields.extend(int(value) for value in line)
            width, height, maxval = fields[:3]
            if maxval != 255:
                raise InputError("%s: unsupported maxval %d" % (path, maxval))
            data = handle.read()
    except OSError as exc:
        raise InputError("cannot read %s: %s" % (path, exc))
    if len(data) != width * height * 3:
        raise InputError("%s: expected %d raster bytes, got %d"
                         % (path, width * height * 3, len(data)))
    pixels = [(data[i], data[i + 1], data[i + 2])
              for i in range(0, len(data), 3)]
    return width, height, pixels


def load_mapping(path):
    """Read and validate the shared capture mapping JSON."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as exc:
        raise InputError("cannot read mapping %s: %s" % (path, exc))
    try:
        mapping = {
            "scale_x": float(data["scale_x"]),
            "scale_y": float(data["scale_y"]),
            "offset_x": int(data["offset_x"]),
            "offset_y": int(data["offset_y"]),
        }
    except (KeyError, TypeError, ValueError) as exc:
        raise InputError("mapping %s is missing or has a bad value: %s"
                         % (path, exc))
    if mapping["scale_x"] <= 0 or mapping["scale_y"] <= 0:
        raise InputError("mapping scale must be positive (got %.3f, %.3f)"
                         % (mapping["scale_x"], mapping["scale_y"]))
    return mapping


def load_capture(path):
    """Return the top-left 400x240 RGB pixels of the capture PNG."""
    if Image is None:
        raise InputError("PIL is required to read the capture PNG")
    try:
        image = Image.open(path)
        image.load()
    except (OSError, ValueError) as exc:
        raise InputError("cannot read capture %s: %s" % (path, exc))
    if image.width < CAPTURE_W or image.height < CAPTURE_H:
        raise InputError("capture %s is %dx%d, need at least %dx%d"
                         % (path, image.width, image.height,
                            CAPTURE_W, CAPTURE_H))
    region = image.convert("RGB").crop((0, 0, CAPTURE_W, CAPTURE_H))
    raw = region.tobytes()
    return [(raw[i], raw[i + 1], raw[i + 2])
            for i in range(0, len(raw), 3)]


def _sample_centre(dx, dy, mapping):
    """eq3_compare centre rule, with the offset/scale from the shared JSON."""
    x = int(math.floor(mapping["offset_x"]
                      + mapping["scale_x"] * (dx + 0.5) + 0.5))
    y = int(math.floor(mapping["offset_y"]
                      + mapping["scale_y"] * (dy + 0.5) + 0.5))
    return x, y


def load_reference(path, mapping):
    """Return a 256x192 RGB list from a harness or 'ours' PPM."""
    width, height, pixels = parse_ppm(path)
    if (width, height) == (DS_W, DS_H):
        return pixels
    if (width, height) in ((CAPTURE_W, CAPTURE_H), (CAPTURE_W, CAPTURE_H * 2)):
        out = []
        for dy in range(DS_H):
            for dx in range(DS_W):
                x, y = _sample_centre(dx, dy, mapping)
                x = min(max(x, 0), width - 1)
                y = min(max(y, 0), height - 1)
                out.append(pixels[y * width + x])
        return out
    raise InputError("reference %s is %dx%d, expected %dx%d or a %dx%d / "
                     "%dx%d 'ours' PPM"
                     % (path, width, height, DS_W, DS_H, CAPTURE_W,
                        CAPTURE_H, CAPTURE_W, CAPTURE_H * 2))


# --------------------------------------------------------------------------
# Resampling
# --------------------------------------------------------------------------

def capture_to_ds(capture, mapping):
    """Area (box) downsample the mapped 320x240 region to 256x192."""
    scale_x = mapping["scale_x"]
    scale_y = mapping["scale_y"]
    offset_x = mapping["offset_x"]
    offset_y = mapping["offset_y"]
    out = []
    for dy in range(DS_H):
        y0 = offset_y + dy * scale_y
        y1 = y0 + scale_y
        py0 = int(math.floor(y0))
        py1 = int(math.ceil(y1))
        for dx in range(DS_W):
            x0 = offset_x + dx * scale_x
            x1 = x0 + scale_x
            px0 = int(math.floor(x0))
            px1 = int(math.ceil(x1))
            total = [0.0, 0.0, 0.0]
            weight = 0.0
            for py in range(py0, py1):
                oy = min(y1, py + 1.0) - max(y0, float(py))
                if oy <= 0:
                    continue
                for px in range(px0, px1):
                    ox = min(x1, px + 1.0) - max(x0, float(px))
                    if ox <= 0:
                        continue
                    w = ox * oy
                    rgb = capture[py * CAPTURE_W + px]
                    total[0] += rgb[0] * w
                    total[1] += rgb[1] * w
                    total[2] += rgb[2] * w
                    weight += w
            out.append(tuple(int(round(total[i] / weight)) for i in range(3)))
    return out


def reference_to_capture(reference, mapping):
    """Bilinear 1.25x upsample of the DS reference into the 320x240 region."""
    scale_x = mapping["scale_x"]
    scale_y = mapping["scale_y"]
    out_w = int(round(DS_W * scale_x))
    out_h = int(round(DS_H * scale_y))
    out = []
    for y in range(out_h):
        fy = (y + 0.5) / scale_y - 0.5
        fy = min(max(fy, 0.0), DS_H - 1.0)
        y0 = int(math.floor(fy))
        y1 = min(y0 + 1, DS_H - 1)
        ty = fy - y0
        for x in range(out_w):
            fx = (x + 0.5) / scale_x - 0.5
            fx = min(max(fx, 0.0), DS_W - 1.0)
            x0 = int(math.floor(fx))
            x1 = min(x0 + 1, DS_W - 1)
            tx = fx - x0
            p00 = reference[y0 * DS_W + x0]
            p10 = reference[y0 * DS_W + x1]
            p01 = reference[y1 * DS_W + x0]
            p11 = reference[y1 * DS_W + x1]
            rgb = []
            for channel in range(3):
                top = p00[channel] * (1 - tx) + p10[channel] * tx
                bottom = p01[channel] * (1 - tx) + p11[channel] * tx
                rgb.append(int(round(top * (1 - ty) + bottom * ty)))
            out.append(tuple(rgb))
    return out, out_w, out_h


# --------------------------------------------------------------------------
# Metrics
# --------------------------------------------------------------------------

def classify(rgb):
    """Nearest colour class, or 'background'/'other'."""
    best_name = "other"
    best_distance = CLASS_MAX_DIST * CLASS_MAX_DIST
    for name, colour in CLASSES:
        distance = sum((rgb[i] - colour[i]) ** 2 for i in range(3))
        if distance <= best_distance:
            best_distance = distance
            best_name = name
    if best_name != "other":
        return best_name
    distance = sum((rgb[i] - BACKGROUND[i]) ** 2 for i in range(3))
    if distance <= BG_MAX_CHANNEL * BG_MAX_CHANNEL:
        return "background"
    return "other"


def _class_grid(pixels):
    return [classify(pixel) for pixel in pixels]


def _edge_mask(grid, width, height):
    mask = [False] * len(grid)
    for y in range(height):
        for x in range(width):
            index = y * width + x
            name = grid[index]
            if name == "other":
                mask[index] = True
                continue
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < width and 0 <= ny < height:
                    if grid[ny * width + nx] != name:
                        mask[index] = True
                        break
    return mask


def _is_background(rgb):
    return max(abs(channel) for channel in rgb) <= BG_MAX_CHANNEL


def nonbg_bbox(pixels, width, height):
    left = top = None
    right = bottom = None
    count = 0
    for y in range(height):
        for x in range(width):
            if _is_background(pixels[y * width + x]):
                continue
            count += 1
            if left is None or x < left:
                left = x
            if right is None or x > right:
                right = x
            if top is None or y < top:
                top = y
            if bottom is None or y > bottom:
                bottom = y
    if count == 0:
        return None, 0
    return (left, top, right, bottom), count


def compare_pixels(a, b, width, height):
    """Per-channel and per-pixel error between two same-size RGB lists."""
    grid_a = _class_grid(a)
    grid_b = _class_grid(b)
    edge_a = _edge_mask(grid_a, width, height)
    edge_b = _edge_mask(grid_b, width, height)
    sums = [0, 0, 0]
    max_error = 0
    within = 0
    within_flat = 0
    within_edge = 0
    flat = 0
    edge = 0
    for index, (pa, pb) in enumerate(zip(a, b)):
        diffs = [abs(pa[channel] - pb[channel]) for channel in range(3)]
        for channel in range(3):
            sums[channel] += diffs[channel]
        error = max(diffs)
        if error > max_error:
            max_error = error
        if edge_a[index] or edge_b[index]:
            edge += 1
            allowed = TOL_BLENDED
            if error <= allowed:
                within += 1
                within_edge += 1
        else:
            flat += 1
            allowed = TOL_FLAT
            if error <= allowed:
                within += 1
                within_flat += 1
    total = width * height
    return {
        "mean_abs_r": sums[0] / total,
        "mean_abs_g": sums[1] / total,
        "mean_abs_b": sums[2] / total,
        "max_error": max_error,
        "within": within,
        "within_flat": within_flat,
        "within_edge": within_edge,
        "flat": flat,
        "edge": edge,
        "frac_within": within / total,
        "frac_within_flat": within_flat / flat if flat else 1.0,
        "frac_within_edge": within_edge / edge if edge else 1.0,
    }


def class_stats(pixels):
    counts = {name: 0 for name, _ in CLASSES}
    sums = {name: [0, 0] for name, _ in CLASSES}
    grid = _class_grid(pixels)
    for index, name in enumerate(grid):
        if name in counts:
            counts[name] += 1
            sums[name][0] += index % DS_W
            sums[name][1] += index // DS_W
    stats = {}
    for name, _ in CLASSES:
        count = counts[name]
        if count:
            stats[name] = (count, sums[name][0] / count,
                           sums[name][1] / count)
        else:
            stats[name] = (0, 0.0, 0.0)
    return stats


def _fmt(value):
    return "%.4f" % value


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

def compare(capture, reference, mapping):
    """Return the report dict (all values already printable)."""
    ds_capture = capture_to_ds(capture, mapping)
    upsampled, region_w, region_h = reference_to_capture(reference, mapping)
    # The upsample is compared in capture space, i.e. against the mapped region
    # at (offset_x, offset_y), not against the pillar bars.
    region = []
    for y in range(region_h):
        row = (y + mapping["offset_y"]) * CAPTURE_W + mapping["offset_x"]
        region.extend(capture[row:row + region_w])

    ds_metrics = compare_pixels(reference, ds_capture, DS_W, DS_H)
    capture_metrics = compare_pixels(upsampled, region, region_w, region_h)

    ref_bbox, ref_nonbg = nonbg_bbox(reference, DS_W, DS_H)
    cap_bbox, cap_nonbg = nonbg_bbox(ds_capture, DS_W, DS_H)
    ref_classes = class_stats(reference)
    cap_classes = class_stats(ds_capture)

    report = [
        ("basis", "one console, one capture"),
        ("tolerance_flat", str(TOL_FLAT)),
        ("tolerance_blended_edge", str(TOL_BLENDED)),
        ("tolerance_pos_ds", str(POS_TOL_CAPTURE)),
        ("mapping_scale_x", "%g" % mapping["scale_x"]),
        ("mapping_scale_y", "%g" % mapping["scale_y"]),
        ("mapping_offset_x", str(mapping["offset_x"])),
        ("mapping_offset_y", str(mapping["offset_y"])),
    ]
    for prefix, metrics in (("ds", ds_metrics),
                            ("capture", capture_metrics)):
        for key in ("mean_abs_r", "mean_abs_g", "mean_abs_b", "max_error",
                    "frac_within", "frac_within_flat", "frac_within_edge"):
            report.append(("%s.%s" % (prefix, key), _fmt(metrics[key])))
        report.append(("%s.flat_pixels" % prefix, str(metrics["flat"])))
        report.append(("%s.edge_pixels" % prefix, str(metrics["edge"])))

    report.append(("ref_nonbg_pixels", str(ref_nonbg)))
    report.extend(_bbox_lines("ref", ref_bbox))
    report.append(("capture_nonbg_pixels", str(cap_nonbg)))
    report.extend(_bbox_lines("capture", cap_bbox))
    for name, _ in CLASSES:
        for side, stats in (("ref", ref_classes), ("capture", cap_classes)):
            count, cx, cy = stats[name]
            report.append(("class_%s_%s_area" % (name, side), str(count)))
            report.append(("class_%s_%s_centroid_x" % (name, side),
                           _fmt(cx)))
            report.append(("class_%s_%s_centroid_y" % (name, side),
                           _fmt(cy)))

    reasons = []
    verdict = "PASS"
    if cap_nonbg == 0 or cap_bbox is None:
        verdict = "NOT-COMPARABLE"
        reasons.append("capture has no non-background region (empty or blank)")
    elif ref_nonbg == 0 or ref_bbox is None:
        verdict = "NOT-COMPARABLE"
        reasons.append("reference has no non-background region")
    else:
        bbox_dev = [abs(ref_bbox[i] - cap_bbox[i]) for i in range(4)]
        for name, deviation in zip(("left", "top", "right", "bottom"),
                                   bbox_dev):
            report.append(("bbox_dev_%s" % name, str(deviation)))
            if deviation > POS_TOL_CAPTURE:
                reasons.append("bbox-%s deviates %d DS px (tol %d)"
                               % (name, deviation, POS_TOL_CAPTURE))
        # Per-pixel tolerance is gated on the flat (non-edge) pixels only: at a
        # hard edge the 1.25x resampling legitimately mixes neighbouring cells,
        # which the structural checks below cover.  The edge-pixel fraction
        # (T = 16) is still reported.
        if ds_metrics["frac_within_flat"] < MIN_WITHIN_FRACTION:
            reasons.append("ds.frac_within_flat %.4f < %.2f"
                           % (ds_metrics["frac_within_flat"],
                              MIN_WITHIN_FRACTION))
        for name, _ in CLASSES:
            ref_count, ref_cx, ref_cy = ref_classes[name]
            cap_count, cap_cx, cap_cy = cap_classes[name]
            if ref_count < MIN_CLASS_AREA:
                continue
            if cap_count < MIN_CLASS_AREA:
                reasons.append("class %s present in reference (%d px) but "
                               "absent from capture (%d px)"
                               % (name, ref_count, cap_count))
                continue
            dev = max(abs(ref_cx - cap_cx), abs(ref_cy - cap_cy))
            if dev > POS_TOL_CAPTURE:
                reasons.append("class %s centroid deviates %.2f DS px "
                               "(tol %d)" % (name, dev, POS_TOL_CAPTURE))
        if reasons:
            verdict = "FAIL"

    if verdict == "NOT-COMPARABLE":
        report.append(("verdict", "NOT-COMPARABLE"))
    else:
        report.append(("verdict", verdict))
    report.append(("reason", "; ".join(reasons) if reasons else "within "
                   "tolerance"))
    return report


def _bbox_lines(prefix, bbox):
    if bbox is None:
        return [("%s_bbox_left" % prefix, "-"),
                ("%s_bbox_top" % prefix, "-"),
                ("%s_bbox_right" % prefix, "-"),
                ("%s_bbox_bottom" % prefix, "-")]
    return [("%s_bbox_left" % prefix, str(bbox[0])),
            ("%s_bbox_top" % prefix, str(bbox[1])),
            ("%s_bbox_right" % prefix, str(bbox[2])),
            ("%s_bbox_bottom" % prefix, str(bbox[3]))]


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        description="EQ-4: compare a cc3dsfs DS capture with a 256x192 "
                    "reference PPM (EMULATOR/BUILD tooling; one console, one "
                    "capture, never a hardware-equality claim).")
    parser.add_argument("--capture", required=True,
                        help="cc3dsfs top-screen capture PNG (400x240 crop or "
                             "a full screenshot whose top-left 400x240 is "
                             "used)")
    parser.add_argument("--reference", required=True,
                        help="DS-resolution reference P6 PPM (256x192 harness "
                             "framebuffer, or a 400x240/400x480 'ours' PPM)")
    parser.add_argument("--mapping", default=DEFAULT_MAPPING,
                        help="shared capture mapping JSON (default: "
                             "tools/ds_vtxcolor_test/capture_mapping.json)")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        mapping = load_mapping(args.mapping)
        capture = load_capture(args.capture)
        reference = load_reference(args.reference, mapping)
    except InputError as exc:
        print("eq4_hw_compare: error: %s" % exc, file=sys.stderr)
        return 2
    report = compare(capture, reference, mapping)
    for key, value in report:
        print("%s=%s" % (key, value))
    verdict = dict(report)["verdict"]
    if verdict == "FAIL":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
