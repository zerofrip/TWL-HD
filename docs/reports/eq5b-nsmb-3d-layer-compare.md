# EQ-5b: NSMB stage-select screen against the DeSmuME 3D-only framebuffer

Labels: FACT = measured here; SOURCE-READ = read in source; INFERENCE = derived from FACT
items; HYPOTHESIS; UNVERIFIED.
Only numbers are recorded; the game data stays local.

## Setup
FACT: second dump of the stage-select screen (swaps 5260..5379, RSTA and 3D-only
framebuffer shots every 20 swaps from the patched DeSmuME dumper; the scene alignment of
hardware and DeSmuME is the owner's report). Compared frame: swap 5320 (about 3570 triangles per
frame; the count varies by a few: 3573 at swap 5260, 3567 at swap 5379). arm11 render
through Azahar, DS stack model, capacity 8192 unless noted.
Noise floors (R/G/B): hardware capture 1 vs 4: 7.6 / 5.6 / 4.0 (the scene is not fully
static this time); DeSmuME 3D shot 5300 vs 5320: not measured separately.

## Geometry and capacity
FACT: swap 5320: 10707 clip vertices without culling, 8046 with culling. The capacity is
8192 (EQ-4zv).
FACT: the no-culling render at capacity 8192 is the fallback (mean colour 8/10/10; mean
absolute difference to the DeSmuME 3D shot 160.6 / 190.8 / 204.5).
FACT: with a temporary capacity of 12288 (not committed) the no-culling render differs from
the culled one in 1 px, so culling does not change the picture here.

## Comparison (mean absolute difference R/G/B, whole 320x240 image)
| pair | R / G / B |
|---|---|
| arm11 (culled) vs DeSmuME 3D-only shot (swap 5320) | 17.4 / 19.0 / 19.4 |
| arm11 (culled) vs hardware capture | 22.7 / 23.9 / 25.9 |
| DeSmuME 3D shot vs hardware capture | 22.4 / 20.0 / 20.3 |
| DeSmuME 3D shot vs DeSmuME final screenshot | 7.8 / 6.7 / 9.9 |
| DeSmuME final screenshot vs hardware | 17.1 / 15.7 / 12.2 |
FACT: the arm11 3D layer differs from the DeSmuME 3D layer by about 18 and from the
hardware final image by about 24; the DeSmuME 3D layer itself differs from hardware by
about 21.
HYPOTHESIS: the 2D layers and their composition account for a large part of the
hardware differences (the DeSmuME final screenshot still differs from hardware by about 15,
so capture and alignment differences play a part too).
FACT: against the DeSmuME 3D shot the mean absolute difference per 30-row band is 49.5
(rows 0..29), 22.2 (30..59), then 14.1, 12.0, 12.3, 13.0, 12.0 and 13.7 for the six bands
from row 60 to row 239.

## The top strip
FACT: 4054 px (5.3 percent) of the arm11 frame are black (no polygon) where the DeSmuME 3D
shot has content; 3265 of them lie in rows 0..19.
FACT: the interpreter output has six very large triangles (indices 0, 1, 6, 7, 8, 9 of
the culled output): clip z/w = 1.0, w between 348 and 381, horizontal extent up to NDC
-2.04 .. 8.12. Their footprint is 14080 px and contains 3520 of the 4054 uncovered px.
SOURCE-READ (`common/twlhd_clipfix.c`): the clip fix uses z' = (z - w) / 2, so z/w = 1.0
maps exactly to the far clip boundary.
FACT: an experiment shifting the depth by 0.00005 w toward the inside (not committed)
changed no pixel: the difference to the DeSmuME shot stayed 17.4 / 19.0 / 19.4 and the
uncovered count stayed 4054.
HYPOTHESIS: the six huge triangles are clipped or dropped (guard band / extent limits of the
PICA clipper or of the emulator); not tested.
UNVERIFIED: whether the same happens on hardware.

## Conclusion
- FACT: 4054 px (5.3 percent of the frame) are black where the DeSmuME 3D shot has content;
  in rows 60..239 (the lower three quarters) the band difference to the DeSmuME 3D shot is
  12-14, in rows 0..59 it is 22-50.
- INFERENCE (10707 > 8192 > 8046): culling is required at capacity 8192 for this frame.
- Next step (proposal): reproduce the six triangles in isolation to see whether large
  far-plane triangles are dropped by the renderer.
