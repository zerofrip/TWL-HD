# EQ-5c: the six large far-plane triangles of the NSMB stage-select frame

Labels: FACT = measured here; SOURCE-READ = read in source; INFERENCE = derived from FACT
items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Question (from EQ-5b)
EQ-5b found 4054 black px at the top of the arm11 render of swap 5320 and 3520 of them inside
the footprint of six huge triangles (indices 0, 1, 6, 7, 8, 9 of the culled interpreter
output). HYPOTHESIS of EQ-5b: the renderer or emulator drops very large triangles.

## Experiment (not committed)
A debug build kept only the first 30 vertices (triangles 0..9) and optionally multiplied
the clip x and y of every kept vertex by 0.25 (w unchanged), then rendered through Azahar.
| xy scale | footprint of the 6 triangles (px) | non-black px inside it | non-black px total |
|---|---|---|---|
| 100 percent | 14080 | 10189 (72 percent) | 26509 |
| 25 percent | 5854 | 4076 (70 percent) | 25258 |
FACT: the fraction of the footprint that is non-black is about the same (72 versus 70
percent) when the triangles are four times smaller on screen; the non-black count may
include pixels drawn by triangles 2-5, which were kept in the same experiment and not
separated.
INFERENCE: at 100 and 25 percent scale the black fraction is about the same, so in this
range it does not depend on the triangle size.
INFERENCE: the large-triangle hypothesis of EQ-5b is not supported (not proven wrong:
the footprint of tris 2-5, kept in the same experiment, was not separated).

## What the six triangles carry
FACT (interpreter `--tex` output): the six triangles use TEXIMAGE_PARAM 0x56630628
(format 5, 4x4 compressed, 512x128 texels; triangles 0, 1) and 0x56430e28 (format 5,
128x128; triangles 6-9), with texture coordinates far outside the texture (s from -1024 to
475.9 texels).
FACT: texture format (TEXIMAGE_PARAM bits 26-28) counted per vertex over the whole frame:
format 5: 4818, 6: 621, 1: 729, 4: 960, 3: 906, 2: 12 vertices.
HYPOTHESIS: the missing content comes from the texture of these triangles (compressed
format 5 decode or the wrap of very large coordinates in the renderer), not from the clip.
UNVERIFIED: the per-entry texture status of this frame (the renderer's status line was not
read).

## Conclusion
- FACT: scaling the triangles to 25 percent changed the non-black fraction of the footprint
  by only 2 points (72 to 70 percent); triangles 2-5 were not separated.
- HYPOTHESIS (replaces EQ-5b's): texture handling of the far-plane triangles.
- Next step (proposal): read the per-entry texture status for this frame, and render the six
  triangles with a debug flat colour to see which part is texture-dependent.
- No code changed.
