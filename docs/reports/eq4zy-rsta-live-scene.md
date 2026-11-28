# EQ-4zy: render-state registers of the settled SM64DS scene

Labels: FACT = measured here; SOURCE-READ = read in source/docs; HYPOTHESIS; UNVERIFIED.

## Measurement
FACT: a dump of the settled scene (camera settled after the A press, 120 swaps, dumper
with RSTA, EQ-4zx; the settling steps are the owner's report, not measured) has 78 RSTA entries per frame. In swap 5869 (one frame of that dump):
DISP3DCNT 0x0019, CLEAR_COLOR 0x3F1F0000, CLEAR_DEPTH 0x7FFF, FOG_COLOR 0x001F0000,
FOG_OFFSET 0, alpha-test reference 0, all 8 edge colours 0. All 120 frames of the dump
have the identical DISP3DCNT / CLEAR_COLOR / edge / fog-table / toon-table values (one
distinct state). The fog table has 31 non-zero entries of 32; the toon table has 6
distinct values.
SOURCE-READ (GBATEK field layout): DISP3DCNT 0x19 = texture mapping on, alpha blending on,
antialiasing on, polygon shading mode = toon (bit 1 = 0; applies only to polygons whose attribute selects shading mode), fog off (bit 7 = 0), edge marking off (bit 5 = 0),
clear image off (bit 14 = 0). CLEAR_COLOR 0x3F1F0000 = colour (0,0,0), fog bit 0,
alpha 31, polygon id 0x3F: an opaque black rear plane.

## Consequences
- SOURCE-READ: the arm11 default clear colour is opaque black 0x000000FF
  (`arm11/include/twlhd_render_config.h`, EQ-4z). FACT: that equals the dump's rear plane;
  the HYPOTHESIS of EQ-4zx (a transparent rear plane in this scene) is refuted for this
  scene: the alpha is 31, not 0.
- FACT: DISP3DCNT bit 5 (edge marking) = 0, bit 7 (fog) = 0, all 8 edge colours 0; the fog
  table has 31 non-zero entries.
- SOURCE-READ-based inference: with edge marking and fog disabled, neither can explain
  differences in this scene (fog applies only when bit 7 is set).
- HYPOTHESIS: the earlier "hardware background (8,12,8)" is the capture path (RGB565
  quantisation and colour processing of the capture board), not a 3D register.
- FACT: the toon table is in the dump (32 entries, 6 distinct values). UNVERIFIED: whether
  any polygon of this scene uses toon/highlight mode, and how the renderer treats it.
- Conclusion from the items above (inference): nothing in the dump asks for a different
  clear colour, so no code was changed.
