#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EQ-4g: eq4_hw_compare DS-space report with the fitted capture mapping.

`eq4_hw_compare.compare()` indexes the capture array with
`mapping["offset_x"]`/`mapping["offset_y"]` directly, so a fractional fitted
offset cannot be passed through it without changing that tool.  This wrapper
reuses its float-safe pieces (`load_capture`, `load_reference`,
`capture_to_ds`, `compare_pixels`, `nonbg_bbox`, `class_stats`) and prints the
same DS-space keys (per-pixel metrics, bbox, bbox deviations, class areas and
centroids, verdict).  The capture-space half is not reproduced.

    python3 tools/eq4g_fit_compare.py --capture cap.png --reference ref.ppm \
        [--mapping current|fit] [--fit-capture fixtures/sc4_3_top400x240.png]

`--mapping fit` fits `scale_x/scale_y/offset_x/offset_y` from the
ds-scale-test capture (default `tools/ds_scale_test/fixtures/
sc4_3_top400x240.png`) with `tools/ds_scale_test/fit_mapping.py` and keeps the
offsets fractional.  `--mapping current` uses the shared capture mapping JSON
(integer offsets, exactly the eq4_hw_compare default).

Exit 0 (the verdict is in the output), 2 on input/PIL errors.
`basis=one console, one capture`.
"""

import argparse
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DS_DIR = os.path.join(TOOLS_DIR, "ds_scale_test")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
if DS_DIR not in sys.path:
    sys.path.insert(0, DS_DIR)

import azahar_presentation  # noqa: E402
import eq4_hw_compare as eq4  # noqa: E402
import fit_mapping  # noqa: E402

DEFAULT_FIT_CAPTURE = os.path.join(DS_DIR, "fixtures",
                                   "sc4_3_top400x240.png")


class InputError(Exception):
    """Usage or input failure: exit 2."""


def fitted_mapping(capture_path):
    """Return the float mapping fitted from `capture_path`."""
    try:
        pattern = fit_mapping.load_json(fit_mapping.PATTERN_JSON,
                                        "pattern.json")
        nominal = fit_mapping.load_json(fit_mapping.MAPPING_JSON,
                                        "capture mapping")
        pixels, width, height = fit_mapping.read_capture(capture_path)
    except ValueError as exc:
        raise InputError(str(exc))
    _observations, fits, _skipped = fit_mapping.fit_capture(
        pixels, width, height, pattern, nominal)
    if fits["x"] is None or fits["y"] is None:
        raise InputError("fit_mapping found too few edges in %s"
                         % capture_path)
    return {
        "scale_x": fits["x"]["a"],
        "scale_y": fits["y"]["a"],
        "offset_x": fits["x"]["b"],
        "offset_y": fits["y"]["b"],
    }


def report(capture, reference, mapping, correction="auto"):
    ds = eq4.capture_to_ds(capture, mapping)
    width, height, pixels = eq4.parse_ppm(reference)
    pixels, applied, reason = azahar_presentation.apply_correction(
        correction, pixels, width, height)
    print(azahar_presentation.format_line(applied, reason))
    ref = eq4.load_reference_pixels(pixels, width, height, mapping, reference)
    metrics = eq4.compare_pixels(ref, ds, eq4.DS_W, eq4.DS_H)
    ref_bbox, ref_n = eq4.nonbg_bbox(ref, eq4.DS_W, eq4.DS_H)
    cap_bbox, cap_n = eq4.nonbg_bbox(ds, eq4.DS_W, eq4.DS_H)
    ref_cls = eq4.class_stats(ref)
    cap_cls = eq4.class_stats(ds)

    lines = [
        ("basis", "one console, one capture"),
        ("wrapper", "eq4g_fit_compare (DS-space eq4_hw_compare subset)"),
        ("tolerance_flat", str(eq4.TOL_FLAT)),
        ("tolerance_blended_edge", str(eq4.TOL_BLENDED)),
        ("tolerance_pos_ds", str(eq4.POS_TOL_CAPTURE)),
        ("mapping_scale_x", "%.6f" % mapping["scale_x"]),
        ("mapping_scale_y", "%.6f" % mapping["scale_y"]),
        ("mapping_offset_x", "%.6f" % mapping["offset_x"]),
        ("mapping_offset_y", "%.6f" % mapping["offset_y"]),
    ]
    for key in ("mean_abs_r", "mean_abs_g", "mean_abs_b", "max_error",
                "frac_within", "frac_within_flat", "frac_within_edge"):
        lines.append(("ds.%s" % key, eq4._fmt(metrics[key])))
    lines.append(("ds.flat_pixels", str(metrics["flat"])))
    lines.append(("ds.edge_pixels", str(metrics["edge"])))
    lines.append(("ref_nonbg_pixels", str(ref_n)))
    lines.append(("capture_nonbg_pixels", str(cap_n)))
    lines.extend(eq4._bbox_lines("ref", ref_bbox))
    lines.extend(eq4._bbox_lines("capture", cap_bbox))
    for name, _ in eq4.CLASSES:
        for side, stats in (("ref", ref_cls), ("capture", cap_cls)):
            count, cx, cy = stats[name]
            lines.append(("class_%s_%s_area" % (name, side), str(count)))
            lines.append(("class_%s_%s_centroid_x" % (name, side),
                          eq4._fmt(cx)))
            lines.append(("class_%s_%s_centroid_y" % (name, side),
                          eq4._fmt(cy)))

    reasons = []
    verdict = "PASS"
    if cap_n == 0 or cap_bbox is None:
        verdict = "NOT-COMPARABLE"
        reasons.append("capture has no non-background region (empty or blank)")
    elif ref_n == 0 or ref_bbox is None:
        verdict = "NOT-COMPARABLE"
        reasons.append("reference has no non-background region")
    else:
        for index, name in enumerate(("left", "top", "right", "bottom")):
            deviation = abs(ref_bbox[index] - cap_bbox[index])
            lines.append(("bbox_dev_%s" % name, str(deviation)))
            if deviation > eq4.POS_TOL_CAPTURE:
                reasons.append("bbox-%s deviates %d DS px (tol %d)"
                               % (name, deviation, eq4.POS_TOL_CAPTURE))
        if metrics["frac_within_flat"] < eq4.MIN_WITHIN_FRACTION:
            reasons.append("ds.frac_within_flat %.4f < %.2f"
                           % (metrics["frac_within_flat"],
                              eq4.MIN_WITHIN_FRACTION))
        for name, _ in eq4.CLASSES:
            ref_count, ref_cx, ref_cy = ref_cls[name]
            cap_count, cap_cx, cap_cy = cap_cls[name]
            if ref_count < eq4.MIN_CLASS_AREA:
                continue
            if cap_count < eq4.MIN_CLASS_AREA:
                reasons.append("class %s present in reference (%d px) but "
                               "absent from capture (%d px)"
                               % (name, ref_count, cap_count))
                continue
            dev = max(abs(ref_cx - cap_cx), abs(ref_cy - cap_cy))
            if dev > eq4.POS_TOL_CAPTURE:
                reasons.append("class %s centroid deviates %.2f DS px "
                               "(tol %d)" % (name, dev, eq4.POS_TOL_CAPTURE))
        if reasons:
            verdict = "FAIL"
    lines.append(("verdict", verdict))
    lines.append(("reason", "; ".join(reasons) if reasons else
                  "within tolerance"))
    return lines


def build_parser():
    parser = argparse.ArgumentParser(
        description="EQ-4g: DS-space eq4_hw_compare report with the fitted "
                    "capture mapping (one console, one capture).")
    parser.add_argument("--capture", required=True,
                        help="cc3dsfs top-screen capture PNG")
    parser.add_argument("--reference", required=True,
                        help="reference P6 PPM (256x192 or a 400x240/400x480 "
                             "'ours' PPM)")
    parser.add_argument("--mapping", choices=("current", "fit"),
                        default="current",
                        help="current shared JSON (integer offsets) or the "
                             "fitted float mapping (default: current)")
    parser.add_argument("--fit-capture", default=DEFAULT_FIT_CAPTURE,
                        help="ds-scale-test capture to fit when --mapping fit "
                             "(default: tools/ds_scale_test/fixtures/"
                             "sc4_3_top400x240.png)")
    parser.add_argument("--azahar-correction", choices=azahar_presentation.MODES,
                        default="auto",
                        help="EQ-4p presentation-row correction for an Azahar "
                             "'ours' reference: auto (guard, default), on "
                             "(forced), off (never)")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        if args.mapping == "fit":
            mapping = fitted_mapping(args.fit_capture)
        else:
            mapping = eq4.load_mapping(eq4.DEFAULT_MAPPING)
        capture = eq4.load_capture(args.capture)
        lines = report(capture, args.reference, mapping,
                       args.azahar_correction)
    except (InputError, eq4.InputError) as exc:
        print("eq4g_fit_compare: error: %s" % exc, file=sys.stderr)
        return 2
    for key, value in lines:
        print("%s=%s" % (key, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
