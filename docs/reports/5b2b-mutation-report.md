# 5B-2b mutation report: interpreter texture latch and texture matrix

Status: **IMPLEMENTED**, host-side only. `make test` green. Nothing EMULATOR or
HARDWARE verified.

Every mutant was applied to a scratch copy of `common/twlhd_gx_interp.c` under
`/tmp/opencode/mutants5b2b/` (outside the worktree), compiled against the
unmodified `tests/host/test_gx_interp.c`, the unmodified parser/writer/record
sources and the unmodified header, then run. The unmutated baseline copy passes
all 133 tests; a mutant is **KILLED** when the test binary exits non-zero and
the failing `gx_interp/...` test is recorded below.

The golden CLI was not rebuilt against each mutant; the C suite exercises the
same fields (`s`, `t`, `teximage_param`, `pltt_base`, counter, stack) with
literal expectations derived from `tools/gx_interp_oracle.py`, and the
independent oracle/CLI cross-check passes on the unmutated build.

## Results

| # | Mutant | Result | Killing test(s) |
|---:|---|---|---|
| 1 | s16 sign extension removed (TEX_COORD halves taken as unsigned) | KILLED | `tex-latch-order`, `tex-s16-sign-edges` |
| 2 | S and T swapped | KILLED | `tex-latch-order`, `tex-s16-sign-edges`, `tex-mode0-vs-mode1`, `tex-quad-list-alignment`, ... |
| 3 | `/16.0` -> `/8.0` for the raw s1.11.4 conversion | KILLED | `tex-latch-order`, `tex-s16-sign-edges`, `tex-mode0-vs-mode1`, ... |
| 4 | TEXIMAGE_PARAM latched at BEGIN instead of immediately (pending copy applied at BEGIN) | KILLED | `tex-param-pltt-latch-order` |
| 5 | PLTT_BASE not latched (handler keeps 0) | KILLED | `tex-param-pltt-latch-order`, `tex-mode0-vs-mode1` |
| 6 | Per-vertex tex taken from the first vertex of the polygon | KILLED | `tex-latch-order`, `tex-param-pltt-latch-order`, `tex-quad-list-alignment`, ... |
| 7 | Quad split second triangle uses tex index 1 instead of 2 | KILLED | `tex-quad-list-alignment` |
| 8 | `tex_out` written for a triangle dropped by the capacity check | KILLED | `tex-capacity-truncation-alignment` (canary at `tex_out[3]`) |
| 9 | Mode field read from bits 29-30 instead of 30-31 | KILLED | `texture-mode-loads-matrix`, `tex-mode0-vs-mode1`, `tex-mode1-rows-and-matrix-time`, ... |
| 10 | Mode 1 uses matrix rows 1/0 instead of 0/1 | KILLED | `tex-mode0-vs-mode1`, `tex-mode1-rows-and-matrix-time` |
| 11 | Mode 1 skips the translation term (`m[12]`/`m[13]`) | KILLED | `tex-mode0-vs-mode1`, `tex-matrix-stack-ops` |
| 12 | Mode 1 skips `>> 12` | KILLED | `tex-mode0-vs-mode1`, `tex-mode1-rows-and-matrix-time`, ... |
| 13 | Modes 2/3 no longer counted in `tex_transform_unsupported` | KILLED | `tex-modes-2-3-raw-and-counter` |
| 14 | Texture PUSH/POP/STORE/RESTORE ignored in mode 3 | KILLED | `tex-matrix-stack-ops`, `tex-matrix-stack-errors` |
| 15 | TEX_COORD applied to the previous VTX (one-vertex lag) | KILLED | `tex-latch-order`, `tex-s16-sign-edges`, ... |

No survivor needed an equivalence argument.

## Sanctioned existing-test updates

The texture semantics invalidate four existing assertions and one golden stream.
No test was deleted, skipped or weakened; each change is a literal or index that
the new behaviour makes wrong, and the behavioural intent is preserved:

1. `tests/host/test_gx_interp.c` `test_texture_mode_ignored` ->
   `test_texture_mode_loads_matrix` (`gx_interp/texture-mode-ignored` ->
   `gx_interp/texture-mode-loads-matrix`): `ignored_no_effect` 1 -> 0 because
   mode-3 LOAD_4x4 now loads the texture matrix; the test also asserts the load
   through a mode-1 texture transform (`s` doubled).
2. `test_light_commands_not_ignored`: `ignored_no_effect` 6 -> 3 because
   TEX_COORD/TEXIMAGE_PARAM/PLTT_BASE now latch; BOX_TEST/POS_TEST/VEC_TEST
   remain ignored.
3. `test_light_mode3_stack_commands_ignored` ->
   `test_light_mode3_stack_commands_leave_vec`
   (`gx_interp/light-mode3-stack-ignored` ->
   `gx_interp/light-mode3-stack-leaves-vec`): `ignored_no_effect` 2 -> 0; the
   lit RGB (31,31,31) assertion is unchanged and still proves the vector matrix
   is untouched by mode 3.
4. `test_light_store_restore_mode3_leaves_vec`: STORE/RESTORE index 2 -> 0
   because the texture stack has one entry (index 2 would now be `ERR_STACK`).
   The intent ("mode-3 stack ops leave the vector matrix alone", RGB 21) is
   unchanged.
5. `tools/test_gx_interp_golden.py`
   `test_light_store_restore_mode3_leaves_vec`: the same index 2 -> 0 change for
   the same reason; the expected colour (21/31) is unchanged.

All other existing C tests and all pre-existing golden streams pass with
unchanged `out[]` bytes; `tex_out == NULL` is compared byte-for-byte against the
old entry point in `gx_interp/tex-out-null-matches-old`.

## Unverified source details

The DeSmuME checkout cited by the research document was not present in this
implementation sandbox (`/home/one/3ds/desmume` does not exist). The mode-1
integer steps and the one-entry texture stack follow the task contract and the
research document and are marked READ/UNVERIFIED against the checkout in
`docs/DS_GX_RESEARCH.md`; the oracle, C literals and golden streams share that
stated formula.

## Repair round 1 (2026-11-10): exact mode-1 formula

The 5B-2b mode-1 transform was wrong: it promoted s/t with `<<8`, scaled the
translation by `FX_ONE`, omitted the `m8`/`m9` addends and divided by 4096.
Repair round 1 rewrites it to the formula quoted for DeSmuME
`desmume/src/gfx3d.cpp:1578-1593`:
`s' = (s32)(((s64)m[0]*s + (s64)m[4]*t + (s64)m[8] + (s64)m[12]) >> 12)`
(and the same with `m[1]/m[5]/m[9]/m[13]` for `t'`), with the raw
sign-extended s1.11.4 latches, raw 20.12 addends, 64-bit sums, arithmetic
`>>12`, an `(int32_t)` cast and `/16.0f` floats. The independent Python oracle
was rewritten to match (Python integers plus an explicit `s32()` wrap).

### Method

As before: every mutant is a scratch copy of `common/twlhd_gx_interp.c` under
`/tmp/opencode/mutants5b2b-r1/` (outside the worktree), compiled with the same
`HOST_CFLAGS` (`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon -Itests/host
-Itools/azahar_host -fsanitize=address,undefined`) against the unmodified
`tests/host/test_gx_interp.c` and unmodified parser/writer/record sources.
The unmutated baseline passes all 142 tests; a mutant is **KILLED** when the
binary exits non-zero and the failing `gx_interp/...` labels are recorded.
The golden CLI was not rebuilt per mutant; the C suite asserts the same
fields with the same oracle-derived literals, and the unmutated oracle/CLI
cross-check passes in `make test`.

### Results

| # | Mutant | Result | Killing test(s) |
|---:|---|---|---|
| 1 | drop the `m[8]`/`m[9]` addends | KILLED | `tex-mode1-m8-m9` |
| 2 | drop the `m[12]`/`m[13]` addends | KILLED | `tex-mode1-translation`, `tex-mode0-vs-mode1`, `tex-matrix-stack-ops` |
| 3 | translation `* 16` | KILLED | `tex-mode1-translation`, `tex-mode0-vs-mode1`, `tex-matrix-stack-ops`, `tex-mode1-negative-sum-floor-shift` |
| 4 | translation `* 4096` | KILLED | same as #3 |
| 5 | promote `s,t` with `<< 8` again | KILLED | `tex-mode1-identity`, `tex-mode1-scale`, `tex-mode1-shear`, `texture-mode-loads-matrix`, ... |
| 6 | `>> 11` instead of `>> 12` | KILLED | `tex-mode1-identity`, `tex-mode1-translation`, `texture-mode-loads-matrix`, ... |
| 7 | `>> 13` instead of `>> 12` | KILLED | same as #6 |
| 8 | logical shift on negative sums (`(uint64_t)` cast) | EQUIVALENT | no test fails; see below |
| 9 | 32-bit accumulation (`uint32_t` row sums) | KILLED | `tex-mode1-64bit-headroom`, `tex-mode1-s32-wrap-extreme`, `tex-mode1-identity`, ... |
| 10 | swap `m[0]`/`m[1]` | KILLED | `tex-mode1-shear`, `tex-mode1-identity`, `texture-mode-loads-matrix`, ... |
| 11 | swap `m[4]`/`m[5]` | KILLED | `tex-mode1-shear`, `tex-mode1-negative-rotation`, ... |
| 12 | swap `m[8]`/`m[9]` | KILLED | `tex-mode1-m8-m9` |
| 13 | swap `m[12]`/`m[13]` | KILLED | `tex-mode1-translation`, `tex-mode0-vs-mode1`, `tex-matrix-stack-ops` |
| 14 | use `m[2]/m[6]/m[10]/m[14]` (and `m[3]/m[7]/m[11]/m[15]` for t) | KILLED | `tex-mode1-identity`, `tex-mode1-translation`, `texture-mode-loads-matrix`, ... |
| 15 | `/4096.0f` instead of `/16.0f` in mode 1 | KILLED | `tex-mode1-identity`, `tex-mode0-vs-mode1`, `tex-matrix-stack-ops`, ... |
| 16 | s/t swapped in mode 1 only | KILLED | `tex-mode1-shear`, `tex-mode1-negative-rotation`, `tex-mode1-identity`, ... |

**Equivalence argument for #8:** for a negative `int64_t` sum `s`, a 64-bit
logical shift gives `((uint64_t)s) >> 12 == (s >> 12) + 2^52`, because
`2^64` is a multiple of `2^12`. The code then casts the shifted value to
`int32_t`, and `2^52` is a multiple of `2^32`, so the low 32 bits and hence
the final `int32_t` result are identical to the arithmetic shift. The mutant
is therefore equivalent **only** because of the mandated `(int32_t)` cast; it
would be killed without it.

### Sanctioned existing-literal updates

Only two existing tests encoded the old wrong formula. No test was deleted,
skipped or weakened; mode 0 and all non-texture assertions are unchanged:

1. `gx_interp/tex-mode0-vs-mode1` (mode-1 half only): `3.0/0.75` ->
   `2.0625/0.9375`. Hand check: `(8192*16 + 4096) >> 12 = 33 -> 2.0625`;
   `(2048*32 - 1024) >> 12 = 15 -> 0.9375`. The old values came from
   `m12*4096` and `/4096`. The same literals are adapted in
   `tools/test_gx_interp_golden.py`.
2. `gx_interp/tex-matrix-stack-ops`: `4.0` -> `2.125` (t stays 2.0). The
   scale(8192)+TRANS(4096) matrix folds to `m0=8192, m12=8192`, so
   `(8192*16 + 8192) >> 12 = 34 -> 2.125`. Same change in the golden stream.

All other pre-existing texture tests keep their literals: their s/t-row terms
are numerically identical under both formulas (`m*(s<<8)>>12/4096 ==
m*s>>12/16`), and they carry no `m8/m9` term; only translation (16x) and the
missing addends changed. Verified by running the unchanged suite: 142/142
pass on the baseline.
