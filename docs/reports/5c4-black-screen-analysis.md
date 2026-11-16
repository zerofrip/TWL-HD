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
| FIFO command port (`GFX_FIFO`, `0x04000400`) | hook records (width 4) or counts as ignored (other widths), then the shim writes hardware unconditionally | `twlhd_gx_shim.hpp:195-196`; `common/twlhd_gx_record.c:96-103` |
| `GFX_FLUSH` / SWAP_BUFFERS (`0x04000540`) | hook records it as command id `0x50`, marks the frame complete and disarms, then the shim writes hardware | `twlhd_gx_shim.hpp:255-256`; `common/twlhd_gx_record.c:117-134` |
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
(`common/twlhd_gx_record.c:65-67`, `arm9v2/source/main.cpp:172/189/194/200/215`).

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
| 1 | `videoSetMode` / BG0-3D bit | `videoSetMode(MODE_0_3D)` at `main.cpp:171/186/193/199/212` | Same: `videoSetMode(MODE_0_3D)` at `Simple_Tri:10`, `Textured_Quad:17` | No. Identical value (`MODE_0_3D = 0x10000 \| DISPLAY_BG0_ACTIVE \| ENABLE_3D`, `video.h:476`). | **FACT** |
| 2 | Which physical screen shows the main engine | Neither our demo nor the examples call `lcdMainOnTop()` / `lcdSwap()` | Same | Unknown. If the launch environment leaves the main engine mapped to the bottom screen, both would be affected unless something resets it. | **FACT** (no call either side); screen assignment hypothesis is **HYPOTHESIS** |
| 3 | `consoleDemoInit` usage | Default path: called **after** `videoSetMode` and **before** `glInit` (`main.cpp:213`, `glInit` at `:215`); `DIAGSCREEN=4`: `:188` before `glInit` at `:189`; `DIAGSCREEN=1`: `:183`, **after** `glInit` (`:172`) | **Not called** by either example | Unlikely by itself (`consoleDemoInit` targets the sub engine / VRAM C, `console.h:228` comment). It is a real difference and moves `glInit` later. | **FACT**; top-screen effect is **HYPOTHESIS** |
| 4 | Init-time 3D state order | Default: `videoSetMode -> consoleDemoInit -> glInit -> rec_init`; per-frame state is re-issued inside `demo_frame.cpp` | `videoSetMode -> glInit`, then `glEnable(GL_ANTIALIAS)`, clear, viewport, projection, lookAt **once at init** (`Simple_Tri:13-33`, `Textured_Quad:20-50`) | Possibly. We re-issue the same state per frame, but the *initial* state before the first frame differs, and `glInit` runs after `consoleDemoInit` instead of immediately after `videoSetMode`. | **FACT**; black-screen causality is **HYPOTHESIS** |
| 5 | `glEnable(GL_ANTIALIAS)` | Per frame at `demo_frame.cpp:346/392/437` | Once at init at `Simple_Tri:16`, `Textured_Quad:26` | No. Same control bit ends up set before draw. | **FACT** |
| 6 | Clear colour / polygon ID / depth | `glClearColor(0,0,0,31)`, `glClearPolyID(63)`, `glClearDepth(GL_MAX_DEPTH)` per frame (`demo_frame.cpp:347-349/393-395/438-440` etc.) | Same values (`Simple_Tri:19-21`, `Textured_Quad:29-31`); `GL_MAX_DEPTH == 0x7FFF` (`videoGL.h:87`) | No. Identical values. | **FACT** |
| 7 | Viewport | `glViewport(0,0,255,191)` per frame (`demo_frame.cpp:350-351/396-397/441-442`) | Same once at init (`Simple_Tri:24`, `Textured_Quad:34`) | No. Identical value. | **FACT** |
| 8 | Polygon format alpha / cull mode | `glPolyFmt(POLY_ALPHA(31))` per frame (`demo_frame.cpp:373/421/466`) | `POLY_ALPHA(31) \| POLY_CULL_NONE` (`Simple_Tri:50`) or `\| POLY_CULL_BACK` (`Textured_Quad:73`) | **Yes.** Cull mode 0 renders neither face; this blanks the whole screen. | **SUPERSEDED** — the "cull mode does not blank the whole screen" conclusion was wrong; see §Hardware results and conclusion. |
| 9 | Projection / camera matrix | `gluPerspective(70, 256/192, 0.1, 100.0)`, camera z=4 (`demo_frame.cpp:404/407-409`) | `gluPerspective(70, 256/192, 0.1, 40.0)`, camera z=1 (`Simple_Tri:27-33`) | No. Different view volume but object is in view in both; would produce a picture, not black. | **FACT** |
| 10 | `glFlush(0)` path | Proxied `GFX_FLUSH` write (`demo_frame.cpp:389/426/471`) | Raw `GFX_FLUSH` write (`Simple_Tri:78`, `Textured_Quad:106`) | No. The proxy forwards the `0x04000540` write identically. | **FACT** |
| 11 | Default cube: VRAM bank setup | None (`SCENE=0` path does not touch VRAM) | `Simple_Tri` does not touch VRAM | N/A for the default cube. | **FACT** |
| 12 | Textured scene: VRAM bank setup | `vramSetBankA(VRAM_A_LCD)`, `vramSetBankE(VRAM_E_LCD)`, copy, then `VRAM_A_TEXTURE`/`VRAM_E_TEX_PALETTE` at init (`demo_frame.cpp:178-187`) | `vramSetBankA(VRAM_A_TEXTURE)` once at init (`Textured_Quad:36`); no palette bank because it uses 16bpp direct colour | For `SCENE=5` only. The final mapping is equivalent for texture; palette bank is required by our I4 format. Could matter if the LCDC→texture switch races the first 3D frame. | **FACT**; black-screen causality for s5 is **HYPOTHESIS** |
| 13 | Textured scene: texture upload path | Manual 16-bit copy to `0x06800000`/`0x06880000` + explicit `GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` writes through proxy (`demo_frame.cpp:200-201`) | `glGenTextures`/`glBindTexture`/`glTexImage2D` out-of-line (`Textured_Quad:38-40`) | No. The same hardware registers end up written; proxy forwards ours. | **FACT** |
| 14 | Textured scene: material / lighting | Not issued | `glMaterialf`, `glMaterialShinyness` (`Textured_Quad:64-70`) | No. Missing material calls would change shading, not blank the screen. | **FACT** |
| 15 | Source include order / proxy bypass | `#include "twlhd_gx_shim.hpp"` **before** `<nds.h>` (`main.cpp:20-30`, `demo_frame.cpp:19-27`) | Only `<nds.h>`; no proxy | No. The proxy forwards every write; the hardware byte stream is identical for the same high-level calls. | **FACT**; a compiler/TU codegen side effect is **HYPOTHESIS** tested by `DIAGSCREEN=2` |
| 16 | libnds out-of-line writes we never issue | None beyond `glInit` for the cube; for s5 we manually issue `GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` | `glInit` for both; `glTexImage2D` and material calls for `Textured_Quad` | For the cube, none identified. For s5, our manual writes cover the registers the example sets through `glTexImage2D`. | **FACT** |

---

## 2.5 Draw-content differences (DIAGSCREEN=5 cube vs libnds examples)

The round-2 evidence already ruled out the recorder, display-setup order and
`lcdMainOnTop()` as the *sole* cause. Round 3 isolates the draw content of the
default cube scene (`arm9v2/source/demo_frame.cpp:390-426`) from the
`Simple_Tri` / `Textured_Quad` example draw bodies.

### Polygon attributes: the key factual difference

`glPolyFmt` is a direct `GFX_POLY_FORMAT = params;` register write
(`/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:775-776`; excerpt in
`docs/reports/5c4-libnds-system-videogl.h-snippet.md`, section "glPolyFmt — lines 770-776"). The real
libnds header defines the cull bits as
(`/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:187-189`):

```
POLY_CULL_FRONT = (1 << 6) = 0x00000040
POLY_CULL_BACK  = (2 << 6) = 0x00000080
POLY_CULL_NONE  = (3 << 6) = 0x000000C0
```

`POLY_ALPHA(31)` alone produces `0x001F0000`; bits 6-7 are **zero**. The
DeSmuME-derived bit table in `docs/DS_GX_RESEARCH.md:20-41` says bits 6-7 are
`SurfaceCullingMode`: `0 = Cull front and back`, `1 = Cull front`,
`2 = Cull back`, `3 = No culling`. Mode 0 renders **neither** face; the
visibility table (`docs/DS_GX_RESEARCH.md:62-65`) returns `false` for both
back-facing and front-facing rows when the mode is 0. This is a **FACT** from
source code, not a hardware measurement.

Our cube issues `glPolyFmt(POLY_ALPHA(31))` (`demo_frame.cpp:421`), which writes
mode 0. Both `Simple_Tri` (`5c4-libnds-simple_tri.cpp:50`) and
`Textured_Quad` (`5c4-libnds-textured_quad.cpp:73`) set bits 6-7 to a non-zero
mode (`POLY_CULL_NONE` and `POLY_CULL_BACK` respectively), and both display on
the owner's hardware. Therefore the earlier internal assumption "default cull is BACK" is **incorrect** for the register word produced by
`POLY_ALPHA(31)` alone: the default (zero) cull mode is **render neither**.

### What the host interpreter and the ARM11 PICA path do with cull bits

The host interpreter records `POLYGON_ATTR` but only consumes the alpha bits
16-20 and the light mask bits 0-3. There is no facing, shoelace, or cull logic
anywhere in `common/twlhd_gx_interp.c`:

- state fields: `pending_alpha` (`:79`), `pending_light_mask` (`:99`);
- command handler copies those two fields out of the raw word (`:864`);
- `BEGIN_VTXS` applies only alpha and light mask (`:1002-1005`).

The ARM11 PICA renderer hard-codes `C3D_CullFace(GPU_CULL_NONE)` every frame
(`docs/DS_GX_RESEARCH.md:182`).

So cull bits are the one piece of captured state that **neither** the host
interpreter **nor** the PICA path acts on. This is exactly why the same
captured cube renders correctly on host/PICA while the DS shows black.
**FACT** (source-verified), with the caveat that real-hardware POLYGON_ATTR
semantics beyond the DeSmuME bit table are still **UNVERIFIED**.

### Remaining draw-content differences

| # | Difference | Our cube | Example | Could blank? | Fact / Hypothesis |
|---|---|---|---|---|---|
| 17 | Cull bits in POLYGON_ATTR | `POLY_ALPHA(31)` → mode 0 (`demo_frame.cpp:421`) | `\| POLY_CULL_NONE` (`Simple_Tri:50`); `\| POLY_CULL_BACK` (`Textured_Quad:73`) | **Yes.** Mode 0 renders neither face. | **FACT** (register + DeSmuME bit table) |
| 18 | Projection far plane | `gluPerspective(70, 256/192, 0.1, 100.0)` (`demo_frame.cpp:404`) | far 40.0 (`Simple_Tri:29`, `Textured_Quad:46`) | No. The cube is inside both view volumes (computed below). | **FACT** |
| 19 | Camera z / lookAt | `gluLookAtf32(0,0,inttof32(4), ...)` (`demo_frame.cpp:409`) | `gluLookAt(0,0,1.0, ...)` (`Simple_Tri:31-33`, `Textured_Quad:48-50`) | No. Both cameras contain the object (computed below). | **FACT** |
| 20 | Scale | `glScalef32(1,1,1)` (`demo_frame.cpp:413`) | implicit 1 for the triangle; `Textured_Quad` also uses `glScalef32`? — no, it uses raw `floattov16(-0.5)` vertices | No. Object fits in view. | **FACT** |
| 21 | Primitive type | `GL_QUADS` (`demo_frame.cpp:77`) | `GL_TRIANGLE` (`Simple_Tri:63`); `GL_QUAD` (`Textured_Quad:87`) | No. DS supports both. | **FACT** |
| 22 | Vertex count / shape | 6 quad faces, 24 vertices (`demo_frame.cpp:38-91`) | 1 triangle or 1 quad | No. More geometry should produce *more* pixels, not none. | **FACT** |
| 23 | Per-vertex colour vs material | `glColor3b` per face (`demo_frame.cpp:81-82`) | `glColor3b` per vertex (`Simple_Tri:65-72`); material + texture (`Textured_Quad`) | No. Missing material would change shading, not blank the screen. | **FACT** |
| 24 | Matrix mode quirk | `glMatrixMode(GL_MODELVIEW)` before rotates (`demo_frame.cpp:407-411`) | `glMatrixMode(GL_MODELVIEW)` *after* rotates (`Simple_Tri:45`) | Possibly. `Simple_Tri` rotates in projection space then switches; our cube rotates in modelview space. A pure rotation commutes with perspective only at the origin; the effect on a centred cube is a different orientation, not disappearance. | **FACT**; causality **HYPOTHESIS** (tested by keeping default cube with cull off in `DIAGSCREEN=7`) |

### Cube face winding (shoelace)

The DS front-face rule is: counter-clockwise on screen = front. The cube faces
are emitted in `cube_faces[6][4]` (`demo_frame.cpp:49-56`). For each face we
project onto the plane perpendicular to its outward normal, orient the 2D axes
so that `(right, up)` is a right-handed basis with the normal pointing toward
the viewer, and compute the signed area with the shoelace formula
`Σ x_i (y_{i+1} - y_{i-1})`.

**Front face `{0,1,2,3}`** (normal +Z). Viewer looks from +Z; right = +X, up = +Y.
Screen coordinates:

```
0 (-1,-1,1) → (-1, -1)
1 ( 1,-1,1) → ( 1, -1)
2 ( 1, 1,1) → ( 1,  1)
3 (-1, 1,1) → (-1,  1)
```

Shoelace:
`(-1)(-1 -  1) + 1(1 - (-1)) + 1(1 - (-1)) + (-1)(-1 - 1)`
`= (-1)(-2) + 1(2) + 1(2) + (-1)(-2) = 2 + 2 + 2 + 2 = +8`.

Signed area +8 ⇒ **CCW** ⇒ front-facing. **FACT** (arithmetic).

**Back face `{5,4,7,6}`** (normal -Z). Viewer looks from -Z; right = -X, up = +Y.
Screen coordinates:

```
5 ( 1,-1,-1) → (-1, -1)
4 (-1,-1,-1) → ( 1, -1)
7 (-1, 1,-1) → ( 1,  1)
6 ( 1, 1,-1) → (-1,  1)
```

Shoelace:
`(-1)(-1 -  1) + 1(1 - (-1)) + 1(1 - (-1)) + (-1)(-1 - 1)`
`= 2 + 2 + 2 + 2 = +8`.

Signed area +8 ⇒ **CCW** ⇒ front-facing. **FACT** (arithmetic).

The other four faces check out the same way (left/right/top/bottom all give +8
with the appropriate right/up axes). Therefore the cube winding is **not**
reversed relative to the DS front-face convention assumed here. The anchor for
that convention is the displaying `Textured_Quad` quad order BL, BR, TR, TL
(`5c4-libnds-textured_quad.cpp:91-100`) with `POLY_CULL_BACK`, which proves
that exact order is hardware-front under the same convention.

**Caveat:** `docs/DS_GX_RESEARCH.md:67-70` marks the hardware front-face sign
as **UNVERIFIED** on real DS hardware; DeSmuME itself warns its winding sign
may be wrong. The arithmetic above assumes the convention is correct; if real
hardware uses the opposite sign, our front faces would be back-facing. This is
the top *remaining* hardware-unverified suspect and is tested by
`DIAGSCREEN=9`.

### View-volume / clip-space check

Default cube scene (`demo_frame.cpp:399-413`):

```
gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);
gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
glRotateXi(angle);
glRotateYi(angle);
glScalef32(1, 1, 1);
```

Let `f = 1 / tan(35°) ≈ 1.428148`, `aspect = 256/192 = 4/3`,
`f/aspect ≈ 1.071111`. For a vertex in eye space `(x_e, y_e, z_e)` with
`w_e = -z_e`, the NDC coordinates are:

```
x_ndc = (f/aspect) * x_e / (-z_e)
y_ndc = f * y_e / (-z_e)
```

**Angle 0.** No rotation. A corner `(x,y,z)` maps to eye space
`(x, y, z - 4)`. The nearest corners have `z_e = -3`, the farthest `z_e = -5`.
Worst-case `|x| = |y| = 1`:

```
|x_ndc|max = 1.071111 / 3 ≈ 0.357
|y_ndc|max = 1.428148 / 3 ≈ 0.476
```

All 8 corners are well inside `[-1, 1]`.

**Angle 4096 (= 45°).** OpenGL modelview order is `LookAt * RotX * RotY`, so
the vertex is first rotated by Y then by X. Applying `R_y(45°)` then
`R_x(45°)` to each unit corner and translating by `(0,0,-4)` gives:

| Corner | Eye x_e | Eye y_e | Eye z_e | x_ndc | y_ndc |
|---|---|---|---|---|---|
| (-1,-1,-1) | -1.414 | -0.707 | -4.707 | -0.322 | -0.215 |
| ( 1,-1,-1) |  0.000 |  0.293 | -5.707 |  0.000 |  0.073 |
| ( 1, 1,-1) |  0.000 |  1.707 | -4.293 |  0.000 |  0.568 |
| (-1, 1,-1) | -1.414 |  0.707 | -3.293 | -0.460 |  0.307 |
| (-1,-1, 1) | -0.000 | -1.707 | -3.707 | -0.000 | -0.658 |
| ( 1,-1, 1) |  1.414 | -0.707 | -4.707 |  0.322 | -0.215 |
| ( 1, 1, 1) |  1.414 |  0.707 | -3.293 |  0.460 |  0.307 |
| (-1, 1, 1) | -0.000 | -0.293 | -2.293 | -0.000 | -0.182 |

Maximum `|x_ndc| ≈ 0.460`, maximum `|y_ndc| ≈ 0.658`. All corners are inside
the view volume at both angles. **FACT** (arithmetic).

### View-volume check for the proposed `DIAGSCREEN=8` camera/projection

`DIAGSCREEN=8` keeps the default camera `z=4` and scale `1`, only changing the
projection far plane to `40.0` (`demo_frame.cpp:402`). Near/far clip for the
cube's eye-z range `[-5, -3]`:

```
z_ndc(z_e) = ((far+near)/(near-far) * z_e + 2*far*near/(near-far)) / (-z_e)
```

With `near=0.1`, `far=40`:

```
z_e = -3 → z_ndc = (-40.1/39.9 * -3 + -8/39.9) / 3 ≈  0.337
z_e = -5 → z_ndc = (-40.1/39.9 * -5 + -8/39.9) / 5 ≈  0.604
```

Both are inside `[-1, 1]`. The x/y NDC bounds are identical to the default
(because only the far plane changed), so the cube is provably inside the
`DIAGSCREEN=8` view volume. **FACT** (arithmetic).

### Ranked cause list after draw-content analysis

*(This is the pre-hardware ranking; it was settled by the owner's hardware run, see §3 and "Hardware results and conclusion": item 1 CONFIRMED.)*

1. **POLYGON_ATTR bits 6-7 = 0 → render neither face.** This is the only
difference that *directly* explains a completely black top screen while the
same captured frame renders fine on host/PICA. Tested by `DIAGSCREEN=7`
(`POLY_CULL_NONE`). **FACT-supported, HARDWARE UNVERIFIED.**

2. **Projection/camera numbers.** Arithmetic shows the default cube is inside
the view volume at angle 0 and at 45°, and `DIAGSCREEN=8` (far 40, same
camera) also contains it. This cause is expected to be exonerated, but the
hardware run of `DIAGSCREEN=8` confirms it. **FACT** (arithmetic).

3. **Hardware front-face sign vs the DeSmuME-derived convention.** The
shoelace arithmetic above says our faces are front under the assumed
convention, but `docs/DS_GX_RESEARCH.md:67-70` explicitly marks that sign as
**UNVERIFIED** on hardware. If the real DS uses the opposite sign, our faces
would be back-facing and `POLY_CULL_BACK` would cull them. Tested by
`DIAGSCREEN=9` (`POLY_CULL_BACK`). **HARDWARE UNVERIFIED.**

### What each hardware outcome would mean

Assuming the render-neither theory is correct:

- `DIAGSCREEN=7` displays ⇒ cull bits were the sole cause.
- `DIAGSCREEN=7` displays **and** `DIAGSCREEN=9` is black ⇒ our faces are
  hardware-back (front-face sign is opposite), because `POLY_CULL_BACK` would
  cull the faces that `POLY_CULL_NONE` renders.
- `DIAGSCREEN=7` black ⇒ cull bits are not the (only) cause; something else
  still blanks the picture.

`DIAGSCREEN=8` is expected to stay black under the render-neither theory (only
the far plane changed; cull mode is still mode 0). If `DIAGSCREEN=8`
unexpectedly displays, the default far plane / depth-clipping interaction was
the cause.

---

## 3. Ranked cause list (round-3 narrowing)

The round-2 hardware evidence already ruled out the recorder, display-setup
order and `lcdMainOnTop()`. The draw-content analysis in §2.5 further narrows
the list:

1. **POLYGON_ATTR bits 6-7 = 0 → render neither face.** Our cube issues
   `glPolyFmt(POLY_ALPHA(31))`, which writes cull mode 0. DeSmuME's bit table
   and the displaying `Simple_Tri` / `Textured_Quad` examples both say mode 0
   renders nothing, while modes 2/3 (`POLY_CULL_BACK` / `POLY_CULL_NONE`)
   render something. This is the only difference that directly explains a
   fully black top screen while host/PICA render the same capture correctly.
   `DIAGSCREEN=7` displays on hardware. **CONFIRMED by hardware.**

2. **Projection / camera numbers.** The clip-space arithmetic in §2.5 proves
   the default cube is inside the view volume at angle 0 and at 45°, and
   `DIAGSCREEN=8` (far 40, same camera) is also contained. Expected to be
   exonerated; the hardware run confirms it. **FACT** (arithmetic +
   `DIAGSCREEN=8` black).

3. **Hardware front-face sign.** The shoelace arithmetic says our faces are
   front under the DeSmuME-derived convention. `DIAGSCREEN=9` displays with
   `POLY_CULL_BACK`, confirming the cube faces are hardware-front and the
   convention is correct. **SETTLED by hardware.**

4. **ARM11 / PICA-side or firmware environment** — if the owner runs our ROMs
   under a patched TWL-HD firmware/arm11 setup that is not used for the stock
   examples, the difference may be outside the ARM9 ROM entirely. No ARM9/ARM11
   code change can test this; it needs owner clarification. **HARDWARE
   UNVERIFIED**, documented as an owner question below.

Historical hypotheses now considered ruled out or subsumed:
- Missing/different display-init sequence (ruled out by round-2 evidence).
- `lcdMainOnTop()` / screen assignment (ruled out by round-2 evidence).
- Recorder / C++ proxy side effect (ruled out by `DIAGSCREEN=2` round-2 result).
- `consoleDemoInit()` traffic (removed in `DIAGSCREEN=5`; subsumed into draw
  content once `DIAGSCREEN=6` displayed).

---

## 4. Owner questions

1. Is the New 3DS running any TWL-HD-specific patch, custom firmware hook, or
   arm11 payload when our ROMs are launched, but **not** when the stock libnds
   examples are launched?
2. What are the exact TWiLight Menu++ and nds-bootstrap-hb versions/settings?
   In particular, is there a "force DS mode / DSi mode / screen swap" option
   that differs between our `.nds` and the examples?
3. (Round 2 answered) Does `DIAGSCREEN=1` (example display setup order) or
   `DIAGSCREEN=4` (`lcdMainOnTop`) produce a picture? — both were black.
4. **(Answered)** `DIAGSCREEN=7` displays, `DIAGSCREEN=8` is black,
   `DIAGSCREEN=9` displays. See the outcome decoding table in §2.5 and the
   "Hardware results and conclusion" section below (§5).
5. **(Answered, hardware)** `DIAGSCREEN=2` (recorder bypass) is black on the
   cube, as are 1, 4 and 5; 6, 7 and 9 display (see "Hardware results and
   conclusion"). `DIAGSCREEN=3` (SCENE=5 VRAM bank switch) has not been run
   on hardware; it is moot for the black screen, whose cause is the cull bits.
6. **(Answered)** Not all variants were black: 6, 7 and 9 displayed, so the
   cause is inside the ARM9 ROM's draw content (cull bits 6-7 = 0), not in the
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
| `DIAGSCREEN=1` | Replicates the libnds example display-setup order: `videoSetMode -> glInit -> glEnable(GL_ANTIALIAS) / clear / viewport / projection / lookAt -> consoleDemoInit -> recorder init`. | `arm9v2/source/main.cpp:167-183` |
| `DIAGSCREEN=2` | **Recorder proxy bypass:** includes `<nds.h>` before the shim so libnds inline helpers compile against raw `GFX_*` macros; never arms the recorder; console prints "recorder disabled". | `arm9v2/source/main.cpp:20-30`, `:225-229`; `arm9v2/source/demo_frame.cpp:19-27` |
| `DIAGSCREEN=3` | Settle VRAM mapping/timing before the first 3D frame: for `SCENE=5` it waits one vblank after texture upload so the LCDC→texture/palette switch is committed before the first draw; for the default cube it is a no-op beyond the banner. | `arm9v2/source/main.cpp:218-223`, `:234-236` |
| `DIAGSCREEN=4` | Forces the main engine onto the top physical screen with `lcdMainOnTop()`. | `arm9v2/source/main.cpp:184-189` |
| `DIAGSCREEN=5` | Default cube demo with `consoleDemoInit()`, every `iprintf` / `consoleClear` call and the recorder removed; no `fatInitDefault`. The variant number is not printed (the ROM file name carries it). | `arm9v2/source/main.cpp:20-30`, `:190-194`, `:225-229`, `:247-269` |
| `DIAGSCREEN=6` | The unmodified libnds `Simple_Tri` example draw logic (`videoSetMode -> glInit -> AA/clear/viewport/projection/lookAt -> counter-rotated coloured triangle -> glFlush`) compiled by our Makefile, flags, startup and link line. No console, no recorder. | `arm9v2/source/main.cpp:20-30`, `:195-210`, `:225-229`, `:247-269`; `arm9v2/source/demo_frame.cpp:19-27`, `:313-343` |
| `DIAGSCREEN=7` | Same no-console/no-recorder base as `DIAGSCREEN=5`, but the single cube is drawn with `glPolyFmt(POLY_ALPHA(31) \| POLY_CULL_NONE)` instead of `POLY_ALPHA(31)` alone. | `arm9v2/source/main.cpp:20-30`, `:190-194`, `:225-229`, `:247-269`; `arm9v2/source/demo_frame.cpp:416-417` |
| `DIAGSCREEN=8` | Same base, but the projection far plane is `40.0` (matching `Simple_Tri`/`Textured_Quad`) instead of `100.0`; camera z=4 and scale=1 are unchanged. | `arm9v2/source/main.cpp:20-30`, `:190-194`, `:225-229`, `:247-269`; `arm9v2/source/demo_frame.cpp:401-402` |
| `DIAGSCREEN=9` | Same base, but the single cube is drawn with `glPolyFmt(POLY_ALPHA(31) \| POLY_CULL_BACK)`. This tests whether our faces are hardware-back under the real DS front-face sign. | `arm9v2/source/main.cpp:20-30`, `:190-194`, `:225-229`, `:247-269`; `arm9v2/source/demo_frame.cpp:418-419` |

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

`DIAGSCREEN=7/8/9` keep the same no-console/no-recorder base as
`DIAGSCREEN=5` and change exactly one draw parameter each (culling, far plane,
culling direction), per §2.5.

## Hardware results and conclusion

**HARDWARE (New 3DS TWL mode via TWiLight Menu++ / nds-bootstrap-hb), owner
run, 2026-11-16:**

- `DIAGSCREEN=1` (example display-setup order): **black** top screen.
- `DIAGSCREEN=2` (recorder bypass, raw libnds macros): **black** top screen.
- `DIAGSCREEN=4` (`lcdMainOnTop()`): **black** top screen.
- `DIAGSCREEN=5` (default cube, no console/recorder): **black** top screen.
- `DIAGSCREEN=6` (verbatim `Simple_Tri` draw code): **displayed** on top screen.
- `DIAGSCREEN=7` (cube + `POLY_CULL_NONE`): **displayed** on top screen.
- `DIAGSCREEN=8` (far plane 40.0, same camera, cull bits left 0): **black** top
  screen.
- `DIAGSCREEN=9` (cube + `POLY_CULL_BACK`): **displayed** on top screen.

**Confirmed cause:** `glPolyFmt(POLY_ALPHA(31))` leaves `POLYGON_ATTR` bits 6-7
(cull mode) at 0. On DS hardware cull mode 0 renders **neither** face, so the
top screen is completely black. The cube winding is correct: `DIAGSCREEN=9`
displays with `POLY_CULL_BACK`, which means the cube faces are hardware-front
under the real DS front-face convention. `DIAGSCREEN=8` staying black rules out
the far plane / depth-clipping interaction as the cause.

**Settled hypotheses (was hypothesis, now fact):**

- §2 row 8 "cull mode does not blank the whole screen" is **falsified** and
  marked superseded above (§2 row 8).
- §2.5 "POLYGON_ATTR bits 6-7 = 0 → render neither face" is **confirmed**.
- §3 item 1 (cull bits are the cause) is **confirmed**.
- §3 item 2 (projection/camera numbers) is **settled**: `DIAGSCREEN=8` is black,
  so the default far plane / depth-clipping interaction is not the cause.
- §3 item 3 (hardware front-face sign) is **settled**: `DIAGSCREEN=9` displays,
  so the cube faces are hardware-front and the DeSmuME-derived front-face
  convention used in §2.5 is correct.
- §4 question 4 is **answered**: `DIAGSCREEN=7` displays, `DIAGSCREEN=8` is
  black, `DIAGSCREEN=9` displays.

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

# after round-3 edits
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

### Round-3 `DIAGSCREEN=7`, `8` and `9` variants

```
make -C arm9v2 clean DIAGSCREEN=7 && make -C arm9v2 DIAGSCREEN=7
sha256sum arm9v2/twlhd-arm9v2-diag7.nds
# a0ddb304db0bb3e0834aa62208f308c13619a242286e6638f72e3a1cd01b24a2

make -C arm9v2 clean DIAGSCREEN=8 && make -C arm9v2 DIAGSCREEN=8
sha256sum arm9v2/twlhd-arm9v2-diag8.nds
# c6fdfb286619cadbc3d866e6ce4b2f3bf6793a0f4264ba8efbd8d0fe98990610

make -C arm9v2 clean DIAGSCREEN=9 && make -C arm9v2 DIAGSCREEN=9
sha256sum arm9v2/twlhd-arm9v2-diag9.nds
# 41e6ba8ad9b66adeba0da8f3a758583d3033ec38f47306a1ce60e1e20f030bc5
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

### Round-4 `SCENE=5` fix (`POLY_CULL_NONE`)

Round 4 changes only the shared `glPolyFmt` call in the `SCENE=5` build path
(`arm9v2/source/demo_frame.cpp`), so the default ROM and the cube
`DIAGSCREEN=1..9` hashes above are byte-identical to round 3. The `SCENE=5`
ROMs and their `DIAGSCREEN=1..4` variants change because the recorded
`POLYGON_ATTR` word is now `0x001F00C0` (`POLY_ALPHA(31) | POLY_CULL_NONE`)
instead of `0x001F0000`.

```
make -C arm9v2 clean && make -C arm9v2
sha256sum arm9v2/twlhd-arm9v2.nds
# 5beefa30303b1a2b36654e09b41a77f3cdbf4892b20f135c2a7be7e2c10c47db

make -C arm9v2 clean SCENE=5 && make -C arm9v2 SCENE=5
sha256sum arm9v2/twlhd-arm9v2-s5.nds
# 1bde37007ab36c05b0457874dce56e633670aedf2c66e840b0c5e219fe8afd01

make -C arm9v2 clean SCENE=5 AUTODUMP_FRAME=240 && make -C arm9v2 SCENE=5 AUTODUMP_FRAME=240
sha256sum arm9v2/twlhd-arm9v2-autodump-s5.nds
# 35a50aadc30dbf26d7fa389192a908024cffaa515614c378e389e3efd45d82bd

make -C arm9v2 clean SCENE=5 DIAGSCREEN=1 && make -C arm9v2 SCENE=5 DIAGSCREEN=1
sha256sum arm9v2/twlhd-arm9v2-s5-diag1.nds
# 1259131b79ba54795912494185b1c487965627b4ccecb575f571d83796389ff9

make -C arm9v2 clean SCENE=5 DIAGSCREEN=2 && make -C arm9v2 SCENE=5 DIAGSCREEN=2
sha256sum arm9v2/twlhd-arm9v2-s5-diag2.nds
# 8942a85e0c8ecfcc9293bfe8d4d64e7f5c6a56fe47b19a08a3d8600e595c46bd

make -C arm9v2 clean SCENE=5 DIAGSCREEN=3 && make -C arm9v2 SCENE=5 DIAGSCREEN=3
sha256sum arm9v2/twlhd-arm9v2-s5-diag3.nds
# 4811fa37523c1bd434b87a50c1abd131cb958f78a136c77cc38377e365c738a7

make -C arm9v2 clean SCENE=5 DIAGSCREEN=4 && make -C arm9v2 SCENE=5 DIAGSCREEN=4
sha256sum arm9v2/twlhd-arm9v2-s5-diag4.nds
# 1753396032373a60828e5b6906acfdaab36769e8186398660c1dd81da5ee8b02
```

**Owner decision:** the default cube and `SCENE=1..4` still issue
`glPolyFmt(POLY_ALPHA(31))` alone and therefore still render nothing on a real
DS. Fixing them would change the recorded `POLYGON_ATTR` word and the
golden/expected literals, so that work is intentionally NOT done in this round.
BUILD-only follow-up (2026-11-16): all `arm9v2` demo draw paths now carry
`POLY_CULL_NONE`; see `docs/EMULATOR_VALIDATION.md` for the impact table.

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
  conditions to cover 5 and 6 (`arm9v2/source/main.cpp:20-30`, `:54-88`,
  `:90-160`, `:167-216`, `:225-229`, `:241-269`).
- `arm9v2/source/demo_frame.cpp` — added `DIAGSCREEN=6` draw branch with the
  verbatim `Simple_Tri` triangle logic and counter-driven rotation
  (`arm9v2/source/demo_frame.cpp:19-27`, `:28-33`, `:313-343`).
- `tools/test_arm9v2_diagscreen.py` — accepted `5` and `6`, added `7`,
  `"1 2"` and environment-based whitespace rejection cases, and updated the
  asserted error message to `1..6`.
- `docs/reports/5c4-black-screen-analysis.md` — added the HARDWARE evidence
  section, narrowed hypotheses, round-2 build commands/hashes, and this
  files-changed subsection.

No files under `common/`, `arm9/`, `arm11/`, `arm11_ucode/`, `arm7/`,
`arm9v2/include/`, `tools/emulator/`, or the v2 format
(`common/twlhd_v2_format.h`, parser/writer) were changed in round 2.

### Round 3

- `arm9v2/Makefile` — extended `DIAGSCREEN` validation range from 1..6 to
  1..9 and updated both error messages (`arm9v2/Makefile:155`, `:167`, `:171`,
  `:173`).
- `arm9v2/source/main.cpp` — shared the no-console/no-recorder `DIAGSCREEN=5`
  init block with 7/8/9; extended recorder-bypass, include-order and loop
  key/console guards to cover 7/8/9
  (`arm9v2/source/main.cpp:20-30`, `:54-88`, `:90-160`, `:190-194`,
  `:225-229`, `:241-269`).
- `arm9v2/source/demo_frame.cpp` — extended recorder-bypass include-order
  condition to cover 7/8/9; added an `#error` guard rejecting `DIAGSCREEN >= 7`
  combined with `SCENE != 0` or `CUBES > 1`; added the three one-change draw
  edits in the single-cube path
  (`arm9v2/source/demo_frame.cpp:19-27`, `:134-138`, `:401-422`).
- `tools/test_arm9v2_diagscreen.py` — accepted `7`, `8`, `9`, moved `"7"` and
  `"9"` out of the bad list and added `"10"`, and updated the asserted error
  message to `1..9`.
- `docs/reports/5c4-black-screen-analysis.md` — added the "Draw-content
  differences" section (§2.5) with computed winding, view-volume arithmetic,
  interpreter/PICA cull-bit evidence, real libnds `POLY_CULL_*` values, and a
  re-ranked cause list; added 7/8/9 semantics to §5, round-3 build
  commands/hashes to §6, and this files-changed subsection.
- `docs/EMULATOR_VALIDATION.md:111` — updated stale `DIAGSCREEN=1..4` status
  line to `1..9`.
- `docs/PHASE5B_PLAN.md:999` — updated stale `DIAGSCREEN=1..4` status line to
  `1..9`.

No files under `common/`, `arm9/`, `arm11/`, `arm11_ucode/`, `arm7/`,
`arm9v2/include/`, `tests/host/`, `tools/emulator/`, root `Makefile`, or the v2
format (`common/twlhd_v2_format.h`, parser/writer) were changed in round 3.

### Round 4

- `arm9v2/source/demo_frame.cpp` — guarded the shared `glPolyFmt(POLY_ALPHA(31))`
  call so only `TWLHD_DEMO_SCENE == 5` issues
  `glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE)`
  (`arm9v2/source/demo_frame.cpp:370-374`). Scenes 1-4, the default cube and the
  multi-cube path are textually unchanged.
- `tools/emulator/compare_texture_capture.py` — added `CMD_POLYGON_ATTR = 0x29`
  and the hand-computed `POLYGON_ATTR = 0x001F00C0` literal; `_check_shape` now
  requires exactly one `POLYGON_ATTR` entry with that value before
  `BEGIN_VTXS`.
- `tools/test_emulator_texture_compare.py` — updated the `scene5_entries()`
  fixture to `0x001F00C0`; added
  `test_polygon_attr_cull_bits_zero_fails` (regression test for cull mode 0);
  extended `DemoSourceCrossCheckTest` with a parser for the scene-5
  `glPolyFmt` literal.
- `tests/host/test_demo_frame.cpp` — added a `TWLHD_TEST_EXPECT_SCENE == 5`
  guarded assertion that the recorded `POLYGON_ATTR` value is `0x001F00C0`.
- `docs/reports/5c4-black-screen-analysis.md` — added the "Hardware results and
  conclusion" section with the owner's observations, marked superseded
  hypotheses, settled §3 items, answered §4 question 4, and added round-4 build
  commands/hashes.
- `docs/TEXTURE_CAPTURE_DESIGN.md` — updated the 5C-4 status (capture content
  hardware-verified earlier; on-screen texture check pending because of cull
  bits; `SCENE=5` now sets `CULL_NONE`; BUILD only).
- `docs/PHASE5B_PLAN.md` — updated the 5C-4 status line.
- `docs/EMULATOR_VALIDATION.md` — added the polygon-cull-bits note for demo
  ROMs and listed the new `POLYGON_ATTR` check in the textured-scene
  comparator checklist.

`docs/reports/5c3-mutation-report.md` counts (187 mutants, 171 killed, 16
survivors) are now slightly stale for the new `POLYGON_ATTR` check; they were
not regenerated in this round.

No files under `common/`, `arm9/`, `arm11/`, `arm11_ucode/`, `arm7/`,
`arm9v2/include/`, `arm9v2/source/main.cpp`, `arm9v2/Makefile`, root `Makefile`,
`tools/emulator/compare_capture.py`, `tools/test_emulator_compare.py`, or the v2
format (`common/twlhd_v2_format.h`, parser/writer) were changed in round 4.
