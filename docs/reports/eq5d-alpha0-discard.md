# EQ-5d: discard alpha-0 fragments even when alpha blending is on

Labels: FACT = measured here; SOURCE-READ = read in source; INFERENCE = derived from FACT
items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Root cause of the top strip of EQ-5b/5c
FACT (interpreter and texture decoder on the dumped frame, swap 5320 of the NSMB
stage-select scene): at pixel (280, 2) of the 320x240 image the two triangles that cover
it are triangle 0 (z/w 0.996) and triangle 8 (z/w 0.9959), both texture format 5. The
decoded texel of triangle 0 there is (0, 0, 0, 0) (alpha 0); the texel of triangle 8 is
(49, 107, 165, 255), which is within 2 per channel of the DeSmuME 3D-only shot at that
pixel (49, 105, 166). Triangle 0 comes before triangle 8 in the draw order (indices 0 and 8).
Our render shows black there.
SOURCE-READ (`arm11/source/renderer.c`, clip path): the alpha test (discard alpha 0) was
enabled only when DISP3DCNT alpha blending was off; the scene has blending on (DISP3DCNT
0x19, bit 3), so alpha-0 fragments were drawn.
INFERENCE: an alpha-0 fragment of triangle 0 is drawn black with depth write enabled and
the nearer triangle 8 does not replace it; this fits the black top strip (4054 px) and
EQ-5c's observation that the black part did not depend on triangle size.
UNVERIFIED: why the depth test rejects triangle 8 (z/w differs from triangle 0 by about
0.0001); the depth compare function and precision of the clip path were not analysed.
FACT: the change removes the strip (4054 -> 2 black px, table below).

## Change
`arm11/source/renderer.c`: the "alpha test greater than 0" setting of the clip path is now
applied regardless of the blend gate. `tests/host/test_blend_config.c`: the call-site check
that required the `!g_blend_enabled` gate now requires its absence and requires exactly
one unconditional `at = twlhd_alpha_test_gt_zero();` in the clip path (no test removed or
weakened).
SOURCE-READ (`docs/reports/eq4y-*.md`, EQ-4x/4y): the blend gate itself stays; only the
alpha-0 discard is decoupled from it.

## Measurements (Azahar renders of dumped frames, culling on, DS stack model)
| scene | before | after |
|---|---|---|
| NSMB stage select, vs DeSmuME 3D shot (R/G/B) | 17.4 / 19.0 / 19.4 | 13.7 / 12.7 / 10.3 |
| NSMB stage select, vs hardware capture | 22.7 / 23.9 / 25.9 | 19.8 / 18.3 / 18.3 |
| NSMB stage select, black px | 4054 | 2 |
| NSMB map of EQ-5a (swap 47680), vs hardware, 4-capture mean | 36.0 / 40.4 / 24.9 | 21.7 / 16.0 / 17.2 |
| SM64DS frame 19180 (EQ-4zu setup), vs hardware, 4-capture mean | 9.19 / 7.09 / 4.89 | 9.14 / 7.08 / 4.86 |
FACT: SM64DS frame 19180 changes in 44 px; `make test` exits 0.

## Open points
- UNVERIFIED: behaviour on the console (the renderer was run only through Azahar).
- UNVERIFIED: whether a polygon alpha of 0 in the wireframe sense (DS draws such polygons
  as outlines) is affected; the alpha-0 test applies to the fragment alpha.
- The remaining difference against the DeSmuME 3D shot (13.7 / 12.7 / 10.3) was not
  analysed.
