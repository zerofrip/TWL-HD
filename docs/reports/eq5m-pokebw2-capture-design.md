# EQ-5m: capturing a retail game's GX stream on the console through a decompilation rebuild

Labels: FACT = measured here; SOURCE-READ = read in source/docs; INFERENCE = derived from
FACT/SOURCE-READ items; HYPOTHESIS; UNVERIFIED; OPEN = decision needed. No game data is
recorded; the decompilation tree and the ROM stay outside this repository.

## What was checked
FACT: the owner's decompilation of Pokemon Black 2 / White 2 (`pokebw2`, USA/Europe) was
configured and built with its documented flow (`python3 configure.py`, `ninja`, 7626
steps); the output `build/pokeblack2_us.nds` has SHA1 e51e6dfb8678a3d19dcd2a10691b96a569ca0abb,
equal to the SHA1 of the unmodified USA Black 2 ROM given in its README. The base ROM
supplied for this has the same SHA1.
FACT: the ROM header of the Japanese ROM first fetched has game code IREJ, version 1.
SOURCE-READ (`pokebw2/README.md`): the tree targets USA / Europe only.
SOURCE-READ (`pokebw2/docs/building.md`): the build downloads pinned third-party tools
(wibo, sjiswrap, objdiff, Metrowerks CodeWarrior tools, dsd) and runs the Windows tools
through wibo.
INFERENCE: the tree can be rebuilt byte-exact here, so a modified ROM can be produced and
differences are attributable to the modification.

## Where the game submits geometry commands (SOURCE-READ, counts from `grep` over the tree)
| path | sites |
|---|---|
| immediate register writes `reg_G3_*` | 120 hits in 8 files |
| writes to the FIFO port `reg_G3X_GXFIFO` | 48 hits in 5 files |
| CPU copy to the FIFO `MIi_CpuSend32` | 19 hits in 4 files |
| display lists by DMA `MI_SendGXCommandAsync` / `...Fast` | 10 hits in 4 files |
| display lists by NDMA (DSi mode) `MI_SendNDmaGXCommandAsync` | 6 hits in 3 files |
| buffer swap (`reg_G3_SWAP_BUFFERS`, `G3_SwapBuffers`, `G3X_SwapBuffers`) | 6 hits in 3 files |
SOURCE-READ (`lib/nnsys/src/g3d/gecom.c`): the NNS G3D engine collects commands in a
geometry buffer and flushes it with `MIi_CpuSend32(..., &reg_G3X_GXFIFO, ...)` or by DMA.
INFERENCE: a recorder needs three funnels, not one: the register / FIFO writes, the CPU
copy to the FIFO, and the DMA / NDMA display lists (parsed as packed command words:
four command bytes followed by their parameters, using the parameter counts of
`twlhd_v2_cmd_param_count`). About 20 files carry the sites.

## What a rebuild-based capture needs besides the commands
- SOURCE-READ (`docs/ARCHITECTURE.md`, `docs/RTCOM_AUDIT.md`): the ARM9 cannot reach the
  PICA200, and RTCom moves one byte per acknowledged exchange (that audit's own estimate for
  a 72 KiB frame is about 73000 exchanges).
- FACT (this session's dumps): one frame of the three retail scenes captured so far is
  199116 B (SM64DS), 297356 B (NSMB) and 270920 B (Pokemon B2).
- INFERENCE: at one byte per exchange a retail frame needs about 200000 to 300000 exchanges
  (the measured sizes above), so streaming frames live to an ARM11 application is not
  feasible; frames must be stored on the card and replayed afterwards.
- UNVERIFIED: how the rebuilt ROM would be started on the console (DS mode through a
  loader, or DSi mode) and whether the card can be written from it; in DSi mode the TWL SDK
  `fatfs` library is present in the tree (`lib/twl/src/fatfs`).
- SOURCE-READ (`docs/TEXTURE_CAPTURE_DESIGN.md`, the out-of-tree DeSmuME dumper): textures
  and palettes (TXDS/TXDT/PLDT chunks) are taken from VRAM texture memory; the DeSmuME
  dumper reads them from the emulator's texture cache.
- UNVERIFIED: how a console build would read them (remapping the VRAM banks to the LCDC
  area temporarily is a common approach; not tested), and whether VRAM is CPU-readable
  while it is mapped as texture memory.

## Proposed first step (HYPOTHESIS, not implemented)
1. In the tree (kept in a local branch of `pokebw2`, not committed to this repository),
   route the sites above through a small recorder that appends 8-byte command entries
   (command word, value) to a RAM buffer and closes the frame at the buffer swap.
2. Run the rebuilt ROM in DeSmuME first and compare its output with the DeSmuME dumper's
   frame of the same scene (both go through `twlhd_v2_parse` and the same interpreter);
   no console is touched in this step.
3. Only after that: storage of frames on the card, then a console run.

## OPEN decisions for the owner
- Region: the tree supports USA / Europe only; the Japanese ROM would need other support.
- How the rebuilt ROM is launched on the console (DS or DSi mode) and the card writer to use.
- Licence of the decompilation tree: the recorder patch would stay in the `pokebw2` tree and
  not enter this Apache-2.0 repository.
