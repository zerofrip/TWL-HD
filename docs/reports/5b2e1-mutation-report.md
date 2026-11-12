# 5B-2e-1 mutation report: host-side textured frame preparation

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` is green; nothing
EMULATOR or HARDWARE is verified. This report contains only numbers and
results produced by the run described below.

## Method

Every mutant is a scratch copy of the ONE file `common/twlhd_pica_frame.c`
under `/tmp/opencode/mutants5b2e1/mNNN/twlhd_pica_frame.c` (outside the
worktree). Every other translation unit was compiled once from the
unmodified worktree. Each mutant was compiled with the repository host flags
(`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
-Itools/azahar_host -fsanitize=address,undefined -fno-omit-frame-pointer`)
and linked into exactly the two artifacts `make test` builds for this
feature:

1. `tests/host/build/test_pica_frame` (the `host-test` step), run directly.
2. `tests/host/build/pica_frame_cli` + `tools/test_pica_frame_golden.py`
   (the `python3 -m unittest discover -s tools` step), with
   `PICA_FRAME_CLI` pointing at the mutant CLI.

A mutant is **KILLED** when at least one of those two steps exits non-zero.
A mutant that stops compiling under `-Werror` is also killed by `make test`
and is listed as `build`. The unmutated baseline passes both steps (7/7 C
test labels, 1/1 golden test).

The set is systematic but curated by hand: operators on every comparison,
logical operator, arithmetic operator and constant that affects run
grouping, keying, dedupe, offsets, capacities, texcoords, status routing and
the result strings, plus +/-1 edits on loop strides, bounds, shifts, format
masks, sizes, indices and the alignment/offset arithmetic. Each entry is one
edit; the runner checks that every edit is unique, so no byte-identical
mutant is counted twice. The set was generated and executed by
`python3 /tmp/opencode/run_mutants5b2e1.py`; machine-readable results are in
`/tmp/opencode/mutants5b2e1/results.json`.

The test file also gained one label during this round,
`pica-frame/dedupe-key-halves`, which pins the case where two records share
only the `pltt_base` half of the dedupe key and the arena is one byte short:
the call must still report `ERR_ARENA` from the sizing pass.

## Results

**92 unique mutants, 80 KILLED, 12 equivalent survivors.**

| Killing step | Mutants |
|---|---:|
| `test_pica_frame` (host-test) | 74 |
| `test_pica_frame_golden.py` (golden) | 5 |
| build (`-Werror`, no binary produced) | 1 |
| **Total killed** | **80** |
| equivalent survivors | 12 |
| **Total** | **92** |

The build-killed mutant is `cap-and` (the `runs > plan_capacity &&
runs > entry_capacity || ...` edit trips `-Werror=parentheses`); `make
test` fails on it exactly as it would for any non-compiling change, so it is
counted as killed.

## Equivalent survivors

All 12 survivors are provably equivalent for every input the module
contract admits; none is an untested behaviour change.

| Mutant | Why it cannot change an observable result |
|---|---|
| `pow2-minus-two` | The size check only ever sees values produced by `twlhd_tex_dimensions` (`8 << shift`), i.e. powers of two. For every power of two `v >= 4`, `v & (v - 1) == 0` and `v & (v - 2) == 0`, so no test input can distinguish the two. |
| `rec-loop-inclusive` | The extra iteration calls `twlhd_v2_txds_record(view, count, &rec)`, which returns false, and `frame_find_record` immediately returns 0 — the same result as leaving the loop. A match in the real range returns before the extra iteration. |
| `rec-invalid-returns-hit` | The `return 1` is dead for every view produced by `twlhd_v2_parse()`: the loop index is `< txds_count`, the view is non-NULL and `rec` is a valid local, so the accessor cannot fail. A hand-built view that violates the parsed-view precondition is outside the module contract; under it the branch is unreachable. |
| `eval-skip-dropped` | Re-evaluating a run that has the same key as the previous one is redundant work, not a result change: the per-key linear scan finds the stored texture and `continue`s, so `tex_count`, the key table and `arena_used` are unchanged. Scratch is documented as undefined on return, so its contents cannot be observed. |
| `max-tex-gt` | A texture is only stored after a TXDS record with the exact `(param, pltt)` pair was found, and the format guarantees the pair is unique (`twlhd_v2_parse.c:358-366`). With at most 256 records, a 257th distinct stored key (the first index the mutated `>` accepts) is unreachable. |
| `write-boundary-ge` | In the write loop `i` takes only multiples of 3 up to `vertex_count`, so `i >= vertex_count` is true exactly when `i == vertex_count`; no other value of `i` can satisfy it. |
| `write-arena-capacity-full` | The capacity argument only feeds `twlhd_pica_tex_swizzle_rgba8`'s `dst_size < width*height*4` check. Pass 1 already proved `arena_capacity >= sum of all texture bytes`, so `arena_capacity` is always at least the remaining need at every call; the swizzle result and every written byte are unchanged. |
| `texcoord-drop-index-test` | When `status == OK && !untextured`, `tex_index` is either a reused index `>= 0` or a freshly stored one `>= 0`; a failed swizzle changes `status` to `UNSUPPORTED` before this condition. The dropped `tex_index >= 0` conjunct is therefore always true here. |
| `zero-loop-inclusive` | This is the fallback loop used only when `twlhd_pica_texcoords_from_gx` fails. With valid width/height (already checked), valid pointers, `vc >= 3` and `texcoords` capacity `>= vc` (checked in pass 1), that helper cannot return anything but OK, so the whole branch is unreachable. |
| `zero-u-one` | Same unreachable fallback branch as `zero-loop-inclusive`; the assignment never executes. |
| `zero-v-one` | Same unreachable fallback branch as `zero-loop-inclusive`; the assignment never executes. |
| `needed-max-gt` | The `arena_needed` copy of the same 256-record bound: at most 256 distinct `(param, pltt)` keys can match a TXDS record, so `tex_count` never reaches 257 and the mutated bound is equivalent to `>=`. |

## Per-mutant results

| Mutant | Edit | Result | Killing step |
|---|---|---|---|
| `size-min-strict` | size lower bound >= 8 -> > 8 | KILLED | test |
| `size-max-strict` | size upper bound <= 1024 -> < 1024 | KILLED | golden |
| `pow2-test-inverted` | power-of-two test == 0 -> != 0 | KILLED | test |
| `pow2-minus-two` | power-of-two v-1 -> v-2 | SURVIVED (equivalent) | - |
| `same-key-param` | run key param == -> != | KILLED | test |
| `same-key-pltt` | run key pltt == -> != | KILLED | test |
| `same-key-and` | run key && -> || | KILLED | test |
| `rec-param-ne` | record lookup param == -> != | KILLED | test |
| `rec-pltt-ne` | record lookup pltt == -> != | KILLED | test |
| `rec-loop-inclusive` | record loop i < count -> <= count | SURVIVED (equivalent) | - |
| `rec-invalid-returns-hit` | invalid record treated as match | SURVIVED (equivalent) | - |
| `format-shift-25` | format shift 26 -> 25 | KILLED | test |
| `format-mask-3` | format mask 7 -> 3 | KILLED | test |
| `untextured-inverted` | format 0 check == -> != | KILLED | test |
| `palette-skip-inverted` | format 7 palette test != -> == | KILLED | test |
| `palette-or` | palette condition && -> || | KILLED | test |
| `index-test-inverted` | format 5 index test == -> != | KILLED | test |
| `index-or` | index condition && -> || | KILLED | test |
| `bytes-times-3` | bytes per texel 4 -> 3 | KILLED | test |
| `bytes-times-8` | bytes per texel 4 -> 8 | KILLED | test |
| `scratch-ge` | scratch fit > -> >= | KILLED | golden |
| `scratch-inverted` | scratch fit > -> < | KILLED | test |
| `decode-ok-inverted` | decode != OK -> == OK | KILLED | test |
| `runs-i-plus2` | run count i += 3 -> 2 | KILLED | test |
| `runs-i-plus4` | run count i += 3 -> 4 | KILLED | test |
| `runs-first-ne` | run count first i == 0 -> != | KILLED | test |
| `runs-double` | run count ++ -> += 2 | KILLED | test |
| `null-drop-entry-status` | NULL check drops entry_status | KILLED | test |
| `null-drop-arena-out` | NULL check drops out_arena_used | KILLED | test |
| `count-mod2` | vertex count mod 3 -> mod 2 | KILLED | test |
| `count-inverted` | vertex count mod test != -> == | KILLED | test |
| `cap-plan-ge` | plan capacity > -> >= | KILLED | test |
| `cap-entry-ge` | entry capacity > -> >= | KILLED | test |
| `cap-texcoord-ge` | texcoord capacity > -> >= | KILLED | test |
| `cap-and` | capacity checks || -> && | KILLED | build |
| `eval-loop-plus2` | validation loop i += 3 -> 2 | KILLED | test |
| `eval-first-ne` | validation skip first i != 0 -> != 1 | KILLED | test |
| `eval-skip-dropped` | validation never skips equal runs | SURVIVED (equivalent) | - |
| `eval-status-and` | classify status || -> && | KILLED | golden |
| `eval-untextured-dropped` | classify ignores untextured flag | KILLED | golden |
| `dedupe-and` | validation dedupe && -> || | KILLED | test |
| `dedupe-param-ne` | validation dedupe param == -> != | KILLED | test |
| `dedupe-pltt-ne` | validation dedupe pltt == -> != | KILLED | test |
| `dedupe-loop-inclusive` | validation dedupe j < -> <= | KILLED | test |
| `max-tex-gt` | max texture count >= -> > | SURVIVED (equivalent) | - |
| `texcap-gt` | texture capacity >= -> > | KILLED | test |
| `arena-sum-plus1` | validation arena sum +1 | KILLED | test |
| `arena-check-ge` | arena check > -> >= | KILLED | test |
| `arena-check-inverted` | arena check > -> < | KILLED | test |
| `write-loop-lt` | write loop <= vertex_count -> < | KILLED | test |
| `write-boundary-ge` | write boundary == -> >= | SURVIVED (equivalent) | - |
| `write-vc-plus1` | run length i-j -> i-j+1 | KILLED | test |
| `write-arena-offset-plus1` | texture start offset +1 (alignment) | KILLED | test |
| `write-arena-capacity-full` | swizzle gets full capacity | SURVIVED (equivalent) | - |
| `write-arena-capacity-minus1` | swizzle capacity off by one | KILLED | test |
| `write-arena-sum-plus1` | write arena sum +1 | KILLED | test |
| `tex-offset-plus128` | stored texture offset +128 | KILLED | test |
| `tex-width-swapped` | stored width <- height | KILLED | test |
| `tex-height-swapped` | stored height <- width | KILLED | test |
| `tex-index-plus1` | new texture index +1 | KILLED | test |
| `tex-count-plus2` | write tex_count += 2 | KILLED | test |
| `write-dedupe-and` | write dedupe && -> || | KILLED | test |
| `write-dedupe-inverted` | write dedupe found test == -> != | KILLED | test |
| `write-reuse-plus1` | reused texture index +1 | KILLED | test |
| `status-always-ok` | entry status forced OK | KILLED | test |
| `entry-texture-zero` | entry texture index forced 0 | KILLED | test |
| `run-index-plus2` | run index += 2 | KILLED | test |
| `texcoord-drop-index-test` | texcoord ignores tex_index >= 0 | SURVIVED (equivalent) | - |
| `texcoord-index-gt` | texcoord tex_index >= 0 -> > 0 | KILLED | test |
| `texcoord-size-swapped` | texcoord width/height swapped | KILLED | test |
| `texcoord-offset-i` | texcoord destination indexed by i | KILLED | test |
| `zero-loop-inclusive` | zeroed texcoord loop < -> <= | SURVIVED (equivalent) | - |
| `zero-u-one` | zeroed u <- 1.0 | SURVIVED (equivalent) | - |
| `zero-v-one` | zeroed v <- 1.0 | SURVIVED (equivalent) | - |
| `zero-loop-inclusive-else` | else zeroed loop < -> <= | KILLED | golden |
| `zero-u-one-else` | else zeroed u <- 1.0 | KILLED | test |
| `zero-v-one-else` | else zeroed v <- 1.0 | KILLED | test |
| `j-next-plus1` | next run start j = i + 1 | KILLED | test |
| `out-runs-plus1` | entry count +1 | KILLED | test |
| `out-tex-plus1` | texture count +1 | KILLED | test |
| `out-arena-plus1` | arena used +1 | KILLED | test |
| `needed-count-mod2` | arena_needed mod 3 -> mod 2 | KILLED | test |
| `needed-loop-plus2` | arena_needed loop i += 2 | KILLED | test |
| `needed-skip-first` | arena_needed first-run test | KILLED | test |
| `needed-dedupe-and` | arena_needed dedupe && -> || | KILLED | test |
| `needed-max-gt` | arena_needed max >= -> > | SURVIVED (equivalent) | - |
| `needed-sum-minus1` | arena_needed sum -1 | KILLED | test |
| `needed-out-plus1` | arena_needed out +1 | KILLED | test |
| `result-ok-wrong` | result_string OK -> ERR_NULL | KILLED | test |
| `result-default-ok` | result_string default -> OK | KILLED | test |
| `status-ok-wrong` | entry_status_string OK -> NO_DATA | KILLED | test |
| `status-default-ok` | entry_status_string default -> OK | KILLED | test |

Rerun: `python3 /tmp/opencode/run_mutants5b2e1.py` (scratch artifacts and
`results.json` under `/tmp/opencode/mutants5b2e1/`).
