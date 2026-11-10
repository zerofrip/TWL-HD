# 5B-2a repair report: format-5 (4x4) palette window

Status: **IMPLEMENTED**, host-side only. `make test` green.

## Defect and fix

The original 5B-2a design fixed `palette_size == 0` for format 5 (4x4
compressed). DeSmuME's PAL4X4 unpacker reads palette RAM per block
(`texcache.cpp` ~1752, unpack ~1789-1843: `palAddress = PLTT_BASE<<4 +
pal1offset`, `pal1offset = (blockword & 0x3FFF)<<1`). `paletteSizeList[5] = 0`
(`texcache.cpp:418/441`) is only the texture-cache copy size, not a statement
that the format has no palette.

Format 5 `palette_size` is now a caller/capture-chosen even window in
`[2, 65536]` bytes. One shared helper,
`twlhd_v2_tex_palette_size_ok()`, is called by both the parser and the writer.
All other formats keep their fixed sizes and format 7 keeps 0.

## Updated tests (old behaviour)

The four format-5 spots that asserted the old rule were updated:

- `tests/host/test_v2_parse.c` old 4x4 positive (palette 0, no PLDT, OK) now
  carries an 8-byte palette plus PLDT, with added 2-byte and 65,536-byte
  positive boundaries.
- `tests/host/test_v2_parse.c` old "4x4 no palette is OK" spot now expects
  `ERR_TXDS_SIZE` for `palette_size 0`, plus a new missing-PLDT format-5
  `ERR_MISSING_CHUNK` case.
- `tests/host/test_v2_write.c` `palette_sizes[4]` changed `0 -> 8`;
  `palette_offsets` recomputed `{0,64,72,104,616,624,0}` (PLDT total 640).
- `tests/host/test_v2_write.c` "no-palette omits PLDT" switched from the 4x4
  entry (index 4) to the 16bpp entry (index 6). The old writer
  "4x4 has no palette -> ERR_TXDS_SIZE" case became format-5 0/odd/65,537
  negatives.

New positives: parser 2-byte and 65,536-byte windows; writer 8-byte and
65,536-byte round trips. New negatives: parser 0, 3, 65,537, 65,538 and
missing PLDT; writer 0, 3, 65,537. The 65,538 case is even so parity cannot
reject it; it isolates the upper bound.

## Mutation proof

Scratch copies only, under `/tmp/opencode/mutants/` (nothing added to the
worktree). Each mutant was built against the unmodified test objects and run
against both `test_v2_parse` and `test_v2_write`.

| # | Mutant | Result | Killing test / reason |
|---:|---|---|---|
| 1 | `TWLHD_V2_TXDS_RECORD_SIZE` 40 -> 36 | KILLED | `v2-parse/tex`, `v2-write/tex-golden`: positive file no longer parses |
| 2 | reserved-field check removed (`parse.c:329`) | KILLED | `v2-parse/tex` reserved-nonzero case returned `OK`, expected `ERR_TXDS` |
| 3 | offset+size bounds 64 -> 32-bit (`parse.c:388-393`) | KILLED | `v2-parse/tex` 64-bit overflow case returned `OK`, expected `ERR_TXDS_BOUNDS` |
| 4a | texel formula shift off-by-one (format 2) | KILLED | `v2-parse/tex`, `v2-write/tex-golden`: I2 sizes mismatch |
| 4b | format-5 index `texels >> 3` -> `>> 2` | KILLED | `v2-parse/tex`, `v2-write/tex-formats`: 4x4 index size mismatch |
| 5 | duplicate-key check removed (`parse.c:337-350`) | KILLED | `v2-parse/tex` duplicate key returned `OK`, expected `ERR_TXDS_KEY` |
| 6 | TXDT-required check removed (`parse.c:379`) | KILLED | `v2-parse/tex` missing TXDT returned `ERR_TXDS_BOUNDS`, expected `ERR_MISSING_CHUNK` |
| 7 | writer always emits `palette_offset 0` (`write.c:185-186`) | KILLED | `v2-write/tex-golden` / `v2-write/tex-formats` palette offsets |
| 8 | parser ignores PLDT size (`parse.c:399`) | KILLED | `v2-parse/tex-limits` oversize PLDT returned `OK`, expected `ERR_PLDT` |
| 9 | format-5 `palette_size 0` accepted | KILLED | `v2-parse/tex` zero-sized window and `v2-write/tex-errors` zero case |
| 10 | format-5 odd `palette_size` accepted | KILLED | `v2-parse/tex` 3-byte window; `v2-write/tex-errors` odd case |
| 11 | format-5 upper bound 65,536 -> 65,537 | SURVIVED (equivalent) | see below |
| 11b | same bound 65,536 -> 65,538 (companion) | KILLED | `v2-parse/tex` 65,538 window returned `OK`, expected `ERR_TXDS_SIZE` |

Mutant 11 is equivalent under the independent parity rule: 65,537 is odd, so
the only input it would newly accept is rejected by the parity check anyway.
No input can distinguish it while parity is enforced, so the mandatory 65,538
negative cannot kill it either (both the correct bound 65,536 and the mutated
65,537 reject 65,538). The observable off-by-two variant (11b) is killed by
the 65,538 test, proving the upper bound is exercised. This report records the
contract's stated mutant honestly rather than claiming a kill it cannot have.

All mutant builds were outside the worktree; `git status` at the end shows only
the files listed below.

## Repair round 2

Five surviving mutants in `validate_tex()` (`common/twlhd_v2_parse.c`) were
targeted with new unit tests. Each mutation was applied to a scratch copy of the
parser under `/tmp/opencode/mutants2/` (outside the worktree), built against the
unmodified test sources, and run; every one turns its new test RED on the
mutated parser and GREEN on the pristine worktree.

| # | Mutant | Result | Killing test |
|---:|---|---|---|
| 1 | reserved field at record offset 36 accepted (second disjunct removed, `parse.c:345`) | KILLED | `v2-parse/tex-reserved1` |
| 2 | index `offset + size` bounds check removed (`parse.c:406-407`) | KILLED | `v2-parse/tex-index-bounds` (both the 24 > 20 range case and the 64-bit overflow case) |
| 3 | `rec.index_size != index_bytes` disabled (`parse.c:380`) | KILLED | `v2-parse/tex-index-size` (format 2 with a 4-byte index; format 5 with index size 4) |
| 4 | `palette_size == 0 && palette_offset != 0` check removed (`parse.c:386`) | KILLED | `v2-parse/tex-palette-offset` (format 7, offset 4, PLDT cushion) |
| 5 | `txds_size % 40` gate removed (`parse.c:333`) | KILLED | `v2-parse/tex-record-size` (44- and 48-byte payloads: one valid record plus trailing bytes) |

The record-size test also documents that a **39-byte** TXDS payload can never
reach `validate_tex`: the chunk payload `% 4` gate runs first (docs section 8,
step 8), so 39 bytes yields `ERR_CHUNK_ALIGN`. The `% 40` mutant is therefore
killed by the 44/48-byte payloads, not by 39.

The known-equivalent mutant at `parse.c:383` (`texel_size == 0 &&
texel_offset != 0`) was left alone: `texel_size` is never 0 for formats 1..7,
so no test-only input can distinguish it.

A byte-exact format-5 golden (264 bytes, 0x108, chunk count 5) was added to both
texture test files. It was derived by running the writer in a scratch build
under `/tmp/opencode/fmt5/` and checked field by field against docs section
7.1 / section 8; the two literals are byte-identical.

## Verification

- `make test` (host suite plus Python suite): green.
- `./tests/host/build/test_v2_parse`, `./tests/host/build/test_v2_write`:
  green after the test updates.
- Mutation builds: `/tmp/opencode/mutants/run_mutants.py` (round 1) and
  `/tmp/opencode/mutants2/run_mutants.py` (round 2), results above.

## Notes and residual risk

- `TWLHD_V2_PLDT_MAX_BYTES` stays 98,304 (6 x 16 KiB DS palette slots). One
  maximum 65,536-byte format-5 window fits; 16 such windows (1,048,576 bytes)
  exceed both this cap and the 1 MiB file limit. Raising the cap was rejected
  because the DS palette model and the file limit both rule the case out.
  Colours beyond the captured window are a later decoder-stage error, not a
  parser error.
- `tools/twlhd_v2_dump.py` remains deliberately unaware of TXDS/TXDT/PLDT
  (section 9 divergence); not touched.
- This report lives at `docs/reports/5b2a-mutation-report.md`.
