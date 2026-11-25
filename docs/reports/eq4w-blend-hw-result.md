# EQ-4w — blend-enabled scene 4 on real hardware: the alpha quads dim only when `GL_BLEND` is enabled

**Basis:** ONE New 3DS console, ONE capture session (3 captures), ONE ROM
(`twlhd-arm9v2-s4-bl-st30.nds`), ONE scene (`SCENE=4 STATIC_ANGLE=30`, DS mode).
Nothing here generalises to another console, session, angle, scene or textured
geometry. Offline analysis only; no console access in this task.

Worktree HEAD for every command below: `021fc849ee9542318b8c6357c6d6c32fba552c10`.

```
$ git rev-parse HEAD
021fc849ee9542318b8c6357c6d6c32fba552c10
```

---

## 1. Question and inputs

EQ-4u §6.3 predicted it and EQ-4v built it: if the opaque alpha-16/alpha-4
quads in the EQ-4r scene-4 capture are caused by the ROM never enabling
`GL_BLEND`, then a ROM that differs only by `glEnable(GL_BLEND)` must dim those
quads while leaving the alpha-31 quad and the alpha-0 outline unchanged.

ROMS (EQ-4v §2/§3, cited, not rebuilt here):

| ROM | sha256 |
|---|---|
| `twlhd-arm9v2-s4-bl-st30.nds` (BLEND=1, run) | `e0aa3778c91ae0a06ecc895a92f3b6cb41fd8775158af579b24ebe4b678e6d29` |
| `twlhd-arm9v2-s4-st30.nds` (non-blend control) | `9662c06c7aeefa18807112c1d66b96003a567550a4a9bd18709b95ea75c80eba` |

The two ROMs differ only by the `BLEND=1` define and its single guarded source
line (`arm9v2/source/demo_frame.cpp:353-356`):

```
#ifdef TWLHD_DEMO_BLEND
    /* Falsification build (EQ-4u §6.3): enable DS alpha blending. */
    glEnable(GL_BLEND);
#endif
```

Captures (owner; the top-left 400x240 crops of 1280x720 root screenshots):

| Capture | sha256 | Where |
|---|---|---|
| blend `a` (comparison capture, EQ-4r a-priori rule) | `8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01` | committed `tools/eq4w_blend_hw/hw_s4bl_a.png` |
| blend `b` (noise comparison only) | `77d537869e0316e2eccfa931e7cb4d2636b19adbe6ab0a5c38e188047c87a9e6` | committed `tools/eq4w_blend_hw/hw_s4bl_b.png` |
| blend `c` | same hash as `a` (OWNER-REPORTED; not committed) | `<staging>/hw/hw_s4bl_c.png` |

The non-blend comparison is the committed EQ-4r fixture
`tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png`, captured in the EQ-4r
session (one console, one capture session per ROM). The committed `a`, `b` and
non-blend fixture hashes above are the raw output of:

```
$ sha256sum tools/eq4w_blend_hw/hw_s4bl_a.png tools/eq4w_blend_hw/hw_s4bl_b.png tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01  tools/eq4w_blend_hw/hw_s4bl_a.png
77d537869e0316e2eccfa931e7cb4d2636b19adbe6ab0a5c38e188047c87a9e6  tools/eq4w_blend_hw/hw_s4bl_b.png
ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9  tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
```

**OWNER-REPORTED:** capture `a` and capture `c` are byte-identical
(`a_c_diff=0`); `b` differs by 93 pixels inside the figure. The tool reproduces
the 93 (`diff_pixels=93` below); the `a` vs `c` equality is not re-checked here
because `c` is not committed. **The origin of the 93-pixel noise is
UNVERIFIED.**

---

## 2. Method: derive the quad rectangles from the source

The tool `tools/eq4w_blend_hw/measure_blend_hw.py` derives the four scene-4
quad rectangles from the source vertices and the demo's own transform chain.
No rectangle is hand-placed and no blend model is assumed.

Source (`arm9v2/source/demo_frame.cpp:250-259`): four quads in v16 20.12 fixed
point, so a vertex is `raw / 4096` world units, alphas `{31, 16, 4, 0}`,
colours `{red, green, blue, white}`. Transform chain
(`arm9v2/source/demo_frame.cpp:350-383`):

```
clip = P(70°, 256/192, 0.1, 100) · LookAt(0,0,4) · Rx(2730) · Ry(2730) · Scale(2)
ndc  = clip.xy / clip.w
ds_x = (ndc_x + 1) * 128              # viewport 0,0,255,191
ds_y_top = (1 - ndc_y) * 96
capture_x = 40 + 1.25 * ds_x          # capture_mapping.json
capture_y = 1.25 * ds_y_top
```

`2730` is `30 * 32768 / 360`, the `STATIC_ANGLE=30` binary angle. The
projection helpers are imported from `tools/gx_interp_oracle.py`
(`gl_perspective`, `gl_look_at`, `gl_rotate_x`, `gl_rotate_y`, `mat_mult`,
`mat_vec`); the mapping is read from
`tools/ds_vtxcolor_test/capture_mapping.json`. The rotated quads project to
trapezoids, so the region is the projected **bounding box** in capture pixels,
shrunk by 2 capture pixels per side ("interior"); the per-channel majority
(mode) over the interior is the measurement. A quad is `blended=yes` when the
majority of its source channel is below the source value 255.

The derived rectangles printed by the tool below agree with the independent
EQ-4r capture bounding box (`82..161 × 62..130` DS px = `142.5..241.25 ×
77.5..162.5` capture px; EQ-4r §5.9): the tool's union of the four rectangles is
`x 143.928..242.251`, `y 79.642..162.786`, i.e. within 2.1 capture px on each
side (the capture box includes antialiasing/outline spread, the projection does
not).

---

## 3. Tool run on the blend capture (FACT)

Full raw output of the committed tool on the committed blend fixture; the
`--diff` capture `b` is the second committed fixture.

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py --diff tools/eq4w_blend_hw/hw_s4bl_b.png tools/eq4w_blend_hw/hw_s4bl_a.png
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
diff_other=tools/eq4w_blend_hw/hw_s4bl_b.png
diff_pixels=93
diff_bbox=144,76,243,159
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

The top census lines reproduce the owner's blend-capture census exactly
(OWNER-REPORTED values: `(0,0,0) 92151`, `(0,0,33) 1348`, `(255,0,0) 892`,
`(0,134,0) 468`, `(0,4,0) 144`, `(0,0,41) 87`, `(0,0,8) 64`, `(0,130,0) 64`).
The tool's `diff_pixels=93` reproduces the owner's a/b count.

---

## 4. Tool run on the non-blend fixture (FACT)

Full raw output of the committed tool on the committed EQ-4r scene-4
non-blend capture.

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
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

The top census lines reproduce the owner's non-blend census exactly
(OWNER-REPORTED values: `(0,0,0) 92056`, `(0,0,255) 1372`, `(255,0,0) 892`,
`(0,255,0) 560`, `(0,4,0) 152`, `(0,0,247) 53`, `(0,0,99) 39`, `(0,247,0) 37`).

---

## 5. Per-quad result against the prediction

Prediction cited from EQ-4u §6.3, which quotes EQ-4h §2
(`python3 tools/ds_alpha_test/expected.py`, columns in variant order
`5bit_round,5bit_floor,8bit_round,8bit_floor,6bit_round`):

```
a= 4 A1=33/33/33/32/32 A2=41/33/40/39/36 sep=5bit_round=8/5bit_floor=0/8bit_round=7/8bit_floor=7/6bit_round=4
a=16 A1=132/132/132/131/130 A2=132/132/135/135/134 sep=5bit_round=0/5bit_floor=0/8bit_round=3/8bit_floor=4/6bit_round=4
```

EQ-4u §6.3 takes the 6-bit A2 column because EQ-4i §5.1 measured hardware
green matching the 6-bit A2 model: **alpha-16 green prediction 134**,
**alpha-4 blue prediction 36**. EQ-4h §2 and EQ-4i §5.1/§5.2 are cited, not
re-derived; the A2 column semantics are read as published there.

| Quad | Alpha | Source | Non-blend measured | Blend measured | Blended prediction | Result |
|---|---|---|---|---|---|---|
| 0 | 31 | red `(255,0,0)` | `R=255` | `R=255` | 255 (alpha 31) | unchanged |
| 1 | 16 | green `(0,255,0)` | `G=255` | `G=134` | **134** (6-bit A2) | matches exactly |
| 2 | 4 | blue `(0,0,255)` | `B=255` | `B=33` | **36** (6-bit A2) | **3 levels below** |
| 3 | 0 | white `(255,255,255)` | outline, 138 bright px | outline, 130 bright px | outline unaffected | outline present both |

The alpha-31 quad is full in both captures, as it should be (its alpha is
already maximal). The alpha-16 green and alpha-4 blue quads are full (255) in
the non-blend capture and dimmed in the blend capture. The dimming happens only
in the blend ROM.

**Blue, stated plainly:** the measured blue majority is **33**, the 6-bit A2
prediction is **36** — a difference of **3 levels** (33 is lower). Green is
exact (134).

**HYPOTHESIS (not established here):** the measured **33** equals the
`5bit_floor` A2 column (33) in the quoted EQ-4h §2 output above (column order
`5bit_round,5bit_floor,8bit_round,8bit_floor,6bit_round`). EQ-4i §5.2 measured
the hardware red/blue channels as the 5-bit quantisation of the same 6-bit A2
value; the match here is consistent with that relation but is not a derivation
of it. The green channel (134) matches the un-quantised 6-bit A2 value, as
EQ-4i §5.1 measured. The cause of the G 6-bit / R-B 5-bit split is UNVERIFIED;
this is an observation consistent with EQ-4i, not a derivation of a new rule.

---

## 6. The alpha-0 quad (outline) in both captures

Quad 3 is alpha 0 (source white). The tool censuses its derived region with the
EQ-4r bright rule `max(r,g,b) > 60` and prints the bright-pixel bounding box
beside the region size:

| Capture | Region | Region pixels | Bright pixels | Bright bbox | Interior bright / interior pixels |
|---|---|---|---|---|---|
| blend `a` | `207..242 × 80..114` | 1260 | 130 | `207..240 × 80..114` | 84 / 992 |
| non-blend | `207..242 × 80..114` | 1260 | 138 | `207..240 × 80..114` | 87 / 992 |

Plain reading: in both captures the bright pixels form a boundary outline, not
a filled quad. The tool prints region `207,80,242,114` and bright bbox
`207,80,240,114` for both captures: the right edge differs by 2 px (bright x
max 240 vs region x max 242), while the other three sides are equal (y 80..114,
x min 207). The interior is almost entirely dark (84 and 87 bright pixels out of
992 interior pixels); a filled quad would put on the order of the whole interior
at bright. The outline is present in both captures; enabling blending did not
remove or fill it. The 8-pixel count difference (138 vs 130) is not investigated
and its origin is UNVERIFIED (it is the same capture-noise question as the a/b
93-pixel count).

This is consistent with EQ-4u §6.3's prediction that the alpha-0 outline is a
separate mechanism, unaffected by the blend enable; it does not prove the
separation.

---

## 7. Verdict

**The hardware result supports the hypothesis** that scene 4's alpha-16 and
alpha-4 quads were opaque on hardware in EQ-4r because the demo never enabled
`GL_BLEND`. With `GL_BLEND` enabled and every other ROM byte unchanged:

- alpha-16 green went from full `255` to `134`, exactly the 6-bit A2 prediction
  in EQ-4u §6.3 / EQ-4h §2;
- alpha-4 blue went from full `255` to `33`, 3 levels below the 36 prediction,
  consistent with EQ-4i's R/B 5-bit quantisation as a HYPOTHESIS;
- alpha-31 red stayed at `255`;
- the alpha-0 outline stayed an outline in both captures.

**What this shows:** on ONE console, in ONE capture session, for ONE scene
(`SCENE=4`, `STATIC_ANGLE=30`) and one ROM pair, dimming of the alpha-16/4
quads is conditional on the blend-enable ROM difference.

**What this does not show:** other consoles or sessions; that
`glEnable(GL_BLEND)` alone (one hardware write) is the only difference — the
ROMs differ only by the `BLEND=1` define and its guarded line (EQ-4v §2/§3
hashes quoted in §1), but the causal chain from that write through the DS
blend unit is not observed on the wire; the origin of the capture noise; any
textured geometry.

UNVERIFIED in full:

- other consoles and other capture sessions;
- that enabling blending is the *only* effective difference (the ROM hashes
  and the guarded source line are cited; the hardware behaviour of the enable
  bit itself is not separately instrumented);
- the origin of the 93-pixel a/b noise (and of the 8-pixel outline count
  difference);
- textured geometry, other angles, other scenes;
- the R/B 5-bit quantisation origin (EQ-4i §5.5).

---

## 8. Consequence for our chain

EQ-4u §4.3 (FACT, source read) shows our arm11 renderer sets
`C3D_AlphaBlend` unconditionally from the diagnostic mode table; it never reads
the recorded `DISP3DCNT` blend-enable state. The non-blend hardware capture
paints alpha-16/4 opaque (`(0,255,0)`, `(0,0,255)`), while our render blends
them toward the PICA clear colour (`(7,142,11)`, `(13,13,60)`; EQ-4r §8). This
hardware result closes that gap as **a real divergence** for ROMs that do not
enable blending: our renderer must not blend when the ROM did not enable it.

**Proposed next implementation task (text only; NOT applied here):**

1. **Carry `DISP3DCNT` bit 3 (alpha-blend enable) to the renderer.** The state
   is already recorded: `common/twlhd_v2_parse.c` keeps the RSTA stream
   (`0x04000060`) and the existing `view->rsta` accessors expose address/value
   pairs (EQ-4u §4.1-§4.2). Take bit 3 of the last write at or before the
   frame's first draw (or track it per BEGIN with the RSTA `gx_index` ordering)
   and use it in `arm11/source/main.c` / `arm11/source/renderer.c` to select
   `twlhd_blend_config_alpha()` (bit set) instead of the unconditional mode-0
   `C3D_AlphaBlend`, or `twlhd_blend_config_opaque()` (bit clear). This is
   EQ-4u §6.1 option 1: a renderer-side flag, no interpreter output-contract
   change, so the existing interpreter goldens and `tools/gx_interp_oracle.py`
   are not weakened.
2. **Alpha-0 outline drawing** is a separate item (EQ-4u §6.2), not part of the
   blend gate: it needs a wireframe flag in the emitted plan, edge extraction
   and a line pass. Sized there, not implemented here.
3. **Verification plan for that task.** (a) Offline: build the scene-4
   `BLEND=`/`BLEND=1` autodump pair, capture each `twlhd_frame_v2.bin`, and assert
   the RSTA whitelist entry at `0x04000060` differs in bit 3. (b) Host: feed each
   frame to the interpreter + arm11 renderer and measure the quads with
   `tools/eq4w_blend_hw/measure_blend_hw.py`; the blend frame must reproduce
   `G=134`, `B=33`, the non-blend frame full `255` (within the existing flat
   tolerance), and the alpha-0 region must stay an outline. (c) New unit tests
   for flag propagation and opaque-vs-blend selection; existing tests are
   extended, never weakened. (d) Console: one capture session of the same two
   ROMs in DS mode, compared with the same tool; other consoles remain
   UNVERIFIED.

**Update (2026-11-25):** EQ-4x implemented this proposal; see
`docs/reports/eq4x-blend-gate.md`.

---

## 9. Tool, tests and audit

Tool and fixtures: `tools/eq4w_blend_hw/` (`measure_blend_hw.py`,
`test_measure_blend_hw.py`, `hw_s4bl_a.png`, `hw_s4bl_b.png`, `README.md`).
The test suite covers a synthetic positive (dimmed quads classify
`blended=yes`), a synthetic negative (a full `(0,255,0)` alpha-16 quad
classifies `blended=no`), both fixture sha256 values, the measured blend and
non-blend values, both alpha-0 censuses, and the a/b difference of 93. The
target is wired into `make test` like `tools/eq4o_hw_rows`.

```
$ make test-eq4w-blend-hw
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4w_blend_hw -p 'test_*.py'
.........
----------------------------------------------------------------------
Ran 9 tests in 0.776s

OK
```

**Hygiene.** No console, ROM, emulator or capture tool was run. No inline
Python expression evaluation was used. The two fixture copies are byte-identical to the
owner staging files (`<staging>/hw/hw_s4bl_a.png`, `<staging>/hw/hw_s4bl_b.png`;
`<staging>/hw/hw_s4bl_c.png` is not committed). Only `a` and `b` are
committed. The absolute staging/home path is never written to the repository.
Diff and size audits are appended below with the final edit.

---

## 10. Size audit (after the final edit)

```
$ git add -N . && git diff main | wc -c
55689
$ du -b tools/eq4w_blend_hw/hw_s4bl_a.png tools/eq4w_blend_hw/hw_s4bl_b.png
3197	tools/eq4w_blend_hw/hw_s4bl_a.png
3189	tools/eq4w_blend_hw/hw_s4bl_b.png
$ du -b tools/eq4w_blend_hw
30430	tools/eq4w_blend_hw
```

The two fixture files are the `du -b` values above (each below 100 KB), and the
whole tool directory is the second `du -b` value. The diff is below the
90000-byte cap. The `wc -c` number is quoted after this line was written into the
report; re-running the command after further edits changes it.

## 11. Extra runs disclosed

- The tool was run once per fixture for the quoted FACTs above, and the
  test suite invokes it on synthetic images and on both fixtures. No other
  measurement run exists.
- `sha256sum` was run on the two committed fixtures (hashes in §1/README) and on
  the staging copies of them; the staging hashes match the committed files:

  ```
  $ HWSTAGE=<staging> sha256sum "$HWSTAGE"/hw/hw_s4bl_*.png | sed "s|$HWSTAGE|<staging>|"
  8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01  <staging>/hw/hw_s4bl_a.png
  77d537869e0316e2eccfa931e7cb4d2636b19adbe6ab0a5c38e188047c87a9e6  <staging>/hw/hw_s4bl_b.png
  8b4538fe5da92ef27176d420473ec0fa31dae3a54f749b2128fbde8e20e8eb01  <staging>/hw/hw_s4bl_c.png
  ```

## 12. Requirement traceability

| Requirement | Where |
|---|---|
| tool + fixtures + README with hashes/provenance | `tools/eq4w_blend_hw/` |
| derived rectangles, interior 2 px, per-channel majority | §2-§4, tool output |
| synthetic positive / negative / real fixtures / `make test` | §9 |
| per-quad table vs 134/36 prediction, blue difference, EQ-4i hypothesis | §5 |
| alpha-0 outline measured in both captures | §6 |
| verdict, shown/not shown, UNVERIFIED | §7 |
| consequence and proposed next task (text only) | §8 |
| pointers in four docs and eq4u/eq4v | see those files |
