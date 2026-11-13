# 5B-2e-2 mutation report: host-testable textured draw plan

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` is green; nothing EMULATOR
or HARDWARE is verified. This report contains only numbers and results produced
by the run described below.

## Method

The mutant set is generated and run by
`docs/reports/5b2e2_mutate_tex_draw_plan.py`, invoked from the repository root
as:

    python3 docs/reports/5b2e2_mutate_tex_draw_plan.py --jobs 4

The script is standalone and is not referenced by the `Makefile` (it is not
part of `make test`). It edits only `common/twlhd_tex_draw_plan.c`, one edit
per mutant:

- every comparison/logical operator occurrence is swapped to its neighbour
  (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`); shift operators
  (`<<`, `>>`) and `->` are skipped;
- every numeric constant gets `+1` and, when positive, `-1` (the sign is not
  part of a numeric token, so the literal in `-1` is mutated in place and the
  result is `-2` or `-0`).

Comment text and string literals are skipped, and `#include` lines are skipped.
`#define` bodies are treated as code because the wrap/filter constants are real
behaviour.

For each mutant the script writes the mutant text over
`common/twlhd_tex_draw_plan.c`, rebuilds the host test
`tests/host/build/test_tex_draw_plan` with `make -j2
tests/host/build/test_tex_draw_plan` (so the repository host flags and the
sanitizer auto-detection stay in sync), and runs
`tests/host/build/test_tex_draw_plan` directly.

With `--jobs 4` the runner splits the mutant list into four chunks and runs
each chunk in a separate temporary copy of the repository; kill/survive
decisions and the mutant set itself are unchanged from the sequential run.

The compiler is the default `HOST_CC = cc`; the detected host flags are
`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host -Itools/azahar_host
-fsanitize=address,undefined -fno-omit-frame-pointer`.

A mutant is **KILLED** when the build or the C test fails; a `-Werror` build
failure counts, because `make test` fails on it. The unmutated baseline is
built and the test must pass before the run starts. Identical mutant texts are
deduplicated. The script restores the original source in a `finally:` block
and ends with `git diff --exit-code common/`, which must be clean. Because the
target file is new in this worktree, the empty intent-to-add index entries were
removed (`git reset HEAD -- common/twlhd_tex_draw_plan.c
common/twlhd_tex_draw_plan.h`) before the run; with that, `git diff
--exit-code common/` is clean after restoration.

## Results

| metric | count |
| --- | --- |
| unique mutants | 62 |
| killed by `tests/host/build/test_tex_draw_plan` | 26 |
| killed by `-Werror` build | 16 |
| survived | 20 |
| total | 62 |

Kill distribution:

| step | count | share |
| --- | --- | --- |
| C test | 26 | 41.9 % |
| build | 16 | 25.8 % |
| survived | 20 | 32.3 % |

The runner command and final summary:

    $ python3 docs/reports/5b2e2_mutate_tex_draw_plan.py --jobs 4
    baseline: build OK, test OK
    mutants: 62 unique
    ...
    killed by test:   26
    killed by build:  16
    survived:         20
    total:            62
    common/ restored: YES

## Pin table

Lines refer to `common/twlhd_tex_draw_plan.c` as of the run.

| line | symbol / construct | role |
| --- | --- | --- |
| 12 | `TEX_DRAW_ALIGN_MASK` | 127 = alignment mask for the 128-byte arena assumption |
| 24-117 | `twlhd_tex_draw_plan_build` | main draw-plan builder |
| 38-43 | `out_result` initialisation | defensive defaults before validation |
| 45-48 | NULL guard | reject missing required pointers |
| 50-53 | empty-entry-count early return | `NO_TEXTURE` for zero entries |
| 55-58 | budget guard | capacity/arena/texcoord overflow fallback |
| 60-64 | alignment guard | `arena_used > 0` and 128-byte alignment check |
| 66-73 | texture size loop | PICA power-of-two 8..1024 guard + per-texture byte cap |
| 75-97 | per-entry mapping | slot/status -> draw descriptor, wrap/filter assignment |
| 99-105 | result write | final verdict and counts |
| 108-117 | `twlhd_tex_draw_verdict_string` | human-readable verdict names |

## Survivor triage

Every survivor was inspected against the source and the test fixtures. None are
silent bugs; they fall into two groups: equivalent initialisation writes, and
boundary mutations that the current test corpus does not reach.

### Equivalent initialisation writes (lines 40-42)

The `if (out_result)` block at lines 38-43 sets defensive defaults before any
validation. Every success path overwrites `verdict`, `entry_count`,
`texture_count` and `all_untextured` at lines 99-103 before returning. Every
error path returns a distinct verdict and the caller is expected to use the
return value, not the possibly-stale `out_result`. Mutating the initial `0`
/`1` literals therefore cannot change any observable test outcome.

| line | mutant | why it survives |
| --- | --- | --- |
| 40 | const 0 -> 1 | `entry_count` is overwritten on success; unused on error paths. |
| 41 | const 0 -> 1 | `texture_count` is overwritten on success; unused on error paths. |
| 42 | const 1 -> 0 / 1 -> 2 | `all_untextured` is overwritten on success; unused on error paths. |

### Boundary mutations not reached by the test corpus (lines 56, 60, 66, 69-70, 78)

These mutations are real semantic changes, but the hand-computed fixtures do
not land on the equality boundaries they would alter. They are classified as
**not killed** rather than equivalent.

| line | mutant | what it would change | why it survives |
| --- | --- | --- | --- |
| 56 | op > -> >= (two occurrences) | Would reject inputs where a budget value exactly equals its cap (e.g. `entry_count == out_capacity`). | Fixtures always use values strictly above or below the cap, never equality. |
| 60 | const 0 -> 1 | `arena_used > 1u` would skip the alignment check when `arena_used == 1`. | No fixture uses `arena_used == 1`. |
| 66 | const 0 -> 1 | Loop starts at `i = 1`, skipping the first texture. | The single-texture fixture (`test_failed_entries_untextured`) has all entries unsupported, so the skipped texture is never referenced. |
| 69 | const 8 -> 7 / 8 -> 9 / 1024 -> 1023 / 1024 -> 1025 / op > -> >= | Change the inclusive/exclusive width/height bounds. | All fixtures use `width == height == 8`; none hit the 8/1024 boundaries. |
| 70 | const 4 -> 3 / 4 -> 5 / op > -> >= | Change the per-texture byte bound (`w*h*3` or `w*h*5` or inclusive). | Fixtures use 8x8 textures (256 bytes) against a 4 MiB cap; boundary not reached. |
| 78 | op >= -> > | `slot > texture_count - 1` is integer-equivalent to `slot >= texture_count`. | Genuinely equivalent for `int32_t` slot values. |

## Per-mutant results

Each row is one mutant, labelled with the original line number and the single
edit. `KILLED` rows name the step that failed; `SURVIVED` rows are the ones
triaged above.

| line | mutant | result |
| --- | --- | --- |
| 12 | const 127 -> 126 | KILLED(test) |
| 12 | const 127 -> 128 | KILLED(test) |
| 36 | const 1 -> 0 | KILLED(test) |
| 36 | const 1 -> 2 | KILLED(test) |
| 45 | op \|\| -> && | KILLED(build) |
| 45 | op \|\| -> && | KILLED(build) |
| 45 | op \|\| -> && | KILLED(build) |
| 45 | op \|\| -> && | KILLED(build) |
| 46 | op \|\| -> && | KILLED(build) |
| 50 | const 0 -> 1 | KILLED(test) |
| 50 | op == -> != | KILLED(test) |
| 55 | op > -> >= | KILLED(test) |
| 55 | op > -> >= | KILLED(test) |
| 55 | op \|\| -> && | KILLED(build) |
| 55 | op \|\| -> && | KILLED(build) |
| 56 | op \|\| -> && | KILLED(build) |
| 60 | op && -> \|\| | KILLED(test) |
| 60 | op > -> >= | KILLED(build) |
| 61 | op == -> != | KILLED(test) |
| 61 | op \|\| -> && | KILLED(test) |
| 62 | const 0 -> 1 | KILLED(test) |
| 62 | op != -> == | KILLED(test) |
| 66 | op < -> <= | KILLED(test) |
| 69 | const 8 -> 9 | KILLED(test) |
| 69 | const 8 -> 9 | KILLED(test) |
| 69 | op < -> <= | KILLED(test) |
| 69 | op < -> <= | KILLED(test) |
| 69 | op \|\| -> && | KILLED(build) |
| 69 | op \|\| -> && | KILLED(build) |
| 69 | op \|\| -> && | KILLED(build) |
| 69 | op \|\| -> && | KILLED(build) |
| 75 | const 0 -> 1 | KILLED(test) |
| 75 | op < -> <= | KILLED(test) |
| 78 | const 0 -> 1 | KILLED(test) |
| 78 | op < -> <= | KILLED(test) |
| 78 | op \|\| -> && | KILLED(build) |
| 78 | op \|\| -> && | KILLED(build) |
| 79 | op \|\| -> && | KILLED(build) |
| 80 | op != -> == | KILLED(test) |
| 84 | const 1 -> 0 | KILLED(test) |
| 84 | const 1 -> 2 | KILLED(test) |
| 95 | const 0 -> 1 | KILLED(test) |
| 40 | const 0 -> 1 | SURVIVED |
| 41 | const 0 -> 1 | SURVIVED |
| 42 | const 1 -> 0 | SURVIVED |
| 42 | const 1 -> 2 | SURVIVED |
| 56 | op > -> >= | SURVIVED |
| 56 | op > -> >= | SURVIVED |
| 60 | const 0 -> 1 | SURVIVED |
| 66 | const 0 -> 1 | SURVIVED |
| 69 | const 1024 -> 1023 | SURVIVED |
| 69 | const 1024 -> 1023 | SURVIVED |
| 69 | const 1024 -> 1025 | SURVIVED |
| 69 | const 1024 -> 1025 | SURVIVED |
| 69 | const 8 -> 7 | SURVIVED |
| 69 | const 8 -> 7 | SURVIVED |
| 69 | op > -> >= | SURVIVED |
| 69 | op > -> >= | SURVIVED |
| 70 | const 4 -> 3 | SURVIVED |
| 70 | const 4 -> 5 | SURVIVED |
| 70 | op > -> >= | SURVIVED |
| 78 | op >= -> > | SURVIVED |
