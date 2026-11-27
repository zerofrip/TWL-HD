# EQ-4zj — DS-mode texture formats on real hardware

Status: measurement recorded. Basis: **one console, one capture session**, three
captures, identical per-quad numbers.

## 1. What was run

- ROM `twlhd-arm9v2-texfmt.nds` (`TEXFMT=1`, `arm9v2/Makefile`), copied to the
  console SD card as a new file by the owner; no existing file was changed.
  It draws the eight-quad grid of `tools/gen_tex_format_frames.py` (A3I5, I2,
  I4 8x8, I8, compressed 4x4, A5I3, 16bpp, I4 16x16) with white vertex colour
  on a black clear. The VRAM images (texture slot 0 texels, slot 1 4x4
  indices, texture palette) and the per-quad TEXIMAGE_PARAM / PLTT_BASE words
  come from `tools/hw/gen_texfmt_rom_data.py` (`arm9v2/source/texfmt_data.h`).
  Offline test: every texture read back out of the three images at the
  programmed offsets decodes to the generator's expected pixels
  (`tools/test_hw_texfmt_data.py`).
- Capture: Optimize N3DS capture board, 1280x720 screenshot, top-left 400x240
  crop. Check: `python3 tools/check_tex_formats.py <ppm> --clear 0,0,0` (the
  checker samples the centre of every texel cell with a 3x3 mean against the
  oracle `tools/tex_decode_oracle.py`, default tolerance 28; `--clear` is new
  and defaults to the old value).

## 2. FACT — per quad (tolerance 28)

| Quad | Format | Cells matched | Worst deviation (of 255) |
|---|---|---|---|
| 0 | A3I5 8x8 | 64/64 | 3 |
| 1 | I2 8x8 | 64/64 | 3 |
| 2 | I4 8x8 | 64/64 | 3 |
| 3 | I8 8x8 | 64/64 | 2 |
| 4 | compressed 4x4 8x8 (modes 0-3) | 64/64 | 7 |
| 5 | A5I3 8x8 | 64/64 | 3 |
| 6 | 16bpp 8x8 | 64/64 | 3 |
| 7 | I4 16x16 | 255/256 | 35 |

Seven of eight quads pass. All three captures gave the same numbers. The
compressed 4x4 quad, which needs the second texture slot for its indices,
decodes correctly on DS hardware (worst 7).

## 3. The one cell above the tolerance (quad 7)

FACT: the single failing cell is cell (13,13) of the I4 16x16 quad
(expected (0,132,255), sampled (35,145,239), deviation 35). In total 22 of the
256 cells deviate by more than 12 levels: all 16 cells of the last texel
column 15 (the strong channel samples about 239 of 255) and six cells in
columns 9, 10 and 13 (deviations 14 to 35). FACT: nearest-colour
classification of all 256 cells against the 15 palette colours misclassifies
none.

HYPOTHESIS, UNVERIFIED: the deviations come from the sampling position, not
from the texture decode: the DS picture is 256x192 scaled by 1.25 in the
capture, so a texel cell of the 16x16 texture is about 6 capture pixels wide
and the 3x3 mean at a cell centre reaches into a neighbour at some cells.

## 4. Reproduction

Fixture: `tools/fixtures/eq4_hw_texfmt/` (README lists the SHA-256). Test:
`make test-eq4zj-texfmt-hw` (also part of `make test`): it pins the per-quad
numbers, requires failure with a tight tolerance, with the default clear
colour and with a wrong expectation, and pins the 256-cell classification.

## 5. Not covered

Texture wrap/repeat/flip, MODULATE with a vertex colour, Gouraud, back-face
culling, alpha-textured blending (A3I5/A5I3 alpha is fully opaque in this
grid), KeyColor0.
