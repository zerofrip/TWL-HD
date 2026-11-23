# Equality check design: does the DS-GX-to-PICA pipeline draw what a DS draws?

**Status: DESIGN STUDY ONLY.** No source, test, tool or Makefile change accompanies this
file. Nothing here was produced by running an emulator, the console or the DeSmuME CLI.
Every DeSmuME statement below was read in the checkout at `$HOME/3ds/desmume`,
revision `fc504ee0` (verified with `git -C $HOME/3ds/desmume rev-parse --short=8 HEAD`
→ `fc504ee0`). Line numbers are from that checkout; where a repository document quotes an
older line number, this file re-checks the line and says so.

Labels follow `docs/DS_GX_RESEARCH.md:10-12` and the wording of the task:
**VERIFIED** = read at the cited line in this session (repository file or DeSmuME source);
**UNVERIFIED** = not checked against hardware or a second source;
**HYPOTHESIS** = a proposal that no evidence yet supports.
Nothing in this document is HARDWARE VERIFIED. It proposes checks; it does not report an
equality result.

## 0. Executive summary

- **Today the pipeline is proven equal to the host interpreter by construction, and the
  texture path is proven against DeSmuME source, Azahar and real PICA hardware — but no one
  has compared the picture a DS draws with the picture our pipeline draws for the same GX
  stream.** The 2026-11-16 hardware result (Run 23) shows our demo ROMs *display* on a real
  New 3DS in TWL mode and the dumped v2 capture is bit-exact (`docs/TEXTURE_CAPTURE_DESIGN.md:1116-1145`),
  but the on-screen texels were "qualitative; texels were not compared pixel by pixel"
  (`docs/TEXTURE_CAPTURE_DESIGN.md:1137`).
- **The cheapest honest first step is not pixels.** A geometry-level numeric comparison of
  our vertices against DeSmuME's post-geometry vertex list (an out-of-tree GPL harness
  reading `viewer3D.gList`, `gfx3d.h:766` / `gfx3d.cpp:3517-3525`) catches matrix, stack and
  primitive-assembly defects with a tolerance derived from the 4.12 fixed-point quantum
  (`docs/EMULATOR_VALIDATION.md:33-34`). It needs no rasteriser and no console. **Caveat
  that shapes EQ-2:** DeSmuME stores that list *after* the viewport transform — x/y in
  16.16 screen pixels, z in 0.31, w in 20.12 (`gfx3d.cpp:3321-3324`) — while our
  `TwlhdGxVertex.clip` is pre-divide **clip space** (`common/twlhd_gx_interp.h:24`). The
  comparison is therefore made in DS screen space: our clip is passed through the same
  viewport formula DeSmuME uses (`gfx3d.cpp:3287-3311`), or a pre-viewport hook is added
  (both in section 5.2).
- **A same-stream software-rasteriser picture comparison is feasible** only through new
  out-of-tree GPL code: the CLI has no screenshot, frame-dump, AVI or Lua facility
  (`desmume/src/commandline.cpp:49-140`, help text; getopt table `:260-340`; draw path
  `desmume/src/frontend/posix/cli/main.cpp:281-297`).
  The core does expose the rendered framebuffer (`GPU.h:1179`, `GPU.h:1889`), so a small
  harness can write PPM frames.
- **The strongest *possible* reference is a DS-side framebuffer dump from our own ROM**
  (option (f), HYPOTHESIS) — but this study found a model-level obstacle. In DeSmuME only an
  LCDC/ABG/OBJ-mapped bank is in the CPU page map (`MMU.cpp:443-458`, `:494-502`,
  `:504-511`); a **texture**-mapped bank reaches only `textureSlotAddr` (`MMU.cpp:555-558`),
  and the repository already records that model as "not CPU-readable"
  (`docs/TEXTURE_CAPTURE_DESIGN.md:1153`). Which mapping the 3D framebuffer uses is
  UNVERIFIED. So EQ-6 starts with a host-only mapping check, not a hardware run.
- **Pixel equality is a goal only for the "must be equal" rows of section 2.3.** Fidelity to
  the DS rasteriser is an explicit non-goal (`docs/PHASE5A_V2_RENDERER_DESIGN.md:41-42`), and
  fog, edge marks, anti-aliasing, toon/highlight, polygon-ID effects, display capture and DS
  translucent-polygon depth/order semantics are out of scope (`docs/PHASE5B_PLAN.md:28-33`,
  `:745`).
- **Six staged checks are proposed (EQ-1…EQ-6)**: EQ-1 is this document, EQ-2 and EQ-3 are
  host-only, EQ-6 begins host-only and only then may need one owner-approved run, and EQ-5
  needs one owner-approved run. The EQ-4 trial ran on 2026-11-19 and found NTR-HR remote play
  not reachable in TWL/DS mode (`docs/reports/eq4-ds-mode-capture.md`). Section 5 derives
  every tolerance from a named difference; hardware tolerances stay "to be derived from the
  first capture" on purpose.

## 1. The question and the current evidence baseline

The question is: for the same GX command stream, is the picture our pipeline produces equal
to the picture a real DS produces? "Our pipeline" is v2 capture → `common/twlhd_gx_interp.c`
→ `TwlhdGxVertex` → `clipfix` → the ARM11 PICA200 renderer (`arm11/source/renderer.c`).
"Equal" has to be defined per stage, because the pipeline deliberately does not implement
the whole DS rasteriser.

What is already established, and what it does not cover:

| Evidence | What it proves | What it does not prove | Source |
|---|---|---|---|
| Host/PICA output equals the host interpreter "by construction" | the interpreter's vertex/colour output is what the renderer consumes | that either matches a DS | task statement |
| Texture decode/orientation/formats vs DeSmuME source | the decoder follows the emulator model | the DS silicon | `docs/DS_GX_RESEARCH.md:260-330` |
| Azahar emulator observation | one emulator's PICA model agrees | the DS or the real PICA200 | `docs/EMULATOR_VALIDATION.md:296-306`, `:494-497` |
| Real PICA200 (New 3DS) runs, Runs 7-13, 18-21 | the renderer draws, does not hang, and the texture path matches the oracle | that the DS draws the same picture | `docs/PHASE5A_V2_RENDERER_DESIGN.md:109`, `docs/PHASE5B_PLAN.md:98-100` |
| Run 23: demo ROM displays in TWL mode; v2 dump bit-exact | the DS software path runs and captures correctly | the DS *picture* was not compared pixel by pixel | `docs/TEXTURE_CAPTURE_DESIGN.md:1116-1145` |
| Run 17: console alpha blend within ~2 levels of a standard blend | the console blends vertex alpha | the DS `(alpha+1)/32` scale, depth/order rules | `docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787` |
| Run 20/21: seven DS texture formats and wrap modes on hardware | texture path is DS-correct for those cases | other formats, filtering, mipmaps, hardware rasterisation | `docs/PHASE5B_PLAN.md:844-852`, `docs/EMULATOR_VALIDATION.md:776-788` |

**Fact:** the evidence is a chain of *component* checks. **HYPOTHESIS (not yet tested):** the
components compose into a picture equal to the DS's. That composition is exactly what an
equality check has to measure.

## 2. Q1 — What could differ, by stage, and what we deliberately do not implement

### 2.1 The stage list with evidence

**Geometry and clipping**

- **Matrix/stack/assembly semantics.** VERIFIED in DeSmuME: `BEGIN_VTXS` mode is
  `param & 3` (`gfx3d.cpp:1416`), quads become one
  `POLYGON_TYPE_QUAD` with four indices (`gfx3d.cpp:1661-1676`), triangle strips alternate
  the index order (`gfx3d.cpp:1682-1715`), quad strips emit `(0,1,3,2)`
  (`gfx3d.cpp:1726-1735`); see `docs/DS_GX_RESEARCH.md:88-95`. Our interpreter emits
  `(0,1,2)+(0,2,3)` for quad lists (`common/twlhd_gx_interp.c:636-655`) and
  `(0,1,3)+(0,3,2)` for quad strips (`common/twlhd_gx_interp.c:684-703`). VERIFIED.
- **Clipping.** VERIFIED: our renderer does not model DS clipping; the PICA200 clips
  (`docs/PHASE5A_V2_RENDERER_DESIGN.md:40`). DeSmuME clips and keeps the quad type
  (`gfx3d.cpp:3260-3270`).
- **Viewport transform.** VERIFIED: DeSmuME applies `x += viewport.x`, then a y flip
  `vtx64.y = (192LL << 16) - vtx64.y` (`gfx3d.cpp:3305-3311`); our mapping is in
  `common/twlhd_clipfix.h:11-19` and was HARDWARE OBSERVED for one non-default viewport
  (Run 8, `docs/PHASE5A_V2_RENDERER_DESIGN.md:67`). The DS-side `+1` size / y1-wrap rule
  remains UNVERIFIED and cannot be exercised on the 3DS
  (`docs/PHASE5A_V2_RENDERER_DESIGN.md:105`).

**Rasterisation**

- **Quad fill.** VERIFIED: DeSmuME fills a quad as one 4-gon, `case 4:
  this->_sort_verts<ISFRONTFACING, 4>()` in a routine commented "can handle any convex N-gon
  up to octagons" (`rasterize.cpp:1190-1202`). We split into two triangles. For a planar,
  convex quad the two agree; for a non-planar quad the diagonal interpolation can differ
  (UNVERIFIED hardware rule, `docs/DS_GX_RESEARCH.md:153-156`).
- **Perspective-correct interpolation.** VERIFIED: DeSmuME premultiplies by `1/w` at precalc
  (`rasterize.cpp:2139`) and divides by the interpolated `1/w` per pixel
  (`rasterize.cpp:987-989`); that is perspective-correct for colour, texture coordinates and
  depth. Our pipeline delegates interpolation to the PICA200, which is perspective-correct in
  hardware; whether its scanline/edge rounding matches the DS is UNVERIFIED.
- **Edge/scanline rounding and the ~1 px shift.** HARDWARE OBSERVED: the Run 19 console
  picture is shifted about 1 px up/left relative to the emulator, cause not identified
  (`docs/PHASE5B_PLAN.md:896-897`).
- **Depth buffer.** VERIFIED: the DS can test a w-buffer or a z-buffer, selected by
  `SWAP_BUFFERS.DepthMode` (`rasterize.cpp:948-968`, w path at `:951`). Our renderer always
  uses a PICA z-buffer with `C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL)`
  (`arm11/source/renderer.c:1536`). The depth-mode register the demo writes is not checked
  here — UNVERIFIED.

**Texture sampling**

- **Decode, orientation, all seven formats.** HARDWARE OBSERVED: Run 20 passes the format
  frame and Run 21 passes every cell of every quad (`docs/PHASE5B_PLAN.md:844-852`,
  `docs/EMULATOR_VALIDATION.md:776-788`, `docs/TEXTURE_CAPTURE_DESIGN.md:1116-1145`).
  DeSmuME's decoder is `texcache.cpp:412-499` (width/height `:424-425`, format-size table
  `:415` applied at `:429`, key-colour `:431-438`, palette address `:440`). VERIFIED.
- **Wrap/mirror.** HARDWARE OBSERVED: REPEAT, CLAMP, MIRRORED_REPEAT and flip-only all pass
  (`docs/EMULATOR_VALIDATION.md:776-783`). The renderer passes the captured wrap code to
  `C3D_TexSetWrap` (`arm11/source/renderer.c:1454-1455`). VERIFIED.
- **Filtering.** Our renderer sets `C3D_TexSetFilter(tex, e->filter, e->filter)`
  (`arm11/source/renderer.c:1456`); where `e->filter` comes from and whether the DS has an
  equivalent is UNVERIFIED in this study.

**Blending, polygon attributes and anti-aliasing**

- **Alpha weight.** VERIFIED in DeSmuME: `alpha = inSrc.a + 1`, `invAlpha = 32 - alpha`,
  `>> 5` (`rasterize.cpp:490-494`); alpha is constant per polygon
  (`rasterize.cpp:831`, `:846`). Our interpreter emits `(alpha+1)/32` with alpha 0 → 0
  (`common/twlhd_gx_interp.c:387-397`); our renderer uses standard
  `GPU_SRC_ALPHA` / `GPU_ONE_MINUS_SRC_ALPHA` (`tests/host/test_blend_config.c:77-80`).
  The hardware scale is UNVERIFIED (`docs/DS_GX_RESEARCH.md:166-170`).
- **Translucent depth write and draw order.** NOT implemented: `docs/PHASE5B_PLAN.md:745`.
  DeSmuME writes depth for a translucent pixel only when `TranslucentDepthWrite_Enable` is
  set (`rasterize.cpp:780`); the draw-order rule is stated in the plan, not read here.
- **Edge marks and anti-aliasing.** NOT implemented; DeSmuME's edge-marking pass is
  `rasterize.cpp:2388-2391` and its edge blend is `rasterize.cpp:499`. Out of scope:
  `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`.
- **Polygon ID effects.** NOT implemented; DeSmuME keeps opaque/translucent polygon IDs per
  pixel (`rasterize.cpp:656-657`, `:727`, `:753-769`). Out of scope:
  `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`.

**Shading**

- **Lighting.** HARDWARE OBSERVED: three lit frames match the host interpreter within
  1 level (Run 18, `docs/PHASE5B_PLAN.md:98-100`). The integer equations are in
  `docs/DS_GX_RESEARCH.md:222-258`.
- **Toon/highlight.** NOT implemented; DeSmuME applies a toon table and highlight add
  (`rasterize.cpp:617-637`). Out of scope: `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`.
- **Fog.** NOT implemented; DeSmuME builds a fog density table (`rasterize.cpp:2295-2316`).
  Out of scope: `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`.

**Colour, clear and display**

- **15-bit colour.** VERIFIED: DS `COLOR` masks to 15 bits, `param & 0x00007FFF`
  (`gfx3d.cpp:1354`), and DeSmuME forces the vertex-colour alpha to 0 (`gfx3d.cpp:1573`).
  Our pipeline renders in PICA 8-bit and expands 5-bit input as `(v<<3)|(v>>2)` (the
  expansion used by the repo's tolerance report, `docs/reports/tex-formats-tolerance.md:90-96`).
- **Clear colour/depth.** VERIFIED: the RSTA stream carries the clear registers; our renderer
  clears with `C3D_RenderTargetClear(..., C3D_CLEAR_ALL, RENDER_CLEAR_COLOR, 0)`
  (`arm11/source/renderer.c:1527-1528`).
- **Display capture.** NOT modelled: DeSmuME has a display-capture path
  (`GPU.cpp:3330`, `_displayCaptureEnable`), and neither the 5A nor the 5B scope list
  includes it (`docs/PHASE5A_V2_RENDERER_DESIGN.md:11`, `docs/PHASE5B_PLAN.md:28-33`).
- **Wireframe/outline-only alpha 0, one-dot polygons, far-plane intersect.** NOT
  implemented: alpha 0 is counted (`common/twlhd_gx_interp.c:1075-1077`) but drawn as an
  ordinary polygon, and the POLYGON_ATTR bits exist only as a bit table
  (`docs/DS_GX_RESEARCH.md:14-40`).
- **3D-engine timing.** Out of scope (`docs/PHASE5B_PLAN.md:28-33`).

### 2.2 Fact vs hypothesis

- **Facts:** the differences listed above with a VERIFIED citation are properties of the
  emulator source or of our source. They are not hardware measurements.
- **Hypotheses:** (i) the "must be equal" rows compose into an equal picture; (ii) the
  "may differ" rows stay inside the derived tolerances of section 5; (iii) the DS rasteriser
  rules that DeSmuME models (quad fill, w-depth, alpha constant) match silicon. None of the
  three is tested.

### 2.3 Scope table — must be equal / may differ within a tolerance / known not implemented

"Equal" is meant at the level of the produced vertex/colour/pixel values, with the tolerance
of section 5 where a tolerance is named.

| Stage | Verdict | Basis |
|---|---|---|
| Matrix stack, `MTX_MULT_*` write order, `MTX_PUSH/POP` semantics | **must be equal** | analytic checks, `docs/EMULATOR_VALIDATION.md:28-34`; `docs/DS_GX_RESEARCH.md:88-95` |
| Primitive assembly: triangle list, tri strip winding, quad carrier corners | **must be equal** | `docs/DS_GX_RESEARCH.md:88-95`; `common/twlhd_gx_interp.c:636-655`, `:684-703` |
| Vertex transform (model × view × projection) | **must be equal** (0.004 clip units = 1 DS px; compared in screen space) | section 5.2; `docs/EMULATOR_VALIDATION.md:28-34` |
| Viewport mapping (scale/offset, y flip) | **must be equal** | Run 8 HARDWARE OBSERVED, `docs/PHASE5A_V2_RENDERER_DESIGN.md:67`; `gfx3d.cpp:3305-3311` |
| Front-face / cull semantics | **must be equal** | HARDWARE settled by `DIAGSCREEN=9` on New 3DS TWL mode (not DS Phat/Lite), `docs/reports/5c4-black-screen-analysis.md:442-462` |
| Texture decode, orientation, seven formats | **must be equal** | Runs 20/21, `docs/PHASE5B_PLAN.md:844-852`, `docs/EMULATOR_VALIDATION.md:776-788` |
| Texture wrap/mirror/clamp | **must be equal** | Run 21, `docs/EMULATOR_VALIDATION.md:776-783` |
| Lit vertex colours | **must be equal** (±1 level) | Run 18, `docs/PHASE5B_PLAN.md:98-100` |
| Clear colour | **must be equal** | RSTA + `arm11/source/renderer.c:1527-1528` |
| Quad fill (one 4-gon vs two triangles) | **may differ** | `rasterize.cpp:1190-1202`; diagonal rule UNVERIFIED (`docs/DS_GX_RESEARCH.md:153-156`) |
| Scanline/edge rounding, ~1 px shift | **may differ** | `docs/PHASE5B_PLAN.md:896-897` |
| Perspective-correct interpolation | **may differ** (model agrees, hardware rounding not) | `rasterize.cpp:2139`, `:987-989` |
| 15-bit colour quantisation | **may differ** (±4 to ±8 per 8-bit channel) | section 5.1; `gfx3d.cpp:1354` |
| Alpha scale `(a+1)/32` vs `/31` | **may differ** (≤1 five-bit level) | section 5.1; `rasterize.cpp:490-494`; `docs/DS_GX_RESEARCH.md:166-170` |
| Depth test (DS w/z buffer vs our z buffer) | **may differ** | `rasterize.cpp:948-968`; `arm11/source/renderer.c:1536` |
| DS translucent depth-write / draw order | **known not implemented** | `docs/PHASE5B_PLAN.md:745` |
| Fog | **known not implemented** | `docs/PHASE5B_PLAN.md:28-33`; `rasterize.cpp:2295-2316` |
| Edge marks / anti-aliasing | **known not implemented** | `docs/PHASE5B_PLAN.md:28-33`; `rasterize.cpp:499`, `:2388-2391` |
| Toon / highlight shading | **known not implemented** | `docs/PHASE5B_PLAN.md:28-33`; `rasterize.cpp:617-637` |
| Polygon-ID effects | **known not implemented** | `docs/PHASE5B_PLAN.md:28-33`; `rasterize.cpp:656-769` |
| Display capture | **known not implemented** | `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`; `GPU.cpp:3330` |
| Wireframe/outline-only alpha 0, one-dot polygons, far-plane intersect | **known not implemented** | `docs/DS_GX_RESEARCH.md:14-40`; `common/twlhd_gx_interp.c:1075-1077` |
| DS-side viewport `+1`/y1 wrap, viewports with y1 > 191 | **known not implemented / unverifiable on 3DS** | `docs/PHASE5A_V2_RENDERER_DESIGN.md:105` |
| 3D-engine timing, DS clipping, polygon sorting, vertex/polygon RAM limits | **known not implemented** | `docs/PHASE5A_V2_RENDERER_DESIGN.md:40`; `docs/PHASE5B_PLAN.md:28-33` |

What is in scope for Phase 5B, for contrast (`docs/PHASE5B_PLAN.md:24-26`): 5B-0
per-polygon alpha and cull mode, 5B-1 DS lighting computed on the CPU in the interpreter,
5B-2 textured polygons. Everything in the "known not implemented" bucket is outside that
list and therefore outside any equality target.

Consequence for the design: an equality check may only demand exactness from the
"must be equal" rows. A whole-frame per-pixel equality test would fail on the
"known not implemented" rows by design and must not be used as the acceptance criterion.

## 3. Q2 — Which reference can we compare with?

### 3.1 What the DeSmuME checkout actually offers for pixels

VERIFIED in the checkout:

- **The CLI has no screenshot, frame-dump, AVI or Lua facility.** Its whole help text is
  `desmume/src/commandline.cpp:49-140` and its getopt table is
  `desmume/src/commandline.cpp:260-340`; there is no `--screenshot`, `--dump-frame`,
  `--avi` or `--lua`. The help text lists only renderer, sync, slot, movie, scanline, debug
  and one utility command (`--advanscene-import`) — nothing that writes an image. The CLI's draw path copies
  `displayInfo.masterNativeBuffer16` into an SDL streaming texture and calls
  `SDL_RenderPresent` (`desmume/src/frontend/posix/cli/main.cpp:281-297`); with
  `SDL_VIDEODRIVER=offscreen` (`docs/EMULATOR_VALIDATION.md:240`) that is a dummy sink,
  not a file.
- **`--play-movie` is parsed but not executed by the CLI.** The option exists
  (`commandline.cpp:322`, `:388`), and `process_movieCommands()` calls
  `FCEUI_LoadMovie` (`commandline.cpp:586-596`), but the CLI never calls
  `process_movieCommands` — only `process_addonCommands` is called
  (`frontend/posix/cli/main.cpp:380`). The GTK frontends do call it
  (`frontend/posix/gtk/main.cpp:4110`). This matches
  `docs/EMULATOR_VALIDATION.md:256-257`.
- **Screenshots exist only in GUI frontends.** The GTK frontend allocates a buffer,
  converts and saves a PNG (`frontend/posix/gtk/main.cpp:2540-2597`); other GUI ports are
  not read in this study. None of them is the headless CLI.
- **The core exposes the picture in memory.** `GPU->GetDisplayInfo()` returns
  `NDSDisplayInfo` (`GPU.h:1889`) with `nativeBuffer16[2]`, the native-size 16-bit
  framebuffers (`GPU.h:1179`); the frame loop is `NDS_exec<false>()`
  (`NDSSystem.h:478`, called at `frontend/posix/cli/main.cpp:331`).
- **The core exposes the post-geometry lists.** `GFX3D_GeometryList` holds
  `rawVtxList`, `rawPolyList` and `clippedPolyList` (`gfx3d.h:642-653`); `NDSVertex` is
  position, texcoord and colour, padded to 32 bytes (`gfx3d.h:581-588`), and **the position
  is post-viewport**: x/y in 16.16 screen pixels, z in 0.31, w in 20.12
  (`gfx3d.cpp:3321-3324`); and the applied list is copied into the public
  `extern Viewer3D_State viewer3D` (`gfx3d.h:766`) at `gfx3d.cpp:3517-3525` when the 3D
  viewer is running.
- **The software rasteriser is the CLI default.** `--3d-render` defaults to `SW`
  (`commandline.cpp:54-55`) and the core registers `gpu3DRasterize` / `SoftRasterizer`
  (`rasterize.cpp:1921-1925`).
- **The existing autodump chain captures the GX stream, not pixels.** `make emu-validate`
  runs the ROM under the CLI, the ROM writes a v2 file to the emulated CompactFlash, and the
  host parses that file (`docs/EMULATOR_VALIDATION.md:11-14`); `desmume_capture.sh` collects
  `twlhd_frame_v2.bin` from the FAT image. No framebuffer is written.

### 3.2 Options table

Feasibility is stated against the checkout above. "New code" is always out-of-tree GPL,
per the licensing posture in `docs/EMULATOR_TRACER_FEASIBILITY.md:112-145`: no emulator code
enters this Apache-2.0 repository, no TWL-HD code enters the fork, and the shared artifact
is the specification only.

| Option | What exists now | What would need new code, and where | Can show | Cannot show | Cost / risk |
|---|---|---|---|---|---|
| **(a1) CLI frame-dump option** | nothing: help text `commandline.cpp:49-140`, getopt table `:260-340`; draw path `frontend/posix/cli/main.cpp:281-297` | new CLI option in an out-of-tree DeSmuME fork | SW-rasteriser frames | hardware; and the option is not in the pinned build | low code, but changes the emulator |
| **(a2) Existing autodump ROM under the CLI** | works (`docs/EMULATOR_VALIDATION.md:16-47`), but dumps the GX stream to FAT | none for the stream; a pixel dump is still missing | the exact GX stream | the picture | already done; does not answer the question |
| **(a3) Out-of-tree harness linking the core** | core API exists (`NDSSystem.h:478`, `GPU.h:1179`, `GPU.h:1889`) | ~100 lines of harness (estimate): init, load ROM, run N frames, read `nativeBuffer16[NDSDisplayID_Main]`, write PPM; lives in a separate GPL repo | the SW rasteriser's 256×192 RGB555 picture of our own ROM/frame | real DS; PICA200 rules | medium: build + link; GPL boundary |
| **(a4) Out-of-tree custom renderer / viewer** | `viewer3D` and `GFX3D_GeometryList` are public (`gfx3d.h:642-653`, `:766`) | a small `Render3D`/`GPU3DInterface` subclass (or a driver view3d) that dumps `rawVtxList`/`clippedPolyList`; separate GPL repo | post-geometry **post-viewport** vertices (16.16 x/y, 0.31 z, 20.12 w) and clipped polygons, numerically | rasterisation, blending, hardware | low-medium: no emulator patch needed |
| **(b) Owner photos of the demo ROMs** | Run 23 photos exist (`docs/TEXTURE_CAPTURE_DESIGN.md:1116-1145`) | none | gross layout/colour, gross regressions | numeric equality; camera, reflections, JPEG, moiré | zero code; the Run 14→17 correction is the cautionary precedent (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:785-787`) |
| **(c) NTR capture of the DS-mode top screen** | `tools/ntr_capture` streams NTR-HR; its policy lists the "TWL/DS-mode path" as owner-approval brick-risk (`tools/ntr_capture/README.md:241-251`) | a read-only trial: remote play while a demo ROM runs in TWL mode | exact RGB of whatever the 3DS streams | not reachable: the 2026-11-19 trial got no TCP 8000 connection while a DS ROM ran in TWL/DS mode (12 attempts); see `docs/reports/eq4-ds-mode-capture.md` | one owner-approved run; brick-risk classification |
| **(d) Analytic geometry cross-check** | see (a4) | the same harness; comparison code may live in `tools/` (Apache) reading two text dumps | matrix/stack/assembly/transform equality, numerically | rasterisation, blending, hardware | cheapest independent check; tolerance from 4.12 quantum |
| **(e) Second independent emulator (melonDS)** | audited in `docs/EMULATOR_TRACER_FEASIBILITY.md:63-72` (hook) and `:155` (M4) — not re-read this session | a melonDS hook/harness, separate GPL repo | cross-emulator agreement on our homebrew frames | hardware | medium; **UNVERIFIED** here (melonDS not read in this session) |
| **(f) DS-side framebuffer dump from our own ROM** | **model says no for a texture-mapped bank**: only an LCDC/ABG/OBJ bank enters the CPU map (`MMU.cpp:443-458`, `:494-502`, `:504-511`), while MST=3 sends the bank only to `textureSlotAddr` (`MMU.cpp:555-558`); already recorded at `docs/TEXTURE_CAPTURE_DESIGN.md:1153`. Which mapping holds our 3D framebuffer is UNVERIFIED. The demo does map bank A to LCDC while uploading textures (`arm9v2/source/demo_frame.cpp:176-179`) then switches it to texture (`:186-187`) | a ROM change: after `SWAP_BUFFERS`, read the 256×192 15-bit framebuffer and write it through the existing FAT path — **after** a host-only check that the bank is CPU-mapped | the **exact DS picture**, bit-exact, no camera/JPEG/emulator | anything the DS does not display (e.g. hidden buffers) | one source/GBATEK question + one ROM change + one owner-approved run; hardware readback UNVERIFIED, model-level obstacle |

**Independence, ranked (and why it matters).** (f) real silicon, shares nothing with us — if
it is obtainable at all. (b) and (c) real hardware too, but (b) adds a camera/JPEG and (c)
depends on an unknown streaming path. (e) a second emulator, independent of DeSmuME and of
our renderer. (a) an independent **rasteriser** model, but **not** an independent geometry
model: our interpreter's equations were derived from DeSmuME in the first place
(`docs/DS_GX_RESEARCH.md:7-12`), so agreement there catches implementation defects
(typos, wrong matrix mode, stack bugs, wrong quad order) and cannot catch a shared misreading
of the DS. (d) inherits exactly (a)'s position.

### 3.3 Per-option detail and the honest recommendation

**(a) DeSmuME software rasteriser.** The only way to get a DeSmuME picture is new
out-of-tree code. Option (a3) is the smallest: the core already returns the framebuffer
through `GetDisplayInfo()`. Option (a4) is even smaller if only vertices are wanted, because
`viewer3D` is a public symbol. What (a) can show: whether our pipeline and an independent DS
rasteriser model agree for the same stream, which is a strong check of the pipeline's own
geometry and rasterisation assumptions. What it cannot show: the DS, because DeSmuME is a
model (`docs/DS_GX_RESEARCH.md:10-12`) and its own winding caveat is explicit
(`rasterize.cpp:24-26`: "due to a late change of a y-coord flipping, our winding order is
wrong").

**(b) Photos.** Qualitative only. The repo has already paid for this lesson: Runs 14-16 read
"no blending" off camera photos and Run 17's numeric NTR capture overturned it
(`docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787`). Photos are acceptable for "is the picture
grossly right", never for equality.

**(c) NTR capture in TWL/DS mode.** **UNKNOWN.** There is no evidence in `tools/ntr_capture`
or in the docs that NTR-HR reads the DS-mode top screen; the tool's policy lists the
"TWL/DS-mode path" among operations that "still require owner approval"
(`tools/ntr_capture/README.md:249`). The tool itself performs no console writes
(`tools/ntr_capture/README.md:235-238`), so a trial is read-only in the tool's sense; the
approval is for taking the console down the TWL path at all. A read-only test that would
decide it: start NTR-HR remote play, launch a demo ROM in TWL mode, capture both screens, and
check whether the top screen shows the DS 3D output (a known scene) rather than a
duplicate/blank/stale frame. One run, no writes, owner-approved. **HYPOTHESIS:** it may work,
because the 3DS streams the top LCD whatever the running mode; **HYPOTHESIS:** it may return
the 3DS's own compositor output instead of the DS framebuffer.

**Result (2026-11-19):** **EQ-4: NTR-HR remote play (TCP 8000 / UDP 8001) is NOT reachable
while a DS ROM runs in TWL/DS mode on this console (one run, 12 attempts 8 s apart,
13:18:34-13:20:38, 2026-11-19); option (c) is not available as a numeric DS-mode capture.**
The 3DS-mode control captured both screens; all 12 DS-mode attempts wrote no frame. The
trial design and the two HYPOTHESES above are kept as pre-run text.
Report: `docs/reports/eq4-ds-mode-capture.md`.

**Result EQ-4b (2026-11-22):** **EQ-4b: a cc3dsfs capture board attached to the New 3DS captured the DS-mode top screen as numeric pixels on 2026-11-22 (36 frames, pure colours preserved). Option (c) is achievable by this route, not via NTR-HR (see EQ-4).** A passive USB capture board, not NTR-HR, supplies the pixels; the image scale/offset and the capture layout are UNVERIFIED.
Report: `docs/reports/eq4b-cc3dsfs-ds-capture.md`.

**Update (2026-11-22):** EQ-4c measured the DS 256×192 image in the EQ-4b capture: 240×240 capture px at x 30..269, y 0..239
(scale_x 0.9375, scale_y 1.25, offset 30,0; all four corner markers found; marker-centre cross-check within 1 px).
Report: `docs/reports/eq4c-ds-scale-measurement.md`.

**Erratum (2026-11-23):** the EQ-4b capture was decoded as 32-bpp BGRX although the XWD header says `bits_per_pixel=24` (first 14 fields `(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)`); the real layout is 3-byte BGR with `bytes_per_line` 5120, so the Update above (`scale_x 0.9375`, offset 30) is the mis-decoded input's number, not the board's. Corrected single-capture (`vt1b_2`) facts: DS pixel (x,y) at capture (40+1.25x, 1.25y); quads white (255,255,255), grey (132,134,132), red (255,0,0), black (0,0,0), background (66,69,66) ±3 (UNVERIFIED). Invalid: the EQ-4b "pure colours preserved" and the eq4c hypotheses 2/3 (0.75 squeeze, non-identity colour path, DS white (255,251,255)). Valid: the board-connects / numeric-pixels verdict and the eq4c §6 source reading. Report: `docs/reports/eq4d-ds-vertex-colour-hw.md`.

**Update (2026-11-23):** EQ-4e recorded a scene5 hardware capture through the fixed decoder: the top 400×240 crop shows the rotating textured quad in five colour cells (black background, white, yellow (255,255,24), green (0,255,8), blue (0,0,247), red (255,0,0)), consistent with the white initial vertex colour (EQ-4d; two ROM variants); no cyan (24,255,255) or magenta (255,24,255) at count ≥ 20 in the histogram. A pixel comparison with the PICA render still needs a static, non-rotating build to align phase. Report: `docs/reports/eq4e-scene5-hw-capture.md`.

**(d) Geometry-level cross-check.** The cheapest check that can catch a real defect. It must
compare against DeSmuME's own numbers, not the host mock's, because the host mock cannot
reproduce the DS divider/sqrt/trig tables (`docs/EMULATOR_VALIDATION.md:53-54`). One
conversion is mandatory: `TwlhdGxVertex.clip` is clip space (`common/twlhd_gx_interp.h:24`),
while `NDSVertex.position` is already through the viewport (`gfx3d.cpp:3321-3324`). Either
apply DeSmuME's own formula to our clip — `x = ((x + w) * width) / (2w)`, then
`x += x1`; `y = ((y + w) * height) / (2w)`, `y += y1`, then `y = (192 << 16) - y`
(`gfx3d.cpp:3287-3311`, per that polygon's latched viewport, `:3258`) — or dump the
pre-viewport values from a custom renderer hook. The first needs no new emulator code and is
the recommended form; it assumes one viewport per frame, which holds for our demo, so a frame
that changes `VIEWPORT` mid-frame must be compared polygon by polygon. Tolerance: section
5.2. This is the recommended first stage after this study — with the independence limit
stated above: it is a defect detector, not a proof that our geometry equations match the DS.

**(e) melonDS.** A second emulator is the standard way to validate a tracer or a reference
implementation (`docs/EMULATOR_TRACER_FEASIBILITY.md:63-72`, M4 at `:155`). It doubles the
cost of any pixel harness. **UNVERIFIED** in this session (melonDS source was not read).

**(f) DS-side framebuffer dump — a model-level obstacle found in this study.** If it worked
it would settle the question, because it removes every model and every camera. The DeSmuME
model does say an **LCDC**-mapped bank is in the CPU page map (`MMU.cpp:504-511`), CPU reads
resolve through it (`MMU.cpp:443-458`) and the mapping is an identity page map
(`MMU.cpp:494-502`); the demo maps bank A to LCDC while uploading textures
(`arm9v2/source/demo_frame.cpp:176-179`). But a bank mapped as **texture** is not placed in
the CPU map at all — only `textureSlotAddr` receives it (`MMU.cpp:555-558`), so a CPU read
resolves to `VRAM_PAGE_UNMAPPED` and returns 0 (`MMU.cpp:443-458`). The repository already
records this: "A texture-mapped bank is CPU-readable — **UNVERIFIED**; DeSmuME's model says
no (`MMU.cpp:555-558`, `:443-456`)" (`docs/TEXTURE_CAPTURE_DESIGN.md:1153`). Which mapping
holds the 3D framebuffer in our ROM's configuration is **UNVERIFIED** (GBATEK was not
consulted; the demo switches bank A to `VRAM_A_TEXTURE` after the upload,
`arm9v2/source/demo_frame.cpp:186-187`). So this option is not "one cheap experiment" yet:
its first step is a host-only source/GBATEK question (which bank, which mapping), and only
then a ROM experiment. Verdict: **low priority, HYPOTHESIS, likely blocked** — not the
recommended first hardware run. The payoff, if it ever works, is the only bit-exact DS
picture available without new hardware.

## 4. Q3 — Staged plan EQ-1…EQ-6

Labels are the repository's BUILD / UNIT / EMULATOR / HARDWARE. "Host-only" means no
console. "Owner approval" means the assistant may not run it. Effort is in pipeline runs
(one run = one build + one execution of the comparison), not in wall-clock.

### EQ-1 — Reference-facility audit (host-only, DOC)

- **Goal:** pin exactly what the DeSmuME checkout can and cannot emit, and where new code
  would live.
- **Deliverable:** this document (sections 3.1-3.2).
- **Files touched:** `docs/EQUALITY_CHECK_DESIGN.md` only.
- **Label:** none (documentation); no BUILD/UNIT/EMULATOR/HARDWARE claim.
- **Risk:** line-number drift; mitigated by re-checking every cited line at `fc504ee0`.
- **Effort:** 0 pipeline runs.
- **Approval:** none.
- **Proves:** that no existing CLI option or repository tool can produce a DeSmuME picture,
  and that the core API needed by EQ-2/EQ-3 exists.
- **Does not prove:** that a harness builds or runs.
- **Status:** done by this study.

### EQ-2 — Geometry-level numeric comparison (host-only, UNIT + EMULATOR)

- **Goal:** for one existing demo/golden frame, compare every `TwlhdGxVertex.clip[4]`
  (`common/twlhd_gx_interp.h:22-26`) with DeSmuME's post-geometry `NDSVertex.position`
  (`gfx3d.h:581-588`) for the same GX stream — **in DS screen space**, because DeSmuME's
  position is post-viewport (16.16 x/y, 0.31 z, 20.12 w; `gfx3d.cpp:3321-3324`) while ours is
  clip (`common/twlhd_gx_interp.h:24`). The script applies the viewport formula
  (`gfx3d.cpp:3287-3311`) to our clip; no emulator patch is needed for that.
- **Deliverable:** an out-of-tree GPL harness that runs our ROM under DeSmuME and writes the
  applied `viewer3D.gList` (`gfx3d.h:766`, filled at `gfx3d.cpp:3517-3525`) as text; plus a
  small Apache-2.0 comparison script in `tools/` that reads the two text dumps and applies
  the section 5.2 tolerance. The harness stays outside the repository.
- **Files touched:** a new separate GPL repository (out of tree) and one new file under
  `tools/` (TWL-HD side). No `common/`, `arm11/`, `arm9*` change.
- **Label:** UNIT for the comparison script; EMULATOR for the harness run (DeSmuME is an
  emulator).
- **Risk:** low. The main risk is that `viewer3D` is only populated when a viewer is running;
  the fallback is a custom `Render3D` subclass (option a4). Another risk is DeSmuME's own
  integer transform path differing from our `double` transform — that is exactly what the
  tolerance bounds.
- **Effort:** 1-2 host runs per frame.
- **Approval:** none (host-only), but the owner must accept the out-of-tree GPL harness
  (decision D1).
- **Proves:** matrix, stack, primitive-assembly and transform equality within 1 DS pixel
  (0.004 clip units = 0.512 px at 128 px per clip unit, section 5.2) on real demo frames;
  catches swapped axes, wrong matrix mode, wrong push/pop and wrong quad/strip assembly,
  which are the defects the pixel checks would only see as a misplaced picture.
- **Cannot prove:** rasterisation, texturing, blending, or anything about the DS.

### EQ-3 — Same-stream software-rasteriser picture comparison (host-only, EMULATOR)

- **Goal:** compare the picture from our pipeline with DeSmuME's software rasteriser for the
  same frame and the same scene.
- **Deliverable:** the (a3) harness writing a 256×192 PPM; the comparison reuses the existing
  structural checkers (`tools/check_tex_shot.py`, `tools/check_tex_formats.py`,
  `tools/check_tex_behaviour.py`) and/or a new comparison script under `tools/`. Our side of
  the picture is the PICA200 output (Azahar host or the 3DS), resampled to 256×192.
- **Files touched:** out-of-tree GPL harness; one new comparison script under `tools/`; no
  renderer or interpreter change.
- **Label:** EMULATOR — and only if our side is the **host** PICA picture (Azahar). Substituting
  the 3DS picture makes the stage HARDWARE and duplicates EQ-4/EQ-5.
- **Risk:** medium-high. Our picture is rendered at 400×240 with the DS viewport in a
  centred 320×240 region (`docs/PHASE5A_V2_RENDERER_DESIGN.md:48-49`), so a 256×192
  comparison needs a 0.8 resample of our side: the resample alone can move an edge or a
  colour by more than the 8-level tolerance, which is why this stage must be **structural**,
  not per-pixel — region bounding boxes, texel-cell centres, colour classes and edge
  exclusion, the way `tools/check_tex_shot.py` already works — and why the quad-diagonal
  difference and the ~1 px shift (`docs/PHASE5B_PLAN.md:896-897`) must be tolerated rather
  than measured. Two models can share a wrong rule (e.g. both assume the same quad fill).
- **Effort:** 2-3 host runs.
- **Approval:** none (host-only); same D1 acceptance as EQ-2.
- **Proves:** the pipeline's rasterisation-level **structure** (where regions land, which
  colour class each cell has) against an independent DS rasteriser model for flat, alpha-31,
  planar scenes, within the section 5.1 tolerance at the sampled cells. It does not prove
  per-pixel equality, because the resample forbids it.
- **Cannot prove:** the DS, or the "known not implemented" rows of section 2.3.

### EQ-4 — DS-mode top-screen capture trial (HARDWARE, owner approval per run)

- **Goal:** decide option (c) — can NTR-HR capture the DS-mode top screen?
- **Deliverable:** a short procedure (in `docs/` or the run log) and one PPM pair; a verdict
  line "NTR-HR works / does not work in TWL mode".
- **Files touched:** documentation of the run only; `tools/ntr_capture` unchanged.
- **Label:** HARDWARE.
- **Risk:** the tool writes nothing, but the TWL/DS-mode path is listed as owner-approval
  brick-risk (`tools/ntr_capture/README.md:249`). Worst case is a console that needs a
  restart; the existing recovery procedure applies.
- **Effort:** 1 owner-approved run.
- **Approval:** **required, per run** (owner executes or explicitly authorises).
- **Proves:** whether numeric DS-mode pixels are obtainable; if yes, it upgrades every later
  hardware comparison from photos to pixels.
- **Cannot prove:** equality by itself; the capture is a JPEG stream, so the measured worst
  cell is 26 and the default tolerance is 28 (`docs/EMULATOR_VALIDATION.md:776-783`).

**Result (2026-11-19):** **EQ-4: NTR-HR remote play (TCP 8000 / UDP 8001) is NOT reachable
while a DS ROM runs in TWL/DS mode on this console (one run, 12 attempts 8 s apart,
13:18:34-13:20:38, 2026-11-19); option (c) is not available as a numeric DS-mode capture.**
Control (3DS mode) captured both screens; all 12 DS-mode attempts wrote no frame. The
remaining hardware options are EQ-5 and EQ-6; EQ-3 is host-only and unaffected.
Report: `docs/reports/eq4-ds-mode-capture.md`.

**Result EQ-4b (2026-11-22):** **EQ-4b: a cc3dsfs capture board attached to the New 3DS captured the DS-mode top screen as numeric pixels on 2026-11-22 (36 frames, pure colours preserved). Option (c) is achievable by this route, not via NTR-HR (see EQ-4).** The route tried after the EQ-4 failure was a cc3dsfs USB capture board (id 0752:8613); 36 frames were captured, image scale/offset UNVERIFIED.
Report: `docs/reports/eq4b-cc3dsfs-ds-capture.md`.

**Update (2026-11-22):** EQ-4c measured the DS 256×192 image in the EQ-4b capture: 240×240 capture px at x 30..269, y 0..239
(scale_x 0.9375, scale_y 1.25, offset 30,0; all four corner markers found; marker-centre cross-check within 1 px).
Report: `docs/reports/eq4c-ds-scale-measurement.md`.

**Erratum (2026-11-23):** the EQ-4b capture was decoded as 32-bpp BGRX although the XWD header says `bits_per_pixel=24` (first 14 fields `(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)`); the real layout is 3-byte BGR with `bytes_per_line` 5120, so the Update above (`scale_x 0.9375`, offset 30) is the mis-decoded input's number, not the board's. Corrected single-capture (`vt1b_2`) facts: DS pixel (x,y) at capture (40+1.25x, 1.25y); quads white (255,255,255), grey (132,134,132), red (255,0,0), black (0,0,0), background (66,69,66) ±3 (UNVERIFIED). Invalid: the EQ-4b "pure colours preserved" and the eq4c hypotheses 2/3 (0.75 squeeze, non-identity colour path, DS white (255,251,255)). Valid: the board-connects / numeric-pixels verdict and the eq4c §6 source reading. Report: `docs/reports/eq4d-ds-vertex-colour-hw.md`.

**Update (2026-11-23):** EQ-4e recorded a scene5 hardware capture through the fixed decoder: the top 400×240 crop shows the rotating textured quad in five colour cells (black background, white, yellow (255,255,24), green (0,255,8), blue (0,0,247), red (255,0,0)), consistent with the white initial vertex colour (EQ-4d; two ROM variants); no cyan (24,255,255) or magenta (255,24,255) at count ≥ 20 in the histogram. A pixel comparison with the PICA render still needs a static, non-rotating build to align phase. Report: `docs/reports/eq4e-scene5-hw-capture.md`.

### EQ-5 — Real-DS qualitative photo comparison (HARDWARE, owner photos only)

- **Goal:** side-by-side of the DS picture and our PICA picture for the same frame, judged
  against the section 2.3 "must be equal" rows only.
- **Deliverable:** a photo set and a verdict table (layout, colours, orientation, gross
  texture behaviour) with every limitation stated.
- **Files touched:** documentation only.
- **Label:** HARDWARE.
- **Risk:** camera, reflections, JPEG, moiré; the Run 14→17 correction is the precedent
  (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787`).
- **Effort:** 1 owner run (can be combined with EQ-4 and EQ-6 in one session).
- **Approval:** **required, per run**.
- **Proves:** gross correctness only.
- **Cannot prove:** numeric equality, or anything within a few levels.

### EQ-6 — DS-side framebuffer dump from our own ROM (host-only check, then HARDWARE, owner approval per run)

- **Goal (first, host-only):** settle whether the bank holding our 3D framebuffer is
  CPU-mapped at all. A bank mapped as texture is absent from the CPU map in DeSmuME
  (`MMU.cpp:555-558`, `:443-458`; `docs/TEXTURE_CAPTURE_DESIGN.md:1153`), so the question is
  which mapping `MODE_0_3D` gives the framebuffer on the DS (`arm9v2/source/main.cpp:171`).
  GBATEK was not consulted in this study; this step is a source read plus a decision, no run.
- **Goal (second, hardware):** if step 1 says the bank is CPU-mapped, test whether the ARM9
  can read the displayed 15-bit framebuffer and write it to the existing FAT path.
- **Deliverable:** step 1 a paragraph in a follow-up doc; step 2 a ROM change that dumps the
  256×192 framebuffer after `SWAP_BUFFERS`, plus a host-side PPM converter. If it works, the
  same frame's DS pixels become a bit-exact reference.
- **Files touched:** step 1 documentation only; step 2 `arm9v2/` (dump code) and `tools/`
  (converter) in a later, separately approved implementation stage; nothing in this study.
- **Label:** step 1 none (DOC); step 2 HARDWARE (plus BUILD/UNIT for the ROM change).
- **Risk:** medium-high. Hardware LCDC-VRAM readback is UNVERIFIED and the model says no for a
  texture-mapped bank; the bank the 3D engine scans out must be identified (VRAMCNT);
  `SCENE=5` switches bank A to texture (`arm9v2/source/demo_frame.cpp:186-187`); a wrong read
  can return stale or blank data.
  The dump must not disturb the running frame.
- **Effort:** 1 owner-approved run after a BUILD/UNIT implementation.
- **Approval:** **required, per run**.
- **Proves:** if it works, the exact DS picture for our own ROM — the only reference in this
  list with no model and no camera in the loop.
- **Cannot prove:** anything about commercial ROMs or other hardware revisions; and it says
  nothing about frames the DS does not display.

### Ordering and what each stage unlocks

1. **EQ-2 first** (cheap, host-only, catches real defects).
2. **EQ-3 next** (still host-only; needs the harness from EQ-2).
3. **EQ-6 step 1 next** (host-only source question, no run); EQ-6 step 2 only if step 1 says
   the framebuffer bank is CPU-mapped, in which case it is worth one hardware run.
4. **EQ-4** only if EQ-6 is blocked, or if numeric pixels of *arbitrary* content are needed.
   This step was executed on 2026-11-19: EQ-4 is not available as a numeric DS-mode capture
   (remote play not reachable in TWL/DS mode, `docs/reports/eq4-ds-mode-capture.md`).
5. **EQ-5** last, as a sanity check that the pipeline's picture looks like the DS's to a
   human.

## 5. Q4 — How the tolerances are derived (not guessed)

Method: the repository's own tolerance method, from
`docs/reports/tex-formats-tolerance.md:90-140`: a tolerance `T` must satisfy
`measured worst <= T < (closest distinct expected pair) / 2`, where the separation is
computed from the frame's own expected colours. The derived floors below are the *lower*
bound; the frame's separation is the *upper* bound; the actual `T` is chosen between the
measured worst on a known-good control and the frame's separation midpoint.

### 5.1 Pixel comparisons — floor components and arithmetic

**(i) 15-bit colour quantisation.** The DS channel is 5-bit (`gfx3d.cpp:1354`). The exact
5-bit→8-bit expansion used in this repository is `(v << 3) | (v >> 2)`
(`docs/reports/tex-formats-tolerance.md:90-96`), so adjacent codes are 8 or 9 levels apart:
`255 / 31 = 8.2258`, minimum step 8, **half-step = 4**. If the two sides expand or
quantise the same 5-bit value independently, the floor is **±4** per channel. If one side passes through a
6-bit intermediate truncated to 5 bits (DeSmuME's rasteriser clamps colour to `0x3F`, i.e.
6 bits, `rasterize.cpp:987-989`), one more 5-bit level is possible: **±8**.

**(ii) Alpha-scale convention.** DeSmuME uses `(a+1)/32` (`rasterize.cpp:490-494`); the
alternative DS reading is `a/31` (`docs/DS_GX_RESEARCH.md:166-170`). The weight difference is

    Δw(a) = (a+1)/32 - a/31 = (31 - a) / 992.

Applied to two colours at most 255 apart, the per-channel difference is
`255 * (31 - a) / 992 = (31 - a) * 0.2571` levels:

| alpha | Δw | 8-bit levels (max colour separation) |
|---|---|---|
| 1 | 0.03024 | 7.71 |
| 4 | 0.02722 | 6.94 |
| 16 | 0.01512 | 3.86 |
| 24 | 0.00706 | 1.80 |
| 30 | 0.00101 | 0.26 |
| 31 | 0 | 0 |

In 5-bit source units the same difference is `(31 - a) / 32`, i.e. **≤1 level of a
5-bit channel** for every `a >= 1` (0.9375 at a = 1, 0 at a = 31; a = 0 is the DS wireframe
case and is excluded) — that is the "≈1 level" often quoted. Our interpreter already emits
`(a+1)/32` (`common/twlhd_gx_interp.c:387-397`), so this floor is zero if hardware uses the
same convention and up to **8** 8-bit levels for low alpha if it uses `/31`. The hardware
convention is UNVERIFIED; blended frames must not be compared at a tolerance tighter than 8
until it is settled.

**(iii) Fixed-point 4.12.** One 20.12 quantum is `1/4096 = 0.000244`
(`docs/EMULATOR_VALIDATION.md:33-34`). A 4-term dot product accumulated in 64 bits with a
final `>> 12` can lose up to 1 LSB per term: 4 LSB = **0.000977** clip units
(HYPOTHESIS: the bound is derived from the arithmetic, not measured; the measured worst in
`docs/EMULATOR_VALIDATION.md:30` is 0.00178, i.e. 7.3 quanta). This bounds vertex
comparisons (section 5.2), not pixels.

**(iv) Console blend agreement.** Run 17 measured the console's standard blend within about
**2 levels** of the predicted value (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:783-787`). This is
a hardware data point, not a derived bound.

**(v) JPEG/capture noise.** The measured worst deviation on a good NTR capture is **26**
levels, the default tolerance is **28**, and the binding closest-distinct-pair midpoint is
**33** (`docs/reports/tex-formats-tolerance.md:90-140`). A hardware capture comparison must
use this measured floor, not the derived ones, whenever it compares against a JPEG stream.

**(vi) The ~1 px shift.** HARDWARE OBSERVED: the console picture is shifted about 1 px
up/left relative to the emulator (`docs/PHASE5B_PLAN.md:896-897`). A per-pixel comparison
must either align by search (dx, dy ∈ {−1, 0, 1}, as `tools/check_tex_formats.py:12-13`
does) or exclude edges; `tools/check_tex_shot.py:319` instead tolerates a 1-px bounding-box
shift when it compares cells. Without one of the two, edge pixels differ by full scale and
the tolerance is meaningless.

**Proposed pixel tolerances**

| Comparison | Derived floor | Proposed T | Why |
|---|---|---|---|
| Host PICA picture vs DeSmuME SW picture, flat, alpha 31, planar, interior pixels | 4 (quantisation) or 8 (6-bit intermediate) | **8** | one full 5-bit level; leaves room for the 6-bit truncation; the existing frame's closest distinct pair is 66 apart (midpoint 33, `docs/reports/tex-formats-tolerance.md:98-140`), so 8 leaves a 25-level margin |
| Same, blended pixels | 8 (alpha convention) + 8 (quantisation) | **16** until the hardware scale is settled | worst case at alpha 1; measure before tightening |
| Hardware NTR capture vs our picture | 26 measured | **28** | the repository's measured default; must stay below the frame's separation midpoint |
| Any frame | — | `T < (closest distinct expected pair)/2` | the tex-formats rule; for the existing frame that midpoint is 33 |

**Do not** use a whole-frame per-pixel equality test: the "known not implemented" rows
(fog, edges, AA, toon, display capture) can differ by full scale by design.

### 5.2 Vertex comparisons — floor and arithmetic

- **Clip space.** Inputs are 20.12 (`docs/PHASE5A_V2_RENDERER_DESIGN.md:31-35`). The
  existing matrix tolerances are 0.004 for perspective (measured worst 0.00178) and 0.002
  for a 3×3 block (measured worst 0.00017), with one quantum 0.000244
  (`docs/EMULATOR_VALIDATION.md:28-34`). `0.004 / 0.000244 = 16.4`, i.e. the existing
  perspective tolerance is about 16 quanta and 2.2× the measured worst. **Proposed vertex
  tolerance: 0.004 clip units per component**, reused rather than re-derived, because it is
  already justified by measurement.
- **Screen space.** The DS viewport maps clip x ∈ [−1, 1] to 256 px, i.e. 128 px per clip
  unit. 0.004 clip units → **0.512 DS px**; add the 1/65536 px integer-divide truncation
  (`gfx3d.cpp:3287-3311` divides and offsets; `:3321-3322` store 16.16) and **1 DS px** is a
  safe screen-space tolerance.
  Our picture is 320×240 content on a 400×240 screen (`docs/PHASE5A_V2_RENDERER_DESIGN.md:48-49`),
  so 1 DS px is **1.25 top-screen px**; the observed ~1 px hardware shift is the same order.
- **Geometry-level (EQ-2).** The two vertex lists are in different spaces, so the comparison
  is defined precisely:
  - **Chosen form (no emulator patch):** convert our clip to DS screen space with DeSmuME's
    formula (`gfx3d.cpp:3287-3311`) using the frame's viewport, then compare x/y with
    `NDSVertex.position.x/y` (16.16) at **1 DS px = 65536 units**, and w with
    `position.w` (20.12) at **0.004 = 16 quanta**. z is clamped to `0 .. 0x7FFFFFFF` and
    stored as 0.31 (`gfx3d.cpp:3313-3317`), so its precision is 1/2³¹ of the depth range —
    far below any pixel tolerance; compare it only after the same conversion.
  - **Alternative:** a custom `Render3D` hook dumps the **pre-viewport** clip values
    (`GFX3D_TransformAndClip` input) and the comparison is clip-to-clip at 0.004 per
    component; this needs new out-of-tree emulator code and is only worth it if the screen
    space comparison proves ambiguous.
  Report the maximum and the index of the worst vertex. Do not compare `rgba` exactly for
  lit frames: the DS lighting is integer 5-bit and the Run 18 agreement is ±1 level
  (`docs/PHASE5B_PLAN.md:98-100`).

### 5.3 What cannot be set yet

Hardware pixel tolerances for EQ-5/EQ-6 must be derived from the first capture, using
the same method (measured worst on a known-good control, bounded by the frame's separation
midpoint). They are deliberately not guessed here. EQ-4 produced no capture — remote play is
not reachable in TWL/DS mode (2026-11-19, `docs/reports/eq4-ds-mode-capture.md`) — so the
derivation falls to EQ-5 and EQ-6. The only hardware numbers available today
are Run 17's ~2 levels (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:783-787`) and the Run 20/21
JPEG worst 26 with T=28 (`docs/EMULATOR_VALIDATION.md:776-783`,
`docs/reports/tex-formats-tolerance.md:90-140`); both are for specific frames, not general
bounds.

## 6. Q5 — Risks, non-goals and owner decisions

### 6.1 Non-goals of this study

- It implements no comparison code, no tool, no ROM change and no Makefile target.
- It runs no emulator, no `desmume-cli`, no console and no hardware.
- It does not claim any equality result, and it does not weaken any existing tolerance or
  test.
- It does not propose vendoring or linking DeSmuME into this repository.

### 6.2 Risks

| Risk | Evidence / mitigation |
|---|---|
| Overclaiming equality from component checks | the repo already corrected an "orientation verified" overclaim (`docs/PHASE5B_PLAN.md:901-918`); every claim here is labelled |
| Two models sharing one wrong rule (EQ-3) | DeSmuME's own winding caveat (`rasterize.cpp:24-26`); add melonDS (option e) if the stakes justify it |
| Camera/JPEG evidence misread as fact | Run 14→17 correction (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787`) |
| GPL contamination | follow `docs/EMULATOR_TRACER_FEASIBILITY.md:112-145`: out-of-tree fork only, spec-only coupling, TWL-HD validators as black boxes |
| Line-number drift in older docs | this file re-checks every cited line at `fc504ee0`; `arm11/source/renderer.c` citations in `docs/DS_GX_RESEARCH.md:182-187` (996-999/998) have drifted to `1532-1537` |
| Hardware readback (EQ-6) disturbs the frame | read only after the frame is complete and before the next one; the texture-capture design already documents the analogous risk (`docs/TEXTURE_CAPTURE_DESIGN.md:326-360`) |
| The DS picture may be unobtainable without a camera | option (c) was executed on 2026-11-19 and returned no capture (remote play not reachable in TWL/DS mode, `docs/reports/eq4-ds-mode-capture.md`); option (f) now has a model-level obstacle (section 3.3(f)). Neither is a promise; EQ-6 starts with a free source check |

### 6.3 Owner decisions (each with a recommendation and its evidence)

**D1 — Accept an out-of-tree GPL harness (EQ-2/EQ-3)?**
**Recommend: yes.** The project already worked out the posture
(`docs/EMULATOR_TRACER_FEASIBILITY.md:112-145`): emulator code stays in a separate
GPL repository or patch, TWL-HD code never enters it, the shared artifact is the
specification, and TWL-HD's validators are used as external black boxes. No code enters this
Apache-2.0 repository.

**D2 — Try DS-mode top-screen capture on the console (EQ-4)?**
**Recommend: yes, one read-only owner-approved run.** `tools/ntr_capture` performs no console
writes (`tools/ntr_capture/README.md:235-238`), but the "TWL/DS-mode path" is explicitly
listed as owner-approval brick-risk (`:249`), so the owner must authorise it. The payoff is
numeric pixels instead of photos for every later hardware comparison. Do it before EQ-6: if
EQ-4 works it gives numeric pixels of arbitrary content and EQ-6's remaining advantage
(bit-exactness, no emulator in the loop) is only worth pursuing separately.

**Result (2026-11-19):** **EQ-4: NTR-HR remote play (TCP 8000 / UDP 8001) is NOT reachable
while a DS ROM runs in TWL/DS mode on this console (one run, 12 attempts 8 s apart,
13:18:34-13:20:38, 2026-11-19); option (c) is not available as a numeric DS-mode capture.**
The 3DS-mode control captured both screens; all 12 DS-mode attempts wrote no frame. The
recommendation above is kept as the pre-run text.
Report: `docs/reports/eq4-ds-mode-capture.md`.

**Result EQ-4b (2026-11-22):** **EQ-4b: a cc3dsfs capture board attached to the New 3DS captured the DS-mode top screen as numeric pixels on 2026-11-22 (36 frames, pure colours preserved). Option (c) is achievable by this route, not via NTR-HR (see EQ-4).** The recommendation's goal (numeric DS-mode pixels) is met by the capture board, not by NTR-HR; the recommendation above is kept as pre-run text.
Report: `docs/reports/eq4b-cc3dsfs-ds-capture.md`.

**Update (2026-11-22):** EQ-4c measured the DS 256×192 image in the EQ-4b capture: 240×240 capture px at x 30..269, y 0..239
(scale_x 0.9375, scale_y 1.25, offset 30,0; all four corner markers found; marker-centre cross-check within 1 px).
Report: `docs/reports/eq4c-ds-scale-measurement.md`.

**Erratum (2026-11-23):** the EQ-4b capture was decoded as 32-bpp BGRX although the XWD header says `bits_per_pixel=24` (first 14 fields `(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)`); the real layout is 3-byte BGR with `bytes_per_line` 5120, so the Update above (`scale_x 0.9375`, offset 30) is the mis-decoded input's number, not the board's. Corrected single-capture (`vt1b_2`) facts: DS pixel (x,y) at capture (40+1.25x, 1.25y); quads white (255,255,255), grey (132,134,132), red (255,0,0), black (0,0,0), background (66,69,66) ±3 (UNVERIFIED). Invalid: the EQ-4b "pure colours preserved" and the eq4c hypotheses 2/3 (0.75 squeeze, non-identity colour path, DS white (255,251,255)). Valid: the board-connects / numeric-pixels verdict and the eq4c §6 source reading. Report: `docs/reports/eq4d-ds-vertex-colour-hw.md`.

**Update (2026-11-23):** EQ-4e recorded a scene5 hardware capture through the fixed decoder: the top 400×240 crop shows the rotating textured quad in five colour cells (black background, white, yellow (255,255,24), green (0,255,8), blue (0,0,247), red (255,0,0)), consistent with the white initial vertex colour (EQ-4d; two ROM variants); no cyan (24,255,255) or magenta (255,24,255) at count ≥ 20 in the histogram. A pixel comparison with the PICA render still needs a static, non-rotating build to align phase. Report: `docs/reports/eq4e-scene5-hw-capture.md`.

**Result EQ-4f (2026-11-23):** **EQ-4f: the first real hardware comparison of the static textured scene5 passes against both references on one console.** The cc3dsfs hardware captures of the plain `STATIC_ANGLE=0` and `=30` ROMs (fixtures `tools/fixtures/eq4_hw/st{0,30}_3_top400x240.png`) compared with the DeSmuME harness frame 240 and with the PICA render each give `verdict=PASS` (`basis=one console, one capture`; `ds.frac_within_flat` 0.9986-0.9999; bbox deviations <= 2 DS px); the swapped-angle controls FAIL. This supersedes the "neither was run on a console" sentence in `docs/EQ_SCENES.md` 6.8 for these two static ROMs only. UNVERIFIED: bit-exactness, other scenes/consoles, the alpha convention (all alpha 31). Report: `docs/reports/eq4f-scene5-static-hw-compare.md`.

**Result EQ-4g (2026-11-23):** **EQ-4g: the ~1-2 DS px offset of the scene5 hardware captures is not produced by the capture mapping.** A sub-pixel least-squares fit of `capture_edge = a*ds_edge + b` over 42 straight ds-scale-test pattern edges (50 %-midpoint linear interpolation, `tools/ds_scale_test/fit_mapping.py`) on `tools/ds_scale_test/fixtures/sc4_3_top400x240.png` gives `a_x=1.250383, b_x=39.933172` (RMS 0.072 capture px) and `a_y=1.250790, b_y=-0.168453` (RMS 0.026 capture px), i.e. at most 0.135 DS px from the current `(1.25, 40, 1.25, 0)` over the whole screen. Re-running the eq4f scene5 comparison with that fitted mapping (`tools/eq4g_fit_compare.py`) changes bbox deviations by at most 1 DS px and class centroids by at most 0.357 DS px (largest: `st30 ours` green dy -1.9839 -> -1.6270); against the PICA render (`ours`) `dy` is negative in all 20 quoted class pairs (-0.3001 to -2.5699 DS px) and `dx` is mixed (-1.7386 to +0.9474 DS px), with `bbox_dev_top=2` in all four pairs and `bbox_dev_left` 0-1; the mapping explains at most ~0.36 DS px of the offset. The DeSmuME-harness arm of that cross-check is UNVERIFIED (its commands and output are not quoted in the report). HYPOTHESIS: the offset is a real shift (hardware versus reference); UNVERIFIED which side. `capture_mapping.json`, `eq4_hw_compare.py` and its tolerances are unchanged and no mapping change is recommended. Report: `docs/reports/eq4g-capture-offset.md`.

**Result EQ-4h (2026-11-23):** **EQ-4h built a DS ROM and an offline classifier for the polygon-alpha blend convention (`A1 = a/31` vs `A2 = (a+1)/32`), but real hardware was NOT measured.** `tools/ds_alpha_test/` contains a static libnds ROM (`ds-alpha-test.nds`), a shared `quads.json`, `expected.py` (both 5-bit-then-expand and expand-then-8-bit blend families, round and floor variants, separation and weak-discriminator table), and `measure_alpha.py` (400×240 capture or `--native`, mean in a 4-DS-px margin, per-quad and overall A1/A2/UNDECIDED, NOT-COMPARABLE fallback), with 30 tests wired into `make test`. SOURCE-READ: DeSmuME's rasteriser uses `alpha = a+1`, `invAlpha = 32-alpha`, `>>5` (`rasterize.cpp:490-494`, local checkout), i.e. A2; our v2/GX interp path also uses `(a+1)/32` (`common/twlhd_gx_interp.c:395-397`, `:1074`), while the v1 frame path forces vertex alpha 1.0 (`common/twlhd_vertex_convert.c:44`) and has no polygon-alpha blend. The DeSmuME render arm is UNVERIFIED (no `desmume-cli` on the host, and `tools/emulator/desmume_capture.sh` requires an arm9v2 autodump file this ROM never writes). Real-hardware verdict is **NOT YET MEASURED**; the owner procedure is in the report. Report: `docs/reports/eq4h-alpha-convention-test.md`.

**Update (2026-11-23):** a built `desmume-cli` and the out-of-tree harness do exist, but `ds-alpha-test.nds` cannot be rendered: DeSmuME faults (`ARM OP_BKPT`) in the ROM's `iprintf` console path before its 3D frame.
A control with that call removed shows the translucent quads are depth-rejected (quad z `0x41000000` vs backdrop `0x40000000`); `expected.py` now also prints the minimum separation (`min=`, 0 for every alpha) and the suite is 30 tests.
The render arm stays UNVERIFIED — `docs/reports/eq4h-alpha-convention-test.md` §5 quotes both attempts.

**Update (2026-11-23):** both ROM-side blockers are fixed; built unmodified from the repo, `ds-alpha-test.nds` renders in DeSmuME (`harness_exit=0`, frame 1 captured) with the quads at z=+64 in front of the backdrops (post-transform z `0x3f000000` vs `0x40000000`).
`measure_alpha.py --native` gives overall verdict **A1** (`5bit_round` decides A1, `8bit_round` UNDECIDED; floor variants A2 sensitivity only); the suite is 32 tests. This is EMULATOR (DeSmuME) only — real hardware remains **NOT YET MEASURED**.
Full commands: report §5.

**Update (2026-11-23):** the repair-2 "overall **A1**" reading was a classifier-model artefact: the tool's 5-bit/8-bit families do not model DeSmuME's internal 6-bit BGR666 pipeline, and every quad happened to fall inside `TOL=8`. A DeSmuME-style `6bit_round` variant (5→6 `(v<<1)|(v>>4)`, A2 blend `((a+1)*S6+(31-a)*B6)>>5`, output `round(v6*255/63)`) added to `expected.py`/`measure_alpha.py` is the best fit on the same harness frame: `best_variant=6bit_round`, `best_convention=A2`, `best_total_dev=9` (L24/R16/R20 +1 per channel; every other quad exact), so the EMULATOR verdict is **A2**, not A1. The 6-bit model is derived from DeSmuME's source (`utils/colorspacehandler/colorspacehandler.cpp:98-134`, `rasterize.cpp:490-496`) and is a HYPOTHESIS for real hardware: real hardware remains **NOT YET MEASURED**.
Full corrected output: report §5.2; the suite is 43 tests.

**Result EQ-4i (2026-11-23):** **EQ-4i measured the polygon-alpha convention on real hardware (one New 3DS, DS mode, one capture session, one ROM): the green channel matches the DeSmuME 6-bit A2 expectation and never A1, and red/blue match the 5-bit quantisation of those 6-bit values; the tool's whole-RGB `verdict=A1` on the same pixels is a classifier-model artefact.** Fixture `tools/ds_alpha_test/fixtures/al1_3_top400x240.png` (sha256 `d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232`); the unmodified tool reports `verdict=A1` (`best_variant=5bit_round`, `best_total_dev=30`), while the additive per-channel analysis gives `g_votes_A1=0 g_votes_A2=10 g_votes_ambiguous=4 g_channel_verdict=A2` and `verdict_note=channels disagree; G channel=A2 but verdict=A1; verdict= is not decisive when channels disagree`; the `6bit_565` model (R,B = expand5(v6>>1), G = round(v6*255/63)) fits R and B with summed deviation 0 on all 14 quads. HYPOTHESIS: the 6-bit A2 blend is the hardware green path; UNVERIFIED: the origin of the R/B 5-bit quantisation (DS engine vs 3DS display path), other consoles/alpha values/textures/polygon-ID interactions. Report: `docs/reports/eq4i-alpha-hw-result.md`.

**Result EQ-4j (2026-11-23):** **EQ-4j measured the sub-pixel DS position of every edge of the known `quads.json` geometry on real hardware and in a DeSmuME native frame (one console, one capture session, one ROM): hardware-minus-DeSmuME is about `-1.07` DS px in y and `-0.09` in x.** New tool `tools/ds_alpha_test/measure_edges.py` (16 tests in `test_measure_edges.py`, wired into `make test`) measures every edge to sub-pixel precision (50 % crossing between locally measured plateaus, `BAND=5`, contrast threshold 10). FACT (quoted output only): under the committed mapping every quad edge is within about 0.1 DS px of nominal (left `-0.1069±0.0553`, right `-0.0879±0.0348`, top `-0.0734±0.0250`, bottom `-0.0772±0.0229`; width `+0.0179±0.0575`, height `-0.0038±0.0460`, n=13/13/14/14/12/14); DeSmuME is exactly nominal on left/right (`0.0000±0.0000`) and exactly `+1.0000±0.0000` on top/bottom, so hardware-minus-DeSmuME is `-0.11`/`-0.09` DS px in x and `-1.07` in y, and the y term survives the EQ-4g fit mapping (`-0.96`/`-1.04`). HYPOTHESIS: that ~1 px dy-negative offset is a real vertical placement difference between hardware and DeSmuME (hardware matching nominal, DeSmuME displaced), not a hardware shift, not the capture mapping and not resampling blur. UNVERIFIED: which side is displaced (no top/bottom screen-line edge, so a whole-frame +1 row shift is indistinguishable), separation of the mapping-relative residuals from an absolute hardware raster shift (the committed mapping was fitted against another ROM's nominal geometry), other consoles/sessions, the true resampler kernel, non-axis-aligned/textured geometry, and the PICA arm. Report: `docs/reports/eq4j-3d-edge-position.md`.

**D3 — Is pixel equality a goal at all?**
**Recommend: only for the "must be equal" rows of section 2.3.** Fidelity to the DS
rasteriser is a non-goal (`docs/PHASE5A_V2_RENDERER_DESIGN.md:41-42`), and fog/edge/AA/toon/
polygon-ID/display capture/translucent ordering are out of scope
(`docs/PHASE5B_PLAN.md:28-33`, `:745`). A whole-frame pixel equality target would be a
scope change, not a check.

**D4 — Approval cost per hardware run.**
**Recommend: one owner approval per run, with EQ-4, EQ-5 and EQ-6 bundled into one session
where possible.** The repo's rule is already "owner approval per run"
(`docs/PHASE5A_V2_RENDERER_DESIGN.md:67`, `docs/PHASE5B_PLAN.md:1017-1020`), and bundling
does not weaken it: each run is still individually authorised and logged.

**D5 — Try the DS-side framebuffer dump (EQ-6)?**
**Recommend: not as the first hardware experiment; do the host-only mapping check first, then
decide.** It is the only reference in this study that needs no model and no camera, and the
payoff is large, but the evidence is weaker than it first looks: only an LCDC/ABG/OBJ-mapped
bank is in the CPU page map (`MMU.cpp:443-458`, `:494-502`, `:504-511`), while a
texture-mapped bank reaches only `textureSlotAddr` (`MMU.cpp:555-558`) and a CPU read of it
resolves unmapped — already recorded as "DeSmuME's model says no" for a texture-mapped bank
(`docs/TEXTURE_CAPTURE_DESIGN.md:1153`). Which mapping our 3D framebuffer uses is UNVERIFIED
(GBATEK not consulted; `arm9v2/source/main.cpp:171` sets `MODE_0_3D`). This is a HYPOTHESIS
with a known model-level obstacle, not a promise; EQ-6 step 1 is a host-only source read that
costs nothing.

**D6 — Which stage first?**
**Recommend: EQ-2.** It is host-only, needs no console, catches matrix/stack/assembly
defects that would otherwise be seen only as a misplaced picture, and its tolerance is
already justified by the repository's measured matrix deviations
(`docs/EMULATOR_VALIDATION.md:28-34`). Its limit must be stated with the result: our
equations come from DeSmuME (`docs/DS_GX_RESEARCH.md:7-12`), so a pass proves the
implementation matches the model we wrote it from, not that the model matches the DS.

## 7. What could not be verified

- **`ntr_capture` in TWL/DS mode — resolved by trial (2026-11-19).** NTR-HR remote play is
  NOT reachable while a DS ROM runs in TWL/DS mode (one run, 12 attempts, no frame), so
  option (c) is not available as a numeric DS-mode capture (section 3.3(c);
  `docs/reports/eq4-ds-mode-capture.md`). Why the console leaves the network is UNVERIFIED.
- **Hardware LCDC-VRAM readback.** A bank mapped LCDC/ABG/OBJ is CPU-readable in the DeSmuME
  model; a bank mapped as texture is not (`MMU.cpp:443-458`, `:555-558`;
  `docs/TEXTURE_CAPTURE_DESIGN.md:1153`). Which mapping our 3D framebuffer uses, and what
  silicon does, are UNVERIFIED (section 3.3(f), EQ-6).
- **Hardware rasterisation rules**: quad fill, edge/scanline inclusion, w-vs-z depth mode,
  alpha scale, constant-vs-interpolated alpha. The DS side of each is UNVERIFIED; only the
  DeSmuME model is cited.
- **melonDS facts.** Not read in this session; option (e) is sourced from
  `docs/EMULATOR_TRACER_FEASIBILITY.md:63-72`, `:155` and labelled UNVERIFIED.
- **`hwtest-staging/RUN2_RESULTS.md` details (Runs 19-23).** Not used as a source; this
  document cites only what the repository docs record.
- **DeSmuME CLI behaviour beyond the source.** The binary was not run (forbidden by the
  task); every CLI statement is from `commandline.cpp` and `frontend/posix/cli/main.cpp`.
- **Our pipeline's exact DS depth mode and texture filter** for the demo frames. The code
  paths exist; the values the demo writes were not traced in this study.
- **Any equality result.** This study proposes checks; it reports none.

## 8. Reference index

Repository (TWL-HD, current `HEAD`):

- Scope, labels, non-goals: `docs/PHASE5B_PLAN.md:3`, `:7-8`, `:12-16`, `:28-33`, `:745`;
  `docs/PHASE5A_V2_RENDERER_DESIGN.md:11`, `:41-42`, `:48-49`, `:105`.
- Evidence baseline: `docs/EMULATOR_VALIDATION.md:11-14`, `:28-34`, `:53-54`, `:240-257`,
  `:774-788`; `docs/TEXTURE_CAPTURE_DESIGN.md:1076-1095`, `:1116-1145`, `:1148-1160`;
  `docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787`; `docs/PHASE5B_PLAN.md:98-100`, `:896-897`,
  `:901-918`.
- Tolerance method: `docs/reports/tex-formats-tolerance.md:90-140`.
- Cull/front-face hardware settlement: `docs/reports/5c4-black-screen-analysis.md:442-476`.
- EQ-4 DS-mode capture trial (2026-11-19): `docs/reports/eq4-ds-mode-capture.md`.
- Licensing and tracer precedent: `docs/EMULATOR_TRACER_FEASIBILITY.md:50`, `:58`,
  `:112-145`, `:151-155`.
- Console policy: `tools/ntr_capture/README.md:235-251`.
- Comparison targets: `common/twlhd_gx_interp.h:22-33`; `common/twlhd_gx_interp.c:387-397`,
  `:636-655`, `:684-703`, `:1075-1077`; `common/twlhd_clipfix.h:11-19`.
- Renderer state: `arm11/source/renderer.c:1454-1456`, `:1527-1528`, `:1532-1537`;
  `tests/host/test_blend_config.c:77-80`.
- ROM video mode / bank remap: `arm9v2/source/main.cpp:171`;
  `arm9v2/source/demo_frame.cpp:176-188`.

DeSmuME (`$HOME/3ds/desmume/desmume/src`, revision `fc504ee0`):

- GPL header: `gfx3d.cpp:1-8`.
- Command funnel: `gfx3d.cpp:117`, `:158`, `:175`; `commandline.cpp:586-596`.
- Geometry: `gfx3d.cpp:1354`, `:1416`, `:1573`, `:1661-1676`, `:1682-1715`, `:1726-1735`,
  `:2681`, `:3260-3270`, `:3287-3311`, `:3321-3324`, `:3349-3363`, `:3517-3525`;
  `gfx3d.h:581-588`, `:603-610`, `:642-653`, `:766`.
- Rasterisation: `rasterize.cpp:24-26`, `:490-494`, `:499`, `:617-637`, `:780`, `:831`,
  `:846`, `:948-968`, `:987-989`, `:1190-1202`, `:2139`, `:2295-2316`, `:2388-2391`.
- Textures: `texcache.cpp:412-499` (width/height `:424-425`, sizes `:415`, `:429`, key colour
  `:431-438`, palette `:440`).
- Display capture: `GPU.cpp:3330`.
- Frontends: `commandline.cpp:49-140`, `:260-340`, `:322`, `:388`; `frontend/posix/cli/main.cpp:281-297`,
  `:331`, `:380`; `frontend/posix/gtk/main.cpp:2540-2597`, `:4110`.
- Harness API: `NDSSystem.h:478`; `GPU.h:1179`, `:1889`; `MMU.cpp:443-458`, `:494-502`,
  `:504-511`, `:555-558`.

**Update (2026-11-23):** repair 4 trimmed duplicated report text (no code or test change) and relabelled the §5.2/README §11 output-stage sentence HYPOTHESIS/UNVERIFIED; the total diff vs `main` is now below the 117,760-byte limit (report §10).
