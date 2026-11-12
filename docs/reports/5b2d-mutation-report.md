# 5B-2d mutation report: host-side PICA texture preparation

Status: **IMPLEMENTED**, host-side only. `make test` green. Nothing EMULATOR or
HARDWARE verified.

## Method

Every mutant is a scratch copy of the ONE file `common/twlhd_pica_tex.c` under
`/tmp/opencode/mutants5b2d/work/mNN/` (outside the worktree). Each mutant was
compiled against the **unmodified** `tests/host/test_pica_tex.c` and the
**unmodified** `tests/host/pica_tex_cli.c` with the repository's `HOST_CFLAGS`
(`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
-Itools/azahar_host -fsanitize=address,undefined -fno-omit-frame-pointer`),
producing exactly the two artifacts `make test` builds and runs for this
feature:

1. `tests/host/build/test_pica_tex` (the `host-test` step) — run directly; its
   `FAIL pica-tex/...` labels and ASan/UBSan aborts are the killers below.
2. `tests/host/build/pica_tex_cli` + `tools/test_pica_tex_golden.py` (the
   `python3 -m unittest discover -s tools` step) — the golden test was run with
   `PICA_TEX_CLI` pointing at the mutant CLI build.

A mutant is **KILLED** when at least one of those two `make test` steps exits
non-zero. The unmutated baseline passes both (13/13 C tests, 3/3 golden tests).
Mutants that abort under ASan/UBSan (out-of-bounds writes caused by a dropped
capacity check) fail `make test` and are listed as such.

Run: `python3 /tmp/opencode/mutants5b2d/run_mutants.py` (machine-readable
`/tmp/opencode/mutants5b2d/results.json`).

## Results

58 mutants, **58 KILLED, 0 survivors**. Coverage: Morton x/y interleave table
entries, tile column/row offsets, tile-row width, texel byte order (both
directions), the vertical flip (and its off-by-one), size validation
(min/max/power-of-two), output-size checks, all four wrap bit shifts, the
mirror/repeat/clamp table and its numeric enum, both axes, NULL checks on both
wrap outputs, texcoord s/t source, both normalisation divisors, the V flip,
sign and capacity check, and the bind plan's run key (both components in both
passes), capacity check, `%3`, entry counts, merge step, first-vertex index,
`untextured` mask/shift, `out_count` and `result_string` values.

| # | Mutant | Result | Killing `make test` step(s) |
|---:|---|---|---|
| 1 | morton-x-lut-1 | KILLED | `test_pica_tex`, `golden` |
| 2 | morton-x-lut-2 | KILLED | `test_pica_tex`, `golden` |
| 3 | morton-y-lut-1 | KILLED | `test_pica_tex`, `golden` |
| 4 | morton-y-lut-2 | KILLED | `test_pica_tex`, `golden` |
| 5 | swizzle-byte-r | KILLED | `test_pica_tex`, `golden` |
| 6 | swizzle-byte-g | KILLED | `test_pica_tex`, `golden` |
| 7 | swizzle-byte-b | KILLED | `test_pica_tex`, `golden` |
| 8 | swizzle-byte-a | KILLED | `test_pica_tex`, `golden` |
| 9 | swizzle-no-vflip | KILLED | `test_pica_tex`, `golden` |
| 10 | swizzle-tile-row-width | KILLED | `test_pica_tex`, `golden` |
| 11 | swizzle-tile-col-32 | KILLED | `test_pica_tex`, `golden` |
| 12 | swizzle-morton-x-mask3 | KILLED | `test_pica_tex`, `golden` |
| 13 | swizzle-morton-y-mask3 | KILLED | `test_pica_tex`, `golden` |
| 14 | swizzle-drop-tile-col | KILLED | `test_pica_tex`, `golden` |
| 15 | swizzle-vflip-off-by-one | KILLED | `test_pica_tex`, `golden` |
| 16 | swizzle-dst-size-disabled | KILLED | `test_pica_tex` |
| 17 | swizzle-width-check-dropped | KILLED | `test_pica_tex` |
| 18 | size-min-8-to-gt | KILLED | `test_pica_tex`, `golden` |
| 19 | size-max-1024-to-512 | KILLED | `test_pica_tex`, `golden` |
| 20 | size-pow2-disabled | KILLED | `test_pica_tex` |
| 21 | byte-size-times-3 | KILLED | `test_pica_tex` |
| 22 | unswizzle-byte-order | KILLED | `test_pica_tex`, `golden` |
| 23 | unswizzle-no-vflip | KILLED | `test_pica_tex`, `golden` |
| 24 | unswizzle-tile-row-width | KILLED | `test_pica_tex`, `golden` |
| 25 | unswizzle-dst-size-disabled | KILLED | `test_pica_tex` |
| 26 | unswizzle-drop-tile-col | KILLED | `test_pica_tex`, `golden` |
| 27 | wrap-repeat-s-shift | KILLED | `test_pica_tex` |
| 28 | wrap-repeat-t-shift | KILLED | `test_pica_tex` |
| 29 | wrap-flip-s-shift | KILLED | `test_pica_tex` |
| 30 | wrap-flip-t-shift | KILLED | `test_pica_tex` |
| 31 | wrap-s-mirror-or | KILLED | `test_pica_tex` |
| 32 | wrap-s-mirror-as-repeat | KILLED | `test_pica_tex` |
| 33 | wrap-s-repeat-as-clamp | KILLED | `test_pica_tex` |
| 34 | wrap-t-mirror-as-flip-only | KILLED | `test_pica_tex` |
| 35 | wrap-t-clamp-as-border | KILLED | `test_pica_tex` |
| 36 | wrap-null-t-dropped | KILLED | `test_pica_tex` |
| 37 | uv-u-from-t | KILLED | `test_pica_tex` |
| 38 | uv-no-vflip | KILLED | `test_pica_tex` |
| 39 | uv-u-divisor | KILLED | `test_pica_tex` |
| 40 | uv-v-divisor | KILLED | `test_pica_tex` |
| 41 | uv-capacity-off-by-one | KILLED | `test_pica_tex` |
| 42 | uv-v-sign | KILLED | `test_pica_tex` |
| 43 | uv-null-out-dropped | KILLED | `test_pica_tex` |
| 44 | bind-count-mod2 | KILLED | `test_pica_tex` |
| 45 | bind-count-no-pltt | KILLED | `test_pica_tex` |
| 46 | bind-write-no-pltt | KILLED | `test_pica_tex` |
| 47 | bind-capacity-ge | KILLED | `test_pica_tex` |
| 48 | bind-entry-count-4 | KILLED | `test_pica_tex` |
| 49 | bind-merge-6 | KILLED | `test_pica_tex` |
| 50 | bind-untextured-eq-1 | KILLED | `test_pica_tex` |
| 51 | bind-untextured-shift25 | KILLED | `test_pica_tex` |
| 52 | bind-outcount-dropped | KILLED | `test_pica_tex` |
| 53 | bind-null-outcount-dropped | KILLED | `test_pica_tex` |
| 54 | bind-capacity-check-disabled | KILLED | `test_pica_tex` |
| 55 | bind-first-vertex-plus1 | KILLED | `test_pica_tex` |
| 56 | bind-count-no-param | KILLED | `test_pica_tex` |
| 57 | bind-write-no-param | KILLED | `test_pica_tex` |
| 58 | result-string-size-ok | KILLED | `test_pica_tex` |

## Notes

- **No survivors needed an equivalence argument.** Every mutation changes
  observable output, an error result, or triggers an ASan/UBSan abort.
- The two capacity/run-key mutants that only mis-count runs (`bind-count-no-pltt`
  and `bind-write-no-pltt` variants) are killed because the one-entry plan test
  passes a buffer of exactly `capacity` elements: an undercounted run count
  makes the write pass overrun it under ASan.
- `bind-untextured-shift25` is not equivalent: shifting by 25 instead of 26
  reads `SizeShiftT` bit 2 as PackedFormat bit 0, so a format-0 texture with
  `SizeShiftT = 4` would be treated as textured; the test pins
  `TEXIMAGE_PARAM = 0x02000000`.
- Mutating comment text was not used and does not count.

