# 5B-2e-1 mutation report: host-side textured frame preparation

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` and the targeted golden
suite are green; nothing EMULATOR or HARDWARE is verified. This report contains
only numbers and results produced by the runs described below.

## Method

The mutant set is generated and run by
`docs/reports/5b2e1_mutate_pica_frame.py`, invoked from the repository root as:

    python3 docs/reports/5b2e1_mutate_pica_frame.py --jobs 4

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
is built and both steps must pass before the run starts (17/17 C labels and 1
golden test with 10 subTests). Identical mutant texts are deduplicated. The
script restores the original source in a `finally:` block, keeps its scratch
copy of each mutant in a temporary directory, and ends with
`git diff --exit-code common/`, which must be clean.

Run commit: `f4cf556` (the round-6 fix on top of `d38e6e4`, applied in a
throwaway copy of this checkout because the runner's final
`git diff --exit-code common/` needs the fixed `common/` to be committed; the
delivered worktree keeps the same fix uncommitted and the file contents are
identical).

The mutant set is regenerated from the reworked source, so the round-3 sizing
pass (the shared no-decode predicate, `frame_fixed_palette_entries()`,
`frame_mode_palette_bytes()` and the format-5 block loop) and the round-6
zero-vertex early return are part of it.

## Results

| metric | count |
| --- | --- |
| unique mutants | 238 |
| killed by `tests/host/build/test_pica_frame` | 180 |
| killed by golden suite | 2 |
| killed by `-Werror` build | 29 |
| equivalent survivors | 27 |
| total | 238 |

Kill distribution:

| step | count | share |
| --- | --- | --- |
| C test | 180 | 75.6 % |
| golden | 2 | 0.8 % |
| build | 29 | 12.2 % |
| survived (equivalent) | 27 | 11.3 % |

The two golden kills are palette-entry miscounts (`frame_fixed_palette_entries`
constant mutations at line 80/81) that change the decoded colour of the 8x8 I4
golden texture enough to fail the oracle's byte-exact comparison.

## Round 5: format-5 palette-window gap

An independent mutation run found a gap the committed mutant set cannot reach:
swapping the shift in `frame_mode_palette_bytes((uint32_t)word >> 14)` (line
192) to `<< 14` made the mode argument garbage, so the switch almost always
took `default: return 4u` and mode-0 (6-byte) and mode-2 (8-byte) blocks were
checked against 4 bytes only. No test had a mode-0 or mode-2 block whose
palette window ended within 4-7 bytes of `palette_size`, so the mutant
survived.

Added tests (no change to `common/`):

- `tests/host/test_pica_frame.c`: one new function
  `test_fmt5_palette_window()` (label `pica-frame/fmt5-palette-window`, so the
  C suite now reports 16 labels) pinning the window rule per block mode for
  `arena_needed` AND `prepare`, with `arena_needed == arena_used` and
  identical per-entry status in every case: mode 0 at offset 4 exact fit
  (10) vs 2 bytes short (8); mode 2 at offset 4 exact fit (12) vs a window
  that fits 4 but not 8 (8); modes 1 and 3 at offset 4 exact fit (8) vs 2
  bytes short (6); and the same mode-2 offender as the LAST and as the FIRST
  of the four blocks.
- `tools/test_pica_frame_golden.py`: `_make_entry`/`_build_frame` take a
  per-entry format-5 index word and palette size, and the frame list gains a
  frame with a mode-0 exact fit (6), a mode-2 exact fit (8), and a mode-2
  window that fits 4 but not 8 (8; the oracle's pre-scan raises
  `ERR_PALETTE_RANGE`, so that entry is the suite's first `UNSUPPORTED`
  result). The golden suite ran 1 test with 10 subTests.

The shift swap is **outside the committed mutant set by design**: the runner's
`OP_RE` (`docs/reports/5b2e1_mutate_pica_frame.py:55`) matches `<<`, `>>` and
`->` only so they can be skipped, and `OP_SWAP` has no entry for them. The
shift mutant is therefore verified manually, with `common/` restored
afterwards:

    make -j2 tests/host/build/test_pica_frame tests/host/build/pica_frame_cli
    ./tests/host/build/test_pica_frame
    PICA_FRAME_CLI=$PWD/tests/host/build/pica_frame_cli \
      python3 -B -I -m unittest discover -s tools -p test_pica_frame_golden.py

With `>> 14` changed to `<< 14` on line 192 the C suite fails
(`FAIL pica-frame/fmt5-palette-window (4 failed checks)`, each
`tests/host/test_pica_frame.c:941: expected 0u == 0, got 256` from the
`arena_needed == 0` assertion), while the golden suite still passes because
the real decoder re-checks the same rule in the write pass. After restoring
the file, `git diff --exit-code common/` is clean and the C suite is green
again.

The full rerun on the final code, from the repository root:

    python3 docs/reports/5b2e1_mutate_pica_frame.py --jobs 4

It reports "baseline: build OK, test OK, golden OK", "mutants: 238 unique",
`killed by test: 180`, `killed by golden: 2`, `killed by build: 29`,
`survived: 27`, `total: 238`, "common/ restored: YES". The totals and the
survivor list are unchanged from the table above; all 27 survivors remain
equivalent, and the parity argument for the `frame_mode_palette_bytes`
constants (lines 93-95; item 4 below) is unchanged and still correct. The
manual `>> -> <<` kill is not counted in those totals.

## Round 6: zero-vertex write-pass out-of-bounds

An independent review found a real bug, confirmed against the source: the
write pass walked `for (i = 0; i <= vertex_count; i += 3u)` and treated
`i == vertex_count` as a run boundary. With `vertex_count == 0` the very
first iteration (`i == j == 0`) took that branch: `frame_classify_run(tex,
view, 0, &run)` read `tex[0]` past an empty vertex array, and the pass wrote
`entry_status[0]`/`entry_texture[0]` (and, with a matching TXDS record,
decoded and wrote `textures[0]` and the arena) although `runs == 0` had
passed every capacity check with `entry_capacity`, `texture_capacity` and
`arena_capacity` all 0. That broke the explicit-capacity contract.

Fix (`common/twlhd_pica_frame.c:243-254`): after the NULL and `% 3` checks,
`vertex_count == 0u` returns `TWLHD_PICA_FRAME_OK` with all three counts 0.
`runs == 0` already passed every capacity check and `arena_used == 0` the
arena check, so the result is behaviour-identical for correct code, and
`vertex_count > 0` is untouched (only this insertion was added; no other line
changed). The other run walkers were checked and needed no change:
`frame_count_runs()` (:217), the validation pass (:270) and
`twlhd_pica_frame_arena_needed()` (:403) all walk `i < vertex_count`, which
is empty when `vertex_count == 0`.

New test `pica-frame/zero-vertex-guards` (the 17th C label): `vertex_count ==
0` with `plan_capacity`, `texture_capacity`, `arena_capacity`,
`texcoord_capacity` and `entry_capacity` all 0, `tex[0]` a valid textured key
(`PARAM_A`, pltt 0) matching the parsed one-record TXDS frame (so the old
code really decodes and swizzles), and a one-element vertex array inside
guard bytes whose tail is 0x00 in pass A and 0xFF in pass B. The call
returns `OK` with all three counts 0, every guard around plan, textures,
arena, texcoords, entry_texture and entry_status is untouched, and the two
passes agree byte for byte. The same frame goes through
`twlhd_pica_frame_arena_needed()` (returns `OK`, `*out_arena_bytes == 0`, the
vertex array and the parsed view byte-identical afterwards), and `tex ==
NULL` still returns `ERR_NULL` with untouched out-words from both functions
(the documented NULL rule is unchanged). On the unfixed code the test fails:
`FAIL pica-frame/zero-vertex-guards (4 failed checks)` from
`guard_intact(g_oob_et, 0u, OOB_FILL)` and `guard_intact(g_oob_es, 0u,
OOB_FILL)`.

The full rerun on the final code, from the repository root:

    python3 docs/reports/5b2e1_mutate_pica_frame.py --jobs 4

It reports "baseline: build OK, test OK, golden OK", "mutants: 238 unique",
`killed by test: 180`, `killed by golden: 2`, `killed by build: 29`,
`survived: 27`, `total: 238`, "common/ restored: YES". The five new mutants
generated from the early return (`==`/`!=` on line 249 and `0u -> 1u` on
lines 250-252) are all killed by the C suite; there are no new survivors and
the 27 survivors are the same equivalence classes as items 1-9 below.

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
| 217-226 | `frame_count_runs` | run count (walks `i < vertex_count`) |
| 228-386 | `twlhd_pica_frame_prepare` | validation + write pass |
| 243-254 | zero-vertex early return | empty frame: OK with three 0 counts |
| 388-427 | `twlhd_pica_frame_arena_needed` | pure sizing pass |

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

7. **Array initialisers (lines 227-228, 391-392)**
   - `{0}` -> `{1}`: C zero-initialises all remaining elements of an array
     partial initialiser, so only the first element changes and the dedupe
     comparisons still see zeroes for unpopulated slots.

8. **Write-pass dedupe loop bound (line 320)**
   - `t < tex_count` -> `t <= tex_count`: the `key_param`/`key_pltt` arrays are
     already populated by the validation pass that ran before the write pass,
     so even with the extra iteration the lookup finds the correct existing
     texture or correctly treats the run as new.

9. **Unreachable texcoord-zeroing branch (lines 366-368)**
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
| 249 | const 0 -> 1 | KILLED(test) |
| 249 | op == -> != | KILLED(test) |
| 250 | const 0 -> 1 | KILLED(test) |
| 251 | const 0 -> 1 | KILLED(test) |
| 252 | const 0 -> 1 | KILLED(test) |
| 257 | op > -> >= | KILLED(test) |
| 257 | op > -> >= | KILLED(test) |
| 257 | op \|\| -> && | KILLED(build) |
| 257 | op \|\| -> && | KILLED(build) |
| 258 | op > -> >= | KILLED(test) |
| 270 | const 0 -> 1 | KILLED(test) |
| 270 | const 3 -> 2 | KILLED(test) |
| 270 | const 3 -> 4 | KILLED(test) |
| 270 | op < -> <= | KILLED(test) |
| 273 | const 0 -> 1 | KILLED(test) |
| 273 | const 3 -> 2 | KILLED(test) |
| 273 | const 3 -> 4 | KILLED(test) |
| 273 | op != -> == | KILLED(test) |
| 273 | op && -> \|\| | KILLED(test) |
| 276 | op != -> == | KILLED(test) |
| 276 | op \|\| -> && | KILLED(test) |
| 277 | op > -> >= | KILLED(test) |
| 279 | const 0 -> 1 | KILLED(test) |
| 279 | op < -> <= | KILLED(test) |
| 280 | op && -> \|\| | KILLED(test) |
| 280 | op == -> != | KILLED(test) |
| 280 | op == -> != | KILLED(test) |
| 282 | op != -> == | KILLED(test) |
| 284 | op >= -> > | KILLED(test) |
| 284 | op \|\| -> && | KILLED(test) |
| 285 | op >= -> > | KILLED(test) |
| 294 | op > -> >= | KILLED(test) |
| 298 | op != -> == | KILLED(test) |
| 302 | const 0 -> 1 | KILLED(test) |
| 303 | const 0 -> 1 | KILLED(test) |
| 304 | const 0 -> 1 | KILLED(test) |
| 305 | const 0 -> 1 | KILLED(test) |
| 305 | const 3 -> 2 | KILLED(test) |
| 305 | const 3 -> 4 | KILLED(test) |
| 305 | op <= -> < | KILLED(test) |
| 306 | op == -> != | KILLED(test) |
| 306 | op \|\| -> && | KILLED(test) |
| 307 | const 0 -> 1 | KILLED(test) |
| 307 | const 3 -> 2 | KILLED(test) |
| 307 | const 3 -> 4 | KILLED(test) |
| 307 | op != -> == | KILLED(test) |
| 307 | op && -> \|\| | KILLED(test) |
| 310 | const 1 -> 0 | KILLED(test) |
| 310 | const 1 -> 2 | KILLED(test) |
| 317 | op && -> \|\| | KILLED(test) |
| 317 | op == -> != | KILLED(test) |
| 320 | const 0 -> 1 | KILLED(test) |
| 321 | op && -> \|\| | KILLED(test) |
| 321 | op == -> != | KILLED(test) |
| 321 | op == -> != | KILLED(test) |
| 323 | op == -> != | KILLED(test) |
| 332 | op != -> == | KILLED(test) |
| 337 | op != -> == | KILLED(test) |
| 360 | op && -> \|\| | KILLED(build) |
| 360 | op && -> \|\| | KILLED(build) |
| 360 | op == -> != | KILLED(test) |
| 361 | const 0 -> 1 | KILLED(test) |
| 361 | op >= -> > | KILLED(test) |
| 363 | op != -> == | KILLED(test) |
| 373 | op < -> <= | KILLED(test) |
| 374 | const 0 -> 1 | KILLED(test) |
| 375 | const 0 -> 1 | KILLED(test) |
| 393 | const 0 -> 1 | KILLED(test) |
| 394 | const 0 -> 1 | KILLED(test) |
| 398 | op \|\| -> && | KILLED(build) |
| 398 | op \|\| -> && | KILLED(build) |
| 401 | const 0 -> 1 | KILLED(test) |
| 401 | const 3 -> 2 | KILLED(test) |
| 401 | const 3 -> 4 | KILLED(test) |
| 401 | op != -> == | KILLED(test) |
| 403 | const 0 -> 1 | KILLED(test) |
| 403 | const 3 -> 2 | KILLED(test) |
| 403 | const 3 -> 4 | KILLED(test) |
| 403 | op < -> <= | KILLED(test) |
| 406 | const 0 -> 1 | KILLED(test) |
| 406 | const 3 -> 2 | KILLED(test) |
| 406 | const 3 -> 4 | KILLED(test) |
| 406 | op != -> == | KILLED(test) |
| 406 | op && -> \|\| | KILLED(test) |
| 409 | op != -> == | KILLED(test) |
| 409 | op \|\| -> && | KILLED(test) |
| 411 | const 0 -> 1 | KILLED(test) |
| 411 | op < -> <= | KILLED(test) |
| 412 | op && -> \|\| | KILLED(test) |
| 412 | op == -> != | KILLED(test) |
| 412 | op == -> != | KILLED(test) |
| 414 | op != -> == | KILLED(test) |
| 416 | op >= -> > | KILLED(test) |
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
| 320 | op < -> <= | SURVIVED (equivalent) |
| 366 | op < -> <= | SURVIVED (equivalent) |
| 367 | const 0 -> 1 | SURVIVED (equivalent) |
| 368 | const 0 -> 1 | SURVIVED (equivalent) |
| 391 | const 0 -> 1 | SURVIVED (equivalent) |
| 392 | const 0 -> 1 | SURVIVED (equivalent) |
