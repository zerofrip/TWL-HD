# Phase 4D — Capture-Format Expansion: Proposal

**Status: PROPOSAL ONLY. Nothing in this document is implemented, and it is
not approved.** Phase 4D implementation must not start without explicit
approval. No file other than this one was changed to produce it.

Evidence labels used throughout:

- **LOCAL** — read directly from this repository or the installed devkitPro
  headers in this session.
- **GBATEK** — read from the public GBATEK pages fetched in this session.
- **UPSTREAM** — read from devkitPro upstream sources fetched in this session.
- **UNVERIFIED** — general knowledge or reasoning, not checked against a
  source or hardware here. Treat as a hypothesis.

## 1. Where we are, and the strategic caveat

Phase 4B/4C render a `TwlhdFrame` dump with a *debug* camera. They cannot
reconstruct the original image because the v1 capture lacks transforms,
textures, polygon attributes and lighting (`docs/PHASE4A_PICA200_FEASIBILITY.md`).

**Caveat on the long-term goal.** The goal is native-resolution rendering of
DS games on New 3DS. The capture → SD file → PICA200 playback pipeline built
so far is an *offline* path. Phase 3 (`docs/RTCOM_AUDIT.md`) showed RTCom
cannot carry a frame live, so this pipeline can validate fidelity and the
renderer, but **by itself it cannot play a DS game in real time.** Real-time
play needs one of: a different live transport (none identified), or running
the DS software's logic on the 3DS side (an emulation-style architecture,
"approach C" in the original design discussion). This is a genuine
architectural decision for you that 4D does not resolve; section 8 lists it.
4D is justified regardless as the way to define a faithful intermediate
representation and to measure what fidelity is achievable.

## 2. Gap inventory against the hardware command set

Source for the command list and parameter counts: GBATEK 3D I/O map
(**GBATEK**). "Format" = has a `TwlhdCmdType` id today; "Wrapper" = an
`arm9/fifo_hook` function actually records it (**LOCAL**).

| Cmd | Name | Format | Wrapper | Needed to render faithfully |
|---|---|---|---|---|
| 10h–12h | MTX_MODE / PUSH / POP | yes | yes | yes |
| 13h,14h | MTX_STORE / RESTORE | yes | no | yes |
| 15h | MTX_IDENTITY | yes | yes | yes |
| 16h–1Ah | MTX_LOAD/MULT 4x4,4x3,3x3 | yes | **no** | yes |
| **1Bh,1Ch** | **MTX_SCALE, MTX_TRANS** | **no** | no | yes (`glScale`/`glTranslate`) |
| 20h–22h | COLOR / NORMAL / TEXCOORD | yes | yes | yes |
| 23h | VTX_16 | yes | yes | yes |
| 24h–27h | VTX_10/XY/XZ/YZ | yes | **no** | yes |
| **28h** | **VTX_DIFF** | **no** | no | yes |
| **29h** | **POLYGON_ATTR** | **no** | no | yes (alpha, cull, light enable, poly id) |
| **2Ah,2Bh** | **TEXIMAGE_PARAM, PLTT_BASE** | **no** | no | yes (texture descriptor) |
| **30h–34h** | **DIF_AMB, SPE_EMI, LIGHT_VECTOR, LIGHT_COLOR, SHININESS** | **no** | no | yes (lighting) |
| 40h,41h | BEGIN_VTXS / END_VTXS | yes | yes | yes |
| **50h** | **SWAP_BUFFERS** | **no** | no | yes (frame boundary) |
| 60h | VIEWPORT | yes | yes | yes |
| 70h–72h | BOX/POS/VEC_TEST | no | no | no (visibility tests) |

Rendering-engine state outside the command FIFO is also absent: clear
color/depth, fog (color/offset/table), toon table, edge colors, alpha-test
reference (**GBATEK** 3D I/O map, "Rendering Engine" registers).

Two consequences worth stating plainly:

1. Even the commands that *are* in the format are mostly **not recorded** —
   only the wrapped gl calls are (**LOCAL**, `arm9/include/fifo_hook.h`).
2. The decoded v1 record is **lossy and homebrew-shaped** (one `TwlhdCmd`
   per logical call, 16 params max, 1024 commands per frame). It cannot hold
   a commercial game's per-frame command volume.

## 3. Capture backends

Per the architecture rule, the intermediate representation must not depend on
how it was captured. Candidate backends, with an honest feasibility read:

| # | Backend | Covers | DS software | Verifiable in this sandbox? | Risk |
|---|---|---|---|---|---|
| A | Call wrappers (**exists**) | only wrapped calls | libnds homebrew | build + host | incomplete by design |
| B | Register-write proxy shim | every GX port write in libnds homebrew, incl. all inline helpers | libnds homebrew, recompiled | build only | low–medium |
| C | Hardware readback augmentation | effective transform at draw time | any code *we* run | build only | low–medium |
| D | Display-list decoder | DMA/`glCallList` command streams | any | **host unit tests** | low |
| E | Emulator trace (offline) | full command stream from real games | commercial | no (no ROMs, external tool) | medium (licensing, effort) |
| F | On-device interception for commercial games | all | commercial | no (needs hardware + games) | **high** |

**A is insufficient — shown, not assumed.** The Phase 1 demo's
`gluPerspective`, `gluLookAtf32` and `glRotateXi` are invisible simply
because no wrapper was written for them. The obvious generic fix, link-time
interposition (`--wrap`), does not work either: `videoGL.h` defines 70
helpers as `GL_STATIC_INL` (`static inline`), including those three
(**LOCAL**, `videoGL.h` lines ~956, ~1046, ~1520), and none of them is an
exported symbol in `libnds9.a` (**LOCAL**: `arm-none-eabi-nm` on the archive
lists 211 text symbols, e.g. the `gl2d` functions, but not these). They are
compiled into the application, so there is no symbol to intercept.

**B — register-write proxy.** All GX ports are macros of the form
`#define GFX_VERTEX16 (*(vu32*)0x0400048C)` in `video.h` (**LOCAL**), and the
inline helpers write through them. A replacement for those macros (a small
C++ proxy with `operator=` that writes the register *and* records the
`(command id, value)` pair) would capture every libnds-issued command with no
per-function wrapper. Limits: needs recompilation against the shim, so it
covers homebrew only; reads (`GFX_STATUS`, `MATRIX_READ_*`) are left alone;
DMA display lists bypass it (see D). Feasibility of the macro-redefinition
trick across all of libnds's headers is **UNVERIFIED** and is the first
thing 4D-1 would prototype.

**C — hardware readback.** The DS exposes the current combined
projection×position matrix at `CLIPMTX_RESULT` (4000640h, 64 bytes) and the
directional matrix at `VECMTX_RESULT` (4000680h) (**GBATEK**). libnds already
wraps this as `glGetFixed(GL_GET_MATRIX_CLIP, …)`, which first spins on
`GFX_BUSY` (**LOCAL**). Snapshotting the clip matrix at each `BEGIN_VTXS`
would recover the effective transform **without tracking any matrix command**
— the fastest route to fix the biggest Phase 4A gap. Cost: a wait for the
geometry engine per draw; its frame-time impact is **UNVERIFIED** and must be
measured. It cannot recover state the game never leaves in the engine.

**D — display-list decoder.** Bulk geometry is sent by DMA in "geometry
command mode" to GXFIFO (**GBATEK**), invisible to any call-level hook. The
stream is *packed*: up to four 8-bit command ids per 32-bit word, followed by
the parameters of those commands in order (**GBATEK**). Because the
parameter count of every valid command is a public table (**GBATEK**), a
decoder can walk any such buffer. It is pure logic, so it can be written in
`common/` and **fully unit-tested on the host** with synthetic lists — the
one backend piece verifiable here. Invalid command ids are ignored by the
hardware and consume no parameters (**GBATEK**); the decoder must match that.

**E — emulator trace.** An instrumented open-source DS emulator can emit the
exact GX stream from real games, offline, which is the only realistic way to
test the format and renderer against *commercial* content before any
on-device work. Caveats, all **UNVERIFIED**: which emulator, whether its
geometry path is easy to tap, and licensing (common DS emulators are GPL;
the tracer would have to stay a separate tool emitting our format, never
linked into Apache-2.0 code). No ROMs are available here and none should be
assumed.

**F — on-device interception for commercial games.** Two ideas, both
high-risk and **UNVERIFIED**:

- *Trap writes with the ARM9 protection unit.* Everything in
  04000000h–04000FFFh (DMA, timers, keys, display, 3D ports) would have to
  fault, if the unit's minimum region is 4 KB as I recall for the ARM946E-S
  (not verified in this session — the GBATEK page name was not found). That
  would make every I/O access trap and require emulating the faulting
  store, including multi-word `STM`/`STRD` that GBATEK says may write the
  FIFO. Almost certainly too slow and fragile.
- *Patch the game's SDK graphics functions* (nds-bootstrap-style signature
  patching). Plausible for known SDK versions, but per-SDK, per-game work
  with no sample ROM to test against here.

**Recommendation:** do not attempt F until A→D prove the format and a
renderer on emulator-captured data. F also needs real hardware.

## 4. Proposed format v2

**Core decision: store the raw GX command stream, not decoded records.**
Rationale: lossless; has a 1:1 definition in GBATEK; covers unknown or future
command ids; and is what *every* backend (B, D, E, F) naturally produces, so
the renderer depends on one representation. Decoded convenience views are
derived by the host/ARM11 converter.

### 4.1 Layout (little-endian, 4-byte aligned)

```
0   u32 version        = 2          <- same offset as v1
4   u32 header_size
8   u32 total_size     (bytes, must equal file size)
12  u32 flags
16  u32 frame_index
20  u32 chunk_count
24  chunk[chunk_count]:  { u32 type (FourCC), u32 payload_size, u32 reserved, payload (padded to 4) }
```

Chunks (initial set, each optional unless noted):

| FourCC | Content | Source of truth |
|---|---|---|
| `GXCM` (required) | raw GX words: (cmd id, param) entries in issue order | GBATEK GXFIFO |
| `CLPM` | 16×s32 clip-matrix snapshots, tagged by command index | `CLIPMTX_RESULT` (backend C) |
| `TXDS` | texture descriptors keyed by VRAM offset/size/format | `TEXIMAGE_PARAM`/`PLTT_BASE` (GBATEK) |
| `TXDT` | texture pixel data | see 4.3 |
| `PLDT` | palette data (16-bit BGR555) | see 4.3 |
| `RSTA` | clear color/depth, fog, toon table, edge colors, alpha-test ref | rendering registers (GBATEK) |
| `META` | backend id, game id (opaque), capture notes | — |

### 4.2 Compatibility rules

- **v1 files stay valid and unchanged.** Parsers dispatch on `version`;
  v1 remains exactly the 73740-byte layout.
- **Old readers fail closed on v2 (LOCAL, verified by reading both).** The C
  parser rejects any size ≠ 73740 (`ERR_SIZE`) and any version ≠ 1
  (`ERR_VERSION`); `tools/twlhd_dump.py` rejects a version mismatch. So a v2
  file can never be silently misread as v1. Improvement to schedule with v2:
  the C parser checks size *before* version, so a v2 file is reported as
  "bad size" rather than "unsupported version"; read the version first when
  ≥4 bytes are available.
- **Within v2, unknown chunk types are skipped** (forward compatibility);
  unknown *required* semantics are signalled via `flags`, never guessed.
- **A v1 writer path is kept** behind a build flag so Phase 2 tooling and
  existing dumps keep working; `fifo_hook` and the v1 struct are not removed.
- `TWLHD_FORMAT_VERSION` in `common/twlhd_frame_format.h` stays 1 for v1
  consumers; v2 gets its own header so the locked v1 layout is not touched.

### 4.3 Bounds and sizing (assumptions to validate)

Every length is validated against `total_size` and a hard cap (proposed
1 MiB per file) with overflow-safe arithmetic, as the v1 parser already does.
Sizing is **UNVERIFIED**: texture VRAM is up to 512 KB in four 128 KB slots
and palettes up to 96 KB (**GBATEK** memory control); the command-volume
ceiling of a real frame must be measured before fixing a `GXCM` cap.

**Texture data is the hard part.** Descriptors are free (they are commands).
Pixels are not: texture VRAM "is not mapped to CPU address space" and must be
temporarily switched to plain-CPU (LCDC) mode to read (**GBATEK**). That is
disruptive to a running game. For homebrew the practical route is to snoop
the upload (`glTexImage2D` source buffer), not read VRAM back. For commercial
games it is an open problem tied to backend F. DS texture formats also need
conversion for PICA200: eight formats including a 3 bpp compressed 4×4 mode
and translucent A3I5/A5I3 (**GBATEK**), so the renderer needs a CPU-side
decode to RGBA.

## 5. Consequences for the renderer (Phase 4E and later, not 4D)

Once `GXCM` exists, `common/twlhd_vertex_convert` gains a real matrix-stack
emulator (pure C, host-testable) and the renderer a per-draw uniform for the
clip matrix, texture binding via the converted RGBA, and `POLYGON_ATTR`
mapped to cull/blend state. Lighting maps onto citro3d's `C3D_LightEnv` /
`C3D_Material` only approximately (different equations; **LOCAL** headers).
Exact DS rasterization (edge marking, toon shading, antialiasing,
translucent sorting rules) will not match PICA200 bit-for-bit; "faithful"
means geometry/texture/lighting parity within stated tolerances.

## 6. Proposed sub-phases (each ends at a gate)

| Step | Scope | Verification available here | Exit criterion |
|---|---|---|---|
| 4D-1 | v2 header + chunk parser + `GXCM` display-list decoder in `common/` (pure C); extend `tools/twlhd_dump.py`; v1 untouched | host C tests (ASan/UBSan) + Python cross-check | decoder reproduces known synthetic lists; v1 files still pass; malformed v2 rejected safely |
| 4D-2 | Backend B prototype (register proxy) + backend C clip-matrix snapshot, emitting v2 `GXCM`+`CLPM` from the Phase 1 demo | **build only** here; behavior needs an emulator/hardware run | demo capture contains its own projection/view/rotation; v1 vs v2 of the same scene compare equal on vertices |
| 4D-3 | Renderer consumes v2 (matrix emulation in converter, clip uniform) | host tests for matrix math; build for ARM11 | cube renders with the *original* camera on host reference rasterization |
| 4D-4 | Textures/lighting descriptors and conversion; backend E evaluation | host tests for decode | decided separately after 4D-3 |

Backend F is not scheduled.

## 7. What this proposal does **not** claim

- No part of B, C or F has been tried; B's feasibility and C's per-draw cost
  are open questions.
- Nothing here has run on a DS or 3DS. Hardware claims remain
  **HARDWARE UNVERIFIED**.
- No statement is made about any commercial game's behavior or SDK.
- GBATEK was read from the public pages fetched in this session; the
  ARM9 protection-unit granularity was not verified.

## 8. Decisions requested

1. **Direction:** approve the raw-`GXCM` v2 core, or prefer extending the
   decoded v1 records (simpler, but lossy and capped at 16 params/1024 cmds).
2. **Scope:** homebrew-only (A–D) for now, with E (emulator trace) as the
   commercial-compatibility research track, and F deferred — or a different
   priority.
3. **Real-time goal:** whether to keep pursuing live play (which needs a
   transport other than RTCom, or a 3DS-side execution architecture) or to
   treat this pipeline as the offline fidelity/validation vehicle (section 1).
4. **Licensing posture for E:** an external, separately licensed tracer that
   only emits our format — acceptable?
5. **Hardware availability:** whether a New 3DS/DS and test software will be
   available for the hardware-dependent steps (4D-2 behavior, any use of C
   and F).

## 9. Decision record (approved scope)

**Status of this section: APPROVED. The implementation is limited to the
scope below. Builds and host tests pass (BUILD VERIFIED / UNIT VERIFIED); all
runtime capture remains HARDWARE UNVERIFIED.**

The five decisions, recorded verbatim:

1. Direction: raw GX command stream (chunk GXCM) is the primary representation
   inside a versioned, chunked v2 container. v1 is preserved.
2. Scope: homebrew only. Capture backend = "register-write proxy shim"
   (backend B). Backends C (clip-matrix readback) and D (display-list decoder)
   are NOT implemented (their chunk FourCCs are only RESERVED in the spec).
   E (emulator trace) is not started and not decided. F (commercial-game
   interception) is out of scope.
3. Real-time transport is out of scope; the offline pipeline is the vehicle
   for this phase. Whether live play is ever pursued stays an OPEN question.
4. Licensing posture for an external emulator tracer: OPEN, no work.
5. No hardware and no DS emulator are available: everything runtime is
   HARDWARE UNVERIFIED.

### 9.1 What was implemented

| Piece | Files | Verification |
|---|---|---|
| Normative format | `docs/TWLHD_V2_FORMAT.md` | reviewed against the implementation |
| Constants + command table | `common/twlhd_v2_format.h` | C host tests, Python mirror |
| Zero-copy parser | `common/twlhd_v2_parse.{h,c}` | C host tests (ASan/UBSan), C↔Python corpus |
| Deterministic writer | `common/twlhd_v2_write.{h,c}` | golden vector, round trip, re-parse gate |
| Recorder core | `common/twlhd_gx_record.{h,c}` | port map, overflow, SWAP resolution |
| Backend B shim | `arm9v2/include/twlhd_gx_shim.hpp` | real libnds headers + mocked MMIO |
| Demo target | `arm9v2/` (`twlhd-arm9v2.nds`) | BUILD VERIFIED only |
| Host tools | `tools/twlhd_v2_dump.py`, `tools/test_twlhd_v2.py`, `tools/test_shim_coverage.py` | `make test` |
| Compat tests | `tests/host/test_v2_compat.c` | v1↔v2 fail-closed both ways |

### 9.2 Spike qualifications (backend B)

- **Intercepted:** every write-capable `GFX_*` / `MATRIX_*` scalar port and the
  `GFX_TOON_TABLE`, `GFX_EDGE_TABLE`, `GFX_FOG_TABLE` arrays. Because the
  libnds `gl*` helpers are `GL_STATIC_INL` in `videoGL.h`, they expand the
  redefined macros when the header is parsed. The shim must therefore be
  included **before** `<nds.h>` in the capture translation unit.
- **Not intercepted:** `glInit()` and any other function compiled into
  `libnds9.a`, because those object files were built against the original
  macros. `glCallList()` and DMA display lists bypass the proxy. This is why
  META advertises `LIBRARY_INTERNAL_WRITES_UNCAPTURED`,
  `DMA_DISPLAY_LISTS_UNCAPTURED` and `INITIAL_STATE_UNKNOWN`.
- **C++ required for the capture TU:** the proxy uses `operator=` /
  `operator|=` / `operator&=` / `operator[]`. The arm9v2 target is separate
  from `arm9/` so the v1 demo and its C-only build stay untouched.
- `arm-none-eabi-nm` on the demo ELF shows the recorder/hook symbols linked
  and **no undefined references** to `glRotatef32i`, `glMaterialf`,
  `glTexImage2D`, `glBindTexture`, `glTexParameter`, `glColorTableEXT`,
  `glTexCoord2f32`. That checked set exists as exported symbols in
  `libnds9.a`, so it is a meaningful negative check: those library calls are
  not used by the demo.
- Not supported by the shim: the packed display-list macros (`FIFO_*`,
  `REG2ID`). The proxy deletes `operator&`, so `REG2ID(GFX_*)` fails to
  compile instead of silently producing a host pointer value.
- `make test` now requires devkitPro: the shim test compiles real libnds
  headers with the host compiler, and the port-coverage test fails (not
  skips) when `DEVKITPRO` is unset.

### 9.3 What remains unimplemented

- Backend C (clip-matrix readback) and its `CLPM` chunk: **not implemented**.
- Backend D (display-list decoder) and the `TXDS`/`TXDT`/`PLDT` texture
  chunks: **not implemented**; textures and lighting capture are out of scope.
- Backend E (emulator trace): not started; licensing stays OPEN.
- Backend F (commercial-game interception): out of scope.
- Real-time transport: out of scope (decision 3).
- No renderer or RTCom changes. v1 files, readers and behavior are untouched.
- The demo, the shim's runtime behavior, libfat SD writes and real capture are
  HARDWARE UNVERIFIED: no DS and no emulator were available.

### 9.4 Verification labels

| Check | Label |
|---|---|
| `make` (arm7, arm9, arm9v2, arm11, arm11_ucode) and `make rtcom_rom` | BUILD VERIFIED |
| `make test` (C under ASan/UBSan, C++ shim against real libnds headers, Python agreement corpus) | UNIT VERIFIED |
| Demo shim runtime on a DS, libfat `/twlhd_frame_v2.bin` write, real capture contents | HARDWARE UNVERIFIED |

### Update (2026-10-29): decision 5 superseded

Decision 5 above said no DS emulator or hardware was available. Since then the project
owner supplied DS/3DS emulator source trees; DeSmuME's CLI was built out of tree and our
`arm9v2` demo was run in it (see `docs/EMULATOR_VALIDATION.md`). Real DS/3DS **hardware is
still unavailable for the capture work**; a New 3DS exists on the owner's network and is the
subject of the staged plan in `docs/HARDWARE_VALIDATION_3DS.md` and
`docs/HARDWARE_TEST_PROCEDURE_3DS.md` (preparation only; nothing deployed).
