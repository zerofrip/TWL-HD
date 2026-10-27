# Architecture

TWL-HD is split into five phases. Only Phase 1 exists in this repository; the
later phases are described here so the capture format and the build layout are
not changed later in incompatible ways.

## Phase roadmap

| Phase | Title | Scope |
|-------|-------|-------|
| 1 | GXFIFO Capture | ARM9 hook library + rotating-cube demo (`.nds`), this release |
| 2 | Analysis Tooling | Host-side tooling to parse captured frames (dump, diff, statistics) |
| 3 | RTCom Transport | Wire `TwlhdFrame` over RTCom from ARM9 to ARM11 |
| 4 | PICA200 Re-render | Translate captured DS geometry into PICA200 commands on ARM11 |
| 5 | Compositing | Composite the high-resolution result back into the displayed frame |

### Phase 1 - GXFIFO Capture

`arm9/source/fifo_hook.c` wraps the libnds 3D calls that map 1:1 onto GXFIFO
commands (`glBegin`, `glEnd`, `glVertex3v16`, `glColor3b`, `glNormal`,
`glTexCoord2t16`, `glMatrixMode`, `glPushMatrix`, `glPopMatrix`,
`glLoadIdentity`, `glViewport`). Every call is forwarded to the real libnds
function and then decoded into `TwlhdFrame` (`common/twlhd_frame_format.h`).
The demo in `arm9/source/main.c` draws a rotating cube and prints per-frame
command statistics once every 60 frames.

### Phase 2 - Analysis Tooling

The frame format is already fixed-width and endian-explicit (32-bit words only),
so a host tool can parse a dump without the DS toolchain. Not in this release.

### Phase 3 - RTCom Transport

The capture hook exposes `fifo_hook_transport_send()` as a weak symbol and its
default body does nothing. Phase 3 overrides it with an RTCom-backed
implementation, and the ARM11 side replaces the stub in
`arm11/source/rtcom_transport.c`. Until then both ARM11 functions return
`TWLHD_ERR_NOT_IMPLEMENTED` and `arm11/source/main.c` reports "not functional".

### Phase 4 - PICA200 Re-render

Parse the captured command stream on the ARM11 and rebuild the draw calls with
citro3d/PICA200 at a higher resolution. Matrix stacks and vertex formats have to
be re-interpreted, not merely replayed, because the two GPUs encode commands
differently.

### Phase 5 - Compositing

Blend the high-resolution render into the frame the system displays, preserving
the DS's own 2D layers and touch-screen behaviour.

## Hardware constraints

- **TWL_FIRM isolation.** A DS/DSi title runs inside TWL_FIRM. The ARM11 and the
  PICA200 are not accessible to the DS-side code; the ARM9 cannot issue PICA200
  commands at all, and the ARM11 cannot read the ARM9's 3D registers.
- **No direct PICA200 access from a DS title.** The 3D core visible to the DS
  side is the legacy DS rasteriser. Any high-resolution rendering has to happen
  on the ARM11 in the 3DS application.
- **RTCom is the only viable channel.** The two cores already exchange messages
  over RTCom for system services; it is the only supported transport available
  to homebrew on both sides of TWL_FIRM. That is why Phase 3 is a hard
  prerequisite for Phases 4 and 5.
- **Capture cost matters.** The hook records into a 72 KiB static frame buffer
  (1024 commands). Recording on every draw call would perturb frame timing, so
  capture is explicitly started and stopped, and drops commands once the frame
  is full.
- **No MMIO interception.** devkitARM/libnds does not expose a hook for writes to
  `GFX_FIFO`; the wrappers in `fifo_hook.c` are the supported interception
  point. Commands issued through unwrapped libnds helpers are invisible to the
  hook (a known Phase 1 limitation).
