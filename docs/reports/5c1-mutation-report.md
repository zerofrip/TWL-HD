# 5C-1 mutation report: pure-C texture capture planner

Status: **IMPLEMENTED**, BUILD/UNIT only. `make test` is green and the golden
suite passes; nothing EMULATOR or HARDWARE is verified. This report contains
only numbers produced by the run described below.

## Method

The mutant set is generated and run by
`docs/reports/5c1_mutate_tex_capture.py`, invoked from the repository root as:

    python3 -B docs/reports/5c1_mutate_tex_capture.py --jobs 8

The script is standalone and is not referenced by the `Makefile` (it is not
part of `make test`). It edits only `common/twlhd_tex_capture.c`, one edit per
mutant:

- every comparison/logical operator occurrence is swapped to its neighbour
  (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`); shift operators
  (`<<`, `>>`) and `->` are skipped;
- every numeric constant gets `+1` and, when positive, `-1`;
- additionally, a pinned targeted set swaps the constants that matter most:
  the 131,072 / 32,768 defaults, the 65,540 full window, the format-5
  `4*max+8` construction, the `0x3FFF` mask, the texel/palette/index shifts,
  the index base and selector, the `align4` pad, the static TXDT/PLDT
  impossibility comparisons, the VTX range, the raw-FIFO skip, the key-table
  full check and the compaction condition. A missing pinned substring is a
  hard error ("targeted mutant substring missing"), so a source change cannot
  silently shrink the set.

Comment text, string literals and `#include` lines are skipped; `#define`
bodies are code. Identical mutant texts are deduplicated. The set contains
222 generic and 13 targeted mutants after dedupe (235 unique total); 27
targeted mutations are attempted and 14 coincide with a generic mutation.

For each mutant the script writes the mutant text over
`common/twlhd_tex_capture.c`, rebuilds `tests/host/build/test_tex_capture`
with the repository host flags (the detected flags are
`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host -Itools/azahar_host
-fsanitize=address,undefined -fno-omit-frame-pointer`), runs the C test
directly, and for a surviving mutant rebuilds `tests/host/build/tex_capture_cli`
and runs

    python3 -B -I -m unittest discover -s tools -p test_tex_capture_golden.py

with `TEX_CAPTURE_CLI` pointing at the freshly built CLI. A mutant is
**KILLED** when at least one step fails: a `-Werror` build failure counts,
because `make test` fails on it.

The baseline is built and both steps must pass before the run starts (14/14 C
labels; 2 golden tests, 36 scenes). With `--jobs 8` the runner splits the
mutant list into eight chunks, each in a private temporary copy of the
repository; the mutant set and kill/survive decision are unchanged from a
sequential run. The original file is restored in `finally:`, and the run ends
with `common/ restored: YES`.

## Results

| metric | count |
| --- | --- |
| unique mutants | 235 |
| killed by `tests/host/build/test_tex_capture` | 188 |
| killed by golden suite | 12 |
| killed by `-Werror` build | 12 |
| equivalent survivors | 23 |
| total | 235 |

Kill distribution:

| step | count | share |
| --- | --- | --- |
| C test | 188 | 80.0 % |
| golden | 12 | 5.1 % |
| build | 12 | 5.1 % |
| survived (equivalent) | 23 | 9.8 % |

## Survivor classification

Each survivor is an equivalent mutant: no input accepted by the planner can
distinguish it from the original. Line numbers are in the delivered
`common/twlhd_tex_capture.c`.

| line | mutant | why it is equivalent |
| --- | --- | --- |
| 16 | `CAP_DROP_UNSUPPORTED` 2 -> 3 | Private state value; compared only against itself through the symbolic name. (The 2 -> 1 twin is killed because it collides with `CAP_DROP_READ`.) |
| 19 | `cap_align4` addend `3u` -> `2u` | Every caller passes a multiple of 4 (fixed tables are multiples of 8; format 5 is `4*max+8`). For `x % 4 == 0`, `(x + 2) & ~3 == x`; the mask mutation `~3` -> `~2` is killed by the exact-fit test. |
| 61 | `uint8_t word[2]` -> `[3]` | Only bytes 0 and 1 are ever written by the 2-byte read or read by the unpack; a larger local array is unobservable. |
| 71 | `entry > max` -> `>=` | When equal, `max = entry` stores the same value. |
| 97 | `texel_bytes = 0` -> `1` | `twlhd_v2_tex_sizes()` overwrites all three outputs for every format 1..7; format 0 never reaches `cap_classify_key()`. |
| 98 | `palette_bytes = 0` -> `1` | Same as line 97. |
| 99 | `index_bytes = 0` -> `1` | Same as line 97. |
| 108 | `texel_offset = 0` -> `1` | `twlhd_tex_capture_layout()` overwrites the offset of every kept record; dropped records are never read. |
| 109 | `palette_offset = 0` -> `1` | Same as line 108. |
| 110 | `index_offset = 0` -> `1` | Same as line 108. |
| 115 | `palette_bytes > PLDT_MAX` -> `>=` | Reachable palette sizes are at most 65,540 B (format-5 full window) or a fixed table of at most 512 B; equality with 98,304 B is impossible. |
| 155 | `write_seq = 0` -> `1` | Sequence numbers are only compared for equality with each other; shifting the initial value shifts every stored value equally. |
| 156 | `param = 0` -> `1` | Both 0 and 1 have format bits 26..28 = 0, so a VTX before any `TEXIMAGE_PARAM` is untextured and skipped either way. |
| 166 | `out->record_count = 0` -> `1` | Unconditionally overwritten with `kept` before return; there is no early return between the two statements. |
| 168 | `max_txdt_bytes > TXDT_MAX` -> `>=` | At equality the clamp stores the same value. |
| 171 | `max_pldt_bytes > PLDT_MAX` -> `>=` | Same as line 168. |
| 174 | `max_records > TXDS_MAX` -> `>=` | Same as line 168. |
| 185 | raw-FIFO skip -> `word == TWLHD_V2_CMD_ID_MASK` (targeted) | Raw entries have `cmd_word == 0x10000`, so `id = word & 0xFF` is 0 and matches no branch either way; a non-raw word 0xFF is not a valid GXCM command. The explicit skip is redundant for accepted input. |
| 195 | `in_begin = 1` -> `2` | Used only as a boolean (`if (!in_begin ...)`); any nonzero value is the same. |
| 255 | `kept != i` -> `kept != i + 1` (targeted) | `kept <= i` always, so the mutated condition is always true: every kept slot is copied, including a harmless self-copy when `kept == i`. |
| 359 | `texel_bytes != 0` -> `!= 1` | No legal texture has 1 texel byte: the minimum is 8x8 I2 = 16 B and every format's size is 16 B or more, so both predicates are true for every kept record. |
| 380 | `texel_bytes != 0` -> `!= 1` in pointer selection | Same size rule as line 359; every kept record has a non-NULL texel pointer either way. |
| 383 | `palette_bytes != 0` -> `!= 1` in pointer selection | Palette bytes are 0 or at least 8 (fixed tables) / `4*max+8 >= 8` (format 5); 1 is unreachable, so the NULL/non-NULL choice is unchanged. |

## Notes

- The run was made after the two test additions that killed the index-base
  selector mutants (`(param & 0xC000)` mask) and the probe-loop start mutant
  (`apply` must probe record 0 so a source failure before the first write still
  leaves the caller's buffers untouched).
- 12 mutants fail the build because the mutation makes the module fail
  `-Werror` (for example an unused or sign-compared expression); they are
  killed by the same command `make test` uses.
- The 12 golden kills are behaviour changes that the hand-written C cases do
  not distinguish but the 36-scene random oracle contract does.
