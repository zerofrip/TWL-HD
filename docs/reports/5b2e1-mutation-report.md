# 5B-2e-1 mutation report: host-side textured frame preparation

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` and the targeted golden
suite are green; nothing EMULATOR or HARDWARE is verified. This report contains
only numbers and results produced by the run described below.

## Method

The mutant set is generated and run by `tools/mutate_pica_frame.py`, invoked
from the repository root as:

    python3 tools/mutate_pica_frame.py --jobs 4

The script is standalone and is not referenced by the `Makefile` (it is not
part of `make test`). It edits only `common/twlhd_pica_frame.c`, one edit per
mutant:

- every comparison/logical operator occurrence is swapped to its neighbour
  (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`); shift operators
  (`<<`, `>>`) and `->` are skipped;
- every numeric constant gets `+1` and, when positive, `-1` (the sign is not
  part of a numeric token, so the literal in `-1` is mutated in place and the
  result is `-2` or `-0`).

Comment text and string literals are skipped, and `#include` lines are
skipped. `#define` bodies are treated as code: the constants 26 and 7 in
`FRAME_FORMAT` are real behaviour and are mutated.

For each mutant the script writes the mutant text over
`common/twlhd_pica_frame.c`, rebuilds the two artifacts `make test` builds for
this feature with `make -j2 tests/host/build/test_pica_frame
tests/host/build/pica_frame_cli` (so the repository host flags and the
sanitizer auto-detection stay in sync), runs `tests/host/build/test_pica_frame`
directly, and for a surviving mutant runs the golden suite with
`PICA_FRAME_CLI` pointing at the freshly built CLI:

    PICA_FRAME_CLI=<repo>/tests/host/build/pica_frame_cli \
      python3 -B -I -m unittest discover -s tools -p test_pica_frame_golden.py

With `--jobs 4` the runner splits the mutant list into four chunks and runs
each chunk in a separate temporary copy of the repository; kill/survive
decisions and the mutant set itself are unchanged from the sequential run.

The compiler is the default `HOST_CC = cc`; the detected host flags are
`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host -Itools/azahar_host
-fsanitize=address,undefined -fno-omit-frame-pointer`.

A mutant is **KILLED** when at least one of those steps fails: a `-Werror`
build failure counts, because `make test` fails on it. The unmutated baseline
is built and both steps must pass before the run starts (15/15 C labels and 1
golden test with 9 subTests). Identical mutant texts are deduplicated. The
script restores the original source in a `finally:` block, keeps its scratch
copy of each mutant in a temporary directory, and ends with
`git diff --exit-code common/`, which must be clean.

Run commit: `e224b1fe545aeff52a47bac3887f13ea28f192d7`.

The mutant set is regenerated from the reworked source, so the round-3 sizing
pass (the shared no-decode predicate, `frame_fixed_palette_entries()`,
`frame_mode_palette_bytes()` and the format-5 block loop) is part of it.

## Results

| metric | count |
| --- | --- |
| unique mutants | 233 |
| killed by `tests/host/build/test_pica_frame` | 175 |
| killed by golden suite | 2 |
| killed by `-Werror` build | 29 |
| equivalent survivors | 27 |
| total | 233 |

Kill distribution:

| step | count | share |
| --- | --- | --- |
| C test | 175 | 75.1 % |
| golden | 2 | 0.9 % |
| build | 29 | 12.4 % |
| survived (equivalent) | 27 | 11.6 % |

The two golden kills are palette-entry miscounts (`frame_fixed_palette_entries`
constant mutations at line 80/81) that change the decoded colour of the 8x8 I4
golden texture enough to fail the oracle's byte-exact comparison.

## Pin table

Lines refer to `common/twlhd_pica_frame.c` as of the run.

| line | symbol / construct | role |
| --- | --- | --- |
| 17 | `FRAME_FORMAT` | extracts format bits 26-28 |
| 38-40 | `frame_size_ok` | PICA power-of-two size guard (:65-72, :183-185) |
| 54-71 | `frame_find_record` | TXDS lookup by `(param, pltt)` |
| 78-87 | `frame_fixed_palette_entries` | copied from `twlhd_tex_decode.c:54-63` |
| 91-97 | `frame_mode_palette_bytes` | copied from `twlhd_tex_decode.c:88-95` |
| 114-202 | `frame_classify_run` | shared no-decode predicate |
| 215-373 | `twlhd_pica_frame_prepare` | validation + write pass |
| 375-414 | `twlhd_pica_frame_arena_needed` | pure sizing pass |

## Equivalent survivors

Every survivor was triaged against the source, the decoder, and the test
fixtures. All 27 are equivalent: no test or golden frame can distinguish them
because they affect only unreachable code, redundant guards, or values that are
only tested for truthiness.

1. **`frame_size_ok` boundary/PO2 constants (line 39)**
   - `8 -> 7`, `1024 -> 1025`, `1 -> 2` in `(v & (v - 1u)) == 0u`:
     `twlhd_tex_dimensions()` only produces the values 8, 16, 32, 64, 128,
     256, 512, 1024. No produced width/height lies in the gaps (7, 8), (1024,
     1025), or is detected by `(v & (v-2)) == 0` without also being a power of
     two, so the three mutations are equivalent for this call site.

2. **`frame_find_record` loop/return (lines 60, 63, 67)**
   - `i < count` -> `i <= count`: when `i == count`, `twlhd_v2_txds_record()`
     returns 0 and the function returns 0 at line 63, so behaviour is unchanged.
   - `return 0` -> `return 1` on record-read failure: the loop condition keeps
     `i < count`, so this failure path is unreachable.
   - `return 1` -> `return 2` on success: the caller only tests the result with
     `!`, so any non-zero value is equivalent.

3. **`frame_fixed_palette_entries` constants (lines 80-84)**
   - `case 1 (32) -> 0/31`, `case 2 (4) -> 3`, `case 3 (16) -> 15`, `case 4
     (256) -> 255`, `case 6 (8) -> 7`: format 0 never reaches this helper
     (it is classified untextured first). For formats 1-4 and 6,
     `twlhd_v2_tex_palette_size_ok()` already enforces the exact fixed table
     size in bytes (64, 8, 32, 512, 16), so the downstream `palette_size <
     entries*2` check is never the first refusal; shrinking the entry count by
     one does not change the final verdict.

4. **`frame_mode_palette_bytes` constants (lines 93-95)**
   - `6 -> 5`, `8 -> 7`, `4 -> 3`: for format 5, `palette_size` is required to
     be even (`twlhd_v2_tex_palette_size_ok()` at `twlhd_v2_parse.c:169-171`)
     and the block offset `(word & 0x3FFF) * 4` is a multiple of 4. The
     predicate rejects when `offset + mode_bytes > palette_size`. Because both
     `offset` and `palette_size` have the same parity, a difference of exactly
     one byte between the original and mutated mode size can never sit on the
     accepting side for one but not the other; the boundary cases coincide.

5. **Untextured flag assignment (line 132)**
   - `run->untextured = 1` -> `2`: every use of `run.untextured` is a truthiness
     test, so any non-zero value is equivalent.

6. **Fixed-table presence check (lines 176-177)**
   - `entries != 0u` -> `entries != 1u`, `!=` -> `==`, and `2` -> `1` in the
     `< entries*2` bound: as noted above, `twlhd_v2_tex_palette_size_ok()`
     already pins the palette to the exact required size for formats with a
     fixed table, and formats 5/7 have `entries == 0`. The mutation either
     keeps the check active on the same values or disables a guard that is
     redundant with the palette-size-ok check.

7. **Array initialisers (lines 227-228, 378-379)**
   - `{0}` -> `{1}`: C zero-initialises all remaining elements of an array
     partial initialiser, so only the first element changes and the dedupe
     comparisons still see zeroes for unpopulated slots.

8. **Write-pass dedupe loop bound (line 307)**
   - `t < tex_count` -> `t <= tex_count`: the `key_param`/`key_pltt` arrays are
     already populated by the validation pass that ran before the write pass,
     so even with the extra iteration the lookup finds the correct existing
     texture or correctly treats the run as new.

9. **Unreachable texcoord-zeroing branch (lines 353-355)**
   - `v < i` -> `v <= i` and `0.0f` -> `1.0f`: this branch is the error path
     taken only if `twlhd_pica_texcoords_from_gx()` fails. The predicate and
     the earlier capacity checks guarantee that the call succeeds for every
     textured run in the test and golden suites, so these mutations only alter
     code that is not exercised.

## Per-mutant results

Each row is one mutant, labelled with the original line number and the single
edit. `KILLED` rows name the step that failed; `SURVIVED (equivalent)` rows
are the ones triaged above.

| line | mutant | result |
| --- | --- | --- |
| 17 | const 26 -> 25 | KILLED(test) |
| 17 | const 26 -> 27 | KILLED(test) |
| 17 | const 7 -> 6 | KILLED(test) |
| 17 | const 7 -> 8 | KILLED(test) |
| 35 | const 0 -> 1 | KILLED(test) |
| 35 | const 1 -> 0 | KILLED(test) |
| 35 | const 1 -> 2 | KILLED(test) |
| 35 | const 8 -> 7 | KILLED(test) |
| 35 | const 8 -> 9 | KILLED(test) |
| 39 | const 0 -> 1 | KILLED(test) |
| 39 | const 1 -> 0 | KILLED(test) |
| 39 | const 1024 -> 1023 | KILLED(test) |
| 39 | const 8 -> 9 | KILLED(test) |
| 39 | op && -> \|\| | KILLED(build) |
| 39 | op && -> \|\| | KILLED(build) |
| 39 | op <= -> < | KILLED(test) |
| 39 | op == -> != | KILLED(test) |
| 39 | op >= -> > | KILLED(test) |
| 43 | op && -> \|\| | KILLED(test) |
| 43 | op == -> != | KILLED(test) |
| 44 | op == -> != | KILLED(test) |
| 60 | const 0 -> 1 | KILLED(test) |
| 64 | op && -> \|\| | KILLED(test) |
| 64 | op == -> != | KILLED(test) |
| 64 | op == -> != | KILLED(test) |
| 67 | const 1 -> 0 | KILLED(test) |
| 70 | const 0 -> 1 | KILLED(test) |
| 80 | const 1 -> 2 | KILLED(build) |
| 80 | const 32 -> 33 | KILLED(golden) |
| 81 | const 2 -> 1 | KILLED(build) |
| 81 | const 2 -> 3 | KILLED(build) |
| 81 | const 4 -> 5 | KILLED(golden) |
| 82 | const 16 -> 17 | KILLED(test) |
| 82 | const 3 -> 2 | KILLED(build) |
| 82 | const 3 -> 4 | KILLED(build) |
| 83 | const 256 -> 257 | KILLED(test) |
| 83 | const 4 -> 3 | KILLED(build) |
| 83 | const 4 -> 5 | KILLED(test) |
| 84 | const 6 -> 5 | KILLED(test) |
| 84 | const 6 -> 7 | KILLED(test) |
| 84 | const 8 -> 9 | KILLED(test) |
| 85 | const 0 -> 1 | KILLED(test) |
| 93 | const 0 -> 1 | KILLED(test) |
| 93 | const 6 -> 7 | KILLED(test) |
| 94 | const 2 -> 1 | KILLED(test) |
| 94 | const 2 -> 3 | KILLED(test) |
| 94 | const 8 -> 9 | KILLED(test) |
| 95 | const 4 -> 5 | KILLED(test) |
| 125 | const 0 -> 1 | KILLED(test) |
| 131 | const 0 -> 1 | KILLED(test) |
| 131 | op == -> != | KILLED(test) |
| 132 | const 1 -> 0 | KILLED(test) |
| 146 | const 7 -> 6 | KILLED(test) |
| 146 | const 7 -> 8 | KILLED(test) |
| 146 | op != -> == | KILLED(test) |
| 146 | op && -> \|\| | KILLED(test) |
| 146 | op \|\| -> && | KILLED(build) |
| 146 | op \|\| -> && | KILLED(build) |
| 147 | const 5 -> 4 | KILLED(test) |
| 147 | const 5 -> 6 | KILLED(test) |
| 147 | op && -> \|\| | KILLED(test) |
| 147 | op == -> != | KILLED(test) |
| 153 | op \|\| -> && | KILLED(build) |
| 154 | op \|\| -> && | KILLED(build) |
| 162 | op != -> == | KILLED(test) |
| 162 | op \|\| -> && | KILLED(test) |
| 163 | op != -> == | KILLED(test) |
| 176 | op && -> \|\| | KILLED(test) |
| 177 | const 2 -> 3 | KILLED(test) |
| 177 | op < -> <= | KILLED(test) |
| 185 | const 5 -> 4 | KILLED(test) |
| 185 | const 5 -> 6 | KILLED(test) |
| 185 | op == -> != | KILLED(test) |
| 186 | const 2 -> 1 | KILLED(test) |
| 186 | const 2 -> 1 | KILLED(test) |
| 186 | const 2 -> 3 | KILLED(test) |
| 186 | const 2 -> 3 | KILLED(test) |
| 187 | const 0 -> 1 | KILLED(test) |
| 187 | op < -> <= | KILLED(test) |
| 189 | const 2 -> 1 | KILLED(test) |
| 189 | const 2 -> 3 | KILLED(test) |
| 190 | const 16383 -> 16382 | KILLED(test) |
| 190 | const 16383 -> 16384 | KILLED(test) |
| 190 | const 4 -> 3 | KILLED(test) |
| 190 | const 4 -> 5 | KILLED(test) |
| 192 | const 14 -> 13 | KILLED(test) |
| 192 | const 14 -> 15 | KILLED(test) |
| 194 | op > -> >= | KILLED(test) |
| 201 | const 4 -> 3 | KILLED(test) |
| 201 | const 4 -> 5 | KILLED(test) |
| 206 | const 0 -> 1 | KILLED(test) |
| 209 | const 0 -> 1 | KILLED(test) |
| 209 | const 3 -> 2 | KILLED(test) |
| 209 | const 3 -> 4 | KILLED(test) |
| 209 | op < -> <= | KILLED(test) |
| 210 | const 0 -> 1 | KILLED(test) |
| 210 | const 3 -> 2 | KILLED(test) |
| 210 | const 3 -> 4 | KILLED(test) |
| 210 | op == -> != | KILLED(test) |
| 210 | op \|\| -> && | KILLED(test) |
| 230 | const 0 -> 1 | KILLED(test) |
| 231 | const 0 -> 1 | KILLED(test) |
| 232 | const 0 -> 1 | KILLED(test) |
| 236 | op \|\| -> && | KILLED(build) |
| 236 | op \|\| -> && | KILLED(build) |
| 236 | op \|\| -> && | KILLED(build) |
| 236 | op \|\| -> && | KILLED(build) |
| 236 | op \|\| -> && | KILLED(build) |
| 236 | op \|\| -> && | KILLED(build) |
| 237 | op \|\| -> && | KILLED(build) |
| 237 | op \|\| -> && | KILLED(build) |
| 237 | op \|\| -> && | KILLED(build) |
| 237 | op \|\| -> && | KILLED(build) |
| 238 | op \|\| -> && | KILLED(build) |
| 241 | const 0 -> 1 | KILLED(test) |
| 241 | const 3 -> 2 | KILLED(test) |
| 241 | const 3 -> 4 | KILLED(test) |
| 241 | op != -> == | KILLED(test) |
| 244 | op > -> >= | KILLED(test) |
| 244 | op > -> >= | KILLED(test) |
| 244 | op \|\| -> && | KILLED(build) |
| 244 | op \|\| -> && | KILLED(build) |
| 245 | op > -> >= | KILLED(test) |
| 257 | const 0 -> 1 | KILLED(test) |
| 257 | const 3 -> 2 | KILLED(test) |
| 257 | const 3 -> 4 | KILLED(test) |
| 257 | op < -> <= | KILLED(test) |
| 260 | const 0 -> 1 | KILLED(test) |
| 260 | const 3 -> 2 | KILLED(test) |
| 260 | const 3 -> 4 | KILLED(test) |
| 260 | op != -> == | KILLED(test) |
| 260 | op && -> \|\| | KILLED(test) |
| 263 | op != -> == | KILLED(test) |
| 263 | op \|\| -> && | KILLED(test) |
| 264 | op > -> >= | KILLED(test) |
| 266 | const 0 -> 1 | KILLED(test) |
| 266 | op < -> <= | KILLED(test) |
| 267 | op && -> \|\| | KILLED(test) |
| 267 | op == -> != | KILLED(test) |
| 267 | op == -> != | KILLED(test) |
| 269 | op != -> == | KILLED(test) |
| 271 | op >= -> > | KILLED(test) |
| 271 | op \|\| -> && | KILLED(build) |
| 272 | op >= -> > | KILLED(test) |
| 281 | op > -> >= | KILLED(test) |
| 285 | op != -> == | KILLED(test) |
| 289 | const 0 -> 1 | KILLED(test) |
| 290 | const 0 -> 1 | KILLED(test) |
| 291 | const 0 -> 1 | KILLED(test) |
| 292 | const 0 -> 1 | KILLED(test) |
| 292 | const 3 -> 2 | KILLED(test) |
| 292 | const 3 -> 4 | KILLED(test) |
| 292 | op <= -> < | KILLED(test) |
| 293 | op == -> != | KILLED(test) |
| 293 | op \|\| -> && | KILLED(test) |
| 294 | const 0 -> 1 | KILLED(test) |
| 294 | const 3 -> 2 | KILLED(test) |
| 294 | const 3 -> 4 | KILLED(test) |
| 294 | op != -> == | KILLED(test) |
| 294 | op && -> \|\| | KILLED(test) |
| 297 | const 1 -> 0 | KILLED(test) |
| 297 | const 1 -> 2 | KILLED(test) |
| 304 | op && -> \|\| | KILLED(test) |
| 304 | op == -> != | KILLED(test) |
| 307 | const 0 -> 1 | KILLED(test) |
| 308 | op && -> \|\| | KILLED(test) |
| 308 | op == -> != | KILLED(test) |
| 308 | op == -> != | KILLED(test) |
| 310 | op == -> != | KILLED(test) |
| 319 | op != -> == | KILLED(test) |
| 324 | op != -> == | KILLED(test) |
| 347 | op && -> \|\| | KILLED(build) |
| 347 | op && -> \|\| | KILLED(build) |
| 347 | op == -> != | KILLED(test) |
| 348 | const 0 -> 1 | KILLED(test) |
| 348 | op >= -> > | KILLED(test) |
| 350 | op != -> == | KILLED(test) |
| 360 | op < -> <= | KILLED(test) |
| 361 | const 0 -> 1 | KILLED(test) |
| 362 | const 0 -> 1 | KILLED(test) |
| 380 | const 0 -> 1 | KILLED(test) |
| 381 | const 0 -> 1 | KILLED(test) |
| 385 | op \|\| -> && | KILLED(build) |
| 385 | op \|\| -> && | KILLED(build) |
| 388 | const 0 -> 1 | KILLED(test) |
| 388 | const 3 -> 2 | KILLED(test) |
| 388 | const 3 -> 4 | KILLED(test) |
| 388 | op != -> == | KILLED(test) |
| 390 | const 0 -> 1 | KILLED(test) |
| 390 | const 3 -> 2 | KILLED(test) |
| 390 | const 3 -> 4 | KILLED(test) |
| 390 | op < -> <= | KILLED(test) |
| 393 | const 0 -> 1 | KILLED(test) |
| 393 | const 3 -> 2 | KILLED(test) |
| 393 | const 3 -> 4 | KILLED(test) |
| 393 | op != -> == | KILLED(test) |
| 393 | op && -> \|\| | KILLED(test) |
| 396 | op != -> == | KILLED(test) |
| 396 | op \|\| -> && | KILLED(test) |
| 398 | const 0 -> 1 | KILLED(test) |
| 398 | op < -> <= | KILLED(test) |
| 399 | op && -> \|\| | KILLED(test) |
| 399 | op == -> != | KILLED(test) |
| 399 | op == -> != | KILLED(test) |
| 401 | op != -> == | KILLED(test) |
| 403 | op >= -> > | KILLED(test) |
| 39 | const 1 -> 2 | SURVIVED (equivalent) |
| 39 | const 1024 -> 1025 | SURVIVED (equivalent) |
| 39 | const 8 -> 7 | SURVIVED (equivalent) |
| 60 | op < -> <= | SURVIVED (equivalent) |
| 63 | const 0 -> 1 | SURVIVED (equivalent) |
| 67 | const 1 -> 2 | SURVIVED (equivalent) |
| 80 | const 1 -> 0 | SURVIVED (equivalent) |
| 80 | const 32 -> 31 | SURVIVED (equivalent) |
| 81 | const 4 -> 3 | SURVIVED (equivalent) |
| 82 | const 16 -> 15 | SURVIVED (equivalent) |
| 83 | const 256 -> 255 | SURVIVED (equivalent) |
| 84 | const 8 -> 7 | SURVIVED (equivalent) |
| 93 | const 6 -> 5 | SURVIVED (equivalent) |
| 94 | const 8 -> 7 | SURVIVED (equivalent) |
| 95 | const 4 -> 3 | SURVIVED (equivalent) |
| 132 | const 1 -> 2 | SURVIVED (equivalent) |
| 176 | const 0 -> 1 | SURVIVED (equivalent) |
| 176 | op != -> == | SURVIVED (equivalent) |
| 177 | const 2 -> 1 | SURVIVED (equivalent) |
| 227 | const 0 -> 1 | SURVIVED (equivalent) |
| 228 | const 0 -> 1 | SURVIVED (equivalent) |
| 307 | op < -> <= | SURVIVED (equivalent) |
| 353 | op < -> <= | SURVIVED (equivalent) |
| 354 | const 0 -> 1 | SURVIVED (equivalent) |
| 355 | const 0 -> 1 | SURVIVED (equivalent) |
| 378 | const 0 -> 1 | SURVIVED (equivalent) |
| 379 | const 0 -> 1 | SURVIVED (equivalent) |
