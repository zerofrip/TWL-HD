# EQ-4zq — GX frame dump from retail software (out-of-tree DeSmuME tool) and the DS stack model

Status: tool built and run on one retail title; interpreter option implemented and
host-tested. No game data, ROM, screenshot or dumped frame is part of the
repository (they are local files; the repository carries only the interpreter
change, its tests and this report).

## 1. Why

The hardware comparisons so far (EQ-4za..4zn) used our own ROMs, which record their
own GX command stream. Retail software cannot do that. To compare our render with
the DS picture of a real game, the command stream of one frame is needed.

## 2. The tool (out of tree, not committed)

A local patch of the DeSmuME source (kept in the staging directory next to the other
harness patches; DeSmuME is GPL, the repository carries no DeSmuME code):

- `gxdump.cpp` hooks every geometry command at `gfx3d_execute` and writes one v2
  capture file per SWAP_BUFFERS (`twlhd_frame_v2_<swap count>.bin`), written with
  the repository's `common/twlhd_v2_write.c`. Environment: `TWLHD_GXDUMP_DIR`,
  `TWLHD_GXDUMP_FRAMES=first-last`, `TWLHD_GXDUMP_SHOT_EVERY=n` (also writes the
  3D renderer framebuffer of every n-th frame as a PPM).
- Each file starts with a state prefix rebuilt from the engine state at the first
  command of the frame, because a frame of retail software does not repeat the
  state it set earlier: the four matrices (projection, texture, position and
  vector, loaded through MTX_LOAD_4x4), the position matrix stack slots
  `0..sp` (PUSH sequence, then POP 1), the light directions (the engine keeps them
  transformed; they are re-encoded under an identity vector matrix, 10-bit
  precision), light colours, material colours, POLYGON_ATTR, TEXIMAGE_PARAM,
  PLTT_BASE, current colour, VIEWPORT and the shininess table (the last
  seen values).
- Texture data: for every (TEXIMAGE_PARAM, PLTT_BASE) pair used by a vertex the
  texel bytes, palette bytes and (4x4 compressed) index bytes are copied from
  DeSmuME's texture memory with the same slot rules as its texture cache; for
  format 5 the palette window ends at the highest colour any block can read.

## 3. Interpreter option `TWLHD_GX_OPT_DS_STACK`

Retail software uses stack idioms the strict interpreter reports as errors: for
example `MTX_POP 0` in position mode, which the DS does not reject (the matrix at
the stack slot is loaded). The new option models the stacks as DeSmuME does
(`gfx3d.cpp:831-1012`, SOURCE-READ): a six-bit wrapping index, POP loads the slot
reached, STORE/RESTORE may use slot 31 (the strict model stops at 30),
projection and texture stacks toggle, nothing is an error. It is off by default; `gx_interp_cli --ds-stack` enables it.
The stack arrays now hold 32 slots.

Tests: five host tests (pop of 0, six-bit wrap, store/restore of slot 31,
projection toggle, off-equals-default on a valid stream), each with the
default-model negative where it applies.

## 4. FACT — first run (one title, local data)

Run through the dumper for 4200 swaps (about 150 s on this host with the JIT):

- 4201 files written, none rejected by the writer's limits.
- Without `--ds-stack` the interpreter stops with a stack error on every sampled
  frame; with it all 4201 files parse and interpret (`result=OK`).
- Triangles per frame: 64 in 3998 frames (a static screen), 0 in 123 frames
  (start-up), a few intermediate values in the transition.
- The prefix is 111 to 152 entries per frame; textures per frame 0 to 4
  (at most 16896 bytes of texel data).

## 5. HYPOTHESIS / UNVERIFIED

- The reconstructed state prefix is a faithful start state only for the state it
  covers; the normal vector latch, stacks of the projection and texture matrices,
  and the vector stack entries below the position stack are not rebuilt exactly.
- The re-encoded light directions lose precision (10-bit fields).
- Nothing here compares our render with the DS picture of the game yet: that needs
  the same scene captured on the console in DS mode and rendered by the arm11
  renderer from the dumped frame.

## 6. Next step

Pick a static scene (the run above shows one that the title reaches without input),
capture it on the console in DS mode, render the dumped frame with the arm11
renderer and compare numerically. Only numbers go into the repository; frames,
screenshots and captures of the game stay local.
