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
