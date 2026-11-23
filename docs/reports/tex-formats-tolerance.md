# Multi-format frame: colour tolerance review

Status: BUILD/UNIT measurement and analysis. No DS, emulator run, console, SD
or network action. Every number below comes from a real capture or from the
frame's own expected palette; nothing is estimated by hand.

Tools:

    python3 tools/measure_tex_formats_deviation.py <ppm>
    python3 tools/check_tex_formats.py <ppm> [--tolerance N]

The measurement tool imports `tools/check_tex_formats.py` (`sample_quad`, which
uses `find_bbox`/`_cell_centre`/`_mean3`) and `tools/gen_tex_format_frames.py`
(`QUAD_SPECS`, `expected_pixels`), so it samples exactly the cells the checker
samples. It prints expected/sampled RGB and the max per-channel deviation per
cell, the per-quad and overall worst cases, a deviation histogram and the input
sha256; it never fails on deviations.

## Input files used

| file | sha256 | bytes | role |
|---|---|---|---|
| `$HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm` | `4a2451a3b760afac828888e881465834e532872524cb3968b9a9745bd2547988` | 288015 | Run 20 New 3DS NTR live capture of the formats frame (the only real capture of this frame) |
| `tools/fixtures/formats_top_0_run20.ppm` | `4a2451a3b760afac828888e881465834e532872524cb3968b9a9745bd2547988` | 288015 | byte-identical copy of the capture, committed as the regression fixture (under the 300 KB cap) |

Other files checked and **excluded**:

- `$HOME/3ds/hwtest-staging/ntr_live/run20/formats_pre_top_0.ppm`
  (sha256 `5a5c62c0d045c4e60c69ba1cbaab3e4676b79090e9cdcc4ae0af563c7364c4b4`)
  is a different (pre-run) screen, not the formats frame: measured with the
  same tool, 3 of 8 quads are MISSING and the other 5 have misplaced bboxes
  with deviations 245-250 (0/64 .. 5/256 cells match).
- `$HOME/3ds/hwtest-staging/ntr_live/run20/formats_bottom_0.ppm` (and the
  other `*_bottom_0.ppm` files) are 320x240 bottom-screen captures; the checker
  only accepts a 400x240/480 top screen (`got 320x240`, exit 2), so they are not
  inputs.
- `$HOME/3ds/hwtest-staging/ntr_live/run21/*_top_0.ppm` are captures of
  other frames (`ctl/tex/over/base/final/beh`, no formats frame in run 21).
- The Azahar emulator shot `/tmp/fmt_shot_199.ppm` documented in
  `docs/EMULATOR_VALIDATION.md` (sha256 `27948117...`) no longer exists in this
  environment. It is not regenerated (no emulator run); only the real capture
  above is used.

No external file was modified (the fixture is a copy).

## Per-quad measurement (Run 20 `formats_top_0.ppm`)

`python3 tools/measure_tex_formats_deviation.py
$HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm`
sampled 704 cells (8 quads: seven 8x8 and one 16x16).

| quad | format | texels | sampled cells | worst deviation | cells > 24 | cells > 26 |
|---|---|---|---|---|---|---|
| 0 | 1 A3I5 | 8x8 | 64 | 5 | 0 | 0 |
| 1 | 2 I2 | 8x8 | 64 | 5 | 0 | 0 |
| 2 | 3 I4 | 8x8 | 64 | 4 | 0 | 0 |
| 3 | 4 I8 | 8x8 | 64 | 3 | 0 | 0 |
| 4 | 5 4x4 | 8x8 | 64 | 4 | 0 | 0 |
| 5 | 6 A5I3 | 8x8 | 64 | 3 | 0 | 0 |
| 6 | 7 16bpp | 8x8 | 64 | 4 | 0 | 0 |
| 7 | 3 I4 | 16x16 | 256 | **26** | **2** | 0 |

Overall worst: **26**, on exactly two cells, both in the 16x16 quad (cells of
about 6 px). Next-worst value anywhere is 22 (6 cells); 11 cells exceed 20.
This confirms the Run 20 note numerically (254/256 at tolerance 24).

Worst cells:

| quad | cell (x,y) | expected RGB | sampled RGB | deviation |
|---|---|---|---|---|
| 7 | (15,3) | (255,255,0) | (252,254,26) | 26 |
| 7 | (5,12) | (0,0,255) | (2,2,229) | 26 |
| 7 | (0,3) | (255,255,0) | (254,253,22) | 22 |
| 7 | (14,3) | (0,0,255) | (0,5,233) | 22 |

Deviation histogram over all 704 cells (tool buckets):

| bucket | 0 | 1-2 | 3-4 | 5-8 | 9-16 | 17-32 | 33-64 | 65-255 |
|---|---|---|---|---|---|---|---|---|
| cells | 40 | 406 | 138 | 38 | 56 | 26 | 0 | 0 |

The worst cells are yellow or blue against the dark background: JPEG chroma
bleeding at colour edges, not a mis-render (a permuted or mirrored texel
deviates by 66-255 levels, see below).

`python3 tools/check_tex_formats.py run20/formats_top_0.ppm --tolerance 24`
reports `quad 7 ... cells=254/256 worst=26 ... FAIL` (exit 1); at the new
default 28 all eight quads report 256/256 (or 64/64) OK (exit 0).

## Separation margin (from the frame's own expected colours)

Metric: max per-channel |difference|, the checker's deviation. 5-bit palette
expansion is `(v << 3) | (v >> 2)`, e.g. 16 -> 132, 31 -> 255. For each quad the
minimum pairwise distance over that quad's **actual** `expected_pixels` set
(including the 4x4 mode-1 average and mode-3 5:3/3:5 blends) was computed by
script:

| quad | distinct expected colours | closest distinct pair | distance | midpoint |
|---|---|---|---|---|
| 0 A3I5 | 8 | (132,0,255) purple - (255,0,255) magenta | 123 | 61.5 |
| 1 I2 | 3 | (0,0,255) - (0,255,0) | 255 | 127.5 |
| 2 I4 8x8 | 15 | (0,132,255) azure - (0,255,132) spring green | 123 | 61.5 |
| 3 I8 | 15 | azure - spring green | 123 | 61.5 |
| 4 4x4 | 12 | (90,156,0) - (156,90,0) (mode-3 blends) | **66** | **33** |
| 5 A5I3 | 8 | purple - magenta | 123 | 61.5 |
| 6 16bpp | 8 | purple - magenta | 123 | 61.5 |
| 7 I4 16x16 | 15 | azure - spring green | 123 | 61.5 |

Arithmetic examples:

- azure/spring green: max(|0-0|, |132-255|, |255-132|) = 123;
  midpoint = ceil(123/2) = 62 (61.5 exact).
- 4x4 mode-3 blends: max(|90-156|, |156-90|, |0-0|) = 66;
  midpoint = 33.
- CLEAR (16,16,24) versus each quad's nearest expected colour: nearest is
  quad 4 `(90,156,0)`, distance max(74,140,24) = 140; every other quad's
  nearest is blue `(0,0,255)`, distance 231. So CLEAR and any real texel colour
  are 140 levels apart at worst, far above any tolerance considered here.

Guarantees with a per-channel tolerance T:

- A texel fully substituted by the closest distinct expected colour deviates
  by at least 66 (quad 4) and fails for every T < 66. At T = 28 the margin is
  38 levels (123 - 28 = 95 in the other quads).
- A 3x3 sample that is the 50/50 blend of the closest pair deviates by about
  half the pair distance: 33 (quad 4), 62 (other quads). It fails for T < 33.
  At T = 28 quad 4 keeps 5 levels of margin.
- Consequently T must be < 33 for the blend guarantee; that is the binding
  constraint, not the 123-level palette margin.

## Decision

**One global default tolerance T = 28** in `tools/check_tex_formats.py`
(measured worst 26 <= 28 < 33). `--tolerance N` still overrides it, exit codes
and the output line format are unchanged.

- Justification: the largest deviation on any good capture is 26 (quad 7, two
  cells), so 24 is too tight by measurement, and 28 adds two levels of
  headroom for capture-to-capture JPEG variation while remaining below the
  smallest separation midpoint (33) of any quad.
- Why not a size-dependent rule: the 16x16 quad is the only one above 24, but
  the binding separation is quad 4's 66-level blend pair, which is independent
  of the 16x16 cell size. Keeping 24 for the 8x8 quads would only reject
  quad-4 blend samples that land at 25..28 from the expected colour (none
  observed: quad 4 worst 4) and would not strengthen the guarantee against a
  fully substituted texel (66 > both 24 and 28). One constant also matches the
  sibling `tools/check_tex_behaviour.py` (TOLERANCE = 28) for the same capture
  chain.
- CLEAR/bbox side effect: `_is_clear` shares the tolerance, but the Run 20
  bbox is unchanged between T = 24 and T = 28 (quad 0 stays
  `(43,9,108,110)`; the 1-pixel capture halo only becomes clear around
  T >= 100). All cells still pass, and the nearest real colour to CLEAR is 140
  levels away, so 28 cannot swallow a texel as background.

What the new tolerance does NOT catch (documented limits):

- Deviations of 27-28 levels, e.g. a texel drifting by up to 28 in one channel.
- On quad 4 only, an edge blend against the closest blend pair (distance 66)
  passes while the wrong colour's share is at most 42% (28/66); a 50/50 blend
  deviates by 33 and fails with 5 levels of margin.
- On the other quads, a blend against the closest palette pair (distance 123)
  passes while the wrong colour's share is at most 23% (28/123); a 50/50 blend
  deviates by 62 and fails with 34 levels of margin.
- A two-pixel-wide chroma average that lands within 28 of the expected colour.
- A wrong texel whose closest distinct expected colour is the pair above is
  still caught: 66 > 28 in quad 4 (margin 38) and 123 > 28 elsewhere.

## Verification commands

    python3 tools/measure_tex_formats_deviation.py \
        $HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm
    python3 tools/check_tex_formats.py \
        $HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm
    python3 tools/check_tex_formats.py \
        $HOME/3ds/hwtest-staging/ntr_live/run20/formats_top_0.ppm --tolerance 24
    python3 -B -I -m unittest discover -s tools -p test_tex_formats.py

Results: measurement prints overall worst 26/704 cells with the histogram
above; default 28 exits 0 with all cells OK; `--tolerance 24` exits 1 with
`quad 7 ... cells=254/256 worst=26 FAIL`. The test module has 25 tests, all
passing, including the committed-capture regression, the synthetic worst-case
(26) regression, the nearest-neighbour / mirrored / shifted-bbox negatives and
the tolerance pin.

Test counts (this change): `tools/test_tex_formats.py` 16 -> 25 tests (nine
added, none removed or renamed); the full suite `make test`
(`python3 -B -I -m unittest discover -s tools`) ran 312 tests, OK, i.e. the
previous 303 plus the nine new ones. No existing test name disappeared (the
16 old names are all still present).
