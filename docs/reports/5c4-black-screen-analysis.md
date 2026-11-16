<!-- SPDX-License-Identifier: Apache-2.0 -->
# 5C-4 arm9v2 black top screen — analysis + DIAGSCREEN bisection variants

**Scope:** BUILD/UNIT only. No DS, no emulator, no console/SD/network action was
performed by the assistant. Every statement about real hardware is a
**HYPOTHESIS** until the owner runs the attached ROMs.

**Symptom:** the `arm9v2` demo ROMs (default cube and `SCENE=5`) show a black
top screen on a New 3DS in TWL mode launched via TWiLight Menu++ /
nds-bootstrap-hb. The bottom-screen console is alive (frame counter, GXCM/RSTA
counts, flags, drop/ignore/invalid all zero/expected), and dumped v2 frames are
correct. Only the 3D picture on the top screen is missing.

**Key logical constraint:** `arm9v2/include/twlhd_gx_shim.hpp` is a *recording*
proxy. Every proxied GX write is first passed to `twlhd_gx_rec_hook` (the
recorder) and only then forwarded to hardware via `TWLHD_SHIM_HW_WRITE`.
Therefore the recorder cannot skip, swallow, or blank a hardware write: the
hook may return early with an ignored/invalid count, but the unconditional
hardware write follows it on every path. Any DS-side cause of a black screen
must be a setup/draw write that stock libnds examples make and our demo does
not, or a launch-environment difference between how the examples and our ROMs
start. The proxy itself cannot blank the screen.

---

## 1. Proxy-forwarding audit (FACT)

| Register class | How it reaches hardware | Evidence |
|---|---|---|
| Direct GX ports (`GFX_*`, `MATRIX_*`) | `Port::operator=` calls `twlhd_gx_rec_hook`, then writes via `TWLHD_SHIM_HW_WRITE` | `arm9v2/include/twlhd_gx_shim.hpp:102-107` |
| Array GX ports (`GFX_TOON_TABLE`, etc.) | `ArrayPort::Element::operator=` / `ArrayPort::write` call the hook, then write hardware | `twlhd_gx_shim.hpp:135-138`, `:171-176` |
| FIFO command port (`GFX_FIFO`, `0x04000400`) | shim forwards width-4 writes to hardware, then recorder logs them | `twlhd_gx_shim.hpp:195-196`; `common/twlhd_gx_record.c:96-103` |
| `GFX_FLUSH` / SWAP_BUFFERS (`0x04000540`) | shim forwards the write; recorder maps it to command id `0x50` and marks frame complete | `twlhd_gx_shim.hpp:255-256`; `common/twlhd_gx_record.c:117-134` |
| Packed FIFO macros (`FIFO_*` / `REG2ID`) | **Deliberately not proxied** (`operator&` deleted); demo does not use them | `twlhd_gx_shim.hpp:27-29` |

Writes that the recorder counts as *ignored* or *invalid* still reach hardware:

| Case | Recorder action | Hardware write suppressed? | Evidence |
|---|---|---|---|
| Recorder disarmed (`!rec->armed`) | `rec->ignored_writes++`, return | **No** — the hardware write happens after the hook returns | `common/twlhd_gx_record.c:91-94`; shim `operator=` order above |
| FIFO width ≠ 4 | `ignored_writes++`, return | **No** | `twlhd_gx_record.c:96-103` |
| Misaligned / non-4-byte / unknown cmd id | `invalid_writes++`, return | **No** | `twlhd_gx_record.c:105-115` |
| Non-whitelisted RSTA address | `ignored_writes++`, return | **No** | `twlhd_gx_record.c:141-146` |

`glInit()` is libnds out-of-line. Its internal writes (power-on, matrix reset,
clear colour/depth, `GFX_CONTROL`, etc.) hit hardware directly and are **not**
recorded, which is why the recorder declares `TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN`
(`common/twlhd_gx_record.c:65-67`, `arm9v2/source/main.cpp:173/190/216`).

---

## 2. Per-difference comparison with libnds examples

Sources read for this comparison (in-repo copies are kept in `docs/reports/`):

- `Simple_Tri`: `/opt/devkitpro/examples/nds/Graphics/3D/Simple_Tri/source/main.cpp`
- `Textured_Quad`: `/opt/devkitpro/examples/nds/Graphics/3D/Textured_Quad/source/main.cpp`
- libnds headers: `/opt/devkitpro/libnds/include/nds/arm9/video.h`,
  `/opt/devkitpro/libnds/include/nds/arm9/videoGL.h`,
  `/opt/devkitpro/libnds/include/nds/system.h`

Our code references are against the current `arm9v2` source.

| # | Difference | Our code | Example code | Could blank top screen? | Fact / Hypothesis |
|---|---|---|---|---|---|
| 1 | `videoSetMode` / BG0-3D bit | `videoSetMode(MODE_0_3D)` at `main.cpp:172/187/213` | Same: `videoSetMode(MODE_0_3D)` at `Simple_Tri:10`, `Textured_Quad:17` | No. Identical value (`MODE_0_3D = 0x10000 \| DISPLAY_BG0_ACTIVE \| ENABLE_3D`, `video.h:476`). | **FACT** |
| 2 | Which physical screen shows the main engine | Neither our demo nor the examples call `lcdMainOnTop()` / `lcdSwap()` | Same | Unknown. If the launch environment leaves the main engine mapped to the bottom screen, both would be affected unless something resets it. | **FACT** (no call either side); screen assignment hypothesis is **HYPOTHESIS** |
| 3 | `consoleDemoInit` usage | Called **after** `videoSetMode` and **before** `glInit` at `main.cpp:184/189/214` | **Not called** by either example | Unlikely by itself (`consoleDemoInit` targets the sub engine / VRAM C, `console.h:228` comment). It is a real difference and moves `glInit` later. | **FACT**; top-screen effect is **HYPOTHESIS** |
| 4 | Init-time 3D state order | Default: `videoSetMode -> consoleDemoInit -> glInit -> rec_init`; per-frame state is re-issued inside `demo_frame.cpp` | `videoSetMode -> glInit`, then `glEnable(GL_ANTIALIAS)`, clear, viewport, projection, lookAt **once at init** (`Simple_Tri:13-33`, `Textured_Quad:20-50`) | Possibly. We re-issue the same state per frame, but the *initial* state before the first frame differs, and `glInit` runs after `consoleDemoInit` instead of immediately after `videoSetMode`. | **FACT**; black-screen causality is **HYPOTHESIS** |
| 5 | `glEnable(GL_ANTIALIAS)` | Per frame at `demo_frame.cpp:341/383/418` | Once at init at `Simple_Tri:16`, `Textured_Quad:26` | No. Same control bit ends up set before draw. | **FACT** |
| 6 | Clear colour / polygon ID / depth | `glClearColor(0,0,0,31)`, `glClearPolyID(63)`, `glClearDepth(GL_MAX_DEPTH)` per frame (`demo_frame.cpp:342-344` etc.) | Same values (`Simple_Tri:19-21`, `Textured_Quad:29-31`); `GL_MAX_DEPTH == 0x7FFF` (`videoGL.h:87`) | No. Identical values. | **FACT** |
| 7 | Viewport | `glViewport(0,0,255,191)` per frame (`demo_frame.cpp:345-346/387-388/422-423`) | Same once at init (`Simple_Tri:24`, `Textured_Quad:34`) | No. Identical value. | **FACT** |
| 8 | Polygon format alpha | `glPolyFmt(POLY_ALPHA(31))` per frame (`demo_frame.cpp:365/403/447`) | `POLY_ALPHA(31) \| POLY_CULL_NONE` (`Simple_Tri:50`) or `\| POLY_CULL_BACK` (`Textured_Quad:73`) | No. Alpha is the same full-opaque value; cull mode does not blank the whole screen. | **FACT** |
| 9 | Projection / camera matrix | `gluPerspective(70, 256/192, 0.1, 100.0)`, camera z=4 (`demo_frame.cpp:390-392/394-396`) | `gluPerspective(70, 256/192, 0.1, 40.0)`, camera z=1 (`Simple_Tri:27-33`) | No. Different view volume but object is in view in both; would produce a picture, not black. | **FACT** |
| 10 | `glFlush(0)` path | Proxied `GFX_FLUSH` write (`demo_frame.cpp:380/407/452`) | Raw `GFX_FLUSH` write (`Simple_Tri:78`, `Textured_Quad:106`) | No. The proxy forwards the `0x04000540` write identically. | **FACT** |
| 11 | Default cube: VRAM bank setup | None (`SCENE=0` path does not touch VRAM) | `Simple_Tri` does not touch VRAM | N/A for the default cube. | **FACT** |
| 12 | Textured scene: VRAM bank setup | `vramSetBankA(VRAM_A_LCD)`, `vramSetBankE(VRAM_E_LCD)`, copy, then `VRAM_A_TEXTURE`/`VRAM_E_TEX_PALETTE` at init (`demo_frame.cpp:173-182`) | `vramSetBankA(VRAM_A_TEXTURE)` once at init (`Textured_Quad:36`); no palette bank because it uses 16bpp direct colour | For `SCENE=5` only. The final mapping is equivalent for texture; palette bank is required by our I4 format. Could matter if the LCDC→texture switch races the first 3D frame. | **FACT**; black-screen causality for s5 is **HYPOTHESIS** |
| 13 | Textured scene: texture upload path | Manual 16-bit copy to `0x06800000`/`0x06880000` + explicit `GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` writes through proxy (`demo_frame.cpp:195-196`) | `glGenTextures`/`glBindTexture`/`glTexImage2D` out-of-line (`Textured_Quad:38-40`) | No. The same hardware registers end up written; proxy forwards ours. | **FACT** |
| 14 | Textured scene: material / lighting | Not issued | `glMaterialf`, `glMaterialShinyness` (`Textured_Quad:64-70`) | No. Missing material calls would change shading, not blank the screen. | **FACT** |
| 15 | Source include order / proxy bypass | `#include "twlhd_gx_shim.hpp"` **before** `<nds.h>` (`main.cpp:20-31`, `demo_frame.cpp:19-27`) | Only `<nds.h>`; no proxy | No. The proxy forwards every write; the hardware byte stream is identical for the same high-level calls. | **FACT**; a compiler/TU codegen side effect is **HYPOTHESIS** tested by `DIAGSCREEN=2` |
| 16 | libnds out-of-line writes we never issue | None beyond `glInit` for the cube; for s5 we manually issue `GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` | `glInit` for both; `glTexImage2D` and material calls for `Textured_Quad` | For the cube, none identified. For s5, our manual writes cover the registers the example sets through `glTexImage2D`. | **FACT** |

---

## 3. Ranked cause list (all hypotheses until hardware)

1. **Missing/different display-init sequence** — our default order is
   `videoSetMode -> consoleDemoInit -> glInit -> rec_init`, and we rely on
   per-frame state re-issue rather than setting AA/clear/viewport/projection at
   init time the way the examples do. `DIAGSCREEN=1` replicates the example
   order. **HARDWARE UNVERIFIED.**

2. **Launch-environment screen/power state** — TWiLight Menu++ /
   nds-bootstrap-hb may leave POWCNT / VRAM bank mappings in a state the stock
   examples tolerate because of an extra setup call we lack (e.g. an implicit
   power-on or a screen-swap default). Neither we nor the examples call
   `lcdMainOnTop()`, but our homebrew may be launched through a different code
   path. `DIAGSCREEN=4` forces `lcdMainOnTop()`. **HARDWARE UNVERIFIED.**

3. **SCENE=5-specific VRAM / texture timing** — the I4 texture path maps VRAM A
   and E through LCDC, copies data, then flips them to texture/palette. The
   Textured_Quad example uses a 16bpp texture and only maps bank A. The extra
   bank remap, or the timing relative to the first 3D frame, could in theory
   blank the top screen for `SCENE=5`. `DIAGSCREEN=3` settles the VRAM bank
   switch one full frame before the first draw by waiting for vblank after
   upload; for the default cube it is a no-op beyond the banner. **HARDWARE
   UNVERIFIED.**

4. **Compiler / TU-layout side effect of the C++ proxy** — the shim turns every
   `GFX_*` macro into a template `Port<addr,width>` object. While the hardware
   writes are identical, code generation, inlining, or stack layout around the
   draw loop could differ enough to matter on real hardware. `DIAGSCREEN=2`
   compiles the same source with raw register macros and the recorder disabled.
   **HARDWARE UNVERIFIED.**

5. **ARM11 / PICA-side or firmware environment** — if the owner runs our ROMs
   under a patched TWL-HD firmware/arm11 setup that is not used for the stock
   examples, the difference may be outside the ARM9 ROM entirely. No ARM9/ARM11
   code change can test this; it needs owner clarification. **HARDWARE
   UNVERIFIED**, documented as an owner question below.

---

## 4. Owner questions

1. Is the New 3DS running any TWL-HD-specific patch, custom firmware hook, or
   arm11 payload when our ROMs are launched, but **not** when the stock libnds
   examples are launched?
2. What are the exact TWiLight Menu++ and nds-bootstrap-hb versions/settings?
   In particular, is there a "force DS mode / DSi mode / screen swap" option
   that differs between our `.nds` and the examples?
3. Does the black-top-screen symptom also appear with the `DIAGSCREEN=1` ROM
   (example display setup order)? With `DIAGSCREEN=4` (`lcdMainOnTop`)?
4. For `SCENE=5`, does `DIAGSCREEN=3` (VRAM bank switch settled one vblank
   before the first draw) change the picture? Does `DIAGSCREEN=2` (recorder
   bypass) produce a picture?
5. If all six `DIAGSCREEN` variants still show a black top screen, the cause
   is almost certainly outside the ARM9 ROM and must be investigated in the
   launch environment or firmware.

---

## 5. `DIAGSCREEN` variant semantics

All variants are opt-in via `make -C arm9v2 DIAGSCREEN=N ...`. `DIAGSCREEN=0`
or unset builds the normal ROM and keeps it byte-identical to the pre-change
ROM. Values containing whitespace are rejected by the Makefile's raw-vs-stripped
check; note that Make itself strips leading/trailing spaces from command-line
assignments, so the rejection test passes such values through the environment.

| Variant | What it changes | Code location |
|---|---|---|
| `DIAGSCREEN=1` | Replicates the libnds example display-setup order: `videoSetMode -> glInit -> glEnable(GL_ANTIALIAS) / clear / viewport / projection / lookAt -> consoleDemoInit -> recorder init`. | `arm9v2/source/main.cpp:168-184` |
| `DIAGSCREEN=2` | **Recorder proxy bypass:** includes `<nds.h>` before the shim so libnds inline helpers compile against raw `GFX_*` macros; never arms the recorder; console prints "recorder disabled". | `arm9v2/source/main.cpp:20-31`, `:226-230`; `arm9v2/source/demo_frame.cpp:19-27` |
| `DIAGSCREEN=3` | Settle VRAM mapping/timing before the first 3D frame: for `SCENE=5` it waits one vblank after texture upload so the LCDC→texture/palette switch is committed before the first draw; for the default cube it is a no-op beyond the banner. | `arm9v2/source/main.cpp:219-224`, `:232-238` |
| `DIAGSCREEN=4` | Forces the main engine onto the top physical screen with `lcdMainOnTop()`. | `arm9v2/source/main.cpp:185-190` |
| `DIAGSCREEN=5` | Default cube demo with `consoleDemoInit()`, every `iprintf` / `consoleClear` call and the recorder removed; no `fatInitDefault`. The variant number is not printed (the ROM file name carries it). | `arm9v2/source/main.cpp:20-31`, `:191-195`, `:226-230`, `:242-272` |
| `DIAGSCREEN=6` | The unmodified libnds `Simple_Tri` example draw logic (`videoSetMode -> glInit -> AA/clear/viewport/projection/lookAt -> counter-rotated coloured triangle -> glFlush`) compiled by our Makefile, flags, startup and link line. No console, no recorder. | `arm9v2/source/main.cpp:20-31`, `:196-211`, `:226-230`, `:242-272`; `arm9v2/source/demo_frame.cpp:19-27`, `:307-337` |

`DIAGSCREEN=5` deliberately prints nothing, so the bottom screen is left
untouched; the variant number can only be told from the ROM file name.
`DIAGSCREEN=6` copies the example's quirks verbatim, including calling
`glMatrixMode(GL_MODELVIEW)` after the projection-space rotates, and rotates
by a counter so a frozen picture is distinguishable from live animation.

### Hardware evidence and narrowed hypotheses (HARDWARE)

**Owner photos, 2026-11-16, New 3DS TWL mode via TWiLight Menu++**
(`HARDWARE UNVERIFIED` evidence — observed, not reproduced by this assistant):

- The unmodified libnds examples `Simple_Tri` and `Textured_Quad` **display**
  on the top screen.
- Our `arm9v2` default cube ROM and `DIAGSCREEN=1`, `DIAGSCREEN=2` (no
  recorder, raw libnds macros) and `DIAGSCREEN=4` (`lcdMainOnTop`) **all show
  a black top screen**.
- The bottom-screen console is alive and the recorder counts are correct.
- `ndstool` headers of our ROM and the example are equal in unit code / ARM7 /
  entry addresses; only the ARM9 size differs (ours `0x21DDC`, example
  `0x45B0`).

Because the examples display and our ROMs do not, the recorder, the display
setup order and `lcdMainOnTop()` are ruled out as the sole cause. The
remaining hypotheses are:

1. `consoleDemoInit()` + `iprintf` / console traffic on the sub screen.
2. Large static buffers / `libfat` link in our ROM.
3. Our build flags / Makefile / link line / ROM structure.
4. Our draw content itself (scene geometry, camera, polygon attributes).

`DIAGSCREEN=5` removes hypothesis 1 (console). If it still blacks, the console
is not the cause.

`DIAGSCREEN=6` replaces our draw content with the exact `Simple_Tri` logic
while keeping our build/link/ROM structure. If it displays, our scene content
is the problem; if it is black, our build/link/ROM structure is the problem.

---

## 6. Build commands and hashes

All builds were performed with devkitARM in this sandbox. The default ROM hash
is identical before and after the changes.

### Default ROM byte-identity proof

```
# before edits (clean tree)
make -C arm9v2 clean && make -C arm9v2
sha256sum arm9v2/twlhd-arm9v2.nds
# 5beefa30303b1a2b36654e09b41a77f3cdbf4892b20f135c2a7be7e2c10c47db

# after edits
make -C arm9v2 clean && make -C arm9v2
sha256sum arm9v2/twlhd-arm9v2.nds
# 5beefa30303b1a2b36654e09b41a77f3cdbf4892b20f135c2a7be7e2c10c47db
```

The `.nds` files are byte-identical, so no `.arm9` fallback comparison was
needed.

### `DIAGSCREEN` cube variants

```
make -C arm9v2 clean DIAGSCREEN=1 && make -C arm9v2 DIAGSCREEN=1
sha256sum arm9v2/twlhd-arm9v2-diag1.nds
# d60cd89f90b0c8e45cdd04725a3a23e8fb59532d0f2543b9db2f2441b9133940

make -C arm9v2 clean DIAGSCREEN=2 && make -C arm9v2 DIAGSCREEN=2
sha256sum arm9v2/twlhd-arm9v2-diag2.nds
# 1374fb48299941006473544c781e331df0cdf8d1ce1e7c137fa5b15f69a42c43

make -C arm9v2 clean DIAGSCREEN=3 && make -C arm9v2 DIAGSCREEN=3
sha256sum arm9v2/twlhd-arm9v2-diag3.nds
# 5cad59abd1d5ccb5a23a6c76442ed04089be530d9cc4c15adaf92d34306ea9e2

make -C arm9v2 clean DIAGSCREEN=4 && make -C arm9v2 DIAGSCREEN=4
sha256sum arm9v2/twlhd-arm9v2-diag4.nds
# 8dfc92469d31fa6fe0edb47f2b3a34ea6a8a0e938ef06e12c8f6e89abc01f670
```

Round 2 added only `DIAGSCREEN=5` and `DIAGSCREEN=6` branches inside `#if`
guards, so the default ROM and `DIAGSCREEN=1/2/3/4` cube ROMs are byte-identical
to their round-1 hashes (the `DIAGSCREEN=3` change happened in round 1).

### Round-2 `DIAGSCREEN=5` and `DIAGSCREEN=6` variants

```
make -C arm9v2 clean DIAGSCREEN=5 && make -C arm9v2 DIAGSCREEN=5
sha256sum arm9v2/twlhd-arm9v2-diag5.nds
# ffdd5bfbf4a616673bd1a8003cbc15f33bf7cc27c5f62ecb39f051546c6f5c97

make -C arm9v2 clean DIAGSCREEN=6 && make -C arm9v2 DIAGSCREEN=6
sha256sum arm9v2/twlhd-arm9v2-diag6.nds
# 374cf632b21111315a8ebbd1da8586e25ca108b5a1643d1ed074dafe81bc9501
```

### `SCENE=5` variants (the failing textured scene)

```
make -C arm9v2 clean SCENE=5 && make -C arm9v2 SCENE=5
sha256sum arm9v2/twlhd-arm9v2-s5.nds
# a0bbc09f824dd0f6aeedad848c974a5d55dff7b874f501c0d0f8726ffec8c890

make -C arm9v2 clean SCENE=5 DIAGSCREEN=1 && make -C arm9v2 SCENE=5 DIAGSCREEN=1
sha256sum arm9v2/twlhd-arm9v2-s5-diag1.nds
# 9e16ef5d7924cf5a964a0501883bae9be9c6a01fb211cd39a4bfb65c2fd9c5c1

make -C arm9v2 clean SCENE=5 DIAGSCREEN=2 && make -C arm9v2 SCENE=5 DIAGSCREEN=2
sha256sum arm9v2/twlhd-arm9v2-s5-diag2.nds
# 531e79da7297e4a47e333cc3d110eddeeb9b29a57e7caa60a483c599895f17da

make -C arm9v2 clean SCENE=5 DIAGSCREEN=3 && make -C arm9v2 SCENE=5 DIAGSCREEN=3
sha256sum arm9v2/twlhd-arm9v2-s5-diag3.nds
# e845562029162b6d6f77f827af103fac4c107d47b3019bd69217077aa3748134

make -C arm9v2 clean SCENE=5 DIAGSCREEN=4 && make -C arm9v2 SCENE=5 DIAGSCREEN=4
sha256sum arm9v2/twlhd-arm9v2-s5-diag4.nds
# df461b179fedfce7c31ba596ddb7072c28f2c10fb66fd5c411a2e1315ba8a0e1
```

### Suffix composition example

```
make -C arm9v2 -n SCENE=5 DIAGSCREEN=2 AUTODUMP_FRAME=240
# output contains: twlhd-arm9v2-autodump-s5-diag2.nds
```

---

## 7. Files changed

### Round 1

- `docs/reports/5c4-black-screen-analysis.md` — corrected hook/write order,
  fixed all line-number citations, updated `DIAGSCREEN=3` semantics and hashes.
- `docs/reports/5c4-libnds-video.h-snippet.md` — regenerated from real
  `/opt/devkitpro/libnds/include/nds/arm9/video.h` with true per-block line
  numbers.
- `docs/reports/5c4-libnds-system-videogl.h-snippet.md` — regenerated from real
  `/opt/devkitpro/libnds/include/nds/system.h` and
  `/opt/devkitpro/libnds/include/nds/arm9/videoGL.h` with true per-block line
  numbers (including the complete `glDisable` body).
- `arm9v2/source/main.cpp` — replaced the placebo `DIAGSCREEN=3` VRAM bank
  assignment with a post-upload `swiWaitForVBlank()` for `SCENE=5`.
- `tools/test_arm9v2_diagscreen.py` — added explicit `"0"` default case and
  `"01"` rejected value to pin the strict `^[1-4]$` check.

No files under `common/`, `arm9/`, `arm11/`, `arm11_ucode/`, `arm7/`,
`arm9v2/include/`, `arm9v2/source/demo_frame.cpp`, `arm9v2/Makefile`, or the v2
format (`common/twlhd_v2_format.h`, parser/writer) were changed in round 1.

### Round 2

- `arm9v2/Makefile` — extended `DIAGSCREEN` validation range from 1..4 to
  1..6, updated the error message, and added a raw-vs-stripped whitespace
  rejection check (`arm9v2/Makefile:160-174`).
- `arm9v2/source/main.cpp` — added `DIAGSCREEN=5` (cube demo without console,
  recorder or `fatInitDefault`) and `DIAGSCREEN=6` (`Simple_Tri` example init
  order, no console/recorder); extended recorder-bypass and include-order
  conditions to cover 5 and 6 (`arm9v2/source/main.cpp:20-31`, `:55-89`,
  `:91-161`, `:168-217`, `:226-230`, `:242-273`).
- `arm9v2/source/demo_frame.cpp` — added `DIAGSCREEN=6` draw branch with the
  verbatim `Simple_Tri` triangle logic and counter-driven rotation
  (`arm9v2/source/demo_frame.cpp:19-27`, `:29-34`, `:307-338`).
- `tools/test_arm9v2_diagscreen.py` — accepted `5` and `6`, added `7`,
  `"1 2"` and environment-based whitespace rejection cases, and updated the
  asserted error message to `1..6`.
- `docs/reports/5c4-black-screen-analysis.md` — added the HARDWARE evidence
  section, narrowed hypotheses, round-2 build commands/hashes, and this
  files-changed subsection.

No files under `common/`, `arm9/`, `arm11/`, `arm11_ucode/`, `arm7/`,
`arm9v2/include/`, `tools/emulator/`, or the v2 format
(`common/twlhd_v2_format.h`, parser/writer) were changed in round 2.
