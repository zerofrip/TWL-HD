#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/ds_scale_test/fixture_colours.py.

Runs the script on the real 2026-11-23 hardware capture
`fixtures/sc4_3_top400x240.png` and asserts the measured corner colours and
the 16-step gradient match.  Skips with a labelled reason when PIL or the
fixture is missing.
"""

import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "fixture_colours.py")
FIXTURE = os.path.join(HERE, "fixtures", "sc4_3_top400x240.png")
EXPECTED_CORNERS = {
    "corner_tl": "255,0,0",
    "corner_tr": "0,251,0",
    "corner_bl": "0,0,255",
    "corner_br": "255,251,0",
}

try:
    import PIL  # noqa: F401
    HAVE_PIL = True
except ImportError:  # pragma: no cover - depends on the host
    HAVE_PIL = False


@unittest.skipUnless(
    HAVE_PIL, "PIL is not available; fixture colour tests skipped")
class FixtureColoursTest(unittest.TestCase):
    @unittest.skipUnless(
        os.path.exists(FIXTURE),
        "fixture sc4_3_top400x240.png is missing")
    def test_real_fixture_corners_and_gradient(self):
        """Real 2026-11-23 capture: marker centres and the README §3 steps."""
        completed = subprocess.run(
            [sys.executable, SCRIPT, FIXTURE],
            capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        lines = completed.stdout.splitlines()
        corners = {}
        gradient = {}
        for line in lines:
            fields = dict(part.split("=", 1) for part in line.split())
            if line.startswith("corner_"):
                name = line.split("=", 1)[0]
                corners[name] = fields[name]
            elif line.startswith("gradient_i="):
                gradient[int(fields["gradient_i"])] = fields
        self.assertEqual(len(gradient), 16)
        for name, want in EXPECTED_CORNERS.items():
            self.assertEqual(corners[name], want, name)
        for i in range(16):
            self.assertEqual(gradient[i]["match"], "yes", i)
        self.assertIn("gradient_match_count=16/16", lines)


if __name__ == "__main__":
    unittest.main()
