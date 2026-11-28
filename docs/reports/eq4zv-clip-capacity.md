# EQ-4zv: arm11 clip vertex capacity 6144 -> 8192

Labels: FACT = measured in this work; HYPOTHESIS = not verified.

## Problem
FACT: SM64DS frame 19180 (settled scene, DS stack model, VTX_DIFF implemented, no culling)
produces 2057 triangles = 6171 vertices (EQ-4zu). `CLIP_VERTEX_CAPACITY` was 6144, so the
arm11 falls back to the built-in scene for this frame.

## Change
`CLIP_VERTEX_CAPACITY` in `arm11/source/main.c`: 6144u -> 8192u. The plan arrays follow
(`CLIP_PLAN_CAPACITY` = capacity / 3). Extra static memory: 2048 vertices x (32 B clip vertex
+ 16 B tex vertex + texcoord entry), roughly 0.1 MB (arithmetic, not measured on hardware).

## Check
- FACT: `make test` exit 0; `make -C arm11` builds.
- FACT: Azahar render of frame 19180 with the committed capacity (option 0x2u, no culling)
  shows no difference from the temporary 8192 build used in EQ-4zu in the top-screen rows
  0..239; the only differing pixels are at rows 257..264 (bottom-screen status area).
- UNVERIFIED: behaviour on real console hardware (memory headroom); older docs in
  `docs/HARDWARE_TEST_PROCEDURE_3DS.md` still quote 6144 for historical runs.
- Frames above 8192 vertices still fall back to the built-in scene.
