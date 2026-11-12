# 5B-2c mutation report: host texture decoder (formats 1-7)

Status: **IMPLEMENTED**, host-side only. `make test` green. Nothing EMULATOR or
HARDWARE verified.

## Method

Every mutant is a scratch copy of the ONE file `common/twlhd_tex_decode.c`
under `/tmp/opencode/mutants5b2c/mNN/` (outside the worktree). Each mutant was
compiled against the **unmodified** `tests/host/test_tex_decode.c` and the
unmodified parser/writer/record sources with the repository's `HOST_CFLAGS`
(`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
-Itools/azahar_host -fsanitize=address,undefined -fno-omit-frame-pointer`),
producing exactly the two artifacts `make test` builds and runs for this
feature:

1. `tests/host/build/test_tex_decode` (the `host-test` step) — run directly;
   its `FAIL tex-decode/...` labels are the killers below.
2. `tests/host/build/tex_decode_cli` + `tools/test_tex_decode_golden.py` (the
   `python3 -m unittest discover -s tools` step) — the golden test was run from
   a scratch tree whose `tools/` holds unmodified copies of the oracle and the
   golden test and whose CLI is the mutant build; `FAIL`/`ERROR` there is the
   second killer.

A mutant is **KILLED** when at least one of those two `make test` steps exits
non-zero. The unmutated baseline copy passes both (20/20 C tests, 4/4 golden
tests). Mutants that fail by aborting under ASan/UBSan (a NULL dereference the
decoder must reject) still fail `make test`; they are listed as such.

Run: `python3 /tmp/opencode/mutants5b2c/run_mutants.py` (log
`/tmp/opencode/mutants5b2c/run.log`, machine-readable
`/tmp/opencode/mutants5b2c/results.json`).

## Results

65 mutants, **65 KILLED, 0 survivors**. Coverage: both component-expansion
formulas, palette bit-15 handling, per-format bit extraction and sub-byte
packing order (A3I5, I2, I4, I8, A5I3, 16bpp), the colour-0 rule (on, off,
I2-only, index, key bit, I4/I8 ignored), all four 4x4 block modes, the
average/5:3 arithmetic, shift and rounding, the palette byte-offset formula
(both the pre-scan and the decode path, multiplier and 14-bit mask), window
bounds (inclusive end, disabled pre-scan, per-mode entry counts), block
order/packing, dimensions, and every validation/error path (NULL, format 0,
texel/index size both directions, palette size, output size, palette-range).

| # | Mutant | Result | Killing `make test` step(s) |
|---:|---|---|---|
| 1 | expand5 formula *255/31 | KILLED | `tex-decode/expansion-tables`, `tex-decode/a5i3-8x8`, `tex-decode/a5i3-8x32-key0-ignored`, golden:test_4x4_block_modes_and_offsets |
| 2 | expand3 formula *255/7 | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, golden:test_all_formats_random_payloads |
| 3 | palette bit 15 becomes alpha (A3I5) | KILLED | `tex-decode/a3i5-8x8`, golden:test_all_formats_random_payloads |
| 4 | green taken from bits 4-8 | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, `tex-decode/a3i5-16x8-key0-ignored`, golden:test_4x4_block_modes_and_offsets |
| 5 | transparent writes A=255 | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i8-8x8`, golden:test_4x4_block_modes_and_offsets |
| 6 | width shift 20 -> 21 | KILLED | `tex-decode/dimensions`, `tex-decode/4x4-16x8-block-order`, golden:test_4x4_block_modes_and_offsets |
| 7 | height shift 23 -> 24 | KILLED | `tex-decode/dimensions`, `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, golden:test_4x4_block_modes_and_offsets |
| 8 | dimensions accepts format 0 | KILLED | `tex-decode/dimensions` |
| 9 | result_string default OK | KILLED | `tex-decode/error-paths` |
| 10 | mode 0 needs 2 entries | KILLED | `tex-decode/4x4-window-boundaries` |
| 11 | mode 0 needs 4 entries | KILLED | `tex-decode/4x4-window-boundaries` |
| 12 | mode 2 needs 3 entries | KILLED | `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_palette_window_errors |
| 13 | modes 1/3 need 4 entries | KILLED | `tex-decode/4x4-window-boundaries`, golden:test_4x4_palette_window_errors |
| 14 | offset multiplier *2 (pre-scan) | KILLED | `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, `tex-decode/error-leaves-output-untouched` |
| 15 | offset mask 0x1FFF (pre-scan) | KILLED | `tex-decode/4x4-high-palette-offsets` |
| 16 | offset multiplier *2 (decode) | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-16x8-block-order`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 17 | offset mask 0x1FFF (decode) | KILLED | `tex-decode/4x4-high-palette-offsets` |
| 18 | window end exclusive | KILLED | `tex-decode/4x4-16x8-block-order`, `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_palette_window_errors |
| 19 | window pre-scan disabled | KILLED | `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, `tex-decode/error-leaves-output-untouched`, golden:test_4x4_palette_window_errors |
| 20 | mode field >>13 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 21 | mode 0 third colour = entry 3 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-window-boundaries`, golden:test_4x4_block_modes_and_offsets |
| 22 | mode 2 fourth colour = entry 2 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-window-boundaries`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 23 | mode 1 average >>2 | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 24 | mode 1 averages 2 channels only | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 25 | mode 1 average alpha 254 | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 26 | mode 1 averages col0 with itself | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 27 | mode 3 t1 weights swapped | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 28 | mode 3 shift >>5 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 29 | mode 3 rounds +32 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 30 | mode 3 green folded <<4 | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 31 | row shift sy*4 | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 32 | texel bits 1-2 first | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 33 | block_x = block / x_blocks | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-16x8-block-order`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 34 | block_y = block % x_blocks | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-16x8-block-order`, `tex-decode/4x4-high-palette-offsets`, golden:test_4x4_block_modes_and_offsets |
| 35 | block word big-endian | KILLED | `tex-decode/4x4-modes-8x8`, golden:test_4x4_block_modes_and_offsets |
| 36 | index word big-endian | KILLED | `tex-decode/4x4-modes-8x8`, `tex-decode/4x4-16x8-block-order`, `tex-decode/4x4-window-boundaries`, golden:test_4x4_block_modes_and_offsets |
| 37 | A3I5 index mask 0x1E | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, golden:test_all_formats_random_payloads |
| 38 | A3I5 alpha >>4 | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, golden:test_all_formats_random_payloads |
| 39 | A3I5 alpha drops bit 2 | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, `tex-decode/a3i5-16x8-key0-ignored`, golden:test_all_formats_random_payloads |
| 40 | I2 three texels per byte | KILLED | `tex-decode/i2-8x8`, `tex-decode/i2-16x8-key0-nonzero-opaque`, golden:test_all_formats_random_payloads |
| 41 | I2 3-bit indices | KILLED | `tex-decode/i2-8x8`, `tex-decode/i2-16x8-key0-nonzero-opaque`, golden:test_all_formats_random_payloads |
| 42 | I4 5-bit indices | KILLED | `tex-decode/i4-8x8`, `tex-decode/i4-8x32-key0`, golden:test_all_formats_random_payloads |
| 43 | sub-byte packing reversed | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i4-8x32-key0`, golden:test_all_formats_random_payloads |
| 44 | key0 only for I2 | KILLED | `tex-decode/i4-8x8`, `tex-decode/i8-8x8` |
| 45 | key0 never | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i8-8x8`, golden:test_all_formats_random_payloads |
| 46 | key0 always | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i8-8x8`, golden:test_all_formats_random_payloads |
| 47 | key0 index 1 | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i8-8x8`, golden:test_all_formats_random_payloads |
| 48 | palette stride 1 byte | KILLED | `tex-decode/i2-8x8`, `tex-decode/i2-16x8-key0-nonzero-opaque`, `tex-decode/i4-8x8`, golden:test_all_formats_random_payloads |
| 49 | keycolour bit 28 | KILLED | `tex-decode/i2-8x8`, `tex-decode/i4-8x8`, `tex-decode/i8-8x8`, golden:test_all_formats_random_payloads |
| 50 | A5I3 index mask 0x0F | KILLED | `tex-decode/a5i3-8x8`, golden:test_all_formats_random_payloads |
| 51 | A5I3 alpha >>2 | KILLED | `tex-decode/a5i3-8x8`, `tex-decode/a5i3-8x32-key0-ignored`, golden:test_all_formats_random_payloads |
| 52 | A5I3 alpha 3-bit table | KILLED | `tex-decode/a5i3-8x8`, golden:test_all_formats_random_payloads |
| 53 | 16bpp alpha bit 14 | KILLED | `tex-decode/expansion-tables`, `tex-decode/16bpp-8x8`, golden:test_all_formats_random_payloads |
| 54 | 16bpp transparent keeps RGB | KILLED | `tex-decode/16bpp-8x8`, `tex-decode/16bpp-8x32`, golden:test_all_formats_random_payloads |
| 55 | 16bpp half the texels | KILLED | `tex-decode/expansion-tables`, `tex-decode/16bpp-8x8`, `tex-decode/16bpp-8x32`, golden:test_all_formats_random_payloads |
| 56 | 16bpp big-endian | KILLED | `tex-decode/expansion-tables`, `tex-decode/16bpp-8x8`, golden:test_all_formats_random_payloads |
| 57 | NULL texel accepted | KILLED | ASan/UBSan abort (non-zero exit) |
| 58 | format 0 check dropped | KILLED | `tex-decode/error-paths` |
| 59 | palette NULL accepted | KILLED | ASan/UBSan abort (non-zero exit) |
| 60 | index NULL accepted | KILLED | ASan/UBSan abort (non-zero exit) |
| 61 | size check accepts larger | KILLED | `tex-decode/error-paths` |
| 62 | output size check <= | KILLED | `tex-decode/error-paths`, golden:test_4x4_block_modes_and_offsets |
| 63 | palette-size helper dropped | KILLED | `tex-decode/4x4-window-boundaries`, `tex-decode/error-paths` |
| 64 | fixed palette check <= | KILLED | `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, `tex-decode/a3i5-16x8-key0-ignored`, golden:test_all_formats_random_payloads |
| 65 | format field >>25 | KILLED | `tex-decode/dimensions`, `tex-decode/expansion-tables`, `tex-decode/a3i5-8x8`, golden:test_4x4_block_modes_and_offsets |
## Notes

- **No survivors needed an equivalence argument.** Two candidate mutants were
  replaced before the final run because they cannot change behaviour:
  - removing the `& 0x7FFF` at any palette call site is EQUIVALENT: every
    caller funnels the value through `store_rgba`, which extracts each
    component with `& 0x1F` after shifts of 0/5/10, so bit 15 can never reach
    an output byte. The bit-15 rule is therefore tested with the plausible
    defect instead (mutant 3: treat palette bit 15 as alpha), which is killed.
  - replacing `expand3` with `expand5` in A3I5 does not build (`-Werror`
    unused-function for the now-unused `expand3`), so the 3-bit table is
    covered by mutant 2 (formula) and mutant 39 (drops alpha bit 2).
- The colour-0 rule for formats 1/6/5/7 ("ignored") is covered by the existing
  `tex-decode/a3i5-16x8-key0-ignored`, `tex-decode/a5i3-8x32-key0-ignored`
  and `tex-decode/4x4-modes-8x8` (key colour on) cases, and by mutants 44
  (I2 only) and 49 (wrong key bit).
- Format 5 palette offsets are exercised up to the full 14-bit field
  (`tex-decode/4x4-high-palette-offsets`, 65,540-byte window, last entry at
  byte 65,532, plus a 20,000-byte window that a `*2` offset would wrongly
  accept), which is what kills mutants 14-17.

## Artifacts

- Scratch copies, binaries and logs: `/tmp/opencode/mutants5b2c/` (outside the
  worktree).
- `git status` shows no change under `common/`; only the new decoder, tests,
  tools, Makefile and docs changed.

## Independent run (supervisor)

An independent mutation run over `common/twlhd_tex_decode.c` (642 mutants,
567 killed) found three surviving TEST GAPS. No decoder behaviour was changed:
only new tests were added. The gaps and their new tests:

### Gap 1: one-element-too-far overruns (out-of-bounds)

Surviving mutant classes: loop bounds `i < texel_bytes` -> `<=` (A3I5 ~191,
A5I3 ~287, I2/I3/I4 outer ~208) and the inner `j < per_byte` -> `<=` (~210),
`per_byte` 4u -> 5u (~203), 16bpp `texel_bytes/2u` -> `/1u` (~297), the 4x4
`block < blocks` -> `<=` (~249) and `sy < 4u` -> `<=` / `4u -> 5u` (~260).
They read/write past the supplied buffers without any earlier test noticing.

New test: `tex-decode/oob-guards`, a reusable harness with four
`[64 guard][exact region][64 guard]` arrays (texel 256, palette 512, index 64,
out 512). It runs 14 cases x 2 passes: formats 1, 2, 3, 4, 6, 7 at 8x8 and 16x8,
plus format 5 at 8x8 and 16x8 with every block mode 2 and a palette window that
ends exactly at its 8th byte. Each pass asserts (a) every output guard byte is
unchanged (the sequential writes always clobber the adjacent guard first) and
(b) the whole decoded output is identical when the input regions are filled
with 0x00 versus 0xFF, so an over-read cannot hide. The harness comment maps
each check to the mutant classes it kills.

The Python golden path is structurally blind to these overruns and cannot be
extended to see them without changing tooling behaviour: `tex_decode_cli`
prints exactly `width*height` pixel lines and receives the exact
`out_size = pixels*4`, so an extra read lands inside the `TXDT` chunk / heap
buffer. The guard checks are therefore C-side only.

### Gap 2: 4x4 channel/position coverage

Surviving mutant classes: the alpha index `dst[3]` / `dst[7]` / `dst[11]`
(`[3]` -> `[2]` / `[4]`), the `(row >> 2)`, `(row >> 4)`, `(row >> 6)` shift
counts and `>>` -> `<<`, and some `7 -> 8` / `2 -> 1` constants in the 16
`dst[]` writes (lines ~268-281). The old 4x4 tests used colours where blue was
255 or alpha 0, so alpha-from-blue and shifted positions coincided.

New tests: `tex-decode/4x4-full-pixel-blocks` (C) and
`test_4x4_channel_position_blocks` (golden, tools/test_tex_decode_golden.py).
One 8x8 texture: block 0 is mode 2 with four pairwise-distinct opaque colours,
block 1 is mode 0 with three opaque colours plus transparent texel 3. Every row
byte is a 2-bit permutation (`0xE4`, `0x1B`, `0xB1`, `0x78`) so each position
holds a different index on every row. The C test asserts the full RGBA of all
16 pixels of both blocks against hand-written literal expected values
(including mode 0's alpha alternating 255/0, which catches skipped/stale
writes), and verifies the row bytes are permutations. The golden test builds
the same texture and lets the independent oracle confirm every pixel.

Residual: the `col[?][3] -> col[?][4]` variant reads 1-3 bytes past the stack
array `col[4][4]`. It is deterministically killed only under ASan/UBSan
(default-on through `HOST_SAN` in the Makefile); without sanitizers it is an
undefined stack read and is recorded here rather than "fixed" in the decoder.

### Gap 3: per-format full-RGBA coverage

Any `dst` channel assignment in A3I5/A5I3/I2/I3/I4/I8/16bpp had to be checked
with colours whose R, G, B (and A, where distinct) all differ, so a
source-channel mix-up cannot coincide with another channel. The new palette
scheme uses 5-bit components in 1..30, which are pairwise distinct for every
entry by construction, so every expanded channel is non-zero, non-255 and
different from the others; A3I5 uses alpha 2/5 (expanded 73/182, neither a
5-bit table value) and A5I3 picks a 5-bit alpha different from that pixel's
three components. Every asserted opaque pixel is additionally checked for
pairwise-distinct RGBA channels inside the test, so a bad colour choice fails
loudly.

New tests: `tex-decode/full-rgba-a3i5`, `tex-decode/full-rgba-a5i3`,
`tex-decode/full-rgba-i2`, `tex-decode/full-rgba-i4`,
`tex-decode/full-rgba-i8`, `tex-decode/full-rgba-16bpp`. Each fills an 8x8
texture so pixel `i` selects index `i % entries` (packed low-bits-first for
I2/I4, `(alpha << 5) | index` for A3I5, `(alpha << 3) | index` for A5I3),
asserts the full RGBA of every pixel from the pinned tables, and adds literal
first/last-pixel expectations; 16bpp includes transparent texels.

The Python golden path already mirrors this gap: `test_all_formats_random_payloads`
(tools/test_tex_decode_golden.py) asserts every pixel of every format against
the independent oracle.

### Equivalent classes not targeted (do not change the decoder for these)

- comment-text line-number mutants (e.g. 1524/1752 inside comments)
- the `fixed_palette_entries` table and its fail-safe check (unreachable after
  `twlhd_v2_tex_palette_size_ok`)
- `mode_palette_bytes` 6/8/4 (the window is even by construction)
- `col[4][4]` sizes
- `i < 3u` in 4x4 mode 1 (the average alpha is overwritten with 255)
- `base + 0u`

### Requirement traceability (independent run)

- REQ-1: out-of-bounds guard harness (`tex-decode/oob-guards`): formats 1-7 at
  8x8 and 16x8, output guard + input-guard comparison, 4x4 palette window
  exactly at its end; mutant classes documented in the harness comment.
- REQ-2: 4x4 channel/position coverage (`tex-decode/4x4-full-pixel-blocks`,
  golden `test_4x4_channel_position_blocks`): mode 2 and mode 0 blocks, 32
  literal pixels, per-row permutation patterns.
- REQ-3: per-format full-RGBA coverage (`tex-decode/full-rgba-a3i5`,
  `-a5i3`, `-i2`, `-i4`, `-i8`, `-16bpp`); golden
  `test_all_formats_random_payloads` already covers every pixel per format.

### Round 2 (repair)

Re-running the surviving mutants after repair 1 left two real gaps; everything
else is an accepted equivalent class (comment line numbers, the
`fixed_palette_entries` table/fail-safe, `mode_palette_bytes`, the `col[4][4]`
sizes, the mode-1 alpha loop, `base + 0u`). Gap 1: `dst[7]` alpha at pixel
position 1 of a 4x4 block row (`col[(row >> 2) & 3u][3]`) was never compared
where the shifted lookup selects a different alpha, so `(row >> 2)` ->
`(row >> 1)`/`(row >> 3)` and `>>` -> `<<` survived; new tests
`tex-decode/4x4-transparent-positions` and golden
`test_4x4_transparent_texel_positions` place the transparent texel (index 3) at
pixel positions 1, 2 and 3 of every row in modes 0 and 1, with neighbours
2/1/0 around position 1, and assert all 128 pixels with hand-computed literals.
Gap 2: the format-7 `i < texel_bytes / 2u` -> `i <= ...` survivor — root cause:
the 0x00/0xFF output-guard fills coincide exactly with what
`store_transparent`/`store_rgba` write for the extra iteration, so the write
was invisible; the fix is a fixed non-writable out-guard fill `OOB_OUT_FILL`
(0x5B, not in either expansion table) plus a new
`oob_fill_16bpp_last_opaque` variant so the two fmt7 guard cases cover the last
16-bit texel with bit 15 clear (8x8) and set (16x8). Scratch-mutant evidence:
`<=` at common/twlhd_tex_decode.c:297 now fails `tex-decode/oob-guards`
(fmt7-8x8 and fmt7-16x8); the line-272 mutants (`>> 2` -> `>> 1`/`>> 3`,
`>>` -> `<<`) fail `tex-decode/4x4-transparent-positions`; the loop-bound
mutants of formats 1-6 and the 4x4 block loop are still killed; the unmutated
baseline is green.

- REQ-4: 4x4 transparent-texel position coverage (modes 0/1, positions 1/2/3,
  neighbours 2/1/0 case), C + golden. TESTS: make test
- REQ-5: format-7 output-guard fill fix + last-texel bit-15 state coverage.
  TESTS: make test
