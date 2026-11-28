# EQ-4zx: render-state registers (RSTA) in DeSmuME dumps

Labels: FACT = measured here; SOURCE-READ = read in source; HYPOTHESIS; UNVERIFIED.

## Problem
FACT (EQ-4zs/4zu): the DeSmuME dumper wrote no RSTA chunk, so clear colour, fog, edge
colours and the toon table were unknown to the interpreter and the arm11 renderer (which
clears to a fixed colour).

## Change (out-of-tree DeSmuME patch, not part of this repository)
FACT (patch as written, dump checked): the dumper writes one RSTA entry per register at
gx_index 0, 78 entries per frame (counted in the dumped files). SOURCE-READ (patch): the
values come from DeSmuME's pending 3D state at the swap. Registers: DISP3DCNT (0x04000060, u16), edge colours
(0x04000330..0x0400033E, 8 x u16), alpha-test reference (0x04000340), CLEAR_COLOR
(0x04000350, u32), CLEAR_DEPTH (0x04000354, u16, recovered as the 24-bit value >> 9),
FOG_COLOR (0x04000358), FOG_OFFSET (0x0400035C), fog table (0x04000360, 32 x u8), toon
table (0x04000380, 32 x u16).
FACT: the parser (`twlhd_v2_parse`, which checks the whitelist) accepted the dumped files,
so all addresses are inside the ranges of `twlhd_v2_rsta_whitelisted`.
SOURCE-READ: DeSmuME keeps these values in `gfx3d.pendingState`; CLEAR_DEPTH is stored
extended to 24 bits (`dsDepthExtend_15bit_to_24bit`).

## Check
- FACT: a headless dump of three consecutive swaps (3000..3002) of a retail game wrote
  78 RSTA entries per frame; `gx_interp_cli --ds-stack` parsed all three (result=OK).
- FACT: values in those frames: DISP3DCNT 0x1D, CLEAR_COLOR 0x3F00FFFF, CLEAR_DEPTH
  0x7FFF, FOG_COLOR 0, FOG_OFFSET 0.
- SOURCE-READ (GBATEK field layout): CLEAR_COLOR bits 0-14 colour, bit 15 fog enable,
  bits 16-20 alpha, bits 24-29 polygon id. FACT: so the measured 0x3F00FFFF means colour
  0x7FFF, fog bit 15 = 1, alpha 0, polygon id 0x3F.
- HYPOTHESIS: with a clear alpha of 0 the background seen on hardware comes from the 2D
  layers, not from the 3D clear colour. UNVERIFIED for the SM64DS scene of EQ-4zs/4zu
  (needs a new dump of that scene).
- UNVERIFIED: the arm11 renderer and the interpreter do not read RSTA yet; clear colour
  stays fixed at the built-in value.

## Next
Dump the settled SM64DS scene again (needs the owner to reach the scene), read its
CLEAR_COLOR / DISP3DCNT, and decide whether the renderer should honour the clear alpha.
