# 5B-2e-1 mutation report: host-side textured frame preparation

Status: **IMPLEMENTED**, BUILD/UNIT only. `make host-test` and the targeted
golden suite are green; nothing EMULATOR or HARDWARE is verified. This report
contains only numbers and results produced by the run described below.

## Method

The mutant set is generated and run by `tools/mutate_pica_frame.py`, invoked
from the repository root as:

    python3 tools/mutate_pica_frame.py

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

The compiler is the default `HOST_CC = cc`; the detected host flags are
`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host -Itools/azahar_host
-fsanitize=address,undefined -fno-omit-frame-pointer`.

A mutant is **KILLED** when at least one of those steps fails: a `-Werror`
build failure counts, because `make test` fails on it. The unmutated baseline
is built and both steps must pass before the run starts (13/13 C labels and 1
golden test with 9 subTests). Identical mutant texts are deduplicated. The
script restores the original source in a `finally:` block, keeps its scratch
copy of each mutant in a temporary directory, and ends with
`git diff --exit-code common/`, which must be clean.

## Results

**192 mutants generated: 164 killed, 28 equivalent survivors.**

| Killing step | Mutants |
|---|---:|
| `test_pica_frame` (host-test) | 140 |
| build (`-Werror`, no binary produced) | 24 |
| golden (`test_pica_frame_golden.py`) | 0 |
| **Total killed** | **164** |
| equivalent survivors | 28 |
| **Total** | **192** |

The 24 build kills are all `-Werror=parentheses` failures from mixing `&&`
and `||` in one expression (`||` -> `&&` next to an existing `&&`, or the
reverse), on lines 19, 102, 107, 108, 161, 162, 163, 169, 272 and 311. `make
test` fails on them exactly as it would for any non-compiling change.

No mutant is killed by the golden step alone: for every behaviour the CLI can
express, the C tests are at least as strong, and the C step runs first. The
golden suite still runs for every survivor and still passes.

Sizes 4, 2048, non-powers of two and 0 never reach `frame_size_ok()`: the only
call site (:108) receives `twlhd_tex_dimensions()` results, which are
`8u << ((param >> 20) & 7u)` and `8u << ((param >> 23) & 7u)`
(`common/twlhd_tex_decode.c:69-70`), i.e. exactly `{8, 16, 32, 64, 128, 256,
512, 1024}`; format 0 has already returned at :87-91 and also makes
`twlhd_tex_dimensions()` fail (`common/twlhd_tex_decode.c:67`). The
reachable boundaries 8 and 1024 are covered by the existing 8x8 tests and the
1024 test; no fake test was added for unreachable values.

## Tests that pin the mutated behaviours

These six labels exist in `tests/host/test_pica_frame.c` (all add-only; no
existing label was renamed, weakened or skipped):

| Label | Closes |
|---|---|
| `pica-frame/size-bounds-1024` (`test_size_bounds_1024`, :999) | Line 19 `1024 -> 1023` and `<= -> <`: 1024x8 and 8x1024 both must be accepted, with exact 32768-byte scratch, 65536 arena bytes, offsets 0/32768 and hand-computed arena texels. |
| `pica-frame/scratch-exact` (`test_scratch_exact`, :1090) | Line 114 `> -> >=`: scratch exactly 256 bytes for one 8x8 texture succeeds; 255 bytes fails with `ERR_CAPACITY` and leaves every output untouched; the same pair through `twlhd_pica_frame_arena_needed`. |
| `pica-frame/texture-cap-256` (`test_texture_cap_256`, :1174) | Lines 197/338 `>= -> >`: 256 distinct textures all stored (per-texture offsets, dimensions, indices, statuses and every arena byte hand-checked), plus a hand-built 257-record view whose `MAX+1` key must report `ERR_CAPACITY`; also pins the `tex_count`/`arena_used` initialisers (:155, :156, :157, :306, :307), the last TXDS record and the last bind-plan entry. |
| `pica-frame/format-index-matrix` (label :1426) | Line 102 `5 -> 4` and `5 -> 6`: format 4 (I8) and format 6 (A5I3) without index data are accepted; format 5 with index data is accepted; a hand-built format-5 record whose index offset is out of bounds is `NO_DATA` (not `UNSUPPORTED`). |
| `pica-frame/failure-run-before-textured` (label :1479) | Line 190 `|| -> &&`: a `NO_DATA` run and an untextured run before the first textured run consume no texture slot, with `texture_capacity` equal to the real texture count. |
| `pica-frame/texcoord-tail-guard` (label :1555) | Line 285 `< -> <=`: last run untextured, exact-size guarded texcoords (the zero loop must not write `texcoords[vertex_count]`), plus the 0x00/0xFF input-tail two-pass comparison. |

Two golden frames also exercise the untextured-run and `NO_DATA`-run
behaviours through the independent Python oracle: an untextured run first
(`[None, (a, 0), (b, 1)]`) and a `NO_DATA` run first with an untextured run
last (`[("missing", m2, 0), (c, 2), None]`).

## Equivalent survivors

Each of the 28 survivors was compiled and run through both steps and neither
failed. The arguments below cite the current line numbers of
`common/twlhd_pica_frame.c` unless another file is named.

| Line(s) | Mutant | Why it cannot change an observable result |
|---|---|---|
| 19 | `8 -> 7` | `frame_size_ok` is static with one call site :108, fed only from `twlhd_tex_dimensions()`; on its domain every value is >= 8, so `>= 7` and `>= 8` agree. |
| 19 | `1024 -> 1025` | Same domain: every value is <= 1024, so `<= 1025` and `<= 1024` agree. |
| 19 | `1 -> 2` (`v - 1` -> `v - 2`) | For `v = 2^k, k >= 3`, both `v - 1` and `v - 2` have set bits only below bit k, so `v & (v - 2) == 0`; the only accepted non-powers are 0 and 2 (`2 & 0 == 0`), both below the domain minimum 8. 7 (`7 & 5 == 5`) and 1025 (`1025 & 1023 == 1`) are rejected by the expression itself. |
| 40 | `< -> <=` | Extra iteration at `i == count`; `twlhd_v2_txds_record()` rejects `index >= txds_count` (`common/twlhd_v2_parse.c:736`), so the helper returns 0 at :43, exactly the loop-exit value at :50. |
| 43 | `0 -> 1` | Unreachable through a parsed view (see below); in a hand-built `txds == NULL` view all three accessors return NULL and the run is `NO_DATA` at :102-104, the same status as the failed-find path. |
| 47 | `1 -> 2` | Sole call site :94 tests truthiness (`!frame_find_record(...)`); 2 is truthy. |
| 72 | `rec_index = 0 -> 1` | Written at :45 before any read; reads are at :99-101 and are reached only after the find succeeds (:94); failure returns at :96. |
| 73 | `width = 0 -> 1` | Written by `twlhd_tex_dimensions()` at :107. Reads at :108 happen only when that call returned true (short-circuit `||`), and :113/:123 are later. |
| 74 | `height = 0 -> 1` | Same as width (:107, :108, :113, :124). |
| 83, 84, 85 | `*out_width`/`*out_height`/`*out_bytes = 0 -> 1` | All three are overwritten at :123-125 on the only path where a caller reads them (`status == OK && !untextured`, :190/:240/:272); untextured, `NO_DATA` and `UNSUPPORTED` runs never read them. |
| 88 | `*out_untextured = 1 -> 2` | Only tested for truthiness at :190, :240, :272 and :331. |
| 90, 96, 104, 110, 119, 126 | `return 0 -> 1` | All three call sites test only `< 0` (:186-188, :234-238, :327-330); a non-negative value takes the classified path. Every affected return is preceded by its out-parameter writes (:80-85, :88-89, :92, :95, :103, :109, :118, :122-125). |
| 114 | `return -1 -> -2` (literal `1 -> 2`) | The same `< 0` tests: -2 is still negative and still produces `ERR_CAPACITY` at :188/:330 (or `UNSUPPORTED` in the write pass). |
| 152, 153 | key tables `{0} -> {1}` | Read only at :193 and :244 inside loops bounded by `tex_count`; every slot `< tex_count` was written at :201-202 before `tex_count++` (:203) in the validation pass, and the write pass visits the same runs in the same order, so it only reads validation-written slots. |
| 304, 305 | key tables `{0} -> {1}` in `arena_needed` | Read only at :334 inside `j < tex_count`; every slot was written at :341-342 before `tex_count++` (:343). |
| 243 | `< -> <=` | Equivalence proof below; no test can distinguish it. |
| 278 | `< -> <=` | The failure branch of `twlhd_pica_texcoords_from_gx()` is unreachable (proof below); the extra write at `v == i` never executes. |
| 279, 280 | fallback `u`/`v = 0 -> 1` (`0.0f -> 1.0f`) | Same unreachable failure branch; the loop body never executes. |

### Line 19: the size check

`frame_size_ok(v)` is `v >= 8u && v <= 1024u && (v & (v - 1u)) == 0u` and is
static with exactly one call site (:108). Its arguments are produced at :107
by `twlhd_tex_dimensions`, whose dimensions are `8u << ((param >> 20) & 7u)`
and `8u << ((param >> 23) & 7u)` (`common/twlhd_tex_decode.c:69-70`); format
0 returns before that and also makes the dimensions helper fail
(`common/twlhd_tex_decode.c:67`). The reachable set is exactly
`{8, 16, 32, 64, 128, 256, 512, 1024}`. On that set:

- `>= 7` and `>= 8` are identical (all values are >= 8);
- `<= 1025` and `<= 1024` are identical (all values are <= 1024);
- for `v = 2^k` with `k >= 3`, `v & (v - 2) == 0` because all set bits of
  `v - 2` are below bit k. The only other accepted values are 0 and 2 (both
  rejected by `>= 8`), and 7 / 1025 are rejected by the expression itself, so
  the mutant accepts exactly the same set as the original.

### Line 40: the TXDS search loop

`count` is `twlhd_v2_txds_count(view)` (:36). At the extra iteration
`i == count`, `twlhd_v2_txds_record(view, count, &rec)` returns 0 because it
requires `index < view->txds_count` (`common/twlhd_v2_parse.c:736`).
`frame_find_record` then returns 0 at :43, before reading `rec`, exactly as
the normal loop exit returns 0 at :50. No caller can observe the extra
iteration.

### Lines 43 and 47: failed TXDS record and the helper's success value

A corrupt record is not reachable through a parsed view. `txds_record` fails
inside the loop only when `view->txds == NULL` (the index is always
`< count`), and the parser sets `txds` if and only if the TXDS chunk was seen
(`common/twlhd_v2_parse.c:632`), then validates it and derives `txds_count`
from the same `seen_txds` condition (`:650-653`); `validate_tex` rejects
`count == 0` and `count > 256` (`:338`). So a parsed view never has
`txds == NULL` with `txds_count > 0`.

The only caller tests the result for truthiness (`if (!frame_find_record(...))`
at :94), so a changed non-zero value is indistinguishable there. For a
hand-built view with `txds == NULL` and `txds_count > 0`, mutant 43 makes
`frame_find_record` return 1 (truthy), so `frame_eval_run` continues with
`rec_index == 0` (initialised at :72, never overwritten) and the uninitialised
`rec` is never read. All three accessors at :99-101 first call
`twlhd_v2_txds_record` and return NULL when `view->txds == NULL`
(`common/twlhd_v2_parse.c:745-747`, `:759-761`, `:773-775`), so :102 takes the
`NO_DATA` branch at :103-104. That is the same status the unmutated code
produces after the failed find. Mutant 47 changes only the success return from
1 to 2; the sole call site :94 tests truthiness, so 2 behaves like 1.

### Lines 72-74 and 152/153/304/305: initialisers

Every read is preceded by a write:

- `rec_index` (:72): written at :45 in `frame_find_record`; read at :99-101
  only after :94 accepted the find; a failed find returns at :96.
- `width`/`height` (:73-74): written at :107; read at :108 only when the
  `&&` chain reached that call after it returned true, and at :113/:123-125
  later.
- `key_param`/`key_pltt` (:152-153): read at :193 and :244 only for indices
  `< tex_count`; each such slot was written at :201-202 before `tex_count++`
  (:203) in the validation pass, and the write pass visits the same runs in
  the same order (same `frame_same_key` boundary predicate at :184 and
  :219-220, same deterministic `frame_eval_run`), so an unwritten slot is
  never read.
- `key_param`/`key_pltt` in `twlhd_pica_frame_arena_needed` (:304-305): read
  at :334 only for `j < tex_count`; each slot was written at :341-342 before
  `tex_count++` (:343).

### Lines 90, 96, 104, 110, 114, 119, 126: `frame_eval_run` return values

All three call sites test only the sign:

- :186-188 `if (frame_eval_run(...) < 0) return TWLHD_PICA_FRAME_ERR_CAPACITY;`
- :234-238 the same test, mapping a negative value to `UNSUPPORTED` in the
  write pass;
- :327-330 the same test in `twlhd_pica_frame_arena_needed`.

The exact non-negative value is never used. Each mutated return writes its
out-parameters first: :80-85 (param, pltt, width, height, bytes),
:88-89 (untextured, status), :92/:95 (`NO_DATA`), :103 (`NO_DATA`),
:107-109 (dimensions failure -> status), :116-118 (decode failure -> status),
:122-125 (`OK` plus width, height, bytes). The `-1` at :114 mutated to `-2`
stays negative, so the `< 0` test can only observe negativity; callers take
the same branch and produce the same status.

### Line 243: the write-pass dedupe probe

The mutation changes only the write pass (:218-292). The extra probe at
`t == tex_count` is followed by `if (t == tex_count)` at :246, which is true
for that iteration, so it enters the same store branch (:247-262) as the
normal loop exit.

- A run whose key was already stored breaks at the same earlier `t`; the
  extra iteration never runs.
- A run whose key is new has its key at slot `tex_count` from the validation
  pass (:201-202), because the validation pass (:175-205) and the write pass
  walk the same runs in the same order with the same boundary predicate
  (:184 vs :219-220) and the same deterministic `frame_eval_run`. The extra
  probe therefore reads that run's own key, matches at `t == tex_count`, and
  takes the store branch exactly as the loop-exit path would. The probe is in
  bounds because the validation pass stored at least as many keys as the
  write pass has stored so far.
- The only out-of-bounds read would be `key_param[256]` (array size is
  `TWLHD_PICA_FRAME_MAX_TEXTURES` = 256, `common/twlhd_pica_frame.h:94`) when
  the write pass already holds 256 textures and processes a further new key.
  That state is unreachable: the validation pass would have processed the
  same 257th distinct key and returned `ERR_CAPACITY` at :197-199 before the
  write pass started. Parsed views cannot reorder anything, and the parser
  caps TXDS at 256 records with unique keys (`common/twlhd_v2_parse.c:338`,
  `:358-366`).

No test can distinguish this mutant. The suggested input (exactly `MAX`
distinct textures, then one reused and one new texture) makes
`twlhd_pica_frame_prepare()` return `ERR_CAPACITY` at :199 in the validation
pass under both the original and the mutant, before the write pass is
reached. The existing `pica-frame/texture-cap-256` label
(`tests/host/test_pica_frame.c:1174`) already pins the 257-distinct ->
`ERR_CAPACITY` behaviour. The mutant is equivalent, not a test gap.

### Lines 278, 279, 280: the zero-fill of the failure path

The `twlhd_pica_texcoords_from_gx()` failure branch (:274-282) is unreachable:

- the call passes `tex + j` and `texcoords + j`; both base pointers were
  checked non-NULL at :161-165, so the NULL check at
  `common/twlhd_pica_tex.c:132` cannot fire;
- `width`/`height` already passed `frame_size_ok` (:107-108), which is the
  same expression as `pica_tex_size_ok` (`common/twlhd_pica_frame.c:18-20`
  vs `common/twlhd_pica_tex.c:25-27`), so the size check at
  `common/twlhd_pica_tex.c:133` cannot fire;
- the call passes `vc` as both `count` and `out_capacity` (:274-276), so the
  capacity check (`common/twlhd_pica_tex.c:135`) cannot fire.

The loop body of the fallback (`v = j; v < i; ...`) therefore never executes,
and mutating its bound or its stores is unobservable.

## Per-mutant results

Each row is one mutant, labelled with the original line number and the single
edit. `KILLED` rows name the step that failed; `SURVIVED (equivalent)` rows
are the ones triaged above.

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
| `line 19 op >= -> >` | KILLED | `test_pica_frame` |
| `line 19 op && -> ||` | KILLED | build (`-Werror`) |
| `line 19 op <= -> <` | KILLED | `test_pica_frame` |
| `line 19 op && -> ||` | KILLED | build (`-Werror`) |
| `line 19 op == -> !=` | KILLED | `test_pica_frame` |
| `line 23 op == -> !=` | KILLED | `test_pica_frame` |
| `line 23 op && -> ||` | KILLED | `test_pica_frame` |
| `line 24 op == -> !=` | KILLED | `test_pica_frame` |
| `line 40 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 40 op < -> <=` | SURVIVED (equivalent) | - |
| `line 43 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 44 op == -> !=` | KILLED | `test_pica_frame` |
| `line 44 op && -> ||` | KILLED | `test_pica_frame` |
| `line 44 op == -> !=` | KILLED | `test_pica_frame` |
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
| `line 87 op == -> !=` | KILLED | `test_pica_frame` |
| `line 88 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 88 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 90 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 92 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 96 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 102 const 7 -> 8` | KILLED | `test_pica_frame` |
| `line 102 const 7 -> 6` | KILLED | `test_pica_frame` |
| `line 102 const 5 -> 6` | KILLED | `test_pica_frame` |
| `line 102 const 5 -> 4` | KILLED | `test_pica_frame` |
| `line 102 op || -> &&` | KILLED | build (`-Werror`) |
| `line 102 op != -> ==` | KILLED | `test_pica_frame` |
| `line 102 op && -> ||` | KILLED | `test_pica_frame` |
| `line 102 op || -> &&` | KILLED | build (`-Werror`) |
| `line 102 op == -> !=` | KILLED | `test_pica_frame` |
| `line 102 op && -> ||` | KILLED | `test_pica_frame` |
| `line 104 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 107 op || -> &&` | KILLED | build (`-Werror`) |
| `line 108 op || -> &&` | KILLED | build (`-Werror`) |
| `line 110 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 113 const 4 -> 5` | KILLED | `test_pica_frame` |
| `line 113 const 4 -> 3` | KILLED | `test_pica_frame` |
| `line 114 const 1 -> 2` | SURVIVED (equivalent) | - |
| `line 114 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 114 op > -> >=` | KILLED | `test_pica_frame` |
| `line 116 op != -> ==` | KILLED | `test_pica_frame` |
| `line 119 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 126 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 131 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 134 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 134 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 134 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 134 op < -> <=` | KILLED | `test_pica_frame` |
| `line 135 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 135 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 135 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 135 op == -> !=` | KILLED | `test_pica_frame` |
| `line 135 op || -> &&` | KILLED | `test_pica_frame` |
| `line 152 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 153 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 155 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 156 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 157 const 0 -> 1` | KILLED | `test_pica_frame` |
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
| `line 166 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 166 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 166 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 166 op != -> ==` | KILLED | `test_pica_frame` |
| `line 169 op > -> >=` | KILLED | `test_pica_frame` |
| `line 169 op || -> &&` | KILLED | build (`-Werror`) |
| `line 169 op > -> >=` | KILLED | `test_pica_frame` |
| `line 169 op || -> &&` | KILLED | build (`-Werror`) |
| `line 170 op > -> >=` | KILLED | `test_pica_frame` |
| `line 175 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 175 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 175 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 175 op < -> <=` | KILLED | `test_pica_frame` |
| `line 184 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 184 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 184 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 184 op != -> ==` | KILLED | `test_pica_frame` |
| `line 184 op && -> ||` | KILLED | `test_pica_frame` |
| `line 187 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 187 op < -> <=` | KILLED | `test_pica_frame` |
| `line 190 op != -> ==` | KILLED | `test_pica_frame` |
| `line 190 op || -> &&` | KILLED | `test_pica_frame` |
| `line 192 const 0 -> 1` | KILLED | `test_pica_frame` |
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
| `line 215 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 216 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 217 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 218 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 218 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 218 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 218 op <= -> <` | KILLED | `test_pica_frame` |
| `line 219 op == -> !=` | KILLED | `test_pica_frame` |
| `line 219 op || -> &&` | KILLED | `test_pica_frame` |
| `line 220 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 220 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 220 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 220 op != -> ==` | KILLED | `test_pica_frame` |
| `line 220 op && -> ||` | KILLED | `test_pica_frame` |
| `line 228 const 1 -> 2` | KILLED | `test_pica_frame` |
| `line 228 const 1 -> 0` | KILLED | `test_pica_frame` |
| `line 235 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 235 op < -> <=` | KILLED | `test_pica_frame` |
| `line 240 op == -> !=` | KILLED | `test_pica_frame` |
| `line 240 op && -> ||` | KILLED | `test_pica_frame` |
| `line 243 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 243 op < -> <=` | SURVIVED (equivalent) | - |
| `line 244 op == -> !=` | KILLED | `test_pica_frame` |
| `line 244 op && -> ||` | KILLED | `test_pica_frame` |
| `line 244 op == -> !=` | KILLED | `test_pica_frame` |
| `line 246 op == -> !=` | KILLED | `test_pica_frame` |
| `line 249 op != -> ==` | KILLED | `test_pica_frame` |
| `line 272 op == -> !=` | KILLED | `test_pica_frame` |
| `line 272 op && -> ||` | KILLED | build (`-Werror`) |
| `line 272 op && -> ||` | KILLED | build (`-Werror`) |
| `line 273 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 273 op >= -> >` | KILLED | `test_pica_frame` |
| `line 275 op != -> ==` | KILLED | `test_pica_frame` |
| `line 278 op < -> <=` | SURVIVED (equivalent) | - |
| `line 279 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 280 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 285 op < -> <=` | KILLED | `test_pica_frame` |
| `line 286 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 287 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 304 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 305 const 0 -> 1` | SURVIVED (equivalent) | - |
| `line 306 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 307 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 311 op || -> &&` | KILLED | build (`-Werror`) |
| `line 314 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 314 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 314 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 314 op != -> ==` | KILLED | `test_pica_frame` |
| `line 316 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 316 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 316 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 316 op < -> <=` | KILLED | `test_pica_frame` |
| `line 325 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 325 const 3 -> 4` | KILLED | `test_pica_frame` |
| `line 325 const 3 -> 2` | KILLED | `test_pica_frame` |
| `line 325 op != -> ==` | KILLED | `test_pica_frame` |
| `line 325 op && -> ||` | KILLED | `test_pica_frame` |
| `line 328 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 328 op < -> <=` | KILLED | `test_pica_frame` |
| `line 331 op != -> ==` | KILLED | `test_pica_frame` |
| `line 331 op || -> &&` | KILLED | `test_pica_frame` |
| `line 333 const 0 -> 1` | KILLED | `test_pica_frame` |
| `line 333 op < -> <=` | KILLED | `test_pica_frame` |
| `line 334 op == -> !=` | KILLED | `test_pica_frame` |
| `line 334 op && -> ||` | KILLED | `test_pica_frame` |
| `line 334 op == -> !=` | KILLED | `test_pica_frame` |
| `line 336 op != -> ==` | KILLED | `test_pica_frame` |
| `line 338 op >= -> >` | KILLED | `test_pica_frame` |
