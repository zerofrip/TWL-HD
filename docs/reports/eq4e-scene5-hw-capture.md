<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4e — scene5 on hardware through the fixed decoder — 2026-11-23

**Scope:** test fixtures and documentation only. No hardware action in this
task: the capture was taken by the owner before it. Added: the fixture
`tools/ds_scale_test/fixtures/s5b_7_top400x240.png`, the histogram tool
`tools/ds_scale_test/colour_histogram.py` and its test, and this report; the
`ds_scale_test` analogue is the `sc4_3` fixture documented in
`tools/ds_scale_test/README.md` §6.

**Labels.** FACT = a command and its raw complete output quoted verbatim below
(or a file read for this report). HARDWARE = a measured capture of the console.
HYPOTHESIS / UNVERIFIED = not measured; stated as such. Nothing outside the
quoted output is FACT, and no cause is inferred from it.

**OWNER-REPORTED capture metadata (not a quoted output).** All three
2026-11-23 captures are passive cc3dsfs captures of the New 3DS in DS mode
(capture board; flags `--enabled_both 0 --enabled_top 1 --scaling_top 1`),
decoded with the fixed `tools/cc3dsfs_capture/xwd2png.py`; the fixtures are
their top 400×240 crops. The owner placed the ROMs on the SD card and started
each from TWiLight Menu++. This capture is the scene5 ROM
(`twlhd-s5cull-plain`) showing its rotating textured quad, one frame.

## 1. Fixture and provenance

The scene5 ROM is `$HOME/3ds/hwtest-staging/run23/s5cull-plain.nds`
(sha256 prefix `1bde37007ab36c05`, the ROM of
`docs/reports/scene5-colour-diagnosis.md`).

**FACT** — ROM hash (quoted command and output):

```
$ cd "$HOME/3ds/hwtest-staging/run23" && sha256sum s5cull-plain.nds
1bde37007ab36c05b0457874dce56e633670aedf2c66e840b0c5e219fe8afd01  s5cull-plain.nds
```

**FACT** — the committed fixture is byte-identical to the owner's crop:

```
$ cp "$HOME/3ds/hwtest-staging/cc3d-out/s5b_7_top400x240.png" tools/ds_scale_test/fixtures/s5b_7_top400x240.png
$ cmp "$HOME/3ds/hwtest-staging/cc3d-out/s5b_7_top400x240.png" tools/ds_scale_test/fixtures/s5b_7_top400x240.png ; echo "exit status $?"
exit status 0
$ sha256sum tools/ds_scale_test/fixtures/s5b_7_top400x240.png
73604c7b8b34b155f86685a0ac531559c56dc3f48fe72fff5398adc252433019  tools/ds_scale_test/fixtures/s5b_7_top400x240.png
```

## 2. Most frequent colours

**FACT** — `colour_histogram.py` on the fixture, top 40, raw complete output.
The block is ordered by count then RGB and contains every colour with count
≥ 20 (35 colours qualify), so an absence below is an absence from that set:

```
$ python3 tools/ds_scale_test/colour_histogram.py tools/ds_scale_test/fixtures/s5b_7_top400x240.png 40
(0, 0, 0) 86822
(255, 255, 24) 1822
(255, 255, 255) 1095
(0, 0, 247) 996
(0, 255, 8) 947
(255, 0, 0) 777
(0, 4, 0) 360
(0, 0, 255) 251
(255, 251, 255) 127
(247, 247, 247) 81
(255, 251, 24) 81
(8, 8, 8) 72
(247, 0, 0) 72
(0, 0, 239) 64
(0, 251, 8) 61
(8, 0, 0) 56
(0, 0, 8) 53
(0, 255, 0) 53
(247, 247, 24) 51
(255, 255, 33) 51
(0, 247, 8) 44
(8, 8, 0) 39
(0, 8, 0) 37
(239, 239, 239) 34
(41, 40, 41) 33
(214, 215, 214) 30
(8, 12, 8) 29
(247, 243, 24) 28
(0, 0, 206) 26
(255, 4, 0) 23
(0, 4, 247) 22
(214, 0, 0) 22
(247, 243, 247) 22
(156, 158, 16) 21
(99, 0, 0) 20
(41, 0, 0) 19
(99, 97, 8) 19
(0, 12, 0) 18
(57, 56, 57) 18
(115, 113, 115) 18
```

The six expected colours are present with these counts: `(0, 0, 0)` 86822
(background), `(255, 255, 255)` 1095 (white), `(255, 255, 24)` 1822 (yellow),
`(0, 255, 8)` 947 (green), `(0, 0, 247)` 996 (blue), `(255, 0, 0)` 777 (red).
They are five lit colour cells on a black background. The exact 5-bit
expansions appear too, e.g. `(0, 0, 255)` 251 and `(0, 255, 0)` 53 — the
capture's colour path is not identity (EQ-4c/EQ-4d). Neither `(24, 255, 255)`
nor `(255, 24, 255)` appears anywhere in the block.

The unit test `tools/ds_scale_test/test_colour_histogram.py` asserts the six
colours are printed and that neither cyan nor magenta reaches count 20 on this
fixture; it asserts only what the tool prints.

## 3. Capture-to-DS mapping

The mapping is the one measured on the 2026-11-23 ds-scale-test capture
(EQ-4c/EQ-4d corrected decoder, `tools/ds_scale_test/README.md` §6). Re-quoted
here as the FACT basis (raw complete output):

```
$ python3 tools/ds_scale_test/measure_scale.py tools/ds_scale_test/fixtures/sc4_3_top400x240.png
frame_found=1
corner_red=1
corner_green=1
corner_blue=1
corner_yellow=1
bbox_x0=40
bbox_y0=0
bbox_x1=359
bbox_y1=239
scale_x=1.250000
scale_y=1.250000
offset_x=40
offset_y=0
strip_h_contrast=179.928060
strip_v_contrast=167.788628
scale_x_check=-0.001589
scale_y_check=-0.002148
offset_x_check=0.173241
offset_y_check=0.176034
```

So the DS 256×192 top screen sits at capture (40,0)..(359,239): DS pixel (x,y)
is at capture (40 + 1.25x, 1.25y), uniform 1.25× on both axes. The 400×240 crop
therefore holds the whole DS image with a 40-px mask on each side
(40 + 320 + 40 = 400); capture columns 0..39 and 360..399 are outside the DS
image. Basis: **one console, one ROM (ds-scale-test), one capture**;
reusing that mapping for this scene5 capture from the same console is a
HYPOTHESIS (the layout is a board/console property, not re-measured here).

## 4. Consequences

**HYPOTHESIS (i)** — hardware shows the textured quad with five colour cells
(the owner's 2026-11-17 photo of the same ROM and this capture), which is
consistent with the initial vertex colour being white in this DS-mode path
(EQ-4d: quad A with no colour command measures white; variant 2 repeats it).
The five lit cells are white, yellow, green, blue and red on black.

**HYPOTHESIS (ii)** — the earlier cyan/magenta statements in
`docs/reports/eq4b-cc3dsfs-ds-capture.md`, `eq4c-ds-scale-measurement.md` and
earlier chat were decoder artefacts: those PNGs were decoded as 32-bpp BGRX
instead of 24-bpp BGR (see the errata in both reports and
`tools/cc3dsfs_capture/README.md`). This correctly decoded capture contains
neither `(24, 255, 255)` nor `(255, 24, 255)` among the colours with count
≥ 20 — the histogram in §2 is the raw evidence.

**HYPOTHESIS (iii)** — NOT yet done: a pixel comparison of this hardware
capture with the PICA render. It needs a static, non-rotating scene5 build to
align the quad's phase between capture and render; this capture is one frame of
a rotating quad, so cell positions cannot be matched frame-for-frame. Plan:
build a static scene5 variant, capture it through the same cc3dsfs path, then
compare cell layout and colours against the PICA render with the existing
capture-comparison tooling. This is a plan, not a result.

## 5. Limits

- One console, one ROM, one capture; loader/TWiLight state is not controlled.
- The mapping is measured on a different ROM's capture (same console) and
  reused as a HYPOTHESIS here (§3).
- The histogram is a colour census of the crop, not a per-pixel comparison with
  the emulator or the PICA renderer; quad phase, blending and texture filtering
  are not measured.
- Counts include blended edge pixels; the exact 5-bit-expanded variants of the
  primary colours are listed in §2 and are part of the capture's colour path,
  not separate cells.
- No cause is offered for the exact counts beyond "the quad is drawn"; the
  rotating-frame phase differs per capture.

## 6. Privacy

Every command above uses `$HOME`; no literal home-directory path is added by
this report. Check over the new/changed paths:

```
$ grep -rn -E '/(home|Users)/' tools/ds_scale_test/colour_histogram.py tools/ds_scale_test/test_colour_histogram.py docs/reports/eq4e-scene5-hw-capture.md
(no output; exit status 1)
```

## 7. Requirement traceability

- **REQ scene5 hardware report with numbers from fixture (3)** — FACT: §§1–2;
  fixture `tools/ds_scale_test/fixtures/s5b_7_top400x240.png`, sha256 quoted.
- **REQ histogram tool stored in `tools/ds_scale_test/` with a unit test** —
  FACT: `tools/ds_scale_test/colour_histogram.py`,
  `tools/ds_scale_test/test_colour_histogram.py`; `make test-ds-scale-test`
  green.
- **REQ quote the tool's raw output; assert only what it prints** — FACT: §2
  raw block; the test asserts the six printed colours and the printed absence
  of cyan/magenta at count ≥ 20.
- **REQ capture-to-DS mapping from part A** — FACT: §3 quoted
  `measure_scale.py` output (bbox 40,0,359,239, scale 1.25/1.25, offset 40,0).
- **REQ HYPOTHESIS-labelled consequences (i)–(iii)** — FACT: §4.
- **REQ append-only pointers in `docs/EQUALITY_CHECK_DESIGN.md` and
  `docs/reports/scene5-colour-diagnosis.md`** — FACT: appended blocks after
  each EQ-4b/Erratum group and at the end of the diagnosis report; `git diff`
  shows additions only.
- **REQ FACT only with command + raw output; `$HOME` only; no worktree
  commands; targeted diff** — FACT: §§1–3 quotes, §6 grep, no `git worktree`
  command.
