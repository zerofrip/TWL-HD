<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4z: change arm11 top-screen clear colour to black

Labels: **FACT**/**RUN** = output from the command quoted next to it; **EMULATOR** = Azahar host render or host test; **HARDWARE** = real New 3DS capture; **UNVERIFIED** = not reproduced here.  In quoted outputs the path tokens `<worktree>`, `<old-worktree>`, `<tmpdir>`, `<outdir>`, `<old-outdir>`, `<staging>`, `<logfile>`, `<core.so>` and `<redacted>` replace machine-specific paths, and `<ppm>`, `<capture>` and `<captured.bin>` stand for command arguments as the tools name them; nothing else was substituted.

## 1. Purpose and basis

SOURCE-READ — basis `docs/reports/eq4y-gate-hw-result.md` §6/§10(b): arm11 cleared the top screen to `(16,16,24)` (`0x101018FF`) while the real DS background is `(0,0,0)`, the largest single term in the remaining blend gap (HARDWARE green 142 vs DS 134; blue ~57-60 vs DS 33).  This implements option (b): match the DS black, update every pinned baseline, leave bottom-screen/console text untouched.

UNVERIFIED — no hardware run of the new `.3dsx`; one console, one capture session; bottom screen untouched.

## 2. Source table: every clear-colour pin

Narrow greps run on this tree:

```
$ git grep -n -E 'RENDER_CLEAR_COLOR' -- arm11/source arm11/Makefile
arm11/Makefile:90:#   make -C arm11 -B TWLHD_EXTRA_CFLAGS=-DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu
arm11/source/renderer.c:1195:                              TWLHD_RENDER_CLEAR_COLOR, 0);
arm11/source/renderer.c:1543:                          TWLHD_RENDER_CLEAR_COLOR, 0);
exit=0

$ git grep -n -E '16, ?16, ?24|0x101018' -- tools/eq3_compare.py tools/hw/predict_lit.py tests/host docs/HARDWARE_TEST_PROCEDURE_3DS.md
docs/HARDWARE_TEST_PROCEDURE_3DS.md:84:`0x000000`; EQ-4z changed the default from the old `0x101018` on 2026-11-25)
docs/HARDWARE_TEST_PROCEDURE_3DS.md:729:  although the clear colour is (16,16,24); compare only within one photo):
docs/HARDWARE_TEST_PROCEDURE_3DS.md:778:Measured with the NTR-HR screen capture (tools/ntr_capture, exact RGB from the JPEG stream, no camera) on the real New 3DS, diag build `twlhd-arm11-diag.3dsx` (sha256 9bf74bc6…, build id `e6e6c88-diag`), SD frame scene4_alpha (sha256 2decd74d…), clear colour (16,16,24). Mean RGB over a 9x9 patch per quad (JPEG, so about +-2):
docs/HARDWARE_TEST_PROCEDURE_3DS.md:782:| 0 normal, measured | (12,12,59) | (252,0,0) | (6,142,10) | (16,16,24) = background |
tests/host/test_render_clear.c:71:    CHECK_EQ_INT(count_substr(buf, "0x101018FFu"), 0);
tests/host/test_render_clear.c:155:    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_COLOR, 0x101018FFu);
tools/eq3_compare.py:75:# The old dark blue-grey clear (16, 16, 24) is deliberately rejected by the
tools/eq3_compare.py:78:# Squared-distance cap for a pixel to count as background.  (16,16,24) is
tools/hw/predict_lit.py:49:# (0,0,0,255); Run 17 measured the old build as (16,16,24) over the top screen.
exit=0
```

Tree-wide search over every path that could pin the colour (counts, plus the discussed lines quoted below):

```
$ git grep -c -E '0x101018|16, ?16, ?24|16,16,24|0x10, ?0x10, ?0x18' -- arm11 common tests tools docs/EQ3_COMPARE.md
arm11/Makefile:1
arm11/include/twlhd_render_config.h:2
docs/EQ3_COMPARE.md:5
tests/host/test_render_clear.c:2
tools/check_tex_behaviour.py:2
tools/check_tex_formats.py:1
tools/check_tex_shot.py:2
tools/eq3_compare.py:2
tools/eq4o_hw_rows/README.md:2
tools/eq4o_hw_rows/measure_rows.py:1
tools/eq4o_hw_rows/test_measure_rows.py:5
tools/eq4y_gate_hw/test_eq4y_gate_hw.py:2
tools/fixtures/eq3/README.md:1
tools/gen_tex_behaviour_frames.py:1
tools/hw/README.md:3
tools/hw/predict_lit.py:1
tools/test_check_tex_shot.py:1
tools/test_eq3_compare.py:2
tools/test_tex_behaviour.py:2
tools/test_tex_formats.py:1
exit=0
$ git grep -c -E '0x101018|16, ?16, ?24|16,16,24|0x10, ?0x10, ?0x18' -- common tools/pica_frame_oracle.py tools/test_pica_frame_golden.py tools/azahar_host
exit=1
```

The second search prints nothing: `common/`, `tools/pica_frame_oracle.py`, `tools/test_pica_frame_golden.py` and `tools/azahar_host/` hold no clear-colour literal. `tests/check_tex_*` does not exist (`ls` exits 2); the texture checkers are `tools/check_tex_*.py`.

Per-row producing commands, raw output:

```
$ sed -n '98,99p' arm11/include/renderer.h
#define TWLHD_RDBG_SKIP_DRAW         0x01u
#define TWLHD_RDBG_SKIP_CLEAR        0x02u
$ sed -n '10,11p' arm11/source/diag_main.c
 * UI FLAGS (8 toggleable rows; hidden flags still work if set by other code):
 *   SKIP_DRAW      -> do not call C3D_DrawArrays.
$ sed -n '1224,1224p' arm11/source/renderer.c
    if (!(g_debug_flags & TWLHD_RDBG_SKIP_DRAW)) {
$ sed -n '506,506p' arm11/source/main.c
    consoleInit(GFX_BOTTOM, NULL);
$ sed -n '185,188p' tools/hw/predict_lit.py
        "clear_colour": list(CLEAR_RGBA8),
        "background": {
            "pixel_centre": list(BACKGROUND_CENTRE),
            "rgba8": list(CLEAR_RGBA8),
$ git grep -n -i 'clear' -- tools/eq4w_blend_hw/measure_blend_hw.py; echo "exit=$?"
exit=1
```

The empty last output confirms `measure_blend_hw.py` has no clear-colour constant.  HARDWARE Run 17, quoted whole:

```
$ sed -n '776,788p' docs/HARDWARE_TEST_PROCEDURE_3DS.md
## Run 17 (2026-11-08): numeric NTR measurement — the console DOES alpha-blend (corrects Runs 14-16)

Measured with the NTR-HR screen capture (tools/ntr_capture, exact RGB from the JPEG stream, no camera) on the real New 3DS, diag build `twlhd-arm11-diag.3dsx` (sha256 9bf74bc6…, build id `e6e6c88-diag`), SD frame scene4_alpha (sha256 2decd74d…), clear colour (16,16,24). Mean RGB over a 9x9 patch per quad (JPEG, so about +-2):

| mode | blue (a 0.156) | red (a 1.0) | green (a 0.531) | white (a 0) |
|---|---|---|---|---|
| 0 normal, measured | (12,12,59) | (252,0,0) | (6,142,10) | (16,16,24) = background |
| 0 predicted for standard alpha blend over the background | (13,13,61) | (255,0,0) | (8,143,11) | background |
| 1 alpha-as-gray, measured | (40,40,40) | (255,255,254) | (136,135,136) | (0,0,0) |
| 2 const-alpha-0.5, measured | (7,8,140) | (137,8,10) | (7,136,12) | (135,135,139) |
| 2 predicted for a 50 % blend | (8,8,140) | (136,8,12) | (8,136,12) | (136,136,140) |

HARDWARE OBSERVED (numeric): the console blends vertex alpha correctly: mode 0 matches the standard blend within about 2 levels, mode 1 shows grey = alpha x 255 (40 / 136 / 255 / 0), mode 2 matches a 50 % blend for all four quads. The alpha-0 quad is "not visible" in mode 0 simply because its blend weight is 0 (it equals the background); it is not discarded. The earlier statements "the console does NOT alpha-blend / the alpha 16 and 4 quads are opaque / cause UNKNOWN" (Runs 14, 15, 16) were WRONG: they came from reading colours off camera photos, where ambient light lifted dark levels and made faint blends look saturated. There is no console blend defect to diagnose. Still unverified: the DS `(alpha + 1) / 32` scale and DS translucent-polygon depth/ordering rules.
```

BEFORE (clean `main`):

```
$ git show main:arm11/source/renderer.c | grep -nE '0x101018|RENDER_CLEAR_COLOR'
41:#define RENDER_CLEAR_COLOR 0x101018FFu
1195:                              RENDER_CLEAR_COLOR, 0);
1543:                          RENDER_CLEAR_COLOR, 0);
```

| location | what it pins | must change? | why |
|---|---|---|---|
| `arm11/source/renderer.c:41` | `#define RENDER_CLEAR_COLOR 0x101018FFu` | **yes** | local `#define` deleted; the header is now the only definition |
| `arm11/source/renderer.c:1195,1543` | `C3D_RenderTargetClear(..., TWLHD_RENDER_CLEAR_COLOR, 0)` (DEBUG / clip paths) | **yes** | uses header macro; clear gated by `TWLHD_RDBG_SKIP_CLEAR` |
| `arm11/include/twlhd_render_config.h:30-32` | default `0x000000FFu` under `#ifndef TWLHD_RENDER_CLEAR_COLOR` | **yes** | new single source of truth |
| `arm11/include/renderer.h:98-99`, `arm11/source/diag_main.c:10-11`, `arm11/source/main.c:506` | diag `SKIP_DRAW`/`SKIP_CLEAR` flags; `consoleInit(GFX_BOTTOM, NULL)` | no | colour-independent diag flags; bottom screen only |
| `tools/eq3_compare.py:74-81` | BEFORE `BACKGROUND_COLOURS = ((0,0,0),(16,16,24))` (AFTER `((0,0,0),)` + `BG_MAX_CHANNEL = 24`) | **yes** | black only; cap rejects `(16,16,24)` (distance² 1088 > 576) |
| `tools/test_eq3_compare.py:43-44,269-285` | `OURS_OLD_BG = (16,16,24)` negative test | **yes** | old colour must fail classification |
| `tools/fixtures/eq3/ours_cube_f240_top.ppm`, `tools/fixtures/eq3/README.md:37-42` | BEFORE cube render on `(16,16,24)` and its sha256 | **yes** | regenerated from the new build; new sha256 quoted |
| `tools/hw/predict_lit.py:48-52,185-188`, `tools/test_hw_lit_predict.py:75-96` | BEFORE `CLEAR_RGBA8 = (16,16,24,255)` + JSON `clear_colour`/`background`; expected `[0,0,0,255]` twice | **yes** | flipped to black; `tools/hw/README.md` corrected (no C macro effect) |
| `tools/hw/README.md:57,118,151,163`, `docs/HARDWARE_TEST_PROCEDURE_3DS.md:84` | Run-17 measurements and the dark blue-grey expected output `0x101018` | **yes** | annotate as OLD-build; `0x000000` + dated EQ-4z note |
| `docs/EQ3_COMPARE.md:102-106`, `docs/EQUALITY_CHECK_DESIGN.md:178-182`, `docs/EQ_SCENES.md:725-727` | historical text pinning `(16,16,24)` / the old clear site | no | text unchanged; appended pointer gives the new state |
| `tools/azahar_presentation.py:43-51,69-85` | `auto` wrap detection relies on `row0 != row239`; uniform black rows make it inconclusive | **yes** | explicit `undetermined (uniform border rows)` mode |
| `docs/reports/eq4y-gate-hw-result.md:438` | §10(b) recommended (a) first, then (b) | **yes** | append-only pointer |
| `tools/check_tex_shot.py:26,61`, `tools/test_check_tex_shot.py:49`, `tools/check_tex_formats.py:47`, `tools/test_tex_formats.py:47`, `tools/check_tex_behaviour.py:11,59`, `tools/test_tex_behaviour.py:42,230`, `tools/gen_tex_behaviour_frames.py:286` | `CLEAR = (16, 16, 24)` and `(16, 16, 24)` palette entries for generated 5B-2e-2 frames and old hardware JPEG captures | no | `check_tex_shot.py:59-60`: `# OLD-build clear colour for this synthetic/old-capture validator`; those are generated frames/old captures, not an arm11 render (see below) |
| `tools/eq4y_gate_hw/test_eq4y_gate_hw.py:67,87` | asserts `census=(16, 16, 24) 92969` on the committed hardware crops | no | class docstring `:52`: `"""The committed real-hardware crops; every number is an EQ-4y FACT."""` → old captures |
| `tools/eq4o_hw_rows/measure_rows.py:20`, `tools/eq4o_hw_rows/test_measure_rows.py:52,199,246,252,277`, `tools/eq4o_hw_rows/README.md:54,73` | OLD-build hardware captures | no | `measure_rows.py:20-21`: `This is the clear colour of the OLD build that produced the Run-16/17 captures` |
| `tests/host/test_render_clear.c:71,104,155` | `:71` negative check (old literal appears 0 times in sources); `:104` header defines `TWLHD_RENDER_CLEAR_COLOR` exactly once; `:155` override build compiled with `-DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu` | no | `:71` `CHECK_EQ_INT(count_substr(buf, "0x101018FFu"), 0)`; the new default is asserted at `:20` |
| `tools/pica_frame_oracle.py`, `tools/test_pica_frame_golden.py`, `common/`, `tools/azahar_host/`, `arm9v2/`, `arm7/` | no clear-colour pin (second `git grep -c` above is empty) | no | `test_pica_frame_golden.py` docstring: `run the C CLI on generated frames`; out-of-scope trees are not in the pathspec |

Files that validate renders of the current arm11 build were the ones changed: `tools/eq3_compare.py`, `tools/test_eq3_compare.py`, `tools/hw/predict_lit.py` and the default at `tests/host/test_render_clear.c:20`.  Everything in the "no" rows above reads only old captures or synthetic frames.

### Diagnostics that rely on non-black clear

Run 17 records the mode-0 alpha-0 quad as `(16,16,24) = background`, mode-1 white as `(0,0,0)`; the diag flags separate clearing from drawing (`renderer.c:1195,1224`; `renderer.h:98-99`; `diag_main.c:10-11`) and are colour-independent.  Mode-0 detection/census is **not broken** (the DS background is already `(0,0,0)`); the mode-1 alpha-0 quad is **ambiguous**, mitigated by building the diag `.3dsx` with `TWLHD_EXTRA_CFLAGS=-DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu`.  Bottom screen untouched.

## 3. Design: single macro, overridable, explicit correction mode

SOURCE-READ — new freestanding header (no 3ds includes, so host tests need no `DEVKITPRO`):

```
$ sed -n '30,32p' arm11/include/twlhd_render_config.h
#ifndef TWLHD_RENDER_CLEAR_COLOR
#define TWLHD_RENDER_CLEAR_COLOR 0x000000FFu
#endif
```

`renderer.c` deletes the local `#define RENDER_CLEAR_COLOR 0x101018FFu` and includes the header; both clear sites pass `TWLHD_RENDER_CLEAR_COLOR`.  Override without editing source via the append-style hook (never `CFLAGS=...`, which overwrites the devkitPro flags):

```sh
make -C arm11 -B TWLHD_EXTRA_CFLAGS=-DTWLHD_RENDER_CLEAR_COLOR=0x101018FFu
```

`test_render_clear.c:test_makefile_has_append_hook` asserts `CFLAGS += $(TWLHD_EXTRA_CFLAGS)`; alpha stays `0xFF`.  Presentation-row detection: uniform black border rows make `auto` inconclusive, so the module returns an explicit mode:

```
$ sed -n '146,148p' tools/azahar_presentation.py
    wrapped, uniform = detect_wrap_state(pixels, width, height)
    if uniform:
        return pixels, False, "row-0 wrap undetermined (uniform border rows)"
```

`tools/eq_scenes.py` and `tools/eq4g_fit_compare.py` default to `--azahar-correction on` (their reference is always an Azahar render); `tools/eq3_compare.py` and `tools/eq4_hw_compare.py` keep `auto` as the CLI default; a sidecar marker `<ppm>.azahar-corrected` prevents double correction.

## 4. Before / after baselines

BEFORE is a clean checkout of `main` at `3af9629`; AFTER is this work branch.

### 4.1 Source

BEFORE (`arm11/source/renderer.c:41`) `#define RENDER_CLEAR_COLOR 0x101018FFu`. AFTER (`arm11/include/twlhd_render_config.h:30-32`):

```c
#ifndef TWLHD_RENDER_CLEAR_COLOR
#define TWLHD_RENDER_CLEAR_COLOR 0x000000FFu
#endif
```

### 4.2 `tools/eq3_compare.py`

```
$ git show main:tools/eq3_compare.py | grep -n 'BACKGROUND_COLOURS'
75:BACKGROUND_COLOURS = ((0, 0, 0), (16, 16, 24))
$ git show main:tools/eq3_compare.py | grep -c 'BG_MAX_CHANNEL'; echo "exit=$?"
0
exit=1
$ grep -n 'BACKGROUND_COLOURS\|BG_MAX_CHANNEL' tools/eq3_compare.py
77:BACKGROUND_COLOURS = ((0, 0, 0),)
81:BG_MAX_CHANNEL = 24
exit=0
```

The cap rejects `(16,16,24)`: distance² = 16² + 16² + 24² = 1088 > 24² = 576.

### 4.3 Cube fixture

```
$ git show main:tools/fixtures/eq3/ours_cube_f240_top.ppm | sha256sum
134a6a8b3f2d76f0b795bd3f198b75cdaa06a0606603b8f607bbf69d41df3120  -
$ sha256sum tools/fixtures/eq3/ours_cube_f240_top.ppm
32d583f3eb6558353667bd41a9d437014c9ddf59625380082536f6cc0274ba06  tools/fixtures/eq3/ours_cube_f240_top.ppm
exit=0
```

The AFTER fixture is the top 400×240 crop of `<outdir>/cube_199.ppm` (sha256 `e3730b865a733bbb2beb04895e900c17b024a119de70919784155ee87d0df25a`). The same crop is byte-identical from the earlier 39b30c2-build screenshot `7c93689d…`; the two full screenshots differ only in the bottom 240 rows (§8).

### 4.4 `docs/HARDWARE_TEST_PROCEDURE_3DS.md:84`

```
$ git show main:docs/HARDWARE_TEST_PROCEDURE_3DS.md | sed -n '83,84p'
**Top screen (400×240, 3D disabled):** a dark blue-grey background (clear colour
`0x101018`) with **one triangle**, static, no animation. The three vertices are red, green
$ sed -n '83,85p' docs/HARDWARE_TEST_PROCEDURE_3DS.md
**Top screen (400×240, 3D disabled):** a black background (clear colour
`0x000000`; EQ-4z changed the default from the old `0x101018` on 2026-11-25)
with **one triangle**, static, no animation. The three vertices are red, green
exit=0
```

`tools/hw/predict_lit.py` and `tools/test_hw_lit_predict.py`:

```
$ git show main:tools/hw/predict_lit.py | grep -n 'CLEAR_RGBA8 ='
50:CLEAR_RGBA8 = (16, 16, 24, 255)
$ grep -n 'CLEAR_RGBA8 =' tools/hw/predict_lit.py
50:CLEAR_RGBA8 = (0, 0, 0, 255)
$ git show main:tools/test_hw_lit_predict.py | sed -n '75,78p'
                self.assertEqual(document["clear_colour"],
                                 [16, 16, 24, 255])
                self.assertEqual(document["background"]["rgba8"],
                                 [16, 16, 24, 255])
$ sed -n '75,78p' tools/test_hw_lit_predict.py
                self.assertEqual(document["clear_colour"],
                                 [0, 0, 0, 255])
                self.assertEqual(document["background"]["rgba8"],
                                 [0, 0, 0, 255])
exit=0
```

The JSON emitter (`tools/hw/predict_lit.py:185,188`, `"clear_colour": list(CLEAR_RGBA8)`) is unchanged in both; only the constant moved, so the emitted `clear_colour`/`background.rgba8` is `(16,16,24,255)` BEFORE and `(0,0,0,255)` AFTER.

### 4.5 Append-only pointers

`docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ_SCENES.md`, `docs/EQ3_COMPARE.md`, `docs/EQ4_HW_COMPARE.md` and `docs/reports/eq4y-gate-hw-result.md` are unchanged except for one appended pointer each:

```
$ git diff main --numstat -- docs/EQ3_COMPARE.md docs/EQUALITY_CHECK_DESIGN.md docs/EQ_SCENES.md docs/EQ4_HW_COMPARE.md docs/reports/eq4y-gate-hw-result.md
2	0	docs/EQ3_COMPARE.md
2	0	docs/EQ4_HW_COMPARE.md
2	0	docs/EQUALITY_CHECK_DESIGN.md
2	0	docs/EQ_SCENES.md
2	0	docs/reports/eq4y-gate-hw-result.md

$ git diff main -- docs/EQ3_COMPARE.md docs/EQUALITY_CHECK_DESIGN.md docs/EQ_SCENES.md docs/EQ4_HW_COMPARE.md docs/reports/eq4y-gate-hw-result.md | grep -c '^-[^-]'; echo exit=$?
0
exit=1
```

Zero non-context deletion lines relative to `main`.

## 5. EMULATOR verification

The original arm11 build command is not recoverable: only a build-log tail survives, with no command line.  The artefact every render used is `<worktree>/arm11/twlhd-arm11.3dsx`:

```
$ sha256sum <worktree>/arm11/twlhd-arm11.3dsx
17eface3968108727ccc17f14ff2c0f20415df78980eacd951aeecd2adebf74e  <worktree>/arm11/twlhd-arm11.3dsx
$ strings <worktree>/arm11/twlhd-arm11.3dsx | grep -E '^[0-9a-f]{7}(-diag)?$'
3cd3bb0-diag
```

That is build ID `3cd3bb0-diag`, built at commit `3cd3bb0b6c795defb27a0848d8e20c54832d5377` (`3cd3bb0`).  To quote a build command and a real exit status, one clean rebuild was made in a scratch worktree of HEAD `d77ac1891aafa810c263520ea452091fd2fa5339`:

```
$ git worktree add --detach <tmpdir>/eq4z-rebuild HEAD
Preparing worktree (detached HEAD d77ac18)
$ make -C <tmpdir>/eq4z-rebuild arm11 > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  381 |                                           rec.palette_size) ||
      |                                           ~~~~~~~~~~~~~~~~~~^~
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    
<tmpdir>/eq4z-rebuild/arm11/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
<tmpdir>/eq4z-rebuild/arm11/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make[1]: Leaving directory '<tmpdir>/eq4z-rebuild/arm11'
make: Leaving directory '<tmpdir>/eq4z-rebuild'
$ sha256sum <tmpdir>/eq4z-rebuild/arm11/twlhd-arm11.3dsx
bdd0f2562f69c5c44f315817410e1c04f8a10aa80c9690a936e639861eb734f0  <tmpdir>/eq4z-rebuild/arm11/twlhd-arm11.3dsx
$ strings <tmpdir>/eq4z-rebuild/arm11/twlhd-arm11.3dsx | grep -E '^[0-9a-f]{7}(-diag)?$'
d77ac18-diag
$ git worktree remove --force <tmpdir>/eq4z-rebuild
$ git worktree list | grep -c eq4z-rebuild; echo "exit=$?"
0
exit=1
```

The rebuild 3dsx (`bdd0f256…`, build ID `d77ac18-diag`) differs from the render artefact (`17eface3…`, `3cd3bb0-diag`) because the build-ID string is compiled in; no `arm11/` or `common/` source differs between the commits.  Every render and measurement below was made with the earlier artefact `17eface3…`; the rebuild produced none of them.  Provenance, plainly: build commit `3cd3bb0…`, HEAD `d77ac18…`.

```
$ git rev-parse HEAD
d77ac1891aafa810c263520ea452091fd2fa5339
$ git diff --stat 3cd3bb0..HEAD -- arm11 common tests tools Makefile
 tools/fixtures/eq3/README.md | 13 ++++++++++---
 1 file changed, 10 insertions(+), 3 deletions(-)
```

The only post-build change is the fixture README (documentation); no arm11, common or tests source changed after `3cd3bb0`.  The normal (non-DIAG) target embeds `<short-hash>-diag` unconditionally; `DIAG=1` only swaps TARGET/BUILD (`arm11/Makefile:51-54`):

```
$ sed -n '75,84p' arm11/Makefile
# Build ID stamp for the ARM11 3DSX. Shown on the bottom-screen console so the
# owner can verify the right .3dsx started. Short git hash (always used, even on
# a dirty tree) with a "-diag" suffix; missing git resolves to "nogit-diag".
TWLHD_GIT_HASH := $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null)
ifeq ($(strip $(TWLHD_GIT_HASH)),)
TWLHD_BUILD_ID_STR := nogit-diag
else
TWLHD_BUILD_ID_STR := $(TWLHD_GIT_HASH)-diag
endif
CFLAGS += -DTWLHD_BUILD_ID=\"$(TWLHD_BUILD_ID_STR)\"
```

So the normal target legitimately embeds `3cd3bb0-diag`; only a no-git build yields `nogit-diag`.

FACT — staging frame hashes, re-hashed read-only.  The two s4 staging frames still exist; the scene-5 `<staging>/sd30/twlhd_frame.bin` no longer exists, so its hash is the value recorded at render time (the same file survives as `<outdir>/sd-s5/twlhd_frame.bin` below):

```
$ sha256sum <staging>/frame_s4_noblend.bin <staging>/frame_s4_blend.bin <staging>/sd30/twlhd_frame.bin; echo "exit=$?"
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  <staging>/frame_s4_noblend.bin
ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5  <staging>/frame_s4_blend.bin
sha256sum: <staging>/sd30/twlhd_frame.bin: No such file or directory
exit=1
```

The scene-5 frame is the EQ-4f static st30 frame (`docs/reports/eq4f-scene5-static-hw-compare.md:59-61,660-661`), the DeSmuME capture of autodump ROM `twlhd-arm9v2-autodump-s5-st30.nds` (sha256 `30337896a415a433376f8478db1ebe973ce401f4bb657d9d9b878bb27ff2063e`, line 38). No earlier report records the frame file's own sha256 (`git grep -n '86bec0d0' -- docs/reports tools` prints nothing, exit 1).

RECONSTRUCTION (not FACT) — the four `--sd-root` input frames.  The cube source is the repo fixture and the two s4 sources still exist, but the scene-5 `<staging>/sd30/twlhd_frame.bin` is gone, so the `cp` lines are the step shape recorded at render time, not a command re-run here; the surviving sd-root files were re-hashed read-only and equal the staging hashes above:

```
$ cp tests/host/fixtures/demo_frame_desmume_v2.bin <outdir>/sd-cube/twlhd_frame.bin
$ cp <staging>/frame_s4_noblend.bin <outdir>/sd-s4/twlhd_frame.bin
$ cp <staging>/frame_s4_blend.bin <outdir>/sd-s4bl/twlhd_frame.bin
$ cp <staging>/sd30/twlhd_frame.bin <outdir>/sd-s5/twlhd_frame.bin
$ sha256sum <outdir>/sd-cube/twlhd_frame.bin <outdir>/sd-s4/twlhd_frame.bin <outdir>/sd-s4bl/twlhd_frame.bin <outdir>/sd-s5/twlhd_frame.bin
acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5  <outdir>/sd-cube/twlhd_frame.bin
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  <outdir>/sd-s4/twlhd_frame.bin
ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5  <outdir>/sd-s4bl/twlhd_frame.bin
86bec0d09f8eaeb0364099a6bfc5c63b3ff92294ac95973abeeb7fa066975157  <outdir>/sd-s5/twlhd_frame.bin
exit=0
```

The s4/s4bl/s5 hashes equal the staging hashes above; the cube frame is `tests/host/fixtures/demo_frame_desmume_v2.bin` (sha256 `acbdb48e…`, `tools/fixtures/eq3/README.md:28`).

FACT — crop each `*_199.ppm` to the top 400×240 rows (the repo's accepted PIL crop form; no `python3 -c`):

```
$ python3 - <<'EOF'
from PIL import Image
for name in ('cube', 's4', 's4bl', 's5'):
    img = Image.open(f'<outdir>/{name}_199.ppm').convert('RGB')
    img.crop((0, 0, 400, 240)).save(f'<outdir>/{name}_top400x240.png')
EOF
exit=0
$ sha256sum <outdir>/cube_top400x240.png <outdir>/s4_top400x240.png <outdir>/s4bl_top400x240.png <outdir>/s5_top400x240.png
271a4f632bbeddf797a32ce8918473c06cddf673e553ed8a3c7d021268711de4  <outdir>/cube_top400x240.png
0bfc0354c8fc22361181ced551c0928ec3fd430bea30c7937bf61b7e032ccc36  <outdir>/s4_top400x240.png
4946280025b792c71fba24aa345b9414b9714bc1cc948445452e85a1b6bc4966  <outdir>/s4bl_top400x240.png
31db860886f9bbc3655ddbf64b0e365dda7017f8cc4824f28bd5983d35c9151e  <outdir>/s5_top400x240.png
exit=0
```

FACT — four Azahar renders (one frame each, new 3DSX, core `<core.so>`); quoted: the command, its exit status and the full unedited `tail log` output from the surviving per-render logs:

```
$ tools/azahar_host/build/azahar_host --core <core.so> --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <outdir>/cube --sd-root <outdir>/sd-cube > log 2>&1; echo "exit=$?"; tail log
exit=0
[azahar_host] wrote <outdir>/cube_last.ppm (400x480)
[azahar_libretro] [   0.993223] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_unload_game:646: Unloading game...
[azahar_libretro] [   0.994580] Kernel <Info> core/hle/kernel/process.cpp:~Process:692: Cleaning up process 11
[azahar_libretro] [   0.995056] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:658: Freeing process memory region 0x00000000 - 0x07C00000
[azahar_libretro] [   0.995059] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:670: Freeing process TLS memory region 0x0E003000 - 0x0E004000
[azahar_libretro] [   0.995059] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:682: Remaining memory used after process cleanup: 0x00000000
[azahar_libretro] [   0.995060] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.995060] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.027548] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.027553] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...

$ tools/azahar_host/build/azahar_host --core <core.so> --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <outdir>/s4 --sd-root <outdir>/sd-s4 > log 2>&1; echo "exit=$?"; tail log
exit=0
[azahar_host] wrote <outdir>/s4_last.ppm (400x480)
[azahar_libretro] [   0.713386] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_unload_game:646: Unloading game...
[azahar_libretro] [   0.714743] Kernel <Info> core/hle/kernel/process.cpp:~Process:692: Cleaning up process 11
[azahar_libretro] [   0.715211] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:658: Freeing process memory region 0x00000000 - 0x07C00000
[azahar_libretro] [   0.715213] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:670: Freeing process TLS memory region 0x0E003000 - 0x0E004000
[azahar_libretro] [   0.715214] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:682: Remaining memory used after process cleanup: 0x00000000
[azahar_libretro] [   0.715214] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.715214] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.744611] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   0.744615] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...

$ tools/azahar_host/build/azahar_host --core <core.so> --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <outdir>/s4bl --sd-root <outdir>/sd-s4bl > log 2>&1; echo "exit=$?"; tail log
exit=0
[azahar_host] wrote <outdir>/s4bl_last.ppm (400x480)
[azahar_libretro] [   0.721318] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_unload_game:646: Unloading game...
[azahar_libretro] [   0.722746] Kernel <Info> core/hle/kernel/process.cpp:~Process:692: Cleaning up process 11
[azahar_libretro] [   0.723288] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:658: Freeing process memory region 0x00000000 - 0x07C00000
[azahar_libretro] [   0.723290] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:670: Freeing process TLS memory region 0x0E003000 - 0x0E004000
[azahar_libretro] [   0.723291] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:682: Remaining memory used after process cleanup: 0x00000000
[azahar_libretro] [   0.723291] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.723291] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.751890] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   0.751894] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...

$ tools/azahar_host/build/azahar_host --core <core.so> --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <outdir>/s5 --sd-root <outdir>/sd-s5 > log 2>&1; echo "exit=$?"; tail log
exit=0
[azahar_host] wrote <outdir>/s5_last.ppm (400x480)
[azahar_libretro] [   0.872930] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_unload_game:646: Unloading game...
[azahar_libretro] [   0.874429] Kernel <Info> core/hle/kernel/process.cpp:~Process:692: Cleaning up process 11
[azahar_libretro] [   0.874945] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:658: Freeing process memory region 0x00000000 - 0x07C00000
[azahar_libretro] [   0.874948] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:670: Freeing process TLS memory region 0x0E003000 - 0x0E004000
[azahar_libretro] [   0.874949] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:682: Remaining memory used after process cleanup: 0x00000000
[azahar_libretro] [   0.874949] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.874949] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   0.904195] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   0.904201] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
```

FACT — `measure_blend_hw.py` on the new s4 no-blend render, re-run read-only on the surviving crop (extra run); the filter is part of the quoted command, so the output shown is exactly what that command printed:

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py <outdir>/s4_top400x240.png | grep -E '^(tool|basis|image|quad[0-9]\.(source_rgb|measured|blended|majority_rgb|classification|bright_pixels|interior_bright_pixels)|census)'
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
quad0.source_rgb=255,0,0 alpha=31
quad0.majority_rgb=255,0,0
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.majority_rgb=0,255,0
quad1.measured=255
quad1.blended=no
quad2.source_rgb=0,0,255 alpha=4
quad2.majority_rgb=0,0,255
quad2.measured=255
quad2.blended=no
quad3.source_rgb=255,255,255 alpha=0
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_pixels=5
quad3.interior_bright_pixels=0
census_top=16
census=(0, 0, 0) 92966
census=(0, 0, 255) 1411
census=(255, 0, 0) 981
census=(0, 255, 0) 642
```

FACT — the same filtered `measure_blend_hw.py` command on the new s4 blend render, re-run read-only on the surviving crop (extra run):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py <outdir>/s4bl_top400x240.png | grep -E '^(tool|basis|image|quad[0-9]\.(source_rgb|measured|blended|majority_rgb|classification|bright_pixels|interior_bright_pixels)|census)'
tool=measure_blend_hw
basis=one console, one capture session; scene=4 static_angle=30
image=400x240
quad0.source_rgb=255,0,0 alpha=31
quad0.majority_rgb=255,0,0
quad0.measured=255
quad0.blended=no
quad1.source_rgb=0,255,0 alpha=16
quad1.majority_rgb=0,135,0
quad1.measured=135
quad1.blended=yes
quad2.source_rgb=0,0,255 alpha=4
quad2.majority_rgb=0,0,40
quad2.measured=40
quad2.blended=yes
quad3.source_rgb=255,255,255 alpha=0
quad3.majority_rgb=0,0,0
quad3.classification=none
quad3.bright_pixels=5
quad3.interior_bright_pixels=0
census_top=16
census=(0, 0, 0) 92966
census=(0, 0, 40) 1411
census=(255, 0, 0) 981
census=(0, 135, 0) 642
```

HYPOTHESIS — EQ-4y §10(b) predicted green 142→≈135 (DS 134) and blue 57→≈40 (DS 33).  The host measurement is green `135` and blue `40`, matching the prediction; the residual blue gap (40 vs DS 33) is output quantisation.

FACT — `eq4_hw_compare.py` runs: `--capture` is the committed hardware/DS PNG, `--reference` the fresh Azahar P6 PPM, and `--azahar-correction on` corrects the reference only (`tools/eq4_hw_compare.py:573-577`):

```
$ sed -n '573,577p' tools/eq4_hw_compare.py
        width, height, pixels = parse_ppm(args.reference)
        marker = (args.reference
                  if args.azahar_correction == "on" else None)
        pixels, applied, reason = azahar_presentation.apply_correction(
            args.azahar_correction, pixels, width, height, marker=marker)
```

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png --reference <outdir>/s4_199.ppm --azahar-correction on > log 2>&1; echo "exit=$?"; grep -E 'presentation-row|mean_abs|verdict|bbox' log
exit=1
presentation-row correction: applied (--azahar-correction on (forced))
ds.mean_abs_r=0.5403
ds.mean_abs_g=0.5538
ds.mean_abs_b=0.6872
capture.mean_abs_r=0.4425
capture.mean_abs_g=0.5070
capture.mean_abs_b=0.6309
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_bbox_left=82
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
bbox_dev_left=1
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)

$ python3 tools/eq4_hw_compare.py --capture tools/eq4y_gate_hw/hw_eq4x_nb_a.png --reference <outdir>/s4_199.ppm --azahar-correction on > log 2>&1; echo "exit=$?"; grep -E 'presentation-row|mean_abs|verdict|bbox' log
exit=0
presentation-row correction: applied (--azahar-correction on (forced))
ds.mean_abs_r=15.6065
ds.mean_abs_g=15.5976
ds.mean_abs_b=23.3340
capture.mean_abs_r=15.5929
capture.mean_abs_g=15.5831
capture.mean_abs_b=23.3011
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_bbox_left=83
capture_bbox_top=72
capture_bbox_right=158
capture_bbox_bottom=130
bbox_dev_left=0
bbox_dev_top=0
bbox_dev_right=1
bbox_dev_bottom=2
verdict=PASS
reason=within tolerance

$ python3 tools/eq4_hw_compare.py --capture tools/eq4w_blend_hw/hw_s4bl_a.png --reference <outdir>/s4bl_199.ppm --azahar-correction on > log 2>&1; echo "exit=$?"; grep -E 'presentation-row|mean_abs|verdict|bbox' log
exit=1
presentation-row correction: applied (--azahar-correction on (forced))
ds.mean_abs_r=0.5403
ds.mean_abs_g=0.4338
ds.mean_abs_b=0.4553
capture.mean_abs_r=0.4425
capture.mean_abs_g=0.4226
capture.mean_abs_b=0.4430
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_bbox_left=83
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
bbox_dev_left=0
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)

$ python3 tools/eq4_hw_compare.py --capture tools/eq4y_gate_hw/hw_eq4x_bl_a.png --reference <outdir>/s4bl_199.ppm --azahar-correction on > log 2>&1; echo "exit=$?"; grep -E 'presentation-row|mean_abs|verdict|bbox' log
exit=0
presentation-row correction: applied (--azahar-correction on (forced))
ds.mean_abs_r=15.7532
ds.mean_abs_g=15.7557
ds.mean_abs_b=23.4382
capture.mean_abs_r=15.7395
capture.mean_abs_g=15.7472
capture.mean_abs_b=23.4382
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_bbox_left=83
capture_bbox_top=72
capture_bbox_right=158
capture_bbox_bottom=130
bbox_dev_left=0
bbox_dev_top=0
bbox_dev_right=1
bbox_dev_bottom=2
verdict=PASS
reason=within tolerance

$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png --reference <outdir>/s5_199.ppm --azahar-correction off > log 2>&1; echo "exit=$?"; grep -E 'presentation-row|mean_abs|verdict|bbox' log
exit=0
presentation-row correction: not applied (--azahar-correction off)
ds.mean_abs_r=12.2021
ds.mean_abs_g=12.3986
ds.mean_abs_b=16.7850
capture.mean_abs_r=12.2662
capture.mean_abs_g=12.4525
capture.mean_abs_b=16.8201
ref_bbox_left=16
ref_bbox_top=17
ref_bbox_right=181
ref_bbox_bottom=162
capture_bbox_left=16
capture_bbox_top=17
capture_bbox_right=182
capture_bbox_bottom=163
bbox_dev_left=0
bbox_dev_top=0
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
```

Scene-5 uses `off` because both inputs are raw Azahar crops: the fixture `tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png` came from an EQ-4f Azahar render and was never shifted (provenance `docs/reports/eq4f-scene5-static-hw-compare.md:67-84`; unchanged sha256 `85e97da974748e175657f86f9632dc061c55807af0f5553e3a6c1311323e9f34`), and `<outdir>/s5_199.ppm` is the fresh render above.

OLD / NEW / DS comparison (scene 4, static angle 30):

| quantity | OLD (eq4x §5) | NEW (EQ-4z) | DS hardware |
|---|---|---|---|
| background census | `(16,16,24) 92965` (`eq4x-blend-gate.md:609,723`) | `(0,0,0) 92966` (`census=(0, 0, 0) 92966`) | `(0,0,0)` |
| non-blend quad1 green | `(0,255,0)` (`eq4x-blend-gate.md:665-667`) | `255` (`quad1.measured=255`, `blended=no`) | `255` |
| non-blend quad2 blue | `(0,0,255)` (`eq4x-blend-gate.md:665-667`) | `255` (`quad2.measured=255`, `blended=no`) | `255` |
| blend quad1 green | `(7,142,11)` (`eq4x-blend-gate.md:610,724`) | `135` (`quad1.measured=135`, `blended=yes`) | `134` |
| blend quad2 blue | `(13,13,60)` (`eq4x-blend-gate.md:611,725`) | `40` (`quad2.measured=40`, `blended=yes`) | `33` |
| `eq4_hw_compare` non-blend vs DS `mean_abs_r/g/b` | `15.9668 / 16.7612 / 26.9291` (`eq4x-blend-gate.md:839-841`) | `0.5403 / 0.5538 / 0.6872` | — |
| `eq4_hw_compare` blend vs DS `mean_abs_r/g/b` | `15.9668 / 15.8522 / 23.6745` (`eq4x-blend-gate.md:1037-1039`) | `0.5403 / 0.4338 / 0.4553` | — |
| `eq4_hw_compare` vs DS verdict | `FAIL` — `bbox-top` 10 px and `bbox-right` 4 px (tol 3); `bbox_dev_right=4` at `eq4x-blend-gate.md:899`, reason at `:902` | `FAIL` — same `bbox-top` 10 px and `bbox-right` 4 px (non-blend and blend: this report's `bbox_dev_right=4` above) | — |

What moved toward the DS: the background is exact `(0,0,0)`; blend green moved `142` → `135` (DS `134`) and blue `~57-60` → `40` (DS `33`).  What did not move: non-blend quads stayed `255`, and both bbox offsets are the pre-existing eq4x §5.6 offsets.  The OLD run already failed both axes — `docs/reports/eq4x-blend-gate.md:899:bbox_dev_right=4` with `:902:reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)`, identical at `:1009,1097,1185` and summarised by the §7 prose at `:1291-1293` — and the NEW runs show the same `bbox-top=10`/`bbox-right=4` against both DS fixtures.  Nothing about `bbox-right` is new, no tolerance was changed, and the FAIL cause is the same pre-existing capture-vs-reference offset (the identical capture fixture also reports `capture_bbox_top=62` at `eq4x-blend-gate.md:864` and in this report's non-blend run above).

FACT — cube baseline.  AFTER (this work branch, `--azahar-correction on` because uniform black rows defeat `auto`):

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

BEFORE (clean `main` in a scratch worktree, old fixture `134a6a8b…`, `auto` default):

```
$ git worktree add --detach <tmpdir>/eq4z-before main
Preparing worktree (detached HEAD 3af9629)
$ python3 <tmpdir>/eq4z-before/tools/eq3_compare.py <tmpdir>/eq4z-before/tools/fixtures/eq3/harness_cube_f240.ppm <tmpdir>/eq4z-before/tools/fixtures/eq3/ours_cube_f240_top.ppm --azahar-correction auto
EQ-3b structural picture comparison (EMULATOR)
presentation-row correction: applied (row-0 wrap signature detected)
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
$ git worktree remove --force <tmpdir>/eq4z-before
$ git worktree list | grep -c eq4z-before; echo "exit=$?"
0
exit=1
```

With uniform black rows `auto` cannot detect the wrap, so AFTER uses `on`, which restores the EQ-4p corrected bbox/centroid; BEFORE's `auto` detects the wrap on the old dark-blue-grey border.

FACT — scene-5 colour census (both images):

```
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
(16, 16, 24) 67777
(0, 0, 0) 16638
(255, 255, 255) 5276
(255, 0, 0) 1815
(0, 255, 8) 1556
(0, 0, 247) 1486
(255, 255, 24) 1452

$ python3 tools/ds_scale_test/colour_histogram.py <outdir>/s5_top400x240.png
(0, 0, 0) 84415
(255, 255, 255) 5276
(255, 0, 0) 1815
(0, 255, 8) 1556
(0, 0, 247) 1486
(255, 255, 24) 1452
```

The OLD fixture's `(16,16,24) 67777` + `(0,0,0) 16638` = `84415` matches the NEW single `(0,0,0) 84415`; every other colour count matches exactly.

## 6. Regression runs

FACT — four targets, each run once; tails as quoted.  The host-test log survived in the scratch directory (`sha256sum <tmpdir>/host_test.log` = `620d244a…`, `wc -l` = 4430); its unedited 15-line tail, including the two `test_blend_gate_wiring` result lines, is quoted below.

```
$ make test > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
ARM9V2_STATIC_DISCOVER_SKIP=1 ARM9V2_BLEND_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
ssssssss.....ssssssssssssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 435 tests in 77.699s

OK (skipped=110)
<tmpdir>/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS

$ make host-test > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
ok   diag-config/cycle
ok   diag-config/source-files/main
ok   diag-config/source-files/renderer
7 test(s) passed
== tests/host/build/test_render_clear
ok   render-clear/default-colour
ok   render-clear/source-renderer
ok   render-clear/makefile-hook
3 test(s) passed
== tests/host/build/test_render_clear_override
ok   render-clear/override-colour
1 test(s) passed
== tests/host/build/test_blend_gate_wiring
ok   blend-gate-wiring/call-sites
1 test(s) passed
```

FACT — the new header-single-definition check, run alone (exit status unpiped); an additional single run of `test_render_clear`, made after the `render-clear/header-defines-once` test was added:

```
$ make tests/host/build/test_render_clear > <logfile> 2>&1; echo "make exit=$?"
make exit=0
$ ./tests/host/build/test_render_clear; echo "exit=$?"
ok   render-clear/default-colour
ok   render-clear/source-renderer
ok   render-clear/header-defines-once
ok   render-clear/makefile-hook
4 test(s) passed
exit=0
```

FACT — a further full `make host-test` run, made after the `render-clear/header-defines-once` test was added (exit status captured without a pipe); its unedited 15-line tail covers the final test set:

```
$ make host-test > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
ok   diag-config/source-files/main
ok   diag-config/source-files/renderer
7 test(s) passed
== tests/host/build/test_render_clear
ok   render-clear/default-colour
ok   render-clear/source-renderer
ok   render-clear/header-defines-once
ok   render-clear/makefile-hook
4 test(s) passed
== tests/host/build/test_render_clear_override
ok   render-clear/override-colour
1 test(s) passed
== tests/host/build/test_blend_gate_wiring
ok   blend-gate-wiring/call-sites
1 test(s) passed
```

FACT — the EQ-3 suite, one extra run after the `classify` docstring edit (no code change):

```
$ PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq3_compare.py'
..................
----------------------------------------------------------------------
Ran 18 tests in 1.847s

OK
```

```
$ DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli make emu-validate > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
+ '[' '!' -f tests/host/build/emu/capture/fat/twlhd_frame_v2.bin ']'
+ cp tests/host/build/emu/capture/fat/twlhd_frame_v2.bin tests/host/build/emu/capture/twlhd_frame_v2.bin
+ echo 'capture: tests/host/build/emu/capture/twlhd_frame_v2.bin'
capture: tests/host/build/emu/capture/twlhd_frame_v2.bin
tools/emulator/validate_with_parsers.sh tests/host/build/emu/capture/twlhd_frame_v2.bin
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<worktree>/common <worktree>/tests/host/v2_dump_cli.c <worktree>/common/twlhd_v2_parse.c -o <tmpdir>/v2_dump_cli
+ <tmpdir>/v2_dump_cli tests/host/build/emu/capture/twlhd_frame_v2.bin
+ python3 <worktree>/tools/twlhd_v2_dump.py validate tests/host/build/emu/capture/twlhd_frame_v2.bin
+ python3 <worktree>/tools/twlhd_v2_dump.py stats tests/host/build/emu/capture/twlhd_frame_v2.bin
+ set +x
parsers: OK (entries=124 rsta=4)
python3 tools/emulator/compare_capture.py \
	tests/host/build/emu/capture/twlhd_frame_v2.bin \
	tests/host/build/emu/expected_frame.txt 61440
PASS: 124 GXCM + 4 RSTA entries match for angle 61440

$ DESMUME_CLI=<redacted>/desmume-build/cli/desmume-cli make emu-validate-tex > <logfile> 2>&1; echo "exit=$?"; tail -n 15 <logfile>
exit=0
+ 7z x -y tests/host/build/emu/capture-tex/cflash.img -otests/host/build/emu/capture-tex/fat
+ '[' '!' -f tests/host/build/emu/capture-tex/fat/twlhd_frame_v2.bin ']'
+ cp tests/host/build/emu/capture-tex/fat/twlhd_frame_v2.bin tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ echo 'capture: tests/host/build/emu/capture-tex/twlhd_frame_v2.bin'
capture: tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
tools/emulator/validate_with_parsers.sh tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I<worktree>/common <worktree>/tests/host/v2_dump_cli.c <worktree>/common/twlhd_v2_parse.c -o <tmpdir>/v2_dump_cli
+ <tmpdir>/v2_dump_cli tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ python3 <worktree>/tools/twlhd_v2_dump.py validate tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ python3 <worktree>/tools/twlhd_v2_dump.py stats tests/host/build/emu/capture-tex/twlhd_frame_v2.bin
+ set +x
parsers: OK (entries=78 rsta=5)
python3 tools/emulator/compare_texture_capture.py \
	tests/host/build/emu/capture-tex/twlhd_frame_v2.bin 61440
PASS: SCENE=5 texture 0x0C000000/0x00000000, 32 texel + 32 palette bytes for angle 61440
```

## 7. Console PLAN (not run, UNVERIFIED)

Use `arm11/twlhd-arm11.3dsx` (sha256 `17eface3…`, build ID `3cd3bb0-diag`); copy `<staging>/frame_s4_noblend.bin` or `<staging>/frame_s4_blend.bin` to `sdmc:/twlhd_frame.bin`; launch, press `A`, capture the top screen with cc3dsfs, crop to 400x240, convert the PNG references to P6 PPM, then run:

```
python3 tools/eq4w_blend_hw/measure_blend_hw.py <capture>.png
python3 tools/eq4_hw_compare.py --capture <capture>.png --reference <outdir>/s4_a_top400x240.ppm --azahar-correction off
python3 tools/eq4_hw_compare.py --capture <capture>.png --reference <outdir>/hw_eq4x_nb_a.ppm --azahar-correction off
```

Expected: no-blend background `(0,0,0) 92966`, quads `255`/`255` unblended; blend green `135`, blue `40`, both `blended=yes`.  `off` is used because both references are hardware crops, which must not be corrected.

## 8. Extra runs

Two builds have surviving renders.  The fixture's recorded full screenshot is from the earlier build at HEAD `39b30c23277315086173fd4cf3f7dc8b89584e70` (dirty worktree, build ID `39b30c2-diag`, 3dsx sha256 `4478db80…`, full screenshot `7c93689d…`; `tools/fixtures/eq3/README.md`).  Every §5 render and the §7 console PLAN use the later build at HEAD `3cd3bb0` (build ID `3cd3bb0-diag`, 3dsx sha256 `17eface3…`, full screenshot `e3730b86…`).  The committed crop `32d583f3…` is byte-identical from both full screenshots, so the fixture does not depend on which build supplied it.  Evidence (read-only):

```
$ sha256sum <outdir>/cube_199.ppm <old-outdir>/cube_199.ppm
e3730b865a733bbb2beb04895e900c17b024a119de70919784155ee87d0df25a  <outdir>/cube_199.ppm
7c93689dbdf507b72acc49fdb48fb17699c18e098d3f2638d3b4f62e97a32f31  <old-outdir>/cube_199.ppm
$ { printf 'P6\n400 240\n255\n'; tail -c +16 <outdir>/cube_199.ppm | head -c 288000; } | sha256sum
32d583f3eb6558353667bd41a9d437014c9ddf59625380082536f6cc0274ba06  -
$ { printf 'P6\n400 240\n255\n'; tail -c +16 <old-outdir>/cube_199.ppm | head -c 288000; } | sha256sum
32d583f3eb6558353667bd41a9d437014c9ddf59625380082536f6cc0274ba06  -
$ cmp -n 288015 <old-outdir>/cube_199.ppm <outdir>/cube_199.ppm && echo first-288015-identical
first-288015-identical
$ cmp -l <old-outdir>/cube_199.ppm <outdir>/cube_199.ppm | wc -l
240
$ strings <old-worktree>/arm11/twlhd-arm11.3dsx | grep -E '^[0-9a-f]{7}-diag$'
39b30c2-diag
$ strings <worktree>/arm11/twlhd-arm11.3dsx | grep -E '^[0-9a-f]{7}-diag$'
3cd3bb0-diag
```

`git diff --stat 3cd3bb0..HEAD -- arm11 common tests tools Makefile` lists only `tools/fixtures/eq3/README.md` (documentation), so no compiled source changed after the clean build.  The separate HEAD `d77ac18` rebuild quoted in §5 (`bdd0f256…`, build ID `d77ac18-diag`) produced no render.  The BEFORE cube run used the scratch worktree of `main` (§5, removed afterwards).  The scene-5 input is the EQ-4f st30 frame (sha256 `86bec0d0…`); no earlier report records that frame file's own hash.

## 9. Size audit

```
$ git add -N . && git diff main | wc -c
103664
$ wc -l docs/reports/eq4z-clear-black.md
896 docs/reports/eq4z-clear-black.md
```

Hygiene: no console/ROM run; no `python3 -c`; no absolute home paths, user names, host names, e-mails, keys, tokens, passwords, ROM/BIOS/firmware/media files or copied proprietary text were added; the quoted fixture is TWL-HD's own `tools/azahar_host` output.
