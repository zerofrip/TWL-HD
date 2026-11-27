# EQ-4zi — DS-mode lighting on real hardware

Status: measurement recorded. Basis: **one console, one capture session**,
three captures per ROM, all three identical in the measured patches.

## 1. What was run

- ROMs: `twlhd-lit1.nds` (`LIT=1`, scene `lit_a`) and `twlhd-lit2.nds`
  (`LIT=2`, scene `lit_b`), built as described in
  `docs/reports/eq4zh-lit-rom.md`. Both were copied to the console SD card as
  new files; no existing file was changed.
- Capture: Optimize N3DS capture board, 1280x720 screenshot, top-left 400x240
  crop. Comparison: `tools/hw/compare_ppm.py <top.ppm> <expected.json> --tol 12`
  against `tools/hw/predict_lit.py` output for `tools/hw/gen_lit_frames.py`
  frames (9x9 patch at each quad centre).

## 2. FACT — measured vs predicted (RGB, tolerance 12 -> `result=PASS`)

`lit1` (scene `lit_a`):

| Patch | Measured | Expected | Dev R/G/B |
|---|---|---|---|
| quad 0 | (206,207,206) | (197,197,197) | 9/10/9 |
| quad 1 | (115,117,115) | (107,107,107) | 8/10/8 |
| quad 2 | (24,28,24) | (25,25,25) | 1/3/1 |
| quad 3 | (24,28,24) | (25,25,25) | 1/3/1 |
| background | (0,0,0) | (0,0,0) | 0/0/0 |

`lit2` (scene `lit_b`):

| Patch | Measured | Expected | Dev R/G/B |
|---|---|---|---|
| quad 0 | (206,0,0) | (197,0,0) | 9/0/0 |
| quad 1 | (33,231,33) | (33,230,33) | 0/1/0 |
| quad 2 | (239,239,239) | (239,239,239) | 0/0/0 |
| quad 3 | (173,0,148) | (173,0,148) | 0/0/0 |
| background | (0,0,0) | (0,0,0) | 0/0/0 |

Consequences (FACT): the `POLY_CULL_NONE` fix works on hardware (no black
screen); ambient-only, emission, specular (shininess table) and two-light
quads match within 3 levels; the diffuse quads are 8..10 levels brighter.

## 3. HYPOTHESIS — diffuse rounding

The diffuse offset (8..10 of 255) equals one 5-bit step (255/31 = 8.2).
Hardware showing 25/31 where the DeSmuME-derived interpreter gives 24/31
suggests a floor-vs-round difference in the diffuse term. **UNVERIFIED**: only
two dot products and one material were sampled. A change to the interpreter
needs more data points (other dot products and materials).

## 4. Reproduction

Fixtures: `tools/fixtures/eq4_hw_lit/` (README lists SHA-256). Test:
`make test-eq4zi-lit-hw` (also part of `make test`). The test pins the values
in section 2, requires PASS at tolerance 12, requires failure at tolerance 5
and with a swapped expectation.

## 5. Not covered

Interpreter fix for diffuse rounding; scissor/over-viewport geometry; texture
formats; MODULATE; Gouraud; back-face culling; the stray pixel at (0,239).
