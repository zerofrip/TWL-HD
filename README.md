# TWL-HD

TWL-HD is a research project investigating whether Nintendo DS 3D content can be
captured on the ARM9 and re-rendered at higher resolution on the DSi's ARM11
PICA200. It contains an ARM9 GXFIFO capture hook plus a rotating-cube demo that
proves the capture path works inside a regular DS title, and a Phase 4B/4C
ARM11 debug renderer that reads a dumped frame from the SD card and draws it
with citro3d using a fixed DEBUG camera. There is still no faithful
reconstruction (the capture contains no matrices or textures) and no
compositing; see `docs/PHASE4_RENDERER.md`.

## Repository layout

| Path | Description |
|------|-------------|
| `common/twlhd_frame_format.h` | Shared capture frame wire format (ARM9 and ARM11) |
| `arm9/` | NDS application (`twlhd-arm9.nds`): GXFIFO hook + demo |
| `arm11/` | 3DS application (`twlhd-arm11.3dsx`): Phase 4B/4C debug renderer + transport stub |
| `docs/` | Architecture roadmap and GXFIFO reference notes |
| `tools/` | Host-side dump analysis (`twlhd_dump.py`, Python 3, stdlib only) |

## Prerequisites

devkitPro with the NDS and 3DS toolchains:

```sh
dkp-pacman -S nds-dev 3ds-dev general-tools
```

and the usual environment variables:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
```

The Makefiles stop with a readable error when `DEVKITARM`, `DEVKITPRO`, `libnds`
or `libctru` are missing, so a half-installed toolchain never produces an empty
output file.

## Build

```sh
make            # both targets
make -C arm9    # NDS only   -> arm9/twlhd-arm9.nds
make -C arm11   # 3DS only   -> arm11/twlhd-arm11.3dsx
make test       # host C tests + Python tests (no devkitPro needed)
make clean
```

## Testing

1. Build `arm9/twlhd-arm9.nds`.
2. Load it in DeSmuME or melonDS (a DSi-capable build is recommended, the hook
   itself is plain DS hardware).
3. Expected result: a spinning six-face cube on the top screen and, on the
   bottom screen, a statistics console similar to:

```
TWL-HD Phase 1 capture
format v1  frame 59
commands 40 / 1024
VTX_16   24
COLOR    6
NORMAL   6
VIEWPORT 0
```

The counters show what the hook decoded from the wrapped 3D calls during the
last frame; they update once every 60 frames. One-shot setup commands
(`VIEWPORT`, the projection matrix) are issued once and are therefore not part
of the steady-state counts.

`arm11/twlhd-arm11.3dsx` can be launched with a 3DS homebrew launcher. It
starts with a deterministic built-in triangle; copy a `twlhd_frame.bin` dump to
`sdmc:/twlhd_frame.bin`, press A to load it, Y to restore the built-in
triangle, and START to exit. The bottom screen lists parse/conversion counts
and always labels the view `DEBUG CAMERA` / `HARDWARE UNVERIFIED (build-only)`.
This debug playback is not a faithful DS reconstruction; see
`docs/PHASE4_RENDERER.md`.

`make test` runs the host-side parser/converter/round-trip C tests (ASan/UBSan
when the host compiler supports them) and the Python tooling suite, including a
C-to-Python parser consistency check.

## Known limitations

**The ARM11 RTCom bridge is NOT functional in this release - it is a Phase 3
work item.** The ARM9 application never talks to the ARM11; the transport
functions in `arm11/source/rtcom_transport.c` return
`TWLHD_ERR_NOT_IMPLEMENTED`, and `fifo_hook_transport_send()` is a weak no-op.
Capture is also limited to the thin wrappers in `arm9/include/fifo_hook.h`;
3D calls issued directly against libnds (for example `glRotateXi`) are not
recorded. See `docs/ARCHITECTURE.md` for the full roadmap.
