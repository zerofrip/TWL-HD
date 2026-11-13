# 5B-2e-1 mutation report (round 1): host-side textured frame preparation

Status: **IMPLEMENTED**, BUILD/UNIT only. `make host-test` and the targeted
golden suite are green; nothing EMULATOR or HARDWARE is verified. This report
contains only numbers and results produced by the run described below.

## Method

Round 1 closes the survivors of an independent 212-mutant run of
`common/twlhd_pica_frame.c` (180 killed, 32 survived). The repair is
**tests only**; `common/` is byte-for-byte unchanged.

The mutant set is generated systematically by a script, one edit per mutant:

- every comparison/logical operator occurrence is swapped to its neighbour
  (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`); shift operators
  (`<<`, `>>`) and `->` are skipped;
- every numeric constant gets `+1` and, when positive, `-1`

inside code only (comments and `#include` lines are skipped). Each mutant is a
scratch copy of the ONE file `common/twlhd_pica_frame.c` under
`/tmp/opencode/mutants5b2e1_r1/mNNN/twlhd_pica_frame.c` (outside the
worktree). Every other translation unit is compiled once from the unmodified
worktree. Each mutant is compiled with the repository host flags
(`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
-Itools/azahar_host -fsanitize=address,undefined -fno-omit-frame-pointer`)
and linked into exactly the two artifacts `make test` builds for this
feature:

1. `tests/host/build/test_pica_frame` (the `host-test` step), run directly.
2. `tests/host/build/pica_frame_cli` + `tools/test_pica_frame_golden.py`,
   with `PICA_FRAME_CLI` pointing at the mutant CLI.

A mutant is **KILLED** when at least one of those two steps exits non-zero (a
`-Werror` build failure counts, because `make test` fails on it). The
unmutated baseline passes both steps (13/13 C test labels, 1 golden test with
9 subTests). Identical mutants are deduplicated by comparing the resulting
source text.

The runner is `python3 /tmp/opencode/run_mutants5b2e1_r1.py`; machine-readable
results are in `/tmp/opencode/mutants5b2e1_r1/results.json`.

**Reconciliation with the independent run.** That run's exact edit list is
not available, so "same method" was implemented as the systematic
enumeration above. It produces **192** unique mutants, not 212: the two
enumerations need not contain exactly the same edits (operator direction,
whether `0 -> -1` is generated, shift operators, and duplicates all change
the count), so the totals legitimately differ. Every one of the 32 survivors
listed in the independent run *is* present in this set. Of those 32, **10 are
killed** by the round-1 tests and **22 remain genuinely equivalent**; the
systematic set also found **6 further equivalents** (lines 83-85, 88, 279,
280) that the independent list did not mention, for **28 survivors total**.
No mutant is killed by the golden step alone in this run: the new C tests are
strictly stronger for every behaviour the CLI can express, and the C step runs
first, so it accounts for all test-step kills. The golden suite is still part
of the run and still passes for every survivor.

## Results

**192 unique mutants, 164 KILLED, 28 equivalent survivors.**

| Killing step | Mutants |
|---|---:|
| `test_pica_frame` (host-test) | 140 |
| build (`-Werror`, no binary produced) | 24 |
| golden (`test_pica_frame_golden.py`) | 0 |
| **Total killed** | **164** |
| equivalent survivors | 28 |
| **Total** | **192** |

The 24 build kills are all `-Werror=parentheses` failures from mixing `&&`
and `||` in one expression (`||` -> `&&` next to existing `&&`, or the
reverse), for example the size check at line 19 and the NULL/capacity checks
at lines 102, 107-108, 161-163, 169, 272 and 311. `make test` fails on them
exactly as it would for any non-compiling change.

## Tests added in round 1

Six new C labels (all added, none renamed or weakened):

| Label | Closes |
|---|---|
| `pica-frame/size-bounds-1024` | line 19 `1024 -> 1023` and `<= -> <`: 1024x8 and 8x1024 both must be OK, with exact 32768-byte scratch, 65536 arena bytes, offsets 0/32768 and hand-computed arena texels. |
| `pica-frame/scratch-exact` | line 114 `> -> >=`: scratch exactly 256 bytes for one 8x8 texture succeeds; 255 bytes fails with `ERR_CAPACITY` and leaves every output untouched; the same pair through `twlhd_pica_frame_arena_needed`. |
| `pica-frame/texture-cap-256` | lines 197/338 `>= -> >`: 256 distinct textures all stored (per-texture offsets, dims, indices, statuses and every arena byte hand-checked), plus a hand-built 257-record view whose `MAX+1` key must report `ERR_CAPACITY`; also kills the `tex_count`/`arena_used` initialiser mutants (lines 155, 156, 157, 306, 307) and pins the last TXDS record and the last bind-plan entry. |
| `pica-frame/format-index-matrix` | line 102 `5 -> 4` and `5 -> 6`: format 4 (I8) and format 6 (A5I3) without index data are OK; format 5 with index data is OK; a hand-built format-5 record whose index offset is out of bounds is `NO_DATA` (not `UNSUPPORTED`). |
| `pica-frame/failure-run-before-textured` | line 190 `|| -> &&`: a `NO_DATA` run and an untextured run before the first textured run must consume no texture slot, with `texture_capacity` equal to the real texture count. |
| `pica-frame/texcoord-tail-guard` | line 285 `< -> <=`: last run untextured, exact-size guarded texcoords (the zero loop must not write `texcoords[vertex_count]`), plus the 0x00/0xFF input-tail two-pass comparison. |

Gap (d) of the task (sizes 4, 2048, non-power-of-two, 0) is unreachable
through the public API: `twlhd_tex_dimensions()` returns
`8 << ((param >> 20) & 7)` / `8 << ((param >> 23) & 7)`, so the domain is
exactly `{8, 16, 32, 64, 128, 256, 512, 1024}`. The reachable boundaries 8 and
1024 are covered by the existing 8x8 tests and the new 1024 test; no fake
test was added for unreachable values.

Two golden frames were added to `_frame_specs()`: an untextured run first
(`[None, (a, 0), (b, 1)]`) and a NO_DATA run first with an untextured run last
(`[("missing", m2, 0), (c, 2), None]`). The CLI allocates the texture buffer
with capacity equal to the entry count and the scratch buffer at exactly the
largest decoded texture, so both frames also exercise the line-190 and exact
scratch behaviour through the independent Python oracle.

## Equivalent survivors

All 28 were confirmed empirically: each single edit was compiled, both steps
were run, and both passed. None is an untested behaviour change.

| Line(s) | Mutant | Why it cannot change an observable result |
|---|---|---|
| 19 | `8 -> 7` | `frame_size_ok` is only called at :108 with values from `twlhd_tex_dimensions` = `8 << (bits & 7)`, i.e. exactly `{8..1024}`; `>= 7` and `>= 8` agree on that domain. |
| 19 | `1024 -> 1025` | Same domain: no reachable size exceeds 1024, and for every power of two in `{8..1024}` both upper bounds accept. |
| 19 | `1 -> 2` (`v - 1` -> `v - 2`) | For every power of two `v >= 8`, `v & (v - 1) == 0` and `v & (v - 2) == 0`; 4, 2048, non-powers and 0 are unreachable (see gap (d) above). |
| 40 | `< -> <=` | The extra iteration is `i == count == view->txds_count`; `twlhd_v2_txds_record()` requires `index < txds_count` (parse.c:736) and returns 0, so the helper returns 0 exactly as on normal loop exit. No accessor can succeed at `i == count`. |
| 43 | `0 -> 1` | The only in-loop failure mode is `view->txds == NULL` (the index is always `< count`); then all three pointer accessors re-run `twlhd_v2_txds_record()` and fail identically, so the run is `NO_DATA` either way. The parser never produces `txds == NULL` with `txds_count > 0`. `rec_index`/`rec` are never read before being (re)derived. |
| 47 | `1 -> 2` | `frame_find_record()` is static with one call site (:94) which tests truthiness only (`!frame_find_record(...)`). |
| 72 | `rec_index = 0 -> 1` | Written at :45 before any read (:99/:101 are reached only after a successful find); on failure the function returns at :96 first. |
| 73, 74 | `width`/`height = 0 -> 1` | Always overwritten by `twlhd_tex_dimensions()` at :107 before the first read at :113; a failing dimensions call returns at :110 (and the `||` short-circuits) without reading them. |
| 83, 84, 85 | `*out_width`/`*out_height`/`*out_bytes = 0 -> 1` | Callers use these out-params only on the `status == OK && !untextured` path, where all three are overwritten at :123-125. Untextured, `NO_DATA` and `UNSUPPORTED` runs discard them. |
| 88 | `*out_untextured = 1 -> 2` | The flag is only tested for truthiness (`|| untextured`, `!untextured`) at :190, :240, :272, :331. |
| 90, 96, 104, 110, 119, 126 | `return 0 -> 1` | All three call sites (:186-189, :234-238, :327-330) test only `< 0`; every caller-relevant value flows through the out-params, which are written before each return. A non-negative return takes the classified path. |
| 114 | `return -1 -> -2` (`1 -> 2` in `-1`) | Same `< 0`-only use: both are negative and produce `ERR_CAPACITY`. |
| 152, 153, 304, 305 | `{0} -> {1}` key tables | The tables are append-only. Every slot read by a dedupe loop (`j`/`t < tex_count`) was written earlier in the same pass or in the validation pass, which stores keys in the same run order; an unwritten slot is never read. |
| 279, 280 | fallback zero-loop `u`/`v = 0 -> 1` | The fallback branch is unreachable: `twlhd_pica_texcoords_from_gx()` cannot fail here (pointers are non-NULL, width/height already passed `frame_size_ok`, and `out_capacity == vc >= count`), so the assignments never execute. |
| 243 | `< -> <=` | In the write pass, a run whose key is new has its validation-pass slot at index `tex_count` (the validation pass stored keys in the same run order and classification), so the extra probe at `t == tex_count` reads the run's own key and breaks at `t == tex_count`, taking the same store branch. Reused keys break earlier. At `tex_count == 256` the probe would be out of bounds, but that state is unreachable (256-record TXDS cap plus the MAX check). |
| 278 | `< -> <=` | Same unreachable fallback as lines 279/280; the loop body never executes. |

## Per-mutant results

| Mutant edit | Result | Killing step |
|---|---|---|
| `line 16 const 26 -> 27` | KILLED | `test_pica_frame` |
| `line 16 const 26 -> 25` | KILLED | `test_pica_frame` |
| `line 16 const 7 -> 8` | KILLED | `test_pica_frame` |
| `line 16 const 7 -> 6` | KILLED | `test_pica_frame` |
| `line 19 const 8 -> 9` | KILLED | `test_pica_frame` |
| `line 19 const 8 -> 7` | SURVIVED (equivalent) | - |
| `line 19 const 1024 -> 1025` | SURVIVED (equivalent) | - |
| `line 19 const 1024 -> 1023` | KILLED | `test_pica_frame` |
| `line 19 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 19 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 19 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 40 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 43 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 47 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 47 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 50 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 72 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 73 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 74 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 83 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 84 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 85 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 87 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 88 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 88 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 90 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 92 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 96 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 102 const 7 -> 8` | KILLED | `test_pica_frame` |
| `line 102 const 7 -> 6` | KILLED | `test_pica_frame` |
| `line 102 const 5 -> 6` | KILLED | `test_pica_frame` |
| `line 102 const 5 -> 4` | KILLED | `test_pica_frame` |
| `line 104 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 110 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 113 const 4 -> 5` | KILLED | `test_pica_frame` |
| `line 113 const 4 -> 3` | KILLED | `test_pica_frame` |
| `line 114 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 114 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 119 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 126 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 131 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 134 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 134 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 134 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 135 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 135 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 135 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 152 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 153 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 155 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 156 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 157 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 166 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 166 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 166 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 175 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 175 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 175 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 184 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 184 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 184 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 187 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 192 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 215 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 216 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 217 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 218 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 218 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 218 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 220 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 220 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 220 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 228 const 1 -> 2` | KILLED | `test_pica_frame` |
| `line 228 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 235 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 243 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 273 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 279 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 280 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 286 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 287 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 304 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 305 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 306 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 307 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 314 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 314 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 314 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 316 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 316 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 316 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 325 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 325 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 325 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 328 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 333 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 19 op >= -> >` | KILLED | `test_pica_frame` |
| `line 19 op && -> ||` | KILLED | build (`-Werror`) |
| `line 19 op <= -> <` | KILLED | `test_pica_frame` |
| `line 19 op && -> ||` | KILLED | build (`-Werror`) |
| `line 19 op == -> !=` | KILLED | `test_pica_frame` |
| `line 23 op == -> !=` | KILLED | `test_pica_frame` |
| `line 23 op && -> ||` | KILLED | `test_pica_frame` |
| `line 24 op == -> !=` | KILLED | `test_pica_frame` |
| `line 40 op < -> <=` | SURVIVED (equivalent) | - |
| `line 44 op == -> !=` | KILLED | `test_pica_frame` |
| `line 44 op && -> ||` | KILLED | `test_pica_frame` |
| `line 44 op == -> !=` | KILLED | `test_pica_frame` |
| `line 87 op == -> !=` | KILLED | `test_pica_frame` |
| `line 102 op || -> &&` | KILLED | build (`-Werror`) |
| `line 102 op != -> ==` | KILLED | `test_pica_frame` |
| `line 102 op && -> ||` | KILLED | `test_pica_frame` |
| `line 102 op || -> &&` | KILLED | build (`-Werror`) |
| `line 102 op == -> !=` | KILLED | `test_pica_frame` |
| `line 102 op && -> ||` | KILLED | `test_pica_frame` |
| `line 107 op || -> &&` | KILLED | build (`-Werror`) |
| `line 108 op || -> &&` | KILLED | build (`-Werror`) |
| `line 114 op > -> >=` | KILLED | `test_pica_frame` |
| `line 116 op != -> ==` | KILLED | `test_pica_frame` |
| `line 134 op < -> <=` | KILLED | `test_pica_frame` |
| `line 135 op == -> !=` | KILLED | `test_pica_frame` |
| `line 135 op || -> &&` | KILLED | `test_pica_frame` |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 161 op || -> &&` | KILLED | build (`-Werror`) |
| `line 162 op || -> &&` | KILLED | build (`-Werror`) |
| `line 162 op || -> &&` | KILLED | build (`-Werror`) |
| `line 162 op || -> &&` | KILLED | build (`-Werror`) |
| `line 162 op || -> &&` | KILLED | build (`-Werror`) |
| `line 163 op || -> &&` | KILLED | build (`-Werror`) |
| `line 166 op != -> ==` | KILLED | `test_pica_frame` |
| `line 169 op > -> >=` | KILLED | `test_pica_frame` |
| `line 169 op || -> &&` | KILLED | build (`-Werror`) |
| `line 169 op > -> >=` | KILLED | `test_pica_frame` |
| `line 169 op || -> &&` | KILLED | build (`-Werror`) |
| `line 170 op > -> >=` | KILLED | `test_pica_frame` |
| `line 175 op < -> <=` | KILLED | `test_pica_frame` |
| `line 184 op != -> ==` | KILLED | `test_pica_frame` |
| `line 184 op && -> ||` | KILLED | `test_pica_frame` |
| `line 187 op < -> <=` | KILLED | `test_pica_frame` |
| `line 190 op != -> ==` | KILLED | `test_pica_frame` |
| `line 190 op || -> &&` | KILLED | `test_pica_frame` |
| `line 192 op < -> <=` | KILLED | `test_pica_frame` |
| `line 193 op == -> !=` | KILLED | `test_pica_frame` |
| `line 193 op && -> ||` | KILLED | `test_pica_frame` |
| `line 193 op == -> !=` | KILLED | `test_pica_frame` |
| `line 195 op != -> ==` | KILLED | `test_pica_frame` |
| `line 197 op >= -> >` | KILLED | `test_pica_frame` |
| `line 197 op || -> &&` | KILLED | `test_pica_frame` |
| `line 198 op >= -> >` | KILLED | `test_pica_frame` |
| `line 207 op > -> >=` | KILLED | `test_pica_frame` |
| `line 211 op != -> ==` | KILLED | `test_pica_frame` |
| `line 218 op <= -> <` | KILLED | `test_pica_frame` |
| `line 219 op == -> !=` | KILLED | `test_pica_frame` |
| `line 219 op || -> &&` | KILLED | `test_pica_frame` |
| `line 220 op != -> ==` | KILLED | `test_pica_frame` |
| `line 220 op && -> ||` | KILLED | `test_pica_frame` |
| `line 235 op < -> <=` | KILLED | `test_pica_frame` |
| `line 240 op == -> !=` | KILLED | `test_pica_frame` |
| `line 240 op && -> ||` | KILLED | `test_pica_frame` |
| `line 243 op < -> <=` | SURVIVED (equivalent) | - |
| `line 244 op == -> !=` | KILLED | `test_pica_frame` |
| `line 244 op && -> ||` | KILLED | `test_pica_frame` |
| `line 244 op == -> !=` | KILLED | `test_pica_frame` |
| `line 246 op == -> !=` | KILLED | `test_pica_frame` |
| `line 249 op != -> ==` | KILLED | `test_pica_frame` |
| `line 272 op == -> !=` | KILLED | `test_pica_frame` |
| `line 272 op && -> ||` | KILLED | build (`-Werror`) |
| `line 272 op && -> ||` | KILLED | build (`-Werror`) |
| `line 273 op >= -> >` | KILLED | `test_pica_frame` |
| `line 275 op != -> ==` | KILLED | `test_pica_frame` |
| `line 278 op < -> <=` | SURVIVED (equivalent) | - |
| `line 285 op < -> <=` | KILLED | `test_pica_frame` |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 314 op != -> ==` | KILLED | `test_pica_frame` |
| `line 316 op < -> <=` | KILLED | `test_pica_frame` |
| `line 325 op != -> ==` | KILLED | `test_pica_frame` |
| `line 325 op && -> ||` | KILLED | `test_pica_frame` |
| `line 328 op < -> <=` | KILLED | `test_pica_frame` |
| `line 331 op != -> ==` | KILLED | `test_pica_frame` |
| `line 331 op || -> &&` | KILLED | `test_pica_frame` |
| `line 333 op < -> <=` | KILLED | `test_pica_frame` |
| `line 334 op == -> !=` | KILLED | `test_pica_frame` |
| `line 334 op && -> ||` | KILLED | `test_pica_frame` |
| `line 334 op == -> !=` | KILLED | `test_pica_frame` |
| `line 336 op != -> ==` | KILLED | `test_pica_frame` |
| `line 338 op >= -> >` | KILLED | `test_pica_frame` |
