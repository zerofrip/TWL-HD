#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""ds-edge-record <-> Makefile <-> plain ds-edge-test consistency tests.

The recorder variant (``record/main.cpp``, built by ``make record``) reuses the
plain ROM's geometry from ``quads.h`` and links ``common/twlhd_gx_record.c``.
These tests pin that wiring without building the ROM (no devkitARM needed):
the plain ROM must stay recorder-free, the variant must include the shim before
``<nds.h>`` and dump exactly the arm9v2 file. The last check is a negative
control: a source with one required token removed must be reported as missing.
"""

import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PLAIN_C = os.path.join(HERE, "source", "main.c")
RECORD_CPP = os.path.join(HERE, "record", "main.cpp")
MAKEFILE = os.path.join(HERE, "Makefile")

# Calls and paths the recorder frame needs; arm9v2/source/main.cpp:90-170,240-268.
RECORDER_TOKENS = (
    "twlhd_gx_rec_init",
    "twlhd_gx_rec_arm",
    "twlhd_gx_rec_serialize",
    "fopen(\"/twlhd_frame_v2.bin\", \"wb\")",
    "fatInitDefault",
    "glFlush(0)",
)
# Plain-ROM tokens that must NOT appear in source/main.c (existing scope).
PLAIN_FORBIDDEN = ("twlhd_", "twlhd_frame_v2.bin", "fatInitDefault")


def missing_tokens(source, tokens):
    """Tokens not present in source; the negative control feeds a mutant here."""
    return [token for token in tokens if token not in source]


class RecordVariantTest(unittest.TestCase):
    def read(self, path):
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read()

    def test_record_has_every_recorder_call(self):
        self.assertEqual(missing_tokens(self.read(RECORD_CPP),
                                        RECORDER_TOKENS), [])

    def test_record_includes_shim_before_nds_h(self):
        source = self.read(RECORD_CPP)
        shim = source.index('#include "twlhd_gx_shim.hpp"')
        nds = source.index("#include <nds.h>")
        self.assertLess(shim, nds, "the shim must be parsed before <nds.h>")

    def test_record_uses_the_same_geometry(self):
        source = self.read(RECORD_CPP)
        for token in ("quads.h", "EDGE_BACKDROPS", "EDGE_QUADS",
                      "glOrthof32(0, 256, 192, 0, -1 << 12, 1 << 12)",
                      "glViewport(0, 0, 255, 191)"):
            self.assertIn(token, source, token)

    def test_plain_rom_stays_recorder_free(self):
        source = self.read(PLAIN_C)
        for token in PLAIN_FORBIDDEN:
            self.assertNotIn(token, source, token)

    def test_makefile_record_target_links_common(self):
        source = self.read(MAKEFILE)
        self.assertIn("RECORD", source)
        self.assertIn("ds-edge-record", source)
        self.assertIn("../../common", source)
        self.assertIn("../../arm9v2/include", source)
        self.assertIn("-lnds9 -lfat", source)

    def test_negative_control_missing_token_is_detected(self):
        source = self.read(RECORD_CPP)
        mutant = source.replace("twlhd_gx_rec_arm(rec);", "", 1)
        self.assertEqual(missing_tokens(source, RECORDER_TOKENS), [])
        self.assertIn("twlhd_gx_rec_arm", missing_tokens(mutant,
                                                        RECORDER_TOKENS))


if __name__ == "__main__":
    unittest.main()
