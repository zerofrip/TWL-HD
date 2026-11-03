# Phase 5A — v2 capture → PICA200 renderer (design)

Status: **DESIGN, approved to start (owner, 2026-11-01).** Scope fixed by the owner: offline rendering of v2 captures from **our own homebrew ROMs**
on a real New 3DS. **Out of scope:** commercial-game GPU interception, real-time DS rendering, RTCom changes. Labels: BUILD / UNIT / EMULATOR / HARDWARE
are kept separate; nothing here is HARDWARE VERIFIED, though the 5A-2 clip path is HARDWARE OBSERVED for one frame (Run 7) and for a non-default DS viewport on one frame (Run 8). This document makes decisions; items marked **CHECK** must be verified against a source
(GBATEK, libnds, citro3d) during implementation before they are relied on.

## 1. Goal and non-goals
Goal: replace the fixed debug camera with the **captured DS transforms**, so the demo cube renders on the 3DS the way the DS geometry engine would place it
(projection × position matrix, DS viewport, DS vertex colours), at the 3DS's native resolution.
Non-goals for 5A: lighting (normals are captured but not used), textures (`TXDS/TXDT/PLDT` still reserved), alpha/blending/fog/edge/toon, polygon-ID
effects, anti-aliasing, display-list DMA, clip-plane exactness beyond what the PICA200 does, 3D-engine timing.

## 2. What the data supports (SOURCE-READ in this repository)
The real DeSmuME capture of `arm9v2` (`tests/host/build/emu/capture/twlhd_frame_v2.bin`, 124 GXCM entries) contains: `MTX_MODE`(0x10), `MTX_PUSH`(0x11),
`MTX_POP`(0x12), `MTX_IDENTITY`(0x15), `MTX_MULT_4x4`(0x18, perspective), `MTX_MULT_4x3`(0x19, look-at), `MTX_MULT_3x3`(0x1A ×2, rotations),
`MTX_SCALE`(0x1B), `MTX_TRANS`(0x1C), `COLOR`(0x20), `NORMAL`(0x21), `VTX_16`(0x23 ×48), `POLYGON_ATTR`(0x29), `BEGIN_VTXS`(0x40, quads),
`END_VTXS`, `SWAP_BUFFERS`(0x50) and `VIEWPORT`(0x60). So 5A has everything it needs for transform + geometry + colour + viewport. The v1 format cannot do this
(no matrices); v1 and the debug-camera view stay as they are.

## 3. Architecture decision: interpret on the CPU, pass clip-space vertices to the GPU
A pure C module `common/twlhd_gx_interp.{h,c}` (allocation-free, host-tested) **emulates the DS matrix stacks and vertex path** and outputs triangles whose
vertices are already in **DS clip space** `(x, y, z, w)` plus RGBA. The ARM11 draws them with a passthrough-style vertex shader and **one uniform
`clipfix`** (a 4×4) that converts DS clip space to the PICA200 convention.
Why: (1) matrix-stack semantics (push/pop/store/restore, modes, the projection×position product) are DS-specific and must follow the captured command order,
not GPU state; (2) it is unit-testable on the host with exact expectations, which the shader path is not; (3) the hardware lesson of Phases 4B/4C is that
GPU-side surprises are expensive to find (see `PHASE4_HARDWARE_FINDINGS.md`), so keeping the GPU path minimal reduces risk.
Rejected alternative: upload DS matrices as uniforms and transform on the GPU — fewer CPU cycles but moves DS semantics into shader code.

## 4. The DS side (what the interpreter must reproduce) — all **CHECK** against GBATEK/libnds/DeSmuME before relying on them
- Matrices: 4×4, signed 32-bit, 20.12 fixed point (raw words in the capture are `value × 4096`). `MTX_MODE`: 0 projection, 1 position, 2 position+vector
  (modelview), 3 texture. Stacks: projection depth 1, position/vector stack depth 31 (CHECK pointer/overflow rules); `MTX_POP n` pops n; `MTX_STORE/RESTORE n`.
- Multiplication: `MTX_MULT_4x4/4x3/3x3`, `MTX_SCALE`, `MTX_TRANS` multiply the **current matrix on the right** by the operand; the 4×3/3×3 forms imply the
  missing row/column of the identity; 64-bit intermediate with a right shift of 12 (CHECK rounding/truncation and word order — the capture's column-major write
  order is pinned by `tools/emulator/compare_capture.py`).
- Vertex path: `clip = projection × position × vertex` (the DS keeps a combined clip matrix, recomputed when either changes); vertices from `VTX_16` (1.3.12),
  `VTX_10` (1.3.6), `VTX_XY/XZ/YZ` (reuse the missing component), `VTX_DIFF` (0x28, if ever captured). Current colour (RGB555) latches into each vertex.
- Primitives: `BEGIN_VTXS` mode 0 triangles, 1 quads, 2 triangle strip, 3 quad strip (CHECK vertex order for quad strips). Quads become two triangles.
- Viewport: `VIEWPORT` word = x1, y1, x2, y2 (bytes), DS screen 256×192, **y measured from the bottom** (CHECK).
- Clipping, W-buffering, polygon sorting, and the 2048-polygon / 6144-vertex limits are **not** modelled; the PICA200 clips; over-limit frames are reported, not truncated silently.
Numeric choice: matrices are kept as the DS keeps them (`int32` 20.12) so the stack semantics and test vectors match the capture bit for bit; the vertex
transform is then done in `double`/`float`, which is acceptable for display (the DS rounds differently; fidelity to the DS rasteriser is a non-goal).

## 5. The PICA200 side (**CHECK** against citro3d source, then verify on hardware)
- Depth: DS clip z ∈ [−w, w]; citro3d projections target z ∈ [−w, 0]. `clipfix` maps `z' = (z − w) / 2` (so −w→−w, +w→0).
- Screen: the top screen is a 240×400 target presented rotated; citro3d's `Mtx_PerspTilt` bakes that rotation into the projection. **UNVERIFIED CLAIM — verify on EMULATOR/HARDWARE:** `clipfix` is believed to apply the citro3d quarter-turn (read `Mtx_PerspTilt` in the citro3d source and
  reproduce it; do not guess its sign).
- Viewport policy (decision): the DS 256×192 viewport is mapped to a **centred 4:3 region of the 400×240 top screen (320×240, 40 px pillar bars)** — aspect
  preserved, native 3DS resolution. Implemented inside `clipfix` (scale/offset in clip space, using the captured `VIEWPORT`), not with PICA scissor state,
  to keep the GPU path to one uniform.
- Vertex layout: reuse the hardware-proven layout (2 loaders float3+float4) with **padding declared** (`twlhd_attr_pad`); the position slot now carries
  `(x, y, z)` and `w` travels in a **third float** → needs a layout of position float4 + colour float4 (32 bytes, no gap, so no padding problem). **Do not**
  introduce any layout whose declared bytes differ from the stride (Phase 4 root cause).
- Culling: off (as in the debug view). `POLYGON_ATTR` cull bits are recorded but not applied in 5A.
- Depth test: `GPU_GREATER`/`GPU_GEQUAL` convention must be re-derived for the new z mapping (CHECK; a wrong choice shows as an empty or inverted cube).

## 6. App integration
`arm11/source/main.c` already loads `sdmc:/twlhd_frame.bin` (v1, 73,740 bytes). 5A adds: detect the version word (v1 = 1, v2 = 2) and the file size, dispatch to
the existing v1 path or the new v2 path; the v1 debug-camera behaviour is unchanged; the bottom screen reports `v2: <entries> GXCM, <triangles> tris, viewport …`.
Errors (bad parse, interpreter limit exceeded) are shown, never silently ignored.

## 7. Phases and exit criteria
| Phase | Work | Verification | Exit |
|---|---|---|---|
| 5A-1 | `twlhd_gx_interp` + host tests (stack semantics, each matrix command, vertex forms, primitives, error paths) | UNIT; golden: the captured/generated demo frame → expected clip coordinates computed independently (Python/double) and cross-checked with an independent review | `make test` green, mutation checks, independent review PASS |
| 5A-2 | ARM11 v2 path: loader dispatch, clip-space shader + `clipfix`, 32-byte vertex layout | BUILD; EMULATOR (Tanuki3DS: pixel sanity — bounding box inside the 4:3 region, expected face colours); *the emulator ignored the Phase 4 padding bug, so this is not hardware evidence* | builds clean; emulator shows a plausible cube; review PASS |
| 5A-3 | Hardware run procedure (new owner approval required) | HARDWARE | **Met for one frame (Run 7, HARDWARE OBSERVED) and for one non-default DS viewport (Run 8, HARDWARE OBSERVED):** the cube rendered from a 36-vertex v2 frame with no GPU hang, and a (32,96)-(159,191) viewport placed it as the oracle/emulator predict (photo estimate); see `HARDWARE_TEST_PROCEDURE_3DS.md` section 12. Not covered: stability, long runs, other captures, large frames, the DS-side viewport +1/y1-wrap (not testable on the 3DS), viewports with y1 > 191 |
| 5B (later, not approved) | normals/lighting, POLYGON_ATTR (cull/alpha), textures (needs `TXDS/TXDT/PLDT`), animation sequences | – | separate approval |
Every GPU-touching change keeps the diagnostic habits that worked: explicit trace markers and a non-blocking completion probe in a diag build, and a
documented recovery procedure. No RTCom gate is touched; the DS-side RTCom ucode gate stays 0.

## 8. Risks
1. DS arithmetic details (stack overflow/underflow, 20.12 truncation, quad-strip order) — mitigated by the **CHECK** list and by comparing against the real
   capture; the existing analytic checks in the emulator comparator already pin the matrix write orders.
2. PICA200 depth/orientation conventions — mitigated by deriving from citro3d source and by the emulator pixel sanity, but only hardware settles them.
3. Unknown hardware behaviour of the new vertex layout — mitigated by using only layouts whose declared bytes equal the stride.
4. Scope creep toward lighting/textures — explicitly deferred (5B).

## 9. 5A-2 status

| Component | Status |
|---|---|
| `common/twlhd_gx_interp.c` — DS GX interpreter | BUILD ✓, UNIT ✓ (make test green) |
| `common/twlhd_clipfix.c` — DS→PICA clip-space matrix | BUILD ✓, UNIT ✓ (test_clipfix: 20 checks) |
| `arm11/source/renderer.c` — clip-space shader + render path | BUILD ✓ only (no unit tests; GPU path cannot be unit-tested on the host) |
| `arm11/source/main.c` — v2 loader + status display | BUILD ✓ |
| Emulator (Tanuki3DS pixel sanity) | EMULATOR ✓ (not hardware evidence) |
| Hardware | HARDWARE OBSERVED (Runs 7 and 8): clip path ran without a GPU hang on the demo cube, and a non-default DS viewport (32,96)-(159,191) placed the cube as predicted (photo estimate); see HARDWARE_TEST_PROCEDURE_3DS.md section 12 |

The 5A-2 build was run in Tanuki3DS with the v2 capture loaded from `sdmc:/twlhd_frame.bin`; its 800x600 screen dump was inspected and the cube's bounding box measured at 311-490 × 53-233 versus 308-492 × 51-235 expected — within 3 px — with the expected face colours. The run is EMULATOR evidence only: it shows the capture, clip and draw path produces a plausible image and says nothing about GPU hangs, vertex/polygon RAM pressure or timing on real hardware.

**39-column rule** (bottom-screen console is 40 columns wide): every `printf` in `update_console()` is either a literal string ≤39 chars, or routed through `status_line()` which truncates to 39 chars, or uses `clamp_max()`/`clamp_4digit()` helpers that enforce a numeric ceiling before formatting. Worst-case column counts are documented in the `update_console()` header comment and were re-counted by an independent review (not machine-checked).

**Known risks still unverified (EMULATOR/HARDWARE):**
- **HARDWARE OBSERVED for one frame (Run 7):** `clipfix` applies the DS→PICA200 depth remap and the citro3d quarter-turn; the cube's screen orientation matched the emulator and the independent oracle on one 36-vertex v2 frame. Other frames unverified.
- **HARDWARE OBSERVED for one frame (Run 7):** no mirroring — the cube's face layout was not mirrored on that frame.
- **HARDWARE OBSERVED for one frame (Run 7):** pillarbox centring — the cube sat horizontally centred (vertical centre slightly above screen middle) on that frame; its ~142×147 px size is a photo estimate.
- **HARDWARE OBSERVED for one frame (Run 8):** the `clipfix` viewport mapping (scale 0.5 plus offset) for a non-default DS viewport (32,96)-(159,191) placed the cube where the oracle and emulator predict, within a few px of a photo estimate. One viewport only, and the figure comes from a photo, not a precise measurement.
- **UNVERIFIED:** depth order — `GPU_GREATER`/`GPU_GEQUAL` choice for the remapped z on far faces.
- **UNVERIFIED:** GBATEK notes DS viewport y1 is measured from the bottom; the interpreter accounts for it, but the **DS-side** +1 size / y1-wrap behaviour (and viewports with y1 > 191) remains unverified — it cannot be exercised on the 3DS. The 3DS-side mapping for a non-default viewport is now HARDWARE OBSERVED (Run 8).

Run 7 (HARDWARE OBSERVED, one 36-vertex v2 frame) covers orientation, no mirroring and pillarbox centring; Run 8 (HARDWARE OBSERVED, one non-default DS viewport) covers the viewport scale/offset mapping. The depth order of far faces and the DS-side viewport y1 wrap/+1 behaviour remain EMULATOR/unverified, and nothing beyond this one demo cube has been run on hardware.

Labels: BUILD = compiles clean. UNIT = host tests pass. EMULATOR = Tanuki3DS pixel sanity (not hardware evidence). HARDWARE = run on real New 3DS; two runs done in 5A (Runs 7 and 8, HARDWARE OBSERVED, one 36-vertex v2 frame each, Run 8 on a non-default DS viewport). Everything else remains unverified: the DS-side viewport +1/y1-wrap and viewports with y1 > 191, captures other than this demo cube, large frames on the clip path, many consecutive frames, long-run stability, and textures/lighting/alpha (5B, not approved).

## 10. Sequence playback (BUILD/UNIT only)

The ARM11 debug renderer can cycle through a folder of v2 captures. Press **X** to toggle sequence mode; **A** loads `sdmc:/twlhd_frame.bin` as before and disables sequencing; **Y** restores the built-in triangle and also disables sequencing; **START** exits. Sequence files use the layout `sdmc:/twlhd_seq/seq_NNN.bin` where `NNN` is a three-digit, zero-padded decimal index starting at `000`. Up to `64` frames are supported (`000`–`063`); playback wraps to `000` after the last present file or as soon as a file is missing, and a loop counter is shown on the bottom screen. The first frame loads in the same main-loop iteration as the X press: on enable, `g_seq_counter` is set to `SEQ_PERIOD - 1`, so it reaches `SEQ_PERIOD` after one increment and the load fires immediately. This sub-section is **BUILD/UNIT only**: it compiles cleanly and is covered by the host `test_seq` harness, but it has not been run on an emulator or on hardware.
