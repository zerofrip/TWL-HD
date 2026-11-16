# Mutation report: `tools/check_tex_formats.py` tolerance review

Runner: `docs/reports/tex_formats_tolerance_mutate.py` (not part of
`make test`). Report produced 2026-11-16 in the task worktree.

    python3 docs/reports/tex_formats_tolerance_mutate.py --jobs 4

Mechanism (copied from `docs/reports/5c3_mutate_tex_compare.py`): every mutant
is one edit of `tools/check_tex_formats.py` in a temporary sandbox copy of the
repository; `CheckerTest` runs
(`python3 -B -I -m unittest discover -s tools -p test_tex_formats.py -k
CheckerTest`); a nonzero/erroring run kills the mutant. The file is restored
in `finally:` and the run verifies the checkout afterwards.

Mutant set: every numeric constant +-1 and every comparison operator swapped in
code (strings/comments blanked), plus 21 targeted mutants of the cell/bbox/
tolerance logic. Baseline `CheckerTest`: OK.

## Result

| mutants | killed | survived | checker restored |
|---|---|---|---|
| 99 | 63 | 36 | YES |

Killed targeted mutants (the tolerance/cell logic the decision depends on):

- `TOLERANCE = 28` -> 27, 29 and back to 24: all killed (the pin test and the
  committed Run 20 fixture test).
- cell centre -> cell left edge, cell centre scaled by texels/size, expected
  row flipped, expected index transposed: killed.
- one-pixel sample instead of the 3x3 mean, deviation using only red, deviation
  using the channel minimum: killed.
- shift score preferring fewer matches, shift selection inverted, `x1` clamped
  with `max`: killed.
- always exit 0 / always exit 1: killed.

## Survived mutants and arguments

All 36 survivors were reviewed; none changes the checker's verdict on any
input in the suite, and the groups below are behaviourally equivalent (or
affect only an untested CLI misuse/display path).

| line | mutant | argument |
|---|---|---|
| 38 | `sys.path.insert(0, ...)` -> insert at 1 | Import path only; `TOOLS_DIR` is already importable (script directory), so both orders import the same modules. Equivalent. |
| 44 | `CLEAR` channels +-1 (6 mutants: 16->15/17 twice, 24->23/25 twice) | Only moves the clear/non-clear boundary by 1 level. No sampled pixel or expected texel is within 112 levels of CLEAR (nearest expected colour is 140 away), so no classification changes on any capture or synthetic test. Equivalent for the sampled data; if a texel colour ever came near CLEAR this mutant would matter (it would also be a bad frame). |
| 45 | `SHIFTS` values changed (`1->0`/`1->2`, twice each) | The +-1 whole-quad shift search is a real-capture halo heuristic. Every test image (and the Run 20 bbox) is tight, so the best shift is `(0,0)` and the search is a no-op: the halo test passes with all shift sets, remaining inside the 6 px texel cells. Survives by design; the search is not pinned by an input where +1 is required. Residual risk accepted: the search is defensive, not observable on the committed evidence. |
| 49 | `range(3)` -> `range(2)` in `_is_clear`; `<=` -> `<` | `range(2)` ignores blue when classifying clear; pure blue (0,0,255) would then count as clear, but every quad has non-blue pixels on all four bbox edges, so the bbox is unchanged. The `<=`/`<` boundary has no pixel exactly at the threshold. Both are equivalent on every input; the blue hole is a latent gap (a blue-only quad edge) that no capture shows. |
| 68, 70, 72, 74 | `x < left` -> `<=`; `x > right` -> `>=` (and y) | Min/max tracking: strict vs non-strict comparison yields the same minimum/maximum. Equivalent. |
| 82 | `total = [0, 0, 0]` -> `[1, 0, 0]` (3 positions, red/green/blue) | Adds 1 to a 9-pixel sum, i.e. 0.11 levels before rounding. On exact and on Run 20 cells this never flips the rounded mean (the checker is 28-tolerant). Equivalent at the tool's resolution. |
| 84 | `max(0, y - 1)` -> `max(1, y - 1)` | Differs only for sample rows y <= 1; all quads (and the synthetic image) sample rows >= 9. Equivalent for this frame. |
| 85 | `max(0, x - 1)` -> `max(1, ...)`, `1 -> 0`, `min(top, y + 2)` -> `y + 1` | Changes the 3x3 window only at the screen/capture edge or shrinks it to 2x3. Every sampled cell is inside a uniform capture texel far from the screen edge, so the mean value is unchanged; a 2x3 mean of the same pixels is identical. Equivalent on the committed inputs; per-pixel noise that could distinguish the windows is not present in the synthetic frames or strong enough in the Run 20 capture. |
| 109 | `box_w = right - left + 1` -> `right - left` | One-pixel narrower sampling grid; on the 16x16 ramp the largest sampling shift is 1 px, still inside the 6 px cell, so all samples hit the same expected colour. Survives because the grid tolerance absorbs it (worst 0). |
| 120, 122 | `+ dy` -> `- dy`, `+ dx` -> `- dx` | `SHIFTS` is symmetric (`-1, 0, 1`), so negating the loop variable visits the same three offsets; the score uses `abs(dx)+abs(dy)`, so the selected shift and all reported values are unchanged. Pure equivalent mutant. |
| 127 | `want[:3]` -> `want[:2]` / `want[:4]` | Only truncates the tuple the measurement tool prints for display; the checker computes its deviation from the full `want` tuple before this line. Equivalent for the checker tests (the measurement tool's numbers are produced from the unmutated code). |
| 128 | `if deviation > worst` -> `>=` | Max update; strict vs non-strict yields the same maximum. Equivalent. |
| 132 | `score = (-matched, -worst, ...)` | Only re-orders ties among shifts with the same `matched` count; on every input the winning shift is unique or the tied shifts give the same reported worst. Equivalent on the tested captures. |
| 133 | `score < best[0]` -> `<=` | Only replaces the best on an exact score tie, with an identical score tuple; reported values unchanged. Equivalent. |
| 162 | `return 2` -> 1 / 3 (wrong-size PPM path) | The CLI misuse path for a non-400x240/480 PPM; no test feeds one and the task keeps 0/1/2 semantics as written. Survives as an untested usage-error code; not part of the tolerance logic. |
| 163 | `top = 240` -> 239 / 241 | Bound for the 400x480 form only; all quads end at y=229 and the 240-row tests never reach the bound. Equivalent for this frame (the 480-row test still passes). |

No survivor hides a wrong tolerance verdict: the default, the per-cell
comparison, the deviation metric, the expected-cell mapping, the bbox cell
window and the exit codes are all pinned by killed mutants or by the committed
tests.

## Reproduce

    python3 docs/reports/tex_formats_tolerance_mutate.py --jobs 4
    # mutants: 99, killed: 63, survived: 36, checker restored: YES

`make test` is unaffected (the runner is not in the test target).
