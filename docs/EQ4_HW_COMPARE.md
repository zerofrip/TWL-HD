# EQ-4 hardware capture comparison (cc3dsfs DS capture vs DS reference)

`tools/eq4_hw_compare.py`, owner-approved 2026-11-23. It compares a real
top-screen capture from the cc3dsfs capture board (DS mode) with a
DS-resolution reference picture. The real hardware run is a **later owner
step** and is **UNVERIFIED** here; this document and the tool are BUILD/UNIT
work only.

Every report carries the line `basis=one console, one capture`. The tool never
claims hardware equality: a PASS means the captured picture is consistent with
the reference under the stated tolerances for one console and one capture,
nothing more.

## 1. Inputs

| Option | Meaning |
|---|---|
| `--capture <png>` | cc3dsfs top-screen capture PNG: a 400x240 crop, or a full 1280x720 root screenshot whose top-left 400x240 region is used (the `measure_scale.py` convention). Requires PIL. |
| `--reference <ppm>` | DS-resolution P6 PPM: 256x192 raw 3D framebuffer from the harness, or a 400x240 / 400x480 "ours" PPM sampled onto the DS grid with the `eq3_compare` centre rule. |
| `--mapping <json>` | Shared capture mapping (default `tools/ds_vtxcolor_test/capture_mapping.json`). Missing or invalid => exit 2. |

The geometry (scale 1.25, offset 40) is read from that JSON and is **not
duplicated** in the tool. The JSON's `source` field records the hardware
report it was measured in (`docs/reports/eq4d-ds-vertex-colour-hw.md`).

## 2. Both mapping directions

* **capture -> DS** (primary, gated): crop x 40..359 of the 400x240 region,
  then an area (box) filter downsamples 320x240 to 256x192. Each DS pixel
  averages the capture pixels it overlaps with fractional weights, so the
  1.25 ratio is not rounded to nearest.
* **reference -> capture** (reported): the 256x192 reference is upsampled
  bilinearly by 1.25 to 320x240 and placed at (40,0) on a 400x240 canvas, then
  compared with the same region of the capture.

Both directions print `mean_abs_r/g/b`, `max_error`, `frac_within`,
`frac_within_flat`, `frac_within_edge`, `flat_pixels` and `edge_pixels` under
the `ds.` and `capture.` prefixes.

## 3. Tolerances

| Value | Source |
|---|---|
| Flat pixel tolerance `T = 8` per channel | `eq3_compare.TOL_COLOUR`, docs/EQUALITY_CHECK_DESIGN.md section 5.1(i) |
| Blended/edge pixel tolerance `T = 16` | docs/EQUALITY_CHECK_DESIGN.md section 5.1 ("blended pixels ... 16 until the hardware scale is settled") |
| Position tolerance `T_POS = 2 DS px` | `eq3_compare.TOL_POS`, section 5.1(vi) |
| Extra `+1 DS px` resampling allowance => 3 DS px total | **HYPOTHESIS**, labelled: the capture's bilinear-like 1.25x resampling was read from a 1-px strip profile in `tools/ds_scale_test/README.md` section 6 (one console, one capture), not measured as a shift distribution |

Edge/blended pixels are the boundary pixels of a colour class (a 4-neighbour
has a different class) or pixels that do not classify. The per-pixel gate is
applied to the **flat** pixels (`frac_within_flat >= 0.95`); at a hard edge the
1.25x resampling legitimately mixes two cells, so edges are covered by the
bounding-box and centroid checks instead. The edge fraction under `T = 16` is
still reported, never hidden.

## 4. Report keys

Stable `key=value` lines, one per key:

```
basis=one console, one capture
tolerance_flat, tolerance_blended_edge, tolerance_pos_ds
mapping_scale_x, mapping_scale_y, mapping_offset_x, mapping_offset_y
ds.mean_abs_r, ds.mean_abs_g, ds.mean_abs_b, ds.max_error
ds.frac_within, ds.frac_within_flat, ds.frac_within_edge
ds.flat_pixels, ds.edge_pixels
capture.<same metrics>
ref_nonbg_pixels, ref_bbox_left, ref_bbox_top, ref_bbox_right, ref_bbox_bottom
capture_nonbg_pixels, capture_bbox_left, capture_bbox_top,
capture_bbox_right, capture_bbox_bottom
bbox_dev_left, bbox_dev_top, bbox_dev_right, bbox_dev_bottom
class_<white|red|green|blue|yellow>_<ref|capture>_area
class_<...>_<ref|capture>_centroid_x
class_<...>_<ref|capture>_centroid_y
verdict=PASS|FAIL|NOT-COMPARABLE
reason=<semicolon-separated reasons, or "within tolerance">
```

The five colour classes are scene 5's lit cells (the 8x8 I4 palette in
`arm9v2/source/demo_frame.cpp:155-160` yields black/yellow/blue/green/red/white).
Background is black; a pixel farther than 96 from every class is `other` and
counts as an edge pixel.

## 5. Verdict and exit codes

* `PASS` (exit 0): capture and reference have a non-background region, every
  bounding-box edge and every reference colour class (area >= 64 DS px) is
  within 3 DS px, and `ds.frac_within_flat >= 0.95`.
* `FAIL` (exit 1): geometry is present but a position, class-presence,
  class-centroid or flat-pixel check fails. A colour class present in the
  reference and missing from the capture is a colour-path failure.
* `NOT-COMPARABLE` (exit 0): the capture or reference has no non-background
  region (empty/blank capture, missing geometry), so there is nothing to
  compare.
* Exit 2: usage, unreadable input, missing/invalid mapping, or PIL absent.

## 6. Tests

`tools/test_eq4_hw_compare.py`, target `make test-eq4-hw-compare`, and the
aggregate `make test` (the discover pass sets
`EQ4_HW_COMPARE_DISCOVER_SKIP` so it runs once). Synthetic PIL data; skips
with a labelled reason when PIL is absent:

| Case | Expected |
|---|---|
| Reference upsampled by the documented 1.25x bilinear method | PASS |
| Capture shifted by 1 DS px | PASS (<= 2 px) |
| Capture shifted by 4 DS px | FAIL |
| One colour class recoloured (red -> green) | FAIL |
| Empty/black capture | NOT-COMPARABLE |
| Missing mapping file | exit 2 |
| Real rotating scene5 fixture vs a static reference | FAIL or NOT-COMPARABLE, never PASS |

The real fixture is `tools/ds_scale_test/fixtures/s5b_7_top400x240.png`; if it
is ever removed the test skips with a labelled reason.

## 7. Owner hardware procedure (PLAN, HYPOTHESIS, not run here)

All steps below are **HYPOTHESIS** until the owner records a capture; nothing
in this document is a hardware measurement.

1. Build the plain static ROM: `make -C arm9v2 SCENE=5 STATIC_ANGLE=<A>`
   (do not commit the `.nds`). The scratch reference PPM is the harness output
   for the same angle; see `docs/EQ_SCENES.md` section 6.
2. Place the `.nds` on the SD card `/roms/nds/` with the owner's existing
   FTP/SD flow (the flow used for the scale test, `tools/ds_scale_test/README.md`
   section 8, and `docs/reports/eq4b-cc3dsfs-ds-capture.md:67-68`).
3. Start the ROM from TWiLight Menu++ in **DS mode**, let the static frame
   display, and capture with the cc3dsfs board. The committed
   `tools/cc3dsfs_capture/cc_shots.sh <label> <n> <gap>` hard-codes
   `cc3dsfs --auto_connect --no_audio --no_auto_save` and does **not** pass the
   three capture-selection flags; the 2026-11-23 captures used an equivalent
   wrapper that adds `--enabled_both 0 --enabled_top 1 --scaling_top 1`
   (`docs/reports/eq4d-ds-vertex-colour-hw.md` section 2). The owner supplies
   those flags via the cc3dsfs configuration or the same wrapper: UNVERIFIED.
4. Decode with `python3 tools/cc3dsfs_capture/xwd2png.py ...` and crop the top
   400x240 region (the `measure_scale.py`/`eq4b` convention).
5. Run
   `python3 tools/eq4_hw_compare.py --capture <top400x240.png> --reference <harness.ppm>`
   and record the `key=value` output.

What each outcome would mean (all HYPOTHESIS, one console / one capture):

* **PASS**: the capture is consistent with the emulator reference under the
  section 3 tolerances. It supports the emulator texture/geometry model; it is
  not a proof of universal hardware equality.
* **Position-only failures** (bbox/centroid outside 3 DS px): the measured
  mapping/offset or the `+1 DS px` resampling allowance is wrong for this
  capture, or the ROM/capture frame is not the static one; no colour claim.
* **Colour-only failures** (class presence/interior colour): a texture or
  initial-vertex-colour path difference between the DS and the emulator model.
  EQ-4d measured hardware initial vertex colour = white on one console, so a
  colour-only failure is a new observation, not an expected result.
* **NOT-COMPARABLE**: the capture is blank, the ROM was not displaying the
  static frame, or the capture/geometry is unusable. Fix the capture; do not
  read it as a pass or a fail of the picture model.

The real hardware comparison is **NOT done in this task** (UNVERIFIED). No
existing rotating-capture result is overridden by this plan.

## 8. REQ traceability (user task requirement 3)

| Requirement | Where |
|---|---|
| (a) capture->DS area downsample and reference->capture bilinear upsample, both reported; geometry read from the shared JSON | `eq4_hw_compare.py` `capture_to_ds`, `reference_to_capture`, `load_mapping`; section 2 |
| (b) EQ-3 design tolerances (8 flat, 16 blended/edge), labelled extra resampling position allowance, per-metric key=value lines, verdict + reasons, "one console, one capture" | `compare`, `compare_pixels`, section 3-5 |
| (c) synthetic identity/shift/colour/empty tests and the real rotating-fixture regression | `tools/test_eq4_hw_compare.py`, section 6 |
| Plan block for the owner's hardware procedure and outcome meanings | section 7 |
| Hardware comparison not done, no equality claim | sections 1, 7 |

## 9. Real hardware run (2026-11-23)

Sections 1, 7 and the section 8 row "Hardware comparison not done" above are
kept as historical text; this section records the first real run and supersedes
them. Every item of the section 7 plan that this run needed is now done: the
plain `SCENE=5 STATIC_ANGLE=<0|30>` ROMs were built (section 7 item
1), uploaded to `/roms/nds/` (item 2, OWNER-REPORTED), started from TWiLight
Menu++ in DS mode, and captured passively with the cc3dsfs board using
`--enabled_both 0 --enabled_top 1 --scaling_top 1` (item 3, OWNER-REPORTED);
the captures were decoded with the fixed 24-bpp decoder and cropped to the top
400x240 region (item 4); and
`python3 tools/eq4_hw_compare.py --capture <png> --reference <ppm>` was run
against both the DeSmuME harness reference and the PICA render (item 5, items
quoted with complete raw output in the report).

Results, each `basis=one console, one capture`:

| Pair | Verdict | ds.frac_within_flat | bbox_dev l/t/r/b |
|---|---|---|---|
| st0 hardware vs harness | PASS | 0.9997 | 2/2/1/1 |
| st30 hardware vs harness | PASS | 0.9998 | 1/2/1/1 |
| st0 hardware vs PICA render | PASS | 0.9999 | 1/2/0/1 |
| st30 hardware vs PICA render | PASS | 0.9986 | 0/2/0/1 |
| st0 hardware vs st30 harness (control) | FAIL | 0.8611 | 41/9/13/1 |
| st30 hardware vs st0 harness (control) | FAIL | 0.8546 | 44/13/15/3 |

Tests: `tools/test_eq4_hw_compare.py` (`RealScene5StaticCompareTest`) asserts
these four PASSes and both non-PASS controls; fixtures live in
`tools/fixtures/eq4_hw/`. Limits: one console, one capture per angle, one ROM
per angle; not bit-exactness, not other scenes/consoles, and the alpha
convention (`a/31` vs `(a+1)/32`) stays UNVERIFIED (all alpha 31). Report:
`docs/reports/eq4f-scene5-static-hw-compare.md`.

## 10. Capture-offset fit (2026-11-23)

Append-only pointer. `docs/reports/eq4g-capture-offset.md` fits the capture
mapping from the ds-scale-test pattern with sub-pixel 50 %-midpoint edges and
finds `a_x=1.250383, b_x=39.933172` (RMS 0.072 capture px),
`a_y=1.250790, b_y=-0.168453` (RMS 0.026 capture px) on
`tools/ds_scale_test/fixtures/sc4_3_top400x240.png`. That is at most 0.135 DS px
from the default mapping `(1.25, 40, 1.25, 0)`, so the ~1-2 DS px scene5 offset
recorded in section 9 is not explained by the mapping: re-running the section 9
pairs with the fitted mapping via `tools/eq4g_fit_compare.py` leaves, against
the PICA render (`ours`), `dy` negative in all 20 quoted class pairs (-0.3001 to
-2.5699 DS px) and `dx` mixed (-1.7386 to +0.9474 DS px), with
`bbox_dev_top=2` in all four pairs and `bbox_dev_left` 0-1. The
DeSmuME-harness arm of that cross-check is UNVERIFIED (its commands and output
are not quoted in the report). The default mapping,
the tolerances and this tool are deliberately unchanged; the report recommends
no mapping change.

## 11. Polygon-alpha convention test (2026-11-23)

Append-only pointer. `docs/reports/eq4h-alpha-convention-test.md` builds
`tools/ds_alpha_test/`: a static DS ROM with opaque black/grey backdrop halves
and 14 translucent white quads (polygon alpha 4..28, distinct polygon ids), a
shared `quads.json`, `expected.py` (A1 = a/31 vs A2 = (a+1)/32, both the
5-bit-then-expand and the expand-then-8-bit families, round and floor), and a
deterministic `measure_alpha.py` (400×240 capture or `--native`, mean in a
4-DS-px margin, per-quad and overall A1/A2/UNDECIDED, NOT-COMPARABLE when the
geometry is not located). SOURCE-READ: DeSmuME's rasteriser uses `alpha = a+1`,
`invAlpha = 32-alpha`, `>>5` (`rasterize.cpp:490-494`), i.e. A2; our v2/GX
interp path also uses `(a+1)/32` (`common/twlhd_gx_interp.c:395-397`, `:1074`)
while the v1 frame path forces vertex alpha 1.0
(`common/twlhd_vertex_convert.c:44`) and implements no polygon alpha. The
DeSmuME render arm is UNVERIFIED (no `desmume-cli` on the host; the repo's
`desmume_capture.sh` needs an arm9v2 autodump file this ROM never writes). The
real-hardware verdict is **NOT YET MEASURED**; the owner procedure is in the
report.

**Update (2026-11-23):** a built `desmume-cli` and the out-of-tree harness exist, but `ds-alpha-test.nds` faults in DeSmuME (`ARM OP_BKPT`) in its `iprintf` console path before the 3D frame.
A control with that call removed shows the translucent quads are depth-rejected (quad z `0x41000000` vs backdrop `0x40000000`); the render arm stays UNVERIFIED.
Full commands: `docs/reports/eq4h-alpha-convention-test.md` §5.

**Update (2026-11-23):** both ROM-side blockers are fixed; built unmodified from the repo, `ds-alpha-test.nds` now renders in DeSmuME (`harness_exit=0`, frame 1 captured, 16 polygons / 64 vertices) with the quads at z=+64 in front of the backdrops (post-transform z `0x3f000000` vs `0x40000000`).
`measure_alpha.py --native` gives overall verdict **A1** (`5bit_round` decides A1, `8bit_round` UNDECIDED; floor variants A2 sensitivity only) — EMULATOR (DeSmuME) only, real hardware **NOT YET MEASURED**.
Full commands and complete output: report §5.

**Update (2026-11-23):** the repair-2 "overall **A1**" reading was a classifier-model artefact: the tool's 5-bit/8-bit families do not model DeSmuME's internal 6-bit BGR666 pipeline, and every quad happened to fall inside `TOL=8`. A DeSmuME-style `6bit_round` variant (5→6 `(v<<1)|(v>>4)`, A2 blend `((a+1)*S6+(31-a)*B6)>>5`, output `round(v6*255/63)`) added to `expected.py`/`measure_alpha.py` is the best fit on the same harness frame: `best_variant=6bit_round`, `best_convention=A2`, `best_total_dev=9` (L24/R16/R20 +1 per channel; every other quad exact), so the EMULATOR verdict is **A2**, not A1. The 6-bit model is derived from DeSmuME's source (`utils/colorspacehandler/colorspacehandler.cpp:98-134`, `rasterize.cpp:490-496`) and is a HYPOTHESIS for real hardware: real hardware remains **NOT YET MEASURED**.
Full corrected output: report §5.2; the suite is 43 tests.

**Update (2026-11-23):** repair 4 trimmed duplicated report text (no code or test change) and relabelled the §5.2/README §11 output-stage sentence HYPOTHESIS/UNVERIFIED; the total diff vs `main` is now below the 117,760-byte limit (report §10).

**Update (2026-11-23):** EQ-4i measured the convention on real hardware (one New 3DS DS-mode run, one capture session, one ROM): the fixture `tools/ds_alpha_test/fixtures/al1_3_top400x240.png` makes the current tool report `verdict=A1`, but that is a classifier-model artefact — the green channel is the DeSmuME 6-bit **A2** value on the decisive quads (`g_channel_verdict=A2`, `g_votes_A1=0 g_votes_A2=10 g_votes_ambiguous=4`) and red/blue are the 5-bit quantisation of those 6-bit values (`6bit_565` R/B fit deviation 0 on all 14 quads). The `6bit_565` model is a HYPOTHESIS and the origin of the R/B quantisation is UNVERIFIED. Full output and labels: `docs/reports/eq4i-alpha-hw-result.md`.

**Update (2026-11-23):** EQ-4j measured the sub-pixel DS position of every `quads.json` edge in the hardware fixture and a DeSmuME native frame (`tools/ds_alpha_test/measure_edges.py`). FACT (quoted output only): under the committed mapping hardware edges are within about 0.1 DS px of nominal (`-0.11`/`-0.09` in x, `-0.07`/`-0.08` in y; width/height `+0.02`/`0.00`), DeSmuME puts every left/right edge exactly nominal and every top/bottom edge exactly `+1.0000` below nominal, and hardware-minus-DeSmuME is about `-1.07` DS px in y and `-0.09` in x (y survives the EQ-4g fit mapping). HYPOTHESIS: the ~1 px dy-negative offset is a real vertical placement difference between hardware and DeSmuME (hardware nominal, DeSmuME displaced), not a hardware shift, not the capture mapping and not resampling blur. UNVERIFIED: which side is displaced (no top/bottom screen-line edge anchors the frame origin), separation of the mapping-relative residuals from an absolute hardware raster shift (the committed mapping was fitted against another ROM's nominal geometry), other consoles/sessions, the resampler kernel, non-axis-aligned/textured geometry. Full evidence: `docs/reports/eq4j-3d-edge-position.md`.

**Update (2026-11-23):** EQ-4k added the frame-origin ROM missing from EQ-4j (`tools/ds_edge_test/`, static opaque libnds scene: white bands on the top/bottom/left/right screen edges, four 32×32 interior squares, a 1-px-high and a 1-px-wide line, one shared `quads.json`, `measure_rows.py`). EMULATOR (DeSmuME frame 1, `harness_exit=0`, 11 polygons/44 vertices): `first_white_row=0` and `frame_top_row_white=1` so row 0 is painted; the top band measures `0..16` (17 rows vs nominal 16), squares `33..64` vs `32..63`, `hline` two rows `96..97`, `bottom_band` `177..191` (clipped), and every vertical edge is nominal (`col_diff=0`). SOURCE-READ: the harness PPM is a linear copy of the 3D framebuffer with no row offset/flip/crop (`harness.cpp:386-399`), and DeSmuME's `Ceil16_16` (`rasterize.cpp:116-124`, `:2127`, row write `:891`) rounds the transformed vertex y up (harness dump `1049088` → `16.007812`, `4194816` → `64.007812`, `2098176` → `32.015625`) while x is exact, so horizontal edges land one row lower. HYPOTHESIS: this is the source of EQ-4j's `+1.0000`; it is a per-edge rasterisation offset, not a whole-frame shift. The hardware prediction table and owner procedure are in `docs/reports/eq4k-edge-rows.md`; real hardware remains **NOT YET MEASURED**.

**Update (2026-11-23):** EQ-4l measured that prediction on real hardware (one New 3DS DS-mode run, one capture session, one ROM): the fixture `tools/ds_edge_test/fixtures/ed1_3_top400x240.png` makes `measure_rows.py` print `classification=none` with every measurable `row_offset` `-0.0758`..`-0.0759`, the `top_band` bottom edge at `hw=15.9241` (nominal 16), and raw pixel probes show white at capture rows 0–4 and 236–239 (col 100) with the left/right band edges exactly at DS columns 0/256 (row 120). So hardware is nominal — 16-row top band from the first capture row, bottom band to the last capture row, every measurable edge within about 0.1 DS px — whereas DeSmuME keeps its 17-row top band and `+1` horizontal end edges. HYPOTHESIS: the hardware rasterises the half-open vertex rectangle `[y0,y1)` while DeSmuME adds a row on horizontal edges (`Ceil16_16`), the likely cause of the 1-DS-px dy-negative offset of EQ-4f/EQ-4g/EQ-4j. UNVERIFIED: other consoles/sessions, textured/non-axis-aligned geometry, 1-px lines (`hline`/`vline` UNMEASURED — resampling), and the PICA arm. Full evidence: `docs/reports/eq4l-edge-rows-hw-result.md`.

**Update (2026-11-23):** EQ-4m fed the same ROM's GX stream through our chain (a new `tools/ds_edge_test/record/main.cpp` recorder variant, `make record`; the plain ROM still links only `-lnds9` and cannot write `twlhd_frame_v2.bin`) and measured the Azahar PICA render with `measure_rows.py`. EMULATOR/FACT: every horizontal edge fits `capture_y = 1.25*y_ds + 1.0` (one 400x240 PICA pixel = 0.8 DS px) with zero residuals; every vertical edge is `40 + 1.25*x_ds` exactly. The top band is still 16 DS rows and the squares 32 DS rows, so the edge rule is hardware-like half-open, not DeSmuME's `+1` DS row; the 0.8 DS offset is a uniform one-capture-pixel downward placement of every painted horizontal boundary (rigid framing versus fill rule UNVERIFIED). At 256x192 whole-pixel resolution it rounds to `+1` and reads like DeSmuME. SOURCE-READ: `twlhd_gx_interp.c:540-559` has no ceil, and Azahar's software rasteriser uses half-pixel centres plus a top-left bias (`sw_rasterizer.cpp:304-326`). UNVERIFIED: the split between the rasteriser fill rule and the arm11/Azahar output framing, plus a one-row top-screen artefact. Report: `docs/reports/eq4m-pica-edge-rows.md`.

**Update (2026-11-24):** REPAIR-2 re-ran the EQ-4m chain as one linear sequence at work-tree HEAD `a4b0cd9`: rebuilt `arm11/twlhd-arm11.3dsx` (sha256 `f38baecb…`), staged the frame whose Step 2 `sha256sum` is `1e6ef3f4…` (§1.4 is the capture of record), and two Azahar host runs into separate `--out` dirs produced identical 400x480 PPMs (`6f647915…`, bottom half included). FACT: the top crop still hashes `227454cc…`, every horizontal edge is still `+0.8000` DS px and every vertical `+0.0000`, and the fit is still `capture_y = 1.25*y_ds + 1.0` with zero residuals, so the §5 conclusion stands. The earlier `c5549ef…`/`a94d88…` full-PPM hashes came from runs whose `.3dsx` was never hashed and are **UNVERIFIED, not used**. HYPOTHESIS/UNVERIFIED: the full-PPM hash changed because `arm11/Makefile:75-84` embeds the short git HEAD in the bottom-screen build-ID line printed at `arm11/source/main.c:406`, and HEAD moved; the measured top screen is unchanged.

**Update (2026-11-24):** REPAIR-3 fixed the four EQ-4m review defects (no code or test change) and re-ran the same chain at work-tree HEAD `b775345` (source-unchanged check quoted in `docs/reports/eq4m-pica-edge-rows.md` §2.2). Rebuild `arm11/twlhd-arm11.3dsx` sha256 `77e4fc0d…`; staged frame Step 2 `sha256sum` `1e6ef3f4…`; both Azahar host runs identical at `d4d785c5…`; top crop `227454cc…` (unchanged), bottom 400x240 crop `231896ed…` (the script label is corrected from `bottom240x240` to `bottom400x240`). FACT: every horizontal edge is still `+0.8000` DS px and every vertical `+0.0000`, and the fit is still `capture_y = 1.25*y_ds + 1.0` with zero residuals. These numbers belong to the REPAIR-3 sequence at HEAD `b775345`; later documentation-only commits change the embedded build ID and thereby the full-PPM/bottom-crop hashes (HYPOTHESIS/UNVERIFIED, SOURCE-READ `arm11/Makefile:75-84`, `arm11/source/main.c:406`), and the measured top screen is the load-bearing result. HYPOTHESIS/UNVERIFIED: the `.3dsx`, full-PPM and bottom-crop hash changes come from the build-ID line (`arm11/Makefile:75-84`, printed at `arm11/source/main.c:406`) embedding the moved short HEAD; the measured top screen is unchanged.

**Update (2026-11-24):** EQ-4n decided where the EQ-4m one-capture-row (0.8 DS px) downward shift comes from. FACT (one Azahar run per frame at HEAD `0a40b6a`): the unchanged `ds-edge-record` frame reproduces the EQ-4m top crop `227454cc…`; a synthetic frame with the same geometry moved `+3` DS rows measures `ceil(1.25*(y+3)) + 1`, so the `+1` is constant and geometry-independent and only the sub-pixel `ceil` follows the PICA grid; row 0 equals row 239 shifted left one pixel and its last pixel is black (a read one pixel past the 240x400 framebuffer). SOURCE-READ: `tools/azahar_host` copies rows verbatim; the Azahar software renderer reads the top framebuffer as `framebuffer[y][stride-x]` (columns 240..1 instead of 239..0, `renderer_software.cpp:47-75`) and the libretro blit maps that column axis to the PPM rows (`libretro_window.cpp:131-152`). The arm11 target/viewport/clipfix has no one-row term, and the PICA top-left rule (`sw_rasterizer.cpp:291-326`) accounts only for the integer `ceil`. Verdict: the shift is the Azahar core's software presentation (B), not the arm11 program (A) and not the PICA fill rule (C); real hardware is UNVERIFIED (Azahar software renderer only). Report: `docs/reports/eq4n-arm11-row-shift.md`.

**Update (2026-11-24):** EQ-4o measured the same horizontal edges on real hardware (one New 3DS, one capture session per frame; fixtures in `tools/eq4o_hw_rows/`). FACT: frame A (the EQ-4n frame) has column-100 white runs `0-19,40-79,160-199,220-239` and frame B (`+3` DS rows) `4-23,44-83,164-203,224-239`; every edge measures `ceil(1.25*y_ds)` (A: 20/40/80/160/200/220, B: 24/44/84/164/204/224), rows 0..2 are white for A and `(16,16,24)` for B, row 120 spans capture columns 40..359, and `row0_wrap=0`. The EQ-4n §3.3 azahar values are exactly one capture row lower on every one of the 12 edges, plus an azahar-only wrapped `0-0` run for B. HYPOTHESIS (B) is supported on this console: the `+1` is introduced by the azahar presentation readback, not by arm11 or PICA. UNVERIFIED: other consoles/sessions, other geometry, why azahar reads `pixel_stride - x`, and the `.3dsx` was not rebuilt this round. Report: `docs/reports/eq4o-hw-row-shift.md`.

**Update (2026-11-24):** EQ-4p made the EQ-4n/EQ-4o presentation row explicit in the analysis tools. One shared helper (`tools/azahar_presentation.py`: EQ-4n/EQ-4o basis constants, the exact `row0[x] == row239[x+1]` + `row0 != row239` wrap guard, the `corrected[y] = image[y+1]` / last-row-duplicate correction) is now an `--azahar-correction auto|on|off` option in `tools/eq4_hw_compare.py`, `tools/eq4g_fit_compare.py`, `tools/eq3_compare.py`, `tools/eq_scenes.py` and `tools/ds_edge_test/measure_rows.py`; every applying tool prints `presentation-row correction: applied|not applied (reason)`, `off` reproduces the legacy numbers, and hardware/DeSmuME inputs are never corrected silently. `tools/eq4_centroid_deltas.py`, `tools/ds_alpha_test/measure_edges.py` and `tools/eq4o_hw_rows/measure_rows.py` were deliberately not changed (reasons in the report). FACT: the guard fires on `tools/fixtures/eq3/ours_cube_f240_top.ppm` and not on either EQ-4o hardware crop. Report: `docs/reports/eq4p-presentation-row-correction.md`.

**Update (2026-11-25):** EQ-4z: with the black clear colour, `auto` can no longer detect the row-0 wrap on uniform black border rows and reports `undetermined (uniform border rows)`. `tools/eq_scenes.py` and `tools/eq4g_fit_compare.py` default to `--azahar-correction on` because their `ours` reference is always an Azahar render; `tools/eq3_compare.py` and `tools/eq4_hw_compare.py` keep `auto` as the CLI default. A sidecar marker `<ppm>.azahar-corrected` prevents a double correction when `on` is used. Report: `docs/reports/eq4z-clear-black.md`.

**Update (2026-11-24):** EQ-4q extends the `STATIC_ANGLE` console plan from SCENE=5 to the cube baseline and SCENE=1..4 / CUBES=2. The same capture procedure (`--enabled_both 0 --enabled_top 1 --scaling_top 1`, top 400×240 crop, `tools/eq4_hw_compare.py`) applies, but per-scene harness references must be generated before comparison; only SCENE=5 static references exist as committed fixtures. No ROM was run on hardware. Report: `docs/reports/eq4q-static-scenes.md`.

**Update (2026-11-24):** EQ-4r ran the EQ-4q per-scene comparison offline for all six static-30 ROMs with the existing tool and the shared 1.25x/offset-40 mapping: twelve runs and 17 fixtures. FACT: cube, s1, s2, s3 MATCH both references; `SCENE=4` is DIFFERENT (hardware draws the alpha-0 quad's outline, `bbox_dev_top=10` vs tol 3 against both references, and alpha 16/4 are opaque on hardware and DeSmuME but blended in our render); `CUBES=2` is NOT-COMPARABLE against DeSmuME (harness exit 1, `ARM OP_BKPT triggered`, no reference) and PASSes against ours. The EQ-4p line is visible in all twelve outputs (`not applied` for the 256x192 harness PPMs, `applied (row-0 wrap signature detected)` for the 400x240 `ours` PPMs) and never applies to hardware or DeSmuME images. UNVERIFIED: other consoles/sessions, capture-noise origin, textured geometry, the hardware ROM identity (not hashed in the run's `SHA256SUMS`). Report: `docs/reports/eq4r-static-scenes-hw-compare.md`.

**Update (2026-11-25):** EQ-4w recorded the EQ-4v blend-ROM hardware capture (one console, one capture session, scene 4 static angle 30): with `GL_BLEND` enabled the alpha-16 green quad moves from full `255` to `134` (6-bit A2 prediction 134) and the alpha-4 blue from `255` to `33` (prediction 36; the 3-level difference is consistent with EQ-4i's R/B 5-bit quantisation as HYPOTHESIS only), the alpha-31 quad stays `255`, and the alpha-0 outline is present in both captures. The blend-enable hypothesis is supported on this console/session; measurement tool and fixtures are in `tools/eq4w_blend_hw/`. Report: `docs/reports/eq4w-blend-hw-result.md`.
