#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline model of the arm11-vs-DS blend residual for EQ-4zb.

Two model families are computed and compared against the measured captures.

DS convention (EQ-4i, ``docs/reports/eq4i-alpha-hw-result.md`` §5.1/§5.2),
reusing the formulas in ``tools/ds_alpha_test/expected.py``:

  * ``expand6(v5) = (v5 << 1) | (v5 >> 4)``            (expected.py:73-80)
  * A2 6-bit blend: ``v6 = ((a+1)*S6 + (31-a)*B6) >> 5`` (expected.py:97-114)
  * green output  : ``round_half_up(v6 * 255 / 63)``     (expected.py:117-127)
  * red/blue out  : ``expand5(v6 >> 1)``, ``expand5(v5) = (v5<<3)|(v5>>2)``
                                                         (expected.py:68-70)

Our PICA path (SOURCE-READ): the alpha reaches the blend unit as the *float*
weight ``w = (a+1)/32`` (``common/twlhd_gx_interp.c:395-397``); no 8-bit
conversion exists in this repository.  The blend is
``ADD(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)`` on a 32-bit ``GPU_RB_RGBA8`` target
(``arm11/include/twlhd_blend_config.h:30-31``,
``arm11/source/renderer.c:757-758``) and the display transfer is RGB8, which is
lossless (``arm11/source/renderer.c:47-52``).  The candidates below model the
one invisible step — the PICA float->8-bit conversion and the blend-unit
rounding — as truncation or half-up rounding, and are labelled by that choice.

Nothing here is fitted to the measurements; each candidate is a mechanical
combination of the quoted conversion/rounding choices.
"""

import math

# EQ-4i / expected.py conventions -------------------------------------------


def expand5(v5):
    """DS 5-bit -> 8-bit display expansion (expected.py:68-70)."""
    return (v5 << 3) | (v5 >> 2)


def expand6(v5):
    """5-bit -> 6-bit expansion (expected.py:73-80)."""
    return (v5 << 1) | (v5 >> 4)


def round_half_up(x):
    """Round a non-negative float half-up, not banker's (expected.py:83-85)."""
    return int(math.floor(x + 0.5))


def ds_a2_v6(alpha, src5=31, backdrop5=0):
    """DeSmuME-style 6-bit A2 intermediate (expected.py:97-114).

    ``v6 = ((a+1)*S6 + (31-a)*B6) >> 5`` with ``S6 = expand6(src5)`` and
    ``B6 = expand6(backdrop5)``, clamped to 0..63.
    """
    src6 = expand6(src5)
    back6 = expand6(backdrop5)
    alpha6 = alpha + 1
    v6 = (alpha6 * src6 + (32 - alpha6) * back6) >> 5
    return max(0, min(63, v6))


def ds_green(alpha, src5=31, backdrop5=0):
    """DS green channel: 6-bit A2 value expanded with round(v6*255/63)."""
    return round_half_up(ds_a2_v6(alpha, src5, backdrop5) * 255.0 / 63.0)


def ds_redblue(alpha, src5=31, backdrop5=0):
    """DS red/blue channel: 5-bit quantisation of the same 6-bit value."""
    return expand5(ds_a2_v6(alpha, src5, backdrop5) >> 1)


# Our PICA path -------------------------------------------------------------


def blend_weight(alpha):
    """DS raw alpha -> float blend weight (common/twlhd_gx_interp.c:395-397).

    Alpha 0 is the DS wireframe case and is emitted as 0.0, not a weight.
    """
    return 0.0 if alpha == 0 else (alpha + 1) / 32.0


def _float_blend(alpha, src8, back8):
    """Ideal blend in 8-bit units with the float weight; returns the raw float."""
    w = blend_weight(alpha)
    return w * src8 + (1.0 - w) * back8


def exact_float_floor(alpha, src8, back8):
    return int(math.floor(_float_blend(alpha, src8, back8)))


def exact_float_round(alpha, src8, back8):
    return round_half_up(_float_blend(alpha, src8, back8))


def _alpha8(alpha, alpha_conv):
    """Model the invisible float->8-bit conversion of the fragment alpha."""
    raw = blend_weight(alpha) * 255.0
    if alpha_conv == "trunc":
        return int(math.floor(raw))
    if alpha_conv == "round":
        return round_half_up(raw)
    raise ValueError("alpha_conv must be 'trunc' or 'round': %r" % (alpha_conv,))


def _a8_blend(alpha, src8, back8, alpha_conv, blend_rounding):
    """8-bit integer blend with the source factor A8/255 and 1-A8/255."""
    a8 = _alpha8(alpha, alpha_conv)
    raw = (a8 * src8 + (255 - a8) * back8) / 255.0
    if blend_rounding == "floor":
        return int(math.floor(raw))
    if blend_rounding == "round":
        return round_half_up(raw)
    raise ValueError("blend_rounding must be 'floor' or 'round': %r"
                     % (blend_rounding,))


def alpha8_trunc_floor(alpha, src8, back8):
    return _a8_blend(alpha, src8, back8, "trunc", "floor")


def alpha8_trunc_round(alpha, src8, back8):
    return _a8_blend(alpha, src8, back8, "trunc", "round")


def alpha8_round_floor(alpha, src8, back8):
    return _a8_blend(alpha, src8, back8, "round", "floor")


def alpha8_round_round(alpha, src8, back8):
    return _a8_blend(alpha, src8, back8, "round", "round")


# The six source-derived candidates of our path, keyed by name.  The first two
# keep the alpha float all the way to the blend; the last four insert the
# invisible float->8-bit alpha conversion before an 8-bit integer blend.
OUR_CANDIDATES = (
    ("exact_float_floor", exact_float_floor),
    ("exact_float_round", exact_float_round),
    ("alpha8_trunc_floor", alpha8_trunc_floor),
    ("alpha8_trunc_round", alpha8_trunc_round),
    ("alpha8_round_floor", alpha8_round_floor),
    ("alpha8_round_round", alpha8_round_round),
)

# Scene-4 quads: (name, alpha, channel, source 8-bit) and the two backdrops.
QUADS = (
    ("green", 16, "G", 255),
    ("blue", 4, "B", 255),
)


def _ds_for(channel, alpha):
    """DS convention on the quad's channel over a black backdrop (EQ-4i)."""
    if channel == "G":
        return ds_green(alpha, backdrop5=0)
    return ds_redblue(alpha, backdrop5=0)


def match_table():
    """Return rows comparing candidates against the measured captures.

    Each row is ``(label, green_black, blue_black, green_grey, blue_grey)``.
    Measured values are cited in the report; this function only computes the
    models.
    """
    backdrops = (("black", (0, 0, 0)), ("grey", (16, 16, 24)))
    rows = []
    ds_green_black = _ds_for("G", 16)
    ds_blue_black = _ds_for("B", 4)
    # The DS was only captured over a black backdrop (EQ-4i), so the grey cells
    # are "n/a" rather than an unmeasured DS-side comparison.
    rows.append(("DS convention (EQ-4i)",
                 ds_green_black, ds_blue_black, None, None))
    for name, fn in OUR_CANDIDATES:
        cells = []
        for _label, back in backdrops:
            cells.append(fn(16, 255, back[1]))  # green, backdrop green byte
            cells.append(fn(4, 255, back[2]))   # blue, backdrop blue byte
        rows.append((name, cells[0], cells[1], cells[2], cells[3]))
    return rows


def alpha_table(channel, our_fn=exact_float_round):
    """Full 0..31 table for one channel over a black backdrop.

    Returns ``(alpha, ds_value, our_value, delta)`` with ``delta = our - ds``.
    Alpha 0 is the DS wireframe case; its formula cells are marked by the
    caller, not here.
    """
    rows = []
    for alpha in range(32):
        ds = _ds_for(channel, alpha)
        ours = our_fn(alpha, 255, 0)
        rows.append((alpha, ds, ours, ours - ds))
    return rows


def main():
    print("tool=eq4zb_blend_model")
    print("basis=offline model, no hardware, no network")
    print("measured_green_black=138 (eq4za)")
    print("measured_blue_black=41 (eq4za)")
    print("measured_green_grey=142 (eq4y/eq4x)")
    print("measured_blue_grey=57 (eq4y) / 60 (eq4x)")
    print("candidate,green_black,blue_black,green_grey,blue_grey")
    for label, g_black, b_black, g_grey, b_grey in match_table():
        print("%s,%s,%s,%s,%s" % (label, g_black, b_black,
                                  "n/a" if g_grey is None else g_grey,
                                  "n/a" if b_grey is None else b_grey))
    print("alpha,ds_g,our_g,delta_g,ds_b,our_b,delta_b")
    for (a, ds_g, our_g, dg), (_a, ds_b, our_b, db) in zip(
            alpha_table("G"), alpha_table("B")):
        print("%d,%d,%d,%d,%d,%d,%d" % (a, ds_g, our_g, dg, ds_b, our_b, db))


if __name__ == "__main__":
    main()
