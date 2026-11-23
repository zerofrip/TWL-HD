# EQ-3b — structural picture comparison: our PICA200 path vs DeSmuME

Stage EQ-3b of `docs/EQUALITY_CHECK_DESIGN.md` section 4 ("EQ-3", lines 425-451)
and section 5.1 (lines 531-599). Owner approved 2026-11-17.

`tools/eq3_compare.py` compares the **picture** our pipeline draws for the
default-cube demo frame with the picture DeSmuME's software rasteriser draws
for the same frame. It is a **structural** comparison, not a per-pixel one:
region bounding boxes, centroids and interior colours after edge erosion.

- **Everything here is EMULATOR.** The reference is the out-of-tree GPL
  harness (`$HOME/3ds/twlhd-desmume-harness`, DeSmuME `fc504ee0`); our side
  is the headless Azahar libretro host (`tools/azahar_host`, software
  renderer). Nothing is hardware-verified.
- **Licence posture:** this repository is Apache-2.0. No harness or DeSmuME
  source, code or repository content was copied here. The harness PPM is its
  program **output** (data, not code), the same posture as
  `tools/fixtures/eq2/README.md`.
- **Scope:** host files only. No console, SD-card or network action. No
  renderer, interpreter or harness change; the harness repository is read-only.

## 1. Producing our-side picture for the same frame

No Azahar cube screenshot existed in the repository. It is produced with the
already-documented 5B-2e-2 procedure (`docs/EMULATOR_VALIDATION.md`, "textured
renderer path"), applied to the default-cube frame:

```sh
make azahar-host
make -C arm11                     # needs devkitPro; writes arm11/twlhd-arm11.3dsx
mkdir -p /tmp/eq3_sd
cp tests/host/fixtures/demo_frame_desmume_v2.bin /tmp/eq3_sd/twlhd_frame.bin
AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
  tools/azahar_host/build/azahar_host \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 \
    --out /tmp/eq3_shot --sd-root /tmp/eq3_sd
```

`A` on frames 60-63 makes `arm11/source/main.c` load `sdmc:/twlhd_frame.bin`
(`main.c:579-597`); frame 199 then captures the static render. The core and
the 3dsx existed; both builds succeeded. The run was repeated and the
400x480 PPM was byte-identical (sha256
`61412c8753571357bf0a053cba7c16948525df60580b089d223649b975f89f81`), so the
committed crop is reproducible.

**Frame/angle alignment** (`docs/EQ2_COMPARE.md` section 3): libnds boot makes
the harness frame `k+2` for ROM counter `k`. The recorder serialises ROM frame
240 as recorder frame index 241, and the harness sample is harness frame 242.
Both pictures therefore show the **same cube angle**: ROM frame 240 =
recorder frame 241 = harness frame 242. The ARM11 app renders the recorded
frame statically, so any post-load shot index shows the same angle.

Our picture is 400x480 (both screens). The top screen is the upper 400x240
rows, the convention already used by `tools/check_tex_shot.py:449-453`. The
committed fixture stores that 400x240 crop (`tools/fixtures/eq3/README.md`);
the tool accepts either 400x240 or 400x480.

## 2. Mapping our 400x240 picture back onto the DS 256x192 frame

The DS 256x192 viewport is drawn into a **centred 320x240 region of the
400x240 top screen, with 40 px pillar bars** (decision documented in
`docs/PHASE5A_V2_RENDERER_DESIGN.md:48-49`). It is implemented in
`common/twlhd_clipfix.c:40`, where the x scale is
`-0.8f * hw` and `0.8 = 320/400`; `common/twlhd_clipfix.h:24-26` states the
same factor explicitly ("the 0.8 factor is 320/400, the share of the
400-pixel top-screen axis used by the pillar-boxed region"). This is the
source of the mapping, not an estimate; `tools/check_tex_shot.py`'s tested
`REGIONS` rectangles on real Azahar shots are consistent with it.

Therefore one DS pixel is **1.25 screen px** wide (`320 / 256`). The inverse
map used by the tool samples the screen at the DS cell centre:

```
sx = floor(40 + 1.25 * (dx + 0.5) + 0.5)      dx, sx in [0,255], [40,359]
sy = floor(       1.25 * (dy + 0.5) + 0.5)    dy, sy in [0,191], [0,239]
```

Pillar-bar columns (x < 40 or x >= 360) and rows outside 0..239 are never
sampled. Each DS cell is classified from that one deterministic centre pixel.
Colour is not read from one pixel: a 3x3 mean around the sampled cell centre
is used, as in `tools/check_tex_shot.py:_mean5`/`_is_base_cell`.

## 3. Colour classes and the two expansion chains

The six cube faces come from the frame's geometry dump
(`tools/fixtures/eq2/harness_cube_f240.txt`, `col-*-6bit` = 63 or 0):

| class | 8-bit |
|---|---|
| magenta | (255, 0, 255) |
| red | (255, 0, 0) |
| yellow | (255, 255, 0) |
| green | (0, 255, 0) |
| blue | (0, 0, 255) |
| cyan | (0, 255, 255) |

Every DS cell is assigned the nearest class by squared RGB distance (ties to
the earlier entry), the pattern of `tools/check_tex_shot.py:75-84`.

**Background differs by side.** The harness README states the DeSmuME buffer
background is `(0,0,0)`; the sample's histogram confirms 40897 black pixels.
Our renderer clears to `RENDER_CLEAR_COLOR 0x101018FF`
(`arm11/source/renderer.c:41`) = `(16,16,24)`. The tool therefore accepts both
`(0,0,0)` and `(16,16,24)` as the background class and compares only the face
structure; background values are not compared. (Design section 5.1 spoke of
"\(16,16,24\)" as the clear colour; the real harness output, which wins here,
is black. This is documented rather than reinterpreted.)

**Expansion asymmetry, explicit.** The two sides expand 5-bit colour
differently:

- harness: 5-bit `v5` -> DeSmuME `GFX3D_5TO6` -> 6-bit `v6` -> PPM
  `(v6<<2)|(v6>>4)`;
- ours: 5-bit `v5` -> `(v5<<3)|(v5>>2)`.

Worst case for a shared 5-bit code:

| v5 | 5TO6 | harness 8-bit | ours 8-bit | difference |
|---|---|---|---|---|
| 16 | 33 | `(33<<2)|(33>>4)` = 134 | `(16<<3)|(16>>2)` = 132 | 2 |
| 17 | 35 | `(35<<2)|(35>>4)` = 142 | `(17<<3)|(17>>2)` = 140 | 2 |

So the chains diverge by **at most 2 per channel** for any shared code. The
frame's actual vertex codes are 0 and 31, for which both chains give exactly
0 and 255. T_COL = 8 covers the worst divergence with a 6-level margin. The
measured one-LSB differences in the real pair (251 vs 255 on some DeSmuME
interior pixels) are DeSmuME's fixed-point interpolation, not a conversion
error; they are 4 levels, also inside 8.

## 4. Tolerances (derived, not guessed)

### Position — T_POS = 2 DS px

Two independent contributions, both from the resample arithmetic:

1. **Centre-sampling rounding.** The cell centre is at the continuous screen
   coordinate `40.625 + 1.25*dx`. Sampling the nearest screen pixel moves it by
   at most 0.5 screen px, which is `0.5 / 1.25 = 0.4 DS px`.
2. **Observed rasteriser/capture shift.** The hardware-observed emulator vs
   console shift is about 1 px up/left (`docs/PHASE5B_PLAN.md:896-897`;
   `docs/EQUALITY_CHECK_DESIGN.md:582-587` requires it to be tolerated, not
   measured).

Combined bound `0.4 + 1 = 1.4 DS px`, rounded up to **T_POS = 2 DS px**,
applied per axis to each bounding-box edge and to each centroid coordinate.

**Erosion margin M = 3 DS px**, greater than T_POS, so no interior pixel can
cross a region edge even at the maximum combined shift. Erosion keeps a pixel
only when its whole 7x7 square (Chebyshev radius 3) is the same class; that
excludes both face/face and face/background boundaries.

### Colour — T_COL = 8 per channel

`docs/EQUALITY_CHECK_DESIGN.md:593`: flat, alpha-31, planar, interior pixels.
The 5-bit step is `255/31 = 8.2258`, half-step 4; passing through a 6-bit
intermediate makes one more level possible, i.e. 8. The frame's closest
distinct expected pair is far above `2*8`. Interior colour is measured as the
mean of a 5x5 grid of cell centres per face, each a 3x3 mean, on cells that are
eroded on **both** sides. Each side is checked against the other **and**
against its declared class colour, so two models sharing the same wrong rule
cannot pass.

## 5. Occlusion rule and exit codes

At this angle three faces are fully occluded on **both** sides (magenta, green,
blue): a face class with no pixels on either side is reported "not visible"
and skipped, not an error. A class present on exactly one side, or differing
visible-face sets, is a mismatch.

| Code | Meaning |
|---|---|
| 0 | every compared face within T_POS and T_COL; PASS line with the worst deviation |
| 1 | structural mismatch: missing/extra face, differing visible-face set, bbox/centroid beyond T_POS, interior colour beyond T_COL, or a face eroded to nothing on one side. The worst deviation, face and field are named; exit 1 never a silent pass |
| 2 | usage/format error: unparseable P6, wrong dimensions (harness must be 256x192, ours 400x240 or 400x480), or an input with **none** of the expected face classes (nothing to anchor the comparison) |

## 6. Real run on the committed pair

Command:

```sh
python3 tools/eq3_compare.py \
  tools/fixtures/eq3/harness_cube_f240.ppm \
  tools/fixtures/eq3/ours_cube_f240_top.ppm
```

**EMULATOR real numbers** (eroded bboxes, DS pixel coordinates; bbox deviation
order is left, top, right, bottom):

| face | DeSmuME bbox | centroid | ours bbox | centroid | bbox dev | centroid dev | colour diff |
|---|---|---|---|---|---|---|---|
| red | (78,43)-(125,113) | (105.58,77.13) | (78,43)-(124,112) | (104.69,76.97) | (0,0,1,1) | (0.89,0.16) | (1,0,0) |
| yellow | (132,43)-(178,112) | (151.16,77.52) | (131,43)-(177,112) | (150.15,77.36) | (1,0,1,0) | (1.01,0.17) | (4,4,0) |
| cyan | (78,84)-(178,146) | (128.14,118.23) | (77,84)-(177,146) | (127.22,118.14) | (1,0,1,0) | (0.92,0.09) | (0,1,1) |
| magenta | not visible | — | not visible | — | — | — | — |
| green | not visible | — | not visible | — | — | — | — |
| blue | not visible | — | not visible | — | — | — | — |

Worst deviations: **bbox 1 DS px (tol 2)**, **centroid 1.01 DS px (tol 2)**,
**colour 4 per channel (tol 8)**. Result: **PASS, exit 0**. No tolerance was
loosened.

The DeSmuME side's 251-level interior pixels (yellow 251/251/0, cyan
0/254/254) are a 4-level difference from 255, inside T_COL. The fixture's
harness PPM histogram matches its README exactly (40897 black, 2455 cyan,
1796 one-LSB cyan, 1622 red, 1410 one-LSB yellow, 580 yellow, 392 one-LSB red).

## 7. Tests and mutant proof

`tools/test_eq3_compare.py` runs via the dedicated `make test-eq3` target; the
discover pass in `make test` sets `EQ3_DISCOVER_SKIP` so the suite executes
exactly once under `make test` (the skip is reported, not silent).

- committed real pair -> exit 0, PASS;
- a 400x480 input's upper 400x240 rows -> accepted;
- a face shifted by 1 DS px -> exit 0 (pins the tolerance from the inside);
- a face shifted by `TOL_POS + 2` -> exit 1 naming `bbox-left` and the face;
- a face interior recoloured (200,0,0) -> exit 1 on the colour field;
- a face removed from one side -> exit 1 ("missing face");
- an extra face on one side -> exit 1;
- harness 256x191, bad magic, truncated payload, an input with no face class
  -> exit 2;
- two in-process mutants: setting `TOL_POS = 10` makes the beyond case wrongly
  pass; setting `ENFORCE_COLOUR = False` makes the colour change wrongly pass.
  Each mutant test first asserts the unmodified tool returns 1.

## 8. What this does NOT prove

- **Per-pixel equality** — forbidden by the 0.8 resample and the ~1 px shift
  (design EQ-3: "the resample alone can move an edge or a colour by more than
  the 8-level tolerance"). Only structure within the stated tolerances.
- **Fog, edge/anti-alias, toon and display-capture rows** of the design's
  "known not implemented" list (section 2.3). This frame has none of them.
- **DS hardware.** Azahar's software renderer is not the PICA200
  (`docs/EMULATOR_VALIDATION.md:494`); nothing here is HARDWARE VERIFIED.
- **Blended or textured frames.** Only this flat, alpha-31, untextured scene
  is compared. The alpha-scale convention is still UNVERIFIED (section
  5.1(ii)), and textured sampling would need its own structural checker.
- **General depth sorting.** The red/yellow quads overlap in projection and
  the green/red quads share coordinates in the dump; the winner is decided by
  the depth-test convention flagged as needing re-derivation in
  `docs/PHASE5A_V2_RENDERER_DESIGN.md:55`. This one frame is consistent, but a
  wrong convention elsewhere would show as swapped/missing face colours and is
  a legitimate exit-1 finding, not something to tolerance away.
- **Quad-diagonal fill and rasteriser rounding** beyond the ~1 px structural
  shift; two independent models can share a wrong rule.
