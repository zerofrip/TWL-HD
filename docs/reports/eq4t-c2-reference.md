# EQ-4t — the CUBES=2 (c2) DeSmuME-harness reference from the plain static ROM

EQ-4r left c2 NOT-COMPARABLE against the harness because the **static autodump**
`CUBES=2` ROM crashes in the harness (`ARM OP_BKPT triggered`, exit 1). EQ-4s
§3.5 showed the **plain static** ROM `twlhd-arm9v2-c2-st30.nds`
(OWNER/ASSISTANT-REPORTED as the ROM uploaded to the console; §2) passes the
harness at frame 242 (exit 0, "12 polygons, 48 vertices"), and EQ-4s §6
proposed using it as the substitute harness reference. This report does exactly
that, once, and finishes the c2 hardware-vs-DeSmuME comparison.

Labels: FACT (quoted command + raw unedited output), SOURCE-READ, HYPOTHESIS,
UNVERIFIED. Hardware and DeSmuME images are NOT presentation-corrected; the
tool's `--azahar-correction` guard is left at its default `auto`.

Note on exit statuses: in the quoted blocks below the exit status was read with
a separate `$ echo "exit=$?"` in the same shell directly after each command,
not inlined as `<command>; echo "exit=$?"`; the harness was not re-run to
change this.

## 0. Result in one table

| pair | mean_abs r/g/b | frac_within_flat | bbox_dev l/t/r/b | verdict | scene |
|---|---|---|---|---|---|
| c2 hw vs harness (this report) | 0.4777/0.3136/0.4495 | 1.0000 | 2/2/0/0 | PASS | MATCHED |
| c2 hw vs ours (EQ-4r §5.12, not re-run) | 16.0627/16.0865/24.0431 | 1.0000 | 1/1/1/1 | PASS | PARTIAL |

## 1. Inputs (FACT)

- ROM: `<staging>/roms/twlhd-arm9v2-c2-st30.nds` (plain static `CUBES=2`,
  `STATIC_ANGLE=30`, no autodump) — the ROM uploaded to the console (§2).
- Hardware capture: `tools/fixtures/eq4_hw_scenes/c2_a_top400x240.png` (capture
  `a`; the committed fixture equals `<staging>/hw/hw_c2_a.png`, EQ-4r §2).
- Harness: `$HOME/3ds/twlhd-desmume-harness/harness` (out-of-tree, GPL-2.0,
  never committed). Frame rule: harness frame 242 = ROM frame 240
  (`tools/fixtures/eq_scenes/README.md`).
- Worktree HEAD at the harness run: `801c50561760459944a3842658ddabf163114ed4`
  (the base commit `git merge-base HEAD main`, quoted below); `main` points at
  the same commit (and still does — `main` is unchanged). The worktree HEAD
  advanced ahead of `main` after the run via supervisor-run merges (`11dc6d8…`,
  then `66084f0…`), so §9's `git rev-parse HEAD` — taken after the final edit —
  quotes the branch tip, not the harness-run HEAD.

```
$ git merge-base HEAD main
801c50561760459944a3842658ddabf163114ed4
```

## 2. ROM identity gate (FACT)

The plain static ROM must be byte-identical to EQ-4s §3.5's
`43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940` before any
reference may be produced:

```
$ sha256sum "$HWSTAGE/roms/twlhd-arm9v2-c2-st30.nds"
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  <staging>/roms/twlhd-arm9v2-c2-st30.nds
```

FACT: matches EQ-4s §3.5 exactly; provenance passes.

FACT (contract error; orchestrator disclosure about the contract, not the ROM):
the EQ-4t task contract gave the hardware c2 ROM sha256 as starting `6453…`;
`6453ba62…` is the sha256 of the **cube** ROM `twlhd-arm9v2-st30.nds` (the
single-cube static ROM), not of c2 — the contract's `6453…` prefix was the
orchestrator's error, stated next to the report's measured
`43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940`.

OWNER/ASSISTANT-REPORTED (the owner-side upload record is an assistant's own
log, not a console-side measurement): the 2026-11-24 console upload record
lists the c2 ROM uploaded to the console with sha256 prefix `43e39eb8`, equal
to the measured value.

```
$ grep -n "twlhd-{cube" "$HWSTAGE/../CONSOLE_ACTION_LOG.txt" | sed "s|$HOME|~|g"
1786:2026-11-24 (assistant, owner started HBL+netloader, approved merge): 3dslink ftpd-classic.3dsx; FTP put new files /roms/nds/twlhd-{cube,s1,s2,s3,s4,c2}-st30.nds (EQ-4q static-angle ROMs, 235520 B each, sha256 prefixes 6453ba62,f9f0d548,dd26ec63,5c6e8c2c,9662c06c,43e39eb8); no existing file touched. run24 backups of /twlhd_frame*.bin were restored earlier.
```

```
$ grep -n c2 "$HWSTAGE/hw/SHA256SUMS"
1:488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6  hw/hw_c2_a.png
2:5c43e91cd121ab3cbc446c80e8a0ce6e463a7e81a9c27aa9c0b7f0748da06d64  hw/hw_c2_b.png
3:488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6  hw/hw_c2_c.png
8:1ea3d1f8aa09ea78a5b02f29b152543ce5163b2c15218113f7924df52072e7c2  hw/hw_s1_b.png
11:41b4356a3539f6ad2f2be79948d70c6c81dae002826544195847f77c2eb6a1e1  hw/hw_s2_b.png
19:82e913696c003b54525c092f4f2e7f483ec52b805cea58366f85f6054d41f3e4  roms/twlhd-arm9v2-autodump-c2-st30.nds
```

(exit status 0; the pattern matches `c2` anywhere, so rows 8/11 match a
substring of the hash; only the autodump c2 ROM is listed and the plain c2 ROM
is absent — consistent with EQ-4r §1.)

FACT (locally measured; `sed` only replaces the staging prefix in the paths):
`6453ba62…` belongs to the cube ROM, not to c2.

```
$ sha256sum "$HWSTAGE/roms/twlhd-arm9v2-c2-st30.nds" "$HWSTAGE/roms/twlhd-arm9v2-st30.nds" | sed "s|$HWSTAGE|<staging>|g"
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  <staging>/roms/twlhd-arm9v2-c2-st30.nds
6453ba6219d717a7de5a785c165574ad9cf15cc2f04d994c1ab34bc7cd335a01  <staging>/roms/twlhd-arm9v2-st30.nds
```

UNVERIFIED: that the console actually ran exactly that c2 ROM file — the only
link from `43e39eb8` to the upload is the owner-side record quoted above.

## 3. Harness run of the plain static ROM (EMULATOR, FACT)

### 3.1 frame 242 — the reference run (once)

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-c2-st30.nds" 242 --out /tmp/opencode/eq4t/harness_c2.txt --ppm /tmp/opencode/eq4t/harness_c2.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 12 polygons, 48 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: B3240B7C
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```

FACT: exit 0, "captured frame 242: 12 polygons, 48 vertices", same `ROM crc:
B3240B7C` and geometry counts as EQ-4s §3.5. (EQ-4s §3.5 additionally printed a
`BackupDevice: DeSmuME .dsv save file not found.` line; here a `.dsv` already
existed from that earlier run, so it is absent. The PPM is unaffected.)

### 3.2 frame 300 — frame-independence check (one disclosed extra run)

The ROM is static (the same frame every refresh), so any presented frame should
be byte-identical. To justify the frame-242 choice a-priori, one extra run at
frame 300:

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-c2-st30.nds" 300 --out /tmp/opencode/eq4t/harness_c2_f300.txt --ppm /tmp/opencode/eq4t/harness_c2_f300.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 300: 12 polygons, 48 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: B3240B7C
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```

```
$ sha256sum /tmp/opencode/eq4t/harness_c2.ppm /tmp/opencode/eq4t/harness_c2_f300.ppm
18270fadf5e4aee721b12f3462de5e89589336f824f9c0c8582efad1998cec37  /tmp/opencode/eq4t/harness_c2.ppm
18270fadf5e4aee721b12f3462de5e89589336f824f9c0c8582efad1998cec37  /tmp/opencode/eq4t/harness_c2_f300.ppm
```

FACT: the frame-242 and frame-300 PPMs are byte-identical (same sha256), so the
presented frame does not depend on the harness frame index; frame 242 (the
shared EQ-4r/eq4s convention) is the reference frame and any other frame would
produce the same picture.

## 4. PPM -> PNG conversion (FACT)

Same lossless pattern as EQ-4r §3.7 (heredoc, no `python3 -c`):

```
$ python3 - <<'EOF'
from PIL import Image
Image.open("/tmp/opencode/eq4t/harness_c2.ppm").convert("RGB") \
    .save("tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png")
EOF
$ echo "exit=$?"
exit=0
```

```
$ sha256sum /tmp/opencode/eq4t/harness_c2.ppm tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png
18270fadf5e4aee721b12f3462de5e89589336f824f9c0c8582efad1998cec37  /tmp/opencode/eq4t/harness_c2.ppm
28979f4ece54f165176dff586e1e0ef673a4f16fc8b5dc494caddae20043852b  tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png
$ stat -c '%s %n' tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png
409 tools/fixtures/eq4_hw_scenes/harness_c2_st30_f240.png
```

FACT: the PPM is the 256x192 P6 framebuffer (147471 bytes, like the other five
harness PPMs); the PNG is 409 bytes, well under the 100 KB cap.

## 5. Comparison: hardware capture a vs the new reference (FACT)

### 5.1 the comparison run (once)

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/c2_a_top400x240.png --reference /tmp/opencode/eq4t/harness_c2.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.4777
ds.mean_abs_g=0.3136
ds.mean_abs_b=0.4495
ds.max_error=255.0000
ds.frac_within=0.9945
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.3258
ds.flat_pixels=48753
ds.edge_pixels=399
capture.mean_abs_r=0.4655
capture.mean_abs_g=0.3007
capture.mean_abs_b=0.4347
capture.max_error=255.0000
capture.frac_within=0.9949
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.3057
capture.flat_pixels=76234
capture.edge_pixels=566
ref_nonbg_pixels=255
ref_bbox_left=6
ref_bbox_top=90
ref_bbox_right=33
ref_bbox_bottom=102
capture_nonbg_pixels=364
capture_bbox_left=4
capture_bbox_top=88
capture_bbox_right=33
capture_bbox_bottom=102
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=160
class_red_ref_centroid_x=19.4250
class_red_ref_centroid_y=97.7875
class_red_capture_area=141
class_red_capture_centroid_x=18.3050
class_red_capture_centroid_y=96.8865
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=2
class_green_capture_centroid_x=13.5000
class_green_capture_centroid_y=90.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=2
class_blue_capture_centroid_x=12.5000
class_blue_capture_centroid_y=91.0000
class_yellow_ref_area=29
class_yellow_ref_centroid_x=23.7586
class_yellow_ref_centroid_y=95.0345
class_yellow_capture_area=20
class_yellow_capture_centroid_x=23.0000
class_yellow_capture_centroid_y=93.7000
bbox_dev_left=2
bbox_dev_top=2
bbox_dev_right=0
bbox_dev_bottom=0
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```

FACT: exit 0, `verdict=PASS`. The `presentation-row correction:` line is
`not applied (no row-0 wrap signature)`, like the other five harness pairs —
the harness PPM is a raw 256x192 framebuffer and is never presentation-corrected.

### 5.2 the 2-row-shift check (one disclosed extra run)

To pin the negative test's numbers, the capture was moved up two capture rows
(~1.6 DS px) and compared once:

```
$ python3 tools/eq4_hw_compare.py --capture /tmp/opencode/eq4t/c2_a_shift2.png --reference /tmp/opencode/eq4t/harness_c2.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.7672
ds.mean_abs_g=0.3005
ds.mean_abs_b=0.7295
ds.max_error=255.0000
ds.frac_within=0.9933
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.2579
ds.flat_pixels=48710
ds.edge_pixels=442
capture.mean_abs_r=0.7370
capture.mean_abs_g=0.2773
capture.mean_abs_b=0.7078
capture.max_error=255.0000
capture.frac_within=0.9935
capture.frac_within_flat=0.9999
capture.frac_within_edge=0.2481
capture.flat_pixels=76147
capture.edge_pixels=653
ref_nonbg_pixels=255
ref_bbox_left=6
ref_bbox_top=90
ref_bbox_right=33
ref_bbox_bottom=102
capture_nonbg_pixels=355
capture_bbox_left=4
capture_bbox_top=87
capture_bbox_right=33
capture_bbox_bottom=100
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=160
class_red_ref_centroid_x=19.4250
class_red_ref_centroid_y=97.7875
class_red_capture_area=141
class_red_capture_centroid_x=18.4894
class_red_capture_centroid_y=95.1489
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=2
class_green_capture_centroid_x=22.0000
class_green_capture_centroid_y=87.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=3
class_blue_capture_centroid_x=15.0000
class_blue_capture_centroid_y=91.6667
class_yellow_ref_area=29
class_yellow_ref_centroid_x=23.7586
class_yellow_ref_centroid_y=95.0345
class_yellow_capture_area=23
class_yellow_capture_centroid_x=22.1739
class_yellow_capture_centroid_y=92.3043
bbox_dev_left=2
bbox_dev_top=3
bbox_dev_right=0
bbox_dev_bottom=2
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```

FACT: shifting c2 up two capture rows moves `bbox_dev_top` from 2 to 3 (the
number changes) but the verdict stays PASS — c2's top `bbox_dev` is 2, one DS
px under the tol-3 boundary, so +1 DS px does not cross it (unlike s1, whose 3
goes to 4 and flips FAIL). The negative test therefore asserts the number
change (2 -> 3), not a verdict flip.

### 5.3 classification under the EQ-4r §7 thresholds

EQ-4r §7 classifies MATCHED when every `bbox_dev` <= 3 DS px and
`ds.frac_within_flat` >= 0.9987 against the harness (>= 0.9872 against ours).
Here `bbox_dev` is 2/2/0/0 (all <= 3) and `ds.frac_within_flat=1.0000`
(>= 0.9987), so c2 is **MATCHED** against the plain-static harness reference.

### 5.4 comparison with the existing c2-vs-ours result (EQ-4r §5.12, not re-run)

EQ-4r §5.12 already ran c2 capture vs our Azahar/PICA render: PASS, mean_abs
16.0627/16.0865/24.0431, `ds.frac_within_flat=1.0000`, `bbox_dev` 1/1/1/1,
with `presentation-row correction: applied (row-0 wrap signature detected)`.
Against the harness reference the mean_abs is far lower
(0.4777/0.3136/0.4495) — the same pattern as cube/s1/s2/s3, where the harness
(DeSmuME) matches the DS `(0,0,0)` background while our render clears to
`(16,16,24)` (EQ-4r §7). This is the same colour-count explanation, not
re-derived. c2 joins cube/s1/s2/s3 as MATCHED against the harness.

## 6. Hypothesis: non-autodump vs autodump reference binary

The EQ-4r harness references (cube, s1, s2, s3, s4) came from the **autodump**
ROMs; this c2 reference comes from the **non-autodump** ROM because the c2
autodump ROM crashes before presenting a frame. SOURCE-READ
`arm9v2/Makefile:60-74`: a non-empty `AUTODUMP_FRAME` sets only
`TARGET=twlhd-arm9v2-autodump`, `BUILD=build-autodump` and appends
`-DTWLHD_DEMO_AUTODUMP_FRAME=<n>`; the plain ROMs are not hashed in the run's
`SHA256SUMS`, so "nothing else differs between the two binaries" is inferred,
not compared bit-for-bit. HYPOTHESIS: the harness frame-242 geometry/PPM is
unaffected by the autodump trigger (a file-dump side effect at ROM frame 240,
`main.cpp:254`, reached after the draw call) — EQ-4s §6. UNVERIFIED-in-principle
that the two binaries present the same frame; here the two-frame hash check
(§3.2) verifies the non-autodump ROM is self-consistent across frames, but it
does not (and cannot, without a working autodump c2 ROM) compare the two
binaries' frames bit-for-bit. The pair the autodump ROM would produce remains
NOT-COMPARABLE.

## 7. Tests (FACT)

`RealStaticScenesCompareTest` in `tools/test_eq4_hw_compare.py`:

- `SCENE_FIXTURE_SHA256` gains `harness_c2_st30_f240.png` (sha
  `28979f4e…`); the count assert goes 17 -> 18; `SCENE_FIXTURES` now includes
  c2's harness path (the `scene != "c2"` filter is removed).
- `test_c2_has_no_harness_reference` is **kept** (renamed
  `test_c2_autodump_pair_not_comparable`; only its file-absence assertion is
  dropped, because the reference now exists): it still asserts the missing
  reference makes the tool exit 2 with `cannot read` on stderr and no
  `verdict=` on stdout. `test_c2_capture_matches_harness_reference` pins
  `bbox_dev_top=2`, `ds.mean_abs_r=0.4777`, `ds.frac_within_flat=1.0000`, and
  the `presentation-row correction: not applied (no row-0 wrap signature)`
  line. Net assertions increase and the renamed NOT-COMPARABLE test keeps
  negative coverage.
- `test_c2_shifted_capture_changes_the_number` mirrors the s1 shift test:
  unshifted PASS with `bbox_dev_top=2`, shifted (2 capture rows up) still PASS
  but `bbox_dev_top=3` (the number changes; see §5.2).

```
$ make test-eq4-hw-compare
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq4_hw_compare.py'
.........................
----------------------------------------------------------------------
Ran 25 tests in 28.925s

OK
```

```
$ make test 2>&1 | tail -n 8
----------------------------------------------------------------------
Ran 422 tests in 90.020s

OK (skipped=97)
/tmp/tmpb0q9em1g/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

Both recipes are unchanged; the diff removes `test_c2_has_no_harness_reference`
and adds `test_c2_capture_matches_harness_reference`,
`test_c2_shifted_capture_changes_the_number`, and
`test_c2_autodump_pair_not_comparable` (one removed, three added, net +2 — the
test was renamed, not restored). `RealStaticScenesCompareTest` is
`DISCOVER_SKIP`-gated in the aggregate discover, so it runs fully only via the
`test-eq4-hw-compare` target (quoted above). The `make test` tail above shows
the aggregate unittest summary (stderr) plus buffered stdout from the
discover-pass tests that flushes after it; `make test` exits 0.

## 8. Privacy (FACT)

Every added command uses `$HOME`/`$HWSTAGE` and `<staging>/...`; no literal
home path, user name or host data is added.

```
$ git diff -U0 -- docs tools | grep -E '^\+' | grep -E '/(home|Users)/'
(no output; exit status 1)
```

## 9. Diff budget (FACT)

```
$ git rev-parse HEAD && git rev-parse main
66084f03ef7a38c58552d72b08f904de657ef87d
801c50561760459944a3842658ddabf163114ed4
$ git add -N . && git diff main | wc -c
35243
$ du -b tools/fixtures/eq4_hw_scenes
37480	tools/fixtures/eq4_hw_scenes
$ du -cb tools/fixtures/eq4_hw_scenes/*.png | tail -1
31030	total
```

(measured after the final edit; `HEAD` (`66084f0`) is ahead of `main`
(`801c505`), so `git diff main` is the full working-tree change versus `main`,
35243 bytes — under the 90000-byte budget.)

## 10. UNVERIFIED

- **Other consoles/sessions.** One console, one capture session per ROM; only
  capture `a` is compared (rule fixed before any number was read).
- **Capture-noise origin** (EQ-4r §2): the three c2 captures differ
  `a_vs_b=40 a_vs_c=0 b_vs_c=40`; counted, not explained.
- **Hardware ROM vs autodump ROM** (§6): inferred from the Makefile define, not
  compared bit-for-bit; the autodump c2 ROM still cannot present a frame, so
  its pair stays NOT-COMPARABLE.
- **Console-side file identity** (this round): the c2 ROM the console ran is
  not measured console-side; its link to the uploaded `43e39eb8…` file rests
  on the assistant-maintained owner upload record (§2).
- **Texture filtering / other scenes / angles / consoles / settings** are
  untested, as in EQ-4r.

## 11. Requirement traceability

- **REQ 1 (harness reference once, quoted, hashes, HEAD, frame justification)**
  — §2-§4.
- **REQ 2 (fixture + README, compare run, comparison with §5.12)** — §4-§5.
- **REQ 3 (tests: positive + negative, no weakening)** — §7.
- **REQ 4 (append-only pointer, eq4t report, MATCHED/DIFFERENT, hypothesis,
  UNVERIFIED)** — §0, §5.3, §6, §10.
- **REQ 5 (FACT only with quoted output, one run each + disclosed extras, no
  home paths, no `python3 -c`, diff < 90000, never weaken tests)** — §1-§9.
