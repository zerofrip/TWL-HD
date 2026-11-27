# EQ-4zk — DS-mode MODULATE (texture x vertex colour) on real hardware

Status: measurement recorded. Basis: **one console, one capture session**, three
captures, identical measurement in all three.

## 1. What was run

- ROM `twlhd-arm9v2-texmod.nds` (`TEXMOD=1`, `arm9v2/Makefile`), copied to the
  console SD card as a new file by the owner; no existing file was changed.
  Eight flat quads, each with one solid 8x8 16bpp texture and one flat vertex
  colour, default polygon mode MODULATE; three (texel, vertex) channel pairs
  per quad, 24 pairs with levels 2..31 (`tools/hw/gen_texmod_rom_data.py`,
  `arm9v2/source/texmod_data.h`). Offline test: the textures read back out of
  the VRAM image decode to the intended solid colours
  (`tools/test_hw_texmod.py`).
- Capture: Optimize N3DS capture board, 1280x720 screenshot, top-left 400x240
  crop. Measurement: `python3 tools/hw/measure_texmod.py <ppm>` (9x9 mean at the
  centre of every quad).
- Models compared (both get the same 5-bit levels):
  **ds6** — DeSmuME six-bit equation (`docs/DS_GX_RESEARCH.md` "Texture
  application"): `x6 = x ? 2x+1 : 0`, `out6 = ((t6+1)*(v6+1)-1) >> 6`;
  **pica** — what the arm11 renderer's `GPU_MODULATE` stage computes: 8-bit
  texel (oracle table) times vertex/31, rounded.

## 2. FACT — capture path resolution

Every measured red and blue value is a multiple of 255/31 and every green value
a multiple of 255/63 (for example red 24, 66, 41, 57, 214 = levels 3, 8, 5, 7,
26 of 31). The capture therefore shows 5 bits of red and blue and 6 bits of
green; the measurement is compared in that domain. HYPOTHESIS, UNVERIFIED: the
board or its host path delivers RGB565. It accounts for the earlier green
offsets of 1..4 levels in the lighting captures (`eq4zi-lit-hw-result.md`).

## 3. FACT — result

Of the 24 channels (capture-level comparison, red/blue 5-bit, green 6-bit):

| Model | Exact capture-level matches | Max deviation in 8-bit values |
|---|---|---|
| ds6 | 24 of 24 | 4 (the 5-bit quantisation of red/blue) |
| pica | 11 of 24 | 6 |

The 13 channels where pica differs are all off by exactly one capture level
(the largest 8-bit gap is 6). Measured values: see the table in
`tools/eq4_hw_texmod/test_hw_texmod.py` (`MEASURED`).

Consequence: the DeSmuME six-bit equation reproduces the real DS MODULATE output
exactly on all 24 pairs tried; the current arm11 `GPU_MODULATE` is up to one
capture level (about 6 of 255 on a channel) away from it. That is below the
tolerances used so far (12 for flat colours) but not zero.

## 4. Reproduction

Fixture: `tools/fixtures/eq4_hw_texmod/` (README lists the SHA-256). Test:
`make test-eq4zk-texmod-hw` (also part of `make test`): it pins the 24 measured
values, the model match counts and maximum deviations, and has a blank-capture
negative test; `tools/test_hw_texmod.py` additionally runs the measurement tool
on synthetic captures of both models (24/24 for the own model, not 24/24 for the
other one).

## 5. Not covered

Gouraud-interpolated vertex colour with a texture, texture alpha with MODULATE
(texels here are opaque), DECAL/TOON/SHADOW modes, a change of the arm11
renderer (not made), vertex colours between the tested levels.
