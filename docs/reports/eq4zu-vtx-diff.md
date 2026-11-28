# EQ-4zu: VTX_DIFF in the host interpreter, and what it did to the culling mismatch

Labels: FACT (measured here), SOURCE-READ, HYPOTHESIS, UNVERIFIED. Only
numbers are recorded; no game data, frame dump or screenshot is part of the
repository. Continues `eq4zt-retail-culling-compare.md`.

## 1. Finding

- FACT: the culling mismatch of EQ-4zt had one root cause outside the culling
  rule: the interpreter did not implement `VTX_DIFF` (0x28), it counted the
  command as `unsupported` and dropped the vertex. The retail scene uses it
  141 times per frame (`unsupported=141`, `geometry_incomplete=141`).
- FACT: the dropped vertex was the first vertex of a 4-vertex triangle strip.
  Without it the strip started one vertex late, so the first triangle of the
  strip was assembled as `(v1, v2, v3)` with the even-triangle winding instead
  of as the second triangle `(v2, v1, v3)`; its facing came out inverted and
  the culling rule removed a triangle that DeSmuME and the hardware draw.
- FACT (DeSmuME log of the same scene, per-polygon): the strip is polygons
  `(v0, v1, v2)` and `(v2, v1, v3)`, mode 2 (front only), both drawn; the
  first vertex lies at screen (26.48, 30.28), w 97241 (20.12 fixed).
- FACT: with `VTX_DIFF` implemented the interpreter produces that vertex at
  NDC (-0.7931, 0.6846), w 23.741 (= 97241 / 4096), i.e. screen (26.5, 30.3).

## 2. Change

`common/twlhd_gx_interp.c`: `VTX_DIFF` adds the three sign-extended 10-bit
fields of its parameter, as raw integer units, to the current s16 vertex
position (each component wraps as an s16) and emits a vertex. SOURCE-READ:
DeSmuME `gfx3d.cpp:1503-1519` (`SetCurrentVertexPosition3s10Relative`,
`SetCurrentVertexPositionRelative`). `stats.unsupported` and
`stats.geometry_incomplete` stay in the struct and are now always 0.
Unit tests (`tests/host/test_gx_interp.c`): the former test that expected
`VTX_DIFF` to be unsupported now expects a vertex (4 vertices in, one triangle,
one leftover); four new tests cover the addition to the previous vertex, the
10-bit sign extension (0x3FF = -1, 0x200 = -512, 0x1FF = +511), the s16 wrap and
the base after a `VTX_XY`.

## 3. Effect on the retail scene (frame 19180, same capture as EQ-4zs/zt)

| render | mean abs diff to hardware R/G/B (4 shots) | pixels changed by culling |
|---|---|---|
| before, no culling | 9.30 / 7.27 / 4.95 | - |
| before, culling | 9.32 / 7.59 / 4.95 | 467 |
| VTX_DIFF, no culling | 9.20 / 7.03 / 4.89 | - |
| VTX_DIFF, culling | 9.19 / 7.09 / 4.89 | 96 |

- FACT: implementing `VTX_DIFF` lowers the difference to hardware in every
  channel; culling now changes 96 pixels (was 467) and the culled render is
  within 0.06 of the unculled one in G.
- FACT: the 8192-vertex check: without culling the frame produces 2057
  triangles = 6171 vertices, more than the 6144 clip-vertex capacity of
  `arm11/source/main.c`; the arm11 run then falls back to its built-in scene.
  The no-culling row above was rendered with a temporary capacity of 8192
  (not committed); with culling the frame needs 3927 vertices and fits.
- UNVERIFIED: the remaining 96 pixels (earlier bisection pointed at two
  back-only quad-strip decisions); other scenes, other VTX_DIFF uses.
- FACT: culling is still opt-in; this report does not enable it.
