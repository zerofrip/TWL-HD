# EQ-5l: clip vertex capacity 16384 and a third title (Pokemon B2 scene)

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded; game data stays local.

## Scene
FACT: a static scene of a third retail title (dump of 120 swaps, 1440..1559, identical
content; swap 1500 used): 5279 triangles from 9471 vertex commands (2305 quads split),
15837 clip vertices without culling and 15096 with culling.
FACT: both exceed the previous capacity of 8192 (EQ-4zv); the arm11 fell back to the
built-in scene (8162 non-dark px at the default options).

## Change
`arm11/source/main.c`: `CLIP_VERTEX_CAPACITY` 8192u -> 16384u (the plan arrays follow).
SOURCE-READ: the renderer allocates its draw buffers dynamically per frame
(`linearAlloc`), no fixed 8192 limit was found in `arm11/source/renderer.c`.
SOURCE-READ (`arm11/source/main.c`, `sizeof` of the types, compiled on the host): per
vertex 32 B clip vertex + 16 B texture vertex + 8 B texture coordinate = 56 B; per plan
entry 20 B plan + 4 B texture index + 4 B status = 28 B, with capacity / 3 plan entries.
INFERENCE (arithmetic): going from 8192 to 16384 adds 8192 x 56 = 458752 B for the vertex
arrays and 2731 x 28 = 76468 B for the plan arrays, 535220 B (about 0.51 MB) of static
memory in total.
FACT: `make test` exits 0; `make -C arm11` builds.

## Azahar check (default option 0 build with capacity 16384)
| SD contents | result |
|---|---|
| frame + `gx = ds_stack` (auto cull applies) | 94349 non-dark px, 0 px differ from the render of the `0x3u` build |
| frame only | 8162 non-dark px (built-in scene) |

## Console (Run 22d, same method as EQ-5f; build `twlhd-arm11-c16.3dsx`)
FACT: SD `/twlhd_frame.bin` = the swap-1500 frame (270920 B) and a new `/twlhd_opts.txt`
(`gx = ds_stack`, 14 B), both read back identical; A button; NTR-HR capture of the top
screen; afterwards the opts file was deleted and the original frame restored (1128 B,
sha256 prefix 2decd74dead1044f). No NAND, system or boot-chain access.
Mean absolute difference R/G/B (320x240):
| pair | R / G / B |
|---|---|
| console vs Azahar render | 12.1 / 9.0 / 9.9 |
| console vs DS-mode hardware (4 capture-board images) | 19.1 / 15.5 / 13.7 |
| Azahar render vs DS-mode hardware | 21.9 / 17.0 / 11.9 |
| console vs DeSmuME 3D-only shot | 18.9 / 17.1 / 14.1 |
| Azahar render vs DeSmuME 3D-only shot | 20.5 / 16.6 / 12.0 |
| DeSmuME 3D-only shot vs DS-mode hardware | 13.8 / 12.1 / 8.2 |
FACT: black pixels (all channels at most 2): console 283, Azahar 661, DeSmuME shot 1.
FACT: the console renders the scene with auto-culling and capacity 16384 without a hang.
SOURCE-READ (EQ-5g report): for the SM64DS frame the console differs from the DS-mode
hardware by 7.1 / 6.4 / 5.1.
INFERENCE: this scene's console render (19.1 / 15.5 / 13.7 against hardware) is further
from hardware than the SM64DS frame, and further than the DeSmuME 3D layer is
(13.8 / 12.1 / 8.2).
SOURCE-READ (EQ-5f report): the console/Azahar difference of the NSMB stage-select frame
is 7.5 / 5.1 / 6.4, smaller than this scene's 12.1 / 9.0 / 9.9.
UNVERIFIED: the cause of the remaining difference, of the black regions (mostly the lowest
30 rows in Azahar) and of the larger console/Azahar difference; none was analysed.
