# EQ-5n: GX command log of a rebuilt retail game checked against the DeSmuME dumper

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT items; HYPOTHESIS; UNVERIFIED. Only numbers are recorded. The decompilation tree, the
ROMs, the BIOS file and the recorder patch stay outside this repository (local branch of
the `pokebw2` tree; no game data here).

## What was built
FACT: in a local branch of the decompilation tree, every geometry command the CPU sends is
appended to a RAM log (one `{command id, value}` entry per parameter word, the GXCM entry
layout of the v2 format): 108 register-write sites (`reg_G3_*` and the FIFO port) are
wrapped so that the log is written before the store, the CPU copies to the FIFO (14 sites
in the G3D engine's `sbc.c`, 2 in `gecom.c` and 6 matrix copies in `g3imm.c`), and the
display lists sent by DMA (`MI_SendGXCommandAsync` and the `Fast` variant). A small parser
follows the packed command words and the parameter counts.
SOURCE-READ (GBATEK, as used by the repository's v2 format): the packed-word layout the
parser follows; it was not checked against a console.
FACT: two buffers alternate; `SWAP_BUFFERS` closes a frame.

## Build
FACT: with the unmodified tree the build equals the USA Black 2 ROM (SHA1 e51e6dfb...).
FACT: the recorder build differs from the original in 1506 bytes of the first 0x4000 bytes
of the ARM9 binary (byte comparison of the extracted and the built `arm9.bin`).
INFERENCE: the differing bytes are branch targets and module parameters that move with the
code size (the differences start at 0x980 and 0xf7c; not individually decoded).
FACT: the ROM tool stopped with "the ARM9 secure area has changed since extracting ...
Provide the ARM7 BIOS"; with the owner's BIOS dump the modified ROM builds
(`configure.py --bugfix`, a local linker-script step places the log's `.rodata` / `.bss`).
SOURCE-READ (`docs/building.md` of the tree): modified builds skip the matching check.

## Memory
FACT: with a log of 480040 bytes (2 x 30000 entries) the rebuilt ROM does not boot in
DeSmuME (DS mode, 4 MB): the ARM9 stops in `OS_Terminate`, reached from `GFL_MemInit`
(call chain read from the stack and the linker map).
FACT: with 2 x 2000 entries (32 KB) it boots to the same 3D scenes as the original ROM.
UNVERIFIED: the memory available in DSi mode (16 MB) on the console.

## Comparison with the DeSmuME dumper
The patched dumper compares, at each swap, the closed frame of the log with the commands it
saw. For frames longer than the buffer the harness moves a window (start offset written into
the log's `skip` field, advancing by 2000 entries per frame and wrapping to 0 after 40000, a
limit chosen above the longest frame seen). A window that starts past the end of the frame
holds 0 entries and counts as matched.
FACT: 5623 swaps compared, 0 mismatches. The frame has 51 entries at swap 0, 30948 at swap 1
and 30947 from swap 2 on; windows from entry 0 to 30000 each matched 2000 entries, and the
last window matched 947 entries (the frame's end).
INFERENCE: for this scene the log reproduces the dumper's command stream exactly, in all
sites that the scene uses.
UNVERIFIED: scene variety (only the boot / intro scene was run; the game's other
submission paths, for example the NDMA display lists of the DSi mode, are not exercised);
the matrix and render state at the start of a frame (the dumper's prefix) is not logged;
texture data; writing frames to the card; running on the console.

## Next steps (proposal)
1. Run the field / battle scenes in DeSmuME (needs the owner to reach them) and compare.
2. Log the render state (RSTA) and a matrix-state prefix at the first command of a frame.
3. Store frames on the card in DSi mode (the tree has the TWL SDK `fatfs`), then a console run.
