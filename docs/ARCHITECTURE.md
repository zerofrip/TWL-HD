# Architecture

TWL-HD is split into five phases. Only Phase 1 exists in this repository; the
later phases are described here so the capture format and the build layout are
not changed later in incompatible ways.

## Phase roadmap

| Phase | Title | Scope |
|-------|-------|-------|
| 1 | GXFIFO Capture | ARM9 hook library + rotating-cube demo (`.nds`), this release |
| 2 | Analysis Tooling | Host-side tooling to parse captured frames (dump, diff, statistics) |
| 3 | RTCom Transport | ARM7 driver (`arm7/`) + ARM11 ucode blob (`arm11_ucode/`) |
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

Phase 3 has two halves that must ship together:

- `arm7/` — an ARM7-side RTCom driver linked into the DS title binary. It owns
  the 3-wire RTC bit-bang, the handshake with the TwlBg RTCom service, and the
  one-time upload of the ARM11 payload.
- `arm11_ucode/` — a bare-metal ARM11 ucode blob (base `0x00201000`, 60 KiB
  window) uploaded at runtime into the already-running TwlBg.cxi process. It is
  not a standalone executable and has no `.3dsx` form.

The existing `arm11/` libctru application is the Phase 4/5 renderer. It is
launched independently from the home menu and can never be the receiving end of
RTCom traffic; `arm11/source/rtcom_transport.c` is therefore not the Phase 3
transport endpoint. Bandwidth is analysed in `docs/RTCOM_AUDIT.md`; carrying a
full `TwlhdFrame` over RTCom is not feasible, which is why Phase 3D changes the
transfer model before Phase 4 consumes it.

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
