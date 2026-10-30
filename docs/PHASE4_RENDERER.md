# Phase 4B/4C — Minimal PICA200 Debug Renderer

**Status: BUILD VERIFIED / UNIT VERIFIED / HARDWARE OBSERVED for the built-in triangle and SD frames up to 900 vertices (see `PHASE4_HARDWARE_FINDINGS.md`); everything else remains HARDWARE UNVERIFIED.** The code
below builds with devkitARM + libctru + citro3d and its host-side modules pass
deterministic tests, but the ARM11 application has never been executed on a
3DS in this project. Nothing here has been compared against real DS output.

This phase implements the minimal prototype proposed in
`docs/PHASE4A_PICA200_FEASIBILITY.md` §4. It is a **debug view, not a
reconstruction**: Phase 4A established that the captured `TwlhdFrame` contains
no projection/view/model matrices, no texture data, no lighting/material state
and no polygon attributes, so the renderer substitutes a hardcoded camera and
flat per-vertex color.

## 1. Three layers

| Layer | Files | Role |
|---|---|---|
| Wire parser | `common/twlhd_frame_parse.h`, `.c` | Decode + validate the 73,740-byte `TwlhdFrame` layout field-by-field. |
| Vertex converter | `common/twlhd_vertex_convert.h`, `.c` | Turn the decoded command stream into a non-indexed `TwlhdVertex` triangle list. |
| Renderer / app | `arm11/source/renderer.c`, `arm11/include/renderer.h`, `arm11/source/main.c`, `arm11/shaders/twlhd_vshader.v.pica` | Upload the triangle list to the PICA200 via citro3d and draw it on the top screen. |

The `common/` modules are pure C11 with no libctru/libnds headers. They compile
both under host `cc` and under devkitARM, and are the only code exercised by the
test suite.

## 2. Data flow

```
sdmc:/twlhd_frame.bin
   │  fopen / exact-size check / fread (73,740 bytes)
   ▼
twlhd_frame_parse()  → TwlhdParsedFrame (pointer views, no copies)
   │
   ▼
twlhd_convert_frame() → TwlhdVertex[]  (12 floats: pos xyz, rgba, n xyz, uv)
   ▼
renderer_set_geometry() → stage pending linearAlloc copy
   │
   ▼
renderer_draw() → commit pending→active + BufInfo_Add + uniforms + draw
```

`main.c` keeps the Phase 1 `rtcom_transport_init()` call and status string.
The console lives on the bottom screen; the top screen is the render target.

## 3. Fixed-point decode

The converter follows GBATEK's DS 3D I/O ports section and the packing used by
`arm9/source/fifo_hook.c`.

| Command | Bits | Decode |
|---|---|---|
| `VTX_16` | `params[0]` low 16 = x, high 16 = y; `params[1]` low 16 = z | signed 16-bit, 1.3.12 → `/4096.0f` |
| `COLOR` | bits 0–4 r, 5–9 g, 10–14 b | `r,g,b / 31.0f`; alpha = 1 |
| `NORMAL` | x bits 0–9, y bits 10–19, z bits 20–29 | signed 10-bit → `/512.0f` |
| `TEX_COORD` | u low 16, v high 16 | signed 16-bit, 12.4 → `/16.0f` |

`NORMAL` is not clipped: the cube's `-1.0` packs as `0x201`, which decodes to
`-511/512 ≈ -0.998`, exactly what the DS-side packing produces.

`COLOR`, `NORMAL` and `TEX_COORD` follow the DS "current attribute" model:
they update latched state, and every following `VTX_16` copies that state.
Defaults are white `{1,1,1,1}`, normal `{0,0,0}` and UV `{0,0}`. A `VTX_16`
outside `BEGIN_VTXS..END_VTXS` is ignored and counted.

## 4. Primitive handling

`BEGIN_VTXS` modes are expanded to an unindexed triangle list:

| Mode | Meaning | Expansion |
|---|---|---|
| 0 | triangles | every 3 vertices → 1 triangle |
| 1 | quads | vertices `(0,1,2,3)` → `(0,1,2)` + `(0,2,3)` |
| 2 | triangle strip | alternating winding, vertex *n* with the previous two |
| 3 | quad strip | quad *k* = `(2k, 2k+1, 2k+3, 2k+2)` → `(2k,2k+1,2k+3)` + `(2k,2k+3,2k+2)` |

**Quad-strip winding check.** `docs/GEOMETRY_FIFO.md` does not pin the winding,
so this was checked against the GBATEK `BEGIN_VTXS`/quad-strip description and
the OpenGL quad-strip convention: vertices arrive column-paired, and each new
pair completes a quad with the previous pair. The order above keeps the two
triangles consistently wound. A trailing incomplete primitive is dropped and
counted in `dropped_incomplete`.

Unsupported commands are counted, never guessed at: all `MTX_*` commands
(`unsupported_matrix`), `VIEWPORT` (`unsupported_viewport`), the
`VTX_10/XY/XZ/YZ` formats (`unsupported_vertex`), and unknown types
(`unsupported_unknown`). Known-type `param_count` mismatches are rejected by
the parser with `ERR_PARAM_COUNT`; unknown types with a valid length pass
through so they can be counted downstream.

Capacity is caller-owned. `twlhd_convert_vertex_bound()` returns a conservative
`3 * cmd_count`; the converter returns `ERR_CAPACITY` rather than overflowing,
keeping partial output for diagnostics.

## 5. Renderer details

Initialization order is `C3D_Init` → render target (400×240, RGBA8 +
depth24/stencil8, top/left) → `DVLB_ParseFile` → `shaderProgramInit` →
`shaderProgramSetVsh` → uniform lookup → attribute loaders (v0 = 3×float
position, v1 = 4×float color, stride 48) → texenv pass-through of
`GPU_PRIMARY_COLOR` → depth test (`GPU_GREATER`, write all) → cull **off** for
debug visibility. The 48-byte vertex stride with only 28 bytes of loaded
attributes (position + color) is expected to be valid because `BufInfo_Add`
lets the caller define the stride independently of the attribute loaders, but
this is HARDWARE UNVERIFIED. Each failure jumps to one unwind path; `renderer_shutdown()`
frees in reverse order, is idempotent and is safe after a partial init.
`gfxExit()` stays the caller's responsibility.

Geometry replacement is deferred: `renderer_set_geometry()` only stages a new
`linearAlloc` buffer as *pending*; it never waits on the GPU and never frees
the *active* buffer that may still be read by the frame in flight. The pending
buffer is promoted to active (and the old active is freed) at the start of the
next `renderer_draw()`, which by contract runs between a successful
`C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` and `C3D_FrameEnd()`. `C3D_FrameBegin`
calls `C3Di_WaitAndClearQueue(-1)`, which upstream citro3d
(`renderqueue.c`/`base.c`, read from the devkitPro/citro3d repository)
guarantees blocks until the previous frame's GX command queue has finished;
when it returns, the GPU is no longer reading the previous frame's vertex
buffer. `C3D_RenderTargetDelete()` and `C3D_Fini()` also drain the queue before
returning, so `renderer_shutdown()` deletes the render target before freeing
the active buffer. A zero-vertex `renderer_set_geometry()` clears the pending
buffer and sets the drawn vertex count to 0, so nothing is drawn. The previous
active buffer is not freed at that point: it stays allocated (unused) until the
next non-empty commit or `renderer_shutdown()`. This is not a leak, and it is
safe because the buffer is never read while the vertex count is 0.
These synchronization semantics were reasoned from upstream source and are
**HARDWARE UNVERIFIED** — they were never executed on a 3DS in this project.

The vertex shader (`arm11/shaders/twlhd_vshader.v.pica`) is an original
minimal MVP pass-through: `modelview` then `projection`, plus the interpolated
per-vertex color. It is compiled by `picasso` at build time into
`arm11/build/twlhd_vshader.shbin` and embedded via `bin2o`; the linked ELF
contains `twlhd_vshader_shbin`.

### DEBUG camera

`Mtx_PerspTilt` at 45° with `C3D_AspectRatioTop` and a fixed look-at whose
distance is derived from the converted geometry's AABB (`radius * 2.8` along a
fixed normalized direction). The camera exists only to put something on
screen. It has no relationship to the DS's real `gluPerspective`/`gluLookAt`
state, which is not captured (Phase 4A §1.3). Both the code and the on-screen
console label it `DEBUG CAMERA`.

## 6. SD-card workflow (4C)

Phase 3 established that RTCom cannot carry a full frame, so playback is
file-based:

1. On the DS, the Phase 1 demo writes `/twlhd_frame.bin` to its SD card when
   A is pressed (`arm9/source/main.c`).
2. Copy that file to `sdmc:/twlhd_frame.bin` on the 3DS SD card.
3. Launch `arm11/twlhd-arm11.3dsx`.

Controls: **A** loads the SD frame, **Y** restores the built-in deterministic
triangle, **START** exits. A file must be exactly 73,740 bytes; the size is
checked before parsing. Missing file, short read, parse error and conversion
error each produce a specific status line and leave the previous scene
untouched. A valid zero-triangle frame is accepted and reported as such.

## 7. What is intentionally not rendered

Per `docs/PHASE4A_PICA200_FEASIBILITY.md` §1.3–1.4 and §4:

- **Matrices.** Projection/view/model transforms are never captured. `MTX_*`
  commands only record mode/push/pop/identity in practice; the demo's real
  transforms bypass the wrappers. Hence the fixed DEBUG camera.
- **Textures.** No pixel/palette upload or texture parameter is captured, so
  UVs are decoded and carried but never sampled.
- **Lighting/material state.** Not captured; normals are carried but unused.
- **Polygon attributes, clear color, fog, toon table, shading mode.**
- **`VIEWPORT`.** Counted but not applied; the target is the full top screen.
- **`VTX_10/XY/XZ/YZ` and direct matrix loads.** Defined in the wire format but
  no capture wrapper exists; they are counted as unsupported.
- **Raw-register / display-list / DMA geometry.** Outside the hook's reach.

The result is colored, unlit, object-space geometry under an invented camera.
That is expected and labeled; it is **not** a faithful DS reconstruction.

## 8. Tests

```sh
make test        # host C tests + Python suite
make host-test   # host C tests only
```

- `tests/host/test_parser.c` — `ERR_NULL`, truncation lengths, exact-size rule,
  version 0/2, `cmd_count` 1024/1025, `param_count` 17, known-type mismatch,
  unknown-type tolerance, offset decoding, result strings.
- `tests/host/test_converter.c` — fixed-point exacts (`0x1000 → 1.0`,
  `0xF000 → -1.0`, sign-extended z), RGB15/normal/UV min–max, attribute
  defaults, outside-`BEGIN` counting, every primitive mode (including
  quad-strip ordering), incomplete drops, `ERR_CAPACITY`, empty/degenerate
  bounds.
- `tests/host/test_roundtrip.c` — the steady-state cube frame (40 commands,
  24 `VTX_16`) converts to 12 triangles.
- `tests/host/test_geometry_slots.c` — active/pending buffer ownership:
  stage never frees active, commit frees old active exactly once, zero-length
  stage clears, allocation failure preserves existing buffers, idempotent
  release, no double free or leak under ASan.
- `tools/test_c_parser_consistency.py` — compiles `tests/host/dump_summary.c`
  and compares accept/reject decisions and per-type counts against
  `tools/twlhd_dump.py` for a valid frame, a truncated file, a wrong version
  and `cmd_count = 1025`.

The host C tests build with `-fsanitize=address,undefined` when the host
compiler supports it. The Python suite runs with `PYTHONDONTWRITEBYTECODE=1`.

## 9. Verification actually performed

| Check | Result | Label |
|---|---|---|
| `make clean && make` | exit 0; arm7, arm9, arm11, arm11_ucode built | BUILD VERIFIED |
| `make rtcom_rom` | exit 0; `twlhd-rtcom.nds` produced | BUILD VERIFIED |
| `make test` | exit 0; host C tests + Python tests pass | UNIT VERIFIED |
| `picasso` shader build | `arm11/build/twlhd_vshader.shbin` produced | BUILD VERIFIED |
| Shader embedded | `nm arm11/twlhd-arm11.elf` shows `twlhd_vshader_shbin` | BUILD VERIFIED |
| GPU synchronization / deferred commit | reasoned from upstream citro3d source; no hardware run | HARDWARE UNVERIFIED |

A runtime-only fault (for example a wrong uniform table index or a citro3d
state assumption that differs on hardware) remains possible and is why every
renderer failure path prints a specific error instead of silently continuing.
