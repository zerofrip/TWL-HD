# EQ-4zl / 4zm / 4zn — DS-mode texture behaviour, Gouraud and culling on real hardware

Status: measurements recorded. Basis: **one console, one capture session**, three
captures per ROM. The per-quad summary numbers were identical in the three
captures of each ROM (individual sampled colours differ by 1-2 levels between
captures).

## 1. What was run

Three DS ROMs, each copied by the owner to the console SD card as a new file (no
existing file was changed), captured with the Optimize N3DS board (1280x720
screenshot, top-left 400x240 crop):

- `TEXBEH=1` (`twlhd-arm9v2-texbeh.nds`, EQ-4zl): the eight-quad behaviour frame of
  `tools/gen_tex_behaviour_frames.py` — texture alpha (A3I5, A5I3), KeyColor0 on and
  off, repeat, clamp, mirrored repeat, flip-only; data from
  `tools/hw/gen_texbeh_rom_data.py`. Checker: `tools/check_tex_behaviour.py
  --clear 0,0,0` (new option, default unchanged), tolerance 28.
- `GRAD=1` (`twlhd-arm9v2-grad.nds`, EQ-4zm): two Gouraud triangles with different
  corner colours. Measurement: `tools/hw/measure_grad.py` (ten interior
  barycentric points per triangle against the blend of the corner colours),
  tolerance 12.
- `CULL=1` (`twlhd-arm9v2-cull.nds`, EQ-4zn): eight white quads, windings
  TL-TR-BR-BL and TL-BL-BR-TR crossed with POLYGON_ATTR bits 6-7 = 1, 2, 3, 0.
  Measurement: `tools/hw/measure_cull.py` (bright-pixel fraction per quad).

The data headers (`texbeh_data.h`, `gradcull_data.h`) equal their generators'
output and the textures decode back out of the VRAM images
(`tools/test_hw_probes.py`).

## 2. FACT — EQ-4zl texture behaviour (tolerance 28)

| Quad | Role | Cells matched | Worst deviation (of 255) |
|---|---|---|---|
| 0 | A3I5 alpha 7/5/3/0 | 64/64 | 2 |
| 1 | A5I3 alpha 31/20/10/0 | 64/64 | 0 |
| 2 | I4, KeyColor0 on | 64/64 | 3 |
| 3 | I4, KeyColor0 off | 64/64 | 0 |
| 4 | repeat S and T, coordinates 0..2 | 248/256 | 69 |
| 5 | clamp, coordinates -0.5..1.5 | 253/256 | 45 |
| 6 | mirrored repeat, coordinates 0..2 | 254/256 | 56 |
| 7 | flip without repeat (clamp), coordinates 0..2 | 256/256 | 8 |

Texture alpha blending (against the black clear), KeyColor0 and the flip-only
clamp behave as the oracle expects. The failing cells of quads 4-6 (8, 3 and 2
cells) are the cells next to a tile seam (column 8 of the repeat quad) or the
clamp / tile edge; their sampled colour is a mixture of the expected texel and
the neighbouring one (for example cell (8,8) of quad 4: expected (0,0,255),
sampled (63,0,186)). No tile is missing, flipped the wrong way or shifted by a
whole texel.

HYPOTHESIS, UNVERIFIED: the seam sits one to two capture pixels away from its
nominal position (the DS picture is scaled by 1.25 in the capture and the
checker samples a 3x3 mean at nominal cell centres), as in the I4 16x16 quad of
`eq4zj-texfmt-hw-result.md`.

## 3. FACT — EQ-4zm Gouraud (tolerance 12)

All 20 interior samples (two triangles, ten each) are within tolerance. The
maximum deviation is 5.3 levels of 255 in the first and third capture and 5.0 in
the second. It occurs in the red/green/blue triangle, whose measured red channel
is 2 to 5 below the barycentric blend at all ten of its points (for example
(37,42,170) measured against (42,42,170) expected); the yellow/cyan/magenta
triangle is within 2.8. The DS interpolation of the vertex colour inside one
triangle is therefore the linear barycentric blend within this accuracy.
HYPOTHESIS, UNVERIFIED: the red offset is the 5-bit capture quantisation plus the
sample position error.

## 4. FACT — EQ-4zn culling

| Quad | Winding | POLYGON_ATTR bits 6-7 | Result |
|---|---|---|---|
| 0 | TL-TR-BR-BL (clockwise on screen) | 1 | drawn |
| 1 | TL-TR-BR-BL | 2 | not drawn |
| 2 | TL-TR-BR-BL | 3 | drawn |
| 3 | TL-TR-BR-BL | 0 | not drawn |
| 4 | TL-BL-BR-TR (counter-clockwise on screen) | 1 | not drawn |
| 5 | TL-BL-BR-TR | 2 | drawn |
| 6 | TL-BL-BR-TR | 3 | drawn |
| 7 | TL-BL-BR-TR | 0 | not drawn |

On this hardware the counter-clockwise polygon is the front face (drawn with bit
7, value 2) and the clockwise one the back face (drawn with bit 6, value 1);
value 3 draws both, value 0 draws neither. The arm11 renderer and the host
interpreter read only the alpha and light-mask bits of POLYGON_ATTR
(`docs/reports/eq4ze-hw-coverage.md` F2), so they draw all eight quads: on this
probe four of eight differ. The consequence for real content depends on
whether it relies on culling (closed opaque meshes with a depth test usually
look the same without it).

## 5. Reproduction

Fixtures: `tools/fixtures/eq4_hw_probes/` (README lists the SHA-256). Test:
`make test-eq4zlmn-probes-hw` (also part of `make test`): it pins the quad
numbers, the maximum Gouraud deviation, the eight culling states, and has
negative tests (tight tolerance, wrong clear colour, wrong capture).

## 6. Not covered

DECAL / TOON / SHADOW polygon modes, Gouraud combined with a texture,
perspective-correct interpolation (all w = 1 here), culling of a real closed mesh,
per-polygon culling on a lit mesh, texture wrap with non-power-of-two spans.
