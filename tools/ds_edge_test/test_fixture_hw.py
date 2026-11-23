#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the real-hardware capture fixture recorded by EQ-4l.

The fixture is the owner's 400x240 top-left crop of a New 3DS DS-mode run of
``ds-edge-test.nds`` (via TWiLight Menu++, passive cc3dsfs capture).  Every
literal pinned here was copied from a real run quoted in
``docs/reports/eq4l-edge-rows-hw-result.md``; nothing is copied from the task
text.

A missing fixture must FAIL, not skip: it is a committed fixture, not an
optional one.  Only the absence of PIL skips the pixel-dependent part, and the
fixture-presence assertion runs first so that a missing fixture still fails.
"""

import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "measure_rows.py")
FIXTURE = os.path.join(HERE, "fixtures", "ed1_3_top400x240.png")

if HERE not in sys.path:
    sys.path.insert(0, HERE)

from test_measure_rows import (  # noqa: E402
    HAVE_PIL, render_capture, row_offsets)

# Pinned from the quoted run in the EQ-4l report.  row_offset is the capture's
# sub-pixel top/bottom edge estimate minus the nominal DS row; the hardware
# capture sits about 0.076 DS px above nominal on every measurable item.
MEASURED_OFFSET = -0.076
OFFSET_TOLERANCE = 0.05


class FixtureCliTest(unittest.TestCase):
    """The committed hardware crop through the capture CLI."""

    def test_fixture_is_committed(self):
        # Presence first: a missing fixture FAILS, never skips.
        self.assertTrue(os.path.exists(FIXTURE),
                        "missing fixture %s; it is committed, not optional"
                        % FIXTURE)

    def test_fixture_classified_none(self):
        self.assertTrue(os.path.exists(FIXTURE),
                        "missing fixture %s; it is committed, not optional"
                        % FIXTURE)
        if not HAVE_PIL:  # pragma: no cover - depends on the host
            self.skipTest("PIL is not available; cannot read the PNG fixture")
        completed = subprocess.run(
            [sys.executable, MEASURE, FIXTURE],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        # Pinned mapping line and classification from the report's quoted run.
        self.assertIn(
            "mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0",
            completed.stdout)
        self.assertIn("classification=none", completed.stdout)
        offsets = row_offsets(completed.stdout)
        for name in ("top_band", "bottom_band"):
            self.assertIn(name, offsets, name)
            self.assertLess(abs(offsets[name] - MEASURED_OFFSET),
                            OFFSET_TOLERANCE, name)
        # Pinned literals taken verbatim from the quoted run.
        self.assertIn(
            "edge rect=top_band kind=quad side=bottom nominal=16.0000 "
            "hw=15.9241 hw-nominal=-0.0759", completed.stdout)
        self.assertIn("item=sq1 row_offset=-0.0758", completed.stdout)


class NegativeControlTest(unittest.TestCase):
    """The +1-row synthetic capture must not be classified none."""

    @unittest.skipUnless(HAVE_PIL, "PIL is not available")
    def test_plus_one_row_synthetic_is_not_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "capture.png")
            render_capture(default_shift=1).save(path)
            completed = subprocess.run(
                [sys.executable, MEASURE, path],
                capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn("classification=none", completed.stdout)
        self.assertIn("classification=uniform+1", completed.stdout)


if __name__ == "__main__":
    unittest.main()
