<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4m — Which row/column rule does our PICA render path follow?

**Status:** OFFLINE MEASUREMENT. No hardware action, no console access, no
production-code change. One libnds test ROM was fed through our existing
capture -> v2 frame -> arm11 PICA render -> `tools/azahar_host` chain and the
400x240 top-screen output was measured with the existing
`tools/ds_edge_test/measure_rows.py`.

```
basis=one build of each ROM, one DeSmuME capture, two Azahar host runs, one
      fixture-independent analyser; labels per docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14
```

Labels: **FACT** = a command and its complete raw output quoted here;
**SOURCE-READ** = a quoted `file:line`; **EMULATOR** = a DeSmuME/Azahar
measurement; **HYPOTHESIS** / **UNVERIFIED** = not measured by the quoted
output.

**Question.** Hardware paints the half-open vertex rectangle `[y0,y1)`
(`docs/reports/eq4l-edge-rows-hw-result.md`); DeSmuME adds one DS row on
horizontal edges (`docs/reports/eq4k-edge-rows.md`). Which rule does our
`twlhd_frame_v2.bin` -> arm11 renderer -> Azahar path follow?

**Answer (summary).** A **third rule at DS measurement scale**: every
horizontal painted boundary lands `capture_y = 1.25 * y_ds + 1.0` (one 400x240
PICA pixel = 0.8 DS px below the committed mapping) and every vertical edge
`capture_x = 40 + 1.25 * x_ds` exactly. Edge *lengths* are nominal (the top
band covers 20 capture rows = 16 DS rows), so the edge rule itself is the
hardware half-open `[y0,y1)`, not DeSmuME's `+1` DS row; the painted 3D output
sits one capture pixel lower. Whether that is a uniform framing offset or the
rasteriser's boundary-inclusion rule is UNVERIFIED. At 256x192 whole-pixel
resolution the 0.8 DS offset rounds every interior item to `+1`, so the
resampled render is indistinguishable from DeSmuME apart from the 1-px lines.
Details, decomposition and uncertainty: §5-§6.

## 1. REQ 1 — can the unchanged chain feed `ds-edge-test.nds`?

### 1.1 Plain ROM build (FACT)

The plain ROM is unchanged and byte-identical to the EQ-4k/EQ-4l build:

```
$ make -C tools/ds_edge_test clean && make -C tools/ds_edge_test
make: Entering directory '.../tools/ds_edge_test'
clean ...
make: Leaving directory '.../tools/ds_edge_test'
make: Entering directory '.../tools/ds_edge_test'
main.c
linking ds-edge-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-edge-test.nds
make: Leaving directory '.../tools/ds_edge_test'
$ sha256sum tools/ds_edge_test/ds-edge-test.nds
3d417c162d5067f1febf7f119182ede6fd8a88e56c28c30853b67857fd0a2706  tools/ds_edge_test/ds-edge-test.nds
```

`3d417c16…` is the EQ-4k ROM hash (`docs/reports/eq4k-edge-rows.md` §2), so the
plain target is untouched by this round.

### 1.2 Plain ROM through the unchanged capture gate — BLOCKED (FACT)

The existing capture script needs a v2 frame written by the ROM; this ROM links
only `-lnds9` (`tools/ds_edge_test/README.md` §1) and never opens
`/twlhd_frame_v2.bin`.

```
$ R=$(mktemp -d)   # scratch dir outside the repo
$ DESMUME_CLI="$HOME/3ds/hwtest-staging/desmume-dbg-build5/cli/desmume-cli" \
  DESMUME_TIMEOUT=25 tools/emulator/desmume_capture.sh \
  tools/ds_edge_test/ds-edge-test.nds "$R/cap-plain" >"$R/plain.out" 2>"$R/plain.err"
$ echo "capture_exit=$?"
capture_exit=1
$ grep -n "error:" "$R/plain.err"
44:+ echo 'error: DeSmuME did not report both the Slot-2 MPCF device and'
45:error: DeSmuME did not report both the Slot-2 MPCF device and
$ grep -n "Slot 2: MPCF\|AUTO-PATCHING DLDI" "$R/cap-plain/desmume.log"
6:Slot 2: MPCF Flash Card Device
$ grep -c 'AUTO-PATCHING DLDI' "$R/cap-plain/desmume.log"
0
```

The emulator started and reported the Slot-2 MPCF device, but the ROM has no
DLDI section to patch: it links `-lnds9` only and no `-lfat`
(`tools/ds_edge_test/Makefile`: `LIBS := -lnds9`). The ROM also never calls
`fopen("/twlhd_frame_v2.bin", "wb")`, so even if the DLDI gate passed, the
script's file check (`tools/emulator/desmume_capture.sh:206-209`) would fail.

**REQ 1 verdict for the unchanged arm: UNVERIFIED.** The missing piece is
ROM-side: `ds-edge-test.nds` has no GX recorder/DLDI, so
`tools/emulator/desmume_capture.sh` cannot produce `twlhd_frame_v2.bin` from it.
No production file (`arm9v2/`, `arm11/`, `common/`, `arm7/`) was edited.

### 1.3 Sanctioned fallback: a recorder variant under `tools/ds_edge_test/`

A variant ROM, `ds-edge-record`, is built from `tools/ds_edge_test/record/main.cpp`
by `make -C tools/ds_edge_test record`. It draws the identical scene from
`include/quads.h` and links `common/twlhd_gx_record.c` plus libfat; the dump
sequence is a copy of `arm9v2/source/main.cpp:90-170,240-268`. The plain target
and its "no `common/` code" assertion are unchanged and still pass (§8).

```
$ make -C tools/ds_edge_test record
make: Entering directory '.../tools/ds_edge_test'
main.cpp
twlhd_attr_pad.c
twlhd_clipfix.c
twlhd_frame_parse.c
twlhd_geometry_slots.c
twlhd_gx_interp.c
twlhd_gx_record.c
twlhd_pica_frame.c
twlhd_pica_tex.c
twlhd_tex_capture.c
twlhd_tex_decode.c
twlhd_tex_draw_plan.c
twlhd_tex_source.c
twlhd_v2_parse.c
twlhd_v2_write.c
twlhd_vertex_convert.c
linking ds-edge-record.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-edge-record.nds
make: Leaving directory '.../tools/ds_edge_test'
$ sha256sum tools/ds_edge_test/ds-edge-record.nds
bec5cf280e98c68c3a9109c57ce196a70ad7afd2c19e940e3a95d810586fd4ef  tools/ds_edge_test/ds-edge-record.nds
```

### 1.4 The variant through the unchanged capture gate (FACT)

```
$ DESMUME_CLI="$HOME/3ds/hwtest-staging/desmume-dbg-build5/cli/desmume-cli" \
  DESMUME_TIMEOUT=25 tools/emulator/desmume_capture.sh \
  tools/ds_edge_test/ds-edge-record.nds "$R/cap-record" \
  >"$R/rec.out" 2>"$R/rec.err"
$ echo "capture_exit=$?"
capture_exit=0
$ tail -2 "$R/rec.err"
+ cp /tmp/.../cap-record/fat/twlhd_frame_v2.bin /tmp/.../cap-record/twlhd_frame_v2.bin
+ echo 'capture: /tmp/.../cap-record/twlhd_frame_v2.bin'
$ grep -n "Slot 2: MPCF\|AUTO-PATCHING DLDI" "$R/cap-record/desmume.log"
6:Slot 2: MPCF Flash Card Device
14:AUTO-PATCHING DLDI to MPCF! Lucky you!
$ ls -l "$R/cap-record/twlhd_frame_v2.bin"
-rw-rw-r-- 1 <user> <group> 1392 Nov 24 00:57 /tmp/.../cap-record/twlhd_frame_v2.bin
$ sha256sum "$R/cap-record/twlhd_frame_v2.bin"
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  /tmp/.../cap-record/twlhd_frame_v2.bin
```

### 1.5 GX stream and harness cross-check (FACT / EMULATOR)

Our interpreter's view of the captured frame:

```
$ tests/host/build/gx_interp_cli --eq2 "$R/cap-record/twlhd_frame_v2.bin"
twlhd-eq2-clip 1
frame 1
viewport 0 0 255 191 1
v 0 BF800000 3F800000 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 1 3F800000 3F800000 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 2 3F800000 BF7FFF80 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 3 BF800000 3F800000 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 4 3F800000 BF7FFF80 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 5 BF800000 BF7FFF80 00000000 3F800000 00000000 00000000 00000000 3F800000 00000000 00000000 0 0
v 6 BF800000 3F800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 7 3F800000 3F800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 8 3F800000 3F555560 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 9 BF800000 3F800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 10 3F800000 3F555560 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 11 BF800000 3F555560 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 12 BF800000 BF5554E0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 13 3F800000 BF5554E0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 14 3F800000 BF7FFF80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 15 BF800000 BF5554E0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 16 3F800000 BF7FFF80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 17 BF800000 BF7FFF80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 18 BF800000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 19 BF600000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 20 BF600000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 21 BF800000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 22 BF600000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 23 BF800000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 24 3F600000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 25 3F800000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 26 3F800000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 27 3F600000 3E2AAB80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 28 3F800000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 29 3F600000 BE2AA980 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 30 BF400000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 31 BF000000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 32 BF000000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 33 BF400000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 34 BF000000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 35 BF400000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 36 3E800000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 37 3F000000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 38 3F000000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 39 3E800000 3F2AAAC0 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 40 3F000000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 41 3E800000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 42 BF400000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 43 BF000000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 44 BF000000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 45 BF400000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 46 BF000000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 47 BF400000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 48 3E800000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 49 3F000000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 50 3F000000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 51 3E800000 BEAAAA00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 52 3F000000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 53 3E800000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 54 BF000000 36800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 55 3F000000 36800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 56 3F000000 BC2A9A80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 57 BF000000 36800000 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 58 3F000000 BC2A9A80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 59 BF000000 BC2A9A80 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 60 3F200000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 61 3F220000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 62 3F220000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 63 3F200000 3EAAAB00 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 64 3F220000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
v 65 3F200000 BF2AAA40 BC800000 3F800000 3F800000 3F800000 3F800000 3F800000 00000000 00000000 0 0
stats commands=94 vertices_in=44 triangles_out=22 quads_split=11 dropped_incomplete=0 ignored_outside_begin=0 ignored_no_effect=0 unsupported=0 raw_fifo_skipped=0 stack_errors=0 capacity_dropped=0 geometry_incomplete=0 tex_transform_unsupported=0
```

The `frame 1`, `viewport 0 0 255 191 1`, 44 vertices and 11 split quads match
the ROM's `glViewport(0,0,255,191)` and 11 submitted quads. The same captured
frame against the out-of-tree harness dump of the same ROM:

```
$ "$HOME/3ds/twlhd-desmume-harness/harness" tools/ds_edge_test/ds-edge-record.nds 1 \
    --out "$R/h-record.txt" --ppm "$R/h-record.ppm" >"$R/h.out" 2>"$R/h.err"
$ echo "harness_exit=$?"
harness_exit=0
$ grep -E "harness:" "$R/h.err"
harness: captured frame 1: 11 polygons, 44 vertices (first non-empty 3D frame: 1)
$ tests/host/build/gx_interp_cli --eq2 "$R/cap-record/twlhd_frame_v2.bin" >"$R/ours.txt"
$ echo "ours_exit=$?"
ours_exit=0
$ sha256sum "$R/ours.txt"
8123b21a8abb3d9394ca34415d1fe9d6fb445610270bdfc2bdb25f61cc104ef3  /tmp/.../ours.txt
$ python3 tools/eq2_compare.py --harness-frame 1 --ours-frame 1 \
    "$R/h-record.txt" "$R/ours.txt"
PASS frame=1 polygons=11 vertices=44
x: worst 0 16.16 px tol 65536 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
y: worst 1536 16.16 px tol 65536 at poly 10 (rawpoly 2) vertex 2 (our vertex 12)
z: worst 0 0.31 tol 4294967 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
w: worst 0 20.12 tol 16 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
r: worst 0 6-bit tol 2 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
g: worst 0 6-bit tol 2 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
b: worst 0 6-bit tol 2 at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
tex-s: worst 0 12.4 reported, not gated at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
tex-t: worst 0 12.4 reported, not gated at poly 9 (rawpoly 0) vertex 0 (our vertex 2)
```

The worst geometry difference is `1536/65536 = 0.0234` DS px (the 20.12
interpreter quantisation), so the captured v2 stream is a faithful record of the
ROM's transforms. The DeSmuME native frame of the *same* recorder ROM is the
EMULATOR control (full output):

```
$ python3 tools/ds_edge_test/measure_rows.py --native "$R/h-record.ppm"
mode=native
size=256x192
white_threshold=128.0
first_white_row=0
last_white_row=191
first_white_col=0
last_white_col=255
white_row_runs=0-16,33-160,177-191
white_col_runs=0-255
frame_top_row_white=1
frame_bottom_row_white=1
frame_left_col_white=1
frame_right_col_white=1
frame_vertical_shift=none
frame_horizontal_shift=none
item=top_band nominal_rows=0-15 nominal_cols=0-255 measured_rows=0-16 measured_cols=0-255 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=17
item=bottom_band nominal_rows=176-191 nominal_cols=0-255 measured_rows=177-191 measured_cols=0-255 row_diff=1 row_end_diff=0 col_diff=0 col_end_diff=0 row_count=15
item=left_band nominal_rows=80-111 nominal_cols=0-15 measured_rows=81-112 measured_cols=0-15 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=right_band nominal_rows=80-111 nominal_cols=240-255 measured_rows=81-112 measured_cols=240-255 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq1 nominal_rows=32-63 nominal_cols=32-63 measured_rows=33-64 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq2 nominal_rows=32-63 nominal_cols=160-191 measured_rows=33-64 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq3 nominal_rows=128-159 nominal_cols=32-63 measured_rows=129-160 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq4 nominal_rows=128-159 nominal_cols=160-191 measured_rows=129-160 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=hline nominal_rows=96-96 nominal_cols=64-191 measured_rows=96-97 measured_cols=64-191 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=2
item=vline nominal_rows=64-159 nominal_cols=208-208 measured_rows=65-160 measured_cols=208-208 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=96
classification=mixed
```

This is byte-for-byte the EQ-4k DeSmuME result (`docs/reports/eq4k-edge-rows.md`
§3), confirming the recorder variant did not change the scene.

## 2. REQ 2 — render through our chain and measure

### 2.1 Scaling and viewport (SOURCE-READ)

The DS 256x192 field is the centred 320x240 region of the 400x240 top screen:
40 px pillar bars in x, 1.25 screen px per DS px. SOURCE-READ —
`tools/eq3_compare.py:77-88`:

```
# Our top screen: the DS 256x192 viewport is the centred 320x240 region with
# 40 px pillar bars (common/twlhd_clipfix.c, docs/PHASE5A_V2_RENDERER_DESIGN.md
# lines 48-49). 0.8 = 320/400 is the clipfix factor; 1.25 = 320/256 is its
# inverse, used here to sample the screen back onto the DS grid.
SCREEN_X0 = 40
SCREEN_W = 320
SCREEN_H = 240
```

SOURCE-READ — `common/twlhd_clipfix.h:24-26` and
`common/twlhd_clipfix.c:34-42` (half-extents from the recorded viewport word;
our ROM's `glViewport(0,0,255,191)` gives `hw=hh=1`, `cx=cy=0`, and the
`0.8 = 320/400` x-factor):

```
 *   - The 320x240 top-screen region sits inside the 400x240 target, so the
 *     0.8 factor is 320/400, the share of the 400-pixel top-screen axis used
 *     by the pillar-boxed region; 256/320 also hitting 0.8 is a coincidence.
```

```
    hw = (float)width / 256.0f;
    hh = (float)height / 192.0f;
    cx = (float)(2 * (int)x1 + width) / 256.0f - 1.0f;
    cy = (float)(2 * y1v + height) / 192.0f - 1.0f;

    m[0][0] = 0.0f;  m[0][1] = hh;    m[0][2] = 0.0f;  m[0][3] = cy;
    m[1][0] = -0.8f * hw; m[1][1] = 0.0f; m[1][2] = 0.0f; m[1][3] = -0.8f * cx;
```

The committed analyser mapping is the same (`tools/ds_edge_test/capture_mapping.json`:
scale 1.25, offset 40,0). The Azahar host renders the top screen as the upper
400x240 rows of a 400x480 buffer (`tools/azahar_host/README.md` §"Fixed core
option answers": `citra_layout_option=default`, `citra_resolution_factor=1`).

### 2.2 Azahar render of the captured v2 frame (EMULATOR / FACT)

This round runs the chain once, in order: rebuild the arm11 program and hash
it, stage the frame, run the Azahar host twice into separate `--out`
directories and hash both PPMs, then crop and measure run 1's PPM (§2.3/§2.4).
Every measured number for *our* chain in §2-§7 comes from this sequence; the
hardware and DeSmuME columns of §5.1 are quoted from EQ-4l/EQ-4k. The staged
frame is identified only by the Step 2 `sha256sum` (`1e6ef3f4…`); the §1.4
capture into this `$R` is the capture of record. These hashes and numbers
belong to the REPAIR-3 sequence at HEAD `b775345`; later documentation-only
commits change the embedded build ID and thereby the full-PPM/bottom-crop
hashes (HYPOTHESIS / UNVERIFIED, SOURCE-READ `arm11/Makefile:75-84`,
`arm11/source/main.c:406`), while the top-screen measurement (`227454cc…`,
every edge `+0.8000`/`+0.0000`, fit zero-residual) is the load-bearing result.

REPAIR-3 re-ran the sequence at work-tree HEAD `b775345`. The
source tree is unchanged since `a4b0cd9`; the quoted read-only check backs that:

```
$ git diff --stat a4b0cd9 HEAD -- . ':!docs'; echo "diff_exit=$?"
diff_exit=0
```

HYPOTHESIS / UNVERIFIED (SOURCE-READ `arm11/Makefile:75-84`,
`arm11/source/main.c:406`): the build ID embeds the short HEAD, so the `.3dsx`,
full-PPM and bottom-crop hashes moved; the top crop and every measured number
are unchanged.

**Step 1 — build and hash the arm11 program:**

```
$ make -C arm11 clean && make -C arm11 2>&1 | tail -6; echo "make_exit=${PIPESTATUS[0]}"
make: Entering directory '.../arm11'
clean ...
make: Leaving directory '.../arm11'
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make: Leaving directory '.../arm11'
make_exit=0
$ sha256sum arm11/twlhd-arm11.3dsx
77e4fc0d919f330c77c74750f2e35ba942702f137fa2d2faa883a79acd84bd61  arm11/twlhd-arm11.3dsx
```

**Step 2 — stage the captured frame as `sdmc:/twlhd_frame.bin`:**

```
$ mkdir -p "$R/sd"
$ cp "$R/cap-record/twlhd_frame_v2.bin" "$R/sd/twlhd_frame.bin"
$ sha256sum "$R/sd/twlhd_frame.bin"
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  /tmp/.../sd/twlhd_frame.bin
```

`arm11/source/main.c:35` defines `FRAME_PATH` as
`"sdmc:/twlhd_frame.bin"`, and the `KEY_A` branch at
`arm11/source/main.c:579-592` calls `load_sd_frame(FRAME_PATH, …)`: A must be
held for the program to load the staged frame. `tools/azahar_host/README.md:53-61`
documents `--press A:60-63` as holding A on frames 60..63 inclusive; `--shot 199`
samples long after that window, once the render is settled.
`tools/azahar_host/azahar_host.c:496-512` recursively copies `--sd-root` into
`<root>/save/Azahar/sdmc`, so `sdmc:/twlhd_frame.bin` resolves to the staged
file whose sha256 is `1e6ef3f4…`.

In both runs the `host_exit` line is checked before any hash is computed.

**Step 3 — run 1 of the Azahar host, `--out "$R/azahar"` (the run that writes
`$R/azahar_199.ppm`):**

```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 \
    --out "$R/azahar" --sd-root "$R/sd" >"$R/az.out" 2>"$R/az.err"
$ echo "host_exit=$?"
host_exit=0
$ grep "av_info geometry" "$R/az.err"
[azahar_host] av_info geometry 400x480 @ 60.000 fps
$ grep -c "software framebuffer 400x480" "$R/az.err"
200
$ grep -m1 "software framebuffer 400x480" "$R/az.err"
[azahar_host] software framebuffer 400x480
$ ls -l "$R/azahar_199.ppm"
-rw-rw-r-- 1 <user> <group> 576015 Nov 24 00:57 /tmp/.../azahar_199.ppm
$ sha256sum "$R/azahar_199.ppm"
d4d785c5dba0436f2b187907528e12d6605afb5bd3f0b4c0a3b318951f1f4142  /tmp/.../azahar_199.ppm
```

**Run 2, a separate `--out` directory (`$R/azahar2`), before the two hashes are
compared:**

```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 \
    --out "$R/azahar2" --sd-root "$R/sd" >"$R/az2.out" 2>"$R/az2.err"
$ echo "host_exit=$?"
host_exit=0
$ grep "av_info geometry" "$R/az2.err"
[azahar_host] av_info geometry 400x480 @ 60.000 fps
$ grep -c "software framebuffer 400x480" "$R/az2.err"
200
$ ls -l "$R/azahar2_199.ppm"
-rw-rw-r-- 1 <user> <group> 576015 Nov 24 00:57 /tmp/.../azahar2_199.ppm
$ sha256sum "$R/azahar_199.ppm" "$R/azahar2_199.ppm"
d4d785c5dba0436f2b187907528e12d6605afb5bd3f0b4c0a3b318951f1f4142  /tmp/.../azahar_199.ppm
d4d785c5dba0436f2b187907528e12d6605afb5bd3f0b4c0a3b318951f1f4142  /tmp/.../azahar2_199.ppm
```

Both runs are byte-identical over the whole 400x480 buffer, so the bottom
screen (rows 240..479) matches too:

```
$ python3 - "$R" <<'PYEOF'
import sys, os, hashlib
from PIL import Image
r = sys.argv[1]
for name in ("azahar_199.ppm", "azahar2_199.ppm"):
    im = Image.open(os.path.join(r, name)).convert("RGB")
    bottom = im.crop((0, 240, 400, 480))
    top = im.crop((0, 0, 400, 240))
    print(name, "bottom400x240 sha256 %s" % hashlib.sha256(bottom.tobytes()).hexdigest())
    print(name, "top400x240 sha256 %s" % hashlib.sha256(top.tobytes()).hexdigest())
PYEOF
azahar_199.ppm bottom400x240 sha256 231896ed8886bc9acb31ad3af4c2390007bd23ae28747ce7d1cc55c11d05f0b4
azahar_199.ppm top400x240 sha256 227454cccddbc47feee337bb4473158f62e58b63cb77599e261349b293bb307f
azahar2_199.ppm bottom400x240 sha256 231896ed8886bc9acb31ad3af4c2390007bd23ae28747ce7d1cc55c11d05f0b4
azahar2_199.ppm top400x240 sha256 227454cccddbc47feee337bb4473158f62e58b63cb77599e261349b293bb307f
```

The label is corrected from `bottom240x240` to `bottom400x240`: the crop is
`(0, 240, 400, 480)`, i.e. 400x240. Measured FACT: this heredoc was re-run on
this round's PPM and the bottom hash `231896ed…` differs from REPAIR-2's
`16121b1f…`, while the top hash `227454cc…` is unchanged. HYPOTHESIS /
UNVERIFIED (SOURCE-READ `arm11/Makefile:75-84`): the bottom hash moved with the
rebuilt `.3dsx` because of the build-ID line on the bottom screen.

The full-PPM hash (`d4d785c5…`) is new this round and differs from REPAIR-2's
`6f647915…`: REPAIR-3 rebuilt the `.3dsx` at the moved work-tree HEAD
(`b775345`), and the top 400x240 crop is byte-identical to REPAIR-2's
(`227454cc…`), so the difference is confined to the bottom screen. SOURCE-READ —
`arm11/Makefile:75-84` embeds `git rev-parse --short HEAD` in
`TWLHD_BUILD_ID`, and `arm11/source/main.c:406` prints it to the bottom-screen
console. **HYPOTHESIS / UNVERIFIED:** that build-ID line changes the full-PPM
hash while the measured top screen stays the same. What is measured as FACT is
that this round's two runs agree with each other and the top crop is unchanged
(§2.3/§2.4 then measure run 1).

**History (not used): arm11 program not identified, UNVERIFIED, not used.**
The first version of this report quoted a PPM sha256 `c5549ef…` from a
pre-rebuild run. That run's `.3dsx` was never hashed, so the arm11 program
behind it is not identified; the `c5549ef…` numbers are UNVERIFIED and not used
anywhere in this revision.

**Changes versus the first version:**

- `arm11/twlhd-arm11.3dsx`: `b18cf701…` → `77e4fc0d…` (measured). HYPOTHESIS /
  UNVERIFIED (SOURCE-READ `arm11/Makefile:75-84`): the build ID embeds the
  short git hash and HEAD moved (`b775345` for this build), changing the hash.
  REPAIR-2's `f38baecb…` was the same source tree at `a4b0cd9`.
- Full 400x480 PPM: `c5549ef…` (unidentified build, UNVERIFIED, not used) /
  `a94d88…` (earlier rebuild) / `6f647915…` (REPAIR-2 at `a4b0cd9`) →
  `d4d785c5…` now, identical across this round's two runs.
- Bottom 400x240 crop: `16121b1f…` (REPAIR-2) → `231896ed…` now, and the script
  label is corrected from `bottom240x240` to `bottom400x240` (the crop is
  400x240).
- Unchanged and reproduced: frame sha256 `1e6ef3f4…`, top crop
  `227454cc…`, every §2.3 probe, every `+0.8000`/`+0.0000` offset, the
  `a=1.250000 b=1.000000` fit with zero residuals, the §2.5 resample output and
  the §2.7 row-0 probe.
- The bottom-screen build-ID sentence is relabelled HYPOTHESIS / UNVERIFIED
  and backed by the source read above instead of an unquoted diff.

### 2.3 Top-screen crop and raw probes (FACT)

Step 4 of the §2.2 sequence, run on run 1's `$R/azahar_199.ppm` (sha256
`d4d785c5…`). The 400x480 PPM's upper 400x240 rows are the top screen (same
crop as `docs/reports/eq4f-scene5-static-hw-compare.md:71-84`). Documented
one-step PIL crop and raw probe:

```
$ python3 - "$R" <<'PYEOF'
import sys, os, hashlib
from PIL import Image
r = sys.argv[1]
im = Image.open(os.path.join(r, "azahar_199.ppm")).convert("RGB")
print("ppm size %dx%d" % im.size)
top = im.crop((0, 0, 400, 240)); top.save(os.path.join(r, "top400x240.png"))
print("top400x240 sha256 %s" % hashlib.sha256(top.tobytes()).hexdigest())
px = top.load()
print("col100 rows 0..4:", [px[100, rr] for rr in range(0, 5)])
print("col100 rows 236..239:", [px[100, rr] for rr in range(236, 240)])
print("row120 cols 37..41:", [(c, px[c, 120]) for c in range(37, 42)])
print("row120 cols 355..361:", [(c, px[c, 120]) for c in range(355, 362)])
print("sq1 top edge col100 rows 37..42:", [(rr, px[100, rr]) for rr in range(37, 43)])
print("sq1 bottom edge col100 rows 78..82:", [(rr, px[100, rr]) for rr in range(78, 83)])
PYEOF
ppm size 400x480
top400x240 sha256 227454cccddbc47feee337bb4473158f62e58b63cb77599e261349b293bb307f
col100 rows 0..4: [(255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
col100 rows 236..239: [(255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
row120 cols 37..41: [(37, (16, 16, 24)), (38, (16, 16, 24)), (39, (16, 16, 24)), (40, (255, 255, 255)), (41, (255, 255, 255))]
row120 cols 355..361: [(355, (255, 255, 255)), (356, (255, 255, 255)), (357, (255, 255, 255)), (358, (255, 255, 255)), (359, (255, 255, 255)), (360, (16, 16, 24)), (361, (16, 16, 24))]
sq1 top edge col100 rows 37..42: [(37, (0, 0, 0)), (38, (0, 0, 0)), (39, (0, 0, 0)), (40, (0, 0, 0)), (41, (255, 255, 255)), (42, (255, 255, 255))]
sq1 bottom edge col100 rows 78..82: [(78, (255, 255, 255)), (79, (255, 255, 255)), (80, (255, 255, 255)), (81, (0, 0, 0)), (82, (0, 0, 0))]
```

- Vertical edges are exact: the left band begins at capture col 40 (DS 0) and
  the right band ends at col 359 (DS 255), black/clear at 360 (DS 256); the
  1-px `vline` is at capture col 300 = DS 208.
- Horizontal edges sit one capture row lower than nominal: the sq1 top is
  white from capture row 41 (nominal DS 32 is capture row 40) and its bottom
  is white through row 80 (nominal DS 64 is capture row 80); `hline` (DS 96) is
  the single capture row 121 (nominal 120).
- The clear colour outside the DS region is the arm11
  `RENDER_CLEAR_COLOR` `(16,16,24)` (`tools/eq3_compare.py:72-75`), not black,
  so the white threshold is unaffected.

### 2.4 Capture-mode measurement (FACT)

`measure_rows.py` reads that 400x240 crop with the committed mapping;
`hw` is the DS-px estimate, `hw-nominal` the signed placement error:

```
$ python3 tools/ds_edge_test/measure_rows.py "$R/top400x240.png"
mode=capture
mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
white_threshold=128.0
backdrop black level=0.0000
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=bottom nominal=16.0000 hw=16.8000 hw-nominal=+0.8000
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 hw=176.8000 hw-nominal=+0.8000
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 hw=16.0000 hw-nominal=+0.0000
edge rect=left_band kind=quad side=top nominal=80.0000 hw=80.8000 hw-nominal=+0.8000
edge rect=left_band kind=quad side=bottom nominal=112.0000 hw=112.8000 hw-nominal=+0.8000
edge rect=right_band kind=quad side=left nominal=240.0000 hw=240.0000 hw-nominal=+0.0000
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 hw=80.8000 hw-nominal=+0.8000
edge rect=right_band kind=quad side=bottom nominal=112.0000 hw=112.8000 hw-nominal=+0.8000
edge rect=sq1 kind=quad side=left nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=right nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=top nominal=32.0000 hw=32.8000 hw-nominal=+0.8000
edge rect=sq1 kind=quad side=bottom nominal=64.0000 hw=64.8000 hw-nominal=+0.8000
edge rect=sq2 kind=quad side=left nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=right nominal=192.0000 hw=192.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=top nominal=32.0000 hw=32.8000 hw-nominal=+0.8000
edge rect=sq2 kind=quad side=bottom nominal=64.0000 hw=64.8000 hw-nominal=+0.8000
edge rect=sq3 kind=quad side=left nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=right nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=top nominal=128.0000 hw=128.8000 hw-nominal=+0.8000
edge rect=sq3 kind=quad side=bottom nominal=160.0000 hw=160.8000 hw-nominal=+0.8000
edge rect=sq4 kind=quad side=left nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=right nominal=192.0000 hw=192.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=top nominal=128.0000 hw=128.8000 hw-nominal=+0.8000
edge rect=sq4 kind=quad side=bottom nominal=160.0000 hw=160.8000 hw-nominal=+0.8000
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
item=top_band row_offset=+0.8000
item=bottom_band row_offset=+0.8000
item=left_band row_offset=+0.8000
item=right_band row_offset=+0.8000
item=sq1 row_offset=+0.8000
item=sq2 row_offset=+0.8000
item=sq3 row_offset=+0.8000
item=sq4 row_offset=+0.8000
classification=uniform+1
```

Every horizontal edge is `+0.8000` DS px; every vertical edge is `+0.0000`.
`hline`/`vline` are skipped for the documented interior-plateau reason
(`tools/ds_edge_test/README.md` §5), so their rows come from the native probe
(§2.6).

### 2.5 256x192 resample (FACT; documented resampling step)

Re-run on this round's PPM: bilinear resample of the exact DS region
`x 40..360, y 0..240` back to 256x192; this is the inverse of the mapping and
is what makes `--native` usable. It smears 1-px features and moves every edge by up to half a capture
pixel (0.4 DS px), so the sub-pixel numbers are taken from §2.4, not here.

```
$ python3 - "$R" <<'PYEOF'
import sys, os
from PIL import Image
r = sys.argv[1]
im = Image.open(os.path.join(r, "azahar_199.ppm")).convert("RGB")
im.crop((40, 0, 360, 240)).resize((256, 192), Image.BILINEAR).save(
    os.path.join(r, "native256x192.ppm"))
print("ds-region crop (40,0,360,240) -> 256x192")
PYEOF
$ python3 tools/ds_edge_test/measure_rows.py --native "$R/native256x192.ppm"
mode=native
size=256x192
white_threshold=128.0
first_white_row=0
last_white_row=191
first_white_col=0
last_white_col=255
white_row_runs=0-16,33-160,177-191
white_col_runs=0-255
frame_top_row_white=1
frame_bottom_row_white=1
frame_left_col_white=1
frame_right_col_white=1
frame_vertical_shift=none
frame_horizontal_shift=none
item=top_band nominal_rows=0-15 nominal_cols=0-255 measured_rows=0-16 measured_cols=0-255 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=17
item=bottom_band nominal_rows=176-191 nominal_cols=0-255 measured_rows=177-191 measured_cols=0-255 row_diff=1 row_end_diff=0 col_diff=0 col_end_diff=0 row_count=15
item=left_band nominal_rows=80-111 nominal_cols=0-15 measured_rows=81-112 measured_cols=0-15 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=right_band nominal_rows=80-111 nominal_cols=240-255 measured_rows=81-112 measured_cols=240-255 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq1 nominal_rows=32-63 nominal_cols=32-63 measured_rows=33-64 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq2 nominal_rows=32-63 nominal_cols=160-191 measured_rows=33-64 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq3 nominal_rows=128-159 nominal_cols=32-63 measured_rows=129-160 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq4 nominal_rows=128-159 nominal_cols=160-191 measured_rows=129-160 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=hline nominal_rows=96-96 nominal_cols=64-191 measured_rows=97-97 measured_cols=64-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=1
item=vline nominal_rows=64-159 nominal_cols=208-208 measured_rows=65-160 measured_cols=208-208 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=96
classification=mixed
```

Whole-pixel 256x192 reading: the top band is 17 rows (`0..16`), every 32x32
square is `33..64` (or `129..160`), the bands are `81..112`, the bottom band is
`177..191`, and `vline` is `65..160`; that is the DeSmuME whole-pixel pattern
(§1.5) except `hline` (one resampled row at 97; the capture probe puts the
unresampled line at row 121 = DS 96.8) and the missing second row of the
DeSmuME 1-px line.

### 2.6 Horizontal/vertical placement fit (FACT)

Re-run on this round's crop. Using the raw white-run boundaries at col 100
(`[(0, 20), (41, 80), (161, 200), (221, 239)]`) against the nominal DS edges
(16, 32, 64, 128, 160, 176):

```
$ python3 - "$R" <<'PYEOF'
import sys, os
from PIL import Image
r = sys.argv[1]
im = Image.open(os.path.join(r, "top400x240.png")).convert("RGB"); px = im.load()
flags = [((px[100, y][0] + px[100, y][1] + px[100, y][2]) / 3.0) >= 128 for y in range(240)]
runs = []; s = None
for i, f in enumerate(flags):
    if f and s is None: s = i
    elif not f and s is not None: runs.append((s, i - 1)); s = None
if s is not None: runs.append((s, 239))
print("col100 white row runs:", runs)
ds = [16, 32, 64, 128, 160, 176]
cap = [21.0, 41.0, 81.0, 161.0, 201.0, 221.0]
n = len(ds); sx = sum(ds); sy = sum(cap); sxx = sum(d * d for d in ds)
sxy = sum(d * c for d, c in zip(ds, cap))
a = (n * sxy - sx * sy) / (n * sxx - sx * sx); b = (sy - a * sx) / n
print("fit capture_edge = a*DS + b: a=%.6f b=%.6f" % (a, b))
print("residuals:", ["%+.3f" % (c - (a * d + b)) for d, c in zip(ds, cap)])
PYEOF
col100 white row runs: [(0, 20), (41, 80), (161, 200), (221, 239)]
fit capture_edge = a*DS + b: a=1.250000 b=1.000000
residuals: ['+0.000', '+0.000', '+0.000', '+0.000', '+0.000', '+0.000']
```

So horizontal edges follow `capture_y = 1.25*y_ds + 1.0` exactly (0.8 DS px
below nominal). The same fit for vertical edges from the raw runs — left band
`40..59` (DS 0..15), right band `340..359` (DS 240..255) — is
`capture_x = 40 + 1.25*x_ds` (offset 40, no extra pixel). Pixels alone cannot
separate a rigid one-pixel down-shift of the geometry from a rasteriser rule
that paints only the lower boundary row; both give this mapping (§2.7, §5.2).

### 2.7 The row-0 artefact (FACT / UNVERIFIED cause)

Re-run on this round's PPM: row 0 of the top-screen crop is not a normal
scanline; it equals row 239 shifted left by one pixel.

```
$ python3 - "$R" <<'PYEOF'
import sys, os
from PIL import Image
r = sys.argv[1]
im = Image.open(os.path.join(r, "azahar_199.ppm")).convert("RGB"); px = im.load()
print("row0[x]==row239[x+1] for x=0..398:",
      all(px[x, 0] == px[x + 1, 239] for x in range(399)))
print("row0 cols 36..41:", [px[x, 0] for x in range(36, 42)])
print("row239 cols 36..41:", [px[x, 239] for x in range(36, 42)])
print("row0 col399:", px[399, 0], "row1 col399:", px[399, 1])
PYEOF
row0[x]==row239[x+1] for x=0..398: True
row0 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
row239 cols 36..41: [(16, 16, 24), (16, 16, 24), (16, 16, 24), (16, 16, 24), (255, 255, 255), (255, 255, 255)]
row0 col399: (0, 0, 0) row1 col399: (16, 16, 24)
```

The cause (a libretro/core row-copy or output-rotation edge effect) is
**UNVERIFIED**. It is one row at the very top and is excluded from the
geometry fit: the top band's real span is rows 1..20 (20 capture rows = 16 DS
rows), i.e. the same 16-row top band as hardware, uniformly placed at
`1.25*y + 1.0`. `measure_rows` skips the top/bottom screen-boundary edges, so
its per-edge numbers are unaffected.

### 2.8 1-px lines (FACT)

Raw probes put the unresampled 1-px `hline` at capture row 121 (nominal DS 96 is
capture row 120) and the 1-px `vline` at capture col 300 (DS 208 = capture col
300, exact in x). At 1.25x neither is a DS-px measurement: `hline` covers one
capture row, `vline` one capture column. The bilinear 256x192 resample smears
them (`hline` one row at 97, `vline` `65..160`), so no sub-pixel claim is made
for the lines.

## 3. REQ 3 — independent source read

### 3.1 Our transform has no viewport quantisation (SOURCE-READ)

SOURCE-READ — `common/twlhd_gx_interp.c:243-256` composes the 20.12 matrices
with a floor division, and `:540-559` emits DS clip space from a double
accumulate with a float result and no rounding or ceil:

```
static void recompute_clip(GxInterp *g) {
    ...
            g->clip.m[col * 4 + row] = fx12_floor_div(acc);
...
static void transform_vertex(GxInterp *g, float x, float y, float z,
                             TwlhdGxVertex *out) {
    ...
    v[0] = (double)x;
    v[1] = (double)y;
    v[2] = (double)z;
    v[3] = 1.0;

    for (row = 0; row < 4u; row++) {
        double acc = 0.0;
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            acc += (double)g->clip.m[k * 4 + row] * v[k];
        }
        out->clip[row] = (float)(acc / 4096.0);
    }
```

`fx12_floor_div` is `(int32_t)((int64_t)v >> 12)` (`twlhd_gx_interp.c:118-123`).
The only fractional error before the GPU is therefore 1/4096 of a clip unit
(0.0234 DS px worst case, §1.5) — no `+1` row and no DS `Ceil16_16`.

SOURCE-READ — `common/twlhd_clipfix.c:32-42` maps DS clip space to PICA clip
space using the recorded viewport word (quoted in §2.1): `hw=hh=1`, `cx=cy=0`
for `glViewport(0,0,255,191)`, `x' = y_ds`, `y' = -0.8*x_ds`, and the 0.8
pillar-box factor. The arm11 program applies it as the vertex-shader uniform:

```
arm11/source/renderer.c:970        twlhd_clipfix_matrix(viewport, g_renderer.clipfix_pending);
arm11/source/renderer.c:1579-1584  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_clipfix, &m4);
arm11/shaders/twlhd_clip_vshader.v.pica:23-26  dp4 outpos.x/y/z/w, clipfix[..], inpos
```

### 3.2 PICA software-rasteriser convention (SOURCE-READ)

The Azahar tree is present at `$HOME/3ds/azahar`. SOURCE-READ —
`src/video_core/renderer_software/sw_rasterizer.cpp:208-227` builds screen
coordinates through the f24 viewport, then `:235-238` converts them to
`Fix12P4` (12.4 fixed point, 1/16 px):

```
    vtx.screenpos[0] = (vtx.pos.x * inv_w + f24::One()) * viewport.halfsize_x + viewport.offset_x;
    vtx.screenpos[1] = (vtx.pos.y * inv_w + f24::One()) * viewport.halfsize_y + viewport.offset_y;
...
        return Common::Vec3{Fix12P4::FromFloat24(vec.x), Fix12P4::FromFloat24(vec.y),
                            Fix12P4::FromFloat24(vec.z)};
```

and `:304-326` rasterises at half-pixel centres with the PICA top-left bias:

```
    for (u16 y = min_y + 8; y < max_y; y += 0x10) {
        ...
            for (u16 x = min_x + 8; x < max_x; x += 0x10) {
                ...
                const s32 w0 = bias0 + SignedArea(vtxpos[1].xy(), vtxpos[2].xy(), {x, y});
                ...
                if (w0 < 0 || w1 < 0 || w2 < 0) {
                    continue;
                }
```

(`bias <n>` is `-1` for a right-side or flat-bottom edge, `:291-295`.)
So the rasteriser tests pixel centres and applies a half-open top-left fill
rule at the 400x240 PICA grid. One PICA pixel is 0.8 DS px here, so an exact
DS edge is representable only to 0.8 DS px, and one side of each edge is
included by the bias.

### 3.3 Prediction

The transform itself predicts the **hardware** rule: it sends the DS
half-open vertex rectangle into PICA clip space with no `ceil` and no DS-row
quantisation (only the 1/4096 matrix floor, 0.0234 DS px). The PICA rasteriser
then quantises to its own 400x240 grid (0.8 DS px) with a top-left fill rule, so
a DS-exact half-open edge is not preserved at sub-DS precision. The measured
`+1.0 capture px` placement (§2.6) is consistent with that PICA/display grid;
its exact split between the rasteriser fill rule and the arm11 -> Azahar
rotation/row-copy is **HYPOTHESIS / UNVERIFIED**. What the source read does
exclude is a DeSmuME-style `Ceil16_16` on the DS y: our interpreter has no such
rounding, and the harness cross-check (§1.5) agrees to 0.0234 DS px.

## 4. Tests (REQ 4)

Only the recorder variant and its static test were added; `measure_rows.py`
and `tools/ds_alpha_test/measure_edges.py` are unmodified. `test_record.py`
pins the variant's wiring (shim before `<nds.h>`, recorder calls, the
`/twlhd_frame_v2.bin` dump, `-lnds9 -lfat`, `../../common`) and keeps the plain
ROM recorder-free, with a negative control that removes `twlhd_gx_rec_arm(rec)`
and requires the check to notice:

```
$ make test-ds-edge-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
..................................
----------------------------------------------------------------------
Ran 34 tests in 0.714s

OK
```

The suite grew from 28 (EQ-4l) to 34 tests; no existing test was weakened or
skipped. A `tools/ds_edge_test/README` sentence for `make record` is the only
doc change under `tools/` besides the new files.

## 5. Answer — which of the three rules

### 5.1 Three-column comparison

Hardware numbers are quoted from `docs/reports/eq4l-edge-rows-hw-result.md` §3
(this round does not re-run hardware); DeSmuME numbers are the EMULATOR control
of §1.5 (identical to `docs/reports/eq4k-edge-rows.md` §3); ours is §2.4/§2.5.

| measurement | hardware (EQ-4l) | DeSmuME (EQ-4k) | ours (this round) |
|---|---|---|---|
| top band | 16 rows from capture row 0; bottom edge `15.9241` | `0..16`, 17 rows | capture rows 1..20 (20 capture = 16 DS rows), bottom `16.8000` |
| bottom band | top edge `175.9241`, reaches last capture row | `177..191`, top `+1` | top `176.8000`, clipped at the 240-row top screen |
| left/right bands (y) | top `79.9241`, bottom `111.9241` | `81..112` | `80.8000..112.8000` |
| sq1 | `31.9242..63.9240` | `33..64` | `32.8000..64.8000` |
| sq2 | `31.9242..63.9240` (x `159.9242..191.9240`) | `33..64` | `32.8000..64.8000` |
| sq3 | `127.9242..159.9240` | `129..160` | `128.8000..160.8000` |
| sq4 | `127.9242..159.9240` | `129..160` | `128.8000..160.8000` |
| horizontal edges | nominal within `~0.08` | exactly `+1.0000` DS | exactly `+0.8000` DS = `+1` capture px |
| vertical edges | nominal within `~0.08` | nominal | nominal (`+0.0000`) |
| hline (1 px) | UNMEASURED | `96..97`, 2 rows | capture row 121 (nominal 120); resampled one row at 97 |
| vline (1 px) | UNMEASURED | `65..160` | x exact (capture col 300); resampled `65..160` |
| `classification=` | `none` | `mixed` | `uniform+1` (capture) / `mixed` (resampled) |

### 5.2 Verdict

**Our render follows a third rule at DS measurement scale.** The painted
horizontal placement is `capture_y = 1.25*y_ds + 1.0` with zero residuals over
six interior edges; vertical placement is `40 + 1.25*x_ds` with no residual.
This is a uniform one-capture-pixel (0.8 DS) down-shift of the painted
horizontal boundaries, not a per-edge `+1` DS row: the top band covers 16 DS
rows and the squares 32 DS rows, and the DS clip geometry is exact up to
0.0234 DS px. Consequently:

- it is **not** the hardware rule *as rendered* (hardware edges are at
  `1.25*y` with `-0.076` DS residuals, EQ-4l),
- it is **not** DeSmuME's `Ceil16_16` rule (`+1.0000` DS per horizontal edge,
  EQ-4k),
- it is a **third rule**: hardware-like half-open edge lengths plus a
  one-PICA-pixel (0.8 DS) downward placement of every painted horizontal
  boundary in the arm11/Azahar output. At 256x192 whole-pixel resolution the
  offset rounds to `+1` and the output is indistinguishable from DeSmuME except
  for the 1-px lines.

**UNVERIFIED** decomposition: pixels alone cannot tell a rigid one-pixel
down-shift of the geometry from a rasteriser rule that paints only the lower
boundary row. The PICA rasteriser works on the 400x240 grid with half-pixel
centres and a top-left bias (`sw_rasterizer.cpp:304-326`), and the 240x400
rotated target / row copy could place the 320x240 DS region one target row low;
which of the two produces the measured mapping is not decided here.

## 6. Measurement uncertainty

- **Grid:** the capture grid is 0.8 DS px (one 400x240 pixel). Edges are binary
  (clear `(16,16,24)` vs white `(255,255,255)`, no antialiasing), so each
  measured boundary is exact to one capture pixel; the fit residuals are
  `+0.000` capture px. The reported `+0.8000` is therefore a one-pixel grid
  step, not a sub-pixel interpolation: the geometric edge lies within the
  neighbouring capture row, i.e. **±0.4 DS px**.
- **Mapping:** committed `scale 1.25, offset (40,0)`; the independent EQ-4g fit
  differs by at most 0.135 DS px across the screen
  (`docs/reports/eq4g-capture-offset.md`), and hardware edges themselves sit
  within about 0.08 DS px of nominal (EQ-4l).
- **Excluded:** row 0 (one-pixel-shifted copy of row 239, §2.7); 1-px lines are
  not sub-pixel measurable at 1.25x (§2.8).

## 7. Recommendation (nothing changed here)

- `common/twlhd_gx_interp.c` and `common/twlhd_clipfix.c` already implement the
  hardware `[y0,y1)` rule: no `Ceil16_16`, no DS-row rounding, 0.0234 DS px
  worst-case. No change to `common/` is indicated by this round.
- The measured 0.8 DS downward placement of the painted horizontal boundaries
  belongs to the PICA/Azahar presentation of the 3D image (the painted output
  is one capture row low: half-open `1..241` instead of `0..240`; whether that
  is a framing offset or the fill rule is UNVERIFIED). If matching hardware's
  *measured* rows matters for a future comparison, that offset would have to be
  accounted for in the arm11 viewport / output rotation or the host mapping —
  **not** by adding a `+1`/`-1` to the DS geometry, and **not** by changing the
  GX interpreter. Any such change needs its own hardware validation; it is not
  made here.
- The row-0 artefact (§2.7) should be understood before any future host-based
  per-row claim; it is one row and does not affect §2.4-§2.6.

**Update (2026-11-24):** EQ-4n resolved the §3.3/§5.2 UNVERIFIED split: the
one-capture-row (`+0.8000` DS) shift and the row-0 artefact are the Azahar
libretro core's software presentation — `renderer_software.cpp` reads the top
framebuffer with a one-column error (`stride-x`, i.e. columns 240..1 instead
of 239..0) and `libretro_window.cpp` maps that column axis to the 400x240 PPM
rows; `tools/azahar_host` itself copies rows verbatim. The arm11 target,
viewport and clipfix have no one-row term, and the PICA top-left rule accounts
only for the integer-grid `ceil`. See `docs/reports/eq4n-arm11-row-shift.md`.

## 8. House rules

- New files: `tools/ds_edge_test/record/main.cpp`,
  `tools/ds_edge_test/test_record.py`; modified: `tools/ds_edge_test/Makefile`
  (adds the `record` target; the plain target is unchanged). No production
  file was touched.
- Diff budget over the whole change set, this report included:

```
$ git diff main | wc -c
76508
```

  76508 bytes, below the 90000-byte budget. This REPAIR-4 round changes only
  the three documentation files (this report and the two append-only
  pointers); the code/test files in the `main` diff are the earlier EQ-4m
  additions, untouched here. REPAIR-3-quoted numbers belong to that sequence at
  HEAD `b775345`; later documentation-only commits change the embedded build ID
  and thereby the full-PPM/bottom-crop hashes (HYPOTHESIS / UNVERIFIED,
  SOURCE-READ `arm11/Makefile:75-84`, `arm11/source/main.c:406`).
- Privacy: every command uses `$HOME` / `$R`; `ls -l` output is quoted with
  `<user> <group>`.

```
$ grep -rn -E '/(home|Users)/' tools/ds_edge_test/record tools/ds_edge_test/test_record.py tools/ds_edge_test/Makefile docs/reports/eq4m-pica-edge-rows.md; echo "grep_exit=$?"
grep_exit=1
```

## 9. Requirement traceability

- REQ 1: §1 — plain ROM capture fails at the DLDI gate (`capture_exit=1`,
  `grep -c 'AUTO-PATCHING DLDI'` = 0, log has only `Slot 2: MPCF Flash Card
  Device`); exact missing piece named (ROM has no recorder/DLDI); recorder
  variant under `tools/ds_edge_test/` then captures (`capture_exit=0`,
  `AUTO-PATCHING DLDI` line 14, 1392-byte frame, sha `1e6ef3f4…`).
- REQ 2: §2 — crop + raw probes, full capture-mode output (every horizontal
  edge `+0.8000`, every vertical `+0.0000`), full 256x192 resample output,
  the `capture_y = 1.25*y + 1.0` fit with zero residuals, and the 1-px-line
  limits.
- REQ 3: §3 — `twlhd_gx_interp.c:243-256,540-559`, `twlhd_clipfix.c:32-42`,
  `renderer.c:970,1579-1584`, the shader `dp4`, and Azahar
  `sw_rasterizer.cpp:208-227,235-238,291-326`; prediction stated in §3.3.
- REQ 4: §4 — `make test-ds-edge-test`, 34 tests, OK; negative control in
  `test_record.py`.
- REQ 5: this report; append-only pointers in
  `docs/EQUALITY_CHECK_DESIGN.md` and `docs/EQ4_HW_COMPARE.md`.
