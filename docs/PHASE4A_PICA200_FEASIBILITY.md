# Phase 4A — PICA200 Offline Renderer Feasibility Audit

**Status: research and architecture planning only. No rendering code, no
GPU translation, no RTCom transport changes were made for this phase.**

This document audits whether the Phase 1/2 `TwlhdFrame` capture format
contains enough information to reconstruct DS 3D rendering on the New
3DS's PICA200 GPU via libctru/citro3d, and proposes the smallest next step
if it does not. Per the Phase 3 audit (`docs/RTCOM_AUDIT.md`), a full
`TwlhdFrame` cannot be streamed live over RTCom — any renderer in this
phase or later necessarily consumes a **file previously dumped to the SD
card** (`fifo_hook_dump_to_file()`, already shipped in Phase 1/2), not a
live RTCom feed. This document does not revisit that conclusion and does
not touch `arm7/`, `arm11_ucode/`, or `arm11/source/rtcom_transport.c`.

## 1. TwlhdFrame content audit

Source: `common/twlhd_frame_format.h`, `arm9/source/fifo_hook.c`,
`arm9/include/fifo_hook.h`.

### 1.1 What is captured

Only commands issued through the thin wrappers in `fifo_hook.h` are
recorded. The full wrapped set, with no omissions, is:

| Wrapper | GXFIFO command | Carries |
|---|---|---|
| `fifo_hook_glBegin` / `_glEnd` | `BEGIN_VTXS` / `END_VTXS` | primitive type |
| `fifo_hook_glVertex3v16` | `VTX_16` | one vertex, 16-bit fixed point |
| `fifo_hook_glColor3b` | `COLOR` | per-vertex RGB15 |
| `fifo_hook_glNormal` | `NORMAL` | packed normal |
| `fifo_hook_glTexCoord2t16` | `TEX_COORD` | per-vertex UV |
| `fifo_hook_glMatrixMode` | `MTX_MODE` | which matrix stack |
| `fifo_hook_glPushMatrix` / `_glPopMatrix` | `MTX_PUSH` / `MTX_POP` | stack depth |
| `fifo_hook_glLoadIdentity` | `MTX_IDENTITY` | — |
| `fifo_hook_glViewport` | `VIEWPORT` | screen rect |

### 1.2 What is defined in the format but never actually captured

`TwlhdCmdType` and the parameter-count table in `fifo_hook.c` also list
`VTX_10`, `VTX_XY`, `VTX_XZ`, `VTX_YZ`, and the matrix `STORE` / `RESTORE`
/ `LOAD_4x4` / `LOAD_4x3` / `MULT_4x4` / `MULT_4x3` / `MULT_3x3` commands —
but **no wrapper function exists for any of them.** If a future demo uses
these DS vertex-compression formats or direct matrix loads, nothing
records them; they are formally part of the wire format and entirely
theoretical in the current capture path.

### 1.3 What is never captured at all — demonstrated, not inferred

`arm9/source/main.c` (this project's own Phase 1 reference demo) issues
all of the following through **unwrapped** libnds calls:

```
arm9/source/main.c:127-132  glInit(); glEnable(GL_ANTIALIAS);
                             glClearColor(0,0,0,31); glClearPolyID(63);
                             glClearDepth(GL_MAX_DEPTH);
arm9/source/main.c:141      gluPerspective(70, 256.0/192.0, 0.1, 100.0);
arm9/source/main.c:146      gluLookAtf32(0,0,inttof32(4), 0,0,0, 0,inttof32(1),0);
arm9/source/main.c:147-148  glRotateXi(angle); glRotateYi(angle);
```

None of `glInit`, `glEnable`, `glClearColor`, `glClearPolyID`,
`glClearDepth`, `gluPerspective`, `gluLookAtf32`, `glRotateXi`,
`glRotateYi` have a `fifo_hook_*` wrapper. This is not a hypothetical gap:
**the project's own demo's projection matrix, view matrix, and object
rotation are invisible to every capture this project has ever produced.**
A dumped frame today contains object-space vertex positions with COLOR
and NORMAL, bracketed by `MTX_MODE`/`MTX_IDENTITY` pairs that carry no
actual transform — there is no projection, no view, and no model
transform in the data at all.

Also never captured, by the same "no wrapper exists" rule:

- **Texture data and setup**: no `TEXIMAGE_PARAM`, no `TEXPLTT_BASE`, no
  texture pixel/palette upload path exists anywhere in `arm9/`. `TEX_COORD`
  is captured, but there is no texture bound to interpret it against.
- **Polygon attributes**: `POLYGON_ATTR` (lighting enable, alpha, cull
  mode, render mode) is not a `TwlhdCmdType` at all.
- **Material / lighting state**: `LIGHT_VECTOR`, `LIGHT_COLOR`,
  `DIFFUSE_AMBIENT`, `SPECULAR_EMISSION`, `SHININESS` are not
  `TwlhdCmdType` values.
- **Clear color/depth, fog, toon table, shading mode.**
- **Any raw-register / display-list / DMA-driven geometry** — confirmed
  existing limitation, `docs/ARCHITECTURE.md` "No MMIO interception".

### 1.4 Verdict

**The current `TwlhdFrame` format does not contain sufficient information
to reconstruct DS 3D rendering**, including for this project's own
reference demo. What it reliably contains today is: an ordered stream of
untextured, unlit, object-space triangles/quads with per-vertex color and
normal, and a viewport rectangle. That is renderable as "colored wireframe
soup in object space" and nothing closer to the original image.

## 2. PICA200 rendering via libctru/citro3d — investigated in this
environment

Checked directly against the installed toolchain
(`/opt/devkitpro/libctru/include/c3d/*.h`, `libctru 2.7.0-1`,
`citro3d 1.7.1-2`, `citro2d 1.7.0-1`, all confirmed installed via
`pacman -Q`) rather than assumed from memory:

- **No fixed-function vertex transform exists.** Unlike the DS's
  fixed-function `MTX_MULT`/`MTX_LOAD` pipeline, PICA200 vertex transform
  on citro3d is done entirely by a programmer-supplied vertex shader
  (`.vsh` source, compiled offline to a `.shbin` by the `picasso` tool —
  confirmed present and runnable at `/opt/devkitpro/tools/bin/picasso`,
  MIT-licensed per its bundled `/opt/devkitpro/licenses/picasso/COPYING`).
  A minimal MVP-multiply passthrough vertex shader is a well-known,
  reusable, one-time asset (the same handful of lines every citro3d
  tutorial/sample starts from) — this is a fixed cost, not a per-draw-call
  translation problem.
- **Texture environment (`c3d/texenv.h`, `C3D_TexEnv`)** is PICA200's
  fixed-function fragment/texture-blend stage — a reasonable conceptual
  analog to the DS's texture blending, configurable without a shader.
  Cannot be exercised meaningfully yet since no texture data is captured
  (§1.3).
- **Lighting (`c3d/light.h`, `C3D_Material` / `C3D_LightEnv`)** is a fixed
  -function-ish lighting block (ambient/diffuse/specular0/specular1/
  emission, driven through LUTs) — a conceptual analog to the DS's light
  vector + material model, but NOT a bit-exact match to the DS lighting
  equation; any reconstruction would be an approximation. Also moot until
  light/material state is captured (§1.3) — not captured today.
- **Vertex attributes (`c3d/attribs.h`, `C3D_AttrInfo`)**: straightforward
  mapping from the captured position/color/normal/texcoord streams to
  PICA200 vertex attribute registers, once a projection/view matrix exists
  to feed the shader.

**Licensing**: libctru, citro3d, and citro2d are publicly documented
(upstream devkitPro GitHub repositories) as zlib-licensed — the same
permissive license already relied on for `libnds` in `arm9/`/`arm11/` in
this repo without issue, and compatible with linking into Apache-2.0 code.
No bundled license file for these three packages was found locally in
this environment (`pacman -Qi` reports `Licenses: custom` with no local
text) — this should be confirmed against the upstream repositories before
any public release claim, but is not expected to be a blocker.

## 3. Can the existing `arm11/` `.3dsx` be reused?

**The build target can be reused; the rendering content cannot, because
there currently is none.**

`arm11/source/main.c` today only calls `gfxInitDefault()` +
`consoleInit(GFX_TOP, NULL)` and prints status text in a `aptMainLoop()`
loop — there is no `C3D_Init`, no render-target setup, no shader, no
vertex buffer, nothing 3D at all. The Makefile, `.3dsx` packaging, and
basic app-lifecycle loop are reusable scaffolding; the actual PICA200
rendering code (§2) would be entirely new.

`arm11/source/rtcom_transport.c` remains the honest "not implemented" stub
from Phase 1 and is **not** modified or referenced by this audit, per
Phase 3's conclusion that RTCom cannot carry a `TwlhdFrame` and per this
task's explicit instruction not to touch RTCom transport. Any renderer
built on top of `arm11/` must read a **file from the SD card**
(`sdmc:/`, via libctru's filesystem API — the 3DS homebrew equivalent of
the libfat SD access `arm9/source/fifo_hook.c` already uses on the DS
side), using the exact same `common/twlhd_frame_format.h` struct layout
Phase 2's `tools/twlhd_dump.py` already parses, including rejecting a
format-version mismatch the same way that tool does.

## 4. Minimal offline rendering prototype — proposed, not implemented

If/when this phase proceeds to implementation, the smallest honest
prototype is:

1. Extend `arm11/` (same build target) to open a `TwlhdFrame` dump copied
   onto the 3DS's SD card (e.g. `sdmc:/twlhd_frame.bin`, the same filename
   `arm9/source/main.c`'s A-button dump already writes on the DS side —
   the file would need to be moved from one SD card/session to the other
   by the user, since there is no live transport) and validate its format
   version exactly like `tools/twlhd_dump.py` does, rejecting a mismatch
   with a clear on-screen error rather than guessing.
2. Add one reusable debug vertex shader (MVP passthrough, built once via
   `picasso`) and configure a basic `C3D_TexEnv` for flat per-vertex color
   (no texture — none is captured).
3. Feed the captured `COLOR`/`NORMAL`/`VTX_16` stream into a PICA200
   vertex buffer via `C3D_AttrInfo`, drawing whatever primitive type the
   captured `BEGIN_VTXS` records.
4. Because no real projection/view/model matrix exists in the capture
   (§1.3), the prototype would have to substitute a **hardcoded debug
   camera** (e.g. a fixed perspective + fixed eye position) just to get
   something on screen — and this must be labeled on-screen and in code
   comments as a debug substitution, not a reconstruction of the original
   DS camera. The rendered result would be recognizable geometry in the
   wrong apparent position/orientation relative to how it looked on the
   DS, which is an expected and honestly-labeled limitation, not a bug to
   hide.
5. Lighting and texturing stay out of scope for this minimal prototype,
   since neither is captured.

This prototype would demonstrate the PICA200 rendering plumbing works
end-to-end on a real capture file, while making unmistakably clear (on
screen and in docs) that it is not yet a faithful reconstruction.

## 5. The actual architectural decision point

**Rendering anything resembling the original DS frame requires extending
the capture format first — a Phase 1 change, not a Phase 4 one.** At
minimum, `arm9/source/fifo_hook.c` would need new wrappers for
`gluPerspective`/`gluLookAtf32` (or whatever the demo/game actually calls)
plus `glRotateXi`/`glRotateYi`/`glTranslatef`-equivalents and real texture
upload, and `common/twlhd_frame_format.h`'s locked wire layout
(`TWLHD_FORMAT_VERSION`) would need a version bump — a change
`docs/ARCHITECTURE.md`'s Phase 1 section explicitly says should not happen
"later in incompatible ways" without deliberate sign-off, since Phase 2's
host tooling and any future Phase 3/4 consumers depend on that layout
staying stable.

This is the decision this audit stops at: whether to (a) extend the
Phase 1 capture format now (bumping `TWLHD_FORMAT_VERSION`, re-touching
`arm9/` and `tools/twlhd_dump.py`) to actually capture projection/view/
model matrices and texture data before any renderer is worth building, or
(b) build only the debug-camera prototype from §4 first, accepting its
honestly-labeled limitations, as a smaller proof that the PICA200
rendering path itself works before investing in capture-format changes.
Both are reasonable; this document does not choose for you.

## Summary

- `TwlhdFrame` today: insufficient for DS reconstruction, confirmed with
  evidence from this project's own demo source, not conjecture.
- PICA200/citro3d: rendering is feasible in principle; requires a
  one-time custom vertex shader (no fixed-function transform exists);
  texturing and lighting are both blocked on capture-format gaps, not on
  PICA200 capability.
- `arm11/` `.3dsx`: build scaffold is reusable; 3D rendering content is
  entirely new work.
- Licensing: zlib (libctru/citro3d/citro2d) is expected compatible with
  Apache-2.0, consistent with this project's existing libnds dependency;
  not locally verified against a bundled license file in this session.
- RTCom transport: untouched, not revisited, not in scope.
- No speculative GPU translation was implemented for this audit.
