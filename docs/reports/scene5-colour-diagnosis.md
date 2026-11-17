<!-- SPDX-License-Identifier: Apache-2.0 -->
# scene5 vertex-colour mismatch — diagnosis (DeSmuME 63 vs our 0)

**Scope:** DIAGNOSIS ONLY. No code, test, interpreter, renderer, ROM, harness or
DeSmuME change; the only file added is this report. Host tooling and the
out-of-tree DeSmuME CLI (`fc504ee0`, wifi off) were run as EMULATOR actions; no
console, SD-card or network action was performed. Every statement about the
real DS is a **HYPOTHESIS** or **UNVERIFIED** (section 3).

**Labels.** FACT = read at the cited `file:line` in this session, or a command
output quoted below. HYPOTHESIS = an inference from facts, not directly read or
run. UNVERIFIED = no local source exists / it was not run. Emulator facts are
facts about the *model*, never about hardware.

**Evidence paths.** DeSmuME line numbers are from the pinned checkout
`/home/one/3ds/desmume` at `release_0_9_13-429-gfc504ee0`; that checkout stores
the geometry engine at `desmume/src/gfx3d.cpp` (there is no `src/3D/`
subdirectory), so the contract's `3D/gfx3d.cpp` citations resolve to
`src/gfx3d.cpp`. libnds 2.0.0: `/home/one/3ds/libnds/source/arm9/videoGL_base.c`
(identical to `/opt/devkitpro/libnds/...`). No GBATEK text exists locally
(`find /home/one/3ds /opt/devkitpro -iname '*gbatek*'` returns nothing; the repo
says so at `docs/DS_GX_RESEARCH.md:12`, `docs/TEXTURE_CAPTURE_DESIGN.md:22`).

---

## 1. What DeSmuME uses as the current vertex colour at reset and after the ROM's init (Q1)

**FACT (EMULATOR source): the reset path ends in a white current colour.**

- `GPUSubsystem::Reset()` (`GPU.cpp:4770`) calls `gfx3d_reset()` (`GPU.cpp:4810`).
- `gfx3d_reset()` (`gfx3d.cpp:417`) calls `_gEngine.Reset()` (`gfx3d.cpp:464`).
- `NDSGeometryEngine::Reset()` (`gfx3d.cpp:657`) is just `this->__Init()`.
- `NDSGeometryEngine::__Init()` sets the current colour to white:
  `_vtxColor15 = 0x7FFF` (`:553`), `_vtxColor555X = {31,31,31,0}` (`:555-558`),
  `_vtxColor666X = {63,63,63,0}` (`:560-563`), and flags a conversion
  (`_doesVertexColorNeedUpdate = true`, `:584`).
- On the next submitted vertex the 555→666 conversion runs
  (`gfx3d.cpp:1568-1575`, alpha forced to 0 at `:1573`) and every vertex copies
  that struct: `vtx.color = this->_vtxColor666X;` (`gfx3d.cpp:1631`).

**FACT (EMULATOR source): only three commands change that state.** `COLOR`
(`gfx3d_glColor3b`, `gfx3d.cpp:2620` → `SetVertexColor`, `:1352-1363`),
`DIF_AMB` bit 15 (`SetDiffuseAmbient`, `:1158-1167`), and the lighting result of
`NORMAL` (`:1324` → same `SetVertexColor`). The ROM issues none of the three
(see section 2), so `_vtxColor666X` is still the reset value at frame 240.

**FACT (libnds 2.0.0): `glInit()` writes no `GFX_COLOR`.** `glInit()`
(`videoGL_base.c:106`) writes, in order: `powerOn(POWER_3D_CORE|POWER_MATRIX)`
(`:108`), a `GFX_STATUS` FIFO clear (`:113-122`), `glResetMatrixStack()`
(`:119`), `glFlush(0)` (`:122`), `GFX_CONTROL = 0` (`:125`), clear colour /
poly-ID / depth (`:128-133`), `GFX_TEX_FORMAT = 0`, `GFX_POLY_FORMAT = 0`
(`:135-136`), and the three matrix modes/identities (`:138-142`). `GFX_COLOR`
(`0x04000480`, defined at `video.h:730`) never appears. `grep -rn GFX_COLOR`
over the libnds tree finds only that define and the `glColor3b` / `glColor`
inline writers (`videoGL.h:569`, `:575`).

**FACT: the texture-modulation white is not this white.** `_shade()` uses
`colorWhite = {0x3F,0x3F,0x3F,0x1F}` only for *pixel* shading, and only when
sampling is disabled (`rasterize.cpp:573-574`, used by MODULATE at `:578-586`).
It never touches the vertex colour. Here sampling is enabled
(`TEXIMAGE_PARAM = 0x0C000000`, `TEX_ENABLE` set via `glEnable(GL_TEXTURE_2D)`,
section 2), so the emulated pixels used the real texels, not the synthetic
white; the dump's 63 can only be the vertex struct copied at `gfx3d.cpp:1631`.

**FACT (model cross-check):** the harness reports `col-a-u8 0`
(`tools/fixtures/eq_scenes/harness_s5_f240.txt:20-31`), which is exactly the
alpha DeSmuME forces in that same conversion (`gfx3d.cpp:1573`,
`docs/EQUALITY_CHECK_DESIGN.md:172-173`).

**Answer (Q1):** the white is **the geometry engine's reset value**. It is not a
`glInit()` write and not the texture-modulation path. FACT for DeSmuME's model
at the pinned commit.

---

## 2. Our interpreter's initial colour, the recorder's pre-arm gap, and the real capture (Q2)

### 2.1 Code

- **FACT (UNIT):** the interpreter initialises the latched RGB to 0:
  `g.rgba[0] = g.rgba[1] = g.rgba[2] = 0.0f;` (`common/twlhd_gx_interp.c:1151`),
  while alpha gets `(31+1)/32 = 1.0` (`:1152-1154`). Only
  `TWLHD_V2_CMD_COLOR` (`0x20`) moves it (`common/twlhd_gx_interp.c:913-915` →
  `set_rgba_from_rgb555`, `:399-404`).
- **FACT (UNIT):** the recorder declares the gap at init and clears nothing
  before arming. `twlhd_gx_rec_init` sets all three coverage bits, including
  `TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN` (`common/twlhd_gx_record.c:60-68`);
  `twlhd_gx_rec_arm` only resets counters/flags, sets `armed = 1` and increments
  the frame index (`:70-83`) — it does not snapshot or synthesise any current
  state. A write while `!rec->armed` is counted and dropped (`:91-94`).
- **FACT (UNIT):** in the ROM, `glInit()` runs before `rec_init`
  (`arm9v2/source/main.cpp:215` vs `:226`), and the loop arms the recorder at
  the top of every iteration (`:242`), so everything `glInit()` wrote is
  pre-arm and can never appear in `GXCM`.
- **FACT:** the documented meaning is the same: `INITIAL_STATE_UNKNOWN` =
  "state set before arming is not reproduced" (`docs/TWLHD_V2_FORMAT.md:211`),
  and the capture's META byte is `coverage 0x07`, i.e. all three bits set
  (quoted in 2.2). The proxy writes hardware unconditionally after the hook
  (`arm9v2/include/twlhd_gx_shim.hpp:102-107`), so the emulator still sees the
  unrecorded state.

### 2.2 The actual capture (regenerated in this session)

Commands (EMULATOR + host tooling):

```sh
make -C arm9v2 SCENE=5 AUTODUMP_FRAME=240          # ROM sha256 35a50aad… (matches README)
DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-build/cli/desmume-cli \
  tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-s5.nds /tmp/opencode/s5cap
python3 tools/twlhd_v2_dump.py validate <cap>/twlhd_frame_v2.bin   # result=OK, 78 GXCM, 5 RSTA
python3 tools/twlhd_v2_dump.py stats    <cap>/twlhd_frame_v2.bin
```

**FACT:** the rebuilt ROM hashes to
`35a50aadc30dbf26d7fa389192a908024cffaa515614c378e389e3efd45d82bd`, matching
`tools/fixtures/eq_scenes/README.md:39`; the capture is 952 bytes, `frame 241`,
`backend 1`, `coverage 0x07`, `78` GXCM entries, `5` RSTA entries — the same
counts as the documented 5C-3 run (`docs/TEXTURE_CAPTURE_DESIGN.md:1084-1086`).

`stats` command histogram (first/last lines elided where a matrix burst):

```
0x00000010 3   0x00000011 1   0x00000012 1   0x00000015 2   0x00000018 16
0x00000019 12  0x0000001A 18  0x0000001B 3   0x0000001C 3   0x00000022 4
0x00000023 8   0x00000029 1   0x0000002A 1   0x0000002B 1   0x00000040 1
0x00000041 1   0x00000050 1   0x00000060 1
```

**FACT: there is no `COLOR` (`0x20`) entry — not before the quad, not anywhere**
in the 78 commands; `0x21 NORMAL` and `0x30 DIF_AMB` are absent too. The first
commands and the whole draw sequence (index: id word value):

```
 0  0x60 VIEWPORT         0xBFFF0000
 1  0x10 MTX_MODE         0x00000000
 2  0x15 MTX_IDENTITY     0x00000000
 3-18  0x18 MTX_MULT_4x4  (16 words of gluPerspective)
19  0x10 MTX_MODE         0x00000002
20  0x15 MTX_IDENTITY     0x00000000
21  0x10 MTX_MODE         0x00000002
22-57  0x19/0x1A/0x1C/0x1B (gluLookAt / scale words)
58  0x11 MTX_PUSH         0x00000000
59  0x29 POLYGON_ATTR     0x001F00C0     <- MODULATE (bits 4..5 = 0), CULL_NONE, alpha 31
60  0x2A TEXIMAGE_PARAM   0x0C000000     <- I4 8x8, slot 0 (scene5's explicit write)
61  0x2B PLTT_BASE        0x00000000     <- scene5's explicit write
62  0x40 BEGIN_VTXS       0x00000001     <- QUADS
63  0x22 TEX_COORD        0x00000000
64  0x23 VTX_16           0xF000F000     <- (-1,-1)
65  0x23 VTX_16           0x00000000
66  0x22 TEX_COORD        0x00000080
67  0x23 VTX_16           0xF0001000
68  0x23 VTX_16           0x00000000
69  0x22 TEX_COORD        0x00800080
70  0x23 VTX_16           0x10001000
71  0x23 VTX_16           0x00000000
72  0x22 TEX_COORD        0x00800000
73  0x23 VTX_16           0x1000F000
74  0x23 VTX_16           0x00000000
75  0x41 END_VTXS         0x00000000
76  0x12 MTX_POP          0x00000001
77  0x50 SWAP_BUFFERS     0x00000000
```

RSTA (`addr width @gxcm_index = value`) — note clear colour and DISP3DCNT only,
no colour port: `0x04000060 w2 @0 = 0x11` (DISP3DCNT: bit 0 `GL_TEXTURE_2D` +
bit 4 `GL_ANTIALIAS`, `videoGL.h:247`, `:251`),
`0x04000350 w4 @0 = 0x3F1F0000`, `0x04000350 w4 @0 = 0x3F1F0000`,
`0x04000354 w2 @0 = 0x7FFF`, `0x04000060 w2 @62 = 0x11` (the
`glEnable(GL_TEXTURE_2D)` write, landing between `PLTT_BASE` and `BEGIN_VTXS`).

**FACT:** `GFX_COLOR` (`0x04000480`) is not in the RSTA whitelist
(`docs/TWLHD_V2_FORMAT.md:243-252`), so even a handled write could only appear
as a `0x20` GXCM entry — and none exists.

### 2.3 The two sides of the comparison

| side | vertex RGB | source |
|---|---|---|
| DeSmuME harness dump | 63,63,63 (6-bit), alpha 0 | `tools/fixtures/eq_scenes/harness_s5_f240.txt:28-31` (all four vertices `:28,:39,:50,:61`) |
| our interpreter `--eq2` | `00000000` ×3 (`k/31 = 0.0`), alpha `3F800000` | `tools/fixtures/eq_scenes/ours_s5_f240.txt:4-7` |

**FACT:** the gap is therefore exactly "engine reset default (white) vs
interpreter initial value (black)", with the recorded stream carrying no write
that could explain either.

---

## 3. The real DS hardware default (Q3)

**UNVERIFIED.** There is no local authoritative source:

- No GBATEK text exists under `/home/one/3ds` or `/opt/devkitpro` (find output
  empty); the repo documents that GBATEK was never read locally
  (`docs/DS_GX_RESEARCH.md:12`, `docs/TEXTURE_CAPTURE_DESIGN.md:22`) and exists
  only as web URLs (e.g. `docs/GEOMETRY_FIFO.md:4-5`).
- libnds does not state the power-on value: `video.h:730` is a bare address
  define and `glInit()` (section 1) deliberately writes no colour.
- `docs/DS_GX_RESEARCH.md:252` says only "Without NORMAL commands the colour is
  whatever COLOR / DIF_AMB bit 15 set" — that is a DeSmuME statement, not a
  hardware statement.

**HYPOTHESIS (weak, from a qualitative hardware run):** the real hardware
default is not black, because the owner's New 3DS run of `SCENE=5` showed the
quad "with the palette colours" (`docs/TEXTURE_CAPTURE_DESIGN.md:1135-1137`).
With MODULATE that picture implies a non-zero effective vertex colour; an
initial 0 would have rendered the quad black. The run was qualitative ("texels
were not compared pixel by pixel", same sentence) and pre-dates this question,
so it is not proof of the *default*; it only bounds it away from 0.

**Experiment that would settle it (NOT run):** a `DIAGSCREEN`-style ROM variant
(same pattern as `docs/reports/5c4-black-screen-analysis.md`) that, after
`glInit()`, clears the rear plane to a contrasting colour (e.g. blue), then
draws one **untextured, unlit quad with `POLY_CULL_NONE` and no `glColor3b`**,
with the recorder bypassed. On a real DS: white quad ⇒ hardware default white
(matches DeSmuME); black quad ⇒ DeSmuME's reset model diverges from hardware.
Run it on at least one DS Phat/Lite *and* the New 3DS in TWL mode, since only
the latter has been observed so far.

---

## 4. Impact on the picture (Q4)

**FACT (numbers):** EQ-2 scene5 colour deviation is **63 of 64 6-bit levels**
against tolerance **±2** (worst `63 (tol 2)`, `docs/EQ_SCENES.md:141`;
narrative `:239-244`; tolerance origin ±1 5-bit level = ±2 six-bit,
`tools/eq2_compare.py:50`). Geometry passes: x 0.0087 px, y 0.0148 px, w 0,
z 165343 (tol 2708990) — same line.

**FACT: no picture comparison could have caught it.**

- EQ-3 for scene5 is **NOT-COMPARABLE**, not a pass: `eq3_compare` has no
  texture model and the geometry list carries no texture state
  (`tools/fixtures/eq_scenes/manifest.json:60-61`;
  `docs/EQ_SCENES.md:255-259`; `docs/EQ3_COMPARE.md:235-237`). So no Azahar
  picture for scene5 was ever produced, and the black-primary-colour defect was
  never rendered.
- `make emu-validate-tex` / `compare_texture_capture.py` compares **texture
  bytes and stream shape only**: `TXDS`/`TXDT`/`PLDT` bytes and the
  `TEXIMAGE_PARAM`/`PLTT_BASE`/`POLYGON_ATTR`/`BEGIN`/`END` positions
  (`tools/emulator/compare_texture_capture.py:111-135`, `:157-165`). It never
  reads `0x20` — there is no colour field in it. Its 5C-3 result was
  "parsers OK (78 GXCM, 5 RSTA), comparator PASS … 32 texel + 32 palette bytes"
  (`docs/TEXTURE_CAPTURE_DESIGN.md:1084-1087`), which is exactly the scope.
- On the emulator the picture was *correct by accident*: DeSmuME's white vertex
  colour multiplied by the texels reproduces the palette colours
  (`rasterize.cpp:578-586` with `vtxColor = 63`). Numbers: output ≈
  `texel·63/63` = texel. Ours would be `texel·0/63` = 0.

**HYPOTHESIS (analysis, not run): on our PICA path the quad renders black.**
The renderer feeds the interpreter's per-vertex RGBA as the colour attribute
(`arm11/source/renderer.c:1295-1301`) and sets stage 0 of TexEnv to
`GPU_TEXTURE0` MODULATE `GPU_PRIMARY_COLOR` (`renderer.c:1313-1320`;
`docs/DS_GX_RESEARCH.md:809-821`). With primary colour 0 the TEV output for this
MODULATE quad is 0 in RGB (alpha would still come from the texture because our
alpha is 1.0). This is the "black quad" the DS itself would have drawn *if* the
hardware default were 0 — which section 3 leaves UNVERIFIED.

**FACT (scope):** on the console the ROM is drawn by the DS-mode GPU, not by
our arm11 renderer, so the hardware picture depends only on the hardware default
(section 3), not on `arm11/source/renderer.c`.

---

## 5. Recommendation (Q5 — analysis only, nothing implemented)

Three candidates, smallest first. All three need owner approval; none is
implemented here.

### (a) Interpreter initial colour = white (recommended, smallest)

Change `common/twlhd_gx_interp.c:1151` to initialise `rgba[0..2]` to `1.0f`
(`k/31` white, which `eq2_compare` reads back as 63). One line; no ROM, format
or stream change, so **no ROM sha256 and no capture content changes**.

Blast radius (enumerated, by grep — must all move together):

| File | Why it changes |
|---|---|
| `common/twlhd_gx_interp.c:1151` | the one-line fix |
| `tests/host/test_gx_interp.c:1542-1566` (`test_default_colour`) | asserts the no-COLOR stream yields 0,0,0; expectation must become 1.0 (a corrected expectation, not a weakened assertion) |
| `tools/gx_interp_oracle.py:368` | the independent oracle also starts `self.colour = (0.0, 0.0, 0.0)`; must match or every no-colour oracle test diverges |
| `tools/fixtures/eq_scenes/ours_s5_f240.txt:4-7` (+ its sha256 in `tools/fixtures/eq_scenes/README.md:27`) | regenerate: colours become `3F800000`; EQ-2 colour deviation becomes 0 |
| `tools/test_eq_scenes.py:131-137` | pins the current finding ("MISMATCH r: 63 > 2", "1 FAIL"); must be updated to the corrected expectation. This deliberately removes the only automated guard on the initial-state gap, so the reason belongs in the commit message |
| `docs/EQ_SCENES.md:141`, `:239-259` | table row verdict and the "suspected cause" narrative |

Unaffected (checked): scenes 1-4, cube and CUBES issue explicit `glColor3b`
(`arm9v2/source/demo_frame.cpp:81`, `:268`, `:301`, `:328-332`), so
`ours_s1/s4`, `ours_cube_f240.txt` and `harness_*` keep their colours; the
lighting tests set the colour through `NORMAL`/`DIF_AMB`
(`tools/test_gx_interp_golden.py:460-520`), and the golden cube fixture contains
`COLOR` commands. Residual risk: the fix matches DeSmuME's **model**, not
verified hardware; `coverage 0x07` (bit 2) stays set in every future capture,
so the state remains unrecorded — it is *assumed*, not captured.

### (b) ROM scene5 issues `glColor3b(31,31,31)` (or 255,255,255)

Larger, and it hides the general gap behind one scene. Blast radius: the GXCM
stream gains a `0x20` entry (command counts, `78` → `79`), the ROM sha256
`35a50aad…` changes (`tools/fixtures/eq_scenes/README.md:39`), the committed
`ours_s5_f240.txt` and its hash change, and at least
`tests/host/test_demo_frame.cpp:336-338` (which today asserts scene5 has **no**
`COLOR`) flips meaning, plus the same `tools/test_eq_scenes.py:131-137` and
`docs/EQ_SCENES.md` updates as (a). It also leaves every future uncoloured scene
with the same 0 default, so the next textured scene repeats this diagnosis.

### (c) Record `glInit()`/pre-arm writes

Not minimal and partly impossible: `glInit()` is out-of-line libnds code writing
raw ports, so the proxy shim never sees it
(`arm9v2/include/twlhd_gx_shim.hpp:102-107`; `arm9v2/source/main.cpp:215`), and
`0x04000480` is a FIFO command port outside the RSTA whitelist
(`docs/TWLHD_V2_FORMAT.md:243-252`), so there is no supported read-back to
snapshot. It would need a libnds rebuild/interposition or a format extension
(new readback backend), and it still has to *decide* what the initial value is —
which is exactly the UNVERIFIED hardware question of section 3.

**Recommended:** (a), with the approval of the EQ/interpreter owner stage
(stage ownership convention in `docs/PHASE5B_PLAN.md`, e.g. stage 5B-1 owns
interpreter colour output, `:94-135`). Do it only together with the section 3
experiment or an explicit note that the value is DeSmuME's model; otherwise the
report's UNVERIFIED label would silently become an assumption in code.

---

CAUSE: DeSmuME's dump carries its geometry engine's reset current colour (white,
`_vtxColor666X = {63,63,63,0}` set in `__Init`) because the ROM and the capture
contain no `COLOR`/`NORMAL`/`DIF_AMB` command at all, while our interpreter
starts its latched colour at 0 and the v2 format cannot carry pre-arm state — so
the 63-vs-0 gap is an unrecorded-initial-state difference between the two
**models** (FACT for both at the cited lines; the real-DS default is UNVERIFIED,
section 3).

## Open items

1. **Real-DS default colour is UNVERIFIED.** Needs the section 3 ROM variant on
   hardware (DS Phat/Lite and New 3DS TWL); no local GBATEK or libnds statement
   exists. Until then, option (a) encodes DeSmuME's model, not hardware.
2. **Harness internals were not read** (out-of-tree GPL tool, licence posture
   `docs/EQ_SCENES.md:15-19`). The claim "the harness reports DeSmuME's vertex
   struct" rests on the value match (63,63,63,0 vs `gfx3d.cpp:560-563,1573`);
   it is FACT for the values, HYPOTHESIS for the plumbing.
3. **The alpha half of the same mismatch is out of scope.** The harness reports
   `col-a-u8 0` (DeSmuME forces vertex alpha 0, `gfx3d.cpp:1573`) while our
   interpreter emits `3F800000` from `POLYGON_ATTR` alpha 31 (`(a+1)/32`,
   `common/twlhd_gx_interp.c:395-409`); it is not gated by EQ-2 and was not
   diagnosed here (`docs/EQ2_COMPARE.md:51`).
4. **No picture-level reproducer exists.** EQ-3 for scene5 is NOT-COMPARABLE
   (`manifest.json:60-61`), so the black-modulate consequence on the PICA path is
   analysis only; a scene5 textured EQ-3 would be needed to see it.
5. **`coverage bit 2` stays set after any of the fixes** — the initial state
   remains uncaptured; option (a) only makes the two models agree, it does not
   record anything.
