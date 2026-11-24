#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/azahar_presentation.py (EQ-4p presentation-row correction).

Positive: a synthetic image built by shifting a nominal 400x240 pattern down
one row (with the documented row-0 wrap) is returned to nominal.  Negative:
an already-nominal image is not corrected by the auto guard, a second pass is
a no-op, a flat image is not flagged, and the two EQ-4o hardware crops are
detected as no-wrap and left byte-unchanged.  The hardware PNG tests skip with
a labelled reason when PIL is missing.

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_azahar_presentation.py'
"""

import os
import sys
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import azahar_presentation as az  # noqa: E402

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    Image = None
    HAVE_PIL = False

# Set only by the aggregate `test` recipe, which runs this suite first via the
# test-azahar-presentation target.
DISCOVER_SKIP = os.environ.get("AZAHAR_PRESENTATION_DISCOVER_SKIP") is not None

HW_DIR = os.path.join(TOOLS_DIR, "eq4o_hw_rows")
HW_CROPS = (os.path.join(HW_DIR, "hw_frameA_top400x240.png"),
            os.path.join(HW_DIR, "hw_frameB_top400x240.png"))

WIDTH, HEIGHT = az.TOP_WIDTH, az.TOP_HEIGHT


def pattern_row(y):
    return [((x * 7 + y * 13) % 256, (x * 3 + y * 5) % 256,
             (x + y) % 256) for x in range(WIDTH)]


def nominal_image():
    rows = [pattern_row(y) for y in range(HEIGHT)]
    # The correction cannot recover true row 239 (no capture row exists), so
    # make the last two nominal rows identical: the documented duplicate is
    # then pixel-identical to nominal.
    rows[HEIGHT - 1] = list(rows[HEIGHT - 2])
    return [pixel for row in rows for pixel in row]


def azahar_image(nominal):
    """Nominal content shifted down one row, plus the documented row-0 wrap."""
    rows = [None] * HEIGHT
    rows[0] = None  # filled after the shifted rows exist
    for y in range(1, HEIGHT):
        rows[y] = list(nominal[(y - 1) * WIDTH:y * WIDTH])
    row239 = rows[HEIGHT - 1]
    row0 = [row239[x + 1] for x in range(WIDTH - 1)] + [(0, 0, 0)]
    rows[0] = row0
    return [pixel for row in rows for pixel in row]


def read_png(path):
    image = Image.open(path).convert("RGB")
    raw = image.tobytes()
    return ([(raw[i], raw[i + 1], raw[i + 2])
             for i in range(0, len(raw), 3)], image.width, image.height)


@unittest.skipIf(DISCOVER_SKIP,
                 "run once via the test-azahar-presentation make target; "
                 "discover skips it to avoid a double run")
class AzaharPresentationTest(unittest.TestCase):
    def test_correction_restores_nominal(self):
        nominal = nominal_image()
        shifted = azahar_image(nominal)
        self.assertTrue(az.detect_wrap(shifted, WIDTH, HEIGHT))
        pixels, applied, reason = az.apply_correction("auto", shifted,
                                                      WIDTH, HEIGHT)
        self.assertTrue(applied, reason)
        self.assertEqual(reason, "row-0 wrap signature detected")
        self.assertEqual(pixels, nominal)

    def test_nominal_image_is_not_corrected_twice(self):
        nominal = nominal_image()
        self.assertFalse(az.detect_wrap(nominal, WIDTH, HEIGHT))
        pixels, applied, reason = az.apply_correction("auto", nominal,
                                                      WIDTH, HEIGHT)
        self.assertFalse(applied)
        self.assertEqual(reason, "no row-0 wrap signature")
        self.assertEqual(pixels, nominal)
        # A second pass on the already-nominal result is a no-op.
        again, applied_again, _reason = az.apply_correction("auto", pixels,
                                                            WIDTH, HEIGHT)
        self.assertFalse(applied_again)
        self.assertEqual(again, nominal)

    def test_flat_image_is_not_flagged(self):
        flat = [(50, 60, 70)] * (WIDTH * HEIGHT)
        self.assertFalse(az.detect_wrap(flat, WIDTH, HEIGHT))
        pixels, applied, _reason = az.apply_correction("auto", flat, WIDTH,
                                                       HEIGHT)
        self.assertFalse(applied)
        self.assertEqual(pixels, flat)

    def test_native_frame_is_never_corrected(self):
        native = [(9, 9, 9)] * (256 * 192)
        pixels, applied, reason = az.apply_correction("auto", native, 256, 192)
        self.assertFalse(applied)
        self.assertIs(pixels, native)
        self.assertEqual(reason, "no row-0 wrap signature")

    def test_explicit_off_leaves_azahar_image_unchanged(self):
        shifted = azahar_image(nominal_image())
        pixels, applied, reason = az.apply_correction("off", shifted,
                                                      WIDTH, HEIGHT)
        self.assertFalse(applied)
        self.assertEqual(reason, "--azahar-correction off")
        self.assertIs(pixels, shifted)

    def test_400x480_correction_keeps_bottom_screen(self):
        top = azahar_image(nominal_image())
        bottom = [(1, 2, 3)] * (WIDTH * HEIGHT)
        pixels, applied, _reason = az.apply_correction(
            "on", top + bottom, WIDTH, 2 * HEIGHT)
        self.assertTrue(applied)
        self.assertEqual(pixels[:WIDTH * HEIGHT], nominal_image())
        self.assertEqual(pixels[WIDTH * HEIGHT:], bottom)

    @unittest.skipUnless(HAVE_PIL, "PIL is not installed, so the hardware PNG "
                                   "crops cannot be read (labelled skip)")
    @unittest.skipUnless(all(os.path.isfile(path) for path in HW_CROPS),
                         "the EQ-4o hardware crops are not present")
    def test_hardware_crops_have_no_wrap_and_are_unchanged(self):
        for path in HW_CROPS:
            pixels, width, height = read_png(path)
            self.assertEqual((width, height), (WIDTH, HEIGHT), path)
            self.assertFalse(az.detect_wrap(pixels, width, height), path)
            corrected, applied, _reason = az.apply_correction(
                "auto", pixels, width, height)
            self.assertFalse(applied, path)
            self.assertEqual(corrected, pixels, path)


if __name__ == "__main__":
    unittest.main()
