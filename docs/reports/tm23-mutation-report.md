# tm23 — mutation report for texture transform modes 2/3

BUILD/UNIT only. No emulator run, no console/SD/network action, no hardware
and no EMULATOR/HARDWARE claim anywhere in this report. Everything below was
produced on the host with the host test suite.

## Run

```
python3 docs/reports/tm23_mutate_gx_interp_tex23.py --jobs 4
```

Runner: `docs/reports/tm23_mutate_gx_interp_tex23.py`, modelled on
`docs/reports/5c1_mutate_tex_capture.py`. It is not part of `make test`.

Target: `common/twlhd_gx_interp.c`, only the NEW modes-2/3 code paths:

- the raw-normal latch in `gx_compute_normal_colour`
  (`g->normal[0..2] = n[0..2]`);
- the `compute_tex_vertex` mode-2 branch (`tmode == 2u`) and its
  `m[0]/m[4]/m[8] * normal[*]`, `tex_s/tex_t * 16777216`, `>> 24` and
  `/ 16.0f` terms;
- the mode-3 branch (`tmode == 3u`) and its
  `m[0]/m[4]/m[8] * raw_vtx[*]` terms;
- the `g->raw_vtx[0..2] = rx/ry/rz` plumbing in `emit_vertex`;
- the raw VTX arguments in the VTX_16/VTX_10/VTX_XY/VTX_XZ/VTX_YZ handlers
  (`(int32_t)sign_extend16(...)`, `sign_extend10(...) * 64`);
- the `TexCoordTransformMode` extraction `(teximage_param >> 30) & 0x3u`.

Mechanism: the mutant text is written over `common/twlhd_gx_interp.c`, the
host test is rebuilt and run; a surviving mutant is additionally checked
against the Python golden suite (`test_gx_interp_golden.py`, rebuilt CLI).
The repository file is restored in `finally:`. Parallel workers use private
copies under `/tmp/opencode`.

## Result (real numbers)

| metric | count |
|---|---|
| unique mutants | **125** |
| killed by `tests/host/build/test_gx_interp` | **104** |
| killed by `tools/test_gx_interp_golden.py` | **21** |
| killed at build (does not compile) | 0 |
| **survivors** | **0** |
| `common/twlhd_gx_interp.c` restored | YES |

Mutant mix: 114 numeric-constant mutants (including every array index, the
`16777216` base constant and the `24` shift amount), 2 comparison/logical
operator mutants, and 9 pinned targeted mutants. Every survivor from the
first run was killed by a new test (see below); the second run has none.

The generic pass never rewrites the `<<`/`>>` operator tokens themselves.
The shift semantics are pinned two ways instead: the shift *amounts* are
numeric constants on the mutated lines (`24 -> 23/25`, `30 -> 29/31`), and the
pinned targeted set rewrites the exact mode-2/3 expressions
`(s_acc >> 24) / 16.0f`, `(t_acc >> 24) / 16.0f` and `* 16777216`
(shift 24 -> 20/28, base multiplier 2^24 -> 2^23/2^25, output scale 16 ->
8/32, mode mask 1 bit, mode shift 29, mode-2 branch -> mode 1, mode-3 branch
-> mode 2). A missing targeted substring is a hard error in the runner.

## Survivors from the first run and how they were killed

First run: 125 mutants, 123 killed, 2 survivors.

1. `line 765 const 16777216 -> 16777217` (mode-2 `tex_t` base multiplier).
   With `t = -1` and no matrix/normal contribution the correct term is
   `-1 * 2^24 >> 24 = -1`, while the mutant gives
   `-1 * 16777217 >> 24 = -2`. A test with a negative base coordinate was
   missing. Killed by the new
   `gx_interp/tex-mode2-base-shift-lsb`
   (`tests/host/test_gx_interp.c`): identity matrix, initial normal 0,
   TEX_COORD (-1,-1), expected (-0.0625, -0.0625). The same case also pins
   the exact `(s<<24)` shift for the `s` row.
2. `line 1047 const 0 -> 1` (`g->raw_vtx[0]` argument in the VTX_YZ handler
   becoming `g->raw_vtx[1]`). No test emitted a mode-3 vertex that had gone
   through VTX_YZ: the golden partial-command scene put the VTX_YZ vertex in a
   triangle that was dropped as incomplete. Killed by (a) the new
   `gx_interp/tex-mode3-partial-vtx-raw` C test, which runs VTX_16, VTX_XY,
   VTX_XZ, VTX_YZ and two more vertices so both triangles are emitted, and
   (b) completing the second triangle in
   `test_tex_random_vtx_partial_commands`.

No survivor is left unkilled; there is no EQUIVALENT declaration in this
round.

## Notes and limits

- The 64-bit accumulation is exercised by
  `gx_interp/tex-mode23-64bit-headroom` (both modes) and the random golden
  scenes: every product/sum exceeds 32 bits there, so a 32-bit accumulator or
  a truncated product diverges.
- The constants pin the DeSmuME formula at
  `desmume/src/gfx3d.cpp:1594-1602`; per the research document the DS
  behaviour itself remains UNVERIFIED (matched to the DeSmuME model only, no
  hardware run). This report makes no EMULATOR/HARDWARE claim.
