# Phase 5B — lighting, POLYGON_ATTR (alpha/cull), textures (PLAN ONLY)

CORRECTION (Run 17, 2026-11-08): numeric NTR captures show the real console DOES alpha-blend (mode 0 blue/green quads match a standard blend within ~2 levels; mode 2 matches a 50 % blend; alpha 0 = blend weight 0, not discarded). Every statement below that the console "does not blend", that alpha 16/4 quads are "opaque", or that the cause is "UNKNOWN" (Runs 14-16) is superseded; it was a misreading of camera photos. See `docs/HARDWARE_TEST_PROCEDURE_3DS.md` section "Run 17".

STATUS: 5B-0b implemented (BUILD/UNIT/EMULATOR) as of 2026-11-07 (owner approval after Run 14); HARDWARE observation (Run 15) shows NO change of the console picture: alpha 16/4 quads still opaque, alpha-0 quad still not drawn; cause UNKNOWN (candidates in Run 15). 5B-0c diagnostic build implemented (BUILD/UNIT) as of 2026-11-07; HARDWARE OBSERVED (Run 16, 2026-11-07) confirms the intended binary ran (build id `e6e6c88-diag`), the vertex alpha reaches the fragment stage distinctly and monotonically (mode 1), and with blending ON plus constant fragment alpha 0.5 the quads are NOT visibly mixed with the background and an alpha-0 quad disappears in mode 0 while the same quad is drawn at alpha 0.5 (mode 2) — i.e. the console blend does not behave like alpha blending with this state; cause UNKNOWN (candidates: colour-buffer read/blend path ineffective with this render target; effect state not what the C3D calls intend; an alpha-0 discard elsewhere). Emulators (Tanuki3DS, Azahar) both blend and are NOT predictors of console blend behaviour. Next step proposed (needs approval): in-console pixel probe reading the top framebuffer at the quad centres. No further 5B stage started.
5B-0a (CPU alpha) is implemented as **BUILD/UNIT** plus an **EMULATOR** observation and a **HARDWARE** observation (Run 14, see §8 and `docs/HARDWARE_TEST_PROCEDURE_3DS.md` section 12): alpha-1.0 frames are unchanged on hardware; alpha<1 frames do NOT blend on the console with the default blend state. The ARM11 binary changes (shared interpreter) but behaves identically for alpha-1.0 frames.
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

**STATUS (2026-11-08): implemented BUILD/UNIT, HARDWARE UNVERIFIED.**
Commands NORMAL, DIF_AMB, SPE_EMI, LIGHT_VECTOR, LIGHT_COLOR and SHININESS are
latched in `common/twlhd_gx_interp.c`; NORMAL computes the lit colour with
DeSmuME's integer equations (source-read, not hardware-measured). Executed
this round: host unit tests and golden streams against an independent Python
oracle. Not done: emulator visual check, hardware run.

Still CHECK (do not claim DS accuracy):
- GBATEK cross-check of the specular exponent/rounding (DeSmuME comments that
  GBATEK is slightly off).
- NORMAL sent outside BEGIN..END: this implementation applies lighting (the
  colour latch is global in DeSmuME); real hardware behaviour is unverified.
- Initial material colours, the half-vector zero-length branch and the
  shininess word-counter reset are implemented from DeSmuME source only;
  hardware was not checked.

What changes:
- Interpreter latches DIF_AMB, SPE_EMI, LIGHT_VECTOR, LIGHT_COLOR, SHININESS
  (previously all `ignored_no_effect`) and NORMAL (previously ignored).
- Per-vertex lit colour is computed in the interpreter using the latched
  normal and lights; output replaces flat `rgba` with lit colour. Alpha from
  5B-0 is preserved. `TwlhdGxVertex` stays 32 B; shader unchanged (passes
  colour through).
- Normal handling: NORMAL is transformed at command time by the vector matrix
  3x3 (DeSmuME gfx3d.cpp:1233-1235) and runs the lighting equation; the result
  replaces the flat latched colour. Alpha is untouched.

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

#### Status 5B-2a — capture-format chunks (`TXDS`/`TXDT`/`PLDT`)

Implemented, **BUILD/UNIT only**, nothing EMULATOR or HARDWARE verified:

- Parser: the three chunks are known and validated (`ERR_TXDS`,
  `ERR_TXDS_KEY`, `ERR_TXDS_SIZE`, `ERR_TXDS_BOUNDS`, `ERR_TXDT`, `ERR_PLDT`),
  with read-only accessors on `TwlhdV2View`.
- Writer: deterministic emission in the fixed order GXCM, META, RSTA, TXDS,
  TXDT, PLDT; a file without textures is byte-identical to the 5A golden.
- Layout: `docs/TWLHD_V2_FORMAT.md` section 7.1, marked owner-reviewable
  proposal.
- Not done: recorder-side texture capture, the Python mirror extension,
  interpreter texcoords, vertex-layout change and PICA upload.

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
c) Results of the DS POLYGON_ATTR/primitive-assembly research: see docs/DS_GX_RESEARCH.md.
d) (proposed, needs approval) In-console pixel probe: read the top framebuffer at
   the quad centres after rendering and print the RGB values on the bottom
   screen, to remove the camera/ambient-light limit of the Run 16 photo
   evidence. See the proposal at the end of this file.

### 5B-0a (CPU alpha) — BUILD/UNIT + EMULATOR observation

Status: interpreter-side alpha only. POLYGON_ATTR (cmd `0x29`) now latches bits
16..20 into a pending raw alpha and applies it at the next BEGIN_VTXS, matching
DeSmuME's latch (`gfx3d.cpp:2681`, `2729-2731`). The raw value is converted by one
named function; `alpha 0 -> 0.0` and `1..31 -> (alpha + 1) / 32.0f`. **TO BE
VERIFIED:** the `(alpha + 1) / 32` blend weight comes from DeSmuME
(`rasterize.cpp:490-494`); hardware/GBATEK were not consulted and `/31` is the
alternative for non-zero alphas.

`TwlhdGxStats` gained `alpha_translucent_polys` (applied raw alpha 1..30) and
`alpha_wireframe_polys` (applied raw alpha 0), one increment per BEGIN group;
alpha 31 and the no-attr default never increment.

NOT implemented here:
- no explicit blending state or GPU change in our code (arm11/source/renderer.c sets cull/depth/TexEnv, not blending). EMULATOR (Tanuki3DS): the default (citro3d) blend state already alpha-blends vertex alpha, so alpha<1 frames differ (scene4 capture 1128 B, sha256 prefix 2decd74dead1044f; old vs new ARM11 build differ 15220 of 1440015 bytes; blue (0,0,240)->(0,0,48), green (0,240,0)->(0,128,0), white quad gone). alpha=1.0 frames (Runs 7-13) are unchanged; only the stats line differs (ignored_no_effect one lower, POLYGON_ATTR now processed). HARDWARE OBSERVED (Run 14): on the real console the alpha 16/31 and 4/31 quads were OPAQUE (no blending) and the alpha-0 quad was not drawn — i.e. the "default blend state already alpha-blends" claim is true only for the EMULATOR, not the console. Consequently 5B-0b must set the blend state explicitly (not rely on the default), the alpha-0 / wireframe handling must be decided, and a further hardware run (its own approval) is needed. HARDWARE OBSERVED for alpha-1.0 frames: unchanged.
- depth/draw-order semantics of DS translucent polygons (DESMUME: depth write only if TranslucentDepthWrite bit, sort order) are NOT implemented; overlapping translucent polygons may differ from DS.
- 5B-0b (explicit blend state in the renderer) is now required, not optional: Run 14 showed the default blend state does NOT alpha-blend on the console, so 5B-0b must set the blend state explicitly (as for cull/depth/TexEnv) and decide the alpha-0 / wireframe handling; a further hardware run (its own approval) is needed before any conclusion.
- no cull mode (bits 6-7 are parsed past, not applied);
- alpha 0 is counted as wireframe but outline-only drawing is not rendered (the
  polygon reaches the output with alpha 0.0 and looks like an ordinary colour);
- the alpha scale above is marked TO BE VERIFIED, not a hardware fact.

Host coverage: literal C unit tests for 31/16/4/1/0, pending-until-BEGIN, two
BEGIN groups, no-attr default, non-alpha bits, RGB isolation; a SCENE=4 host
variant with per-quad alphas 31/16/4/0; and Python golden streams checked against
an independently written oracle.

### 5B-0b (explicit blend state) — BUILD/UNIT/EMULATOR; HARDWARE (Runs 15, 16) no change

Status: implemented (BUILD/UNIT/EMULATOR) as of 2026-11-07 (owner approval after
Run 14); HARDWARE OBSERVED (Run 15, 2026-11-07) and (Run 16, 2026-11-07) show
NO change of the console picture relative to Run 14: alpha 16/4 quads still
opaque, alpha-0 quad still not drawn. Run 16 (build id `e6e6c88-diag`) confirms
the intended binary ran and that the vertex alpha reaches the fragment stage
distinctly and monotonically (mode 1), while blending ON with constant
fragment alpha 0.5 does not mix with the background and an alpha-0 quad
disappears in mode 0 but is drawn at alpha 0.5 (mode 2). Cause UNKNOWN
(candidates: colour-buffer read/blend path ineffective with this render target;
effect state not what the C3D calls intend; an alpha-0 discard elsewhere).
Emulators (Tanuki3DS, Azahar) both blend and are NOT predictors. Next step
proposed (needs approval): in-console pixel probe. No further 5B stage is
started.

What was implemented:
- New header-only, pure-data carrier `arm11/include/twlhd_blend_config.h` with
  two blend configs (standard alpha blend and opaque) and one disabled alpha-test
  config.
- `arm11/source/renderer.c` now calls `C3D_AlphaBlend` + `C3D_AlphaTest` in both
  render paths: alpha blend in the clip-space path, opaque in the DEBUG path,
  with alpha test explicitly disabled in both.
- The HARD RULE comment now names depth, cull, blend, alpha test, and TexEnv.

What it fixes:
- Alpha < 1.0 is now requested to blend using the standard PICA translucent
  equation (`src*srcAlpha + dst*(1-srcAlpha)`).
- Alpha 0.0 under normal alpha blending produces `out = dst` (the fragment is
  not visible), which is the interim decision for the alpha-0 / wireframe case.

What stays UNVERIFIED:
- The `(alpha + 1) / 32` scale (`common/twlhd_gx_interp.c:301`).
- DS translucent-polygon depth/draw-order semantics: depth test stays
  `GPU_GREATER` + `GPU_WRITE_ALL`.
- Wireframe/outline-only drawing is still NOT implemented.
- Everything visual: HARDWARE OBSERVED (Run 15) shows no change of the console
  picture relative to Run 14; the cause is UNKNOWN (see Run 15 candidates). The
  `(alpha + 1) / 32` scale, DS translucent-polygon depth/draw-order semantics,
  wireframe/outline-only drawing and all later 5B stages remain UNVERIFIED;
  diagnostics (build id on screen; separate the candidates) are required before
  any further renderer change.

### 5B-0c / Run 16 diagnostic build — BUILD/UNIT + HARDWARE OBSERVED (Run 16)

Status: implemented BUILD/UNIT as of 2026-11-07; HARDWARE OBSERVED (Run 16,
2026-11-07): the intended binary ran (build id `e6e6c88-diag`); the vertex
alpha reaches the fragment stage distinctly and monotonically (mode 1); with
blending ON and constant fragment alpha 0.5 the quads are not visibly mixed
with the background and an alpha-0 quad disappears in mode 0 while the same
quad is drawn at alpha 0.5 (mode 2). The console blend does not behave like
alpha blending with this state; cause UNKNOWN. No further 5B stage started.

Purpose: bisect why the explicit alpha blend (5B-0b) showed no effect on the
real console in Runs 14/15 (alpha 16/31 and 4/31 quads drew opaque; alpha-0 quad
absent). The Run-16 build adds:

- A compile-time `TWLHD_BUILD_ID` (git short hash + `-diag`, or `unknown`) shown
  on the bottom console so the owner can confirm the right `.3dsx` started.
- A B-key-cycled clip-path diagnostic mode with three states:
  - **Mode 0 "normal"**: explicit alpha blend, TexEnv passes primary vertex
    colour/alpha unchanged — identical in effect to the current build.
  - **Mode 1 "alpha-as-gray"**: opaque blend, RGB = source alpha
    (grey = raw fragment alpha), output alpha forced to 1.0 via constant. If
    the fragment alpha differs from the vertex alpha, this mode reveals it.
  - **Mode 2 "const-alpha-0.5"**: explicit alpha blend, RGB = primary colour,
    alpha forced to 0.5 via constant. If the blend registers are ineffective,
    this mode will still draw opaque despite the forced alpha.
- Pure-data header `arm11/include/twlhd_diag_config.h` plus host tests with a
  mutation table.

HARDWARE UNVERIFIED: this is BUILD/UNIT only. The supervisor runs EMULATOR
checks; the owner runs the hardware test. Mode 0 is designed to be pixel-identical
to the current build on the emulator (Tanuki3DS screenshot comparison unchanged)
so any console difference is isolated to real-hardware blend-register behaviour
or fragment-alpha interpretation.

### Proposed next step (needs owner approval): in-console pixel probe

Proposal (not yet approved, not implemented): an in-console pixel probe that
reads the top framebuffer at the quad centres after rendering and prints the
RGB values on the bottom screen, removing the camera/ambient-light limit of
the photographic evidence. This is the Run 16 "open" item: the photo evidence
is relative, and a numeric in-console readout would settle whether the blend
is truly ineffective or only appears so in photos. Requires its own approval
before any code (section 6).
