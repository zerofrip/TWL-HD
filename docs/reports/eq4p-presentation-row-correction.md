<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4p — the Azahar presentation row, accounted for in the comparison tools

**Status:** OFFLINE TOOLING CHANGE. No console, no emulator run, no production
code change (`arm9v2/`, `arm11/`, `common/`, `arm7/`, `arm9/`,
`tools/azahar_host/` untouched; the external Azahar core was not touched).
One new shared helper, its unit tests, flags and tests in the tools that read
an Azahar picture, this report, and append-only pointers.

```
basis=EQ-4n (mechanism) + EQ-4o (hardware); one console, one capture session
      per frame; the +1 row and the row-0 wrap are measured only for 400x240
      top-screen output.
```

Labels: **FACT** = a command and its complete raw output quoted here;
**SOURCE-READ** = a claim about repository source, cited by file/line;
**HYPOTHESIS** / **UNVERIFIED** = not measured by the quoted output.

Every command below was run once at work-tree HEAD `293e0f6`, except §8,
re-quoted at `af62411`. Privacy redaction: the commands contain no home path;
paths below the root are shown repository-relative.

## 1. What EQ-4n/EQ-4o established (SOURCE-READ, quoted)

SOURCE-READ `docs/reports/eq4n-arm11-row-shift.md` (the mechanism):

> The Azahar core's software renderer stores the top-screen framebuffer
> transposed with a one-pixel column error (`info.width` columns read as
> `stride-x` = 240..1 instead of 239..0), and the libretro presentation then
> maps that column axis to the PPM row axis: `PPM(ox,oy) =
> framebuffer[ox][240-oy]`, not `[ox][239-oy]`. That shifts every horizontal
> boundary down exactly one capture row, leaves the vertical axis exact, makes
> PPM row 0 the wrapped column 240 of the next framebuffer row, and makes the
> last pixel of row 0 a read one pixel past the 240x400 framebuffer (black in
> this run).

SOURCE-READ `docs/reports/eq4o-hw-row-shift.md` §5:

> **Hardware matches `ceil(1.25*y_ds)` exactly for both frames** — all 12
> measured edges, `match=ceil`, no residual (§3, §4).

The corollary the tools must encode: an Azahar 400x240 top crop carries one
presentation row that hardware does not, and a host-vs-hardware/DeSmuME
comparison must account for it **visibly**.

## 2. Tool triage

Changed (each demonstrably consumes an Azahar top-screen PPM):

| tool | Azahar input | change |
|---|---|---|
| `tools/azahar_presentation.py` (new) | — | constants, wrap guard, correction, decision line |
| `tools/eq4_hw_compare.py` | the 400x240/400x480 "ours" `--reference` PPM | flag, guard, correction before `capture_to_ds`/`compare`, visible line |
| `tools/eq4g_fit_compare.py` | the same "ours" reference through `eq4.load_reference` | flag passed through, same correction, visible line |
| `tools/eq3_compare.py` | the 400x240/400x480 "ours" picture | flag on the CLI, correction inside `load_picture`, visible line |
| `tools/eq_scenes.py` | `_prepare_azahar` output and the committed cube pair | flag, correction in `compare_eq3`, visible line |
| `tools/ds_edge_test/measure_rows.py` | the EQ-4m capture-mode 400x240 crop | flag, correction after `read_image` in capture mode only, visible line |

Not changed, with the reason:

- **`tools/eq4_centroid_deltas.py`** — SOURCE-READ `:19-31`: it parses the
  `key=value` stdout of `eq4_hw_compare.py`; it never sees an image, so it has
  nothing to correct. It inherits the correction through the report it reads.
- **`tools/ds_alpha_test/measure_edges.py`** — SOURCE-READ module docstring
  and `:48-60`: its documented inputs are a real-hardware capture and/or a
  DeSmuME 256x192 native frame. An Azahar crop reaches `measure_image` only
  through `measure_rows.py`, which corrects it upstream before calling.
- **`tools/eq4o_hw_rows/measure_rows.py`** — SOURCE-READ `:165-176`: it reads
  only the two real-hardware PNG crops. It also *is* the required no-wrap
  negative case (`row0_wrap=0`), so changing it would weaken the guard test.

`tools/ds_edge_test/measure_rows.py` imports `measure_edges.measure_image`
by design; the correction is applied to the pixel array before that call, so
the imported module stays untouched.

## 3. The shared helper (SOURCE-READ)

`tools/azahar_presentation.py`:

- `BASIS` constant cites EQ-4n/EQ-4o, one console, one capture session per
  frame, and that the numbers are measured only for 400x240 top-screen output.
- **Wrap guard, exactly:** for a 400-wide image whose top screen is 240 rows,
  wrap iff `row0[x] == row239[x+1]` for all x = 0..398 **and**
  `row0 != row239` at some x. The second clause rejects flat/uniform images,
  which the EQ-4o probe alone would accept.
- **Correction:** `corrected[y] = image[y+1]` for y = 0..238 (content moves up
  one row; the wrapped row 0 is dropped), and `corrected[239] = image[239]` —
  no capture row exists for true row 239, so the last corrected row is a
  documented duplicate of true row 238. For a 400x480 PPM only the upper
  400x240 top screen is corrected; the bottom screen is unchanged.
- `apply_correction(mode, pixels, width, height)` returns
  `(pixels, applied, reason)` for `auto` (guard), `on` (forced), `off`
  (never). `off` is the byte-identical legacy path.
- `format_line` prints exactly
  `presentation-row correction: applied|not applied (reason)`.

The guard fires on the committed cube fixture `tools/fixtures/eq3/ours_cube_f240_top.ppm`
(its row 0 is `(16,16,24)` for x=0..398 plus one black last pixel) and does
not fire on either EQ-4o hardware crop (`row0_eq_row239_shifted=397/399` and
`79/399` in EQ-4o §3). This is the documented residual: a hardware crop whose
rows 0 and 239 are both uniform background would also match; the `row0 !=
row239` clause narrows but cannot eliminate it.

## 4. Changed tools — quoted commands and raw output

### 4.1 `tools/eq3_compare.py` (FACT)

```
$ python3 tools/eq3_compare.py tools/fixtures/eq3/harness_cube_f240.ppm tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction off
EQ-3b structural picture comparison (EMULATOR)
presentation-row correction: not applied (--azahar-correction off)
face magenta: not visible (fully occluded on both sides)
face red: desmume bbox=(78,43)-(125,113) centroid=(105.58,77.13) pixels=1198; ours bbox=(78,43)-(124,112) centroid=(104.69,76.97) pixels=1196; dev bbox=(0,0,1,1) centroid=(0.89,0.16)
face red colour: desmume~(254, 0, 0) ours~(255, 0, 0) diff=(1, 0, 0) tol 8
face yellow: desmume bbox=(132,43)-(178,112) centroid=(151.16,77.52) pixels=1180; ours bbox=(131,43)-(177,112) centroid=(150.15,77.36) pixels=1181; dev bbox=(1,0,1,0) centroid=(1.01,0.17)
face yellow colour: desmume~(251, 251, 0) ours~(255, 255, 0) diff=(4, 4, 0) tol 8
face green: not visible (fully occluded on both sides)
face blue: not visible (fully occluded on both sides)
face cyan: desmume bbox=(78,84)-(178,146) centroid=(128.14,118.23) pixels=3188; ours bbox=(77,84)-(177,146) centroid=(127.22,118.14) pixels=3188; dev bbox=(1,0,1,0) centroid=(0.92,0.09)
face cyan colour: desmume~(0, 254, 254) ours~(0, 255, 255) diff=(0, 1, 1) tol 8
PASS faces=3 worst colour-r=4 (tol 8) at face yellow
exit=0
```

```
$ python3 tools/eq3_compare.py tools/fixtures/eq3/harness_cube_f240.ppm tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction on
EQ-3b structural picture comparison (EMULATOR)
presentation-row correction: applied (--azahar-correction on (forced))
face magenta: not visible (fully occluded on both sides)
face red: desmume bbox=(78,43)-(125,113) centroid=(105.58,77.13) pixels=1198; ours bbox=(77,42)-(124,112) centroid=(104.68,76.22) pixels=1193; dev bbox=(1,1,1,1) centroid=(0.90,0.92)
face red colour: desmume~(254, 0, 0) ours~(255, 0, 0) diff=(1, 0, 0) tol 8
face yellow: desmume bbox=(132,43)-(178,112) centroid=(151.16,77.52) pixels=1180; ours bbox=(131,43)-(177,112) centroid=(150.16,76.59) pixels=1180; dev bbox=(1,0,1,0) centroid=(1.00,0.93)
face yellow colour: desmume~(251, 251, 0) ours~(255, 255, 0) diff=(4, 4, 0) tol 8
face green: not visible (fully occluded on both sides)
face blue: not visible (fully occluded on both sides)
face cyan: desmume bbox=(78,84)-(178,146) centroid=(128.14,118.23) pixels=3188; ours bbox=(77,83)-(178,145) centroid=(127.20,117.34) pixels=3191; dev bbox=(1,1,0,1) centroid=(0.94,0.89)
face cyan colour: desmume~(0, 254, 254) ours~(0, 255, 255) diff=(0, 1, 1) tol 8
PASS faces=3 worst colour-r=4 (tol 8) at face yellow
exit=0
```

The legacy number is the off `face red` line; the corrected number is the on
`face red` line (bbox top 43 -> 42, centroid y 76.97 -> 76.22). This fixture's
content is not one capture row below the harness on its axis-aligned top edge,
so the forced correction makes that bbox one row *further* from DeSmuME on
this frame; the verdict is PASS in both modes. The guard result itself is the
finding, not the sign of the delta.

### 4.2 `tools/eq_scenes.py` (FACT)

```
$ python3 tools/eq_scenes.py --manifest tools/fixtures/eq_scenes/manifest.json --azahar-correction off
presentation-row correction: not applied (--azahar-correction off)
EQ-2/EQ-3 scene coverage (EMULATOR; frame 240 -> recorder 241 -> harness 242)
scene    build                        polys/verts  x          y          z(0.31)  w   rgb  tex  EQ-3  verdict
cube     AUTODUMP_FRAME=240           6/24         0.0190 px  0.0109 px  114325   0   0   0    PASS colour-r=4 PASS
scene1   SCENE=1 AUTODUMP_FRAME=240   8/24         0.0118 px  0.0148 px  74058    1   1   0    NOT-COMPARABLE PASS
scene2   SCENE=2 AUTODUMP_FRAME=240   0 compared (reference 10/30) -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE
scene3   SCENE=3 AUTODUMP_FRAME=240   -            -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE
scene4   SCENE=4 AUTODUMP_FRAME=240   4/16         0.0166 px  0.0164 px  91608    1   0   0    NOT-COMPARABLE PASS
scene5   SCENE=5 AUTODUMP_FRAME=240   1/4          0.0087 px  0.0148 px  165343   0   0   0    NOT-COMPARABLE PASS
scene5-static0 SCENE=5 STATIC_ANGLE=0 AUTODUMP_FRAME=240 1/4          0.0000 px  0.0000 px  0        0   0   0    NOT-COMPARABLE PASS
scene5-static30 SCENE=5 STATIC_ANGLE=30 AUTODUMP_FRAME=240 1/4          0.0146 px  0.0113 px  122797   0   0   0    NOT-COMPARABLE PASS
cubes    CUBES=2 AUTODUMP_FRAME=240   0 compared (reference 12/48) -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE

scene1: PASS
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3; eq3's six cube face classes cannot model 24 distinct red shades

scene2: NOT-COMPARABLE
  reason: the capture aborted with ARM OP_BKPT triggered before the autodump frame (no recorder/demo_frame limit is reached, and the faulting PC was not measurable here; docs/EQ_SCENES.md section 3); no our-side v2 stream, so EQ-2 cannot run (the harness reference itself succeeded)
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] (demo_frame.cpp:237, colour loop :266-269, drawn at :283); eq3's six cube face classes cannot model 12 distinct red shades

scene3: NOT-COMPARABLE
  reason: the SCENE=3 ROM presents no 3D frame in the harness (exit 1: 'ROM never presented a 3D frame within 1200 frames') and the capture logs ARM OP_BKPT, so there is no reference geometry
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] (demo_frame.cpp:237, colour loop :266-269, drawn at :289); eq3's six cube face classes cannot model 12 distinct red shades

scene4: PASS
  EQ-3 report:
    blended pixels need the design section 5.1 blended tolerance 16 (alpha convention a/31 vs (a+1)/32 UNVERIFIED); eq3 models neither blending nor the alpha-0 wireframe (design 2.3)

scene5: PASS
  EQ-3 report:
    textured scene: eq3 has no texture model and the geometry list carries no texture state

scene5-static0: PASS
  EQ-3 report:
    fixed-angle textured scene: eq3 has no white face class and no texture model, so its six cube face classes collapse the white textured cells onto magenta (tool-model limitation)

scene5-static30: PASS
  EQ-3 report:
    fixed-angle textured scene: eq3 has no white face class and no texture model, so its six cube face classes collapse the white textured cells onto magenta (tool-model limitation)

cubes: NOT-COMPARABLE
  reason: DeSmuME capture aborted with ARM OP_BKPT before the autodump frame; no our-side v2 stream, so EQ-2 cannot run (the harness reference itself succeeded)
  EQ-3 report:
    our-side picture not produced because the v2 capture failed

summary: 9 rows, 6 PASS, 0 FAIL, 3 NOT-COMPARABLE
exit=0
```

```
$ python3 tools/eq_scenes.py --manifest tools/fixtures/eq_scenes/manifest.json --azahar-correction on
presentation-row correction: applied (--azahar-correction on (forced))
EQ-2/EQ-3 scene coverage (EMULATOR; frame 240 -> recorder 241 -> harness 242)
scene    build                        polys/verts  x          y          z(0.31)  w   rgb  tex  EQ-3  verdict
cube     AUTODUMP_FRAME=240           6/24         0.0190 px  0.0109 px  114325   0   0   0    PASS colour-r=4 PASS
scene1   SCENE=1 AUTODUMP_FRAME=240   8/24         0.0118 px  0.0148 px  74058    1   1   0    NOT-COMPARABLE PASS
scene2   SCENE=2 AUTODUMP_FRAME=240   0 compared (reference 10/30) -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE
scene3   SCENE=3 AUTODUMP_FRAME=240   -            -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE
scene4   SCENE=4 AUTODUMP_FRAME=240   4/16         0.0166 px  0.0164 px  91608    1   0   0    NOT-COMPARABLE PASS
scene5   SCENE=5 AUTODUMP_FRAME=240   1/4          0.0087 px  0.0148 px  165343   0   0   0    NOT-COMPARABLE PASS
scene5-static0 SCENE=5 STATIC_ANGLE=0 AUTODUMP_FRAME=240 1/4          0.0000 px  0.0000 px  0        0   0   0    NOT-COMPARABLE PASS
scene5-static30 SCENE=5 STATIC_ANGLE=30 AUTODUMP_FRAME=240 1/4          0.0146 px  0.0113 px  122797   0   0   0    NOT-COMPARABLE PASS
cubes    CUBES=2 AUTODUMP_FRAME=240   0 compared (reference 12/48) -          -          -        -   -   -    NOT-COMPARABLE NOT-COMPARABLE

scene1: PASS
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3; eq3's six cube face classes cannot model 24 distinct red shades

scene2: NOT-COMPARABLE
  reason: the capture aborted with ARM OP_BKPT triggered before the autodump frame (no recorder/demo_frame limit is reached, and the faulting PC was not measurable here; docs/EQ_SCENES.md section 3); no our-side v2 stream, so EQ-2 cannot run (the harness reference itself succeeded)
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] (demo_frame.cpp:237, colour loop :266-269, drawn at :283); eq3's six cube face classes cannot model 12 distinct red shades

scene3: NOT-COMPARABLE
  reason: the SCENE=3 ROM presents no 3D frame in the harness (exit 1: 'ROM never presented a 3D frame within 1200 frames') and the capture logs ARM OP_BKPT, so there is no reference geometry
  EQ-3 report:
    per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] (demo_frame.cpp:237, colour loop :266-269, drawn at :289); eq3's six cube face classes cannot model 12 distinct red shades

scene4: PASS
  EQ-3 report:
    blended pixels need the design section 5.1 blended tolerance 16 (alpha convention a/31 vs (a+1)/32 UNVERIFIED); eq3 models neither blending nor the alpha-0 wireframe (design 2.3)

scene5: PASS
  EQ-3 report:
    textured scene: eq3 has no texture model and the geometry list carries no texture state

scene5-static0: PASS
  EQ-3 report:
    fixed-angle textured scene: eq3 has no white face class and no texture model, so its six cube face classes collapse the white textured cells onto magenta (tool-model limitation)

scene5-static30: PASS
  EQ-3 report:
    fixed-angle textured scene: eq3 has no white face class and no texture model, so its six cube face classes collapse the white textured cells onto magenta (tool-model limitation)

cubes: NOT-COMPARABLE
  reason: DeSmuME capture aborted with ARM OP_BKPT before the autodump frame; no our-side v2 stream, so EQ-2 cannot run (the harness reference itself succeeded)
  EQ-3 report:
    our-side picture not produced because the v2 capture failed

summary: 9 rows, 6 PASS, 0 FAIL, 3 NOT-COMPARABLE
exit=0
```

Only the cube row has an Azahar picture in this manifest; the visible change
is the correction line, and the table's rounded EQ-3 column is unchanged.

### 4.3 `tools/eq4_hw_compare.py` (FACT)

Command pair uses the only committed Azahar PPM as `--reference` with a real
hardware capture; the scenes differ, so the verdict is FAIL in both modes and
the quoted evidence is the visible line plus the changed `ref_bbox_top`
(34 -> 33) and the row-derived class metrics.

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction off
presentation-row correction: not applied (--azahar-correction off)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=50.5609
ds.mean_abs_g=56.6083
ds.mean_abs_b=50.8485
ds.max_error=255.0000
ds.frac_within=0.0070
ds.frac_within_flat=0.0807
ds.frac_within_edge=0.0021
ds.flat_pixels=3059
ds.edge_pixels=46093
capture.mean_abs_r=50.7548
capture.mean_abs_g=56.8003
capture.mean_abs_b=51.0472
capture.max_error=255.0000
capture.frac_within=0.0068
capture.frac_within_flat=0.0734
capture.frac_within_edge=0.0022
capture.flat_pixels=4961
capture.edge_pixels=71839
ref_nonbg_pixels=8252
ref_bbox_left=69
ref_bbox_top=34
ref_bbox_right=186
ref_bbox_bottom=150
capture_nonbg_pixels=8042
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=2008
class_red_ref_centroid_x=103.2674
class_red_ref_centroid_y=78.2963
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1990
class_yellow_ref_centroid_x=151.5558
class_yellow_ref_centroid_y=78.6603
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=11
bbox_dev_top=8
bbox_dev_right=9
bbox_dev_bottom=13
verdict=FAIL
reason=bbox-left deviates 11 DS px (tol 3); bbox-top deviates 8 DS px (tol 3); bbox-right deviates 9 DS px (tol 3); bbox-bottom deviates 13 DS px (tol 3); ds.frac_within_flat 0.0807 < 0.95; class red centroid deviates 24.11 DS px (tol 3); class yellow centroid deviates 24.05 DS px (tol 3)
exit=1
```

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction on
presentation-row correction: applied (--azahar-correction on (forced))
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=50.4448
ds.mean_abs_g=56.5831
ds.mean_abs_b=50.8106
ds.max_error=255.0000
ds.frac_within=0.0073
ds.frac_within_flat=0.0851
ds.frac_within_edge=0.0021
ds.flat_pixels=3057
ds.edge_pixels=46095
capture.mean_abs_r=50.6429
capture.mean_abs_g=56.7781
capture.mean_abs_b=51.0107
capture.max_error=255.0000
capture.frac_within=0.0071
capture.frac_within_flat=0.0788
capture.frac_within_edge=0.0022
capture.flat_pixels=4949
capture.edge_pixels=71851
ref_nonbg_pixels=8247
ref_bbox_left=69
ref_bbox_top=33
ref_bbox_right=186
ref_bbox_bottom=149
capture_nonbg_pixels=8042
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=2005
class_red_ref_centroid_x=103.2643
class_red_ref_centroid_y=77.5232
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1989
class_yellow_ref_centroid_x=151.5556
class_yellow_ref_centroid_y=77.8748
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=11
bbox_dev_top=7
bbox_dev_right=9
bbox_dev_bottom=14
verdict=FAIL
reason=bbox-left deviates 11 DS px (tol 3); bbox-top deviates 7 DS px (tol 3); bbox-right deviates 9 DS px (tol 3); bbox-bottom deviates 14 DS px (tol 3); ds.frac_within_flat 0.0851 < 0.95; class red centroid deviates 24.11 DS px (tol 3); class yellow centroid deviates 24.05 DS px (tol 3)
exit=1
```

### 4.4 `tools/eq4g_fit_compare.py` (FACT)

Same pair; this is the DS-space subset of §4.3 (`ds.*`, bbox, classes).

```
$ python3 tools/eq4g_fit_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction off
presentation-row correction: not applied (--azahar-correction off)
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250000
mapping_scale_y=1.250000
mapping_offset_x=40.000000
mapping_offset_y=0.000000
ds.mean_abs_r=50.5609
ds.mean_abs_g=56.6083
ds.mean_abs_b=50.8485
ds.max_error=255.0000
ds.frac_within=0.0070
ds.frac_within_flat=0.0807
ds.frac_within_edge=0.0021
ds.flat_pixels=3059
ds.edge_pixels=46093
ref_nonbg_pixels=8252
capture_nonbg_pixels=8042
ref_bbox_left=69
ref_bbox_top=34
ref_bbox_right=186
ref_bbox_bottom=150
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=2008
class_red_ref_centroid_x=103.2674
class_red_ref_centroid_y=78.2963
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1990
class_yellow_ref_centroid_x=151.5558
class_yellow_ref_centroid_y=78.6603
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=11
bbox_dev_top=8
bbox_dev_right=9
bbox_dev_bottom=13
verdict=FAIL
reason=bbox-left deviates 11 DS px (tol 3); bbox-top deviates 8 DS px (tol 3); bbox-right deviates 9 DS px (tol 3); bbox-bottom deviates 13 DS px (tol 3); ds.frac_within_flat 0.0807 < 0.95; class red centroid deviates 24.11 DS px (tol 3); class yellow centroid deviates 24.05 DS px (tol 3)
exit=0
```

```
$ python3 tools/eq4g_fit_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction on
presentation-row correction: applied (--azahar-correction on (forced))
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250000
mapping_scale_y=1.250000
mapping_offset_x=40.000000
mapping_offset_y=0.000000
ds.mean_abs_r=50.4448
ds.mean_abs_g=56.5831
ds.mean_abs_b=50.8106
ds.max_error=255.0000
ds.frac_within=0.0073
ds.frac_within_flat=0.0851
ds.frac_within_edge=0.0021
ds.flat_pixels=3057
ds.edge_pixels=46095
ref_nonbg_pixels=8247
capture_nonbg_pixels=8042
ref_bbox_left=69
ref_bbox_top=33
ref_bbox_right=186
ref_bbox_bottom=149
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=2005
class_red_ref_centroid_x=103.2643
class_red_ref_centroid_y=77.5232
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1989
class_yellow_ref_centroid_x=151.5556
class_yellow_ref_centroid_y=77.8748
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=11
bbox_dev_top=7
bbox_dev_right=9
bbox_dev_bottom=14
verdict=FAIL
reason=bbox-left deviates 11 DS px (tol 3); bbox-top deviates 7 DS px (tol 3); bbox-right deviates 9 DS px (tol 3); bbox-bottom deviates 14 DS px (tol 3); ds.frac_within_flat 0.0851 < 0.95; class red centroid deviates 24.11 DS px (tol 3); class yellow centroid deviates 24.05 DS px (tol 3)
exit=0
```

### 4.5 `tools/ds_edge_test/measure_rows.py` (FACT)

The committed Azahar PPM is a cube frame, not the edge-test geometry, so the
edge classifier prints `classification=no-data`; the quoted change is the
visible line. Corrected edge *numbers* are proven by the new synthetic test in
`tools/ds_edge_test/test_measure_rows.py` (§6): the same capture reads
`row_offset=+0.8` with `--azahar-correction off` and `0.0` with auto.

```
$ python3 tools/ds_edge_test/measure_rows.py tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction off
presentation-row correction: not applied (--azahar-correction off)
mode=capture
mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
white_threshold=128.0
backdrop black level=18.6667
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=bottom nominal=16.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=left_band kind=quad side=top nominal=80.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=left_band kind=quad side=bottom nominal=112.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=left nominal=240.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=bottom nominal=112.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=left nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=right nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=top nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=bottom nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=left nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=right nominal=192.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=top nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=bottom nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=left nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=right nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=top nominal=128.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=bottom nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=left nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=right nominal=192.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=top nominal=128.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=bottom nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=no 50%% crossing in the expected direction
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=no 50%% crossing in the expected direction
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
classification=no-data
exit=0
```

```
$ python3 tools/ds_edge_test/measure_rows.py tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction on
presentation-row correction: applied (--azahar-correction on (forced))
mode=capture
mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
white_threshold=128.0
backdrop black level=18.6667
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=bottom nominal=16.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=left_band kind=quad side=top nominal=80.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=left_band kind=quad side=bottom nominal=112.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=left nominal=240.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=right_band kind=quad side=bottom nominal=112.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=left nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=right nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=top nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq1 kind=quad side=bottom nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=left nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=right nominal=192.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=top nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq2 kind=quad side=bottom nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=left nominal=32.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=right nominal=64.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=top nominal=128.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq3 kind=quad side=bottom nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=left nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=right nominal=192.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=top nominal=128.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=sq4 kind=quad side=bottom nominal=160.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=no 50%% crossing in the expected direction
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=no 50%% crossing in the expected direction
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
classification=no-data
exit=0
```

The guard leaves the EQ-4o hardware crops uncorrected
(SOURCE-READ/UNVERIFIED, not re-measured): the unit test
`test_hardware_crops_have_no_wrap_and_are_unchanged`
(`tools/test_azahar_presentation.py:140`) asserts both are no-wrap and
byte-unchanged. `--native` 256x192 frames never take the correction.

## 5. Before/after of the EQ-4m / EQ-4n / EQ-4o key numbers

These are quoted, not recomputed.

EQ-4m (SOURCE-READ, `docs/reports/eq4m-pica-edge-rows.md`): the fit and the
per-item rows are

> horizontal painted boundary lands `capture_y = 1.25 * y_ds + 1.0` (one 400x240

and every measured item line carries `row_diff=1`, for example:

> item=bottom_band nominal_rows=176-191 nominal_cols=0-255 measured_rows=177-191 measured_cols=0-255 row_diff=1 row_end_diff=0 col_diff=0 col_end_diff=0 row_count=15
> item=left_band nominal_rows=80-111 nominal_cols=0-15 measured_rows=81-112 measured_cols=0-15 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=right_band nominal_rows=80-111 nominal_cols=240-255 measured_rows=81-112 measured_cols=240-255 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=sq1 nominal_rows=32-63 nominal_cols=32-63 measured_rows=33-64 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=sq2 nominal_rows=32-63 nominal_cols=160-191 measured_rows=33-64 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=sq3 nominal_rows=128-159 nominal_cols=32-63 measured_rows=129-160 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=sq4 nominal_rows=128-159 nominal_cols=160-191 measured_rows=129-160 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
> item=hline nominal_rows=96-96 nominal_cols=64-191 measured_rows=97-97 measured_cols=64-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=1
> item=vline nominal_rows=64-159 nominal_cols=208-208 measured_rows=65-160 measured_cols=208-208 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=96

and the vertical edges are

> `capture_x = 40 + 1.25*x_ds` (offset 40, no extra pixel).

EQ-4n/EQ-4o (SOURCE-READ, `docs/reports/eq4o-hw-row-shift.md` §4, quoted):

> Frame A (azahar white runs `(0,20),(41,80),(161,200),(221,239)`):
>
> | edge | y_ds | nominal | ceil | hardware | azahar | azahar−hardware |
> |---|---:|---:|---:|---:|---:|---:|
> | `top_band.bottom` | 16 | 20.00 | 20 | 20 | 21 | +1 |
> | `sq1.top` | 32 | 40.00 | 40 | 40 | 41 | +1 |
> | `sq1.bottom` | 64 | 80.00 | 80 | 80 | 81 | +1 |
> | `sq3.top` | 128 | 160.00 | 160 | 160 | 161 | +1 |
> | `sq3.bottom` | 160 | 200.00 | 200 | 200 | 201 | +1 |
> | `bottom_band.top` | 176 | 220.00 | 220 | 220 | 221 | +1 |
>
> Frame B, `+3` DS rows (azahar white runs
> `(0,0),(5,24),(45,84),(165,204),(225,239)`):
>
> | edge | y_ds | nominal | ceil | hardware | azahar | azahar−hardware |
> |---|---:|---:|---:|---:|---:|---:|
> | `top_band.bottom` | 19 | 23.75 | 24 | 24 | 25 | +1 |
> | `sq1.top` | 35 | 43.75 | 44 | 44 | 45 | +1 |
> | `sq1.bottom` | 67 | 83.75 | 84 | 84 | 85 | +1 |
> | `sq3.top` | 131 | 163.75 | 164 | 164 | 165 | +1 |
> | `sq3.bottom` | 163 | 203.75 | 204 | 204 | 205 | +1 |
> | `bottom_band.top` | 179 | 223.75 | 224 | 224 | 225 | +1 |

**Before/after (the change this task makes).** Before EQ-4p, every tool
consumed those azahar values as-is (21/41/81/161/201/221 for frame A,
25/45/85/165/205/225 for frame B). After EQ-4p, the opt-in/auto correction
moves the content up one capture row, so the same azahar edges would read
20/40/80/160/200/220 and 24/44/84/164/204/224 — the hardware column above.
This is HYPOTHESIS for the edge ROM: the repository has no committed azahar
edge-test PPM, so the corrected edge *numbers* are verified only on the
synthetic unit test (§6), while the guard is demonstrated on the committed
cube PPM (§4). The correction does not invent data: it drops row 0 and
duplicates the last row, so the last row cannot recover a true row 239.

## 6. Tests (FACT)

`tools/test_azahar_presentation.py` (positive + negatives):

- a nominal 400x240 pattern shifted down one row with the row-0 wrap is
  returned to nominal by `auto` (pixel equality; the synthetic's last two
  nominal rows are equal so the documented duplicate is exact);
- an already-nominal image is not corrected, and a second pass is a no-op;
- a flat/uniform image is not flagged;
- a 256x192 native frame is never corrected;
- `off` leaves an Azahar image unchanged;
- a 400x480 correction leaves the bottom screen unchanged;
- both EQ-4o hardware crops are detected as no-wrap and byte-unchanged
  (`HW_CROPS`; skipped with a labelled reason if PIL is missing).

`tools/test_eq3_compare.py` pins the committed Azahar fixture: `off` reproduces
`ours bbox=(78,43)-(124,112) centroid=(104.69,76.97)`; `on` gives
`ours bbox=(77,42)-(124,112) centroid=(104.68,76.22)`.
`tools/test_eq4_hw_compare.py` pins the real scene5 pair: `off` gives
`ref_bbox_top=28 bbox_dev_top=2`; `auto` gives `ref_bbox_top=27 bbox_dev_top=1
bbox_dev_bottom=0`, both PASS. `tools/ds_edge_test/test_measure_rows.py` pins
the synthetic shifted capture: `off` reads `row_offset≈+0.8`, `auto` reads
`≈0.0` and prints the applied line.

HEAD at time of run: `293e0f6`.

```
$ make test-azahar-presentation
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_azahar_presentation.py'
.......
----------------------------------------------------------------------
Ran 7 tests in 0.109s

OK
```

```
$ make test > /tmp/make-test-eq4p-final.log 2>&1; rc=$?; echo "make_test_exit=$rc"; tail -20 /tmp/make-test-eq4p-final.log
make_test_exit=0
----------------------------------------------------------------------
Ran 7 tests in 0.109s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
.....ssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 405 tests in 79.744s

OK (skipped=80)
/tmp/tmpdva64b0e/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

No existing assertion was edited: the test diff is new methods/helpers only
(`git diff main -- tools/test_*.py tools/ds_edge_test/test_measure_rows.py`).

## 7. UNVERIFIED

- **Other consoles and other capture sessions.** EQ-4n/EQ-4o are one console,
  one capture session per frame; the constants and the guard are scoped to
  that basis.
- **Non-axis-aligned or textured geometry.** EQ-4m/EQ-4n measured the
  axis-aligned opaque `ds_edge_test` scene. The committed cube fixture is the
  only Azahar picture used here; its correction changes structural numbers but
  the scene is not the edge ROM.
- **Whether the wrap signature occurs with other Azahar versions.** EQ-4n
  matched one core build's arithmetic; no other version was run.
- **The `+1` constant is measured only for 400x240 top-screen output.** A
  400x480 PPM is corrected on its upper 400x240 rows only; the bottom-screen
  presentation is not characterised.
- **Guard residual:** a hardware (or other) 400x240 image whose rows 0 and 239
  are both uniform background can satisfy the shifted-equality clause; the
  `row0 != row239` clause and the EQ-4o fixtures (`397/399`, `79/399`) narrow
  it but do not eliminate it.
- **The cube fixture's content is not one capture row below DeSmuME on its
  top edge** (§4.1): the correction is applied because the guard fires, and
  the verdict stays PASS, but the sign of that delta is not explained here.

## 8. Privacy and diff budget (FACT)

```
$ git rev-parse --short HEAD
af62411
$ git add -N tools/azahar_presentation.py tools/test_azahar_presentation.py docs/reports/eq4p-presentation-row-correction.md
$ git diff main | grep -cE '/(home|Users)/'
0
$ git diff main --name-only
Makefile
docs/EQ4_HW_COMPARE.md
docs/EQUALITY_CHECK_DESIGN.md
docs/reports/eq4o-hw-row-shift.md
docs/reports/eq4p-presentation-row-correction.md
tools/azahar_presentation.py
tools/ds_edge_test/measure_rows.py
tools/ds_edge_test/test_measure_rows.py
tools/eq3_compare.py
tools/eq4_hw_compare.py
tools/eq4g_fit_compare.py
tools/eq_scenes.py
tools/test_azahar_presentation.py
tools/test_eq3_compare.py
tools/test_eq4_hw_compare.py
$ git diff main | wc -c
87460
```

No production directory and no `tools/azahar_host/` file is in the diff. Every
analysis, render and quoted command ran as a script file or a tool CLI; no
inline `python3 -c` produced any number in this report. The quoted commands
contain no home path.

## 9. Requirement traceability

| User-task requirement | Where |
|---|---|
| 1. One shared helper module + constants + guard + correction + unit tests | `tools/azahar_presentation.py`, `tools/test_azahar_presentation.py`, §3, §6 |
| 2. Flag integration with the visible line; tests keep old numbers and show corrected real-fixture numbers | §4, §6, changed tools listed in §2 |
| 3. Report: changed/skipped tools and reasons, quoted output off/on, before/after key numbers, labels, UNVERIFIED, append-only pointers | §2, §4, §5, §7; `docs/EQ4_HW_COMPARE.md`, `docs/EQUALITY_CHECK_DESIGN.md`, `docs/reports/eq4o-hw-row-shift.md` §7 |
| 4. Wire tests into `make test`, no home paths, no weakened tests, no `python3 -c`, one run per quote, HEAD, diff budget | §6, §8, `Makefile` |
