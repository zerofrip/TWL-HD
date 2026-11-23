#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Expected DS polygon-alpha blend values for the A1 = a/31 vs A2 = (a+1)/32 test.

The DS framebuffer is 5 bits per channel; the display expands a 5-bit value v5
to 8 bits with `v8 = (v5 << 3) | (v5 >> 2)` (``expand5``).  A translucent polygon
with 5-bit polygon alpha ``a`` (1..30) blends its 5-bit source colour ``S5`` over
the 5-bit backdrop ``B5`` with a blend weight ``w``:

    A1 = a / 31          A2 = (a + 1) / 32

Three *families* of variants are computed because it is not known where the
hardware rounds and where it drops to 5 bits:

  * ``5bit_*``  blend in 5-bit units (``b5 = w*S5 + (1-w)*B5``), round or floor
    to an integer 5-bit value, then expand to 8 bits.  This is the physically
    likely path: the DS colour buffer is 5-bit.
  * ``8bit_*``  expand both operands to 8 bits first (``S8 = expand5(S5)``,
    ``B8 = expand5(B5)``), blend in 8-bit units, round or floor.
  * ``6bit_round``  DeSmuME-style 6-bit pipeline: expand 5-bit to 6 bits with
    ``expand6(v5) = (v5<<1)|(v5>>4)``, blend per convention, then output
    ``round_half_up(v6*255/63)``.  This models DeSmuME's internal BGR666
    framebuffer (5->6 ``material_5bit_to_6bit`` and 6->8
    ``material_6bit_to_8bit``) and is a **HYPOTHESIS** for real hardware.

Rounding is always **round-half-up** (``floor(x + 0.5)``), never Python's
banker's rounding, so the printed tables are reproducible.  ``floor`` variants
are included to show how much a truncating rasteriser would move the result.

This module is the single place the numbers come from: the measurement tool and
the unit tests import it, and the ROM uses the same ``quads.json`` geometry.

    python3 expected.py
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
QUADS_JSON = os.path.join(HERE, "quads.json")

ALPHAS = (4, 8, 12, 16, 20, 24, 28)
VARIANTS = ("5bit_round", "5bit_floor", "8bit_round", "8bit_floor",
            "6bit_round")
CONVENTIONS = ("A1", "A2")

# Best-case separation (8-bit levels) below which a quad cannot reliably
# decide the convention.  a = 16 reaches only 4 on one variant/backdrop, and
# a >= 20 reach 3 or less (see main()).
WEAK_SEPARATION = 5


def expand5(v5):
    """DS 5-bit -> 8-bit display expansion."""
    return (v5 << 3) | (v5 >> 2)


def expand6(v5):
    """DeSmuME-style 5-bit -> 6-bit expansion: ``(v5 << 1) | (v5 >> 4)``.

    DeSmuME's ``material_5bit_to_6bit`` table is ``2*v+1`` for ``v >= 1`` and 0
    for 0 (utils/colorspacehandler/colorspacehandler.cpp:98-103); the two agree
    at the values this test uses (0, 16, 31).  HYPOTHESIS for real hardware.
    """
    return (v5 << 1) | (v5 >> 4)


def round_half_up(x):
    """Round a non-negative float half-up (deterministic, not banker's)."""
    return int(math.floor(x + 0.5))


def weight(alpha, convention):
    """Blend weight for a 5-bit polygon alpha under A1 or A2."""
    if convention == "A1":
        return alpha / 31.0
    if convention == "A2":
        return (alpha + 1) / 32.0
    raise ValueError("convention must be A1 or A2: %r" % (convention,))


def expected8(alpha, backdrop5, convention, variant, src5=31):
    """Expected 8-bit display value of one blended channel."""
    if variant not in VARIANTS:
        raise ValueError("unknown variant: %r" % (variant,))
    w = weight(alpha, convention)
    if variant == "6bit_round":
        src6 = expand6(src5)
        back6 = expand6(backdrop5)
        if convention == "A2":
            # DeSmuME-exact: alpha = a+1, integer blend in 6-bit units, >>5.
            alpha6 = alpha + 1
            v6 = (alpha6 * src6 + (32 - alpha6) * back6) >> 5
        else:
            # Counterfactual A1 (no source basis); floor mirrors the >>5 floor.
            v6 = int(math.floor(w * src6 + (1.0 - w) * back6))
        v6 = max(0, min(63, v6))
        # HYPOTHESIS output stage: round-half-up(v6*255/63).
        return round_half_up(v6 * 255.0 / 63.0)
    if variant.startswith("5bit"):
        raw5 = w * src5 + (1.0 - w) * backdrop5
        if variant == "5bit_round":
            b5 = round_half_up(raw5)
        else:
            b5 = int(math.floor(raw5))
        b5 = max(0, min(31, b5))
        return expand5(b5)
    src8 = expand5(src5)
    back8 = expand5(backdrop5)
    raw8 = w * src8 + (1.0 - w) * back8
    if variant == "8bit_round":
        return round_half_up(raw8)
    return int(math.floor(raw8))


def expectations(alpha, backdrop5):
    """{(variant, convention): expected8} for one quad over one backdrop."""
    return {(variant, conv): expected8(alpha, backdrop5, conv, variant)
            for variant in VARIANTS for conv in CONVENTIONS}


def separations(alpha, backdrop5):
    """{variant: |E_A1 - E_A2|} for one quad over one backdrop."""
    return {variant: abs(expected8(alpha, backdrop5, "A1", variant)
                         - expected8(alpha, backdrop5, "A2", variant))
            for variant in VARIANTS}


def best_separation(alpha, backdrops5=(0, 16)):
    """Largest A1/A2 separation over every variant and backdrop."""
    return max(separations(alpha, b)[v]
               for b in backdrops5 for v in VARIANTS)


def min_separation(alpha, backdrops5=(0, 16)):
    """Smallest A1/A2 separation over every variant and backdrop."""
    return min(separations(alpha, b)[v]
               for b in backdrops5 for v in VARIANTS)


def weak_alphas():
    return [a for a in ALPHAS if best_separation(a) < WEAK_SEPARATION]


def load(path=QUADS_JSON):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _print_formula():
    print("DS 5->8 expansion: v8 = (v5 << 3) | (v5 >> 2)")
    print("A1 weight w = a/31 ; A2 weight w = (a+1)/32 ; source colour 31,31,31")
    print("5bit_* : b5 = w*31 + (1-w)*B5 -> round/floor -> expand5")
    print("8bit_* : S8 = expand5(31) = 255 ; B8 = expand5(B5) ; "
          "e8 = w*S8 + (1-w)*B8 -> round/floor")
    print("6bit_round : v6 = (v5 << 1) | (v5 >> 4) ; "
          "A2 v6 = ((a+1)*S6 + (31-a)*B6) >> 5 (blend floored) ; "
          "A1 v6 = floor((a/31)*S6 + ((31-a)/31)*B6) (counterfactual, no source basis)")
    print("6bit_round output rounding: round-half-up(v6*255/63) = floor(x + 0.5)")
    print("6bit_round source: DeSmuME BGR666 path "
          "(material_5bit_to_6bit / material_6bit_to_8bit, "
          "utils/colorspacehandler/colorspacehandler.cpp:97-134) -- "
          "HYPOTHESIS for real hardware, NOT a hardware measurement")
    print("rounding: round-half-up = floor(x + 0.5)")
    print()


def main():
    data = load()
    backdrops = data["backdrops"]
    _print_formula()
    for backdrop in backdrops:
        b5 = backdrop["colour5"][0]
        print("== backdrop %s (5-bit %d -> 8-bit %d) =="
              % (backdrop["name"], b5, expand5(b5)))
        for alpha in ALPHAS:
            a1 = [expected8(alpha, b5, "A1", v) for v in VARIANTS]
            a2 = [expected8(alpha, b5, "A2", v) for v in VARIANTS]
            sep = separations(alpha, b5)
            print("a=%2d A1=%s A2=%s sep=%s"
                  % (alpha,
                     "/".join(str(x) for x in a1),
                     "/".join(str(x) for x in a2),
                     "/".join("%s=%d" % (v, sep[v]) for v in VARIANTS)))
        print("   (columns follow variant order %s)" % ",".join(VARIANTS))
        print()
    print("== best-case separation over all variants/backdrops ==")
    for alpha in ALPHAS:
        best = best_separation(alpha)
        least = min_separation(alpha)
        tag = "WEAK" if best < WEAK_SEPARATION else "strong"
        print("a=%2d best=%d min=%d %s" % (alpha, best, least, tag))
    weak = weak_alphas()
    print("weak discriminators (best separation < %d): %s"
          % (WEAK_SEPARATION, ", ".join("a=%d" % a for a in weak) or "none"))
    print("strong discriminators: %s"
          % ", ".join("a=%d" % a for a in ALPHAS if a not in weak))
    below = [a for a in ALPHAS if min_separation(a) < WEAK_SEPARATION]
    print("below WEAK_SEPARATION under minimum separation: %s"
          % (", ".join("a=%d" % a for a in below) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
