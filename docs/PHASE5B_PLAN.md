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

**STATUS (2026-11-12): implemented BUILD/UNIT; EMULATOR (Azahar) and HARDWARE OBSERVED (Run 18) for the colour path.**
Run 18 (New 3DS, NTR numeric capture, tools/hw): lit colours of three synthetic frames (one white light with four normals;
red light/emission/specular/two lights; unlit control) match the host interpreter's prediction within 1 level; Azahar: deviation 0.
Not shown: equality of the DeSmuME-derived equations with real DS hardware, toon/highlight, textures.
Earlier status (2026-11-08): implemented BUILD/UNIT, HARDWARE UNVERIFIED.
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
- Repair round 1: format 5 (4x4) `palette_size` is now a caller-chosen even
  window in `[2, 65540]` bytes (round 1 first shipped `[2, 65536]`; raised in
  round 3), not the fixed 0 this stage originally specified. DeSmuME's PAL4X4
  unpacker reads palette RAM per block (`texcache.cpp` ~1752/1789-1843), so
  the old missing-palette rule was a design defect; the parser and writer now
  share `twlhd_v2_tex_palette_size_ok()`. Still BUILD/UNIT only.
- Repair round 3: format-5 window cap raised to 65,540 bytes (DeSmuME
  `texcache.cpp:1752/1791` derivation: `0x3FFF*4 + 7` is the last byte index).
  Orphan `TXDT`/`PLDT` chunks without `TXDS` now report `ERR_MISSING_CHUNK`
  before the payload caps. Parser stays lax, writer canonical; two full
  windows (131,080 bytes) remain a known `PLDT` cap limit (format doc section
  12). Still BUILD/UNIT only.
- Repair round 4: the writer zero-pads the `PLDT` payload to a multiple of 4
  because format-5 windows are only even and may be 2 mod 4. The 98,304-byte
  `PLDT` cap is applied to the padded length by both writer and parser; the
  padding is never part of a record. Still BUILD/UNIT only.

#### Status 5B-2b — interpreter texture latch and texture matrix

Implemented, **BUILD/UNIT only**, nothing EMULATOR or HARDWARE verified:

- `common/twlhd_gx_interp.[ch]`: new `TwlhdGxTexVertex` (16 B) and
  `twlhd_gx_interpret_ex(view, out, cap, out_count, viewport, stats,
  tex_out)`; `twlhd_gx_interpret()` keeps its signature and is a
  `tex_out == NULL` wrapper. `TwlhdGxVertex` stays 32 B (static asserts).
- Latches: TEX_COORD (0x22, applied at VTX time), TEXIMAGE_PARAM (0x2A) and
  PLTT_BASE (0x2B) (latched immediately). Per-vertex `tex_out` is a parallel
  array: same ordering, quad splitting and capacity truncation as `out`.
- Texture matrix: MTX_MODE 3 gets its own 20.12 matrix and one-entry stack
  (LOAD/MULT/IDENTITY/SCALE/TRANS/PUSH/POP/STORE/RESTORE). The lighting vector
  matrix is untouched by mode 3. STORE/RESTORE ignore the index and always use
  slot 0 (gfx3d.cpp:950-1000); PUSH/POP use the same single slot with
  `gxstat.se` on wrap (gfx3d.cpp:860-871, :918-929).
- Transform: mode 0 = raw s1.11.4 (/16.0); mode 1 = texture-matrix rows 0/1
  applied to the raw s1.11.4 coordinate with raw m8/m9 and m12/m13 addends,
  64-bit sum, arithmetic `>>12`, `(int32_t)` cast and `/16.0` floats, exactly
  `desmume/src/gfx3d.cpp:1578-1593` (see `docs/DS_GX_RESEARCH.md`). Modes 2/3
  are **not implemented**: raw coordinates plus
  `TwlhdGxStats.tex_transform_unsupported` (new, appended field), counted per
  VTX command.
- Tests: C unit tests in `tests/host/test_gx_interp.c` (latch order, sign
  edges, mode 0 vs 1, modes 2/3 counter, quad/strip/capacity alignment,
  `tex_out == NULL` equivalence, stack ops and stack errors); golden streams
  in `tools/test_gx_interp_golden.py` compared to the independent
  `TextureOracle` in `tools/gx_interp_oracle.py`.
- Sanctioned existing-test updates (stats/stack semantics only, no assertion
  deleted): `texture-mode-ignored` -> `texture-mode-loads-matrix`
  (`ignored_no_effect` 1 -> 0, plus a mode-1 assertion);
  `light-commands-not-ignored` `ignored_no_effect` 6 -> 3;
  `light-mode3-stack-ignored` -> `light-mode3-stack-leaves-vec`
  (`ignored_no_effect` 2 -> 0); `light-store-restore-mode3-leaves-vec` keeps
  STORE/RESTORE index 2: mode 3 ignores the index and always uses slot 0
  (gfx3d.cpp:950-1000), so RGB 21 still holds. The same index is used in the
  matching golden stream. (An earlier round changed this index to 0; reverted
  in repair round 3, see `docs/reports/5b2b-mutation-report.md`.)
- Not done: recorder-side texture capture; sampling/TexEnv; PICA upload; the
  vertex stride change; modes 2/3.

- Repair round 1 (2026-11-10): mode 1 was 16x wrong for translation and
  missing the m8/m9 terms. Rewritten to the exact DeSmuME
  `desmume/src/gfx3d.cpp:1578-1593` formula (raw s1.11.4 inputs, raw
  m8/m9/m12/m13 addends, 64-bit sum, arithmetic `>>12`, `(int32_t)` cast,
  `/16.0f`). Oracle and golden streams updated; new literal cases cover
  identity, translation, m8/m9, scale, shear, negative-rotation, negative-sum
  floor shift, 64-bit headroom and the s32 wrap. Only two existing literals
  were adapted (`test_tex_mode0_vs_mode1` mode-1 half 3.0/0.75 ->
  2.0625/0.9375; `test_tex_matrix_stack_ops` 4.0 -> 2.125), both because they
  encoded the old `m12*4096`/`/4096` formula. Non-texture and mode-0
  behaviour is unchanged.

- Repair round 3 (2026-11-11): mode-3 STORE/RESTORE treated the index as a slot
  (index >= 1 -> `ERR_STACK`), but DeSmuME ignores it and always copies
  `_mtxStackTexture[0]` with no error (`gfx3d.cpp:950-1000`). The guard now
  matches modes 0 and 3, the reverted index-2 test is restored in both test
  suites, and a STORE 5 / RESTORE 7 round-trip case was added. Details and
  mutation proof: `docs/reports/5b2b-mutation-report.md`.

- Repair round 4 (2026-11-11): PUSH/POP and STORE/RESTORE now share ONE
  storage slot, as DeSmuME does with `_mtxStackTexture[0]` (PUSH :861, POP :928,
  STORE :965, RESTORE :996); the PUSH/POP depth counter is kept apart, in both
  the C interpreter and `tools/gx_interp_oracle.py`. Only `tex_stack.stack[0]`
  is now initialised to identity in `twlhd_gx_interpret_ex`, matching
  `MatrixInit(_mtxStackTexture[0])` (`gfx3d.cpp:531-532`), so a mode-3
  RESTORE/POP before any write returns identity instead of zeros. The
  projection/position/vector stacks deliberately stay zeroed (documented GAP in
  `docs/DS_GX_RESEARCH.md`). New C and golden tests
  `tex-matrix-push-store-share-slot` and `tex-restore-without-store` kill the
  separate-slots and init mutants; see `docs/reports/5b2b-mutation-report.md`.

#### Status 5B-2c — host texture decoder (formats 1-7 to RGBA8)

Implemented, **BUILD/UNIT only**, nothing EMULATOR or HARDWARE verified:

- `common/twlhd_tex_decode.[ch]`: allocation-free decoder from one TXDS record
  (formats 1-7) to a caller-owned RGBA8 buffer (row-major, top row first,
  R,G,B,A), with an explicit `TwlhdTexResult` error enum and
  `twlhd_tex_dimensions()` / `twlhd_tex_result_string()` helpers. Size checks
  call `twlhd_v2_tex_sizes()` / `twlhd_v2_tex_palette_size_ok()`; no formula is
  duplicated and the output buffer is untouched on every error path.
- Formulas were re-verified against the DeSmuME checkout
  (`desmume/src/texcache.cpp`, commit `fc504ee0`): A3I5 `:1522-1528`, A5I3
  `:1743-1749`, I2/I4/I8 `:1057-1112/1341-1386/1389-1405`, 4x4 `:1752/1755-1885`,
  16bpp `:2007-2026`, colour-0 rule `:429-436`. The verified formulas, the
  sub-byte packing order (low bits = first texel) and the per-mode 4x4 rules are
  written up in `docs/DS_GX_RESEARCH.md` section "Decoder (5B-2c)".
- Tests: `tests/host/test_tex_decode.c` (hand-computed literals for every
  format, both square and non-square sizes, alpha edges, palette boundaries,
  colour-0 on/off/ignored, 4x4 modes/offsets/window bounds, error paths and the
  output-canary check), `tests/host/tex_decode_cli.c` plus the independent
  oracle `tools/tex_decode_oracle.py` and golden cross-check
  `tools/test_tex_decode_golden.py`, both wired into `make test`.
- Mutation evidence: `docs/reports/5b2c-mutation-report.md`.
- Not done: no PICA upload, no recorder-side capture change, no v2-format,
  vertex-layout or interpreter change. The 4x4 decoder reads only the captured
  PLDT window and errors past it instead of wrapping palette RAM.

#### Status 5B-2d — host-side PICA texture preparation (RGBA8)

Implemented; orientation corrected in this repair round, **EMULATOR** now confirms the corrected orientation, **HARDWARE** has not yet re-verified it:

- `common/twlhd_pica_tex.[ch]`: allocation-free host preparation of a decoded
  RGBA8 image for the PICA200. `twlhd_pica_tex_swizzle_rgba8()` /
  `..._unswizzle_rgba8()` produce/consume 8x8 Morton tiles with A,B,G,R texel
  bytes and top-down tile rows (facts and `file:line` citations in
  `docs/DS_GX_RESEARCH.md`, "PICA texture upload (5B-2d)");
  `twlhd_pica_wrap_from_teximage()` maps DS TEXIMAGE_PARAM bits 16-19 to the
  PICA wrap values 0-3; `twlhd_pica_texcoords_from_gx()` normalises DS texel
  coordinates to a packed 8-byte `TwlhdPicaTexCoord` array with
  `u = s/width`, `v = 1 - t/height`; `twlhd_pica_tex_bind_plan()` groups a
  triangle list into one entry per `(TEXIMAGE_PARAM, PLTT_BASE)` run, with an
  `untextured` flag for PackedFormat 0. Sizes are powers of two in 8..1024,
  every error path leaves the output untouched and no core function allocates.
- Owner decision point 3 is **ANSWERED** (2026-11-12): texcoords travel in a
  separate attribute buffer; `TwlhdGxVertex` stays 32 B and main-buffer vertex
  `i` pairs with texcoord `i`.
- Tests: `tests/host/test_pica_tex.c` (hand-written 8x8 destination-index table
  and literal corners, non-square 16x8/8x16, round trips, all 16 wrap
  combinations, hand-computed texcoord floats, bind-plan runs/capacity/%3/
  untextured, and guard-byte harnesses), `tests/host/pica_tex_cli.c` plus the
  independent oracle `tools/pica_tex_oracle.py` and golden cross-check
  `tools/test_pica_tex_golden.py`, all wired into `make test`.
- Mutation evidence: `docs/reports/5b2d-mutation-report.md` (orientation-fix
  addendum: 202 unique mutants, 187 killed by test, 0 killed by golden,
  10 killed by build, 5 equivalent survivors; targeted
  `swizzle-restore-vflip` and `unswizzle-restore-vflip` both killed by
  `pica-tex/orientation-rows`).
- Not done: no renderer/arm11 consumer, no GPU calls, no recorder-side texture
  capture, no v2-format change, no second texture format (RGBA8 only), and no
  hardware verification of the layout, alignment, size limits or sampler
  defaults.

#### Status 5B-2e-1 — host-side textured frame preparation

Implemented, **BUILD/UNIT only**, nothing EMULATOR or HARDWARE verified:

- `common/twlhd_pica_frame.[ch]`: one allocation-free call turns a parsed v2
  frame (`TwlhdV2View` with TXDS/TXDT/PLDT) plus the interpreter's per-vertex
  `TwlhdGxTexVertex` latch into (a) bind-plan entries, (b) deduplicated
  decoded+swizzled PICA RGBA8 textures packed into a caller arena, (c) an
  8-byte-per-vertex texcoord array normalised with each entry's texture size,
  and (d) an entry -> texture index array with a per-entry status. It reuses
  the v2 parser, `twlhd_tex_decode`, and `twlhd_pica_tex_*` and never allocates.
- Keying: bind-plan runs are keyed by the triangle's first vertex
  (`common/twlhd_pica_tex.h:118-129`); the texture is the TXDS record with the
  exact same `(teximage_param, pltt_base)` pair, which the format guarantees is
  unique (`common/twlhd_v2_parse.c:358-366`, `docs/TWLHD_V2_FORMAT.md:296`).
  Missing/unsupported textures are reported per entry; NULL, capacity, arena
  and `count % 3` errors fail the whole call. `ERR_ARENA` is distinct and never
  drops textures silently. `vertex_count == 0` is a valid empty frame (`OK`
  with zero entries/textures/arena bytes); `tex` stays a required pointer and
  still fails with `ERR_NULL`.
- Memory budget (stated exactly in `docs/DS_GX_RESEARCH.md`, "Textured frame
  preparation (5B-2e-1)"): 4 MiB for one 1024x1024 RGBA8, 1 GiB worst-case
  arena (256 overlapping TXDS records), 8 MiB non-overlapping worst case,
  4 MiB scratch, 512 KiB texcoords. 128-byte texture-start alignment is an
  explicit UNVERIFIED-for-hardware assumption.
- Tests: `tests/host/test_pica_frame.c` (hand-built v2 frame with two
  textures, a reused texture, an untextured entry and a paletted entry;
  byte-exact hand-computed arena bytes for the 8x8 texture and the 16x8
  paletted one; hand-computed non-square texcoords; every NULL/capacity/arena/
  count path with untouched-output canaries; missing-record and
  decoder-rejected per-entry statuses; format-5 palette windows pinned per
  block mode, with mode-0/2 exact fits, mode-0/2 windows that fit 4 but not
  6/8 bytes, mode-1/3 exact fits vs 2 bytes short, the offender first and
  last, and `arena_needed == arena_used` in every case; guard bytes
  before/after every output and a 0x00/0xFF input-tail two-pass compare;
  `vertex_count == 0` with every capacity 0, a textured `tex[0]` that
  matches a TXDS record and a poisoned one-element array, returning OK with
  zero entries/textures/arena bytes and untouched guards, plus the
  `tex == NULL` `ERR_NULL` rule for both entry points).
  `tests/host/pica_frame_cli.c` with the independent
  `tools/pica_frame_oracle.py` and `tools/test_pica_frame_golden.py`, all wired
  into `make test`.
- Mutation evidence: `docs/reports/5b2e1-mutation-report.md` (238 mutants,
  211 killed, 27 equivalent; runner
  `python3 docs/reports/5b2e1_mutate_pica_frame.py --jobs 4`, 17/17 C labels).
  The runner skips shift operators by design, so the format-5 mode shift
  (`word >> 14` -> `word << 14`, line 192) is verified manually: the C suite
  fails via the `arena_needed == 0` assertion while the golden stays green
  (the decoder re-checks the rule); `common/` is restored and
  `git diff --exit-code common/` clean. The round-6 fix moved the source
  lines below `common/twlhd_pica_frame.c:254` down by 13.
- Not done: no renderer/arm11 consumer, no GPU or citro3d calls, no recorder
  change, no v2-format/decoder/interpreter change.

### Status 5B-2e-2 — ARM11 renderer texture path

Status labels:

- **BUILD:** `make -C arm11` and `make -C arm11 DIAG=1` compile with the
  project's existing ARM11 flags (`-g -Wall -O2` per `arm11/Makefile`); the new
  shader `arm11/shaders/twlhd_clip_tex_vshader.v.pica` assembles via picasso in
  both builds. A pre-existing `-Wmaybe-uninitialized` warning in
  `common/twlhd_v2_parse.c` reappears after reverting a stage hunk that was out
  of scope; see OPEN ITEM below.
- **UNIT:** `make test` green, including the new
  `tests/host/test_tex_draw_plan`, `tools/test_check_tex_shot.py` and
  `tools/test_gen_tex_frames.py`. `tests/host/test_diag_config.c` WAS re-pinned:
  the `twlhd_diag_config(` count in `renderer.c` went 1 → 2 and the whole-file
  `g_diag_mode` count went 4 → 5, because the new
  `draw_clip_textured_entries()` adds one more `twlhd_diag_config(g_diag_mode)`
  call; a new exact-count check for `draw_clip_textured_entries` was added at
  `test_diag_config.c:231-234, 248-251`. `tests/host/test_blend_config.c` was
  not changed. No assertion was loosened. Mutation report
  `docs/reports/5b2e2-mutation-report.md` covers
  `common/twlhd_tex_draw_plan.c` (109 unique mutants, 82 killed by tests,
  20 killed by `-Werror` build, 7 equivalent survivors).
- **EMULATOR:** generator script `tools/gen_tex_frames.py` and checker
  `tools/check_tex_shot.py` committed with a documented procedure; see
  `docs/EMULATOR_VALIDATION.md` for the exact commands. Reproduced in this
  sandbox with `/home/one/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`
  (Azahar `9c38437`). The regenerated screenshots differ:
  `tex_shot_199.ppm` sha256
  `58003d653e8d8c18b08af47ffcf3f6316460735e57a440fdeb9bc1f4a22f896a`,
  `tex_ctl_shot_199.ppm` sha256
  `b1be377150b726bff4b43a7f85f407988ff3d310301c67c6d693693a37f4fa7c`.
  The screenshots were regenerated with the repair-round-5 binary; the
  control console layout is unchanged (no `T:` trace-ring line on untextured
  frames) and the checker output matches the previous reference run.
  The bottom-screen readout shows `tex staged OK: 4 entries, 3 textures` and
  the trace ring includes `tex commit`, `tex bind`, and `clip tex DrawArrays`,
  confirming the texture path is exercised. `check_tex_shot.py --frame tex`
  passes all four regions:

      UL colour OK: 2 distinct colours: (0, 255, 0) (255, 255, 255)
      UR colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
      LL colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
      LR colour OK: 2 distinct colours: (0, 0, 255) (255, 255, 255)
      UL structure OK: quad=125x66 cells=4x2 predicted_period=31.2x33.0 measured_period=31.0x32.5
      UR structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
      LL structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
      LR structure OK: quad=125x66 cells=4x4 predicted_period=31.2x16.5 measured_period=31.0x16.2
      UL ratio OK: UL=0.95 square_avg=1.91 diff=0.95
      UR==LL reuse OK: period=True phase=True

  `--frame ctl` passes the colour check. Tolerance: per-channel RGB `<= 40`;
  structure tolerance `25 %` (or `1 px`) of predicted cell period, ratio diff
  `> 0.15`. This is EMULATOR evidence only; it is not HARDWARE VERIFIED.
- **HARDWARE:** NOT RUN. A draft procedure is below; needs owner approval
  before any console action.

Requirement 2 verification: the silent no-texture path in
`arm11/source/main.c:152-157` contains no `renderer_trace()` call, no write to
`g_status`, and the only `renderer_set_trace()` action is the guarded restore
from `tex_hook_apply()` at line 156, which is a no-op when no textured frame
owns the hook; the builtin path (`main.c:654-655`) and v1 path
(`main.c:674-675`) contain no renderer trace-function calls at all.

What changed:

- `common/twlhd_tex_draw_plan.[ch]`: allocation-free pure C module that maps a
  prepared frame (`plan[]`, `entry_texture[]`, `entry_status[]`, texture table,
  arena) to per-entry draw descriptors and a whole-frame verdict. Texture sizes
  are guarded to PICA200 power-of-two limits (8..1024). A new pure lifecycle
  state machine mirrors the pending/active release rule used by the renderer.
  Wrap/filter codes are exported as constants bit-identical to libctru.
- `arm11/source/renderer.c`: textured clip-scene staging API
  `renderer_set_clip_scene_textured()` with a bottom-screen staging-status
  readout; per-entry draw loop with texture bind, wrap/filter, separate
  texcoord buffer, and untextured fallback for `NO_DATA`/`UNSUPPORTED`/failed
  entries; whole-frame fallback on any allocation or prepare failure; texture
  unit 0 unbound before every free of a possibly-bound `C3D_Tex` set
  (commit, clear, shutdown, and end-of-frame); `renderer_set_geometry()` now
  clears the texture lifecycle so a v1/debug load after a textured v2 frame
  frees the texture set; DEBUG draw branch commits the texture lifecycle and
  handles `FREE_ACTIVE_NOW`; separate textured shader uniform lookup for
  `clipfix`.
- `arm11/include/renderer.h`: new staged API alongside the existing untextured
  functions; existing signatures unchanged.
- `arm11/source/main.c`: switched to `twlhd_gx_interpret_ex()` so per-vertex
  texture latches are filled; the texture path now saves the renderer trace
  hook before installing `trace_cb` and restores the saved hook (never `NULL`)
  on fallbacks or when the textured frame ends. `tex_hook_apply()` at
  `main.c:95-105` is the only place in the file that calls
  `renderer_set_trace()`. The hook is installed at `main.c:168-169` only after
  the frame carries texture data and fits the budget; budget/prepare/staging
  fallbacks (`main.c:160-164`, `main.c:181-185`, `main.c:199-202`) call the
  guarded restore. The builtin path (`main.c:654-655`) and v1 geometry path
  (`main.c:674-675`) never touch the trace hook. The silent no-texture v2
  branch (`main.c:152-157`) performs a guarded restore, emits no trace marker,
  writes no `g_status`, and is otherwise identical to the untextured path.
  `TWLHD_DISABLE_TEXTURE_PATH` now compiles to a zero-side-effect early return.
  The `T:` trace-ring line in `update_console()` (`main.c:471`) is printed only
  when `g_tex_path_attempted` is set, so untextured/control frames keep the
  original bottom-screen layout.
- `arm11/shaders/twlhd_clip_tex_vshader.v.pica`: clip shader variant that
  passes a separate `(u,v)` attribute to `texcoord0`; existing shaders untouched.
- `tools/check_tex_shot.py` / `tools/test_check_tex_shot.py`: stdlib-only
  screenshot checker (colour + checker structure) and its synthetic PPM unit
  tests, including negative cases for wrong cell period, square cells where
  non-square are expected, and mismatched reuse.
- `docs/EMULATOR_VALIDATION.md`: documented checker command, rectangles,
  tolerances, root-cause diagnosis, and the real checker output from the
  regenerated screenshots.
- `docs/reports/5b2e2-mutation-report.md`: rerun with the final module.
- `tools/test_gen_tex_frames.py`: unit tests for the deterministic textured
  frame generator, including the new over-budget option.

Compile-time/runtime switch:

- The texture path is engaged only when the loaded v2 frame carries texture
  chunks (`twlhd_pica_frame_arena_needed()` returns `> 0` arena bytes, prepare
  succeeds, and the renderer staging succeeds). Frames without textures use the
  exact existing `renderer_set_clip_scene()` → `draw_clip_internal()` path.
- Define `TWLHD_DISABLE_TEXTURE_PATH` at compile time to force the untextured
  path regardless of the frame content (useful for a quick bisection). The
  switch is documented here; no build-time default is changed.

UNVERIFIED (build-only reasoning):

- Whether `C3D_TexLoadImage` swizzles; only the header is installed, so the
  renderer uses raw `memcpy` into `tex->data` and marks this point
  UNVERIFIED.
- Whether `C3D_TexInit(..., onVram=false)` accepts the prepared Mortonised
  layout byte-for-byte.
- 128-byte alignment of `tex->data` and the arena base.
- VRAM vs linear trade-offs and heap budget.
- PICA sampler defaults (nearest/linear/mipmap); the renderer explicitly sets
  `GPU_NEAREST` as a DS-fidelity decision.
- Attribute 2 with `BufInfo_Add` permutation `0x2` on real hardware.
- Peak memory budget (≈ 20.5 MiB transient) on arm11 heap/FCRAM.

OPEN ITEMS:

- `common/twlhd_v2_parse.c:372-379` triggers a `-Wmaybe-uninitialized`
  warning for `texel_bytes`/`index_bytes` under the project's existing ARM11
  flags (`-g -Wall -O2`). The warning is pre-existing in main-tree code: loop 1
  at lines 342-353 already rejects every `teximage_param` for which
  `twlhd_v2_tex_sizes()` fails, so by loop 3 the call can never fail, but GCC
  does not propagate that fact across the loop. The stage's earlier hunk that
  added `TWLHD_V2_ERR_TXDS_SIZE` on the ignored result was out of scope
  (it changed the v2 parser path), so it was reverted. The only permitted fix
  is a parser edit, which is forbidden by the task scope, or adding flags,
  which is also forbidden. The warning is therefore recorded here and does not
  block the stage; it should be fixed upstream or accepted as a known
  main-tree warning.

Rollback:

1. Delete `arm11/shaders/twlhd_clip_tex_vshader.v.pica`,
   `common/twlhd_tex_draw_plan.c`, `common/twlhd_tex_draw_plan.h`,
   `tests/host/test_tex_draw_plan.c`, `tools/gen_tex_frames.py`,
   `tools/check_tex_shot.py`, `tools/test_check_tex_shot.py`,
   `tools/test_gen_tex_frames.py`,
   `docs/reports/5b2e2_mutate_tex_draw_plan.py`,
   `docs/reports/5b2e2-mutation-report.md`.
2. Revert `arm11/source/renderer.c`, `arm11/include/renderer.h`,
   `arm11/source/main.c`, `tests/host/test_diag_config.c`, root `Makefile`,
   `docs/DS_GX_RESEARCH.md`, `docs/EMULATOR_VALIDATION.md` and
   `docs/PHASE5B_PLAN.md` to the pre-5B-2e-2 versions.
3. Because the feature is opt-in per loaded frame, setting
   `TWLHD_DISABLE_TEXTURE_PATH` or loading untextured frames gives the old path
   without reverting code.

#### Status 5B-2e-2 follow-up: multi-format frames

`common/twlhd_pica_tex.c` changed in the orientation-fix round; `arm11/` was not changed.

- **BUILD:** no product build change. `tools/gen_tex_format_frames.py` writes
  `twlhd_frame_v2_formats.bin` (8 quads: formats 1 A3I5, 2 I2, 3 I4, 4 I8,
  5 4x4 with block modes 0-3, 6 A5I3, 7 16bpp, plus a 16x16 I4 quad).
- **UNIT:** `make test` green, including the new `tools/test_tex_formats.py`
  (determinism, v2 parse, byte-exact `tex_decode_cli` vs the Python oracle,
  `twlhd_pica_frame_prepare()` with 8 entries / 8 textures, checker
  perfect/noise/shift pass and permuted/mirrored/wrong-format/missing fails,
  hand-computed literal colours per format). No existing assertion weakened.
- **EMULATOR:** `tools/check_tex_formats.py` on the Azahar host screenshot at
  frame 199 matches all 8 quads exactly (worst channel deviation 0) once the
  checker was corrected to expect decoded row 0 at the top quad edge. Reference
  commands and verbatim output are in `docs/EMULATOR_VALIDATION.md`, subsection
  "Multi-format frame (5B-2e-2 follow-up)".
- **FINDING and FIX (orientation):** textures were rendering vertically
  mirrored: decoded row 0 appeared at the bottom quad edge because
  `common/twlhd_pica_tex.c` was storing tile rows bottom-up. The Azahar
  sources (`texture_codec.h:892-897`, `:577-604`) actually store memory tile
  rows top-down; the old reading of the "bottom-up linear buffer" comment was
  wrong. Fix (A): removed the `vy = height-1-y` row flip in the swizzle and
  unswizzle and kept `v = 1 - t/height`. The multi-format checker now pins
  decoded row 0 at the top edge. HARDWARE re-verification of the corrected
  orientation has NOT been run.
- **HARDWARE NOT RUN.**

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
   Answered 2026-11-12: **ANSWERED** — keep the 32 B vertex and use a separate
   texcoord attribute buffer (`common/twlhd_pica_tex.h`, 8-byte
   `TwlhdPicaTexCoord`); implemented host-side in 5B-2d.
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

## 5B-2e-2 hardware run (EXECUTED 2026-11-14 as Run 19; the text below is the original draft)

**HARDWARE OBSERVED (Run 19, 2026-11-14, owner-approved, assistant-executed under
CONSOLE_ACCESS_POLICY.md).** Build main `f89d3bd`, `twlhd-arm11-tex.3dsx`
(sha256 `d49f404b...`), frames from `tools/gen_tex_frames.py`. Method as Run 18
(3dslink, ftpd-classic FTP swap with read-back, ntr_input, numeric ntr_capture).
Details and hashes: `hwtest-staging/RUN2_RESULTS.md` (Run 19) and `RUN19_PROCEDURE.md`.

- Control frame (no texture chunks): `tools/check_tex_shot.py --frame ctl` PASS on
  the console capture. The default untextured path is unchanged on hardware.
- Textured frame: the console draws the checker textures. After snapping every
  pixel of the JPEG capture to the nearest of {region colour, white, clear} the
  colour, period/structure and non-square-ratio checks PASS. Cell-centre
  sampling gives identical patterns to the emulator (LL, LR fully; first 6 rows
  of UL/UR), and UR/LL (same texture) show the same pattern. Pixel-class
  agreement with the emulator inside the four rectangles 95.7 % (mismatches are
  checker edges), whole top screen mean abs diff 4.38 per channel. The
  hardware picture is shifted about 1 px up/left relative to the emulator
  (cause not identified: rasteriser rule or capture).
- **ORIENTATION (corrected interpretation):** the Run 19 hardware pictures
  were vertically mirrored relative to DS semantics: the checker phase at the
  top-left cell of each quad was white, not the base colour, matching the
  emulator's mirrored output with the old bottom-up swizzle. The earlier
  "orientation OK" / "orientation/V verified" wording in Run 19 reporting was
  wrong; only cell period/structure and reuse were actually validated. The
  orientation bug was fixed in `common/twlhd_pica_tex.c` (remove row flip,
  keep `v = 1 - t/height`), and `tools/check_tex_shot.py` now has a top-left
  cell phase check. A HARDWARE re-run with the corrected build is required to
  confirm the fixed orientation on real hardware.
- The script's "UR==LL reuse" sub-check printed FAIL on the snapped hardware
  image (it compares the colour of one corner pixel per quad, which a 1 px
  shift and JPEG edges flip); the cell-centre comparison is the reuse evidence.
  `tools/check_tex_shot.py` now samples a 5x5 mean at the top-left cell centre
  for its phase check, which is robust to the known ~1 px shift.
- Over-budget frame (3 x 1024x1024, arena 12 MiB > 8 MiB cap): no hang, no
  garbage; picture equals the emulator fallback (4743 white pixels on both,
  mean abs diff 0.71).
- Not observed: the bottom screen stays black in the capture, so the build-id
  line and the trace readout could not be read (the 3dsx sha256 was verified
  before upload). Not shown: DS-hardware accuracy of the texture equations,
  paletted/compressed formats beyond the test frame, performance, real memory
  exhaustion.
- The original `/twlhd_frame.bin` (1128 B, sha256 `2decd74d...`) was restored
  with read-back.

## 5B-2e-2 hardware run (original draft)

This section is a **DRAFT** only. No SD/console/network scripts are created by
this stage. The owner must review and approve the procedure before any real
hardware is touched.

### Goal

Confirm that the arm11 renderer can upload, bind and draw the already-prepared
RGBA8 textures from a v2 frame with texture chunks, with the separate 8-byte
texcoord attribute buffer, and that untextured/failed entries still draw.

### Prerequisites

- A New 3DS with the usual homebrew entry point and SD access.
- The approved 5B-2e-2 `.3dsx` built from a known commit hash
  (`TWLHD_BUILD_ID` shown on the bottom screen).
- A generated test frame from `tools/gen_tex_frames.py` placed on SD as
  `sdmc:/twlhd_frame.bin`.
- The existing `renderer_trace` hook available via RTCom or a debug build.

### Procedure

1. **Build hash check.** Start the `.3dsx` and verify the bottom screen shows
   the expected `build: <hash> blend=explicit` line.
2. **Baseline.** Load an untextured v2 frame (or press Y for the built-in
   triangle) and confirm the top screen draws the expected picture.
3. **Textured frame.** Copy the generated textured frame to
   `sdmc:/twlhd_frame.bin`, press A to load it, and observe the top screen.
4. **Expected picture.** At least one 8x8 or 16x16 two-colour checker texture
   should be visible, one non-square texture should be visible, and a texture
   reused by two draw entries should look identical in both places.
5. **Fallback probe.** Generate an over-budget frame with
   `python3 tools/gen_tex_frames.py <outdir> --over-budget 3`, copy
   `<outdir>/twlhd_frame_v2_tex_over.bin` to `sdmc:/twlhd_frame.bin`, and press
   A to load it. Each of the three distinct 1024x1024 I2 keys decodes to a 4 MiB
   RGBA8 texture, so `twlhd_pica_frame_arena_needed()` reports 12 MiB, which is
   above the 8 MiB practical cap. The renderer should fall back to the
   untextured clip path with a `tex budget fallback` trace marker, drawing solid
   colours and not crashing.
6. **B-key recovery.** If the screen hangs or shows garbage, press B to cycle
   the diagnostic mode and observe whether the picture changes; if still hung,
   press Y to return to the built-in triangle, or START to exit.

### Trace markers to watch

The renderer emits the following markers on the texture path:

- `tex plan fallback`, `tex upload fallback`, `tex staging fallback` — a
  whole-frame fallback occurred.
- `tex commit` — pending texture set promoted to active at frame start.
- `tex attr` — switching to the textured shader/attribute layout.
- `tex bind` — a new texture is bound.
- `tex fallback entry` — an untextured/`NO_DATA`/`UNSUPPORTED` entry is drawn
  with the untextured clip shader.
- `clip tex DrawArrays` — each textured or fallback draw call.

### Risks

- **Texture memory exhaustion.** The renderer keeps both the caller arena and
  the GPU copies during upload; peak use can approach ~20 MiB. If linear memory
  is exhausted, `linearAlloc` returns NULL and the frame falls back, but the
  exact arm11 heap budget is UNVERIFIED.
- **Stride/attribute mismatch hang.** The Run 5 precedent showed that a missing
  padding declaration can wedge the PICA200. The new texcoord buffer adds
  attribute slot 2 and permutation `0x2`; if the reasoning from `0x0`/`0x1` is
  wrong, the GPU may hang. The renderer refuses to draw if the padding check
  fails, but that check itself is build-reasoned, not hardware-verified.
- **Double swizzle.** If `C3D_TexInit`'s backing memory or `C3D_TexLoadImage`
  expects a different layout than the prepared bytes, the texture will look
  garbage. The raw-copy path was chosen to avoid this, but the internal layout
  expectation is UNVERIFIED.
- **State leak between textured and untextured entries.** Mid-frame shader/
  TexEnv/AttrInfo switches are a known 3DS failure mode (Run 14 blend-state
  precedent). The code rebuilds state per entry class, but persistence bugs
  could still hang or misrender.
- **Recovery.** If a hang occurs, the non-blocking completion probe and B-key
  cycle are the first recovery step; Y returns to the built-in triangle; START
  exits. No network or SD writes are required during the run.

### Approval gate

Do not run this on hardware until the owner explicitly approves the procedure,
the test frame, and the build hash. The assistant never executes hardware runs.
