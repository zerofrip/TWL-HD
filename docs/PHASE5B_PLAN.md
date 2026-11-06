# Phase 5B — lighting, POLYGON_ATTR (alpha/cull), textures (PLAN ONLY)

STATUS: PLAN — stage order approved by the owner (2026-11-06); NO stage is approved for implementation yet; each stage, each format extension, each stride change and each hardware run still needs its own approval.
Equations written here are placeholders marked **TO BE
VERIFIED**; they are not facts.

Scope (fixed by the owner, unchanged from Phase 5A): offline rendering of
captures from **our own homebrew ROMs** on a real New 3DS. Out of scope:
commercial-game GPU interception, real-time DS rendering, RTCom changes, and any
hardware execution by the assistant. Labels used throughout: BUILD / UNIT /
EMULATOR / HARDWARE.

Companion: `docs/PHASE5A_V2_RENDERER_DESIGN.md` (5B row; non-goals line 11).
Format spec: `docs/TWLHD_V2_FORMAT.md`, `common/twlhd_v2_format.h`.

## 1. Goals and non-goals

Goal: extend the Phase 5A clip-space path so a captured homebrew frame can show
(5B-0) per-polygon alpha and cull mode, (5B-1) DS lighting computed on the CPU
in the interpreter, and (5B-2) textured polygons, while keeping the 5A
stride-safety and diagnostic habits.

Non-goals (unchanged from 5A, `docs/PHASE5A_V2_RENDERER_DESIGN.md` line 11):
lighting was a 5A non-goal and becomes in-scope only via 5B-1; textures
(`TXDS/TXDT/PLDT`) stay reserved unless 5B-2 is approved; alpha/blending/fog/
edge/toon, polygon-ID effects, anti-aliasing, display-list DMA, clip-plane
exactness and 3D-engine timing remain out of scope unless a later plan adds
them. No RTCom changes in any stage.

## 2. Verified repo facts (cited)

- v2 GXCM stream already records COLOR, NORMAL, TEX_COORD, POLYGON_ATTR,
  TEXIMAGE_PARAM, PLTT_BASE, DIF_AMB, SPE_EMI, LIGHT_VECTOR, LIGHT_COLOR,
  SHININESS as command entries (`common/twlhd_v2_format.h` lines 88-111;
  ids 0x20..0x34).
- Interpreter output is `TwlhdGxVertex` = 32 B: `clip[4] + rgba[4]`, alpha
  always 1 (`common/twlhd_gx_interp.h` lines 22-26). Commands with no visible
  effect are counted in `ignored_no_effect`
  (`common/twlhd_gx_interp.c` lines 583-597).
- Texture DATA is NOT in the capture: VRAM contents (texture image, palette;
  `TXDS/TXDT/PLDT`) are reserved/not captured (5A design non-goals,
  `docs/TWLHD_V2_FORMAT.md` section 4). The demo ROM's own textures would need
  a capture-format extension or a side file.
- PICA200 stride rule (Run 5): declared bytes == stride; the clip vertex is
  32 B. Adding texcoords/normals changes the vertex layout and re-opens that
  risk on hardware (`docs/HARDWARE_TEST_PROCEDURE_3DS.md` sections 9/12).
- Hardware evidence so far: Runs 7-11 (36 to 6120 vertices, a 5-minute
  sequence, no GPU hang). Emulator (Tanuki3DS) does not model GPU hangs
  (`docs/PHASE5A_V2_RENDERER_DESIGN.md` lines 67, 107).
- Unrelated open issues: unexplained DeSmuME fault at AUTODUMP_FRAME=120 for
  some CUBES values; ROM-side v2 parse returned ERR_GXCM_GROUP once under gdb
  (`docs/EMULATOR_VALIDATION.md`).

## 3. Proposed stages (ordered by risk)

### Stage 5B-0 — POLYGON_ATTR alpha (0..31) and cull mode, no new vertex data

What changes:
- Interpreter stops counting POLYGON_ATTR as `ignored_no_effect`; latches its
  alpha (0..31) into the current polygon/vertex alpha instead of forcing 1.0.
  Alpha already travels in `rgba[3]`, so `TwlhdGxVertex` stays 32 B.
- Interpreter latches cull mode from POLYGON_ATTR; renderer's clip path sets
  `C3D_CullFace` accordingly instead of the hard-coded `GPU_CULL_NONE`
  (`arm11/source/renderer.c` line 999). Blend state for alpha may need enabling.
- A small state carrier (new struct or fields) is needed to move cull/alpha
  from interpreter to renderer; today only vertices + viewport cross that
  boundary (`renderer_set_clip_scene`).

Data/format changes: none. POLYGON_ATTR is already captured (cmd 0x29).

Test plan:
- BUILD: clean compile of common/ + arm11; static assert 32 B unchanged.
- UNIT: extend `tests/host/test_gx_interp.c` with streams carrying POLYGON_ATTR
  and assert `rgba[3] == alpha/31` (or agreed scale) and cull state; host
  oracle recomputed independently. Mutation-tested.
- EMULATOR: Tanuki3DS pixel sanity — face visibility under cull, translucent
  polygon appearance. (Emulator does not model GPU hangs.)
- HARDWARE: new run procedure draft needed (owner approval) — alpha/cull on the
  demo cube; no stride change, so hang risk is low but state change is new.

Exit criteria: `make test` green; emulator plausible; (if approved) hardware
run observed with no hang and expected face visibility/alpha.

Risks: cull-bit and winding semantics unknown until GBATEK/DeSmuME read;
alpha blend interaction with depth (`GPU_GREATER`) and clear; PICA blend enable
syntax. Mitigation: keep cull off by default until bits verified.

Rollback: revert POLYGON_ATTR to `ignored_no_effect`; renderer cull none,
blend off; alpha forced to 1.0.

### Stage 5B-1 — lighting on the CPU in the interpreter (colour out, 32 B stride unchanged)

What changes:
- Interpreter latches DIF_AMB, SPE_EMI, LIGHT_VECTOR, LIGHT_COLOR, SHININESS
  (currently all `ignored_no_effect`) and NORMAL (currently ignored).
- Per-vertex lit colour is computed in the interpreter using the latched
  normal and lights; output replaces flat `rgba` with lit colour. Alpha from
  5B-0 is preserved. `TwlhdGxVertex` stays 32 B; shader unchanged (passes
  colour through).
- Normal handling: DS latches NORMAL into vertices like COLOR; transform of the
  normal by the current matrix is **TO BE VERIFIED** (see section 4).

Data/format changes: none. All inputs already captured (cmds 0x21, 0x30..0x34).

Test plan:
- BUILD: clean compile.
- UNIT: extend `test_gx_interp.c` with lit-cube streams; expected colours from an
  independent Python/double oracle using only equations marked verified.
  Mutation-tested; performance sanity (large frames up to 6120 vertices).
- EMULATOR: Tanuki3DS pixel sanity — shading gradients, no colour explosion.
- HARDWARE: new run procedure draft needed (owner approval); stride unchanged,
  so no new hang class expected, but visual verification required.

Exit criteria: `make test` green; emulator plausible; (if approved) hardware
run observed.

Risks: DS lighting equation details (attenuation, specular power, clamping,
normal transform, number of lights) are not yet verified; CPU cost per vertex
on large frames; colour saturation.

Rollback: revert lit-colour computation; flat vertex colour restored; lighting
commands return to `ignored_no_effect`.

### Stage 5B-2 — textures (capture-format extension, new vertex attribute, PICA upload)

What changes:
- Capture format: implement the reserved chunks `TXDS`/`TXDT`/`PLDT` (or a
  documented side file) so texture image + palette data travel with the capture.
  This is a format change: parser, writer, recorder, tests, and Python tools.
- Interpreter: stop ignoring TEX_COORD (cmd 0x22); latch into vertices. This
  requires a vertex layout change unless texcoords go in a separate attribute
  buffer (decision point 3).
- Renderer: PICA texture object creation/upload, sampler/bind, TexEnv sampling
  (replace or modulate), format conversion. RGB and paletted formats first.

Data/format changes: yes — this stage cannot proceed without a format decision
and owner approval. Vertex layout change is likely (stride != 32 B) and
re-opens the Run 5 stride risk on hardware.

Test plan:
- BUILD: clean compile; static asserts updated for the new vertex size.
- UNIT: format parser/writer tests for the new chunks; interp texcoord tests;
  host oracle cross-check.
- EMULATOR: Tanuki3DS pixel sanity — textured cube (may not exercise upload
  paths fully).
- HARDWARE: new run procedure draft needed (owner approval) — highest risk stage
  due to stride change; must use the diagnostic habits (trace markers,
  non-blocking completion probe, documented recovery).

Exit criteria: `make test` green; emulator plausible; (if approved) hardware run
observed with no hang and correct texturing.

Risks: format extension complexity; stride mismatch hang; texture memory and
tiling/swizzle differences between DS and PICA; palette formats; unverified DS
texture parameters.

Rollback: disable texture path and texcoord attribute; revert to 32 B stride and
5B-1 state. Keep format chunks optional/unknown if already shipped.

## 4. Where the DS GX equations must come from

All DS-side equations are **TO BE VERIFIED** before use. Sources to read, in
this order:

1. GBATEK (geometry engine + 3D rendering engine sections): POLYGON_ATTR
   bitfields, COLOR/NORMAL/TEX_COORD semantics, DIF_AMB, SPE_EMI, LIGHT_VECTOR,
   LIGHT_COLOR, SHININESS, TEXIMAGE_PARAM, PLTT_BASE, texture formats.
2. DeSmuME source (the repo's emulator of record): implementation of the above
   commands and the lighting/texture units; used to cross-check GBATEK.
3. libnds headers/source for naming and any helper constants.

PICA-side facts come from citro3d source (texture upload, TexEnv, attribute/
buffer rules) and the existing 5A hardware runs.

The plan deliberately does NOT state DS lighting or texture formulas. Any
implementation PR must cite the GBATEK/DeSmuME location for each equation and
mark unverified items with **CHECK** until hardware confirms them, exactly as
5A did for matrices.

## 5. Decision points for the owner (yes/no)

1. Approve stage order 5B-0 -> 5B-1 -> 5B-2? (Recommend: yes.)
   Answered 2026-11-06: yes — stage order 5B-0 -> 5B-1 -> 5B-2 approved.
2. Extend the v2 capture format with TXDS/TXDT/PLDT, or use a side file for
   texture/palette data? (Recommend: extend format, but only with owner approval.)
   Answered 2026-11-06: extend the v2 format with TXDS/TXDT/PLDT (stage-5B-2 implementation still needs its own approval).
3. Accept a vertex stride/layout change for textures, or keep 32 B and use a
   separate texcoord attribute buffer? (Recommend: decide after 5B-1 results.)
   Answered 2026-11-06: open — decide after 5B-1 results.
4. Are test-ROM scenes required for each stage (alpha/cull cube, lit cube,
   textured cube), and who produces the captures? (Recommend: yes; owner/ROM
   author runs the demo with AUTODUMP.)
   Answered 2026-11-06: yes, required; the worker (supervisor pipeline) creates them from our own homebrew demo ROM (alpha/cull cube, lit cube, textured cube), captures via AUTODUMP; hardware is not touched.
5. Proceed with 5B-0/5B-1 only on CPU-side colour output before any GPU-state
   change lands? (Recommend: yes, split alpha latch from cull state if needed.)
   Answered 2026-11-06: yes.
6. Keep cull disabled by default until GBATEK cull bits are verified? (Recommend: yes.)
   Answered 2026-11-06: yes.

## 6. What requires a new owner approval before any code

- Implementation of each stage (5B-0, 5B-1, 5B-2), one approval per stage.
- Any capture-format extension, including TXDS/TXDT/PLDT payload definitions.
- Any vertex stride or layout change, and any new hardware run that exercises it.
- Each new HARDWARE run and its procedure (one approval per run/build hash).
- Any new test-ROM scene generation or AUTODUMP change.
- Any change touching RTCom (none planned; would need its own approval).
- Any deviation from the scope in section 1.

## 7. Labels

BUILD = compiles clean. UNIT = host tests pass. EMULATOR = Tanuki3DS pixel
sanity (not hardware evidence). HARDWARE = run on real New 3DS (owner-run,
assistant never executes hardware). Nothing in 5B is currently verified at any
label.

## 8. Next steps (proposal, needs owner approval to start)

a) Stage 5B-0 implementation task: interpreter alpha/cull latch + UNIT tests + test-ROM scene generation via the pipeline.
b) GBATEK/DeSmuME verification of POLYGON_ATTR bit layout as a research task before any equation is used.