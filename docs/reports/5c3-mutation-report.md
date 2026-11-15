# 5C-3 mutation report: SCENE=5 texture comparator

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` is green and the texture
comparator tests pass; nothing EMULATOR or HARDWARE is verified. This report
contains only numbers produced by the run described below.

## Method

The mutant set is generated and run by
`docs/reports/5c3_mutate_tex_compare.py`, invoked from the repository root as:

    python3 -B -I docs/reports/5c3_mutate_tex_compare.py --jobs 4

The script is standalone and is not referenced by the `Makefile` (it is not
part of `make test`). Each mutant is written over
`tools/emulator/compare_texture_capture.py` in a temporary sandbox copy of the
repository, one edit per mutant:

- every comparison/logical operator occurrence is swapped to its neighbour
  (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`); shift operators
  (`<<`, `>>`) and `->` are skipped;
- every numeric constant gets `+1` and, when positive, `-1`;
- additionally, a pinned targeted set mutates the constants that matter most:
  the `3 << 26` TEXIMAGE_PARAM literal, the PLTT_BASE literal, the 32/32/0
  size literals, the TEXIMAGE_PARAM/PLTT_BASE/TEX_COORD/BEGIN/END/SWAP command
  ids, `GL_QUADS`, the vertex count, the backend/coverage constants, the
  default angle, the record-count and key comparisons, the key-order
  comparison, the END-after-BEGIN check, the group walk, the SWAP-last check,
  the FRAME_COMPLETE/OVERFLOW checks, and two expected-array bytes. A missing
  pinned substring is a hard error ("targeted mutant substring missing"), so a
  source change cannot silently shrink the set; the two array-byte mutants
  exist to show the `demo_frame.cpp` cross-check catches expected-data drift
  (they are killed by `DemoSourceCrossCheckTest`, not by the comparator logic).

Comment text, string literals, triple-quoted docstrings and the two expected
byte tables are skipped; the byte tables are data, not comparator logic, and
are pinned against `arm9v2/source/demo_frame.cpp` by a dedicated unit test.
Identical mutant texts are deduplicated. The set contains 172 generic and 15
targeted mutants after dedupe (187 unique total); 32 targeted mutations are
attempted and 17 coincide with a generic mutation.

A kill means the comparator test class (`TextureCompareTest`) plus the
`demo_frame.cpp` cross-check class (`DemoSourceCrossCheckTest`) exits nonzero
with the mutant in place. The baseline was run first in the same sandbox and
passed. The comparator is restored in `finally:` and the script verified the
restored file equals the original byte for byte ("comparator restored: YES").

## Result

| Metric | Real number |
|---|---:|
| Unique mutants | 187 |
| Killed | 171 |
| Survived | 16 |
| Kill rate | 91.4% |

Run output (fixed lines):

    baseline: test OK
    mutants: 187 unique
    killed:     171
    survived:   16
    comparator restored: YES

## Every survivor, classified

Diagnostic-text mutants only change a value printed inside an existing failure
reason; they cannot change PASS/FAIL, and the tests assert the reason label
(not the numeric detail), so they are equivalent for acceptance:

| Line | Mutant | Argument |
|---:|---|---|
| 48 | `sys.path.insert(0, TOOLS_DIR)` index `0 -> 1` | Import-path ordering only. `EMULATOR_DIR` and `TOOLS_DIR` expose no colliding module names, so both orders resolve `compare_capture`/`twlhd_v2_dump`/`compare_texture_capture` identically. |
| 49 | `sys.path.insert(0, EMULATOR_DIR)` index `0 -> 1` | Same as line 48. |
| 54 | `DEFAULT_ANGLE = 61440 -> 61439` | One angle LSB changes each rotation element by about `2*pi/32768 = 1.9e-4`, far below `ROTATION_ELEMENT_TOLERANCE = 0.002`; the default-angle path is therefore insensitive by design. The explicit-angle tests pin the same tolerance (and `test_emulator_compare.py` pins its boundary), so this is a true tolerance equivalence, not an untested branch. |
| 54 | `DEFAULT_ANGLE = 61440 -> 61441` | Same as the previous row. |
| 163 | `% (entries[teximage[0]][1], TEXIMAGE_PARAM)` value index `1 -> 0` | Diagnostic operand only: prints the command word instead of the value in the already-selected `teximage-value` failure; the branch and reason are unchanged. |
| 170 | `% (entries[pltt[0]][1], PLTT_BASE)` value index `1 -> 0` | Diagnostic operand only, as line 163. |
| 180 | `"%d, expected GL_QUADS (1)" % entries[begin][1]` value index `1 -> 0` | Diagnostic operand only; the `begin-type` reason is unchanged. |
| 183 | `teximage[0] < pltt[0]` -> `<=` | Equivalent: the two positions index entries with different command ids (`0x2A` and `0x2B`), so equality is impossible. |
| 183 | `pltt[0] < begin` -> `<=` | Equivalent: `PLTT_BASE` (`0x2B`) and `BEGIN_VTXS` (`0x40`) cannot occupy the same entry. |
| 183 | targeted strict order `<` -> `<=` | Same equivalence as the two rows above; included to show the targeted set and the generic operator swap agree. |
| 190 | `[index for index in ends if index > begin]` `>` -> `>=` | Equivalent: an entry cannot be both `END_VTXS` (`0x41`) and `BEGIN_VTXS` (`0x40`), so `index == begin` is impossible. |
| 209 | `% len(groups[0x18])` -> `groups[0x19]` | Diagnostic operand only: the `perspective` reason is already selected by line 207; with one group of each kind the printed number is identical, and with two perspective groups the new `test_extra_perspective_group_fails` still sees the `perspective` reason. |
| 214 | `% len(groups[0x19])` -> `groups[0x18]` | Diagnostic operand only; `test_extra_lookat_group_fails` pins the `lookat` reason. |
| 214 | `% len(groups[0x19])` -> `groups[0x1A]` | Diagnostic operand only, as the previous row. |
| 219 | `% len(groups[0x1A])` -> `groups[0x19]` | Diagnostic operand only: the `rotation` reason is already selected by line 217, and the count branch is pinned by the two-rotation positive control. |
| 219 | `% len(groups[0x1A])` -> `groups[0x1B]` | Diagnostic operand only, as the previous row. |

No survivor leaves a wrong capture accepted: every one either does not affect
control flow or is insensitive within a documented tolerance. The tests that
kill the remaining 171 mutants include the positive control, each negative
case, the key/size checks, the structural order and group checks, the
perspective/lookat/rotation counts, the OVERFLOW/FRAME_COMPLETE flags, the
matrix analytic checks and the `demo_frame.cpp` array cross-check.
