# EQ-5a: second title - New Super Mario Bros. stage-select screen, hardware vs DeSmuME vs arm11

Labels: FACT = measured here; SOURCE-READ = read in source; HYPOTHESIS; UNVERIFIED.
The game ROM stays local; only numbers are recorded here.

## Setup
FACT: stage-select screen of NSMB (top screen; the owner aligned hardware and DeSmuME; the
alignment is the owner's report). DeSmuME dump of 120 swaps (47620..47739, RSTA included),
hardware: 4 passive captures of the top screen (400x240, columns 40..359 used), DeSmuME
screenshots of the main screen scaled to 320x240. arm11 render of dump swap 47680 through
Azahar with the DS stack model, capacity 8192, with and without culling.
Noise floors (mean absolute difference R/G/B): hardware capture 1 vs 4: 2.3 / 1.4 / 1.3;
DeSmuME screenshot 1 vs 3: 1.5 / 0.7 / 0.7. FACT: the scene is almost static.

## Geometry
FACT: swap 47680: 2812 triangles, 4908 vertex commands, 479 quads split. Without culling
the interpreter produces 8436 clip vertices, above the arm11 capacity of 8192 (EQ-4zv);
with culling 5952.

## Results (whole 320x240 image, mean absolute difference R/G/B)
| render | vs hardware (mean of 4) | vs DeSmuME |
|---|---|---|
| no culling | 130.1 / 183.7 / 67.8 | 131.7 / 186.8 / 69.5 |
| culling | 36.0 / 40.4 / 24.9 | 35.9 / 40.4 / 25.2 |

FACT: hardware vs DeSmuME: 16.7 / 14.7 / 10.6 on the same image.
FACT: the no-culling frame needs 8436 clip vertices, above the capacity of 8192.
HYPOTHESIS: the large difference of the no-culling row is the fallback to the built-in
scene (the render content was not inspected).
FACT: culling changes 65846 px against the no-culling render.
Inference (from 5952 < 8192 < 8436): culling is what makes this frame fit at the current
capacity.

## Pixels covered by our 3D output
Mask = pixels where our culled render is brighter than 8 (65133 px of 76800); interior
mask (all 8 neighbours covered): 60335 px.
| mask | ours vs hardware | ours vs DeSmuME | hardware vs DeSmuME |
|---|---|---|---|
| covered | 18.1 / 14.1 / 14.0 | 18.0 / 13.9 / 14.0 | 16.1 / 14.6 / 10.1 |
| interior | 17.2 / 13.3 / 13.1 | 16.9 / 13.1 / 13.0 | 15.0 / 14.1 / 9.5 |

FACT, per channel, covered mask: R 18.1 (ours) vs 16.1 (DeSmuME) and G 14.1 vs 14.6 are
comparable; B is clearly worse, 14.0 vs 10.1 (hardware noise floor about 1.3). Interior
mask: R 17.2 vs 15.0, G 13.3 vs 14.1, B 13.1 vs 9.5.
HYPOTHESIS: the common difference comes from the 2D layers and their blending with the 3D
layer (not modelled by our render); not checked.
UNVERIFIED: the mask is a brightness threshold taken from our own render, not the true 3D
coverage (a DeSmuME 3D-only framebuffer was not used), so the comparison may be biased
toward our render.

## Conclusion
- FACT: on this second title the culled arm11 render matches hardware in R and G about as
  well as DeSmuME does on the covered pixels; B differs more (14.0 vs 10.1). Caveat: the
  mask is a brightness threshold taken from our own render, not the true 3D coverage.
- Inference: culling (or a clip capacity of at least 8436 vertices) is required for this
  frame; culling stays opt-in.
- FACT (earlier dumps of this session, same game): the pause/minigame screens had 0 and 468
  triangles in a corner of the screen respectively, so they were not compared.
