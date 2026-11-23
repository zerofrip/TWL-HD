<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4o — the arm11 horizontal edges on real hardware: `ceil(1.25*y_ds)`, no `+1`, no row-0 wrap

**Status:** REAL HARDWARE MEASURED (offline analysis of the owner's capture).
The owner ran `arm11/twlhd-arm11.3dsx` on a real New 3DS and captured the top
screen. On this console the two frames' horizontal boundaries sit exactly at
`ceil(1.25*y_ds)` — nominal for our arm11 render — with **no** extra capture
row and **no** row-0 wrap. The Azahar host render of the same frames is one
capture row lower on every edge. EQ-4n hypothesis (B) is therefore supported
on this console: the `+1` is introduced by the Azahar presentation readback,
not by arm11 or PICA.

```
basis=one console, one capture session per frame
```

Labels follow `docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`:
**FACT** = a command and its complete raw output quoted here;
**OWNER-REPORTED** = the owner's statement, not reproduced here;
**SOURCE-READ** = a claim about repository source, cited by file/line;
**HYPOTHESIS** / **UNVERIFIED** = not measured by the quoted output.

## 1. What was run (OWNER-REPORTED procedure; offline analysis only)

- **Console and rig — OWNER-REPORTED.** One New 3DS. The top screen was
  captured through a passive capture board with the `cc3dsfs` rig
  (`tools/cc3dsfs_capture`) as 1280×720 root screenshots. The top screen is
  the top-left 400×240 crop; the arm11 render is 400×240, so the crop maps 1:1
  and a capture row is a render row. Each frame's session produced four
  byte-identical crops.
- **Frames — OWNER-REPORTED.** Frame A is the EQ-4n frame (sha256
  `1e6ef3f4…`), loaded from the SD card with A as `sdmc:/twlhd_frame.bin`.
  Frame B is frame A with every vertex y moved `+3` DS px (sha256
  `2959a7f2…`).
- **Program — OWNER-REPORTED.** The `.3dsx` sha256 was
  `93b86587f7f60511938e4d492e4ddbb2441f5a728f47998a7b209ef5acd0a241`,
  built Nov 23 from a `main` HEAD earlier than the EQ-4n report. It was **not
  rebuilt or re-run** this round, and no render was re-run.
- **Scope.** Every claim below is about this one console and one capture
  session per frame. Nothing here generalises to other consoles, other
  sessions or other geometry.

All commands actually run this round are quoted below with their complete raw
output. The staging directory is referred to only as `"$HWSTAGE"`.

## 2. Fixtures (FACT)

```
$ cp "$HWSTAGE"/hw_frameA_top400x240.png "$HWSTAGE"/hw_frameB_top400x240.png tools/eq4o_hw_rows/
$ cmp "$HWSTAGE"/hw_frameA_top400x240.png tools/eq4o_hw_rows/hw_frameA_top400x240.png && echo "identical A"
identical A
$ cmp "$HWSTAGE"/hw_frameB_top400x240.png tools/eq4o_hw_rows/hw_frameB_top400x240.png && echo "identical B"
identical B
$ sha256sum tools/eq4o_hw_rows/hw_frameA_top400x240.png tools/eq4o_hw_rows/hw_frameB_top400x240.png
68912b01d4a7e8164f41b8547910a0a533d4ca399cbf16e3a8bf729551856352  tools/eq4o_hw_rows/hw_frameA_top400x240.png
fc2b6534d1c50bfabdb11c21735ad8b4ab2eb5309a035116dce5f9328a3dbb3b  tools/eq4o_hw_rows/hw_frameB_top400x240.png
```

Both copied files are byte-identical to the staging inputs; the "identical"
lines would be absent if `cmp` differed.

## 3. Measurements (FACT)

`tools/eq4o_hw_rows/measure_rows.py` reuses the owner-side white-run logic
(mean `(R+G+B)/3 >= 128`), maps the six EQ-4n §3.3 DS edges with
`nominal = 1.25*(y_ds + dy)`, and reports the actual state transition: for a
`*.top` edge the first white row whose row above is dark, for a `*.bottom` edge
the first dark row whose row above is white, scanning `ceil(nominal)-3 ..
ceil(nominal)+3` (clamped to `0..239`). Row 0 counts as a transition when it is
already in the target state (white for `*.top`, dark for `*.bottom`), since
there is no row above; if no transition is found in the window the measured row
is `none` and the classification is `other`.

Frame A, column 100 (HEAD at time of run: `d18a9b9`):

```
$ python3 tools/eq4o_hw_rows/measure_rows.py tools/eq4o_hw_rows/hw_frameA_top400x240.png
size=400x240
white_threshold=128.0
dy=0
column=100
white_runs=0-19,40-79,160-199,220-239
row0_white=1
row0_rgb=255,255,255
row1_white=1
row1_rgb=255,255,255
row2_white=1
row2_rgb=255,255,255
row239_white=1
row239_rgb=255,255,255
row0_eq_row239=400/400
row0_eq_row239_shifted=397/399
row0_wrap=0
edge=top_band.bottom y_ds=16 nominal=20.00 ceil=20 measured=20 match=ceil
edge=sq1.top y_ds=32 nominal=40.00 ceil=40 measured=40 match=ceil
edge=sq1.bottom y_ds=64 nominal=80.00 ceil=80 measured=80 match=ceil
edge=sq3.top y_ds=128 nominal=160.00 ceil=160 measured=160 match=ceil
edge=sq3.bottom y_ds=160 nominal=200.00 ceil=200 measured=200 match=ceil
edge=bottom_band.top y_ds=176 nominal=220.00 ceil=220 measured=220 match=ceil
row120_first_white_col=40
row120_last_white_col=359
row120_col39_rgb=16,16,24
row120_col40_rgb=255,255,255
row120_col359_rgb=255,255,255
row120_col360_rgb=16,16,24
```

Frame B (`+3` DS rows), column 100 (HEAD at time of run: `d18a9b9`):

```
$ python3 tools/eq4o_hw_rows/measure_rows.py --dy 3 tools/eq4o_hw_rows/hw_frameB_top400x240.png
size=400x240
white_threshold=128.0
dy=3
column=100
white_runs=4-23,44-83,164-203,224-239
row0_white=0
row0_rgb=16,16,24
row1_white=0
row1_rgb=16,16,24
row2_white=0
row2_rgb=16,16,24
row239_white=1
row239_rgb=255,255,255
row0_eq_row239=80/400
row0_eq_row239_shifted=79/399
row0_wrap=0
edge=top_band.bottom y_ds=19 nominal=23.75 ceil=24 measured=24 match=ceil
edge=sq1.top y_ds=35 nominal=43.75 ceil=44 measured=44 match=ceil
edge=sq1.bottom y_ds=67 nominal=83.75 ceil=84 measured=84 match=ceil
edge=sq3.top y_ds=131 nominal=163.75 ceil=164 measured=164 match=ceil
edge=sq3.bottom y_ds=163 nominal=203.75 ceil=204 measured=204 match=ceil
edge=bottom_band.top y_ds=179 nominal=223.75 ceil=224 measured=224 match=ceil
row120_first_white_col=40
row120_last_white_col=359
row120_col39_rgb=16,16,24
row120_col40_rgb=255,255,255
row120_col359_rgb=255,255,255
row120_col360_rgb=16,16,24
```

Both frames print `row0_wrap=0`: the hardware row 0 is not the shifted last
row. Frame A's rows 0..2 are white; frame B's rows 0..2 are clear
`(16,16,24)`, so B does not wrap a white band into row 0 either. The vertical
edges are unchanged: row 120 is black at capture columns 39 and 360 and white
at 40 and 359 in both frames.

## 4. Edge table — hardware vs azahar vs nominal vs ceil

`hardware` is the `measured` value quoted in §3. `azahar` is the EQ-4n §3.3
table (`docs/reports/eq4n-arm11-row-shift.md` §3.3, `runA_199.ppm` /
`runB_199.ppm`), not re-measured this round. `ceil` is `ceil(nominal)`.

Frame A (azahar white runs `(0,20),(41,80),(161,200),(221,239)`):

| edge | y_ds | nominal | ceil | hardware | azahar | azahar−hardware |
|---|---:|---:|---:|---:|---:|---:|
| `top_band.bottom` | 16 | 20.00 | 20 | 20 | 21 | +1 |
| `sq1.top` | 32 | 40.00 | 40 | 40 | 41 | +1 |
| `sq1.bottom` | 64 | 80.00 | 80 | 80 | 81 | +1 |
| `sq3.top` | 128 | 160.00 | 160 | 160 | 161 | +1 |
| `sq3.bottom` | 160 | 200.00 | 200 | 200 | 201 | +1 |
| `bottom_band.top` | 176 | 220.00 | 220 | 220 | 221 | +1 |

Frame B, `+3` DS rows (azahar white runs
`(0,0),(5,24),(45,84),(165,204),(225,239)`):

| edge | y_ds | nominal | ceil | hardware | azahar | azahar−hardware |
|---|---:|---:|---:|---:|---:|---:|
| `top_band.bottom` | 19 | 23.75 | 24 | 24 | 25 | +1 |
| `sq1.top` | 35 | 43.75 | 44 | 44 | 45 | +1 |
| `sq1.bottom` | 67 | 83.75 | 84 | 84 | 85 | +1 |
| `sq3.top` | 131 | 163.75 | 164 | 164 | 165 | +1 |
| `sq3.bottom` | 163 | 203.75 | 204 | 204 | 205 | +1 |
| `bottom_band.top` | 179 | 223.75 | 224 | 224 | 225 | +1 |

The azahar frame B additionally has the wrapped `(0,0)` run that hardware does
not have (hardware B row 0 is `(16,16,24)`), consistent with EQ-4n §3.2.

## 5. Verdict

1. **Hardware matches `ceil(1.25*y_ds)` exactly for both frames** — all 12
   measured edges, `match=ceil`, no residual (§3, §4).
2. **No `+1` row and no row-0 wrap on hardware.** Frame A's first boundary is
   at row 20 (not 21) and its run starts at row 0 with 20 rows; frame B's runs
   start at 4/44/164/224 and rows 0..2 are clear; `row0_wrap=0` for both
   (§3).
3. **EQ-4n hypothesis (B) is supported on this console.** The `+1` is
   introduced by the Azahar host/core presentation readback, not by the arm11
   program and not by the PICA fill rule. The hardware render is the nominal
   half-open `ceil` geometry.
4. **Separately: the shift attributable to azahar is exactly +1 capture row
   for every edge in both frames** — 12 of 12 edges, one row each (§4), plus
   the azahar-only `(0,0)` wrapped run in frame B. This is a property of the
   host readback under test, not of the hardware.

**SOURCE-READ (EQ-4n §4, cited, not re-read this round).** The Azahar software
renderer reads the top framebuffer as `framebuffer[y][stride-x]` and the
libretro blit maps that column axis to the capture rows; the arm11
target/viewport/clipfix has no one-row term. That is the mechanism hypothesis
(B) names for the `+1`.

No generalisation is made beyond this console and these two capture sessions.

## 6. UNVERIFIED

- Other consoles and other capture sessions (all numbers here are one New 3DS,
  one session per frame).
- Other geometry: textured polygons, non-axis-aligned edges, and 1-px lines.
  Only the axis-aligned opaque `ds_edge_test` scene was captured.
- Why azahar reads `pixel_stride - x` (the missing `-1`): the upstream intent
  is not established; EQ-4n matched the arithmetic only.
- The `arm11/twlhd-arm11.3dsx` used was **not rebuilt** this round, so its
  binary content rests on the owner's reported sha256.
- The four byte-identical crops per session are the owner's report, not
  re-hashed here.

## 7. Consequence

- **No change to `arm11/` or `common/`.** The hardware result removes the
  reason for a production offset; the arm11 path and PICA geometry stay as
  they are.
- **Host-based per-row comparisons must subtract the presentation row** (one
  capture row) or fix the core readback. This applies to `tools/azahar_host`
  measurements: a comparison against hardware must add `-1` to the captured
  row, or the Azahar core's readback must be corrected.
- **Proposed separately, not implemented here:** a follow-up task to fix the
  core readback (the transposed `pixel_stride - x` access) upstream or to
  teach the host tool to drop the extra presentation row. No code was changed
  in this task.

## 8. Tests (FACT)

The new suite contains the negative case (a synthetic `+1` capture-row shift
must be detected), the two new early-edge negative cases, and the real
fixtures. HEAD at time of run: `d18a9b9`.

```
$ make test-eq4o-hw-rows
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4o_hw_rows -p 'test_*.py'
.............
----------------------------------------------------------------------
Ran 13 tests in 0.798s

OK
```

`make test` also passes with the new target wired into its prerequisite list
(HEAD at time of run: `d18a9b9`):

```
$ make test > /tmp/opencode/eq4o-make-test.log 2>&1; rc=$?; echo "make_test_exit=$rc"; tail -30 /tmp/opencode/eq4o-make-test.log
make_test_exit=0

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
..................................
----------------------------------------------------------------------
Ran 34 tests in 0.748s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4o_hw_rows -p 'test_*.py'
.............
----------------------------------------------------------------------
Ran 13 tests in 0.798s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
.....sssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 394 tests in 87.802s

OK (skipped=69)
/tmp/tmp5m2jij9o/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

The fixture tests are not skipped: `Ran 13 tests` with no `s` for the
`HardwareFixtureTest` cases.

## 9. Privacy and diff budget (FACT)

```
$ grep -rn -E '/(home|Users)/' Makefile docs/EQ4_HW_COMPARE.md docs/EQUALITY_CHECK_DESIGN.md docs/reports/eq4n-arm11-row-shift.md tools/eq4o_hw_rows docs/reports/eq4o-hw-row-shift.md
$ git diff main | grep -cE '/(home|Users)/'
0
```

The first command printed nothing (no match). The staging path appears only as
`"$HWSTAGE"` in this report.

```
$ git add -N tools/eq4o_hw_rows docs/reports/eq4o-hw-row-shift.md
$ git diff main --name-only
Makefile
docs/EQ4_HW_COMPARE.md
docs/EQUALITY_CHECK_DESIGN.md
docs/reports/eq4n-arm11-row-shift.md
docs/reports/eq4o-hw-row-shift.md
tools/eq4o_hw_rows/README.md
tools/eq4o_hw_rows/hw_frameA_top400x240.png
tools/eq4o_hw_rows/hw_frameB_top400x240.png
tools/eq4o_hw_rows/measure_rows.py
tools/eq4o_hw_rows/test_measure_rows.py
$ git diff main | wc -c
52025
```

No production directory (`arm9v2/`, `arm11/`, `common/`, `arm7/`, `arm9/`) is
in the diff. No `python3 -c` was used; every analysis ran as a script file.

## 10. Requirement traceability

| User-task requirement | Where |
|---|---|
| 1. `tools/eq4o_hw_rows/` fixtures + tool + tests + README, wired into `make test` | §2, §3, §8; `tools/eq4o_hw_rows/`, `Makefile` |
| 2. Report with (a) procedure, (b) edge tables, (c) verdict, (d) labels, (e) consequence | §1, §4, §5, §6, §7 |
| 3. Append-only pointers in three docs | §9 diff name list; EQ-4o updates in `docs/EQ4_HW_COMPARE.md`, `docs/EQUALITY_CHECK_DESIGN.md`, and the one-line note in `docs/reports/eq4n-arm11-row-shift.md` §4 |
| 4. FACT quoting, privacy, test integrity, diff budget, no `python3 -c` | §1–§9 |
