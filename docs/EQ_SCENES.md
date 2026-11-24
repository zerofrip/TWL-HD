# EQ-2/EQ-3 scene coverage — every arm9v2 demo scene

Stage EQ-2b/EQ-3b of `docs/EQUALITY_CHECK_DESIGN.md` section 4 (EQ-2 lines
396-423, EQ-3 lines 425-451) and sections 5.2/5.1, owner-approved 2026-11-17.
Extends the default-cube runs of `docs/EQ2_COMPARE.md` and
`docs/EQ3_COMPARE.md` to the other demo scenes: `SCENE=1..5` and the `CUBES=2`
variant. `SCENE=5` is the textured scene.

- **Everything here is EMULATOR.** The geometry reference is DeSmuME
  `fc504ee0` through the out-of-tree GPL harness
  `$HOME/3ds/twlhd-desmume-harness` (`format geometry-dump 1`). Our side is
  the recorder's v2 stream interpreted on the host (`gx_interp_cli --eq2`) and,
  for EQ-3, the headless Azahar libretro host (`tools/azahar_host`). Nothing is
  hardware-verified.
- **Licence posture:** this repository is Apache-2.0. No harness or DeSmuME
  source, code or repository content was copied here. The committed harness
  dumps are its program **output** (data) with origin notes in
  `tools/fixtures/eq_scenes/README.md`, the same posture as
  `tools/fixtures/eq2/README.md`.
- **Scope:** host files only. No console, SD-card or network action. The
  harness and DeSmuME trees are unmodified; the interpreter and renderer are
  unmodified.

The runner is `tools/eq_scenes.py`. It imports `tools/eq2_compare.py` and
`tools/eq3_compare.py` and restates no tolerance. The real numbers below are its
EMULATOR output.

## 1. Per-scene build and artifact chain

The build variables are composed by `arm9v2/Makefile` in the order AUTODUMP
(lines 60-75) -> CUBES (116-129) -> SCENE (139-152). The scene data and draw
functions are in `arm9v2/source/demo_frame.cpp:225-310` (scenes 1-4 at
`demo_frame.cpp:225-260`, the textured scene 5 at `:147-218`), and the frame
loop/autodump is `arm9v2/source/main.cpp:240-268`.

| Scene | Build variables (in `arm9v2/`) | ROM |
|---|---|---|
| cube (baseline) | `AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump.nds` |
| scene1 tri-list | `SCENE=1 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s1.nds` |
| scene2 tri-strip | `SCENE=2 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s2.nds` |
| scene3 quad-strip | `SCENE=3 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s3.nds` |
| scene4 alpha quads | `SCENE=4 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s4.nds` |
| scene5 textured | `SCENE=5 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s5.nds` |
| cubes variant | `CUBES=2 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-c2.nds` |

The artifact chain is the existing `make emu-validate` / `emu-validate-tex`
flow, applied per ROM:

1. `make -C arm9v2 <build variables>` (worker step; ROMs and PPMs stay in a
   scratch directory and are not committed);
2. harness geometry and picture for harness frame 242:
   `$HOME/3ds/twlhd-desmume-harness/harness <rom.nds> 242 --out harness.txt
   --ppm harness.ppm`;
3. DeSmuME v2 capture:
   `DESMUME_CLI=<cli> tools/emulator/desmume_capture.sh <rom.nds> <capdir>`
   (the flow of `Makefile:473-479` and `:493-497`), which writes
   `twlhd_frame_v2.bin`;
4. our-side geometry: `tests/host/build/gx_interp_cli --eq2
   <capdir>/twlhd_frame_v2.bin > ours.txt`;
5. EQ-2: `tools/eq2_compare.py harness.txt ours.txt` (via import in the driver;
   section 5.2 tolerances unchanged);
6. EQ-3, where the colour model is valid and an Azahar picture is produced:
   copy the same `twlhd_frame_v2.bin` to the host SD root as
   `sdmc:/twlhd_frame.bin`, run
   `tools/azahar_host/build/azahar_host --game arm11/twlhd-arm11.3dsx
   --frames 200 --press A:60-63 --shot 199 --out <prefix> --sd-root <sd>`
   (`docs/EQ3_COMPARE.md` section 1), and compare the 400x480 PPM with the
   harness PPM through `tools/eq3_compare.py` (section 5.1 tolerances
   unchanged).

### Frame and angle alignment (same arithmetic for every scene)

All scene ROMs share `main.cpp`'s loop, including `angle += 256`
(`main.cpp:263`) and the autodump at `frame == 240` (`main.cpp:253-256`), so ROM
frame 240 is the same angle for every scene:

| Counter | Value |
|---|---|
| ROM frame counter (`AUTODUMP_FRAME=240`) | 240 |
| angle, binary units / degrees | 240 * 256 = **61440** = 315 degrees |
| v2 recorder `frame_index` (1-based; printed by `--eq2`) | **241** |
| DeSmuME/harness `currFrameCounter` | **242** |

libnds' boot consumes two VBlanks before the ROM's first draw reaches the GPU,
so ROM counter `k` is harness frame `k + 2` (measured in the harness README and
`docs/EQ2_COMPARE.md` section 3). The driver enforces, per row:

- the harness header has `requested-frame 242` **and** `captured-frame 242`;
- our dump has `frame 241`;
- the harness header `rom-sha256` matches the SHA-256 of that row's ROM (full
  mode).

A row that fails any of these checks, or whose harness/our-side dump cannot be
loaded at all, is **NOT-COMPARABLE** with exit code 0: the two sides are not the
same frame/ROM, so there is nothing to compare. A polygon or vertex **count**
mismatch between two dumps of the same frame is a real **FAIL** with exit code 1
(the frame lines agree, so the geometry really differs). Neither case is ever a
silent PASS, and a count mismatch is never turned into NOT-COMPARABLE. The
table below states for each scene whether the capture held the same frame as
the harness frame.

| Scene | ROM frame (AUTODUMP) | Recorder frame (`--eq2`) | Harness requested/captured | Same frame? |
|---|---|---|---|---|
| cube | 240 | 241 | 242 / 242 | yes |
| scene1 | 240 | 241 | 242 / 242 | yes |
| scene2 | 240 | — (capture aborted) | 242 / 242 | harness yes; no our-side frame |
| scene3 | 240 | — (capture aborted) | — (ROM never presents) | no reference frame |
| scene4 | 240 | 241 | 242 / 242 | yes |
| scene5 | 240 | 241 | 242 / 242 | yes |
| cubes | 240 | — (capture aborted) | 242 / 242 | harness yes; no our-side frame |

Every compared row therefore holds the same scene, the same angle (61440) and
the same frame on both sides; the rows with `—` did not produce a comparison.

## 2. Real run (EMULATOR, 2026-11-17)

Command (worker, one run):

```sh
python3 tools/eq_scenes.py \
  --harness-bin $HOME/3ds/twlhd-desmume-harness/harness \
  --desmume-cli $HOME/3ds/hwtest-staging/desmume-build/cli/desmume-cli \
  --rom-dir arm9v2 --work-dir <scratch> \
  --azahar-host tools/azahar_host/build/azahar_host \
  --azahar-core $HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
  --arm11-3dsx arm11/twlhd-arm11.3dsx \
  --desmume-timeout 30 --capture-retries 4
```

`--capture-retries` repeats the same documented `desmume_capture.sh` flow when
the capture aborts with `ARM OP_BKPT`; it does not change the capture method.
The work directory holds the ROM captures and PPMs and is not committed.

| Scene | Build variables | Polys/verts compared | Worst EQ-2 x | Worst EQ-2 y | Worst EQ-2 z (0.31) | Worst EQ-2 w | Worst colour (6-bit) | Worst tex s/t (12.4, not gated) | EQ-3 | Verdict |
|---|---|---|---|---|---|---|---|---|---|---|
| cube | `AUTODUMP_FRAME=240` | 6 / 24 | 0.0190 px (1244) | 0.0109 px (715) | 114325 (tol 1873103) | 0 (tol 16) | 0 (tol 2) | 0 | PASS, worst colour-r=4 (tol 8) | **PASS** |
| scene1 | `SCENE=1 AUTODUMP_FRAME=240` | 8 / 24 | 0.0118 px (772) | 0.0148 px (967) | 74058 (tol 1288019) | 1 (tol 16) | 1 (tol 2) | 0 | NOT-COMPARABLE (red-ramp model) | **PASS (geometry)** |
| scene2 | `SCENE=2 AUTODUMP_FRAME=240` | 0 compared (reference 10/30) | — | — | — | — | — | — | NOT-COMPARABLE (red-ramp model) | **NOT-COMPARABLE** |
| scene3 | `SCENE=3 AUTODUMP_FRAME=240` | — | — | — | — | — | — | — | NOT-COMPARABLE (red-ramp model) | **NOT-COMPARABLE** |
| scene4 | `SCENE=4 AUTODUMP_FRAME=240` | 4 / 16 | 0.0166 px (1090) | 0.0164 px (1074) | 91608 (tol 1500947) | 1 (tol 16) | 0 (tol 2) | 0 | NOT-COMPARABLE (blended T=16; wireframe) | **PASS (geometry only)** |
| scene5 | `SCENE=5 AUTODUMP_FRAME=240` | 1 / 4 | 0.0087 px (571) | 0.0148 px (969) | 165343 (tol 2708990) | 0 (tol 16) | **0 (tol 2)** | 0 | NOT-COMPARABLE (texture model) | **PASS (geometry+colour)** |
| cubes | `CUBES=2 AUTODUMP_FRAME=240` | 0 compared (reference 12/48) | — | — | — | — | — | — | NOT-COMPARABLE (no picture) | **NOT-COMPARABLE** |

`—` means no comparison ran; the rationale is in section 3. The
`0 compared (reference N/M)` form is what `format_table` prints when the
harness dump was parsed but EQ-2 did not run: N polygons / M vertices are the
harness reference counts, not compared counts. The z tolerance is per vertex
(`floor(0.004 * 2^31 / (2*|w|))`, docs/EQ2_COMPARE.md section 4), so the table
shows the tolerance at the reported worst vertex. No tolerance was changed,
loosened or re-derived by the driver.

### 3. Per-scene findings, with the exact reason

**cube — PASS.** 6 polygons / 24 vertices. x/y differ from DeSmuME by at most
0.019 px, far inside 1 DS px; w and colour are exact after conversion. EQ-3
passes the existing structural comparison (generated Azahar picture of the same
recorder frame 241; worst colour-r = 4, tol 8; bbox/centroid worst 1 DS px,
tol 2; `docs/EQ3_COMPARE.md` section 6 has the per-face numbers).

**scene1 — PASS (geometry).** 8 triangles / 24 vertices. Worst x 0.0118 px,
y 0.0148 px, w 1 quantum, colour 1 6-bit LSB, z 74058 (tol 1288019). EQ-3 is
NOT-COMPARABLE: each vertex has a unique 5-bit red ramp
`(i + 1) << 3` (`demo_frame.cpp:266-269`), and `eq3_compare`'s six cube face
classes (`eq3_compare.py:64-71`) cannot model 24 distinct red shades — the
nearest-class collapse is a tool-model limitation, not a pipeline defect.

**scene2 — NOT-COMPARABLE (capture aborts).** The harness reference succeeded
(10 polygons / 30 vertices, frame 242; `harness_s2_f240.txt` is committed), but
the documented DeSmuME v2 capture produced no `twlhd_frame_v2.bin`. Reproduced
(EMULATOR, 2026-11-17) with the ROM from
`make -C arm9v2 SCENE=2 AUTODUMP_FRAME=240` (sha256 `d84f210b…`,
`tools/fixtures/eq_scenes/README.md`): the documented script

    DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-build/cli/desmume-cli \
      tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s2.nds <dir>

exits 1, `<dir>/desmume.log` ends with `ARM OP_BKPT triggered` (the same run
earlier logged `Slot 2: MPCF Flash Card Device` and `AUTO-PATCHING DLDI to
MPCF`), and no v2 stream is written. The same ROM started without a Slot-2 card
(`--cflash-path <dir>`, which DeSmuME reports as
`Slot2 auto-selected device type: PassME (0x08)`, no DLDI auto-patch) runs the
full 60 s watchdog with `--disable-limiter` and no BKPT — but it writes nothing
either, so that run only shows that the ROM does not fault before it mounts the
card; it does not isolate the cause.

The **faulting PC has since been measured** with an out-of-tree diagnostic
DeSmuME CLI (no gdb stub needed): `0x01FF853C`, the reset slot
`__arm_excpt_rst` whose instruction is `bkpt 0x8000`, entered because the ROM
branches to address 0 from newlib `setvbuf` at frame 0 — a consequence, not the
cause. For this scene2 ROM, in the tested configurations, the fault needs
DeSmuME's in-place DLDI auto-patch of the ROM image and reproduces only with it
(one scratch run, `run-pcB`, is an unexplained counter-example — see the
report). Details, per-PC evidence and labels:
`docs/reports/bkpt-diagnosis.md` (root cause still UNRESOLVED). What is ruled out
with file:line evidence is a recorder or `demo_frame` limit: `arm9v2/` and
`common/twlhd_gx_record.{c,h}` contain no runtime assert or BKPT (the only
assertions are compile-time `_Static_assert`, `common/twlhd_gx_record.h:79-91`),
and every capacity is far from full for a 10-polygon frame
(`TWLHD_GX_REC_MAX_ENTRIES` 16384, `common/twlhd_gx_record.h:30`; a few dozen
entries recorded; serialise buffer 135264 B, `:41-45`; texture-path maximum
309395 B, `:68-71`). The aborted scenes also draw no more than the working ones:
scene2 submits 12 ROM vertices (`scene_strip_vertices[12][2]`,
`arm9v2/source/demo_frame.cpp:237-241`, drawn with count 12 at `:282-286`),
fewer than scene1's 24, and scene1's capture succeeded (its `ours_s1_f240.txt`
is committed); the scenes with committed our-side dumps (cube, scene1, scene4,
scene5) have 4-24 ROM vertices, the three that abort are scene2, scene3 and
cubes. What this repository does contain is
the calico trap: `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-s2.elf
0x01ff853c` returns `__arm_excpt_rst`
(`calico-1.2.0/source/nds/arm9/excpt_handler.32.s:27`), whose instruction is
`bkpt 0x8000` (`arm-none-eabi-objdump`), and `__excpt_vectors` at `0x01ff8038`
starts with `.word 0x01ff853c`, the reset slot. `docs/EMULATOR_VALIDATION.md:286`
measured control reaching that same reset vector in the analogous CUBES=64
AF=120 failure; scene2 does stop there (measured, frame 0 — see
`docs/reports/bkpt-diagnosis.md`). **DEFECT
FOUND: none** — no recorder/`demo_frame` limit is reached and no ROM file is
changed by this task. Whether the trap comes from our write path, from the
DLDI/libfat/MPCF stack it calls, or from the emulator is **UNRESOLVED** and is
not attributed to DeSmuME; the measurement does show that for this scene2 ROM,
in the tested configurations, it needs the emulator's in-place DLDI auto-patch
of the ROM image and that the ROM's own bytes are not sufficient.
**HYPOTHESIS: emulator-side** (see the report); the cause is not established.
The harness dump is committed so the reference counts stay verifiable.

**scene3 — NOT-COMPARABLE (no reference frame).** The harness itself exits 1:
`ROM never presented a 3D frame within 1200 frames`, and the capture log ends
with the same `ARM OP_BKPT triggered`. No `SWAP_BUFFERS` reaches the GPU, so
neither the reference geometry nor our stream exists. The PC was measured for
scene3 too: `0x01FF853C` (reset slot) reached by a branch to address 0 at frame
0, with LR `__libc_lock_release_recursive` — a different branch site from
scene2's (`docs/reports/bkpt-diagnosis.md`). No fixture is committed for this
row because there is no data to commit.

**scene4 (alpha quads) — PASS, geometry only.** 4 polygons / 16 vertices. Worst
x 0.0166 px, y 0.0164 px, w 1 quantum, colour 0, z 91608 (tol 1500947). All four
quads, including the alpha-0 quad, are present in both lists: DeSmuME keeps the
alpha-0 quad in its geometry list, and our interpreter emits it with vertex
alpha 0.0. EQ-3 is NOT-COMPARABLE by requirement: blended pixels need the
section 5.1 **blended-pixel tolerance T = 16** (design lines 594-595: "Same,
blended pixels ... 16 until the hardware scale is settled"), and the alpha
convention `a/31` vs `(a+1)/32` is UNVERIFIED (design 5.1(ii); our interpreter
emits `(a+1)/32`, `common/twlhd_gx_interp.c:395-409`). `eq3_compare` models
neither blending (T_COLOUR = 8 for flat alpha-31 faces) nor the alpha-0
wireframe/outline path (design 2.3; `common/twlhd_gx_interp.c:1075-1077`). The
16-level blended tolerance is therefore **stated but not exercised**; what is
proven is vertex/geometry equality for the alpha scene, nothing about blending.

**scene5 (textured) — PASS (geometry+colour).** 1 polygon / 4 vertices.  All
EQ-2 fields are inside tolerance: worst x 0.0087 px, y 0.0148 px, w 0, z 165343
with tol 2708990, colour 0 (tol 2), and tex s/t 0 (reported but never gated).
The geometry was already passing; the colour deviation dropped from 63 to 0
once the interpreter's latched RGB was initialised to white to match DeSmuME's
reset model (`common/twlhd_gx_interp.c:1151`).

Root cause and remaining caveat: scene 5 never issues `glColor3b` for the
textured quad (`demo_frame.cpp:190-216`), so the colour is whatever state the
geometry engine reset left. The recorder only captures writes made through the
shim after it is armed; `glInit()`'s library-internal writes are not recorded,
which is exactly the `INITIAL_STATE_UNKNOWN` coverage flag
(`docs/TWLHD_V2_FORMAT.md:211`: "state set before arming is not reproduced";
`docs/EQ2_COMPARE.md` section 1's `INITIAL_STATE_UNKNOWN`). The fix makes the
interpreter agree with DeSmuME's model, but the real-DS default colour stays
UNVERIFIED (`docs/reports/scene5-colour-diagnosis.md` section 3); the coverage
bit remains set in every capture. The textured-picture EQ-3 is still
NOT-COMPARABLE: `eq3_compare` has no texture model (design/EQ3 section 8:
"textured sampling would need its own structural checker"), and the geometry
list carries no texture state; texture data lives in the v2 TXDT/PLDT chunks,
which the picture comparison does not read.

**cubes (`CUBES=2`) — NOT-COMPARABLE (capture aborts).** The harness reference
succeeded (12 polygons / 48 vertices, frame 242), but the DeSmuME v2 capture
aborted with `ARM OP_BKPT triggered` (log line; the faulting PC has since been
measured for this row: `0x01FF853C`, the reset slot, reached by a branch to
address 0 at frame 6 with SP inside the DLDI region —
`docs/reports/bkpt-diagnosis.md`).
`docs/EMULATOR_VALIDATION.md:269-292` holds the prior CUBES investigation: in
the CUBES=64 AF=120 run gdb showed control reaching the `0x01ff853c` reset
vector, and that root cause stays UNEXPLAINED (the same diagnostic CLI measures
it at frame 129, after 120+ frames presented, branching to 0 from the
`pop {pc}` epilogue of `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:263-264`)
— a different site from `CUBES=2`'s).
`CUBES=2` draws 48 ROM
vertices, twice the working cube frame's 24, yet no recorder capacity is near its limit
(`common/twlhd_gx_record.h:30,41-45`), so there is no failing size limit to
report and no "DEFECT FOUND" statement to make. Without our-side geometry no
EQ-2 row and no Azahar picture can be produced. `CUBES=2` was chosen to match
the existing host test `test_demo_frame_c2`; the value is a driver/scene
parameter, not a tolerance. The harness dump is committed
(`harness_c2_f240.txt`).

## 4. Tolerances and what was not changed

EQ-2 uses exactly `tools/eq2_compare.py`'s section 5.2 values: x/y 1 DS pixel
(65536 in 16.16), w 16 quanta (0.004 clip), z `floor(0.004 * 2^31 / (2*|w|))`,
colour +/-2 in the harness's 6-bit channels, texture s/t reported but never
gated. EQ-3 uses exactly `tools/eq3_compare.py`'s section 5.1 values: position
T = 2 DS px, colour T = 8 per channel, erosion 3 DS px. The blended-pixel
T = 16 from section 5.1 is cited for the alpha scene but is not used, because
the picture comparison is not valid there (section 3). No interpreter,
renderer, tolerance or harness/DeSmuME file was modified.

## 5. What this does NOT prove

- **DS hardware.** DeSmuME's software rasteriser and Azahar's PICA200 model are
  emulators; nothing is HARDWARE VERIFIED.
- **Rasterisation, texture sampling, blending, fog, edge/AA, toon or display
  capture** beyond the structural EQ-3 checks. Scene 4's blended pixels are not
  compared; scene 5's texture is not compared.
- **The scene2/scene3/cubes rows.** Scene 2 and CUBES=2 have reference geometry
  but no our-side capture; scene 3 has neither. In every failing case with the
  patch the emulator log ends with `ARM OP_BKPT triggered`; no recorder or
  `demo_frame` limit is reached (section 3), the faulting PC is `0x01FF853C` in
  every such row and is the reset slot reached by a branch to address 0
  (`docs/reports/bkpt-diagnosis.md`), and the cause of that branch is UNRESOLVED.
  Only scene2's branch site (newlib `setvbuf`) is measured to be outside our
  code; scene3's and CUBES=2's sites are not attributed (UNVERIFIED, see the
  report's open items 3-4); for CUBES=64
  AF=120 the branch site **is** in our code (`common/twlhd_tex_capture.c:263-264`, LR
  in `twlhd_tex_capture_plan` at `:263`), also UNRESOLVED; a fresh rebuild shows
  the instruction is a `pop {pc}` return from a zeroed saved LR, not an
  indirect call — see `docs/reports/cubes64-branch0-diagnosis.md`.
  The emulator's in-place DLDI auto-patch is required by **scene2 in the tested
  configurations** (the only ROM the with/without-patch discriminator ran on;
  the scratch run `run-pcB` is an unexplained counter-example). Without the
  patch this task measured scene3 (no fault in 30 s, but no 3D frame either),
  CUBES=2 (no fault, frames presented) and CUBES=64 (not the `0x01FF853C`
  fault; instead `ARM9: Undefined instruction: 0x3C000000 PC=0x0500049C` at
  ~frame 490). A future capture fix could turn these into real comparisons; the
  driver already retries the documented capture and reports the log reason.
- **Clipping.** EQ-2 remains valid only for unclipped frames (all scenes are
  inside the host-test frustum at this angle; a surprise clipped polygon is a
  FAIL/NOT-COMPARABLE row, not a silent pass).
- **Frame alignment for a different ROM.** The +2 offset is measured for these
  demo ROMs and confirmed by each harness header; the driver checks it per row
  and marks a different frame NOT-COMPARABLE.

## 6. Fixed-angle (`STATIC_ANGLE`) textured scene (2026-11-23)

The rotating scene 5 can be frozen at a build-time angle so every frame is
identical. This is the reference-picture input for the EQ-4 hardware capture
comparison (`docs/EQ4_HW_COMPARE.md`); the hardware run itself is a later,
separate step.

### 6.1 Build option

`arm9v2/Makefile` composes `AUTODUMP` -> `VP` -> `CUBES` -> `SCENE` ->
`STATIC_ANGLE` -> `DIAGSCREEN`. `STATIC_ANGLE` must be empty (the default) or
a decimal integer in 0..359; anything else stops the build with

```
*** STATIC_ANGLE must be empty or a decimal integer in 0..359 (got: 360).  Stop.
```

The block appends `-st<angle>` to both `TARGET` and `BUILD` and defines
`-DTWLHD_DEMO_STATIC_ANGLE_DEG=<angle>`. Unset/empty leaves TARGET, BUILD and
CFLAGS untouched, so the default ROMs stay byte-identical (6.3). The define is
accepted but unused when `SCENE != 5`, the same acceptance rule as `VP_*`.
`tools/test_arm9v2_static_angle.py` (target `make test-arm9v2-static`) dry-runs
the eight validation cases.

| Static build variables (in `arm9v2/`) | ROM |
|---|---|
| `SCENE=5 STATIC_ANGLE=0 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s5-st0.nds` |
| `SCENE=5 STATIC_ANGLE=30 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s5-st30.nds` |
| `SCENE=5 STATIC_ANGLE=0` (plain, hardware) | `twlhd-arm9v2-s5-st0.nds` |
| `SCENE=5 STATIC_ANGLE=30` (plain, hardware) | `twlhd-arm9v2-s5-st30.nds` |

### 6.2 Angles and their justification

The textured quad lies in the z = 0 plane (`demo_frame.cpp:209-215`), the
camera is on +z looking at the origin (`:359`), and the animated build applies
`glRotateXi(angle); glRotateYi(angle)` about the same X-then-Y pair
(`:360-361`). So 0 degrees leaves the quad face-on to the camera and 30
degrees is an oblique view about the same two axes. Degrees are converted to
the DS binary angle convention (32768 == 360, `main.cpp:262-263`) with integer
truncation: `degrees * 32768 / 360` (30 -> 2730, 315 -> 28672).

```
$ sed -n '357,363p' arm9v2/source/demo_frame.cpp
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG) && TWLHD_DEMO_SCENE == 5
    /* Fixed modelview angle (STATIC_ANGLE=0..359 in the Makefile): the
     * animated build rotates by the frame counter, in binary angle units
     * (32768 == 360 degrees, main.cpp:262-263). Degrees -> binary units with
$ grep -n 'glRotateXi\|glRotateYi' arm9v2/source/demo_frame.cpp
367:    glRotateXi((int)((unsigned)TWLHD_DEMO_STATIC_ANGLE_DEG * 32768u / 360u));
368:    glRotateYi((int)((unsigned)TWLHD_DEMO_STATIC_ANGLE_DEG * 32768u / 360u));
370:    glRotateXi((int)angle);
371:    glRotateYi((int)angle);
421:    glRotateXi((int)angle);
422:    glRotateYi((int)angle);
474:        glRotateXi((int)angle);
475:        glRotateYi((int)angle);
```

### 6.3 Byte-identity of the existing ROMs

The pair was built before the edit and rebuilt from `make -C arm9v2 clean`
after it. Both pairs are equal; the scene5 value matches the recorded
`tools/fixtures/eq_scenes/README.md` baseline `35a50aad...`.

Before:

```
$ make -C arm9v2 -j4
$ sha256sum arm9v2/twlhd-arm9v2.nds
d7427286347d809c17376af6c5037b0d08e3de28f5b8c08d42f3e747450cfb6a  arm9v2/twlhd-arm9v2.nds
$ make -C arm9v2 -j4 SCENE=5 AUTODUMP_FRAME=240
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s5.nds
35a50aadc30dbf26d7fa389192a908024cffaa515614c378e389e3efd45d82bd  arm9v2/twlhd-arm9v2-autodump-s5.nds
```

After:

```
$ make -C arm9v2 clean && make -C arm9v2 -j4
$ sha256sum arm9v2/twlhd-arm9v2.nds
d7427286347d809c17376af6c5037b0d08e3de28f5b8c08d42f3e747450cfb6a  arm9v2/twlhd-arm9v2.nds
$ make -C arm9v2 -j4 SCENE=5 AUTODUMP_FRAME=240
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s5.nds
35a50aadc30dbf26d7fa389192a908024cffaa515614c378e389e3efd45d82bd  arm9v2/twlhd-arm9v2-autodump-s5.nds
```

### 6.4 EMULATOR chain and verdicts (2026-11-23)

Commands (scratch directory, ROMs/PPMs not committed):

```
$ make -C arm9v2 -j4 SCENE=5 STATIC_ANGLE=0 AUTODUMP_FRAME=240
$ sha256sum arm9v2/twlhd-arm9v2-autodump-s5-st0.nds
8f4d506fbbe92e693e0727a3c71e7c9392f1cbb677a37d350d8c15e87b8d960e
$ $HOME/3ds/twlhd-desmume-harness/harness \
    arm9v2/twlhd-arm9v2-autodump-s5-st0.nds 242 --out harness.txt --ppm harness.ppm
$ DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-build/cli/desmume-cli DESMUME_TIMEOUT=45 \
    tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s5-st0.nds capture
$ tests/host/build/gx_interp_cli --eq2 capture/twlhd_frame_v2.bin > ours.txt
$ python3 tools/eq2_compare.py harness.txt ours.txt
PASS frame=242 polygons=1 vertices=4
x: worst 0 16.16 px tol 65536 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
y: worst 0 16.16 px tol 65536 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
z: worst 0 0.31 tol 1073741 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
w: worst 0 20.12 tol 16 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
r: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
g: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
b: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
tex-s: worst 0 12.4 reported, not gated at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
tex-t: worst 0 12.4 reported, not gated at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
$ cp capture/twlhd_frame_v2.bin sd/twlhd_frame.bin
$ tools/azahar_host/build/azahar_host --core $HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 \
    --out azahar --sd-root sd
$ python3 tools/eq3_compare.py harness.ppm azahar_199.ppm
MISMATCH colour-g-nominal: 255 > 8 at face magenta
MISMATCH colour-b-nominal: 28 > 8 at face yellow
MISMATCH colour-b-nominal: 12 > 8 at face green
FAIL faces desmume=magenta,red,yellow,green,blue ours=magenta,red,yellow,green,blue
```

For `STATIC_ANGLE=30` the same commands produce ROM sha256
`30337896a415a433376f8478db1ebe973ce401f4bb657d9d9b878bb27ff2063e` and EQ-2:

```
PASS frame=242 polygons=1 vertices=4
x: worst 960 16.16 px tol 65536 at poly 0 (rawpoly 0) vertex 1 (our vertex 5)
y: worst 743 16.16 px tol 65536 at poly 0 (rawpoly 0) vertex 2 (our vertex 0)
z: worst 122797 0.31 tol 2011915 at poly 0 (rawpoly 0) vertex 1 (our vertex 5)
w: worst 0 20.12 tol 16 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
r: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
g: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
b: worst 0 6-bit tol 2 at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
tex-s: worst 0 12.4 reported, not gated at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
tex-t: worst 0 12.4 reported, not gated at poly 0 (rawpoly 0) vertex 0 (our vertex 2)
```

EQ-3 for `STATIC_ANGLE=30`:

```
MISMATCH bbox-bottom: 17 > 2 at face magenta
MISMATCH colour-g-nominal: 255 > 8 at face magenta
MISMATCH centroid-y: 2.146264130135094 > 2 at face yellow
MISMATCH colour-b-nominal: 28 > 8 at face yellow
MISMATCH colour-b-nominal: 12 > 8 at face green
MISMATCH centroid-x: 2.1890537858020593 > 2 at face blue
FAIL faces desmume=magenta,red,yellow,green,blue ours=magenta,red,yellow,green,blue
```

The EQ-3 FAIL is a **tool-model limitation, not a pipeline defect**:
`eq3_compare` has six cube face classes and no white class, so the white
textured cells collapse onto magenta (distance tie at 255^2) and then fail the
declared-class interior colour check; it also has no texture model. The
manifest rows therefore declare `eq3: "not-comparable"` with that reason, the
same shape as the rotating scene5 row; the actual verdict lines above are
quoted and nothing is forced to PASS.

### 6.5 Static-ness proof

The harness PPM is identical at two different frames, so the ROM itself does
not rotate:

```
$ $HOME/3ds/twlhd-desmume-harness/harness arm9v2/twlhd-arm9v2-autodump-s5-st0.nds 102 \
    --out harness_f102.txt --ppm harness_f102.ppm
$ $HOME/3ds/twlhd-desmume-harness/harness arm9v2/twlhd-arm9v2-autodump-s5-st0.nds 242 \
    --out harness.txt --ppm harness.ppm
$ sha256sum harness_f102.ppm harness.ppm
799d6bada988e642540cede5a1e7bebb569a99f2722dc08927ecf8effb78a1d1  harness_f102.ppm
799d6bada988e642540cede5a1e7bebb569a99f2722dc08927ecf8effb78a1d1  harness.ppm
```

`STATIC_ANGLE=30` gives the same pair at
`4b10a82e9fe94488dce1309b982aee32a033b2fb25d19e0630abc8e9392549aa`.

### 6.6 Committed fixtures

Two manifest rows, `scene5-static0` and `scene5-static30`, were added to
`tools/fixtures/eq_scenes/manifest.json`; the `SCENES` tuple in
`tools/eq_scenes.py` is unchanged, so the documented full-run mode does not
require the static scratch ROMs.

| File | SHA-256 |
|---|---|
| `harness_s5_st0_f240.txt` | `b8bd5c33fdf8f0627ac20e7c679d0a3f1dc4706eca9c78482930c95987f34148` |
| `ours_s5_st0_f240.txt` | `aa6f9b0cb9e9f20e8f52a8106a6fd43bfabae4adc0e1bbc1ee8c92c86d0e274f` |
| `harness_s5_st30_f240.txt` | `197bf7f78694eb51dfc5534111c0413aed9e8009d3f94d092dde59c0680b2e63` |
| `ours_s5_st30_f240.txt` | `09caa4574c21ff6a1a7ea08f0f1ed1302b1211a2fb346c6855d1a98b00150a60` |
| `harness_s5_st0_f240.png` | `dce0a5c7f6decfdd78c0d65547f43e840818fe41bcf55c1f6dd68b51a8e848bb` |
| `harness_s5_st30_f240.png` | `5be2f7e0d772421b814af1712f2197c2d087b0363ea806e7189113758421bd35` |

The PNGs are the 256x192 harness PPMs converted losslessly with PIL; they are
431 B and 1396 B, well under the 100 KB fixture limit. They are reference
pictures only, not committed PPMs.

### 6.7 Plain hardware ROMs (built, not run, not committed)

Build recipe chosen: the same Makefile variables without `AUTODUMP_FRAME`, i.e.
`make -C arm9v2 SCENE=5 STATIC_ANGLE=<A>`.

```
$ sha256sum arm9v2/twlhd-arm9v2-s5-st0.nds arm9v2/twlhd-arm9v2-s5-st30.nds
345bc8efc51706c8e5cc5b4cfb8f49a226c5fe93dc0154fb59aadad9bc937081  arm9v2/twlhd-arm9v2-s5-st0.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  arm9v2/twlhd-arm9v2-s5-st30.nds
$ ndstool -i arm9v2/twlhd-arm9v2-s5-st0.nds
Header information:
0x00	Game title               	HOMEBREW
0x0C	Game code                	####
0x12	Unit code                	0x02
0x14	Device capacity          	0x01 (2 Mbit)
0x20	ARM9 ROM offset          	0x4000
0x24	ARM9 entry address       	0x2004800
0x2C	ARM9 code size           	0x21FEC
0x30	ARM7 ROM offset          	0x26000
0x3C	ARM7 code size           	0xB374
```

The `STATIC_ANGLE=30` plain ROM differs only in the ARM9 code size
(`0x21FF4`) and the SHA-256 above. The earlier `s5cull-plain.nds` was built in
an out-of-repo staging directory (docs/reports/eq4e-scene5-hw-capture.md
records only its SHA-256 prefix `1bde3700`); its exact recipe is
**UNVERIFIED**, so the plain build above is documented as the chosen recipe
and no claim is made that it reproduces that file.

### 6.8 What section 6 does not prove

- **DS hardware.** All chain results are DeSmuME/Azahar EMULATOR output. The
  plain ROMs are build evidence only; neither was run on a console.
- **Texture/structure equality.** EQ-3 is a tool-model FAIL (6.4), not a
  picture match; the geometry and vertex colour match (EQ-2) and the static
  reference is stable (6.5).
- **Capture-board behaviour.** The EQ-4 comparison tool
  (`docs/EQ4_HW_COMPARE.md`) exists, but the real hardware capture is a later
  owner step and is UNVERIFIED here.

### 6.9 Hardware run of the static scene5 ROMs (2026-11-23, supersedes 6.8)

The plain `SCENE=5 STATIC_ANGLE=0` and `=30` ROMs of 6.7 were run on one New
3DS in DS mode and captured passively with the cc3dsfs board (owner-reported
procedure; fixtures `tools/fixtures/eq4_hw/`). Compared with both the DeSmuME
harness frame 240 and the PICA render, each capture gives `verdict=PASS` under
the EQ-4 tool tolerances (`ds.frac_within_flat` 0.9986-0.9999, bbox deviations
<= 2 DS px); swapped-angle controls FAIL. This supersedes the "neither was run
on a console" sentence in 6.8 for these two static ROMs only; bit-exactness,
other scenes/consoles and the alpha convention (all alpha 31) remain
UNVERIFIED. Tests: `tools/test_eq4_hw_compare.py`. Report:
`docs/reports/eq4f-scene5-static-hw-compare.md`.


## 7. Tests

`tools/test_eq_scenes.py` runs via `make test-eq-scenes`; the discover pass in
`make test` sets `EQ_SCENES_DISCOVER_SKIP` so it executes exactly once. It uses
only the committed text fixtures in `tools/fixtures/eq_scenes/` (no harness,
DeSmuME or Azahar needed):

- the nine-row manifest table: real scene-5 PASS (including the two
  `STATIC_ANGLE` rows), compared counts 6/24 etc.,
  `0 compared (reference 10/30)` for the rows where EQ-2 did not run, and the
  NOT-COMPARABLE rows; plus a negative test that a perturbed black-vs-white
  `ours_s5_f240.txt` still FAILs with `MISMATCH r: 63 > 2`;
- a pass/NOT-COMPARABLE manifest -> exit 0 (NOT-COMPARABLE never causes exit 1);
- the verdict rule, one test per branch: a harness frame other than 242, our
  frame other than 241, or an unloadable/truncated dump -> NOT-COMPARABLE with
  exit 0 and the reason printed; a polygon-count mismatch (harness 8 vs manifest
  9) -> FAIL with exit 1; two dumps of the same frame with different polygon
  counts (harness 8/24, our-side copy with one output triangle removed) -> FAIL
  with exit 1 and the count reason printed;
- the first `pos-x-16.16` of the scene1 fixture perturbed beyond
  `eq2_compare.TOL_XY_16_16` -> FAIL row and exit 1;
- missing harness/DeSmuME/ROM prerequisites -> exit 2 before any run; missing
  manifest input or manifest path -> exit 2;
- two in-process mutants: a broken verdict rule (`aggregate` forced to 0) and a
  broken per-scene loop (`select_rows` forced to the first row) each make the
  CLI wrongly accept the committed FAIL row; a file-level mutant of
  `eq_scenes.py` that forces the two-dump count mismatch to NOT-COMPARABLE makes
  the CLI exit 0 instead of 1. Each mutant test first asserts the unmodified
  driver's exit code, so the mutation is what flips it.

## 8. Update (2026-11-24): STATIC_ANGLE now covers every scene

The `STATIC_ANGLE=0..359` build option described in §6.1 has been extended from
the textured SCENE=5 to the cube baseline, SCENE=1..4 and the `CUBES=2` variant.
The sentence in §6.1 stating that the define is "accepted but unused when
`SCENE != 5`" is superseded by this update: the define now applies to every draw
path that rotates with the frame counter (`demo_frame.cpp:366-379`, `:426-433`,
`:485-492`). The default ROMs remain byte-identical, the new static ROMs have
been built and hashed, and the owner console-run plan is documented in
`docs/reports/eq4q-static-scenes.md`.

## 9. Update (2026-11-24): the six static-30 scenes against real hardware (EQ-4r)

Per-scene harness references now exist for cube, s1, s2, s3 and s4 (256x192
PNGs in `tools/fixtures/eq4_hw_scenes/`, with the hardware capture `a` and the
Azahar/PICA crop of each scene). FACT (one console, one capture session per
ROM): cube, s1, s2, s3 PASS against both references; SCENE=4 FAILs both
(`bbox_dev_top=10`, tol 3, the alpha-0 quad's outline); `CUBES=2` is
NOT-COMPARABLE against the harness — `harness: ROM never presented a 3D frame
within 1200 frames` and exit 1 with `ARM OP_BKPT triggered` — but PASSes
against our render; its DeSmuME v2 capture did succeed this round (unlike the
non-static ROM in §3). UNVERIFIED: other consoles/sessions, capture-noise
origin, textured geometry, the scene-4 alpha/blend mechanisms. Report:
`docs/reports/eq4r-static-scenes-hw-compare.md`.

## 10. Update (2026-11-24): EQ-4s — the `CUBES=2` static autodump harness BKPT

EQ-4s rebisected the `ARM OP_BKPT` with one harness run per variant: the non-static `CUBES=2 AUTODUMP_FRAME=240` ROM (`703dcad6…`, the EQ-3b fixture ROM) passes at HEAD and at base `d21d189`, the plain static `twlhd-arm9v2-c2-st30.nds` passes at frame 242, and only the new static+autodump `CUBES=2` ROM fails (zero 3D flushes, same undefined-instruction PC `0x02017310`, independent of autodump frame 60/240 and angle 30/31). FACT: the fault appears only in the EQ-4q-introduced static+autodump `CUBES=2` build, not caused by the angle value (30/31) or the autodump frame (60/240). HYPOTHESIS: a pre-existing binary-layout-sensitive wild jump before the first 3D flush (boot, early main or the first draw call; location UNVERIFIED), not the EQ-4q macro's semantics and not the frame/run length; mechanism UNVERIFIED. These runs cannot exclude that EQ-4q merely exposed a latent fault. Proposed (not implemented) compare workaround: use the plain static `c2` ROM for the harness reference. Report: `docs/reports/eq4s-c2-harness-bkpt.md`.

## 11. Update (2026-11-24): EQ-4t — the c2 harness reference from the plain static ROM

EQ-4t implemented the EQ-4s §6 workaround: the c2 DeSmuME-harness reference is
now `tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png`, produced from the
plain static ROM `twlhd-arm9v2-c2-st30.nds` (sha `43e39eb8…`, the ROM uploaded
to the console — hash in eq4t §2; the static autodump c2 ROM still crashes in
the harness). FACT: harness frame 242 = 12 polygons / 48 vertices, exit 0; a
second run at frame 300 is
byte-identical (frame independence). The c2 hardware capture `a` vs this
reference is PASS (mean_abs 0.4777/0.3136/0.4495, `ds.frac_within_flat=1.0000`,
`bbox_dev` 2/2/0/0), so c2 is MATCHED under the EQ-4r §7 thresholds. Tests:
the "no harness reference" test is kept (renamed
`test_c2_autodump_pair_not_comparable`; its file-absence assertion is dropped)
and a positive c2-harness test is added, and the c2 2-row-shift negative test
asserts `bbox_dev_top` 2 -> 3 (the verdict
stays PASS; c2's margin is one DS px larger than s1's). UNVERIFIED: the
autodump-vs-non-autodump reference binary identity beyond the Makefile define,
other consoles/sessions, capture-noise origin. Report:
`docs/reports/eq4t-c2-reference.md`.

## 12. Update (2026-11-25): EQ-4u — why scene 4's alpha differs (source read, no code change)

The EQ-4r scene-4 difference is separated by source read: the ROM never sets
`GL_BLEND` (`DISP3DCNT` bit 3 clear), so the DS draws `alpha 16`/`4` opaque
(HYPOTHESIS, supported by the capture and by DeSmuME's gated blend), while our
chain records `DISP3DCNT` but never consumes it and always blends; `alpha 0`
selects wireframe, which no reference draws as an outline. Proposed carriers,
the sized-not-implemented outline work and a not-applied `BLEND=1` hardware
falsification plan are in `docs/reports/eq4u-scene4-alpha-why.md`.

## 13. Update (2026-11-25): EQ-4v — blend-enabled falsification ROM

EQ-4v implemented the opt-in `BLEND=1` arm9v2 ROM variant from EQ-4u §6.3
(`arm9v2/Makefile`, `arm9v2/source/demo_frame.cpp`, `tools/test_arm9v2_blend.py`)
and documented the (not-run) hardware capture plan in
`docs/reports/eq4v-blend-rom.md`.
