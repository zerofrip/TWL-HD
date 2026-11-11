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
