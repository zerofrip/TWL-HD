<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4b — cc3dsfs capture board, DS-mode top-screen pixels — hardware result (2026-11-22)

**Scope:** documentation and two small host helpers. The run was a passive USB
video capture, **nothing was sent to the console**. Added: this report,
`docs/EQUALITY_CHECK_DESIGN.md` result blocks (append-only), and
`tools/cc3dsfs_capture/` (`cc_shots.sh`, `xwd2png.py`, the unittest,
`README.md`) plus one `make test` target. `cc3dsfs` itself was built out of tree
and its checkout was not modified.

**Labels.** FACT = a command output or raw block quoted verbatim below (or a
file:line opened for this report). HARDWARE = measured on the console, passive
video capture only. HYPOTHESIS / UNVERIFIED = not measured; stated as such.
Nothing outside the quoted output is FACT, and no cause is inferred from it.

**Why this route.** `docs/reports/eq4-ds-mode-capture.md` records the
2026-11-19 EQ-4 trial: NTR-HR remote play was not reachable while a DS ROM ran
in TWL/DS mode (12 attempts, no frame). EQ-4b tried a different capture path —
a cc3dsfs USB capture board attached to the New 3DS.

## 1. Setup

**FACT (HARDWARE, build output quoted):** `cc3dsfs` was built out of tree from
the unmodified checkout at `$HOME/3ds/cc3dsfs`, with

```
cmake -S $HOME/3ds/cc3dsfs -B $HOME/3ds/hwtest-staging/cc3dsfs-build -DCMAKE_BUILD_TYPE=Release
cmake --build $HOME/3ds/hwtest-staging/cc3dsfs-build --config Release -j4
```

and the build output ended with:

```
[100%] Linking CXX executable bin/cc3dsfs
[100%] Built target cc3dsfs
```

**OWNER-REPORTED (not a quoted output; UNVERIFIED in this record):** the
capture board is USB device `0752:8613` (sysfs `idVendor:idProduct`); the owner
connected it to the New 3DS and used the console in New 3DS mode; the owner
installed the udev rule `usb_rules/51-optimize.rules` and `xvfb` (sudo run by
the owner; the rule matches `0752:8613` with `MODE=0666`, after which
`/dev/bus/usb/004/...` showed `crw-rw-rw-`).

**FACT (HARDWARE, quoted output):** the host `DISPLAY` was dead, so the GUI
program was started under `xvfb-run`; without it the run printed

```
Failed to open X11 display; make sure the DISPLAY environment variable is set correctly
Aborted (core dumped)
```

## 2. Connection

**FACT (HARDWARE, raw first-run log quoted verbatim):**

```
Setting vertical sync not supported
[cc3dsfs] File $HOME/.config/cc3dsfs/cc3dsfs.cfg load failed.
Defaults re-loaded.
[cc3dsfs] Connected to Opt. N3DS - USB 0
```

## 3. DS-mode capture

**OWNER-REPORTED (not a quoted output; UNVERIFIED in this record):** the owner
started `/roms/nds/twlhd-s5cull-plain.nds` (sha256 prefix `1bde37007ab36c05`;
rotating textured quad, reported to be the same ROM as Run 23/24) from TWiLight
Menu++, then ran

```
sh cc_shots.sh ds1 24 5
sh cc_shots.sh ds2 12 3
```

The owner reports that frames `ds1_1..24.png` cover the menu (1-18), the
transition (19) and the ROM (20-24), that `ds2_1..12.png` cover the ROM running,
and that all are 1280×720 Xvfb root screenshots written by `xwd2png.py`. None
of these statements is a quoted output or an opened file:line in this record.

**HYPOTHESIS (not read from cc3dsfs):** the content sits in the top-left
400×480 of the screenshot, with top screen rows 0-239 and bottom screen rows
240-479. This layout is inferred from the measured bounding boxes below, not
read from the cc3dsfs source or output.

## 4. Raw per-frame statistics

**FACT (HARDWARE, raw per-frame statistics quoted verbatim):**

```
frame 3: top distinct 69; top bbox (149, 6, 152, 235); bot bbox (31, 0, 270, 110)
frame 8: top distinct 681; most frequent: (0,0,0) 86597, (255,255,255) 2196, (255,0,0) 497, (0,0,255) 458, (0,255,0) 423; top bbox (78, 26, 213, 204); bot bbox (31, 0, 270, 110)
frame 12: top distinct 847; (0,0,0) 89826, (255,255,255) 2019, (0,0,255) 252, (255,0,0) 237, (0,255,0) 233; top bbox (116, 33, 244, 240); bot bbox (31, 0, 270, 110)
```

**FACT (plain observation from the three quoted lines):** the values `255` and
`0` appear in the most-frequent colour lists of frames 8 and 12.

**HYPOTHESIS (interpretation, not in the quoted output):** frame 3 is a nearly
edge-on quad, read from its 3 px wide bbox. The pure `255`/`0` values suggest
the capture preserved saturated colours.

**OWNER-REPORTED (not a quoted output; UNVERIFIED in this record):** the mean of
ds1 frames 1-18 is grey 184, read by the owner as the menu; the statistics above
come from a script that crops the top as (0,0,400,240), calls `getcolors` and
takes the bbox of each channel > 24.

## 5. Not measured (UNVERIFIED)

- **UNVERIFIED:** the scale and offset of the DS 256×192 image inside the
  400×240 capture.
- **HYPOTHESIS:** the capture board output is the raw LCD data and therefore
  pixel-exact. The pure 255/0 colours above are circumstantial evidence only;
  the board's processing is not documented here.
- **UNVERIFIED:** frame-to-frame sync with the ROM.
- **UNVERIFIED:** that the capture window is 400×480; the bounding boxes only
  suggest the top-left 400×480 region.
- **UNVERIFIED:** that this board is what the owner calls "偽トロ". The device
  reports itself as "Opt. N3DS" (an Optimize New 3DS board); Nisetro DS(i) is a
  different product and was not identified.

## 6. Verdict

**EQ-4b: a cc3dsfs capture board attached to the New 3DS captured the DS-mode top screen as numeric pixels on 2026-11-22 (36 frames, pure colours preserved). Option (c) is achievable by this route, not via NTR-HR (see EQ-4).**

The DS-mode pixels are numeric PNGs, unlike the 2026-11-19 NTR-HR route
(`docs/reports/eq4-ds-mode-capture.md`), which produced no frame.

## 7. Next steps (plan, not done)

- Measure the scale and offset of the 256×192 image inside the capture with a
  static known pattern.
- Feed the captured PNGs (converted to PPM) into the EQ-3 comparer.

## 8. Requirement traceability

- **REQ-1 (record the method).** Setup (build commands and build tail), device
  and udev details, the dead-DISPLAY abort, and the connect log are in
  sections 1-2; owner-reported items are labelled as such, not FACT.
- **REQ-2 (capture evidence quoted).** The capture commands appear as text in
  section 3 (owner-reported, not a quoted output); the raw per-frame statistics
  are quoted verbatim in section 4.
- **REQ-3 (FACT/HYPOTHESIS/UNVERIFIED discipline).** Section 5 lists scale,
  offset, pixel-exactness, sync, window size and the "偽トロ" identity; the
  layout is marked HYPOTHESIS in section 3.
- **REQ-4 (verdict in both files).** Section 6 carries the verdict line
  verbatim; `docs/EQUALITY_CHECK_DESIGN.md` repeats it in the §3.3(c), §4 EQ-4
  and §6.3 D2 "Result EQ-4b (2026-11-22)" blocks.
- **REQ-5 (helpers added and tested).** `tools/cc3dsfs_capture/cc_shots.sh` and
  `xwd2png.py` are the used helpers with `$HOME`/`CC3DSFS_BIN`/`OUT_DIR`
  instead of home paths; `test_xwd2png.py` converts a synthetic 32bpp BGRX
  XWD pixel-exactly and skips with a labelled reason without PIL;
  `make test-cc3dsfs` runs it and `make test` depends on it.
- **REQ-6 (no other change).** No existing file under `tools/`, `common/`,
  `arm9v2/` or `arm11/` is modified and no existing test is touched;
  `docs/EQUALITY_CHECK_DESIGN.md` gains only appended blocks.
- **TESTS: `make test`**

## 9. Erratum (2026-11-23)

Append-only correction. The 2026-11-22 capture was decoded wrongly:
`tools/cc3dsfs_capture/xwd2png.py` treated the `xwd -root` screenshot as
32 bits per pixel ('BGRX') although the file is 24 bpp. The real header is
(python `struct '>25I'` of the first 100 bytes of a real capture, first 14
fields):

```
(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)
```

i.e. `pixmap_depth=24`, width 1280, height 720, `bits_per_pixel=24`
(index 11), `bytes_per_line=5120` (index 12), `ncolors`/visual-class field 5.
The data are 3-byte BGR pixels (row stride `bytes_per_line=5120`). Decoding
them as 4-byte BGRX squeezed the picture horizontally by 3/4 and produced
repeating R/G/B channel stripes on coloured regions; decoding as 3-byte BGR
with the header stride gives the correct picture.

Evidence on the same capture (`raw_vt1b_2.xwd`): the old decoder gave a
non-black top bbox x30..269 (`scale_x` 0.9375 measured from the
ds-scale-test ROM); the correct decoder gives x40..359, y0..239 (the DS
256×192 screen shown uniformly 1.25× as 320×240 centred, offset x=40), and the
quad colours exactly white (255,255,255), explicit grey 16/31 =
(132,134,132) (= `(16<<3)|(16>>2)`, identity colour path), red (255,0,0),
black (0,0,0), background (66,69,66) = 5-bit 8 expanded to 66, with ±3
dither-like variation (**UNVERIFIED**: no cause is offered for that variation).

**Correction of this report.** The per-frame statistics in section 4 were
taken from mis-decoded PNGs and are **invalid as measurements of the board**
(they describe the mis-decoded image). The statement that "pure colours were
preserved" (section 4 and the section 6 verdict) is invalid for the same
reason. The `(255,251,255)` DS-white reading belongs to `eq4c` and is also an
artefact.

**What remains valid.** The board connects (`[cc3dsfs] Connected to Opt.
N3DS - USB 0`) and the route shows the DS-mode top screen as numeric pixels;
that part of the EQ-4b verdict stands. The corrected geometry and colour facts
above are from the single 2026-11-23 `vt1b_2` capture — **single-capture
basis** — recorded in `docs/reports/eq4d-ds-vertex-colour-hw.md`. The
2026-11-22 statistics must be re-taken through the fixed decoder before they
are used again.

Report: `docs/reports/eq4d-ds-vertex-colour-hw.md` (decoder fix, tests,
corrected mapping).
