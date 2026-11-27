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

The tables in section 2 use the interpreter before the fix in section 3
(separate truncation); after it the same captures deviate by at most 4 levels.

## 3. Diffuse + ambient rounding: measured and fixed

HYPOTHESIS (made before lit_c, from the section 2 data): diffuse and ambient
are summed at full precision and truncated once. The alternative "ceil of the
diffuse term" was rejected by the existing data: lit2 quad 3 would be 181,
measured 173. The lit_c measurement below is the FACT that confirms "floor of
the sum".

FACT — `twlhd-lit3.nds` (`LIT=3`, scene `lit_c`, white light, three captures,
identical). Five-bit levels of the red channel, predicted per rule:

| Quad (diffuse/ambient, fixed_diffuse) | separate floor | floor of sum | ceil diffuse | measured |
|---|---|---|---|---|
| 0 (1/1, 4088) | 0 | 1 | 1 | 1 (8) |
| 1 (1/0, 4088) | 0 | 0 | 1 | 0 |
| 2 (1/0, 2044) | 0 | 0 | 1 | 0 |
| 3 (28/8, 1022) | 13 | 14 | 14 | 14 (115) |

Only "floor of the sum" matches all four. `common/twlhd_gx_interp.c` now sums
`diffuse*light*fixed_diffuse` and `ambient*light<<12` before the single `>>17`.
After the change the lit1/lit2/lit3 captures deviate from the prediction by at
most 4 levels (FACT, e.g. lit3 quad 0: measured G=12, expected 8), and the
former 8..10 level offset on the diffuse quads is gone. The cause of the
remaining green offset is presumably the capture path (5-bit to 6-bit green
expansion); HYPOTHESIS, UNVERIFIED.

UNVERIFIED (not covered by this measurement): rounding of the specular term
and of the sum across several lights; they are left unchanged.

## 4. Reproduction

Fixtures: `tools/fixtures/eq4_hw_lit/` (README lists SHA-256). Test:
`make test-eq4zi-lit-hw` (also part of `make test`). The test pins the
measured and expected values, requires PASS at tolerance 4, and requires
failure at tolerance 3, with a swapped expectation and with the former
separate-truncation expectation.

## 5. Not covered

Specular and multi-light rounding; scissor/over-viewport geometry; texture
formats; MODULATE; Gouraud; back-face culling; the stray pixel at (0,239).
