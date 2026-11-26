<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4y — blend gate on real hardware: arm11 gates alpha blending on the recorded `DISP3DCNT` bit 3

**Status:** REAL HARDWARE MEASURED (offline analysis of the owner's captures).

**Basis:** ONE New 3DS console, ONE capture session per frame, ONE arm11 3dsx
(`twlhd-arm11.3dsx`, built from `main` `2ce38d0`), ONE scene (`SCENE=4`,
`STATIC_ANGLE=30`, DS mode). Nothing here generalises to another console,
session, frame, program, angle or a mid-frame blend toggle. Offline analysis
only; no console access in this task.

Worktree HEAD for every command below:

```
$ git rev-parse HEAD
2ce38d0708ed3c134737ef0eb91a280039a91fc9
```

---

## 1. Question and inputs

EQ-4x gated arm11 alpha blending on the recorded `DISP3DCNT` bit 3
(`arm11/include/twlhd_blend_gate.h`, `arm11/source/renderer.c`) and predicted,
as a PLAN (§7, not run), what the new 3dsx would show on a real New 3DS for the
two scene-4 static-angle-30 frames. The owner has now run it. This report
records the result against that PLAN.

Inputs (owner, outside the repo; staging path shown as `<staging>`, never
written to the repository):

| Input | sha256 | Where |
|---|---|---|
| non-blend crop `hw_eq4x_nb_a.png` | `deb38c18a5bb996062666032f028f85285082e1784f2fc6d1b2b430253ee4994` | committed `tools/eq4y_gate_hw/hw_eq4x_nb_a.png` |
| blend crop `hw_eq4x_bl_a.png` | `b69fe759b2ea71e5478c161e62ab91e66e43cc753083ded41f687ee33cbc48de` | committed `tools/eq4y_gate_hw/hw_eq4x_bl_a.png` |
| frame `frame_s4_noblend.bin` | `2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e` | recorded `twlhd_frame_v2.bin`, RSTA `0x04000060 = 0x10` |
| frame `frame_s4_blend.bin` | `ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5` | recorded `twlhd_frame_v2.bin`, RSTA `0x04000060 = 0x18` |
| arm11 program `twlhd-arm11.3dsx` | `738b5c1772e6c40cb25a67ed1c923eac98e450f5fbea6a07768bb7f676b62f60` | built from `main` `2ce38d0` |

**Stated plainly:** the run 3dsx (`738b5c17…`) is **NOT** the 3dsx hashed in
EQ-4x §4 (`2dc7d386…`, built at HEAD `a0eea169…`); it was built from `main`
`2ce38d0`, and the build ID embeds the short HEAD (`arm11/Makefile:75-84`). It
was not rebuilt in this task. Each frame was loaded from SD as
`sdmc:/twlhd_frame.bin` with the A button (`docs/reports/eq4n-arm11-row-shift.md`
§4); the SD file originally there was backed up and restored owner-side.

FACT — the frame-bin and 3dsx hashes (raw `sha256sum`, redacted):

```
$ sha256sum "$HWSTAGE"/frame_s4_noblend.bin "$HWSTAGE"/frame_s4_blend.bin "$HWSTAGE"/twlhd-arm11.3dsx | sed "s|$HWSTAGE|<staging>|"
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  <staging>/frame_s4_noblend.bin
ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5  <staging>/frame_s4_blend.bin
738b5c1772e6c40cb25a67ed1c923eac98e450f5fbea6a07768bb7f676b62f60  <staging>/twlhd-arm11.3dsx
```

Provenance (OWNER-REPORTED run, offline analysis only): one New 3DS console;
each frame captured in its own session by a passive capture board through
`cc3dsfs` as a 1280x720 root screenshot, cropped to the top-left 400x240; each
frame was captured 3 times and the three crops were byte-identical (nothing
else varied).

---

## 2. Method: reuse `measure_blend_hw.py` unchanged

The measurement tool is the unmodified
`tools/eq4w_blend_hw/measure_blend_hw.py` (EQ-4w §2, cited; not re-derived
here). No change was needed: its blend classification is source-relative
(`measured < 255` on the quad's source channel, `measure_blend_hw.py:269`), the
alpha-0 census is the absolute bright rule `max(r,g,b) > 60` (`:50`, `:152`),
and the colour census is unconditional (`:163-169`). There is **no** `(0,0,0)`
background assumption, so the tool takes these arm11 captures (which clear to
`(16,16,24)`, §10(b)) directly, as is.

---

## 3. Fixture integrity (FACT)

```
$ HWSTAGE=<staging> sha256sum "$HWSTAGE"/hw/hw_eq4x_nb_a.png "$HWSTAGE"/hw/hw_eq4x_bl_a.png tools/eq4y_gate_hw/hw_eq4x_nb_a.png tools/eq4y_gate_hw/hw_eq4x_bl_a.png | sed "s|$HWSTAGE|<staging>|"
deb38c18a5bb996062666032f028f85285082e1784f2fc6d1b2b430253ee4994  <staging>/hw/hw_eq4x_nb_a.png
b69fe759b2ea71e5478c161e62ab91e66e43cc753083ded41f687ee33cbc48de  <staging>/hw/hw_eq4x_bl_a.png
deb38c18a5bb996062666032f028f85285082e1784f2fc6d1b2b430253ee4994  tools/eq4y_gate_hw/hw_eq4x_nb_a.png
b69fe759b2ea71e5478c161e62ab91e66e43cc753083ded41f687ee33cbc48de  tools/eq4y_gate_hw/hw_eq4x_bl_a.png
$ cmp "$HWSTAGE"/hw/hw_eq4x_nb_a.png tools/eq4y_gate_hw/hw_eq4x_nb_a.png && echo "nb identical"
nb identical
$ cmp "$HWSTAGE"/hw/hw_eq4x_bl_a.png tools/eq4y_gate_hw/hw_eq4x_bl_a.png && echo "bl identical"
bl identical
```

The two committed fixtures are byte-identical to the staging copies; each is
`du -b` 1167 and 1175 bytes (both below 100 KB, §11).

---

## 4. RSTA premise (FACT)

The two frame bins are the recorded `twlhd_frame_v2.bin` inputs; their
`0x04000060` RSTA entry carries the blend-enable bit the gate reads. Dump with
`tools/twlhd_v2_dump.parse` (EQ-4x §5.2 pattern):

```
$ HWSTAGE=<staging> python3 - <<'EOF'
import os, sys
sys.path.insert(0, 'tools')
from twlhd_v2_dump import parse

stage = os.environ['HWSTAGE']
def dump(name):
    path = os.path.join(stage, name)
    with open(path, 'rb') as f:
        data = f.read()
    view = parse(data)
    print(f'{name}: rsta_count={len(view.rsta)} gxcm_count={len(view.gxcm)}')
    for addr, value, gx_index, width in view.rsta:
        if addr == 0x04000060:
            bit3 = (value >> 3) & 1
            print(f'  addr=0x{addr:08x} value=0x{value:08x} bit3={bit3} gx_index={gx_index} width={width}')

dump('frame_s4_noblend.bin')
dump('frame_s4_blend.bin')
EOF
frame_s4_noblend.bin: rsta_count=4 gxcm_count=121
  addr=0x04000060 value=0x00000010 bit3=0 gx_index=0 width=2
frame_s4_blend.bin: rsta_count=5 gxcm_count=121
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
```

Non-blend frame: bit 3 clear (`0x10`). Blend frame: bit 3 set (`0x18`). This
matches the owner-reported RSTA premise and EQ-4x §4/§5.2.

---

## 5. Tool runs (FACT)

### 5.1 Non-blend capture

Full raw output, run once:

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_nb_a.png
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

### 5.2 Blend capture

Full raw output, run once:

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_bl_a.png
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

The top census lines reproduce the owner's preliminary census exactly
(OWNER-REPORTED: no-blend `(16,16,24) 92969`, `(0,0,255) 1407`, `(255,0,0) 982`,
`(0,255,0) 642`; blend `(16,16,24) 92969`, `(8,12,57) 1407`, `(255,0,0) 982`,
`(0,142,8) 642`). The tool was run on each fixture once for these FACTs.

---

## 6. Per-quad result

DS hardware numbers are cited from EQ-4w §3/§4 (`134`/`33` blend,
`255`/`255` non-blend, alpha-0 bright `130`/`138`); the emulator column is
cited from EQ-4x §5.5 (non-blend `255`/`255`, blend `142`/`60`), not re-run.
arm11 hardware numbers are the §5 measurements.

| Quad | Alpha | DS hw non-blend | arm11 hw non-blend | DS hw blend | arm11 hw blend | emulator (EQ-4x §5: nb / bl) |
|---|---|---|---|---|---|---|
| 0 (red) | 31 | R=255 | R=255 | R=255 | R=255 | 255 / 255 |
| 1 (green) | 16 | G=255 | G=255 | G=134 | G=142 | 255 / 142 |
| 2 (blue) | 4 | B=255 | B=255 | B=33 | B=57 | 255 / 60 |
| 3 (white) | 0 | outline, 138 bright | 12 bright | outline, 130 bright | 12 bright | 5 / 5 bright |

**What matches (stated plainly).** arm11 non-blend: the alpha-16 green and
alpha-4 blue quads read full `255` and `blended=no`, exactly like the DS
non-blend capture (`255`/`255`). The EQ-4x §7 prediction for (a) — "alpha-16
green `(0,255,0)`, alpha-4 blue `(0,0,255)`, `blended=no`" — is reproduced on
hardware.

**What does not match.** arm11 blend dims the quads to green 142 / blue 57,
but the DS blend capture reads green 134 / blue 33. The arm11 blend values are
closer to the EQ-4x emulator render (142/60) than to the DS hardware (134/33);
the 142/57-vs-134/33 gap is the blend-math difference analysed in §10(a)/(b). The
background is `(16,16,24)` on arm11 (both captures) against the DS `(0,0,0)`
(eq4f §5(iii), eq4r §7; §10(b)).

---

## 7. The alpha-0 quad

Quad 3 is alpha 0 (source white). The tool censuses its derived region with the
bright rule `max(r,g,b) > 60` (§2). Measured numbers only:

| Capture | Region bright | Interior bright / interior |
|---|---|---|
| arm11 non-blend | 12 | 1 / 992 |
| arm11 blend | 12 | 1 / 992 |
| DS non-blend (EQ-4w §4) | 138 | 87 / 992 |
| DS blend (EQ-4w §3) | 130 | 84 / 992 |

Measured on arm11: 12/1260 bright region pixels and 1/992 interior bright
pixels, identical in both captures; the 12 bright region pixels form the small
corner box `231,112,237,114`. That box lies inside
quad1's rect `206.478,112.166,238.177,144.043` (§5.1/§5.2), where the alpha-16
green quad's edge passes — HYPOTHESIS: the 12 pixels are edge spill from quad1,
not the filled outline the DS draws (130/138 bright, bbox `207,80,240,114`).
HYPOTHESIS: on the opaque path the EQ-4x alpha-test override discards alpha-0
fragments, and on the blend path alpha 0 blends to the clear colour, so the
arm11 quad is not drawn either way. The emulator showed `quad3.bright_pixels=5`
(`interior_bright_pixels=0`); hardware shows 12 (`interior_bright_pixels=1`).
The small difference is not investigated and its origin is UNVERIFIED.

---

## 8. Verdict

**YES.** On ONE console, in ONE capture session per frame, for ONE scene
(`SCENE=4`, `STATIC_ANGLE=30`) and one arm11 3dsx (built from `main`
`2ce38d0`), the gate works: the non-blend frame (bit 3 clear) paints the
alpha-16/4 quads opaque (`255`/`255`, matching the DS), the blend frame (bit 3
set) dims them (`142`/`57`), and the alpha-0 quad region shows the measured
12/1260 bright region pixels and 1/992 interior bright pixels in both
(HYPOTHESIS: edge spill from quad1, not a drawn outline) — all as EQ-4x §7
predicted for the gating. The
residual arm11-vs-DS blend difference (`142`/`57` vs
`134`/`33`) is the pre-existing blend-math gap (§10(a)/(b)), not a gate failure.

---

## 9. UNVERIFIED

- Other consoles, other capture sessions, other frames, other programs.
- Mid-frame `DISP3DCNT` bit-3 toggles (the gate reads the last write at or
  before the first draw; EQ-4x §1).
- The origin of the blend-math difference (`142`/`57` vs `134`/`33`): the
  6-bit G / 5-bit R/B quantisation split (EQ-4i §5.5) is a HYPOTHESIS only.
- Whether the capture path alters colours: this run uses the same board and
  settings as before (SOURCE-READ from EQ-4r §2 lines 133-138: `--enabled_both 0
  --enabled_top 1 --scaling_top 1`, 24-bpp decoder), so a path-dependent colour
  shift is not excluded but was not separately instrumented.
- The small alpha-0 spill difference (12 vs the emulator's 5 bright pixels).

---

## 10. Consequence (text only, NOT applied)

### (a) Blend-math gap: model EQ-4i's per-channel convention

arm11 blends alpha-16 to green 142 and alpha-4 to blue 57 over the `(16,16,24)`
clear colour; the DS blends to green 134 and blue 33 over `(0,0,0)`. Our
interpreter already uses the A2 blend **weight** `(a+1)/32` — a 5-bit-alpha
weight, not a 6-bit A2 value — SOURCE-READ, verbatim:

```
$ sed -n '395,397p' common/twlhd_gx_interp.c
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}
```

The float weight is applied per BEGIN group at `common/twlhd_gx_interp.c:1074`
(`g->rgba[3] = gx_alpha_float(g->applied_alpha)`), carried as `rgba[3]`
(`common/twlhd_gx_interp.h:25`), blended `GPU_SRC_ALPHA` /
`GPU_ONE_MINUS_SRC_ALPHA` (`arm11/include/twlhd_blend_config.h:30-31`).

What our PICA path **models**: the A2 weight convention `(a+1)/32`, which is
what EQ-4i's green-channel verdict selects on hardware (EQ-4i §5.1, FACT
there). What it does **not** model: EQ-4i's per-channel **output** convention —
the DS green channel is the 6-bit A2 value (`round(v6*255/63)`), while red and
blue are the 5-bit quantisation of that same 6-bit value (`expand5(v6>>1)`),
EQ-4i §5.1/§5.2 (FACT there, cited). Whether EQ-4i's 6-bit integer pipeline
(`model_6bit_rounding=blend floor`, EQ-4i :299) is exactly reproduced by this
float weight cannot be established from the quoted lines — UNVERIFIED. To match
the DS, arm11's blend output would have to quantise green to 6 bits and
red/blue to 5 bits after blending — i.e. a per-channel output quantisation in
the clip path, not a change to the A2 weight (which the quoted lines show is
already the `(a+1)/32` convention).

The split between the two terms is HYPOTHESIS (§10(b)'s arithmetic over the §5
FACTs): the background `(16,16,24)` is the larger single term, accounting for
almost the whole green gap (measured 142 vs DS 134) and most of the blue gap
(measured 57 vs DS 33); the per-channel output quantisation above addresses the
residual — green ~135 over black sits within one level of the DS 134, and blue
~40 over black leaves ~7 levels for 5-bit R/B.

Verification on hardware: rebuild the modified arm11 3dsx, load the recorded
`frame_s4_blend.bin` (sha256 `ca471235…`, §1 table) as
`sdmc:/twlhd_frame.bin` per eq4n §4, capture with the same board and settings
(EQ-4r §2 lines 133–138), run `measure_blend_hw.py`, and compare against the DS
`134`/`33` and the eq4i / `tools/ds_alpha_test/expected.py` expectations (6-bit
A2 green, 5-bit R/B). Size: moderate — one output-quantisation step in the clip
draw path plus new goldens; it does not touch the public vertex-alpha stream.

### (b) Background clear colour `(16,16,24)` vs DS `(0,0,0)`

SOURCE-READ — the clear colour is set in `arm11/source/renderer.c:41` and used
at `:1195` (DEBUG path) and `:1543` (clip path):

```
#define RENDER_CLEAR_COLOR 0x101018FFu
```

The arm11 top screen clears to `(16,16,24)` (the §5 censuses, 92969 pixels),
where the DS clears to `(0,0,0)` (eq4f §5(iii), eq4r §7; `docs/EQ3_COMPARE.md:103-106`
documents the same and accepts both as the background class). Per the
arithmetic written out below, this background accounts for almost all of the
green gap (arm11 142 vs DS 134) and most of the blue gap (arm11 57 vs DS 33, ~20
of the 24 levels); the residual belongs to the per-channel output quantisation
(§10(a)). The split between the background and quantisation contributions is
HYPOTHESIS (eq4i §5.1/§5.2 for the quantisation, eq4u §4.4 for the scene-4
background account).

HYPOTHESIS — the size of the background contribution, derived over the FACT
inputs (weight `(a+1)/32` from the §10(a) source line, background `(16,16,24)`
and the measured `142`/`57` from §5.2) with the arithmetic written out here (not
a measurement): green `17/32·255 + 15/32·16 ≈ 143` ≈ measured 142, and over
black `17/32·255 ≈ 135`; blue `5/32·255 + 27/32·24 ≈ 60` ≈ measured 57-ish, and
over black `5/32·255 ≈ 40`. The background therefore does **not** fully explain
the blue 57-vs-33 gap (over-black ≈40 ≠ the DS 33); the green over-black ≈135
sits within one level of the DS 134. This is arithmetic, not a measurement.

**Risk of changing it:** every existing golden/eq3/eq4 baseline
pins `(16,16,24)` as our clear colour (`docs/EQ3_COMPARE.md:103-106` background
class; the eq4f/eq4r comparisons already treat it as a known divergence), so
changing `RENDER_CLEAR_COLOR` would ripple through all of them. Size: trivial
(one constant) but broad blast radius.

### (c) Alpha-0 outline

Unchanged by this result and by EQ-4x: the alpha-0 quad is not drawn on arm11
(12/1260 bright region pixels, 1/992 interior in both captures), and the DS
draws an outline. Sized in EQ-4u §6.2 (wireframe
flag + edge extraction + a `GPU_LINES` pass + new goldens + a hardware capture);
not a cheap follow-up and orthogonal to the blend gate.

**Ranking (value/risk).** By §10(b)'s arithmetic (the split itself HYPOTHESIS),
(b) is the larger single term and a one-line change, but its risk is the broad
blast radius: every existing golden/eq3/eq4 baseline pins `(16,16,24)` as our
clear colour (`docs/EQ3_COMPARE.md:103-106` background class; §10(b) risk
above), so changing `RENDER_CLEAR_COLOR` would ripple through all of them. (a)
addresses the smaller residual but is self-contained (clip-path only, no shared
clear colour or existing goldens touched). Because (b)'s stated blast radius is
higher than (a)'s, **recommend (a) first**, then (b) as a separate,
carefully-scoped decision; (c) is expensive and orthogonal.

**Update (2026-11-25):** EQ-4z implemented (b): the default clear colour is now `(0,0,0)` from `arm11/include/twlhd_render_config.h`, with an overridable `TWLHD_RENDER_CLEAR_COLOR` macro for diagnostic builds. Report: `docs/reports/eq4z-clear-black.md`.

---

## 11. Tool, tests and audit

Tool and fixtures: `tools/eq4y_gate_hw/` (`hw_eq4x_nb_a.png`,
`hw_eq4x_bl_a.png`, `README.md`, `test_eq4y_gate_hw.py`). The test suite pins
both fixture sha256s and the measured per-quad numbers (§5); the target is
wired into `make test` like `tools/eq4w_blend_hw`:

```
$ make test-eq4y-gate-hw
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4y_gate_hw -p 'test_*.py'
....
----------------------------------------------------------------------
Ran 4 tests in 0.284s

OK
```

**Hygiene.** No console, ROM, emulator or capture tool was run. No inline
Python expression evaluation (`python3 -c`) was used; the RSTA dump is a heredoc
over the committed `tools/twlhd_v2_dump.py` parser. The two fixture copies are
byte-identical to the owner staging files (§3); the absolute staging/home path
is never written to the repository. Diff and size audits are appended with the
final edit.

```
$ git add -N . && git diff main | wc -c
38510
$ du -b tools/eq4y_gate_hw/hw_eq4x_nb_a.png tools/eq4y_gate_hw/hw_eq4x_bl_a.png
1167	tools/eq4y_gate_hw/hw_eq4x_nb_a.png
1175	tools/eq4y_gate_hw/hw_eq4x_bl_a.png
```

The two fixtures are the `du -b` values above (each below 100 KB), and the diff
is below the 90000-byte cap. The `wc -c` number was measured with the rest of
the text in place, immediately before the number itself was written in; any
later edit changes it.

---

## 12. Extra runs disclosed

- `measure_blend_hw.py` was run once per fixture for the §5 FACTs; the
  `test_eq4y_gate_hw.py` suite invokes it on both fixtures (its own runs).
- `sha256sum` was run on the two committed fixtures (§3), on the two staging
  copies (§3) and on the frame bins + 3dsx (§1/README).
- `cmp` was run twice (§3). The RSTA dump heredoc was run once (§4).
- `sed -n '395,397p' common/twlhd_gx_interp.c` (SOURCE-READ, §10(a)) was run
  twice, and `git add -N . && git diff main | wc -c` (§11) three times: once for
  an earlier quote, once after the final text edits, and once more to confirm
  that the number quoted in §11 still holds with all text in place.
  No re-run of `measure_blend_hw.py` was needed — all §5 numbers already exist as
  FACTs.
- `make test-eq4w-blend-hw` (9 tests OK) and `make test` (430 tests OK,
  105 skipped) were each run once to confirm the new `test-eq4y-gate-hw`
  Makefile wiring leaves the existing suites untouched; both re-run the
  measurement tool internally on the committed fixtures (disclosed extra runs).
