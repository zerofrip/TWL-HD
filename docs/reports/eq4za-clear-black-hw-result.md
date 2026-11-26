<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4za — black clear on real hardware: arm11 scene 4 against the DS
**Status:** REAL HARDWARE MEASURED (offline analysis of the owner's captures). **Basis:** ONE console, ONE capture session per frame by a passive capture board through `cc3dsfs` (1280x720 root screenshot, top-left 400x240 crop), ONE arm11 3dsx (`twlhd-arm11.3dsx`, built from `main` `bd58fab`, NOT rebuilt), ONE scene (`SCENE=4`, `STATIC_ANGLE=30`, DS mode), each frame captured 3 times with byte-identical crops; no console access in this task.

```
$ git rev-parse HEAD
bd58fab8a63ecec59a24012a4dc197ba6e170a5d
```
## 1. Question and inputs
EQ-4z's black clear left a console PLAN (its §7, not run); this records it.

| Input | sha256 | Where |
|---|---|---|
| non-blend crop | `1b6c9aaa99265601d1f35f27d9025f33d663f75bcff40eef6b31ce7fb68568f8` | `<staging>/hw/hw_eq4z_nb_a.png` → `tools/eq4za_clear_hw/hw_eq4za_nb_a.png` |
| blend crop | `17245fa9ab9ff5ef129cf082af9a899e186b3a30dd23ebb4c148d78a145bcf3c` | `<staging>/hw/hw_eq4z_bl_a.png` → `tools/eq4za_clear_hw/hw_eq4za_bl_a.png` |
| `frame_s4_noblend.bin` | `2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e` | RSTA `0x04000060 = 0x10` |
| `frame_s4_blend.bin` | `ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5` | RSTA `0x04000060 = 0x18` |
| `twlhd-arm11.3dsx` | `8e7e02f60729316a40cfd80d1cc6a99c8ee65deb4ae8c0f1d29eebcc5cc16fbc` | built from `main` `bd58fab`, not rebuilt |

The build ID embeds the short HEAD (`arm11/Makefile:75-84`). Each frame was loaded from SD as `sdmc:/twlhd_frame.bin` with the A button (`docs/reports/eq4n-arm11-row-shift.md` §4); the SD file was backed up and restored owner-side.
## 2. Method: reuse `measure_blend_hw.py` unchanged
The tool is unmodified: blend classification is source-relative (`measure_blend_hw.py:269`), the alpha-0 census is an absolute bright rule (`:50`, `:152`), and the colour census is unconditional (`:163-169`); there is no `(0,0,0)` background assumption, so it takes these captures directly.
## 3. Fixture integrity (FACT)
```
$ sha256sum tools/eq4za_clear_hw/*.png
17245fa9ab9ff5ef129cf082af9a899e186b3a30dd23ebb4c148d78a145bcf3c  tools/eq4za_clear_hw/hw_eq4za_bl_a.png
1b6c9aaa99265601d1f35f27d9025f33d663f75bcff40eef6b31ce7fb68568f8  tools/eq4za_clear_hw/hw_eq4za_nb_a.png
$ cmp <staging>/hw/hw_eq4z_nb_a.png tools/eq4za_clear_hw/hw_eq4za_nb_a.png && echo cmp_nb_ok
cmp_nb_ok
$ cmp <staging>/hw/hw_eq4z_bl_a.png tools/eq4za_clear_hw/hw_eq4za_bl_a.png && echo cmp_bl_ok
cmp_bl_ok
$ du -b tools/eq4za_clear_hw/*.png
806	tools/eq4za_clear_hw/hw_eq4za_bl_a.png
787	tools/eq4za_clear_hw/hw_eq4za_nb_a.png
```
## 4. Tool runs (FACT)
Each command was run once with its output redirected to a log, then `cat`ted.

**4.1 arm11 now, non-blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4za_clear_hw/hw_eq4za_nb_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,255,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=255
quad1.blended=no
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,255
quad2.source_channel=2
quad2.source_value=255
quad2.measured=255
quad2.blended=no
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=12
quad3.bright_bbox=231,112,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=1
census_top=16
census=(0, 0, 0) 92968
census=(0, 0, 255) 1407
census=(255, 0, 0) 982
census=(0, 255, 0) 642
census=(16, 0, 0) 1
```

**4.2 arm11 now, blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4za_clear_hw/hw_eq4za_bl_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,138,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=138
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,41
quad2.source_channel=2
quad2.source_value=255
quad2.measured=41
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=12
quad3.bright_bbox=231,112,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=1
census_top=16
census=(0, 0, 0) 92968
census=(0, 0, 41) 1407
census=(255, 0, 0) 982
census=(0, 138, 0) 642
census=(16, 0, 0) 1
```

**4.3 arm11 before (EQ-4y), non-blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_nb_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,255,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=255
quad1.blended=no
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,255
quad2.source_channel=2
quad2.source_value=255
quad2.measured=255
quad2.blended=no
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=12
quad3.bright_bbox=231,112,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=1
census_top=16
census=(16, 16, 24) 92969
census=(0, 0, 255) 1407
census=(255, 0, 0) 982
census=(0, 255, 0) 642
```

**4.4 arm11 before (EQ-4y), blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_bl_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,142,8
quad1.source_channel=1
quad1.source_value=255
quad1.measured=142
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=8,12,57
quad2.source_channel=2
quad2.source_value=255
quad2.measured=57
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=16,16,24
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=12
quad3.bright_bbox=231,112,237,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=1
census_top=16
census=(16, 16, 24) 92969
census=(8, 12, 57) 1407
census=(255, 0, 0) 982
census=(0, 142, 8) 642
```

**4.5 DS reference, non-blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,255,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=255
quad1.blended=no
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,255
quad2.source_channel=2
quad2.source_value=255
quad2.measured=255
quad2.blended=no
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=138
quad3.bright_bbox=207,80,240,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=87
census_top=16
census=(0, 0, 0) 92056
census=(0, 0, 255) 1372
census=(255, 0, 0) 892
census=(0, 255, 0) 560
census=(0, 4, 0) 152
census=(0, 0, 247) 53
census=(0, 0, 99) 39
census=(0, 247, 0) 37
census=(0, 0, 8) 33
census=(0, 251, 0) 29
census=(0, 215, 0) 24
census=(247, 0, 0) 24
census=(0, 8, 0) 23
census=(99, 0, 0) 21
census=(0, 0, 214) 19
census=(0, 0, 41) 17
```

**4.6 DS reference, blend**
```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4w_blend_hw/hw_s4bl_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
projection=perspective(70,1.333333,0.1,100) look_at=0,0,4 rotate_x=2730 rotate_y=2730 scale=2 viewport=0,0,255,191
derivation=clip=P*LookAt*Rx*Ry*Scale(2); ndc=clip.xy/clip.w; ds_x=(ndc_x+1)*128; ds_y_top=(1-ndc_y)*96; capture_x=40+1.25*ds_x; capture_y=1.25*ds_y_top
quad0.source_rgb=255,0,0 alpha=31
quad0.rect=150.885,125.766,193.268,162.786
quad0.interior=153,128,191,161
quad0.majority_rgb=255,0,0
quad0.source_channel=0
quad0.source_value=255
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.rect=206.478,112.166,238.177,144.043
quad1.interior=208,114,236,142
quad1.majority_rgb=0,134,0
quad1.source_channel=1
quad1.source_value=255
quad1.measured=134
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.rect=143.928,89.476,192.503,130.364
quad2.interior=146,91,191,128
quad2.majority_rgb=0,0,33
quad2.source_channel=2
quad2.source_value=255
quad2.measured=33
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.rect=207.183,79.642,242.251,114.336
quad3.interior=209,82,240,112
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_rule=max_rgb_gt_60
quad3.region=207,80,242,114 region_pixels=1260
quad3.bright_pixels=130
quad3.bright_bbox=207,80,240,114
quad3.interior=209,82,240,112
quad3.interior_pixels=992
quad3.interior_bright_pixels=84
census_top=16
census=(0, 0, 0) 92151
census=(0, 0, 33) 1348
census=(255, 0, 0) 892
census=(0, 134, 0) 468
census=(0, 4, 0) 144
census=(0, 0, 41) 87
census=(0, 0, 8) 64
census=(0, 130, 0) 64
census=(0, 146, 0) 37
census=(0, 0, 24) 36
census=(0, 0, 16) 29
census=(247, 0, 0) 24
census=(99, 0, 0) 21
census=(0, 138, 0) 19
census=(0, 113, 0) 18
census=(0, 142, 0) 15
```
## 5. Per-quad result
| Row | DS no-blend | arm11 before (eq4y) | arm11 now | DS blend | arm11 before | arm11 now |
|---|---|---|---|---|---|---|
| background | `(0,0,0)` 92056 | `(16,16,24)` 92969 | `(0,0,0)` 92968 | `(0,0,0)` 92151 | `(16,16,24)` 92969 | `(0,0,0)` 92968 |
| quad1 green | G=255 | G=255 | G=255 | G=134 | G=142 | G=138 |
| quad2 blue | B=255 | B=255 | B=255 | B=33 | B=57 | B=41 |
| census | `(0,0,0)` 92056, B255 1372, R255 892, G255 560 | `(16,16,24)` 92969, B255 1407, R255 982, G255 642 | `(0,0,0)` 92968, B255 1407, R255 982, G255 642, `(16,0,0)` 1 | `(0,0,0)` 92151, B33 1348, R255 892, G134 468 | `(16,16,24)` 92969, B57 1407, R255 982, G142 642 | `(0,0,0)` 92968, B41 1407, R255 982, G138 642, `(16,0,0)` 1 |

Toward the DS: background is now `(0,0,0)` exactly (both captures, 92968), green 142 → 138 vs DS 134, blue 57 → 41 vs DS 33. Not closed: green gap 4, blue gap 8; EQ-4z predicted green 135 / blue 40 (`docs/reports/eq4z-clear-black.md:484-489`, `:861`).
## 6. The single stray pixel `(16,0,0)`
Present once in each new capture, absent from both EQ-4y censuses.

```
$ python3 - <<'PY' > <logfile> 2>&1
from PIL import Image
for path in ("tools/eq4za_clear_hw/hw_eq4za_nb_a.png",
             "tools/eq4za_clear_hw/hw_eq4za_bl_a.png"):
    px = Image.open(path).convert("RGB").load()
    hits = [(x, y) for y in range(240) for x in range(400)
            if px[x, y] == (16, 0, 0)]
    print("%s (16,0,0) count=%d positions=%s" % (path, len(hits), hits))
PY
$ cat <logfile>
tools/eq4za_clear_hw/hw_eq4za_nb_a.png (16,0,0) count=1 positions=[(0, 239)]
tools/eq4za_clear_hw/hw_eq4za_bl_a.png (16,0,0) count=1 positions=[(0, 239)]
```

The pixel is at `(0, 239)`, the bottom-left corner, in both captures. HYPOTHESIS: a capture-path or scanout edge artefact; origin not investigated.
## 7. The alpha-0 quad
Measured numbers only, same derivation as EQ-4y §7/§8:

| Capture | Region bright / region | Interior bright / interior | Bright bbox |
|---|---|---|---|
| arm11 non-blend (§4.1) | 12 / 1260 | 1 / 992 | 231,112,237,114 |
| arm11 blend (§4.2) | 12 / 1260 | 1 / 992 | 231,112,237,114 |
| DS non-blend (§4.5) | 138 / 1260 | 87 / 992 | 207,80,240,114 |
| DS blend (§4.6) | 130 / 1260 | 84 / 992 | 207,80,240,114 |
## 8. Verdict
**YES.** On ONE console, in ONE capture session per frame, for ONE scene (`SCENE=4`, `STATIC_ANGLE=30`) and one arm11 3dsx built from `main` `bd58fab`, the black clear works on hardware: background `(0,0,0)` exactly (92968 pixels, both captures), the non-blend frame paints the alpha-16/4 quads opaque (`255`/`255`), and the blend frame dims them to green 138 / blue 41. The remaining blend gaps (4 / 8) and the alpha-0 outline are not clear-colour failures.
## 9. UNVERIFIED
- Other consoles, capture sessions, frames, programs, angles.
- The remaining blend-gap origin: EQ-4i per-channel output quantisation is a HYPOTHESIS (eq4y §10(a), `docs/reports/eq4y-gate-hw-result.md:360-371`).
- Whether the capture path alters colours; not separately instrumented.
- The stray `(16,0,0)` pixel origin (§6, HYPOTHESIS only).
## 10. Consequence (text only, NOT applied)
Next candidates: (a) blend output quantisation, green to 6 bits and red/blue to 5 bits after blending, per eq4y §10(a); (b) the alpha-0 outline (eq4u §6.2), orthogonal and expensive. Recommendation: take (a) first — self-contained in the clip draw path, and the black background has removed the larger term, leaving quantisation as the main measurable blend difference; schedule (b) separately.
**EQ-4zb (2026-11-26):** offline model of this residual (no production code change): no source-derived candidate reproduces the console 138 / 41, and the quantisation term explains at most 1 of the 4 green / 7 of the 8 blue levels — see `docs/reports/eq4zb-blend-residual.md` and `tools/eq4zb_blend_model/`.
## 11. Tool, tests and audit
Fixtures/test `tools/eq4za_clear_hw/`, wired into `make test`; tests pin both sha256s and §4.1/§4.2 (incl. `(16,0,0) 1`), and the old `(16,16,24)` EQ-4y crop is the negative (not black background).

```
$ make test-eq4za-clear-hw > <logfile> 2>&1; echo "exit=$?"; tail -4 <logfile>
exit=0
----------------------------------------------------------------------
Ran 5 tests in 0.356s

OK
$ git add -N . && git diff main | wc -c
35330
$ wc -l docs/reports/eq4za-clear-black-hw-result.md
449 docs/reports/eq4za-clear-black-hw-result.md
$ make test > <logfile> 2>&1; echo "exit=$?"; grep -E '^(Ran 435 tests|OK \(skipped=110\))' <logfile>
exit=0
Ran 435 tests in 81.838s
OK (skipped=110)
```
Hygiene: no console, ROM, emulator or capture tool; no `python3 -c`; no absolute staging/home path or absolute log path is written to the repository.
## 12. Extra runs disclosed
The 6 §4 runs were each run once. The committed test invokes the tool on both new crops and the old EQ-4y crop; `make test-eq4za-clear-hw` was run twice and `make test` once, each re-running that suite (extra runs). `sha256sum`, `cmp`, `du -b` and the stray-pixel heredoc ran once each.
