# EQ-4zz: shadow-mode polygons explain the remaining culling difference

Labels: FACT = measured here; SOURCE-READ = read in source/docs; HYPOTHESIS; UNVERIFIED.
Scene: settled SM64DS frame 19180 (EQ-4zs/4zu setup, DS stack model, capacity 8192).

## Polygon modes in the scene
FACT: in one frame of the RSTA dump of the same kind of scene (swap 5869; 16441 GXCM
entries, 671 BEGIN groups) the POLYGON_ATTR writes are 40 with mode 0 (modulate) and 4 with
mode 3 (shadow); none with decal or toon/highlight. 651 BEGIN groups run under mode 0 and
20 under mode 3. The mode-3 writes alternate between two attributes: alpha 15, polygon id 0,
cull back-only (a shadow mask) and alpha 15, polygon id 1, cull front-only (the shadow
polygon). FACT: the DeSmuME cull log of an earlier dump of that scene lists back-only
alpha-15 polygons around the character's feet.
SOURCE-READ (`docs/DS_GX_RESEARCH.md`): the renderer does not implement DECAL,
TOONHIGHLIGHT or SHADOW; shadow-mode polygons are drawn like ordinary polygons.

## Experiment (not committed)
Host option flags in the interpreter dropped (a) shadow polygons with id 0, (b) all
shadow-mode polygons; Azahar renders of frame 19180 compared with the 4 hardware captures
(mean absolute difference R/G/B, averaged over the captures) and with one DeSmuME frame:

| variant | hw R/G/B | DeSmuME R/G/B | px changed vs baseline |
|---|---|---|---|
| baseline (no culling, shadow polygons drawn) | 9.20 / 7.03 / 4.89 | 8.03 / 6.08 / 4.69 | 0 |
| drop shadow id 0 | 9.20 / 7.03 / 4.89 | 8.03 / 6.08 / 4.69 | 0 |
| drop all shadow polygons | 9.21 / 7.06 / 4.99 | 7.95 / 6.21 / 4.83 | 967 |

FACT: culling changes 96 px with shadow polygons drawn and 6 px with all shadow polygons
dropped. So 90 of the 96 px come from shadow-mode polygons.
FACT: dropping all shadow polygons changes 967 px and is slightly worse against hardware
(G 7.03 -> 7.06, B 4.89 -> 4.99), mixed against DeSmuME. Drawing them as translucent
polygons is therefore closer to hardware than not drawing them.

## Conclusion
- FACT: the remaining culling difference of EQ-4zw/4zu is almost entirely shadow polygons.
- HYPOTHESIS: hardware draws the shadow only where the shadow mask (id 0 volume) is
  stencilled; our renderer draws the shadow polygon as a plain translucent polygon, so the
  facing of the shadow-mode polygons is the visible difference. A correct shadow model
  would need the mask/stencil behaviour; not attempted.
- UNVERIFIED: which of the 6 remaining px belong to which polygons.
- The experiment flags were not kept; no code changed. Culling stays opt-in.
