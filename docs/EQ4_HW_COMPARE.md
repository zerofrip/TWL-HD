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
