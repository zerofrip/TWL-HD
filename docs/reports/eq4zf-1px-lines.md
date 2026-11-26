<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4zf — the 1-px `hline`/`vline` fill rule: hardware vs DeSmuME vs our render

**Status:** OFFLINE ANALYSIS of committed data, one DeSmuME harness run and two Azahar-host runs (§3); no console access; no production source change (`arm9v2/`, `arm11/`, `common/`, `arm7/`, `tools/azahar_host/` untouched). Experiment 1 of `docs/reports/eq4ze-hw-coverage.md` §4; one DS capture session (EQ-4l), one DeSmuME run, one PICA session (EQ-4o). Labels: **FACT** = full command + raw output; **SOURCE-READ** = quoted file/line or report number; **EMULATOR** = DeSmuME/Azahar; **HYPOTHESIS**/**UNVERIFIED** = not measured/inferred.

**Question.** How does hardware fill the ROM's `hline` (DS `64,96,128x1`) and `vline` (`208,64,1x96`) (`quads.json:29,31`) in capture and DS pixels, versus DeSmuME and our arm11/PICA render?

**Answer.** Hardware: each line is **exactly one capture row/column** at the nominal position (`hline` row 120 = DS 96.0, `vline` col 300 = DS 208.0), peak 255, neighbours ≤ 50. Azahar and the EQ-4o PICA hardware match the run/position result (side profile 0.0). DeSmuME differs: `hline` is **two native rows** (`96..97`) and `vline` sits one DS row low (`65..160`). Hardware and our render **pass the eq4ze §4 criterion within the 1.25x grid**; DeSmuME fails the one-row thickness condition (exact DS thickness is **inferred**: one capture pixel = 0.8 DS px).

## 1. Hardware arm (FACT; measured on the EQ-4l fixture)

New opt-in `--row-runs` in `tools/ds_edge_test/measure_rows.py`; default output is byte-identical without it (§7). One console, one capture session, fixture `tools/ds_edge_test/fixtures/ed1_3_top400x240.png` (EQ-4l):

```
$ python3 tools/ds_edge_test/measure_rows.py --row-runs tools/ds_edge_test/fixtures/ed1_3_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
presentation-row correction: not applied (no row-0 wrap signature)
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
edge rect=top_band kind=quad side=bottom nominal=16.0000 hw=15.9241 hw-nominal=-0.0759
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 hw=175.9241 hw-nominal=-0.0759
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 hw=15.9240 hw-nominal=-0.0760
edge rect=left_band kind=quad side=top nominal=80.0000 hw=79.9241 hw-nominal=-0.0759
edge rect=left_band kind=quad side=bottom nominal=112.0000 hw=111.9241 hw-nominal=-0.0759
edge rect=right_band kind=quad side=left nominal=240.0000 hw=239.9242 hw-nominal=-0.0758
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 hw=79.9242 hw-nominal=-0.0758
edge rect=right_band kind=quad side=bottom nominal=112.0000 hw=111.9239 hw-nominal=-0.0761
edge rect=sq1 kind=quad side=left nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq1 kind=quad side=right nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq1 kind=quad side=top nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq1 kind=quad side=bottom nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq2 kind=quad side=left nominal=160.0000 hw=159.9242 hw-nominal=-0.0758
edge rect=sq2 kind=quad side=right nominal=192.0000 hw=191.9240 hw-nominal=-0.0760
edge rect=sq2 kind=quad side=top nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq2 kind=quad side=bottom nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq3 kind=quad side=left nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq3 kind=quad side=right nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq3 kind=quad side=top nominal=128.0000 hw=127.9242 hw-nominal=-0.0758
edge rect=sq3 kind=quad side=bottom nominal=160.0000 hw=159.9240 hw-nominal=-0.0760
edge rect=sq4 kind=quad side=left nominal=160.0000 hw=159.9242 hw-nominal=-0.0758
edge rect=sq4 kind=quad side=right nominal=192.0000 hw=191.9240 hw-nominal=-0.0760
edge rect=sq4 kind=quad side=top nominal=128.0000 hw=127.9242 hw-nominal=-0.0758
edge rect=sq4 kind=quad side=bottom nominal=160.0000 hw=159.9240 hw-nominal=-0.0760
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
item=top_band row_offset=-0.0759
item=bottom_band row_offset=-0.0759
item=left_band row_offset=-0.0759
item=right_band row_offset=-0.0758
item=sq1 row_offset=-0.0758
item=sq2 row_offset=-0.0758
item=sq3 row_offset=-0.0758
item=sq4 row_offset=-0.0758
classification=none
row-runs=on
row-runs mapping_scale_x=1.25 mapping_scale_y=1.25 mapping_offset_x=40 mapping_offset_y=0
row-runs note=positions_and_lengths_measured; *_ds_values_inferred_by_dividing_by_the_mapping_scale; only_rows_or_columns_with_a_white_run_are_listed_individually; the_full_neighbourhood_is_in_the_profile_line
row-runs item=hline axis=row nominal_ds=96.0000 nominal_capture=120.0000 search_capture=117-123 extent_capture=120-279
row-runs item=hline capture_row=120 ds=96.0000 white_runs=120-279 run_start=120 run_length=160 run_start_ds=64.0000 run_length_ds=128.0000 peak_luminance=255.0
row-runs item=hline rows_covered=1 row_run=120-120 peak_luminance=255.0
row-runs item=hline profile_capture=115-125 profile_luminance=0.0,0.0,4.7,0.0,50.0,254.7,50.0,0.0,4.7,0.0,0.0
row-runs item=vline axis=col nominal_ds=208.0000 nominal_capture=300.0000 search_capture=297-303 extent_capture=80-199
row-runs item=vline capture_col=300 ds=208.0000 white_runs=80-199 run_start=80 run_length=120 run_start_ds=64.0000 run_length_ds=96.0000 peak_luminance=255.0
row-runs item=vline cols_covered=1 col_run=300-300 peak_luminance=255.0
row-runs item=vline profile_capture=295-305 profile_luminance=0.0,0.0,4.7,0.0,50.0,254.6,50.0,0.0,4.7,0.0,0.0
```

Measured (FACT): one capture row/column each (120, 300), runs `120..279` (160) and `80..199` (120), peak 255, neighbours 50. Inferred from the mapping: DS 96.0/208.0, starts 64.0, lengths 128.0/96.0, one capture row/column = 0.8 DS px, so "one DS row" is inferred, not a native scan.

## 2. DeSmuME arm (FACT / EMULATOR; harness run 1, plain ROM build 1)

No DeSmuME image was committed for this ROM (`fixtures/` held only the hardware capture); eq4k §2 records the clean rebuild and §3 the bare `harness` command, so one run is sanctioned. SOURCE-READ, `docs/reports/eq4k-edge-rows.md` §2:76 (ROM) and §3:97 (PPM run):

```
$ sed -n '76p;97p' docs/reports/eq4k-edge-rows.md > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
3d417c162d5067f1febf7f119182ede6fd8a88e56c28c30853b67857fd0a2706  tools/ds_edge_test/ds-edge-test.nds
d653c89e1cd676090b0a4f1b2633db30804f60637d7bbf0d595d31d340ed309c  /tmp/tmp.../h.ppm
```

Measured here (FACT), the same full values:

```
$ sha256sum tools/ds_edge_test/ds-edge-test.nds > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
3d417c162d5067f1febf7f119182ede6fd8a88e56c28c30853b67857fd0a2706  tools/ds_edge_test/ds-edge-test.nds
```

`harness` below is the out-of-tree DeSmuME harness recorded in eq4k §3 (path
not shown), invoked by bare name with the eq4k §3 command shape:

```
$ harness tools/ds_edge_test/ds-edge-test.nds 1 --out <logfile>.txt --ppm <logfile>.ppm > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 1: 11 polygons, 44 vertices (first non-empty 3D frame: 1)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 26347E7B
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ sha256sum <logfile>.ppm > <logfile>.sha 2>&1; echo "exit=$?"; cat <logfile>.sha
exit=0
d653c89e1cd676090b0a4f1b2633db30804f60637d7bbf0d595d31d340ed309c  <logfile>.ppm
```

The PPM is committed as `tools/ds_edge_test/fixtures/ed1_3_desmume_native256x192.png` (483 bytes,
PNG, < 100 KB; the PPM sha above is eq4k §3:97, quoted above). Probe on the committed native PNG
(identity mapping, DS 256x192, scale 1):

```
$ python3 tools/ds_edge_test/measure_rows.py --native --row-runs tools/ds_edge_test/fixtures/ed1_3_desmume_native256x192.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
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
row-runs=on
row-runs mapping_scale_x=1 mapping_scale_y=1 mapping_offset_x=0 mapping_offset_y=0
row-runs note=positions_and_lengths_measured; *_ds_values_inferred_by_dividing_by_the_mapping_scale; only_rows_or_columns_with_a_white_run_are_listed_individually; the_full_neighbourhood_is_in_the_profile_line
row-runs item=hline axis=row nominal_ds=96.0000 nominal_capture=96.0000 search_capture=94-98 extent_capture=64-191
row-runs item=hline capture_row=96 ds=96.0000 white_runs=64-191 run_start=64 run_length=128 run_start_ds=64.0000 run_length_ds=128.0000 peak_luminance=255.0
row-runs item=hline capture_row=97 ds=97.0000 white_runs=64-191 run_start=64 run_length=128 run_start_ds=64.0000 run_length_ds=128.0000 peak_luminance=255.0
row-runs item=hline rows_covered=2 row_run=96-97 peak_luminance=255.0
row-runs item=hline profile_capture=91-101 profile_luminance=0.0,0.0,0.0,0.0,0.0,255.0,255.0,0.0,0.0,0.0,0.0
row-runs item=vline axis=col nominal_ds=208.0000 nominal_capture=208.0000 search_capture=206-210 extent_capture=64-159
row-runs item=vline capture_col=208 ds=208.0000 white_runs=65-159 run_start=65 run_length=95 run_start_ds=65.0000 run_length_ds=95.0000 peak_luminance=255.0
row-runs item=vline cols_covered=1 col_run=208-208 peak_luminance=255.0
row-runs item=vline profile_capture=203-213 profile_luminance=0.0,0.0,0.0,0.0,0.0,252.3,0.0,0.0,0.0,0.0,0.0
```

The committed PNG reproduces the eq4k §3 PPM-probe lines exactly (the unchanged item lines are
SOURCE-READ from eq4k §3), so it provably holds the probed pixels. `hline` covers two native rows,
each a 128-px run; `vline` is one column whose run starts at row 65 inside the nominal extent
(95 px), full line `65..160` — a real 2-row/+1-row difference.

## 3. Our render arm (EMULATOR; FACT on complete blocks, excerpts labelled)

The eq4m §2.2 chain is quoted below in the order run; that sequence supplies every §3/§5/§6
number. Its top crop is `c8e639ce…` (block 6). Tree state, taken at this HEAD:

```
$ { git rev-parse HEAD; git status --porcelain -- arm11 common arm7 arm9v2; git diff 238b25e HEAD -- arm11 common arm7 arm9v2; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
238b25e642bec591f787850b3239912a31083ddd
```

The empty `status`/`diff` outputs are the clean production tree (`*.nds`/`*.3dsx` gitignored).
Step 1, the recorder ROM:

```
$ make -C tools/ds_edge_test record > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
make: Entering directory '<worktree>/tools/ds_edge_test'
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
make: Leaving directory '<worktree>/tools/ds_edge_test'
```

EXCERPT (not FACT) above: `twlhd_v2_parse.c` emits two `-Wmaybe-uninitialized` warning blocks,
2×24 lines (`texel_bytes`, `index_bytes`), elided with this count; the same blocks repeat in the
arm11 build below.

```
$ sha256sum tools/ds_edge_test/ds-edge-record.nds > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
bec5cf280e98c68c3a9109c57ce196a70ad7afd2c19e940e3a95d810586fd4ef  tools/ds_edge_test/ds-edge-record.nds
```

Step 2, capture (`DESMUME_CLI` set to the desmume-cli of eq4m §1.4, path not shown). The script's
own `set -x` trace is the full output:

```
$ DESMUME_TIMEOUT=25 tools/emulator/desmume_capture.sh tools/ds_edge_test/ds-edge-record.nds <logfile>.cap-record > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ mkdir -p <logfile>.cap-record
+ image=<logfile>.cap-record/cflash.img
+ log=<logfile>.cap-record/desmume.log
+ fatdir=<logfile>.cap-record/fat
+ rm -f <logfile>.cap-record/cflash.img <logfile>.cap-record/desmume.log <logfile>.cap-record/twlhd_frame_v2.bin
+ rm -rf <logfile>.cap-record/fat
+ mkfs.fat -C <logfile>.cap-record/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=2698058
+ sleeper_pid=2698059
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 25
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 2698058 2698059
+ first_status=0
+ set -e
+ kill -0 2698058
+ kill -0 2698059
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 2698058
+ grace_pid=2699801
+ set +e
+ wait -n 2698058 2699801
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 2698058
+ kill -0 2699801
+ kill -TERM 2699801
+ wait 2699801
+ true
+ grace_pid=
+ set +e
+ wait 2698058
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' <logfile>.cap-record/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' <logfile>.cap-record/desmume.log
+ 7z x -y <logfile>.cap-record/cflash.img -o<logfile>.cap-record/fat
+ '[' '!' -f <logfile>.cap-record/fat/twlhd_frame_v2.bin ']'
+ cp <logfile>.cap-record/fat/twlhd_frame_v2.bin <logfile>.cap-record/twlhd_frame_v2.bin
+ echo 'capture: <logfile>.cap-record/twlhd_frame_v2.bin'
capture: <logfile>.cap-record/twlhd_frame_v2.bin
```

```
$ sha256sum <logfile>.cap-record/twlhd_frame_v2.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  <logfile>.cap-record/twlhd_frame_v2.bin
```

Step 3, the arm11 build is an EXCERPT (not FACT): 2×24 warning lines omitted per the disclosure above; the `clean ...` lines were printed to the terminal by the unredirected `make -C arm11 clean`, not by `cat <logfile>`:

```
$ make -C arm11 clean && make -C arm11 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
make: Entering directory '<worktree>/arm11'
clean ...
make: Leaving directory '<worktree>/arm11'
make: Entering directory '<worktree>/arm11'
twlhd_clip_tex_vshader.v.pica
twlhd_clip_tex_vshader.shbin
twlhd_clip_vshader.v.pica
twlhd_clip_vshader.shbin
twlhd_vshader.v.pica
twlhd_vshader.shbin
main.c
renderer.c
rtcom_transport.c
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
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make: Leaving directory '<worktree>/arm11'
```

```
$ sha256sum arm11/twlhd-arm11.3dsx > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
821ada8e56d2501de87002833c97bb7d5513f6269a804ce054996f8669c0c155  arm11/twlhd-arm11.3dsx
```

The `.3dsx` and full-frame pixel hashes differ from eq4m because the build ID embeds the short HEAD
(`arm11/Makefile:75-84`); the top crop is unchanged (block 6).

Step 4, stage the frame and run the host (`make azahar-host`). `azahar_libretro.so` below is the out-of-tree Azahar core recorded in eq4m §2.2 step 3, invoked by bare file name (path not shown, see eq4m §2.2 step 3):

```
$ mkdir -p <logfile>.sd && cp <logfile>.cap-record/twlhd_frame_v2.bin <logfile>.sd/twlhd_frame.bin && sha256sum <logfile>.sd/twlhd_frame.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
1e6ef3f464738f29ba695d9373271540ed70ff0b041f0a654d0d0806c9b10dc5  <logfile>.sd/twlhd_frame.bin
$ tools/azahar_host/build/azahar_host --core azahar_libretro.so --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <logfile>.azahar --sd-root <logfile>.sd > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
[azahar_host] isolated environment: <logfile>.tmp
[azahar_host] save dir <logfile>.tmp/save; system dir <logfile>.tmp/system
[azahar_host] staged --sd-root <logfile>.sd as sdmc:/ (<logfile>.tmp/save/Azahar/sdmc)
[azahar_host] core option definitions accepted
[azahar_host] core API version 1
[azahar_host] core Azahar 9c38437
[azahar_host] save directory: <logfile>.tmp/save
[azahar_host] geometry 400x480
[azahar_host] pixel format XRGB8888 accepted
[azahar_host] geometry 400x480
[azahar_host] core option values accepted (10 lookups answered)
[azahar_host] av_info geometry 400x480 @ 60.000 fps
[azahar_host] wrote <logfile>.azahar_199.ppm (400x480)
[azahar_host] wrote <logfile>.azahar_last.ppm (400x480)
```

EXCERPT (not FACT): the `azahar_host` output above shows 14 of its 463 log lines (log not in the repo; exact positions not recorded). Composition: 24 distinct host lines (14 quoted; 10 core-option lines folded, first `use_cpu_jit=disabled`, last `use_libretro_save_path=LibRetro Default`), 200 identical `[azahar_host] software framebuffer 400x480` lines, 239 timestamped `[azahar_libretro]` core-log lines; the 449 omitted are counted, not quoted, every decision-bearing line above.

```
$ sha256sum <logfile>.azahar_199.ppm > <logfile>.ppm.sha 2>&1; echo "exit=$?"; cat <logfile>.ppm.sha
exit=0
9d0de7aefc2046ad44dbf1147121e2cf1d9a3d98b067d2034f497d836be584ce  <logfile>.azahar_199.ppm
```

Step 5, the documented eq4m §2.3 PIL crop `(0,0,400,240)`, on `<logfile>.azahar_199.ppm` (the
step-4 PPM, re-checked `9d0de7ae…` as quoted above), writing the `<logfile>.top400x240.png` that
step 6 reads. The script, written at `<logfile>.crop.py`, is SOURCE-READ below:

```
$ python3 -I <logfile>.crop.py <logfile> > <logfile>.crop 2>&1; echo "exit=$?"; cat <logfile>.crop
exit=0
ppm size 400x480
full frame pixel sha256 fdcfea32a31508ea49de301383114e4fa391f51714fa9741855969e9a2c758cf
top400x240 sha256 c8e639ce3b78b3e9a6f4b26465156bcf88525d7d8065a3b10c1fc0069ebc21cf
```

```
import sys, os, hashlib
from PIL import Image
r = sys.argv[1]
im = Image.open(r + ".azahar_199.ppm").convert("RGB")
print("ppm size %dx%d" % im.size)
print("full frame pixel sha256 %s" % hashlib.sha256(im.tobytes()).hexdigest())
top = im.crop((0, 0, 400, 240)); top.save(r + ".top400x240.png")
print("top400x240 sha256 %s" % hashlib.sha256(top.tobytes()).hexdigest())
```

`top400x240 sha256 c8e639ce…` is the pixel sha of the crop's array; the PNG file it saved hashes
to `f3fdc7d9…` (step 6 block) — the file step 6 probes.

Step 6, the probe of that PNG with the mandatory presentation-row correction. EXCERPT (not FACT) of
its output, lines omitted: `edge rect=` (44), `item=` (8); every `row-runs` line is quoted:

```
$ sha256sum <logfile>.top400x240.png > <logfile>.png.sha 2>&1; echo "exit=$?"; cat <logfile>.png.sha
exit=0
f3fdc7d99eacc44ebda6a329c7caea18b2dd218a67ae5d11932e1f723b1da867  <logfile>.top400x240.png
$ python3 tools/ds_edge_test/measure_rows.py --row-runs --azahar-correction on <logfile>.top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
presentation-row correction: applied (--azahar-correction on (forced))
classification=none
row-runs=on
row-runs mapping_scale_x=1.25 mapping_scale_y=1.25 mapping_offset_x=40 mapping_offset_y=0
row-runs note=positions_and_lengths_measured; *_ds_values_inferred_by_dividing_by_the_mapping_scale; only_rows_or_columns_with_a_white_run_are_listed_individually; the_full_neighbourhood_is_in_the_profile_line
row-runs item=hline axis=row nominal_ds=96.0000 nominal_capture=120.0000 search_capture=117-123 extent_capture=120-279
row-runs item=hline capture_row=120 ds=96.0000 white_runs=120-279 run_start=120 run_length=160 run_start_ds=64.0000 run_length_ds=128.0000 peak_luminance=255.0
row-runs item=hline rows_covered=1 row_run=120-120 peak_luminance=255.0
row-runs item=hline profile_capture=115-125 profile_luminance=0.0,0.0,0.0,0.0,0.0,255.0,0.0,0.0,0.0,0.0,0.0
row-runs item=vline axis=col nominal_ds=208.0000 nominal_capture=300.0000 search_capture=297-303 extent_capture=80-199
row-runs item=vline capture_col=300 ds=208.0000 white_runs=80-199 run_start=80 run_length=120 run_start_ds=64.0000 run_length_ds=96.0000 peak_luminance=255.0
row-runs item=vline cols_covered=1 col_run=300-300 peak_luminance=255.0
row-runs item=vline profile_capture=295-305 profile_luminance=0.0,0.0,0.0,0.0,0.0,255.0,0.0,0.0,0.0,0.0,0.0
```

`hline`/`vline` are one capture row/column (120/300), run starts 120/80, lengths 160/120, peak 255,
neighbours 0.0; in DS px (INFERRED) 96.0/208.0, starts 64.0, lengths 128.0/96.0 — same row/column,
start, length, peak as hardware; only the side profile differs (50 vs 0.0).

## 4. Bonus: our render on real PICA hardware — EXCERPT (not FACT), EQ-4o fixture

`tools/eq4o_hw_rows/hw_frameA_top400x240.png` is the real-console PICA render of the same
`twlhd_frame_v2.bin` (`68912b01…`, eq4o §2), another session; probe with default correction (`auto`,
no forced row-0 removal; eq4o `row0_wrap=0`):

```
$ python3 tools/ds_edge_test/measure_rows.py --row-runs tools/eq4o_hw_rows/hw_frameA_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
presentation-row correction: not applied (no row-0 wrap signature)
classification=none
row-runs=on
row-runs mapping_scale_x=1.25 mapping_scale_y=1.25 mapping_offset_x=40 mapping_offset_y=0
row-runs note=positions_and_lengths_measured; *_ds_values_inferred_by_dividing_by_the_mapping_scale; only_rows_or_columns_with_a_white_run_are_listed_individually; the_full_neighbourhood_is_in_the_profile_line
row-runs item=hline axis=row nominal_ds=96.0000 nominal_capture=120.0000 search_capture=117-123 extent_capture=120-279
row-runs item=hline capture_row=120 ds=96.0000 white_runs=120-279 run_start=120 run_length=160 run_start_ds=64.0000 run_length_ds=128.0000 peak_luminance=255.0
row-runs item=hline rows_covered=1 row_run=120-120 peak_luminance=255.0
row-runs item=hline profile_capture=115-125 profile_luminance=0.0,0.0,0.0,0.0,0.0,255.0,0.0,0.0,0.0,0.0,0.0
row-runs item=vline axis=col nominal_ds=208.0000 nominal_capture=300.0000 search_capture=297-303 extent_capture=80-199
row-runs item=vline capture_col=300 ds=208.0000 white_runs=80-199 run_start=80 run_length=120 run_start_ds=64.0000 run_length_ds=96.0000 peak_luminance=255.0
row-runs item=vline cols_covered=1 col_run=300-300 peak_luminance=255.0
row-runs item=vline profile_capture=295-305 profile_luminance=0.0,0.0,0.0,0.0,0.0,255.0,0.0,0.0,0.0,0.0,0.0
```

Every `row-runs` line is identical to §3: one capture row 120 / column 300, peak 255, neighbours 0.0;
EXCERPT (not FACT): the 44 `edge rect=` and 8 `item=` lines are omitted, correction `not applied`,
not repeated; this excerpt supports §5's fourth column.

## 5. Three-way (four-column) deltas (DS px; 1.25x-arm entries inferred by scale)

| quantity | hardware DS (EQ-4l) | DeSmuME (measured here) | ours: Azahar (measured here) | ours: PICA HW (EQ-4o) |
|---|---|---|---|---|
| `hline` row(s) / covered | capture 120 → DS 96.0; 1 capture row = 0.8 DS | DS 96–97; 2 DS rows | capture 120 → DS 96.0; 1 capture row = 0.8 DS | capture 120 → DS 96.0; 1 capture row = 0.8 DS |
| `hline` run start (x) | 120 → DS 64.0 | DS 64.0 | 120 → DS 64.0 | 120 → DS 64.0 |
| `hline` run length (x) | 160 → DS 128.0 | DS 128.0 | 160 → DS 128.0 | 160 → DS 128.0 |
| `vline` col / covered | capture 300 → DS 208.0; 1 capture col = 0.8 DS | DS 208; 1 DS col | capture 300 → DS 208.0; 1 capture col = 0.8 DS | capture 300 → DS 208.0; 1 capture col = 0.8 DS |
| `vline` run start (y) | 80 → DS 64.0 | DS 65.0 | 80 → DS 64.0 | 80 → DS 64.0 |
| `vline` run length (y) | 120 → DS 96.0 | DS 95.0 in extent (`65..160` full) | 120 → DS 96.0 | 120 → DS 96.0 |
| peak luminance | 255.0 | 255.0 | 255.0 | 255.0 |
| Δ `hline` row start vs 96 | 0.0 | 0.0 / +1.0 (2nd row) | 0.0 | 0.0 |
| Δ `hline` thickness vs 1 | −0.2 (grid) | +1.0 | −0.2 (grid) | −0.2 (grid) |
| Δ `vline` col vs 208 | 0.0 | 0.0 | 0.0 | 0.0 |
| Δ `vline` row start vs 64 | 0.0 | +1.0 | 0.0 | 0.0 |

## 6. Verdict (eq4ze §4 exp.1 criterion: white run length 1, run start at nominal within 1 DS px)

- **Measured.** Hardware/Azahar/PICA: one capture row/column at the nominal position, along-line runs 160/120, peak 255, neighbours ≤ 50 (hardware) or 0.0 (render). DeSmuME: `hline` two native rows; `vline` one column, run start 65.
- **INFERRED.** DS-px values and the 0.8-DS thickness of the 1.25x arms; "exactly one DS row" comes from a 0.8-DS grid, not a native scan.
- **Hardware PASS within the 1.25x grid.** Δ start = 0.0 DS, one capture row/column, neighbours below threshold (50 < 128) so the line is not split; |0.8 − 1.0| = 0.2 < half a capture pixel, so a 1-DS line is consistent. The data do **not** force "inconclusive because of 1.25x resampling".
- **Ours PASS** (same run/position numbers; corrected Azahar and real PICA hardware, §1 complete block plus §3/§4 excerpts).
- **DeSmuME `hline` FAIL** (2 DS rows); **`vline` +1 start** (one DS row low, column nominal) — the same per-horizontal-edge `+1` rule as eq4k §3.
- **Proposal (text only).** No new capture; a finer test would be a 2-px line at the same positions or a phase with one dominant capture row; not built.

**UNVERIFIED:** diagonal 1-px lines, other widths, wireframe polygon mode (alpha 0), line colour/blending. ONE console and ONE session per hardware arm (DS edge: EQ-4l; PICA: EQ-4o); axis-aligned lines only.

## 7. Option, tests and size

`--row-runs` is opt-in; without it every existing line is byte-identical. Baseline is `main`'s script,
which already carries `--azahar-correction` (FACT below) and differs from the current file only by
the `--row-runs` additions:

```
$ git show main:tools/ds_edge_test/measure_rows.py > <logfile>.measure_rows.py; grep -c azahar-correction <logfile>.measure_rows.py > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
3
```

```
$ mkdir -p <logfile>.dir/old-root && git archive main tools/ds_edge_test tools/ds_alpha_test tools/azahar_presentation.py | tar -x -C <logfile>.dir/old-root > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
$ python3 <logfile>.dir/old-root/tools/ds_edge_test/measure_rows.py tools/ds_edge_test/fixtures/ed1_3_top400x240.png > <logfile>.old 2>&1; echo "exit=$?"; wc -l <logfile>.old
exit=0
58 <logfile>.old
$ python3 tools/ds_edge_test/measure_rows.py tools/ds_edge_test/fixtures/ed1_3_top400x240.png > <logfile>.new 2>&1; echo "exit=$?"; cmp <logfile>.old <logfile>.new > <logfile> 2>&1; echo "cmp_exit=$?"; cat <logfile>
exit=0
cmp_exit=0
```

`cmp_exit=0` with empty `cmp` output is the byte-identity proof (both 58-line outputs are
byte-identical; only equality matters, so they are not reprinted). New tests are
`tools/ds_edge_test/test_row_runs.py`, one added fixture-pinning test; existing tests unchanged:

```
$ make test-ds-edge-test > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
..............................................
----------------------------------------------------------------------
Ran 46 tests in 1.180s

OK
```

Root `make test` also passes (435 tests, 110 skipped, exit 0). Size audit:

```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zf-1px-lines.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
51671
543 docs/reports/eq4zf-1px-lines.md
```

Run counts, exact per type: plain ROM build 1, record build 2, capture 2, arm11
build 2, `azahar_host` 2 (top crop identical in both), harness 1, §1 probe 4,
§2 PNG probe 1, §3 crop 4 (every run: pixel sha `c8e639ce…`, PNG sha
`f3fdc7d9…`), §4 probe 1, §7 old/new 2. No console action; hardware numbers are
from the fixtures.
