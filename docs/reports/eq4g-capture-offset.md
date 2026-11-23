<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4g — capture-offset fit: mapping error or real shift? — 2026-11-23

**Scope.** Offline analysis only: no hardware action, no console access, and no
change to any existing tool, test or mapping. Added: `tools/ds_scale_test/
fit_mapping.py`, `tools/test_fit_mapping.py`, the small wrapper
`tools/eq4g_fit_compare.py` and this report; the root `Makefile` gains only the
`test-fit-mapping` target and its `test` plumbing required by the task (§7).
`tools/ds_vtxcolor_test/
capture_mapping.json`, `tools/eq4_hw_compare.py` (behaviour, default mapping,
tolerances), `measure_scale.py`, `eq4_centroid_deltas.py`, `eq3_compare.py`
and the existing tests are untouched (§8).

**Question.** The recorded hardware captures sit about 1-2 DS px off both
references — `dy` negative in all quoted class pairs, `dx` mixed
(`docs/reports/eq4f-scene5-static-hw-compare.md` §5(ii)). Is that offset
produced by the capture mapping
DS (x,y) -> capture (40+1.25x, 1.25y), or is it a real shift of the captured
picture?

**Labels.** FACT = a command and its complete raw output quoted verbatim
below. HYPOTHESIS / UNVERIFIED = not shown by the quoted output. EVERY number
here carries **basis=one console, one capture, one scene pattern**; the edge
fit itself is **one console, one capture, one pattern ROM**.

## 1. Method (fit_mapping.py)

**FACT (source):** `tools/ds_scale_test/fit_mapping.py`, docstring and code.
The method required by the task is implemented exactly as follows.

- **Coordinates** are pixel-corner coordinates on both sides: the boundary
  between DS pixels d-1 and d is at DS coordinate d; the boundary between
  capture pixels p-1 and p is at capture coordinate p; capture pixel p has its
  centre at p+0.5. The model per axis is `capture_edge = a*ds_edge + b`.
- **Edge inventory** from `pattern.json`: both sides of the 1-px frame
  (outer where it meets the pillar bars, inner boundary of the border), both
  sides of the 1-px centre cross, all 255 hstrip column boundaries and all 191
  vstrip row boundaries, the 15 gradient step boundaries, and the two vertical
  and two horizontal sides of each of the four corner rects.
- **Profiles:** for a vertical edge the profile is the mean R,G,B luminance of
  each capture column, averaged over the edge's parallel span; a horizontal
  edge uses the mean of each capture row. The parallel span is chosen inside
  the element and is only used when the pattern colour on each side is
  constant along the whole span (DS coordinates outside 256x192 count as the
  pattern background).
- **Plateau levels:** the two levels are the pattern's own rendered colours of
  the two DS pixels flanking the edge (5-bit round trip applied exactly as the
  ROM displays them). Edges whose levels differ by less than 6 are skipped.
- **Edge position:** the capture coordinate where the profile crosses the
  50 % level `(high+low)/2`, by **linear interpolation between the two capture
  pixels that straddle the crossing**: `x = i + 0.5 + (mid-p[i])/(p[i+1]-p[i])`.
  The crossing must be in the expected direction, unique in a +/-5 capture px
  window, and each plateau level must actually be observed on its own side
  (within 12 % of the level difference, with a floor of 6 luminance levels).
  Periodic 1-px probes show several crossings and are rejected; the 1-px
  frame/cross/corner edges stay usable.
- **Fit:** ordinary least squares per axis over all accepted observations,
  `a = sum((d-dbar)(c-cbar)) / sum((d-dbar)^2)`, `b = cbar - a*dbar`, with the
  RMS residual and the standard errors of `a` and `b` (from the residuals)
  printed. Observations are sorted by `(axis, ds, name)`, so the
  output is deterministic.
- **Difference to the current mapping** at a screen position `pos` is printed
  in DS px as `delta_ds(pos) = ((a-1.25)*pos + (b-offset))/a` at the centre
  (128,96) and at the corners (0,0) and (256,192).

## 2. Inputs (FACT)

Quoted command and complete output:

```
$ sha256sum tools/ds_scale_test/pattern.json tools/ds_vtxcolor_test/capture_mapping.json tools/ds_scale_test/fixtures/sc4_3_top400x240.png tools/fixtures/eq4_hw/st0_3_top400x240.png tools/fixtures/eq4_hw/st30_3_top400x240.png tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
81d7a2010e174fa4117a600a580e4371fcfec3bc16fc9822eee0984cfdfc391d  tools/ds_scale_test/pattern.json
5b7bbb4604cb8181e9e08b36a116c7084da7683ef5ff02daebe890e5ad199d33  tools/ds_vtxcolor_test/capture_mapping.json
f7ad03e17a82ba03a03d210b440ba31490654029c21f50e899ec096f8cd198d5  tools/ds_scale_test/fixtures/sc4_3_top400x240.png
d7fca639bba85e2cfa02812ed9ef86d625a653f9c4d9c658a9e4b0daee1cbe99  tools/fixtures/eq4_hw/st0_3_top400x240.png
b50d97f2c6776c801c37476d429ebf38129eef8996120989e86aa8c5131ada04  tools/fixtures/eq4_hw/st30_3_top400x240.png
7ec6d8c09b992da14b3cf5a48b5313360b904c16e043550b735c09c3effd8ce3  tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png
85e97da974748e175657f86f9632dc061c55807af0f5553e3a6c1311323e9f34  tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png
```

The mapping under test is the shared one (read, never modified):

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

## 3. Fit on the real ds-scale-test capture (FACT)

Command and **complete stdout** (per-edge DS/capture position and residual,
then the per-axis fit and difference to the current mapping):

```
$ python3 tools/ds_scale_test/fit_mapping.py tools/ds_scale_test/fixtures/sc4_3_top400x240.png
axis=x edge=frame_left_outer ds=0.0000 capture=40.0026 residual=0.0695
axis=x edge=corner_bl_left ds=1.0000 capture=41.0914 residual=-0.0922
axis=x edge=corner_tl_left ds=1.0000 capture=41.0822 residual=-0.1014
axis=x edge=frame_left_inner ds=1.0000 capture=41.0923 residual=-0.0912
axis=x edge=corner_bl_right ds=9.0000 capture=51.0985 residual=-0.0882
axis=x edge=corner_tl_right ds=9.0000 capture=51.0890 residual=-0.0976
axis=x edge=gradient_16 ds=16.0000 capture=60.0000 residual=0.0607
axis=x edge=gradient_32 ds=32.0000 capture=80.0265 residual=0.0811
axis=x edge=gradient_48 ds=48.0000 capture=100.0368 residual=0.0853
axis=x edge=gradient_64 ds=64.0000 capture=120.0369 residual=0.0792
axis=x edge=gradient_80 ds=80.0000 capture=140.0252 residual=0.0614
axis=x edge=gradient_96 ds=96.0000 capture=160.0363 residual=0.0663
axis=x edge=gradient_112 ds=112.0000 capture=180.0472 residual=0.0711
axis=x edge=cross_v_left ds=128.0000 capture=199.8805 residual=-0.1016
axis=x edge=gradient_128 ds=128.0000 capture=200.0232 residual=0.0410
axis=x edge=cross_v_right ds=129.0000 capture=201.1195 residual=-0.1131
axis=x edge=gradient_144 ds=144.0000 capture=220.0355 residual=0.0472
axis=x edge=gradient_160 ds=160.0000 capture=240.1203 residual=0.1259
axis=x edge=gradient_176 ds=176.0000 capture=260.0391 residual=0.0385
axis=x edge=gradient_192 ds=192.0000 capture=280.0523 residual=0.0456
axis=x edge=gradient_208 ds=208.0000 capture=300.0489 residual=0.0362
axis=x edge=gradient_224 ds=224.0000 capture=320.0523 residual=0.0334
axis=x edge=gradient_240 ds=240.0000 capture=340.0609 residual=0.0359
axis=x edge=corner_br_left ds=247.0000 capture=348.7157 residual=-0.0620
axis=x edge=corner_tr_left ds=247.0000 capture=348.7128 residual=-0.0649
axis=x edge=corner_br_right ds=255.0000 capture=358.7456 residual=-0.0351
axis=x edge=corner_tr_right ds=255.0000 capture=358.7388 residual=-0.0419
axis=x edge=frame_right_inner ds=255.0000 capture=358.7226 residual=-0.0582
axis=x edge=frame_right_outer ds=256.0000 capture=360.0000 residual=-0.0311
axis=y edge=corner_tl_top ds=1.0000 capture=1.0822 residual=-0.0001
axis=y edge=corner_tr_top ds=1.0000 capture=1.0864 residual=0.0041
axis=y edge=frame_top_inner ds=1.0000 capture=1.0923 residual=0.0100
axis=y edge=corner_tl_bottom ds=9.0000 capture=11.0890 residual=0.0003
axis=y edge=corner_tr_bottom ds=9.0000 capture=11.0869 residual=-0.0018
axis=y edge=cross_h_top ds=96.0000 capture=119.8805 residual=-0.0269
axis=y edge=cross_h_bottom ds=97.0000 capture=121.1195 residual=-0.0387
axis=y edge=vstrip_140 ds=140.0000 capture=175.0197 residual=0.0776
axis=y edge=corner_bl_top ds=183.0000 capture=228.7163 residual=-0.0098
axis=y edge=corner_br_top ds=183.0000 capture=228.7157 residual=-0.0104
axis=y edge=corner_bl_bottom ds=191.0000 capture=238.7246 residual=-0.0078
axis=y edge=corner_br_bottom ds=191.0000 capture=238.7456 residual=0.0132
axis=y edge=frame_bottom_inner ds=191.0000 capture=238.7226 residual=-0.0099
n_x=29 a_x=1.250383 b_x=39.933172 rms_x=0.072352 se_a_x=0.000142 se_b_x=0.022656
n_y=13 a_y=1.250790 b_y=-0.168453 rms_y=0.026147 se_a_y=0.000089 se_b_y=0.011447
delta_ds_centre dx=-0.0143 dy=-0.0741
delta_ds_corner_0_0 dx=-0.0534 dy=-0.1347
delta_ds_corner_256_192 dx=0.0249 dy=-0.0134
mapping_current scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
```

Edges the method rejects (several 50 % crossings in the 1-px probes, no plateau
contrast, non-constant span, or on the crop boundary) are listed on stderr with
their reason. The accepted 42 observations are the 29 x and 13 y lines printed
above: 6 frame sides, 4 cross sides, 15 gradient step boundaries, 16 corner-rect
sides and the strip row `vstrip_140`.

## 4. Results (FACT, from the quoted output)

| axis | observations | a (capture px / DS px) | b (capture px) | RMS residual (capture px) |
|---|---|---|---|---|
| x | 29 | 1.250383 | 39.933172 | 0.072352 |
| y | 13 | 1.250790 | -0.168453 | 0.026147 |

Difference to the current mapping `(1.25, 40, 1.25, 0)`, in DS px (quoted
`delta_ds_*` lines; negative = the fitted mapping places the content left/up
of the current mapping):

| screen position (DS) | dx (DS px) | dy (DS px) |
|---|---|---|
| centre (128, 96) | -0.0143 | -0.0741 |
| corner (0, 0) | -0.0534 | -0.1347 |
| corner (256, 192) | +0.0249 | -0.0134 |

**FACT:** the fitted mapping differs from the current `(1.25, 40, 1.25, 0)`
by at most 0.135 DS px over the whole screen (largest at the top-left corner,
dy = -0.1347), an order of magnitude below the 1-2 DS px scene5 offset. The
intercepts do differ from `(40, 0)` by more than their standard errors: with
`se_b_x=0.022656` and `se_b_y=0.011447` capture px (quoted above),
`b_x-40=-0.066828` is about 3 standard errors and `b_y=-0.168453` about 15.
The full residual scatter is 0.072 and 0.026 capture px.

## 5. Scene5 cross-check with both mappings (FACT)

`tools/eq4g_fit_compare.py` reuses the **float-safe** pieces of
`tools/eq4_hw_compare.py` (`load_capture`, `load_reference`, `capture_to_ds`,
`compare_pixels`, `nonbg_bbox`, `class_stats`) and prints the same DS-space
keys. It does not call `compare()`, which indexes the capture array with the
offset directly (`eq4_hw_compare.py:423`) and so cannot take a fractional
offset. `--mapping fit` runs `fit_mapping.py` on the ds-scale-test capture
above and keeps the fractional offsets.

The reference PPMs are conversions of the committed PICA renders (the same
conversion as eq4f §2). Commands that create every file the following steps
read (`$` is the shell prompt):

```
$ mkdir -p /tmp/eq4g-refs /tmp/eq4g-out
$ python3 - <<'EOF'
from PIL import Image
for src, dst in (
    ("tools/fixtures/eq4_hw/ours_s5_st0_top400x240.png", "/tmp/eq4g-refs/ours_s5_st0.ppm"),
    ("tools/fixtures/eq4_hw/ours_s5_st30_top400x240.png", "/tmp/eq4g-refs/ours_s5_st30.ppm"),
):
    Image.open(src).convert("RGB").save(dst)
EOF
```

Each wrapper run is redirected to its own `/tmp/eq4g-out/` file; the `cat`
below prints the complete content of all four:

```
$ for a in 0 30; do for m in current fit; do { echo "=== st$a ours $m ==="; python3 tools/eq4g_fit_compare.py --capture tools/fixtures/eq4_hw/st${a}_3_top400x240.png --reference /tmp/eq4g-refs/ours_s5_st${a}.ppm --mapping $m; } > /tmp/eq4g-out/ours_st${a}_${m}.txt; done; done
$ cat /tmp/eq4g-out/ours_st0_current.txt /tmp/eq4g-out/ours_st0_fit.txt /tmp/eq4g-out/ours_st30_current.txt /tmp/eq4g-out/ours_st30_fit.txt
=== st0 ours current ===
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250000
mapping_scale_y=1.250000
mapping_offset_x=40.000000
mapping_offset_y=0.000000
ds.mean_abs_r=12.8123
ds.mean_abs_g=13.0460
ds.mean_abs_b=17.0238
ds.max_error=255.0000
ds.frac_within=0.3366
ds.frac_within_flat=0.9999
ds.frac_within_edge=0.0419
ds.flat_pixels=15122
ds.edge_pixels=34030
ref_nonbg_pixels=7193
capture_nonbg_pixels=8042
ref_bbox_left=59
ref_bbox_top=28
ref_bbox_right=195
ref_bbox_bottom=164
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
=== st0 ours fit ===
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250383
mapping_scale_y=1.250790
mapping_offset_x=39.933172
mapping_offset_y=-0.168453
ds.mean_abs_r=12.6586
ds.mean_abs_g=12.9017
ds.mean_abs_b=16.9035
ds.max_error=254.0000
ds.frac_within=0.3367
ds.frac_within_flat=0.9999
ds.frac_within_edge=0.0409
ds.flat_pixels=15158
ds.edge_pixels=33994
ref_nonbg_pixels=7193
capture_nonbg_pixels=8072
ref_bbox_left=59
ref_bbox_top=28
ref_bbox_right=195
ref_bbox_bottom=164
capture_bbox_left=58
capture_bbox_top=26
capture_bbox_right=195
capture_bbox_bottom=163
class_white_ref_area=2380
class_white_ref_centroid_x=127.0000
class_white_ref_centroid_y=96.0000
class_white_capture_area=2355
class_white_capture_centroid_x=126.0679
class_white_capture_centroid_y=94.3490
class_red_ref_area=1208
class_red_ref_centroid_x=126.9859
class_red_ref_centroid_y=95.6200
class_red_capture_area=1173
class_red_capture_centroid_x=127.3768
class_red_capture_centroid_y=94.5072
class_green_ref_area=1208
class_green_ref_centroid_x=126.9859
class_green_ref_centroid_y=96.3800
class_green_capture_area=1172
class_green_capture_centroid_x=127.6382
class_green_capture_centroid_y=94.4778
class_blue_ref_area=1207
class_blue_ref_centroid_x=127.0141
class_blue_ref_centroid_y=96.8451
class_blue_capture_area=1155
class_blue_capture_centroid_x=127.3532
class_blue_capture_centroid_y=95.4268
class_yellow_ref_area=1190
class_yellow_ref_centroid_x=127.0000
class_yellow_ref_centroid_y=96.0000
class_yellow_capture_area=1166
class_yellow_capture_centroid_x=127.5223
class_yellow_capture_centroid_y=94.1535
bbox_dev_left=1
bbox_dev_top=2
bbox_dev_right=0
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
=== st30 ours current ===
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250000
mapping_scale_y=1.250000
mapping_offset_x=40.000000
mapping_offset_y=0.000000
ds.mean_abs_r=13.5205
ds.mean_abs_g=13.7165
ds.mean_abs_b=17.7399
ds.max_error=255.0000
ds.frac_within=0.3290
ds.frac_within_flat=0.9986
ds.frac_within_edge=0.0484
ds.flat_pixels=14516
ds.edge_pixels=34636
ref_nonbg_pixels=7444
capture_nonbg_pixels=8335
ref_bbox_left=16
ref_bbox_top=17
ref_bbox_right=181
ref_bbox_bottom=162
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
=== st30 ours fit ===
basis=one console, one capture
wrapper=eq4g_fit_compare (DS-space eq4_hw_compare subset)
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.250383
mapping_scale_y=1.250790
mapping_offset_x=39.933172
mapping_offset_y=-0.168453
ds.mean_abs_r=13.3692
ds.mean_abs_g=13.6161
ds.mean_abs_b=17.6304
ds.max_error=255.0000
ds.frac_within=0.3296
ds.frac_within_flat=0.9986
ds.frac_within_edge=0.0482
ds.flat_pixels=14554
ds.edge_pixels=34598
ref_nonbg_pixels=7465
capture_nonbg_pixels=8309
ref_bbox_left=16
ref_bbox_top=17
ref_bbox_right=181
ref_bbox_bottom=162
capture_bbox_left=16
capture_bbox_top=15
capture_bbox_right=181
capture_bbox_bottom=162
class_white_ref_area=3402
class_white_ref_centroid_x=68.3527
class_white_ref_centroid_y=57.5373
class_white_capture_area=3306
class_white_capture_centroid_x=66.7374
class_white_capture_centroid_y=55.5992
class_red_ref_area=1173
class_red_ref_centroid_x=116.2924
class_red_ref_centroid_y=65.0708
class_red_capture_area=1174
class_red_capture_centroid_x=116.1627
class_red_capture_centroid_y=63.2666
class_green_ref_area=981
class_green_ref_centroid_x=130.4363
class_green_ref_centroid_y=72.5708
class_green_capture_area=979
class_green_capture_centroid_x=130.9632
class_green_capture_centroid_y=70.9438
class_blue_ref_area=972
class_blue_ref_centroid_x=134.0525
class_blue_ref_centroid_y=76.4290
class_blue_capture_area=937
class_blue_capture_centroid_x=134.8655
class_blue_capture_centroid_y=74.1601
class_yellow_ref_area=937
class_yellow_ref_centroid_x=123.4749
class_yellow_ref_centroid_y=91.7407
class_yellow_capture_area=876
class_yellow_capture_centroid_x=122.5879
class_yellow_capture_centroid_y=91.4406
bbox_dev_left=0
bbox_dev_top=2
bbox_dev_right=0
bbox_dev_bottom=0
verdict=PASS
reason=within tolerance
```

Per-class centroid deltas (capture minus reference), quoted command and
complete output of the existing read-only helper on those four report files:

```
$ python3 tools/eq4_centroid_deltas.py /tmp/eq4g-out/ours_st0_current.txt /tmp/eq4g-out/ours_st0_fit.txt /tmp/eq4g-out/ours_st30_current.txt /tmp/eq4g-out/ours_st30_fit.txt
/tmp/eq4g-out/ours_st0_current.txt
white dx=-0.9139 dy=-1.8777
red dx=0.3909 dy=-1.1128
green dx=0.6238 dy=-1.8881
blue dx=0.3247 dy=-1.4032
yellow dx=0.5086 dy=-1.8748
/tmp/eq4g-out/ours_st0_fit.txt
white dx=-0.9321 dy=-1.6510
red dx=0.3909 dy=-1.1128
green dx=0.6523 dy=-1.9022
blue dx=0.3391 dy=-1.4183
yellow dx=0.5223 dy=-1.8465
/tmp/eq4g-out/ours_st30_current.txt
white dx=-1.7386 dy=-2.0972
red dx=-0.1902 dy=-1.8289
green dx=0.5879 dy=-1.9839
blue dx=0.9474 dy=-2.5699
yellow dx=-0.8603 dy=-0.4586
/tmp/eq4g-out/ours_st30_fit.txt
white dx=-1.6153 dy=-1.9381
red dx=-0.1297 dy=-1.8042
green dx=0.5269 dy=-1.6270
blue dx=0.8130 dy=-2.2689
yellow dx=-0.8870 dy=-0.3001
```

The per-class change between the two mappings (current -> fit, absolute DS px),
from those same four files, computed by a throwaway script kept outside the
repository:

```
$ python3 /tmp/opencode/eq4g_delta_check.py
st0 changes (current -> fit, absolute DS px):
  white  dx -0.9139 -> -0.9321 = 0.0182 ; dy -1.8777 -> -1.6510 = 0.2267
  red    dx 0.3909 -> 0.3909 = 0.0000 ; dy -1.1128 -> -1.1128 = 0.0000
  green  dx 0.6238 -> 0.6523 = 0.0285 ; dy -1.8881 -> -1.9022 = 0.0141
  blue   dx 0.3247 -> 0.3391 = 0.0144 ; dy -1.4032 -> -1.4183 = 0.0151
  yellow dx 0.5086 -> 0.5223 = 0.0137 ; dy -1.8748 -> -1.8465 = 0.0283
st30 changes (current -> fit, absolute DS px):
  white  dx -1.7386 -> -1.6153 = 0.1233 ; dy -2.0972 -> -1.9381 = 0.1591
  red    dx -0.1902 -> -0.1297 = 0.0605 ; dy -1.8289 -> -1.8042 = 0.0247
  green  dx 0.5879 -> 0.5269 = 0.0610 ; dy -1.9839 -> -1.6270 = 0.3569
  blue   dx 0.9474 -> 0.8130 = 0.1344 ; dy -2.5699 -> -2.2689 = 0.3010
  yellow dx -0.8603 -> -0.8870 = 0.0267 ; dy -0.4586 -> -0.3001 = 0.1585
source: /tmp/eq4g-out/ours_st0_current.txt; /tmp/eq4g-out/ours_st0_fit.txt; /tmp/eq4g-out/ours_st30_current.txt; /tmp/eq4g-out/ours_st30_fit.txt
OVERALL MAX change: 0.3569 DS px (st30 green dy: -1.9839 -> -1.6270)
max dx change: 0.1344 (st30 blue)
max dy change: 0.3569 (st30 green)
```

**FACT — does the offset shrink?** No. Under the fitted mapping the bbox
deviations are unchanged for st0 (1/2/0/1 under both mappings); for st30 only
bottom changes, 1 -> 0. `bbox_dev_top=2` in all four pairs and
`bbox_dev_left` is 0-1. The centroid deltas move by at most
0.357 DS px (largest: `st30 ours` green dy -1.9839 -> -1.6270); in all 20
quoted class pairs `dy` is negative, from -0.3001 to -2.5699 DS px, and `dx` is
mixed, from -1.7386 to +0.9474 DS px. The fitted mapping therefore explains
at most about 0.36 DS px of the observed offset.

The DeSmuME-harness arm of this cross-check is **UNVERIFIED**: its commands
and raw output are not quoted, so no harness number is used as FACT here or in
the doc pointers.

## 6. Conclusion (labelled)

**FACT (only what the quoted numbers show):** the capture mapping measured by
the ds-scale-test pattern on this console/capture is `(a_x, b_x) =
(1.250383, 39.933172)`, `(a_y, b_y) = (1.250790, -0.168453)` with RMS residual
0.072 / 0.026 capture px. That differs from the current `(1.25, 40, 1.25, 0)`
by at most 0.135 DS px at the screen corners. Switching the scene5 comparison
to this fitted mapping changes bbox deviations by at most 1 DS px and class
centroids by at most
0.357 DS px (largest: `st30 ours` green dy -1.9839 -> -1.6270); against the
PICA render (`ours`) `dy` is negative in all 20 quoted class pairs
(-0.3001 to -2.5699 DS px) and `dx` is mixed (-1.7386 to +0.9474 DS px),
with `bbox_dev_top=2` in all four pairs and `bbox_dev_left` 0-1. The
DeSmuME-harness arm of the comparison is UNVERIFIED here (§5).

**Conclusion (FACT reading of the above):** the ~1-2 DS px offset of the
scene5 captures against the PICA render — `dy` negative in every quoted class,
`dx` mixed — is **not** produced by the capture mapping. The mapping is
accurate to about 0.1 DS px on this capture. The offset must come from
elsewhere.

**HYPOTHESIS:** the offset is a real shift of the captured picture relative to
the PICA render (or a position difference in the reference itself), not a
mapping error; the quoted data cannot separate the two sides and only rules out
the mapping as a 1-2 px explanation. This is consistent with eq4f §5(ii), and
the +1 DS px resampling allowance (`tools/eq4_hw_compare.py:61-65`) is not the
offset either: the measured mapping error is ~0.1 px.

**UNVERIFIED:** other consoles and boards; other captures of the same ROM; the
exact resampler filter; which side (hardware, harness, PICA render) is
displaced; any scene5-specific position difference between the hardware ROM and
the PICA render. None is measured by the quoted commands.

**Recommendation:** do **not** change `tools/ds_vtxcolor_test/
capture_mapping.json`, `tools/eq4_hw_compare.py` or its tolerances. The fitted
intercepts differ from `(40, 0)` by more than their standard errors (`b_y`
about 15x `se_b_y`, `b_x-40` about 3x `se_b_x`), but the whole mapping
deviation is at most 0.135 DS px, an order of magnitude below the 1-2 DS px
offset and not its cause. If a future task wants to record the point estimate,
the only defensible refinement would be `scale_x=1.2504`, `offset_x=39.93`,
`scale_y=1.2508`, `offset_y=-0.17` (capture px), a sub-0.2 DS px change that
would not alter the scene5 verdict or explain the offset. That change is
recommended for consideration only and is **not applied here**.

## 7. Tests (FACT)

`tools/test_fit_mapping.py` (7 tests) is wired like
`tools/test_eq4_hw_compare.py`: a `test-fit-mapping` target
(`unittest discover -s tools -p 'test_fit_mapping.py'`), a prerequisite of
`test`, and a `FIT_MAPPING_DISCOVER_SKIP=1` guard in the aggregate `discover`
run, so it does not run twice. `make test-ds-scale-test` no longer discovers it
(15 tests there).
The suite covers: synthetic recovery within 0.05 px for the nominal mapping,
`a=1.245` (scale decoupled) and `b_x=41` (offset decoupled); a one-DS-pixel
negative control (>= 0.9 DS px); fixture pinned at `a_x=1.250383`,
`b_x=39.933172`, `a_y=1.250790`, `b_y=-0.168453` with the stated tolerance
(+/-0.001 for `a`, +/-0.05 capture px for `b`, about 2x the larger intercept
standard error `se_b_x=0.022656`); CLI determinism and the exit-2 usage path.
The pin is tied to the committed sha256 `f7ad03e17a82…d198d5`.

```
$ make test-fit-mapping
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_fit_mapping.py'
.......
----------------------------------------------------------------------
Ran 7 tests in 0.724s

OK
```

```
$ make test-ds-scale-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_scale_test -p 'test_*.py'
...............
----------------------------------------------------------------------
Ran 15 tests in 1.213s

OK
```

`make test` exits 0 (full host-test log 4479 lines); two complete sections of
that one fresh run are quoted verbatim below, one omission marker between them:

```
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_fit_mapping.py'
.......
----------------------------------------------------------------------
Ran 7 tests in 0.733s

OK
[... other sections omitted ...]
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
.....sssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 394 tests in 83.965s

OK (skipped=69)
```

## 8. Privacy and scope (FACT)

Every command uses `$HOME`, repo-relative or session-temp paths; no literal
home directory, user name, host name, e-mail or key is added. The added lines
contain no `/(home|Users)/` path:

```
$ grep -rn -E '/(home|Users)/' tools/ds_scale_test/fit_mapping.py tools/test_fit_mapping.py tools/eq4g_fit_compare.py Makefile docs/reports/eq4g-capture-offset.md
(no output; exit status 1)
```

`git status --short` lists the new files, the moved test, the root `Makefile`
`test-fit-mapping` plumbing, the two doc pointers and the README row; the
mapping JSON and `tools/eq4_hw_compare.py` show no diff.

## 9. Requirement traceability

- **REQ new tool `tools/ds_scale_test/fit_mapping.py`, per-axis sub-pixel edge
  midpoint at 50 % with linear interpolation, method stated, all usable
  straight edges, least squares per axis, per-edge DS/capture/residual and fit
  a/b/RMS, deterministic output** — FACT: §1, §3 full stdout (a/b/RMS plus
  their standard errors).
- **REQ apply to `sc4_3_top400x240.png`, print complete output, compare with
  (1.25,40,1.25,0), give a/b and RMS, express the difference in DS px at the
  centre and both corners** — FACT: §3, §4 tables and `delta_ds_*` lines.
- **REQ cross-check scene5 with both mappings against `ours_s5_st0/30`, print
  complete outputs, say whether the 1-2 DS px offset shrinks, without
  changing `eq4_hw_compare.py` or its default mapping/tolerances** — FACT: §5
  full blocks (every file-creating command quoted); offset does not shrink;
  `git diff` shows no change to `eq4_hw_compare.py` (see §8). The harness arm
  is UNVERIFIED (§5).
- **REQ tests `tools/test_fit_mapping.py` wired like the sibling tests:
  synthetic recovery within 0.05 px, a 1-DS-px negative control detected, the
  real fixture pinned with a stated tolerance** — FACT: §7, `make
  test-fit-mapping` 7 tests OK, `make test` runs it once (guard) and is green
  (394 tests, skipped=69).
- **REQ report with full raw outputs, results tables, FACT/HYPOTHESIS/
  UNVERIFIED, basis "one console, one capture, one scene pattern", a mapping
  recommendation without applying it, append-only pointers** — FACT: this
  report; EQ-4g paragraph in `docs/EQUALITY_CHECK_DESIGN.md`; section 10 in
  `docs/EQ4_HW_COMPARE.md`; recommendation in §6 is not applied.
- **REQ never weaken tests; do not touch existing tests/tools/mapping** — FACT:
  the 7 tests and their tolerances are unchanged (moved only); §7 full suite
  green; §8 `git status`.
- **REQ no literal home paths or user names** — FACT: §8 grep has no output.

Fresh evidence for the repaired sentences (this revision; `head` caps the
report so the block is not matched by its own rerun); §7 quotes the fresh
`Ran 7 tests`, `Ran 394 tests` and `OK (skipped=69)` lines:

```
$ p='0\.3001 to -2\.5699\|1\.7386 to +0\.9474\|about 3 standard errors\|about 15x\|in all four pairs\|0\.357 DS px\|green dy -1\.9839 -> -1\.6270\|0\.36 DS px\|unchanged for st0'; grep -no "$p" docs/reports/eq4g-capture-offset.md | head -n 14; grep -no "$p" docs/EQUALITY_CHECK_DESIGN.md docs/EQ4_HW_COMPARE.md
179:about 3 standard errors
532:unchanged for st0
533:in all four pairs
535:0.357 DS px
535:green dy -1.9839 -> -1.6270
536:0.3001 to -2.5699
537:1.7386 to +0.9474
538:0.36 DS px
553:0.357 DS px
553:green dy -1.9839 -> -1.6270
555:0.3001 to -2.5699
555:1.7386 to +0.9474
556:in all four pairs
580:about 15x
docs/EQUALITY_CHECK_DESIGN.md:743:0.357 DS px
docs/EQUALITY_CHECK_DESIGN.md:743:green dy -1.9839 -> -1.6270
docs/EQUALITY_CHECK_DESIGN.md:743:0.3001 to -2.5699
docs/EQUALITY_CHECK_DESIGN.md:743:1.7386 to +0.9474
docs/EQUALITY_CHECK_DESIGN.md:743:in all four pairs
docs/EQUALITY_CHECK_DESIGN.md:743:0.36 DS px
docs/EQ4_HW_COMPARE.md:215:1.7386 to +0.9474
docs/EQ4_HW_COMPARE.md:216:in all four pairs
```
