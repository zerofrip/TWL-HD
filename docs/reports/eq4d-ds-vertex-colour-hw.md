<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4d — DS hardware initial vertex colour (corrected decoder) — 2026-11-23

**Scope:** host-only correction plus documentation. No new hardware action: the
capture analysed here was taken by the owner before this task. Changed:
`tools/cc3dsfs_capture/xwd2png.py` (24/32-bpp dispatch, length validation),
`test_xwd2png.py` (24-bpp pixel-exact, bug pin, unsupported-bpp cases),
`tools/cc3dsfs_capture/README.md`; `tools/ds_vtxcolor_test/capture_mapping.json`
(corrected mapping), its tests/README, and the new real fixture
`tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png`;
`tools/ds_scale_test` fixture/test/README relabelled as mis-decoded input;
append-only errata in `docs/reports/eq4b-cc3dsfs-ds-capture.md`,
`docs/reports/eq4c-ds-scale-measurement.md`, `docs/EQUALITY_CHECK_DESIGN.md`
and pointers in the two scene5 reports.

**Labels.** FACT = a command and its raw complete output quoted verbatim below
(or a file:line opened for this report). HARDWARE = a measured capture of the
console. HYPOTHESIS / UNVERIFIED = not measured; stated as such. Nothing
outside the quoted output is FACT, and no cause is inferred from it.

## 1. Root cause: the XWD decoder

**FACT** — the real header of the 2026-11-23 capture, first 14 fields of the
25-field header (`python3 -c "import struct; d=open('$HOME/3ds/hwtest-staging/cc3d-out/raw_vt1b_2.xwd','rb').read(); print(struct.unpack('>25I', d[:100])[:14])"`):

```
(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)
```

So `pixmap_depth=24`, width 1280, height 720, **`bits_per_pixel=24` (index
11)**, **`bytes_per_line=5120` (index 12)**, `ncolors`/visual-class field 5.
The pixel data are 3-byte BGR with row stride 5120. The old
`tools/cc3dsfs_capture/xwd2png.py` ignored `bits_per_pixel` and always decoded
4-byte `BGRX`, squeezing the picture horizontally by 3/4 and producing
repeating R/G/B channel stripes on coloured regions.

**FACT** — the same file through the old and the fixed decoder, top-left 400×240
region, bbox of pixels with any channel > 24 (the old script is a copy of
`HEAD:tools/cc3dsfs_capture/xwd2png.py`):

```
$ python3 /tmp/opencode/eq4d_oldconv/xwd2png.py "$HOME/3ds/hwtest-staging/cc3d-out/raw_vt1b_2.xwd" /tmp/opencode/vt1b_2_old.png
1280 720 24
$ python3 tools/cc3dsfs_capture/xwd2png.py "$HOME/3ds/hwtest-staging/cc3d-out/raw_vt1b_2.xwd" /tmp/opencode/vt1b_2_new.png
1280 720 24
$ python3 -c "from PIL import Image
for label, path in (('old', '/tmp/opencode/vt1b_2_old.png'), ('new', '/tmp/opencode/vt1b_2_new.png')):
    px = Image.open(path).convert('RGB').load()
    pts = [(x, y) for y in range(240) for x in range(400) if max(px[x, y]) > 24]
    print(label, 'max>24 top bbox', (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)))"
old max>24 top bbox (30, 0, 269, 239)
new max>24 top bbox (40, 0, 359, 239)
```

The corrected image is the DS 256×192 screen shown uniformly 1.25× as 320×240,
centred at x = 40. This is a **single-capture basis** (one `vt1b_2` capture).

**FACT** — the fixed decoder's unittest suite (new 24-bpp case, padded rows,
stride > 3·width):

```
$ make test-cc3dsfs
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/cc3dsfs_capture -p 'test_xwd2png.py'
....
----------------------------------------------------------------------
Ran 4 tests in 0.163s

OK
```

**FACT** — the new 24-bpp test against the OLD decoder fails (the bug is
pinned):

```
$ python3 /tmp/opencode/eq4d_oldconv/test_xwd2png_old.py Xwd2PngTest.test_converts_24bpp_bgr_pixel_exact
F
======================================================================
FAIL: test_converts_24bpp_bgr_pixel_exact (__main__.Xwd2PngTest)
----------------------------------------------------------------------
Traceback (most recent call last):
  File "/tmp/opencode/eq4d_oldconv/test_xwd2png_old.py", line 121, in test_converts_24bpp_bgr_pixel_exact
    self.assertEqual(img.tobytes(),
    ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^
                     bytes(v for p in PIXELS for v in p))
                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
AssertionError: b'\x00\x00\x00\x00\xff\x00\x00\xff\x00\x00\x00\x00\xff\xf[28 chars]\x00' != b'\x00\x00\x00\xff\x00\x00\x00\xff\x00\x00\x00\xff\xff\xf[22 chars]\xff'

----------------------------------------------------------------------
Ran 1 test in 0.060s

FAILED (failures=1)
```

## 2. Hardware measurement

**OWNER-REPORTED (not a quoted output):** the ROM is
`tools/ds_vtxcolor_test` variant 1, sha256
`15c4e48dacfa2f1c08b2b408e28a571cf9f3f30fa0770e51240c65d42cddfce7`. The owner
started it from TWiLight Menu++ in DS mode on the New 3DS and captured with the
2026-11-23 flags `--enabled_both 0 --enabled_top 1 --scaling_top 1` (the
`sh cc_shots_flags.sh vt1b 3 3` equivalent of
`tools/cc3dsfs_capture/cc_shots.sh`). Variant 2 was not run.

**FACT** — the committed fixture is the correctly decoded top 400×240 region
of that capture (byte-identical to the owner's file):

```
$ cp "$HOME/3ds/hwtest-staging/cc3d-out/vt1b_2_top400x240.png" tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png
$ sha256sum tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png
f48c9f1be832516466b45e74dd35728f64aa0c5963587c078642a2bf3a8acb1c  tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png
```

**FACT** — the analyser on that fixture, raw complete output:

```
$ python3 tools/ds_vtxcolor_test/measure_vtxcolor.py tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png
geometry_found=1
qA_rgb=255,255,255
qB_rgb=255,255,255
qC_rgb=0,0,0
qD_rgb=132,134,132
qE_rgb=255,0,0
bg_rgb=66,69,66
default_colour_class=same_as_qB
```

`qA` is the quad with **no colour command before its vertices**; `qB` is the
explicit white reference. The measured medians are identical (255,255,255).
`qD` is the explicit 5-bit grey 16/31 = 132,134,132 = `(16<<3)|(16>>2)`
(identity colour path); `qC` black (0,0,0); `qE` red (255,0,0); background
(66,69,66) = 5-bit 8 expanded to 66. The ±3 variation in the grey/background
channels is **UNVERIFIED** (no cause is offered).

## 3. Conclusions

- **FACT:** on this capture, quad A (no colour command) measures
  (255,255,255), identical to the explicit white quad B; C black, D grey, E
  red.
- **HYPOTHESIS:** the initial current vertex colour of this DS hardware path
  (after the loader/TWiLight start, first frame, 3D cleared) is **white**,
  agreeing with DeSmuME and disagreeing with melonDS.

## 4. Limits

- One capture, one loader state (TWiLight Menu++ / nds-bootstrap, first frame).
  The pre-loader GX state is not controlled.
- Variant 2 (reset + one empty displayed frame) is not yet run; a hardware run
  is planned.
- The mapping (1.25× uniform, offset x=40) is from this single capture; a
  re-capture of the ds-scale-test ROM through the fixed decoder is planned and
  is **not** part of this task.
- 5-bit white expands to 255; the measured value is consistent with that.
- The background ±3 variation is not explained (UNVERIFIED).

## 5. Cross-references

- Decoder fix and tests: `tools/cc3dsfs_capture/README.md`.
- Invalidated earlier capture statements: erratum in
  `docs/reports/eq4b-cc3dsfs-ds-capture.md`,
  `docs/reports/eq4c-ds-scale-measurement.md` and the EQ-4b/EQ-4c blocks of
  `docs/EQUALITY_CHECK_DESIGN.md`.
- Scene5 context: append-only updates in
  `docs/reports/scene5-colour-diagnosis.md` and
  `docs/reports/melonds-scene5-check.md`.

## 6. Privacy

Every command above uses `$HOME`. Check over the new/changed tool paths and the
added lines of the edited reports (the scene5 reports already carry older
literal paths that are untouched):

```
$ grep -rn -E '/(home|Users)/' tools/cc3dsfs_capture tools/ds_vtxcolor_test tools/ds_scale_test docs/reports/eq4d-ds-vertex-colour-hw.md
(no output; exit status 1)
$ git diff -U0 -- docs/reports/scene5-colour-diagnosis.md docs/reports/melonds-scene5-check.md docs/reports/eq4b-cc3dsfs-ds-capture.md docs/reports/eq4c-ds-scale-measurement.md docs/EQUALITY_CHECK_DESIGN.md tools/cc3dsfs_capture tools/ds_vtxcolor_test tools/ds_scale_test | grep -E '^\+.*/(home|Users)/'
(no output; exit status 1)
```

## 7. Requirement traceability

- **REQ fix the decoder (24 → 3-byte BGR + stride, 32 → BGRX, other bpp exit
  non-zero, length validation, keep CLI)** — FACT: §1 (header and suite);
  `tools/cc3dsfs_capture/xwd2png.py`.
- **REQ extend tests (24-bpp padded, keep 32, pin old decoding, unsupported
  bpp)** — FACT: §1 (4 tests pass; old decoder fails the new test);
  `tools/cc3dsfs_capture/test_xwd2png.py`.
- **REQ update capture README with 24/32 distinction and header fields** —
  FACT: `tools/cc3dsfs_capture/README.md` "XWD header fields used".
- **REQ corrected mapping 1.25/1.25/40/0 in `capture_mapping.json` and
  tests/README; real fixture + tolerance-12 test** — FACT: §2 raw output and
  sha256; `tools/ds_vtxcolor_test/`.
- **REQ keep the `measure_scale.py` ↔ `capture_mapping.json` cross-check green
  without weakening it** — FACT: the cross-check now runs on a synthetic
  uniform-1.25 capture (`test_mapping_matches_measure_scale_on_synthetic_capture`);
  `make test-ds-vtxcolor-test` 15 tests OK.
- **REQ `tools/ds_scale_test`: rename the mis-decoded fixture, pin the old
  numbers as mis-decoded input, mark README statements SUPERSEDED** — FACT:
  `fixtures/sc1_5_crop400x480_misdecoded.png`, `test_measure_scale.py`,
  `README.md` §1/§6/§7; `make test-ds-scale-test` 10 tests OK.
- **REQ append-only errata to eq4b, eq4c and the EQ-4b/EQ-4c result blocks;
  pointers in the scene5 reports** — FACT: `git diff` shows additions only in
  those files.
- **REQ new eq4d report with ROM sha256, capture flags, raw tool output,
  FACT/HYPOTHESIS labels and limits** — FACT: this report.
- **REQ no literal home paths, no worktree operations, targeted diff** — FACT:
  §6 and the final `git status`/`git diff --stat` in the task's report.
