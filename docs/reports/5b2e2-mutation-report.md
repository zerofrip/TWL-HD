# 5B-2e-2 mutation report: host-testable textured draw plan

Status: **IMPLEMENTED**, BUILD/UNIT/EMULATOR. `make test` is green and the
Azahar emulator screenshots reproduce with the procedure in
docs/EMULATOR_VALIDATION.md; HARDWARE is not verified. This report contains only
numbers and results produced by the run described below.

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

The target file now contains both the original draw-plan builder and the new
pure lifecycle helper (`twlhd_tex_lifecycle_*`), so the mutant set covers both.

The compiler is the default `HOST_CC = cc`; the detected host flags are
`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host -Itools/azahar_host
-fsanitize=address,undefined -fno-omit-frame-pointer`.

A mutant is **KILLED** when the build or the C test fails; a `-Werror` build
failure counts, because `make test` fails on it. The unmutated baseline is
built and the test must pass before the run starts. Identical mutant texts are
deduplicated. The script restores the original source in a `finally:` block
and ends with `git diff --exit-code common/`. Because the file was modified as
part of this repair, the final `git diff` check is non-zero even though the
mutant edits were restored; no mutation state remains in the working tree.

## Results

| metric | count |
| --- | --- |
| unique mutants | 93 |
| killed by `tests/host/build/test_tex_draw_plan` | 68 |
| killed by `-Werror` build | 18 |
| survived | 7 |
| total | 93 |

Kill distribution:

| step | count | share |
| --- | --- | --- |
| C test | 68 | 73.1 % |
| build | 18 | 19.4 % |
| survived | 7 | 7.5 % |

The runner command and final summary:

    $ python3 docs/reports/5b2e2_mutate_tex_draw_plan.py --jobs 4
    baseline: build OK, test OK
    mutants: 93 unique
    ...
    killed by test:   68
    killed by build:  18
    survived:         7
    total:            93
    common/ restored: NO

## Pin table

Lines refer to `common/twlhd_tex_draw_plan.c` as of the run.

| line | symbol / construct | role |
| --- | --- | --- |
| 13 | `TEX_DRAW_ALIGN_MASK` | 127 = alignment mask for the 128-byte arena assumption |
| 24-108 | `twlhd_tex_draw_plan_build` | main draw-plan builder |
| 39-43 | `out_result` initialisation | defensive defaults before validation |
| 46-48 | NULL guard | reject missing required pointers |
| 51-53 | empty-entry-count early return | `NO_TEXTURE` for zero entries |
| 56-58 | budget guard | capacity/arena/texcoord overflow fallback |
| 61-64 | alignment guard | `arena_used > 0` and 128-byte alignment check |
| 67-75 | texture size loop | PICA power-of-two 8..1024 guard + per-texture byte cap |
| 77-99 | per-entry mapping | slot/status -> draw descriptor, wrap/filter assignment |
| 101-105 | result write | final verdict and counts |
| 110-118 | `twlhd_tex_draw_verdict_string` | human-readable verdict names |
| 121-183 | `twlhd_tex_lifecycle_*` | pure pending/active release state machine |

## Survivor triage

Every survivor was inspected against the source and the test fixtures. None are
silent bugs; they are all equivalent for the inputs that can reach the mutated
line after the preceding guards.

| line | mutant | why it survives |
| --- | --- | --- |
| 70 | const 1024 -> 1025 (two occurrences) | `w > 1024u` -> `w > 1025u`. Any PICA power-of-two width/height larger than 1024 is at least 2048 and is still rejected; non-power-of-two values >1024 are rejected by the `(w & (w-1)) != 0` check before this comparison matters. Equivalent for all reachable inputs. |
| 70 | const 8 -> 7 (two occurrences) | `w < 8u` -> `w < 7u`. The smallest valid power-of-two is 8, and any dimension `< 8` is also `< 7`; non-power-of-two values `>= 8` are rejected by the power-of-two check. Equivalent for all reachable inputs. |
| 71 | const 1 -> 2 (two occurrences) | `(w & (w - 1u)) != 0u` -> `(w & (w - 2u)) != 0u`. For every power-of-two value `>= 8` both expressions are zero; non-power-of-two values are rejected earlier. Exhaustively verified for 8..1024. Equivalent for all reachable inputs. |
| 160 | const 0 -> 1 | `s->active_ready = 0` inside `commit()` is immediately followed by `s->active_ready = 1` at line 162, so the assignment is overwritten before the function returns. The state machine action flags are identical either way. |

### Corrected slot-bound argument

The original report claimed the mutant `slot >= (int32_t)texture_count`
-> `slot > (int32_t)texture_count` was "integer-equivalent". This is wrong.
With `texture_count == 0`, the expression `texture_count - 1` wraps in unsigned
arithmetic, but the actual mutation is on the cast `int32_t` comparison: the
mutant accepts `slot == texture_count` and then dereferences
`textures[slot]`, reading one element past the end of the texture array.
In the final source the comparison lives at line 80 of
`common/twlhd_tex_draw_plan.c` (the line shifted down after adding the
power-of-two guard).

The new test `test_slot_equals_count()` exercises exactly this case
(`entry_texture[0] == texture_count == 1`, status OK, textured plan entry) and
kills the mutant: the original code treats the entry as untextured and returns
`NO_TEXTURE`; the mutant reads `textures[1]` (inside the guard-byte harness)
and would return `OK` with a garbage texture slot.

## Per-mutant results

Each row is one mutant, labelled with the original line number and the single
edit. `KILLED` rows name the step that failed; `SURVIVED` rows are the ones
triaged above.

| line | mutant | result |
| --- | --- | --- |
| 13 | const 127 -> 126 | KILLED(test) |
| 13 | const 127 -> 128 | KILLED(test) |
| 37 | const 1 -> 0 | KILLED(test) |
| 37 | const 1 -> 2 | KILLED(test) |
| 41 | const 0 -> 1 | KILLED(test) |
| 42 | const 0 -> 1 | KILLED(test) |
| 43 | const 1 -> 0 | KILLED(test) |
| 43 | const 1 -> 2 | KILLED(test) |
| 46 | op \|\| -> && | KILLED(build) |
| 46 | op \|\| -> && | KILLED(build) |
| 46 | op \|\| -> && | KILLED(build) |
| 46 | op \|\| -> && | KILLED(build) |
| 47 | op \|\| -> && | KILLED(build) |
| 51 | const 0 -> 1 | KILLED(test) |
| 51 | op == -> != | KILLED(test) |
| 56 | op > -> >= | KILLED(test) |
| 56 | op > -> >= | KILLED(test) |
| 56 | op \|\| -> && | KILLED(build) |
| 56 | op \|\| -> && | KILLED(build) |
| 57 | op > -> >= | KILLED(test) |
| 57 | op > -> >= | KILLED(test) |
| 57 | op \|\| -> && | KILLED(build) |
| 61 | const 0 -> 1 | KILLED(test) |
| 61 | op && -> \|\| | KILLED(test) |
| 61 | op > -> >= | KILLED(build) |
| 62 | op == -> != | KILLED(test) |
| 62 | op \|\| -> && | KILLED(test) |
| 63 | const 0 -> 1 | KILLED(test) |
| 63 | op != -> == | KILLED(test) |
| 67 | const 0 -> 1 | KILLED(test) |
| 67 | op < -> <= | KILLED(test) |
| 70 | const 1024 -> 1023 | KILLED(test) |
| 70 | const 1024 -> 1023 | KILLED(test) |
| 70 | const 8 -> 9 | KILLED(test) |
| 70 | const 8 -> 9 | KILLED(test) |
| 70 | op < -> <= | KILLED(test) |
| 70 | op < -> <= | KILLED(test) |
| 70 | op > -> >= | KILLED(test) |
| 70 | op > -> >= | KILLED(test) |
| 70 | op \|\| -> && | KILLED(build) |
| 70 | op \|\| -> && | KILLED(build) |
| 70 | op \|\| -> && | KILLED(build) |
| 70 | op \|\| -> && | KILLED(build) |
| 71 | const 0 -> 1 | KILLED(test) |
| 71 | const 0 -> 1 | KILLED(test) |
| 71 | const 1 -> 0 | KILLED(test) |
| 71 | const 1 -> 0 | KILLED(test) |
| 71 | op != -> == | KILLED(test) |
| 71 | op != -> == | KILLED(test) |
| 71 | op \|\| -> && | KILLED(build) |
| 71 | op \|\| -> && | KILLED(build) |
| 72 | const 4 -> 3 | KILLED(test) |
| 72 | const 4 -> 5 | KILLED(test) |
| 72 | op > -> >= | KILLED(test) |
| 77 | const 0 -> 1 | KILLED(test) |
| 77 | op < -> <= | KILLED(test) |
| 80 | const 0 -> 1 | KILLED(test) |
| 80 | op < -> <= | KILLED(test) |
| 80 | op >= -> > | KILLED(test) |
| 80 | op \|\| -> && | KILLED(build) |
| 80 | op \|\| -> && | KILLED(build) |
| 81 | op \|\| -> && | KILLED(build) |
| 82 | op != -> == | KILLED(test) |
| 86 | const 1 -> 0 | KILLED(test) |
| 86 | const 1 -> 2 | KILLED(test) |
| 97 | const 0 -> 1 | KILLED(test) |
| 122 | const 0 -> 1 | KILLED(test) |
| 129 | const 1 -> 0 | KILLED(test) |
| 129 | const 1 -> 2 | KILLED(test) |
| 130 | const 1 -> 0 | KILLED(test) |
| 130 | const 1 -> 2 | KILLED(test) |
| 138 | const 0 -> 1 | KILLED(test) |
| 139 | const 0 -> 1 | KILLED(test) |
| 141 | const 1 -> 0 | KILLED(test) |
| 141 | const 1 -> 2 | KILLED(test) |
| 153 | const 0 -> 1 | KILLED(test) |
| 154 | const 0 -> 1 | KILLED(test) |
| 162 | const 1 -> 0 | KILLED(test) |
| 162 | const 1 -> 2 | KILLED(test) |
| 163 | const 0 -> 1 | KILLED(test) |
| 164 | const 1 -> 0 | KILLED(test) |
| 164 | const 1 -> 2 | KILLED(test) |
| 178 | const 0 -> 1 | KILLED(test) |
| 179 | const 0 -> 1 | KILLED(test) |
| 180 | const 0 -> 1 | KILLED(test) |
| 181 | const 0 -> 1 | KILLED(test) |
| 70 | const 1024 -> 1025 | SURVIVED |
| 70 | const 1024 -> 1025 | SURVIVED |
| 70 | const 8 -> 7 | SURVIVED |
| 70 | const 8 -> 7 | SURVIVED |
| 71 | const 1 -> 2 | SURVIVED |
| 71 | const 1 -> 2 | SURVIVED |
| 160 | const 0 -> 1 | SURVIVED |
