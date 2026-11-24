#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Azahar host presentation-row correction (EQ-4n / EQ-4o).

The Azahar libretro core's software presentation of the arm11 top screen adds
one capture row to every horizontal boundary and wraps PPM row 0 as a
horizontally shifted copy of row 239.  Real hardware places the same edges at
``ceil(1.25*y_ds)`` with no wrap.  Basis: EQ-4n (mechanism) and EQ-4o
(hardware), one console, one capture session per frame; measured only for
400x240 top-screen output.

Wrap signature (exactly): for a 400-wide image the top screen has 240 rows and
``row0[x] == row239[x+1]`` for every x in 0..398, and ``row0 != row239`` at
some x (the second clause rejects flat/uniform images, which the EQ-4o probe
alone would accept).  The row-0 wrap is dropped by the correction.

Correction: ``corrected[y] = image[y+1]`` for y = 0..238, so content moves up
one row.  ``corrected[239] = image[239]``: no capture row exists for true row
239, so the last corrected row is a documented duplicate of true row 238 (and
equals the uncorrected row 239).  For a 400x480 PPM only the upper 400x240
top screen is corrected; the 400x240 bottom screen is left unchanged.

Every applying tool prints ``presentation-row correction: applied|not applied
(reason)``.  The mode is ``auto`` (guard), ``on`` (forced) or ``off`` (never);
``off`` reproduces the legacy numbers byte for byte.
"""

BASIS = ("EQ-4n/EQ-4o | basis: one console, one capture session per frame | "
         "measured only for 400x240 top-screen output")

TOP_WIDTH = 400
TOP_HEIGHT = 240
FULL_HEIGHT = 480

MODES = ("auto", "on", "off")


def _top_screen(pixels, width, height):
    """True when this is a 400x240 / 400x480 top-screen image."""
    return width == TOP_WIDTH and height in (TOP_HEIGHT, FULL_HEIGHT)


def detect_wrap(pixels, width, height):
    """Return True when the documented row-0 wrap signature is present."""
    if not _top_screen(pixels, width, height):
        return False
    row0 = pixels[0:TOP_WIDTH]
    row239 = pixels[(TOP_HEIGHT - 1) * TOP_WIDTH:TOP_HEIGHT * TOP_WIDTH]
    if row0 == row239:
        return False
    return all(row0[x] == row239[x + 1] for x in range(TOP_WIDTH - 1))


def remove_presentation_row(pixels, width, height):
    """Drop the wrapped row 0 and shift the top screen up one row.

    Non-400-wide images and 256x192 native frames are returned unchanged.  The
    last top-screen row duplicates true row 238 (see the module docstring).
    """
    if not _top_screen(pixels, width, height):
        return pixels
    out = list(pixels[TOP_WIDTH:TOP_HEIGHT * TOP_WIDTH])
    out.extend(pixels[(TOP_HEIGHT - 1) * TOP_WIDTH:TOP_HEIGHT * TOP_WIDTH])
    if height == FULL_HEIGHT:
        out.extend(pixels[TOP_HEIGHT * TOP_WIDTH:])
    return out


def apply_correction(mode, pixels, width, height):
    """Return ``(pixels, applied, reason)`` for the requested mode."""
    if mode not in MODES:
        raise ValueError("unknown presentation-row correction mode %r" % mode)
    if mode == "off":
        return pixels, False, "--azahar-correction off"
    if mode == "on":
        if not _top_screen(pixels, width, height):
            return (pixels, False,
                    "--azahar-correction on but image is %dx%d, not a 400-wide "
                    "azahar top screen" % (width, height))
        return (remove_presentation_row(pixels, width, height), True,
                "--azahar-correction on (forced)")
    if detect_wrap(pixels, width, height):
        return (remove_presentation_row(pixels, width, height), True,
                "row-0 wrap signature detected")
    return pixels, False, "no row-0 wrap signature"


def format_line(applied, reason):
    """The visible line every applying tool prints."""
    return "presentation-row correction: %s (%s)" % (
        "applied" if applied else "not applied", reason)
