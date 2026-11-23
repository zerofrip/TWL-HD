<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4f — static textured scene5: hardware vs harness and PICA — 2026-11-23

**Scope:** fixtures, tests and documentation only; no hardware action in this
task. Added: four PNG fixtures in `tools/fixtures/eq4_hw/`, six real-fixture
comparison tests in `tools/test_eq4_hw_compare.py`, the centroid-delta helper
`tools/eq4_centroid_deltas.py` with its test, and this report. The directory
`tools/fixtures/eq4_hw/` was chosen over `tools/ds_scale_test/fixtures/` because
these pictures are EQ-4 inputs (a hardware capture plus two reference renders),
not ds-scale-test captures, and the existing eq_scenes harness references stay
in `tools/fixtures/eq_scenes/`.

**Labels.** FACT = a command and its raw complete output quoted verbatim below.
HARDWARE = the cc3dsfs capture of the console; EMULATOR = the DeSmuME harness
frame or the Azahar/PICA host render; BUILD = a ROM hash. HYPOTHESIS /
UNVERIFIED = not measured here. OWNER-REPORTED = an owner statement, not a
quoted output. Every comparison carries `basis=one console, one capture` and
nothing here claims universal hardware equality.

**OWNER-REPORTED hardware actions (not FACT).** The assistant uploaded the two
plain ROMs through the FTP flow to `/roms/nds/twlhd-s5-st0.nds` and
`/roms/nds/twlhd-s5-st30.nds`; the owner started each from TWiLight Menu++ in
DS mode on the New 3DS, and the passive cc3dsfs capture board took four frames
per ROM, 2 s apart, with `--enabled_both 0 --enabled_top 1 --scaling_top 1` and
the fixed 24-bpp decoder (`tools/cc3dsfs_capture/xwd2png.py`). The third frame
(`st*_3`) is the fixture. The owner placed and started nothing else by hand.

## 1. Setup and inputs

The ROMs were built (task input, not rebuilt here) from main `9ef50e9` with
`make -C arm9v2 SCENE=5 STATIC_ANGLE=<0|30>` (plain, hardware) and the same
plus `AUTODUMP_FRAME=240` (autodump, emulator). The ROMs are not committed.
**FACT** — hashes and sizes (quoted command and complete output):

```
$ cd "$HOME/3ds/hwtest-staging" && sha256sum twlhd-arm9v2-autodump-s5-st0.nds twlhd-arm9v2-autodump-s5-st30.nds twlhd-arm9v2-s5-st0.nds twlhd-arm9v2-s5-st30.nds && ls -l twlhd-arm9v2-s5-st0.nds twlhd-arm9v2-s5-st30.nds
8f4d506fbbe92e693e0727a3c71e7c9392f1cbb677a37d350d8c15e87b8d960e  twlhd-arm9v2-autodump-s5-st0.nds
30337896a415a433376f8478db1ebe973ce401f4bb657d9d9b878bb27ff2063e  twlhd-arm9v2-autodump-s5-st30.nds
345bc8efc51706c8e5cc5b4cfb8f49a226c5fe93dc0154fb59aadad9bc937081  twlhd-arm9v2-s5-st0.nds
1b6cb5c10fe32e85f2c498d145240836476c40360a9a86f9a8dc9df44944a105  twlhd-arm9v2-s5-st30.nds
-rw-rw-r-- 1 <user> <group> 235520 Nov 23 01:29 twlhd-arm9v2-s5-st0.nds
-rw-rw-r-- 1 <user> <group> 235520 Nov 23 01:29 twlhd-arm9v2-s5-st30.nds
```

Owner/group fields of quoted ls -l lines are replaced by <user> <group> (privacy); all other fields are verbatim.

The harness references are the committed 256x192 PNGs; `eq4_hw_compare.py`
reads PPM, so the tests convert them in a `tempfile.mkdtemp` directory.

**FACT** — reference hashes:

```
$ sha256sum tools/fixtures/eq_scenes/harness_s5_st0_f240.png tools/fixtures/eq_scenes/harness_s5_st30_f240.png
dce0a5c7f6decfdd78c0d65547f43e840818fe41bcf55c1f6dd68b51a8e848bb  tools/fixtures/eq_scenes/harness_s5_st0_f240.png
5be2f7e0d772421b814af1712f2197c2d087b0363ea806e7189113758421bd35  tools/fixtures/eq_scenes/harness_s5_st30_f240.png
```

The "ours" reference is the PICA render of the same frame from
`tools/azahar_host` (EMULATOR), with `sdmc:/twlhd_frame.bin` = the DeSmuME
`twlhd_frame_v2.bin` capture of the autodump ROM:

```
$ azahar_host --core <libretro core> --game arm11/twlhd-arm11.3dsx \
      --frames 200 --press A:60-63 --shot 199 --out <dir> --sd-root <sd>
```

**FACT** — the committed `ours_s5_st*` crops are pixel-identical to the top
400x240 crop of `azahar{0,30}_199.ppm` (quoted command and complete output):

```
$ python3 - <<'EOF'
from PIL import Image
import os
home = os.environ["HOME"]
for a in ("0", "30"):
    ppm = Image.open(home + "/3ds/hwtest-staging/oursrun/azahar" + a
                     + "_199.ppm").convert("RGB").crop((0, 0, 400, 240)).tobytes()
    png = Image.open("tools/fixtures/eq4_hw/ours_s5_st" + a
                     + "_top400x240.png").convert("RGB").tobytes()
    print(a, "pixels equal", ppm == png)
EOF
0 pixels equal True
30 pixels equal True
```

**FACT** — the four fixtures are byte-identical copies from the staging crop
directory, and every file is below the 100 KB cap (quoted command and
complete output):

```
$ for f in st0_3_top400x240.png st30_3_top400x240.png ours_s5_st0_top400x240.png ours_s5_st30_top400x240.png; do cmp "$HOME/3ds/hwtest-staging/cc3d-out/$f" "tools/fixtures/eq4_hw/$f" && echo "$f identical"; done
st0_3_top400x240.png identical
st30_3_top400x240.png identical
ours_s5_st0_top400x240.png identical
ours_s5_st30_top400x240.png identical
$ sha256sum tools/fixtures/eq4_hw/*.png && ls -l tools/fixtures/eq4_hw/*.png
7ec6d8c09b992da14b3cf5a48b5313360b904c16e043550b735c09c3effd8ce3  tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png
85e97da974748e175657f86f9632dc061c55807af0f5553e3a6c1311323e9f34  tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
d7fca639bba85e2cfa02812ed9ef86d625a653f9c4d9c658a9e4b0daee1cbe99  tools/fixtures/eq4_hw/st0_3_top400x240.png
b50d97f2c6776c801c37476d429ebf38129eef8996120989e86aa8c5131ada04  tools/fixtures/eq4_hw/st30_3_top400x240.png
-rw-rw-r-- 1 <user> <group>  884 Nov 23 10:15 tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png
-rw-rw-r-- 1 <user> <group> 1846 Nov 23 10:15 tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
-rw-rw-r-- 1 <user> <group> 2965 Nov 23 10:15 tools/fixtures/eq4_hw/st0_3_top400x240.png
-rw-rw-r-- 1 <user> <group> 9285 Nov 23 10:15 tools/fixtures/eq4_hw/st30_3_top400x240.png
```

The two hardware-capture hashes match the prefixes given in the task
(`d7fca639bba85e2cfa02…`, `b50d97f2c6776c801c37…`). The mapping is the shared
one measured in EQ-4d/eq4c (scale 1.25, offset 40):

```
$ cat tools/ds_vtxcolor_test/capture_mapping.json
{
  "scale_x": 1.25,
  "scale_y": 1.25,
  "offset_x": 40,
  "offset_y": 0,
  "source": "docs/reports/eq4d-ds-vertex-colour-hw.md"
}
```

## 2. Commands and raw outputs (re-run)

Reference PPMs are produced in a temp directory because `eq4_hw_compare.py`
accepts PPM references only (the capture side is read from the PNG directly):

```
$ mkdir -p /tmp/eq4f-refs && python3 - <<'EOF'
from PIL import Image
for src, dst in (
    ("tools/fixtures/eq_scenes/harness_s5_st0_f240.png", "/tmp/eq4f-refs/harness_s5_st0.ppm"),
    ("tools/fixtures/eq_scenes/harness_s5_st30_f240.png", "/tmp/eq4f-refs/harness_s5_st30.ppm"),
    ("tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png", "/tmp/eq4f-refs/ours_s5_st0.ppm"),
    ("tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png", "/tmp/eq4f-refs/ours_s5_st30.ppm"),
):
    Image.open(src).convert("RGB").save(dst)
EOF
```

### 2.1 st0 capture vs harness reference — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference /tmp/eq4f-refs/harness_s5_st0.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=4.2553
ds.mean_abs_g=4.6742
ds.mean_abs_b=3.5130
ds.max_error=255.0000
ds.frac_within=0.9468
ds.frac_within_flat=0.9997
ds.frac_within_edge=0.4163
ds.flat_pixels=44701
ds.edge_pixels=4451
capture.mean_abs_r=3.9901
capture.mean_abs_g=4.3992
capture.mean_abs_b=3.3469
capture.max_error=255.0000
capture.frac_within=0.9529
capture.frac_within_flat=0.9996
capture.frac_within_edge=0.4160
capture.flat_pixels=70649
capture.edge_pixels=6151
ref_nonbg_pixels=6987
ref_bbox_left=60
ref_bbox_top=28
ref_bbox_right=196
ref_bbox_bottom=164
capture_nonbg_pixels=8042
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=2346
class_white_ref_centroid_x=127.5072
class_white_ref_centroid_y=95.3768
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=1156
class_red_ref_centroid_x=128.5000
class_red_ref_centroid_y=96.0000
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=1156
class_green_ref_centroid_x=128.5000
class_green_ref_centroid_y=96.0000
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=1156
class_blue_ref_centroid_x=128.5000
class_blue_ref_centroid_y=96.0000
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1173
class_yellow_ref_centroid_x=127.5072
class_yellow_ref_centroid_y=96.3768
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=2
bbox_dev_top=2
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
```

### 2.2 st30 capture vs harness reference — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st30_3_top400x240.png --reference /tmp/eq4f-refs/harness_s5_st30.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=5.7679
ds.mean_abs_g=6.0295
ds.mean_abs_b=4.6784
ds.max_error=255.0000
ds.frac_within=0.9455
ds.frac_within_flat=0.9998
ds.frac_within_edge=0.4175
ds.flat_pixels=44568
ds.edge_pixels=4584
capture.mean_abs_r=5.7684
capture.mean_abs_g=6.0176
capture.mean_abs_b=4.6958
capture.max_error=255.0000
capture.frac_within=0.9431
capture.frac_within_flat=0.9995
capture.frac_within_edge=0.3651
capture.flat_pixels=69967
capture.edge_pixels=6833
ref_nonbg_pixels=7441
ref_bbox_left=17
ref_bbox_top=17
ref_bbox_right=182
ref_bbox_bottom=162
capture_nonbg_pixels=8335
capture_bbox_left=16
capture_bbox_top=15
capture_bbox_right=181
capture_bbox_bottom=161
class_white_ref_area=3405
class_white_ref_centroid_x=68.9797
class_white_ref_centroid_y=57.3028
class_white_capture_area=3276
class_white_capture_centroid_x=66.6914
class_white_capture_centroid_y=55.4829
class_red_ref_area=1160
class_red_ref_centroid_x=117.5224
class_red_ref_centroid_y=64.8862
class_red_capture_area=1174
class_red_capture_centroid_x=116.1022
class_red_capture_centroid_y=63.2419
class_green_ref_area=994
class_green_ref_centroid_x=131.8873
class_green_ref_centroid_y=72.1237
class_green_capture_area=978
class_green_capture_centroid_x=130.9356
class_green_capture_centroid_y=70.8885
class_blue_ref_area=955
class_blue_ref_centroid_x=135.8147
class_blue_ref_centroid_y=75.3654
class_blue_capture_area=932
class_blue_capture_centroid_x=134.9721
class_blue_capture_centroid_y=73.9131
class_yellow_ref_area=927
class_yellow_ref_centroid_x=123.8220
class_yellow_ref_centroid_y=92.6656
class_yellow_capture_area=869
class_yellow_capture_centroid_x=122.6018
class_yellow_capture_centroid_y=91.2693
bbox_dev_left=1
bbox_dev_top=2
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
```

### 2.3 st0 capture vs ours (PICA) reference — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference /tmp/eq4f-refs/ours_s5_st0.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=12.8123
ds.mean_abs_g=13.0460
ds.mean_abs_b=17.0238
ds.max_error=255.0000
ds.frac_within=0.3366
ds.frac_within_flat=0.9999
ds.frac_within_edge=0.0419
ds.flat_pixels=15122
ds.edge_pixels=34030
capture.mean_abs_r=12.3624
capture.mean_abs_g=12.5465
capture.mean_abs_b=16.7582
capture.max_error=255.0000
capture.frac_within=0.3486
capture.frac_within_flat=0.9985
capture.frac_within_edge=0.0465
capture.flat_pixels=24374
capture.edge_pixels=52426
ref_nonbg_pixels=7193
ref_bbox_left=59
ref_bbox_top=28
ref_bbox_right=195
ref_bbox_bottom=164
capture_nonbg_pixels=8042
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=2380
class_white_ref_centroid_x=127.0000
class_white_ref_centroid_y=96.0000
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=1208
class_red_ref_centroid_x=126.9859
class_red_ref_centroid_y=95.6200
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=1208
class_green_ref_centroid_x=126.9859
class_green_ref_centroid_y=96.3800
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=1207
class_blue_ref_centroid_x=127.0141
class_blue_ref_centroid_y=96.8451
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=1190
class_yellow_ref_centroid_x=127.0000
class_yellow_ref_centroid_y=96.0000
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=1
bbox_dev_top=2
bbox_dev_right=0
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
```

### 2.4 st30 capture vs ours (PICA) reference — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st30_3_top400x240.png --reference /tmp/eq4f-refs/ours_s5_st30.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=13.5205
ds.mean_abs_g=13.7165
ds.mean_abs_b=17.7399
ds.max_error=255.0000
ds.frac_within=0.3290
ds.frac_within_flat=0.9986
ds.frac_within_edge=0.0484
ds.flat_pixels=14516
ds.edge_pixels=34636
capture.mean_abs_r=13.2929
capture.mean_abs_g=13.4602
capture.mean_abs_b=17.6354
capture.max_error=255.0000
capture.frac_within=0.3326
capture.frac_within_flat=0.9964
capture.frac_within_edge=0.0459
capture.flat_pixels=23166
capture.edge_pixels=53634
ref_nonbg_pixels=7444
ref_bbox_left=16
ref_bbox_top=17
ref_bbox_right=181
ref_bbox_bottom=162
capture_nonbg_pixels=8335
capture_bbox_left=16
capture_bbox_top=15
capture_bbox_right=181
capture_bbox_bottom=161
class_white_ref_area=3391
class_white_ref_centroid_x=68.4300
class_white_ref_centroid_y=57.5801
class_white_capture_area=3276
class_white_capture_centroid_x=66.6914
class_white_capture_centroid_y=55.4829
class_red_ref_area=1173
class_red_ref_centroid_x=116.2924
class_red_ref_centroid_y=65.0708
class_red_capture_area=1174
class_red_capture_centroid_x=116.1022
class_red_capture_centroid_y=63.2419
class_green_ref_area=972
class_green_ref_centroid_x=130.3477
class_green_ref_centroid_y=72.8724
class_green_capture_area=978
class_green_capture_centroid_x=130.9356
class_green_capture_centroid_y=70.8885
class_blue_ref_area=971
class_blue_ref_centroid_x=134.0247
class_blue_ref_centroid_y=76.4830
class_blue_capture_area=932
class_blue_capture_centroid_x=134.9721
class_blue_capture_centroid_y=73.9131
class_yellow_ref_area=937
class_yellow_ref_centroid_x=123.4621
class_yellow_ref_centroid_y=91.7279
class_yellow_capture_area=869
class_yellow_capture_centroid_x=122.6018
class_yellow_capture_centroid_y=91.2693
bbox_dev_left=0
bbox_dev_top=2
bbox_dev_right=0
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
```

### 2.5 Negative control: st0 capture vs st30 harness reference

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st0_3_top400x240.png --reference /tmp/eq4f-refs/harness_s5_st30.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=36.0166
ds.mean_abs_g=35.6132
ds.mean_abs_b=26.6464
ds.max_error=255.0000
ds.frac_within=0.7993
ds.frac_within_flat=0.8611
ds.frac_within_edge=0.3287
ds.flat_pixels=43447
ds.edge_pixels=5705
capture.mean_abs_r=35.9817
capture.mean_abs_g=35.5746
capture.mean_abs_b=26.6323
capture.max_error=255.0000
capture.frac_within=0.8006
capture.frac_within_flat=0.8611
capture.frac_within_edge=0.3066
capture.flat_pixels=68425
capture.edge_pixels=8375
ref_nonbg_pixels=7441
ref_bbox_left=17
ref_bbox_top=17
ref_bbox_right=182
ref_bbox_bottom=162
capture_nonbg_pixels=8042
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=3405
class_white_ref_centroid_x=68.9797
class_white_ref_centroid_y=57.3028
class_white_capture_area=2289
class_white_capture_centroid_x=126.0861
class_white_capture_centroid_y=94.1223
class_red_ref_area=1160
class_red_ref_centroid_x=117.5224
class_red_ref_centroid_y=64.8862
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=994
class_green_ref_centroid_x=131.8873
class_green_ref_centroid_y=72.1237
class_green_capture_area=1171
class_green_capture_centroid_x=127.6097
class_green_capture_centroid_y=94.4919
class_blue_ref_area=955
class_blue_ref_centroid_x=135.8147
class_blue_ref_centroid_y=75.3654
class_blue_capture_area=1154
class_blue_capture_centroid_x=127.3388
class_blue_capture_centroid_y=95.4419
class_yellow_ref_area=927
class_yellow_ref_centroid_x=123.8220
class_yellow_ref_centroid_y=92.6656
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5086
class_yellow_capture_centroid_y=94.1252
bbox_dev_left=41
bbox_dev_top=9
bbox_dev_right=13
bbox_dev_bottom=1
verdict=FAIL
reason=bbox-left deviates 41 DS px (tol 3); bbox-top deviates 9 DS px (tol 3); bbox-right deviates 13 DS px (tol 3); ds.frac_within_flat 0.8611 < 0.95; class white centroid deviates 57.11 DS px (tol 3); class red centroid deviates 29.62 DS px (tol 3); class green centroid deviates 22.37 DS px (tol 3); class blue centroid deviates 20.08 DS px (tol 3); class yellow centroid deviates 3.69 DS px (tol 3)
```

### 2.6 Negative control: st30 capture vs st0 harness reference

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw/st30_3_top400x240.png --reference /tmp/eq4f-refs/harness_s5_st0.ppm
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=36.8492
ds.mean_abs_g=36.6302
ds.mean_abs_b=28.1045
ds.max_error=255.0000
ds.frac_within=0.7922
ds.frac_within_flat=0.8546
ds.frac_within_edge=0.3267
ds.flat_pixels=43339
ds.edge_pixels=5813
capture.mean_abs_r=36.8375
capture.mean_abs_g=36.5998
capture.mean_abs_b=28.0943
capture.max_error=255.0000
capture.frac_within=0.7892
capture.frac_within_flat=0.8514
capture.frac_within_edge=0.2927
capture.flat_pixels=68259
capture.edge_pixels=8541
ref_nonbg_pixels=6987
ref_bbox_left=60
ref_bbox_top=28
ref_bbox_right=196
ref_bbox_bottom=164
capture_nonbg_pixels=8335
capture_bbox_left=16
capture_bbox_top=15
capture_bbox_right=181
capture_bbox_bottom=161
class_white_ref_area=2346
class_white_ref_centroid_x=127.5072
class_white_ref_centroid_y=95.3768
class_white_capture_area=3276
class_white_capture_centroid_x=66.6914
class_white_capture_centroid_y=55.4829
class_red_ref_area=1156
class_red_ref_centroid_x=128.5000
class_red_ref_centroid_y=96.0000
class_red_capture_area=1174
class_red_capture_centroid_x=116.1022
class_red_capture_centroid_y=63.2419
class_green_ref_area=1156
class_green_ref_centroid_x=128.5000
class_green_ref_centroid_y=96.0000
class_green_capture_area=978
class_green_capture_centroid_x=130.9356
class_green_capture_centroid_y=70.8885
class_blue_ref_area=1156
class_blue_ref_centroid_x=128.5000
class_blue_ref_centroid_y=96.0000
class_blue_capture_area=932
class_blue_capture_centroid_x=134.9721
class_blue_capture_centroid_y=73.9131
class_yellow_ref_area=1173
class_yellow_ref_centroid_x=127.5072
class_yellow_ref_centroid_y=96.3768
class_yellow_capture_area=869
class_yellow_capture_centroid_x=122.6018
class_yellow_capture_centroid_y=91.2693
bbox_dev_left=44
bbox_dev_top=13
bbox_dev_right=15
bbox_dev_bottom=3
verdict=FAIL
reason=bbox-left deviates 44 DS px (tol 3); bbox-top deviates 13 DS px (tol 3); bbox-right deviates 15 DS px (tol 3); ds.frac_within_flat 0.8546 < 0.95; class white centroid deviates 60.82 DS px (tol 3); class red centroid deviates 32.76 DS px (tol 3); class green centroid deviates 25.11 DS px (tol 3); class blue centroid deviates 22.09 DS px (tol 3); class yellow centroid deviates 5.11 DS px (tol 3)
```

**Re-run vs the pre-quoted numbers.** All four PASS cases reproduce the
pre-quoted numbers exactly (mean_abs, `frac_within_flat`, bboxes, bbox_dev,
verdict, reason, class areas and centroids); the complete blocks above also
carry the keys the brief omitted (`ds.max_error`, `capture.*`, flat/edge pixel
counts). The two negative controls exit 1 with `verdict=FAIL`, as required.
No deviation had to be recorded for these six runs.

## 3. Results table (FACT, only from the quoted outputs)

| Pair | Kind | ds.mean_abs r/g/b | ds.frac_within_flat | ds.frac_within_edge | bbox_dev l/t/r/b | Verdict |
|---|---|---|---|---|---|---|
| st0 hw vs harness | HARDWARE/EMULATOR | 4.2553/4.6742/3.5130 | 0.9997 | 0.4163 | 2/2/1/1 | PASS |
| st30 hw vs harness | HARDWARE/EMULATOR | 5.7679/6.0295/4.6784 | 0.9998 | 0.4175 | 1/2/1/1 | PASS |
| st0 hw vs ours | HARDWARE/EMULATOR | 12.8123/13.0460/17.0238 | 0.9999 | 0.0419 | 1/2/0/1 | PASS |
| st30 hw vs ours | HARDWARE/EMULATOR | 13.5205/13.7165/17.7399 | 0.9986 | 0.0484 | 0/2/0/1 | PASS |
| st0 hw vs st30 harness | control | 36.0166/35.6132/26.6464 | 0.8611 | 0.3287 | 41/9/13/1 | FAIL |
| st30 hw vs st0 harness | control | 36.8492/36.6302/28.1045 | 0.8546 | 0.3267 | 44/13/15/3 | FAIL |

Class areas (reference/capture, DS pixels) from the same outputs:

| Pair | white | red | green | blue | yellow |
|---|---|---|---|---|---|
| st0 vs harness | 2346/2289 | 1156/1173 | 1156/1171 | 1156/1154 | 1173/1166 |
| st30 vs harness | 3405/3276 | 1160/1174 | 994/978 | 955/932 | 927/869 |
| st0 vs ours | 2380/2289 | 1208/1173 | 1208/1171 | 1207/1154 | 1190/1166 |
| st30 vs ours | 3391/3276 | 1173/1174 | 972/978 | 971/932 | 937/869 |

## 4. Environment finding: a stale arm11 build drew the quad black

The first "ours" render used a stale `arm11/twlhd-arm11.3dsx` built on
2026-11-14, i.e. before the 2026-11-17 initial-vertex-colour fix (commit
`e4df370`, 2026-11-17 20:32:56 +0900). The exact worktree path was overwritten
by the later `make arm11`; the surviving 2026-11-14 build copies and the
rebuilt file have these timestamps:

**FACT** — timestamps (quoted command and complete output):

```
$ cd "$HOME/3ds/hwtest-staging" && ls -l --time-style=long-iso twlhd-arm11-tex.3dsx twlhd-arm11-tex2.3dsx
-rw-rw-r-- 1 <user> <group> 203420 2026-11-14 07:05 twlhd-arm11-tex.3dsx
-rw-rw-r-- 1 <user> <group> 203436 2026-11-14 14:46 twlhd-arm11-tex2.3dsx
$ cd "$HOME/3ds/TWL-HD/arm11" && ls -l --time-style=long-iso twlhd-arm11.3dsx
-rw-rw-r-- 1 <user> <group> 203876 2026-11-23 01:36 twlhd-arm11.3dsx
```

**FACT** — a re-run of the 2026-11-14 `twlhd-arm11-tex2.3dsx` through the
same Azahar host path (same staged `twlhd_frame.bin`) draws two colours only,
the quad black; the rebuilt render draws the seven colours in the section 5
census. Commands and complete output:

```
$ mkdir -p /tmp/eq4f-stale/st0/sd /tmp/eq4f-stale/st30/sd
$ cp "$HOME/3ds/hwtest-staging/oursrun/sd0/twlhd_frame.bin" /tmp/eq4f-stale/st0/sd/
$ cp "$HOME/3ds/hwtest-staging/oursrun/sd30/twlhd_frame.bin" /tmp/eq4f-stale/st30/sd/
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game "$HOME/3ds/hwtest-staging/twlhd-arm11-tex2.3dsx" --frames 200 --press A:60-63 --shot 199 --out /tmp/eq4f-stale/st0/before --sd-root /tmp/eq4f-stale/st0/sd > /tmp/eq4f-stale/st0/render.log 2>&1; echo "exit=$?"
exit=0
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game "$HOME/3ds/hwtest-staging/twlhd-arm11-tex2.3dsx" --frames 200 --press A:60-63 --shot 199 --out /tmp/eq4f-stale/st30/before --sd-root /tmp/eq4f-stale/st30/sd > /tmp/eq4f-stale/st30/render.log 2>&1; echo "exit=$?"
exit=0
$ python3 - <<'EOF'
from PIL import Image
for a in ("0", "30"):
    ppm = Image.open("/tmp/eq4f-stale/st%s/before_199.ppm" % a).convert("RGB")
    ppm.crop((0, 0, 400, 240)).save("/tmp/eq4f-stale/st%s/before_top400x240.png" % a)
EOF
$ python3 tools/ds_scale_test/colour_histogram.py /tmp/eq4f-stale/st0/before_top400x240.png
(16, 16, 24) 66415
(0, 0, 0) 29585
$ python3 tools/ds_scale_test/colour_histogram.py /tmp/eq4f-stale/st30/before_top400x240.png
(16, 16, 24) 67777
(0, 0, 0) 28223
```

The task brief quoted "black region 18233/16638 px before": the re-run shows
those two numbers are the black-background counts of the **current, textured**
fixtures (after the rebuild), not of the pre-fix render. Re-run wins; the
pre-fix black counts measured now are 29585 (st0) and 28223 (st30), and the
pre-fix renders have only two distinct colours. The cause is **UNVERIFIED**:
the timestamp evidence is consistent with a stale build that predates the
initial-colour fix, but the old build's source revision was not recorded.

## 5. Interpretations (all HYPOTHESIS, with limits)

**(i) Agreement of the three pictures.** For these two static angles on one
console, hardware, the DeSmuME harness and our PICA render agree on the five
cell colours, on cell areas within about 5% (e.g. st0 white 2346 vs 2289
= -2.4%, st30 white 3405 vs 3276 = -3.8%, st0 ours red 1208 vs capture 1173
= -2.9%), and on geometry within the tool's tolerances (all bbox and centroid
checks pass, `ds.frac_within_flat >= 0.9986`). Limits: one console, one
capture per angle, one ROM per angle; "agreement" means the tool's stated
tolerances, not bit-exactness.

**(ii) Systematic upward/left offset of the capture.** **FACT** — per-class
centroid deltas (capture minus reference), computed by the new helper and
quoted complete:

```
$ python3 tools/eq4_centroid_deltas.py /tmp/eq4f-out/st0_harness.txt /tmp/eq4f-out/st0_ours.txt /tmp/eq4f-out/st30_harness.txt /tmp/eq4f-out/st30_ours.txt
/tmp/eq4f-out/st0_harness.txt
white dx=-1.4211 dy=-1.2545
red dx=-1.1232 dy=-1.4928
green dx=-0.8903 dy=-1.5081
blue dx=-1.1612 dy=-0.5581
yellow dx=0.0014 dy=-2.2516
/tmp/eq4f-out/st0_ours.txt
white dx=-0.9139 dy=-1.8777
red dx=0.3909 dy=-1.1128
green dx=0.6238 dy=-1.8881
blue dx=0.3247 dy=-1.4032
yellow dx=0.5086 dy=-1.8748
/tmp/eq4f-out/st30_harness.txt
white dx=-2.2883 dy=-1.8199
red dx=-1.4202 dy=-1.6443
green dx=-0.9517 dy=-1.2352
blue dx=-0.8426 dy=-1.4523
yellow dx=-1.2202 dy=-1.3963
/tmp/eq4f-out/st30_ours.txt
white dx=-1.7386 dy=-2.0972
red dx=-0.1902 dy=-1.8289
green dx=0.5879 dy=-1.9839
blue dx=0.9474 dy=-2.5699
yellow dx=-0.8603 dy=-0.4586
```

HYPOTHESIS: the capture sits systematically above BOTH references — `dy` is
negative in all 20 class/angle/reference pairs, from -0.46 to -2.57 DS px —
and against the harness it also sits left (`dx` negative in 9 of 10, down to
-2.29; against ours `dx` is mixed, -1.74..+0.95). This is consistent with a
capture-mapping/rounding offset (1.25x resampling, the area filter and the
+1 DS px allowance), but a real ~1-px shift of the captured picture is not
excluded; **UNVERIFIED which**. Limits: the mapping is reused from a different
ROM's capture (same console), and the deltas are class centroids, not a
per-pixel registration.

**(iii) mean_abs against ours is larger because of the PICA clear colour.**
**FACT** — the full colour census of the current ours fixtures:

```
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png
(16, 16, 24) 66415
(0, 0, 0) 18233
(255, 255, 255) 3784
(0, 0, 247) 1892
(0, 255, 8) 1892
(255, 0, 0) 1892
(255, 255, 24) 1892
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
(16, 16, 24) 67777
(0, 0, 0) 16638
(255, 255, 255) 5276
(255, 0, 0) 1815
(0, 255, 8) 1556
(0, 0, 247) 1486
(255, 255, 24) 1452
```

**FACT** — inside the sampled 256x192 DS grid, the clear colour covers most
of the pixels the hardware renders black (quoted command and complete output):

```
$ python3 - <<'EOF'
import sys
sys.path.insert(0, "tools")
import eq4_hw_compare as eq4
mapping = eq4.load_mapping("tools/ds_vtxcolor_test/capture_mapping.json")
for angle in ("st0", "st30"):
    capture = eq4.capture_to_ds(
        eq4.load_capture("tools/fixtures/eq4_hw/%s_3_top400x240.png" % angle),
        mapping)
    ours = eq4.load_reference("/tmp/eq4f-refs/ours_s5_%s.ppm" % angle, mapping)
    clear_vs_black = sum(1 for o, h in zip(ours, capture)
                         if o == (16, 16, 24) and h == (0, 0, 0))
    print(angle, "ours_clear_vs_hardware_black=%d of %d DS pixels"
          % (clear_vs_black, len(ours)))
EOF
st0 ours_clear_vs_hardware_black=29623 of 49152 DS pixels
st30 ours_clear_vs_hardware_black=30234 of 49152 DS pixels
```

HYPOTHESIS: the difference of 16/16/24 per channel over about 60% of the DS
grid (29623 and 30234 of 49152 pixels) explains most of the larger mean_abs
against ours (12.81/13.05/17.02 and 13.52/13.72/17.74) compared with the
harness (4.26/4.67/3.51 and 5.77/6.03/4.68): the harness renders those pixels
black like the hardware, while the PICA top screen is cleared to (16,16,24).
The brief's wording "outside the DS area" is corrected here: the histogram is
a census of the whole 400x240 crop, and the contrast is inside the sampled DS
grid as well. Limits: this is a colour-count argument, not a pixel-pair
decomposition; lighting/filtering differences inside the quad would add to
mean_abs but are not separated here.

**(iv) What this does NOT show.** NOT shown: bit-exactness (the tool's PASS is
a tolerance result), any other scene, any other console or capture board setting,
texture filtering details, the alpha convention (`a/31` vs `(a+1)/32`), and
blending behaviour. The alpha convention remains **UNVERIFIED**: all alpha
values in this scene are 31, so the two conventions coincide and this run does
not exercise the difference.

## 6. Tests

`tools/test_eq4_hw_compare.py` gains `RealScene5StaticCompareTest` with six
tests, added to the existing `test-eq4-hw-compare` target (same file and skip
conventions, so no Makefile change):

| Case | Assertion (only what the tool prints) |
|---|---|
| st0 capture vs harness | verdict PASS, exit 0, basis, every bbox_dev <= 3, `frac_within_flat >= 0.95` |
| st30 capture vs harness | same |
| st0 capture vs ours | same |
| st30 capture vs ours | same |
| st0 capture vs st30 harness | verdict in (FAIL, NOT-COMPARABLE), matching exit code (never PASS) |
| st30 capture vs st0 harness | same |

`tools/eq4_centroid_deltas.py` is covered by `tools/test_eq4_centroid_deltas.py`
(inline report strings; aggregate `make test` discovers it).

## 7. Privacy

Every added command uses `$HOME`; no literal home-directory path is added. The
three pointer docs carry older, pre-existing literal paths, so the check is
scoped to the added lines plus the new files:

```
$ git diff -U0 -- docs/EQ4_HW_COMPARE.md docs/EQUALITY_CHECK_DESIGN.md docs/EQ_SCENES.md tools/test_eq4_hw_compare.py | grep -E '^\+' | grep -E '/(home|Users)/'
(no output; exit status 1)
$ grep -rn -E '/(home|Users)/' tools/eq4_centroid_deltas.py tools/test_eq4_centroid_deltas.py docs/reports/eq4f-scene5-static-hw-compare.md
(no output; exit status 1)
```

## 8. Requirement traceability

- **REQ fixtures added (four PNGs, <= 100 KB each, hw sha prefixes match,
  directory choice documented)** — FACT: §1 copy/cmp/sha/ls blocks;
  `d7fca639bba85e2cfa02…` and `b50d97f2c6776c801c37…` match the task.
- **REQ four PASS tests plus two negative controls wired into the existing
  target** — FACT: `tools/test_eq4_hw_compare.py:RealScene5StaticCompareTest`;
  `make test-eq4-hw-compare` ran 13 tests OK.
- **REQ report with setup, owner-reported procedure, re-run raw outputs,
  results table, hypotheses (i)-(iv), limits** — FACT: §§1-5, all outputs
  quoted from re-runs.
- **REQ centroid deltas from a small script/test, quoted** — FACT:
  `tools/eq4_centroid_deltas.py`, `tools/test_eq4_centroid_deltas.py`, §5(ii).
- **REQ stale-3dsx environment finding with `ls -l` quote and UNVERIFIED
  cause** — FACT: §4; the brief's "18233/16638 before" is corrected to the
  current-texture counts, re-run before counts 29585/28223.
- **REQ append-only pointers** — `Result EQ-4f` in
  `docs/EQUALITY_CHECK_DESIGN.md`, section 9 in `docs/EQ4_HW_COMPARE.md`,
  §6.9 in `docs/EQ_SCENES.md`; additions only.
- **REQ never weaken tests; `make test` green** — all existing tests untouched;
  `make test` green (see the implementation report).
- **REQ no worktree commands** — none run.
