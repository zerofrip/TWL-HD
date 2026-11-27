#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the v2 frame of the EQ-4zn culling probe.

``twlhd_frame_v2_cull.bin`` holds the eight white quads of the CULL=1 ROM
(tools/hw/gen_gradcull_rom_data.py: two windings crossed with POLYGON_ATTR
bits 6-7 = 1, 2, 3, 0), as quad-list polygons, so the host interpreter can be
compared with the hardware capture of the same scene.

Stdlib only.  Nothing here contacts a console or a network address.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(HERE)
for path in (TOOLS_DIR, HERE):
    if path not in sys.path:
        sys.path.insert(0, path)

from test_twlhd_v2 import build, entries  # noqa: E402
import gen_gradcull_rom_data as rom  # noqa: E402
import gen_lit_frames as lit  # noqa: E402

FRAME_NAME = "twlhd_frame_v2_cull.bin"
WHITE_555 = 0x7FFF


def cull_entries():
    out = []
    for index, (_winding, bits) in enumerate(rom.CULL_CASES):
        out.append((lit.CMD_POLYGON_ATTR, (31 << 16) | (bits << 6)))
        out.append((lit.CMD_COLOR, WHITE_555))
        out.append((lit.CMD_BEGIN_VTXS, 1))     # quad list
        for x, y in rom.cull_corners(index):
            out.extend(lit._vtx16(x, y))
        out.append((lit.CMD_END_VTXS, 0))
    return out


def frame_bytes():
    return build(entries(*cull_entries()))


def main(argv=None):
    parser = argparse.ArgumentParser(prog="gen_cull_frames.py",
                                     description=__doc__.split("\n")[0])
    parser.add_argument("outdir")
    args = parser.parse_args(argv)
    os.makedirs(args.outdir, exist_ok=True)
    path = os.path.join(args.outdir, FRAME_NAME)
    with open(path, "wb") as handle:
        handle.write(frame_bytes())
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
