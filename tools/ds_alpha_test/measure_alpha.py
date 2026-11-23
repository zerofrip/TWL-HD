#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Decide the DS polygon-alpha blend convention from a capture of ds-alpha-test.

The ROM (tools/ds_alpha_test/) draws an opaque black backdrop on the left and an
opaque 5-bit-16 grey backdrop on the right, each carrying seven translucent
white quads with polygon alpha a = 4,8,12,16,20,24,28.  This tool measures the
mean colour inside each quad and compares it against the expected values from
expected.py under A1 = a/31 and A2 = (a+1)/32, for five variants:

    5bit_round, 5bit_floor   (blend in 5-bit, then expand to 8-bit)
    8bit_round, 8bit_floor   (expand to 8-bit, then blend)
    6bit_round               (DeSmuME-style 6-bit: 5->6 `(v<<1)|(v>>4)`,
                              blend, output `round(v6*255/63)`; HYPOTHESIS)

Input mapping
-------------
  * capture (default): a cc3dsfs capture; the top-left 400x240 region is
    analysed with capture_mapping.json (scale 1.25, offset 40,0), the EQ-4c
    measured DS -> capture mapping.
  * --native: the image is the native 256x192 DS frame (identity mapping).  Use
    this for an emulator frame dump or a dpad-cropped native capture.

Measurement margin
------------------
Each quad patch is the centre patch_ds x patch_ds DS pixels (patch_ds = 8), so
its edge is margin_ds = 4 DS pixels from the quad edge; that clears the 1.25x
nearest-neighbour resampling smear of the capture path.  The mean (not median)
of the mapped capture pixels is reported.

Verdict rule
------------
1. Locate the two backdrops (8x8 patch at each backdrop "probe").  Each measured
   backdrop must be within TOL of its expected expanded 8-bit colour
   (expand5(0) = 0, expand5(16) = 132); a quad patch must fall inside the image.
   Otherwise the whole run is NOT-COMPARABLE.
2. For a variant, a quad is a *strong discriminator* when |E_A1 - E_A2| >=
   VOTE_SEP (3 8-bit levels).  It votes for the nearer convention.
3. Variant verdict = A1 when every strong discriminator votes A1 **and** every
   measured quad is within TOL of its A1 expectation; A2 symmetrically; else
   UNDECIDED (no strong discriminator also gives UNDECIDED).
4. Overall verdict = the convention of the *best-fitting variant family*: over
   the voting variants (5bit_round, 8bit_round, 6bit_round) x {A1, A2}, sum
   |measured - expected| over every quad and every channel, and take the
   minimum (deterministic tie-break: variant order, then A1 before A2).  The
   overall verdict is that convention iff *every* quad is within TOL of that
   family's expectation, else UNDECIDED.  Per-quad deviations for the winning
   family are printed (`quad_<name>_dev_best=`).  The floor variants are
   sensitivity checks (a truncating rasteriser) and are reported per variant
   but do not vote in the overall verdict: at some points an 8-bit-round A1
   value coincides with a 5-bit-floor A2 value, which is an artefact of mixing
   those families.

TOL = 8 is the flat per-channel tolerance used for the "matches this convention"
test and for the backdrop sanity check.  It is *not* used as the discrimination
gate: the largest A1/A2 separation over black is only 7 levels (a = 4), so a
flat 8-level discrimination gate would make every quad ambiguous.  VOTE_SEP (3)
is the separate, tighter gate.

    python3 measure_alpha.py [--native] <capture.png>

Exit status: 0 decided (A1 or A2); 1 UNDECIDED or NOT-COMPARABLE (report still
printed); 2 usage / missing PIL / unreadable input.
"""

import json
import math
import os
import sys

from expected import VARIANTS, expand5, expected8, separations

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

HERE = os.path.dirname(os.path.abspath(__file__))
QUADS_JSON = os.path.join(HERE, "quads.json")
MAPPING_JSON = os.path.join(HERE, "capture_mapping.json")

REGION_WIDTH = 400
REGION_HEIGHT = 240
NATIVE_WIDTH = 256
NATIVE_HEIGHT = 192

VOTE_SEP = 3     # |E_A1 - E_A2| needed for a quad to vote (8-bit levels)
TOL = 8          # flat per-channel tolerance for matching a convention
PRIMARY_VARIANT = "8bit_round"
# Round variants agree by construction on a clean capture.  The floor variants
# are sensitivity checks and do not vote in the overall verdict.  6bit_round is
# the DeSmuME-style 6-bit pipeline (HYPOTHESIS for hardware); it is included so
# the tool can model a 6-bit rasteriser instead of only 5/8-bit families.
OVERALL_VARIANTS = ("5bit_round", "8bit_round", "6bit_round")


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def identity_mapping():
    return {"scale_x": 1.0, "scale_y": 1.0, "offset_x": 0, "offset_y": 0}


def capture_xy(ds_x, ds_y, mapping):
    cx = mapping["offset_x"] + (ds_x + 0.5) * mapping["scale_x"] - 0.5
    cy = mapping["offset_y"] + (ds_y + 0.5) * mapping["scale_y"] - 0.5
    return int(round(cx)), int(round(cy))


def _round_half_up(x):
    return int(math.floor(x + 0.5))


def _sample_rect(pixels, width, height, ds_box, mapping):
    """Mean RGB over the capture pixels mapped from a DS-pixel rectangle."""
    x0, y0, x1, y1 = ds_box
    rs, gs, bs = [], [], []
    for dy in range(y0, y1):
        for dx in range(x0, x1):
            cx, cy = capture_xy(dx, dy, mapping)
            if 0 <= cx < width and 0 <= cy < height:
                r, g, b = pixels[cy * width + cx]
                rs.append(r)
                gs.append(g)
                bs.append(b)
    if not rs:
        return None
    return (_round_half_up(sum(rs) / len(rs)),
            _round_half_up(sum(gs) / len(gs)),
            _round_half_up(sum(bs) / len(bs)))


def _patch_box(cx, cy, patch_ds):
    half = patch_ds // 2
    return (cx - half, cy - half, cx + half, cy + half)


def _maps_inside(box, mapping):
    """True when all four corners of a DS box map into a finite image region.

    The image bounds are not known here; the caller checks that the mapped
    sample set is non-empty.  This only rejects boxes outside the DS screen.
    """
    x0, y0, x1, y1 = box
    return 0 <= x0 and 0 <= y0 and x1 <= 256 and y1 <= 192


def _all_within(measured, expected, tol=TOL):
    if isinstance(expected, int):
        expected = _triple(expected)
    return all(abs(m - e) <= tol for m, e in zip(measured, expected))


def _triple(value):
    return (value, value, value)


def _nearest(measured, a1, a2):
    """Nearer convention by summed per-channel distance; 'tie' when equal.

    a1/a2 may be a scalar channel value or an RGB tuple.
    """
    if isinstance(a1, int):
        a1 = _triple(a1)
    if isinstance(a2, int):
        a2 = _triple(a2)
    d1 = sum(abs(m - e) for m, e in zip(measured, a1))
    d2 = sum(abs(m - e) for m, e in zip(measured, a2))
    if d1 == d2:
        return "tie"
    return "A1" if d1 < d2 else "A2"


def _variant_verdict(quads, variant):
    if not quads:
        return "UNDECIDED"
    votes = []
    for q in quads:
        sep = q["sep"][variant]
        if sep < VOTE_SEP:
            continue
        near = _nearest(q["measured"], q["A1"][variant], q["A2"][variant])
        if near != "tie":
            votes.append(near)
    if not votes:
        return "UNDECIDED"
    if set(votes) == {"A1"} and all(
            _all_within(q["measured"], q["A1"][variant]) for q in quads):
        return "A1"
    if set(votes) == {"A2"} and all(
            _all_within(q["measured"], q["A2"][variant]) for q in quads):
        return "A2"
    return "UNDECIDED"


def _best_family_verdict(quads):
    """Return (verdict, variant, convention, total_dev) for the best-fit family.

    Total deviation is the per-channel sum over every quad and channel, the
    same metric as ``_nearest``.  Ties resolve deterministically to the earliest
    ``OVERALL_VARIANTS`` entry, then A1 before A2.  The verdict is the winning
    convention only when *every* quad is within TOL of that family, else
    UNDECIDED.
    """
    best = None
    for variant in OVERALL_VARIANTS:
        for convention in ("A1", "A2"):
            total = 0
            for q in quads:
                exp = _triple(q[convention][variant])
                total += sum(abs(m - e) for m, e in zip(q["measured"], exp))
            if best is None or total < best[0]:
                best = (total, variant, convention)
    total, variant, convention = best
    within = all(_all_within(q["measured"], q[convention][variant])
                 for q in quads)
    return (convention if within else "UNDECIDED"), variant, convention, total


def _quad_deviation(measured, expected):
    """Per-channel summed |measured - expected| for one quad."""
    return sum(abs(m - e) for m, e in zip(measured, _triple(expected)))


def measure(image, data, mapping):
    """Measure a PIL image.

    Returns (lines, verdict, comparable): lines is an ordered list of
    (key, value) pairs, verdict is A1/A2/UNDECIDED/NOT-COMPARABLE.
    """
    native = (mapping["scale_x"] == 1.0 and mapping["scale_y"] == 1.0
              and mapping["offset_x"] == 0 and mapping["offset_y"] == 0)
    region_w = NATIVE_WIDTH if native else REGION_WIDTH
    region_h = NATIVE_HEIGHT if native else REGION_HEIGHT
    region = image.convert("RGB").crop(
        (0, 0, min(region_w, image.width), min(region_h, image.height)))
    width, height = region.size
    raw = region.tobytes()
    pixels = [(raw[i], raw[i + 1], raw[i + 2])
              for i in range(0, len(raw), 3)]

    patch_ds = int(data["patch_ds"])
    lines = [("mode", "native" if native else "capture")]

    # --- backdrops: measure probes and sanity-check -------------------------
    comparable = True
    backdrop_rgb = {}
    for bd in data["backdrops"]:
        px, py = bd["probe"]
        box = _patch_box(px, py, patch_ds)
        rgb = _sample_rect(pixels, width, height, box, mapping)
        backdrop_rgb[bd["name"]] = rgb
        if rgb is None:
            comparable = False
            continue
        if not _all_within(rgb, [expand5(bd["colour5"][0])] * 3):
            comparable = False

    lines.append(("geometry_found", "1" if comparable else "0"))
    for bd in data["backdrops"]:
        rgb = backdrop_rgb.get(bd["name"])
        lines.append(("backdrop_%s_measured" % bd["name"],
                      _fmt_rgb(rgb)))

    # --- quads --------------------------------------------------------------
    quads = []
    for quad in data["quads"]:
        bd = next(b for b in data["backdrops"]
                  if b["name"] == quad["backdrop"])
        b5 = bd["colour5"][0]
        cx = quad["x"] + quad["w"] // 2
        cy = quad["y"] + quad["h"] // 2
        box = _patch_box(cx, cy, patch_ds)
        if not _maps_inside(box, mapping):
            comparable = False
        rgb = _sample_rect(pixels, width, height, box, mapping)
        if rgb is None:
            comparable = False
            rgb = (0, 0, 0)
        alpha = quad["alpha"]
        A1 = {v: expected8(alpha, b5, "A1", v) for v in VARIANTS}
        A2 = {v: expected8(alpha, b5, "A2", v) for v in VARIANTS}
        sep = separations(alpha, b5)
        entry = {"name": quad["name"], "measured": rgb, "A1": A1, "A2": A2,
                 "sep": sep}
        entry["nearest"] = _nearest(rgb, A1[PRIMARY_VARIANT], A2[PRIMARY_VARIANT]) \
            if sep[PRIMARY_VARIANT] >= VOTE_SEP else "ambiguous"
        quads.append(entry)

        lines.append(("quad_%s_measured" % entry["name"], _fmt_rgb(rgb)))
        for variant in VARIANTS:
            lines.append(("quad_%s_A1_%s" % (entry["name"], variant),
                          _fmt_rgb([A1[variant]] * 3)))
            lines.append(("quad_%s_A2_%s" % (entry["name"], variant),
                          _fmt_rgb([A2[variant]] * 3)))
        lines.append(("quad_%s_nearest" % entry["name"], entry["nearest"]))

    if not comparable:
        verdict = "NOT-COMPARABLE"
        variant_verdicts = {v: "NOT-COMPARABLE" for v in VARIANTS}
        best_lines = [("best_variant", "none"),
                      ("best_convention", "none"),
                      ("best_total_dev", "none")]
    else:
        variant_verdicts = {v: _variant_verdict(quads, v) for v in VARIANTS}
        verdict, best_variant, best_convention, best_total = \
            _best_family_verdict(quads)
        best_lines = [("best_variant", best_variant),
                      ("best_convention", best_convention),
                      ("best_total_dev", str(best_total))]
        for q in quads:
            best_lines.append(
                ("quad_%s_dev_best" % q["name"],
                 str(_quad_deviation(q["measured"],
                                     q[best_convention][best_variant]))))

    for variant in VARIANTS:
        lines.append(("variant_%s_verdict" % variant, variant_verdicts[variant]))
    lines.append(("model_6bit_rounding",
                  "blend floor; output round-half-up(v6*255/63)"))
    lines.extend(best_lines)
    lines.append(("verdict", verdict))
    return lines, verdict, comparable


def _fmt_rgb(rgb):
    if rgb is None:
        return "none"
    return "%d,%d,%d" % tuple(rgb)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    native = "--native" in argv[1:]
    unknown = [a for a in argv[1:] if a.startswith("--") and a != "--native"]
    if unknown or len(args) != 1:
        sys.stderr.write("usage: measure_alpha.py [--native] <capture.png>\n")
        return 2
    if Image is None:
        sys.stderr.write(
            "measure_alpha: PIL is not installed (pip install Pillow)\n")
        return 2
    try:
        data = load_json(QUADS_JSON)
        mapping = identity_mapping() if native else load_json(MAPPING_JSON)
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "measure_alpha: cannot read quads/mapping json: %s\n" % exc)
        return 2
    try:
        image = Image.open(args[0])
        image.load()
    except (OSError, ValueError) as exc:
        sys.stderr.write("measure_alpha: cannot read %s: %s\n"
                         % (args[0], exc))
        return 2

    lines, verdict, comparable = measure(image, data, mapping)
    for key, value in lines:
        print("%s=%s" % (key, value))

    if not comparable:
        sys.stderr.write(
            "measure_alpha: NOT-COMPARABLE: backdrops or quads not located "
            "in this image\n")
        return 1
    if verdict == "UNDECIDED":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
