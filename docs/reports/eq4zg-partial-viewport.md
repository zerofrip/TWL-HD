<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4zg — partial viewport (32,96)-(159,191): hardware vs DeSmuME vs our render

**Status:** OFFLINE ANALYSIS of committed data, one DeSmuME harness run, one DeSmuME v2 capture and one Azahar-host run (§3); no console access; no production source change (`arm9v2/`, `arm11/`, `common/`, `arm7/`, `tools/azahar_host/` untouched). Experiment 3 of `docs/reports/eq4ze-hw-coverage.md` §4; one DS capture session (three captures, capture `a` used by the a-priori rule). Labels: **FACT** = full command + raw output; **SOURCE-READ** = quoted file/line or report number; **EMULATOR** = DeSmuME/Azahar; **HYPOTHESIS**/**UNVERIFIED** = not measured/inferred.

**Question and answer.** The ROM draws scene 4 into the partial DS viewport `(32,96)-(159,191)` (y from the bottom): does our arm11/PICA render keep the non-background region inside it, or does geometry bleed because the renderer scales into the viewport without a scissor? No bleed: all three arms have **0** non-background pixels outside the derived viewport rectangle, and the non-background region is confined to the viewport bytes. `ds.frac_within_flat` is `0.9999` (hardware vs harness) and `1.0000` (hardware vs ours), both above 0.95. Both comparison runs still exit 1 on `bbox_dev_top=6` (tol 3): consistent with the alpha-0 quad's hardware-only outline recorded as the whole scene-4 bbox difference in EQ-4r §7 (HYPOTHESIS here), which neither DeSmuME nor our interpreter draws; the other three bbox edges deviate 0..2 DS px. This scene's projected geometry stays inside the viewport, so the no-scissor concern is not exercised; larger geometry remains UNVERIFIED. Hardware, the DeSmuME harness reference and our render are compared on the same frame.

## 1. Inputs, capture rule and noise (FACT; OWNER-REPORTED console work)

```
$ HWSTAGE=<staging> cat "$HWSTAGE/hw/SHA256SUMS" > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
8d108cea597d40a8ade862573b039c24892c33d9bd715990519c29ef960a6f33  hw/hw_vp_s4_a.png
5642f8b1aee33dcad38655404a9acac486342464dd8d3132cf8ebcc992c3f1de  hw/hw_vp_s4_b.png
51a18d0d5c037078a304c1f63048bae2de496b2de4bdd80269c89996fbc3e057  roms/twlhd-arm9v2-vp-autodump-s4-st30.nds
439e121711140d73054de34ba776a19e67e982db2e5867f06ddf1d2c9c9c553b  roms/twlhd-arm9v2-vp-s4-st30.nds
$ sha256sum tools/fixtures/eq4_hw_vp/*.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
1a21e797c34b4ed1b9920ba21bc5aeae23762fc92028e85deeedffe86ccfec3b  tools/fixtures/eq4_hw_vp/harness_vp_s4_st30_f240.png
8d108cea597d40a8ade862573b039c24892c33d9bd715990519c29ef960a6f33  tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png
52be0c6fa49f08e13fb3db4327d1f34b1383898a58c75f903d69711b888dd188  tools/fixtures/eq4_hw_vp/ours_vp_s4_st30_top400x240.png
$ HWSTAGE=<staging> cmp "$HWSTAGE/hw/hw_vp_s4_a.png" tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
```

`exit=0` with no `cmp` output proves the committed hardware PNG is byte-identical to the staged capture. The a-priori rule (fixed before any comparison number): capture `a` is the comparison capture; `b` is used only for the noise count:

```
$ HWSTAGE=<staging> python3 tools/eq4_hw_vp/measure_vp.py --diff "$HWSTAGE/hw/hw_vp_s4_b.png" tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_vp
viewport_ds=x1=32 y1=96 x2=159 y2=191 (inclusive bytes; y from bottom)
background_rule=max_channel<=24
mode=capture
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
derivation=ds_top=192-(y2+1)=0..192-(y1+1)=95; capture_x=40+1.25*ds_x; capture_y=0+1.25*(192-ds_y_from_bottom)
viewport_rect=80,0,239,119
nonbg_pixels=1072
bbox=131,38,179,80
outside_viewport_pixels=0
outside_viewport_bbox=none
diff_other=<staging>/hw/hw_vp_s4_b.png
diff_pixels=33
diff_bbox=132,38,177,76
```

Measured: `33` differing pixels, all inside the figure bbox (`132,38,177,76`). The run staged only `a` and `b`; the third capture is not present under `<staging>`, so the owner's "a and the third capture are byte-identical" is OWNER-REPORTED and not re-checked here. The noise origin is **UNVERIFIED** (capture board, scaler or console not isolated; captures not time-stamped).

## 2. Harness reference (EMULATOR, FACT; harness run 1, out-of-tree harness of `docs/reports/eq4k-edge-rows.md` §3, invoked by bare name, path not shown; frame 242 = ROM frame 240 per `docs/reports/eq4r-static-scenes-hw-compare.md` §3)

```
$ HWSTAGE=<staging> harness "$HWSTAGE/roms/twlhd-arm9v2-vp-autodump-s4-st30.nds" 242 --out <logfile>.txt --ppm <logfile>.ppm > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 4 polygons, 16 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 78FF702E
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
BackupDevice: DeSmuME .dsv save file not found. Trying to load a .sav file.
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ sha256sum <logfile>.ppm > <logfile>.sha 2>&1; echo "exit=$?"; cat <logfile>.sha
exit=0
0c81d42386fb5835c3c7f175312e97f20b656858250fc1cdc85aec03d319de99  <logfile>.ppm
$ sha256sum <logfile>.png > <logfile>.png.sha 2>&1; echo "exit=$?"; cat <logfile>.png.sha
exit=0
1a21e797c34b4ed1b9920ba21bc5aeae23762fc92028e85deeedffe86ccfec3b  <logfile>.png
```

The committed `harness_vp_s4_st30_f240.png` is that lossless PPM conversion; the block below is a second run of it from the same PPM, to a fresh scratch PNG (not the fixture), and the printed sha256 equals the fixture's:

```
$ cat <logfile>.ppm2png.py
import sys, hashlib
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
im.save(sys.argv[2])
print("png %dx%d sha256 %s" % (*im.size, hashlib.sha256(open(sys.argv[2], "rb").read()).hexdigest()))
$ python3 -I <logfile>.ppm2png.py <logfile>.ppm <logfile>.rerun.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
png 256x192 sha256 1a21e797c34b4ed1b9920ba21bc5aeae23762fc92028e85deeedffe86ccfec3b
```

## 3. Our render arm (EMULATOR; FACT; HEAD state at `git rev-parse HEAD` below, empty `status` on `arm11/ common/ arm7/ arm9v2/`, `*.3dsx` gitignored)

```
$ { git rev-parse HEAD; git status --porcelain -- arm11 common arm7 arm9v2; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
0824c3ade0af281f9972b7aeb65ba2bbb15a8278
```

Step 1, the one `arm11` build (FACT; the two `-Wmaybe-uninitialized` blocks are the same blocks as EQ-4zf §3):

```
$ make -C arm11 > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
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
In function 'validate_tex',
    inlined from 'twlhd_v2_parse' at <worktree>/arm11/../common/twlhd_v2_parse.c:650:18:
<worktree>/arm11/../common/twlhd_v2_parse.c:379:12: warning: 'texel_bytes' may be used uninitialized [-Wmaybe-uninitialized]
  379 |         if (rec.texel_size != texel_bytes ||
      |            ^
<worktree>/arm11/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
<worktree>/arm11/../common/twlhd_v2_parse.c:372:18: note: 'texel_bytes' was declared here
  372 |         uint32_t texel_bytes;
      |                  ^~~~~~~~~~~
In function 'validate_tex',
    inlined from 'twlhd_v2_parse' at <worktree>/arm11/../common/twlhd_v2_parse.c:650:18:
<worktree>/arm11/../common/twlhd_v2_parse.c:381:61: warning: 'index_bytes' may be used uninitialized [-Wmaybe-uninitialized]
  379 |         if (rec.texel_size != texel_bytes ||
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  380 |             !twlhd_v2_tex_palette_size_ok(rec.teximage_param,
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  381 |                                           rec.palette_size) ||
      |                                           ~~~~~~~~~~~~~~~~~~^~
  382 |             rec.index_size != index_bytes) {
      |             ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
<worktree>/arm11/../common/twlhd_v2_parse.c: In function 'twlhd_v2_parse':
<worktree>/arm11/../common/twlhd_v2_parse.c:373:18: note: 'index_bytes' was declared here
  373 |         uint32_t index_bytes;
      |                  ^~~~~~~~~~~
twlhd_v2_write.c
twlhd_vertex_convert.c
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make: Leaving directory '<worktree>/arm11'
$ sha256sum arm11/twlhd-arm11.3dsx > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
d9fdb54c275bfb64dfea2d3ff45a9b70cc1cab0b5437f33e46346ce180db1dee  arm11/twlhd-arm11.3dsx
```

Step 2, the one v2 capture (`DESMUME_CLI` set to the desmume-cli of `docs/reports/eq4m-pica-edge-rows.md` §1.4, path not shown; the script's `set -x` trace is the full output):

```
$ HWSTAGE=<staging> DESMUME_TIMEOUT=25 tools/emulator/desmume_capture.sh "$HWSTAGE/roms/twlhd-arm9v2-vp-autodump-s4-st30.nds" <logfile>.cap > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
+ mkdir -p <logfile>.cap
+ image=<logfile>.cap/cflash.img
+ log=<logfile>.cap/desmume.log
+ fatdir=<logfile>.cap/fat
+ rm -f <logfile>.cap/cflash.img <logfile>.cap/desmume.log <logfile>.cap/twlhd_frame_v2.bin
+ rm -rf <logfile>.cap/fat
+ mkfs.fat -C <logfile>.cap/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=3131332
+ sleeper_pid=3131333
+ grace_pid=
+ emulator_killed_by_script=0
+ trap cleanup EXIT INT TERM
+ sleep 25
+ set +e
+ wait -n 3131332 3131333
+ first_status=0
+ set -e
+ kill -0 3131332
+ kill -0 3131333
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 3131332
+ grace_pid=3133079
+ set +e
+ wait -n 3131332 3133079
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 3131332
+ kill -0 3133079
+ kill -TERM 3133079
+ wait 3133079
+ true
+ grace_pid=
+ set +e
+ wait 3131332
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' <logfile>.cap/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' <logfile>.cap/desmume.log
+ 7z x -y <logfile>.cap/cflash.img -o<logfile>.cap/fat
+ '[' '!' -f <logfile>.cap/fat/twlhd_frame_v2.bin ']'
+ cp <logfile>.cap/fat/twlhd_frame_v2.bin <logfile>.cap/twlhd_frame_v2.bin
+ echo 'capture: <logfile>.cap/twlhd_frame_v2.bin'
capture: <logfile>.cap/twlhd_frame_v2.bin
$ sha256sum <logfile>.cap/twlhd_frame_v2.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
f917aa858459e376b4aa1eb4455d751cd9bad95fa96ba6b98029e84ecbd7bfb3  <logfile>.cap/twlhd_frame_v2.bin
```

Step 3, stage the frame as `sdmc:/twlhd_frame.bin`; the host build `make azahar-host` is an unquoted run (§8 run counts):

```
$ mkdir -p <logfile>.sd && cp <logfile>.cap/twlhd_frame_v2.bin <logfile>.sd/twlhd_frame.bin && sha256sum <logfile>.sd/twlhd_frame.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
f917aa858459e376b4aa1eb4455d751cd9bad95fa96ba6b98029e84ecbd7bfb3  <logfile>.sd/twlhd_frame.bin
```

Step 4, the one host run. `azahar_libretro.so` is the out-of-tree core recorded in `docs/reports/eq4m-pica-edge-rows.md` §2.2, invoked by bare file name (path not shown). EXCERPT (not FACT): 463 log lines; 24 distinct host lines quoted, 439 omitted (200 identical `software framebuffer 400x480` lines and 239 timestamped `[azahar_libretro]` core-log lines; the 24 quoted are every non-repeating host line):

```
$ tools/azahar_host/build/azahar_host --core azahar_libretro.so --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <logfile>.azahar --sd-root <logfile>.sd > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
[azahar_host] isolated environment: <logfile>.tmp
[azahar_host] save dir <logfile>.tmp/save; system dir <logfile>.tmp/system
[azahar_host] staged --sd-root <logfile>.sd as sdmc:/ (<logfile>.tmp/save/Azahar/sdmc)
[azahar_host] core option definitions accepted
[azahar_host] core API version 1
[azahar_host] core Azahar 9c38437
[azahar_host] core option citra_use_cpu_jit=disabled
[azahar_host] core option citra_region_value=Auto
[azahar_host] core option citra_language_value=English
[azahar_host] core option citra_graphics_api=Software
[azahar_host] core option citra_use_disk_shader_cache=disabled
[azahar_host] core option citra_resolution_factor=1
[azahar_host] core option citra_layout_option=default
[azahar_host] core option citra_swap_screen=Top
[azahar_host] core option citra_use_virtual_sd=enabled
[azahar_host] core option citra_use_libretro_save_path=LibRetro Default
[azahar_host] save directory: <logfile>.tmp/save
[azahar_host] geometry 400x480
[azahar_host] pixel format XRGB8888 accepted
[azahar_host] geometry 400x480
[azahar_host] core option values accepted (10 lookups answered)
[azahar_host] av_info geometry 400x480 @ 60.000 fps
[azahar_host] wrote <logfile>.azahar_199.ppm (400x480)
[azahar_host] wrote <logfile>.azahar_last.ppm (400x480)
$ sha256sum <logfile>.azahar_199.ppm > <logfile>.sha 2>&1; echo "exit=$?"; cat <logfile>.sha
exit=0
0bbd7826204021d4c7b48a25941446ac54121b4f0af2d6c0407c8778e65057a8  <logfile>.azahar_199.ppm
```

Step 5, the `(0,0,400,240)` PIL crop on that PPM (script at `<logfile>.crop.py`, the eq4m §2.3 crop shape):

```
$ cat <logfile>.crop.py
import sys
import hashlib
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
print("size %dx%d" % im.size)
print("full frame pixel sha256 %s" % hashlib.sha256(im.tobytes()).hexdigest())
top = im.crop((0, 0, 400, 240))
top.save(out)
print("top400x240 sha256 %s" % hashlib.sha256(top.tobytes()).hexdigest())
$ python3 -I <logfile>.crop.py <logfile>.azahar_199.ppm <logfile>.top400x240.png > <logfile>.crop 2>&1; echo "exit=$?"; cat <logfile>.crop
exit=0
size 400x480
full frame pixel sha256 35b597b78ca55716f43b0cf105f07110e2eb7111774b361d025bb1701022f6a4
top400x240 sha256 3a6337f0d669791563137cdb0e0c5902a1f3df744e543d84478f2288dc63a308
$ sha256sum <logfile>.top400x240.png > <logfile>.png.sha 2>&1; echo "exit=$?"; cat <logfile>.png.sha
exit=0
52be0c6fa49f08e13fb3db4327d1f34b1383898a58c75f903d69711b888dd188  <logfile>.top400x240.png
```

## 4. Comparisons (FACT; `tools/eq4_hw_compare.py` run once per pair; exit 0 PASS, 1 FAIL, 2 input error; first block hardware vs harness, correction `auto`, hardware and DeSmuME images never corrected; second block hardware vs ours, `--azahar-correction on` forced, the presentation-row correction applying to the Azahar reference only)

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png --reference <logfile>.ppm > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=1
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.4185
ds.mean_abs_g=0.4176
ds.mean_abs_b=0.5530
ds.max_error=255.0000
ds.frac_within=0.9911
ds.frac_within_flat=0.9999
ds.frac_within_edge=0.4011
ds.flat_pixels=48424
ds.edge_pixels=728
capture.mean_abs_r=0.4169
capture.mean_abs_g=0.4066
capture.mean_abs_b=0.5427
capture.max_error=255.0000
capture.frac_within=0.9919
capture.frac_within_flat=0.9999
capture.frac_within_edge=0.3889
capture.flat_pixels=75792
capture.edge_pixels=1008
ref_nonbg_pixels=490
ref_bbox_left=74
ref_bbox_top=36
ref_bbox_right=111
ref_bbox_bottom=64
capture_nonbg_pixels=762
capture_bbox_left=72
capture_bbox_top=30
capture_bbox_right=111
capture_bbox_bottom=64
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=160
class_red_ref_centroid_x=85.2375
class_red_ref_centroid_y=57.4937
class_red_capture_area=152
class_red_capture_centroid_x=84.3289
class_red_capture_centroid_y=56.5263
class_green_ref_area=106
class_green_ref_centroid_x=104.5566
class_green_ref_centroid_y=51.1038
class_green_capture_area=127
class_green_capture_centroid_x=103.8898
class_green_capture_centroid_y=49.8819
class_blue_ref_area=224
class_blue_ref_centroid_x=83.5446
class_blue_ref_centroid_y=43.7411
class_blue_capture_area=269
class_blue_capture_centroid_x=82.5539
class_blue_capture_centroid_y=42.6840
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=2
bbox_dev_top=6
bbox_dev_right=0
bbox_dev_bottom=0
verdict=FAIL
reason=bbox-top deviates 6 DS px (tol 3)
```

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png --reference <logfile>.azahar_199.ppm --azahar-correction on > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=1
presentation-row correction: applied (--azahar-correction on (forced))
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.2543
ds.mean_abs_g=0.3012
ds.mean_abs_b=0.4198
ds.max_error=255.0000
ds.frac_within=0.9922
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.4311
ds.flat_pixels=48477
ds.edge_pixels=675
capture.mean_abs_r=0.2106
capture.mean_abs_g=0.2751
capture.mean_abs_b=0.4087
capture.max_error=255.0000
capture.frac_within=0.9938
capture.frac_within_flat=0.9999
capture.frac_within_edge=0.4642
capture.flat_pixels=75919
capture.edge_pixels=881
ref_nonbg_pixels=483
ref_bbox_left=73
ref_bbox_top=36
ref_bbox_right=110
ref_bbox_bottom=63
capture_nonbg_pixels=762
capture_bbox_left=72
capture_bbox_top=30
capture_bbox_right=111
capture_bbox_bottom=64
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=160
class_red_ref_centroid_x=84.0187
class_red_ref_centroid_y=56.7250
class_red_capture_area=152
class_red_capture_centroid_x=84.3289
class_red_capture_centroid_y=56.5263
class_green_ref_area=104
class_green_ref_centroid_x=103.9615
class_green_ref_centroid_y=50.2212
class_green_capture_area=127
class_green_capture_centroid_x=103.8898
class_green_capture_centroid_y=49.8819
class_blue_ref_area=219
class_blue_ref_centroid_x=82.5525
class_blue_ref_centroid_y=42.7032
class_blue_capture_area=269
class_blue_capture_centroid_x=82.5539
class_blue_capture_centroid_y=42.6840
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=6
bbox_dev_right=1
bbox_dev_bottom=1
verdict=FAIL
reason=bbox-top deviates 6 DS px (tol 3)
```

## 5. Viewport rectangle and non-background census (FACT; `tools/eq4_hw_vp/measure_vp.py`)

Derivation (mapping from `tools/ds_vtxcolor_test/capture_mapping.json`, scale 1.25, offset x=40): `capture_x = 40 + 1.25*ds_x`; `capture_y = 1.25*(192 - ds_y_from_bottom)`. The viewport bytes are inclusive, so `x` covers DS `[32,160)` and `y_from_bottom` covers `[96,192)`, i.e. DS top-down rows `[0,96)`. In capture pixels that is `x=40+1.25*32=80` to `40+1.25*160-1=239`, and `y=1.25*0=0` to `1.25*96-1=119`: `viewport_rect=80,0,239,119`. Native 256x192 images use the identity mapping: `32,0,159,95`. The harness and the hardware both place the figure in the top half, which cross-checks the y-from-bottom flip (the other half would give non-zero outside counts for both).

```
$ python3 tools/eq4_hw_vp/measure_vp.py tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_vp
viewport_ds=x1=32 y1=96 x2=159 y2=191 (inclusive bytes; y from bottom)
background_rule=max_channel<=24
mode=capture
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
derivation=ds_top=192-(y2+1)=0..192-(y1+1)=95; capture_x=40+1.25*ds_x; capture_y=0+1.25*(192-ds_y_from_bottom)
viewport_rect=80,0,239,119
nonbg_pixels=1072
bbox=131,38,179,80
outside_viewport_pixels=0
outside_viewport_bbox=none
$ python3 tools/eq4_hw_vp/measure_vp.py tools/fixtures/eq4_hw_vp/harness_vp_s4_st30_f240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_vp
viewport_ds=x1=32 y1=96 x2=159 y2=191 (inclusive bytes; y from bottom)
background_rule=max_channel<=24
mode=native
image=256x192
mapping=identity (256x192 native DS framebuffer)
derivation=ds_top=192-(y2+1)=0..192-(y1+1)=95; native rect=x1..x2, ds_top..ds_bottom
viewport_rect=32,0,159,95
nonbg_pixels=490
bbox=74,36,111,64
outside_viewport_pixels=0
outside_viewport_bbox=none
$ python3 tools/eq4_hw_vp/measure_vp.py tools/fixtures/eq4_hw_vp/ours_vp_s4_st30_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_vp
viewport_ds=x1=32 y1=96 x2=159 y2=191 (inclusive bytes; y from bottom)
background_rule=max_channel<=24
mode=capture
image=400x240
capture_mapping=scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
derivation=ds_top=192-(y2+1)=0..192-(y1+1)=95; capture_x=40+1.25*ds_x; capture_y=0+1.25*(192-ds_y_from_bottom)
viewport_rect=80,0,239,119
nonbg_pixels=762
bbox=132,46,178,81
outside_viewport_pixels=0
outside_viewport_bbox=none
```

## 6. Three-way numbers and the eq4ze §4 exp.3 criterion

DS coordinates below convert the capture bboxes with the same mapping: `ds_x=(capture_x-40)/1.25`, `ds_y_from_bottom=192-capture_y/1.25` (INFERRED at the 1.25x grid; the harness column is native).

| quantity | hardware (capture `a`) | DeSmuME harness | ours (Azahar) |
|---|---|---|---|
| image | 400x240 crop | 256x192 native | 400x240 crop |
| `nonbg_pixels` | 1072 | 490 | 762 |
| capture bbox (l,t,r,b) | 131,38,179,80 | n/a (native) | 132,46,178,81 |
| native bbox (l,t,r,b) | n/a | 74,36,111,64 | n/a |
| DS x (left..right) | 72.8..111.2 | 74..111 | 73.6..110.4 |
| DS y from bottom | 128.0..161.6 | 128..156 | 127.2..155.2 |
| `outside_viewport_pixels` | 0 | 0 | 0 |
| `ds.frac_within_flat` vs hardware | — | 0.9999 | 1.0000 |
| `bbox_dev` (l/t/r/b) vs hardware | — | 2/6/0/0 | 1/6/1/1 |
| tool verdict | — | FAIL | FAIL |

Applying the criterion honestly: `ds.frac_within_flat >= 0.95` PASS for both pairs (`0.9999`, `1.0000`); every `bbox_dev <= 3` FAIL for both pairs, `bbox_dev_top=6` in each, the other three edges 0..2 (harness) and 1 (ours); non-background region confined to viewport bytes `x 32..159 / y 96..191` PASS for all three arms (DS terms: hardware x `72.8..111.2`, y from bottom `128.0..161.6`; harness `74..111` / `128..156`; ours `73.6..110.4` / `127.2..155.2`; capture terms: each `outside_viewport_pixels=0` against the `80,0,239,119` rectangle); **bleed of our render outside the viewport** none measured (`outside_viewport_pixels=0`), and the same is true of hardware and harness.

**Why `bbox_dev_top=6` is not read as viewport placement.** The three bbox tops, one labelled unit — DS top-down rows: hardware 30 (capture 38), harness 36 (native), ours 36 (capture 46); the tool compares `capture_bbox_top=30` against `ref_bbox_top=36`, and the non-top deviations are 0..2 DS px in both tool runs. SOURCE-READ, `docs/reports/eq4r-static-scenes-hw-compare.md` §7: "**DIFFERENT (s4): the hardware draws the alpha-0 quad's outline.** ... Neither reference has an outline pixel ... HYPOTHESIS: on the DS alpha 0 selects outline drawing, which neither DeSmuME nor our interpreter implements; this is the whole s4 failure". HYPOTHESIS for this capture: the 6 DS px top deviation is that hardware-only outline, not a different viewport mapping; this scene does not isolate an outline pixel independently, so the attribution stays a hypothesis.

## 7. Verdict, scope and proposal (text only, not applied)

* **Verdict.** For ONE console, ONE capture session, scene 4, `STATIC_ANGLE=30`, viewport `(32,96)-(159,191)` only: no arm bleeds outside the viewport (`outside_viewport_pixels=0` for hardware, harness and ours), the non-background region is confined to the viewport bytes, the tool verdicts are FAIL solely on `bbox_dev_top=6` (consistent with the known scene-4 alpha-0 outline, HYPOTHESIS, not isolated here; left/right/bottom deviations 0..2 DS px), and the design concern (renderer scales into the viewport without a scissor) is **not exercised** because this scene's projected geometry, including the outline, lies inside the viewport rectangle. **Bleed risk with larger geometry.** UNVERIFIED: only this viewport and this scene were measured; geometry that projects outside the DS clip volume could map outside the viewport rectangle, and neither a hardware capture nor our render was exercised for it. **UNVERIFIED, explicitly.** Other viewport sizes/positions, scissor behaviour with larger geometry, and the stray bottom-row pixel seen in earlier arm11 captures (absent from the top 400x240 crop measured here by construction).
* **Proposal (text only; NOT applied).** No fix is indicated by this scene because no bleed was measured. If a future larger-geometry scene shows our render drawing outside the viewport while hardware does not, the smallest fix is a PICA scissor at the two clip-scene staging sites in `arm11/source/renderer.c` (`renderer_set_clip_scene` near the `twlhd_clipfix_matrix` call at `:985`, and `renderer_set_clip_scene_textured` near `:1095`), using the same `TwlhdGxViewport` already in hand, mapped to the 400x240 target in C3D scissor state; this would revise the viewport decision recorded at `docs/PHASE5A_V2_RENDERER_DESIGN.md:48-49` ("Implemented inside `clipfix` ... not with PICA scissor state"). Verification plan: same scene with geometry larger than the viewport, one new capture, `python3 tools/eq4_hw_vp/measure_vp.py` on that capture with `outside_viewport_pixels=0`, and the hardware/harness comparison unchanged. **Caveat (SOURCE-READ):** the DS viewport is a coordinate transform; the design note does not say hardware clips to the viewport rectangle, so the scissor must be justified by a hardware capture that shows confinement, not assumed from this scene.

## 8. Fixtures, tests, pointers and size

Fixtures `tools/fixtures/eq4_hw_vp/` (each < 100 KB; SHA-256 in §1 and in the fixture README; no harness/DeSmuME code or ROM committed): hardware crop `a` (byte-identical to `<staging>/hw/hw_vp_s4_a.png`), harness PNG (lossless PPM conversion), ours crop (crop of the Azahar PPM). The fixture README records provenance, ROM hashes and the build/frame hashes.

```
$ wc -c tools/fixtures/eq4_hw_vp/*.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
 461 tools/fixtures/eq4_hw_vp/harness_vp_s4_st30_f240.png
1883 tools/fixtures/eq4_hw_vp/hw_vp_s4_a_top400x240.png
 583 tools/fixtures/eq4_hw_vp/ours_vp_s4_st30_top400x240.png
2927 total
```

New tests `tools/eq4_hw_vp/test_measure_vp.py` (rect derivation `80,0,239,119` / native `32,0,159,95`; synthetic inside/crossing/native/blank/diff cases; fixture SHA-256 pinning and the three measured fixture blocks; negative size/usage exits), wired into `make test` via `test-eq4zg-vp`:

```
$ make test-eq4zg-vp > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4_hw_vp -p 'test_*.py'
............
----------------------------------------------------------------------
Ran 12 tests in 1.249s

OK
```

Append-only pointers added (blank line before each): `docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ_SCENES.md`, and `docs/reports/eq4ze-hw-coverage.md` §4 exp.3. Run counts: harness 1, v2 capture 1, `make -C arm11` 1, `make azahar-host` 1 (unquoted), `azahar_host` 1, PPM->PNG conversion 2 (the quoted block is the second run), crop 1, `eq4_hw_compare.py` 2 (one per pair), `measure_vp.py` 4 (three images once each plus the a/b noise run), `cmp` 2 (the quoted block is the second run); the new test suite has 12 tests and reads the fixtures in its fixture class. Root `make test` also passes. Size audit after the final edit:

```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zg-partial-viewport.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
56729
558 docs/reports/eq4zg-partial-viewport.md
```
