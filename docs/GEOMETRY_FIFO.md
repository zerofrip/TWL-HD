# DS GXFIFO command encoding

Reference notes for the capture format in `common/twlhd_frame_format.h`.
Command numbers and parameter layouts follow GBATEK's "DS 3D I/O Ports"
section (<https://problemkaputt.de/gbatek.htm#ds3dio>). GBATEK is cited as the
source; its text is not reproduced here.

## Packed command stream

The DS 3D core accepts packed command words at `0x04000400` (`GFX_FIFO`, see
`nds/arm9/video.h`). One 32-bit header word can carry up to four command bytes:

```
bits  7..0   first command
bits 15..8   second command
bits 23..16  third command
bits 31..24  fourth command
```

The parameter words for all commands packed into that header follow
immediately, concatenated in command order. A command may also be issued alone
by writing to the register alias for its command byte; both forms have the same
effect on the FIFO. libnds' `gl*` helpers use the register aliases, for example
`glVertex3v16()` writes `GFX_VERTEX16` (`0x0400048C`, command `0x23`) twice.

`TwlhdCmd` stores one decoded command instead of the raw header word: the
command byte, the number of parameter words, and the parameter words
themselves. This keeps the capture format readable on the ARM11 without
re-running the FIFO packer.

## Command table

Parameter counts are the number of 32-bit words that follow the command byte:

| Code | Name | Params | Notes |
|------|------|--------|-------|
| `0x10` | `MTX_MODE` | 1 | 0=projection, 1=position, 2=modelview, 3=texture |
| `0x11` | `MTX_PUSH` | 0 | push current matrix |
| `0x12` | `MTX_POP` | 1 | pop N matrices |
| `0x13` | `MTX_STORE` | 1 | store to stack index |
| `0x14` | `MTX_RESTORE` | 1 | restore from stack index |
| `0x15` | `MTX_IDENTITY` | 0 | load identity |
| `0x16` | `MTX_LOAD_4x4` | 16 | 4x4 matrix, 4.12 fixed point |
| `0x17` | `MTX_LOAD_4x3` | 12 | 4x3 matrix |
| `0x18` | `MTX_MULT_4x4` | 16 | multiply by 4x4 |
| `0x19` | `MTX_MULT_4x3` | 12 | multiply by 4x3 |
| `0x1A` | `MTX_MULT_3x3` | 9 | multiply by 3x3, used by `glRotateXi` and friends |
| `0x20` | `COLOR` | 1 | 15-bit BGR555 in the low half |
| `0x21` | `NORMAL` | 1 | three packed 10-bit components |
| `0x22` | `TEX_COORD` | 1 | two packed 12.4 fixed-point texels |
| `0x23` | `VTX_16` | 2 | word 0 = `(y << 16) | x`, word 1 = `z` |
| `0x24` | `VTX_10` | 1 | three packed 10-bit components |
| `0x25` | `VTX_XY` | 1 | x/y updated, z unchanged |
| `0x26` | `VTX_XZ` | 1 | x/z updated, y unchanged |
| `0x27` | `VTX_YZ` | 1 | y/z updated, x unchanged |
| `0x40` | `BEGIN_VTXS` | 1 | 0=triangles, 1=quads, 2=triangle strip, 3=quad strip |
| `0x41` | `END_VTXS` | 0 | finish the current primitive group |
| `0x60` | `VIEWPORT` | 1 | four packed bytes: x1, y1, x2, y2 |

All vertex and matrix values are 4.12 fixed point unless stated otherwise.

## Where the hook intercepts

devkitARM/libnds gives no interception point for the packed FIFO. The capture
hook therefore wraps the public 3D helpers: the wrapper calls the real libnds
function (which performs the MMIO write) and then records the equivalent
decoded command through the parameter-count table in `arm9/source/fifo_hook.c`.
The hook does not reconstruct the original packing of up to four commands per
header word; it records the logical command stream, which is what the ARM11
renderer needs.

Consequences:

- Only calls that go through the wrappers in `arm9/include/fifo_hook.h` are
  captured. Direct libnds calls such as `glRotateXi()` or `glTranslatef()` are
  executed on the DS but not recorded.
- Commands are recorded in issue order, so vertex/normal/colour interleaving
  matches the order the DS 3D core received them.
- A frame is capped at `TWLHD_RING_CAPACITY` (1024) commands. The demo needs
  about 40 per frame, so the cap only matters for large scenes and for the
  Phase 2 analysis tooling.
