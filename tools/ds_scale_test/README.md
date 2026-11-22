<!-- SPDX-License-Identifier: Apache-2.0 -->
# ds_scale_test — static DS top-screen scale probe

A static libnds DS ROM plus a host PNG analyser that together measure how a
New 3DS (through the cc3dsfs capture board used for EQ-4b) shows the DS
256×192 top screen inside the 400×240 capture region: bounding box, horizontal
and vertical scale, offset, and whether 1-px alternation survives resampling.

**Status of this task:** BUILD/UNIT only. The ROM was **not run on hardware**.
Build, pattern/header consistency and the analyser are verified on the host;
the display output, the 400×240 capture layout and the resampling hint are
**UNVERIFIED** until the owner runs the procedure in §8. Labels follow
`docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`: FACT = a command and its raw
output quoted here; HYPOTHESIS / UNVERIFIED = not measured.

## 1. Files

| Path | Purpose |
|---|---|
| `pattern.json` | single source of truth for every pattern element |
| `gen_pattern.py` | generates `include/pattern.h`; also renders the pattern for tests |
| `include/pattern.h` | committed build-time output of `gen_pattern.py` |
| `source/main.c` | ARM9 ROM: static 16-bit bitmap pattern + bottom console text |
| `Makefile` | devkitARM/libnds build of `ds-scale-test.nds` |
| `measure_scale.py` | capture-PNG analyser, stable `key=value` report |
| `test_measure_scale.py` | synthetic-capture scale/offset tests + real-capture regression |
| `test_pattern.py` | pattern.json ↔ header ↔ ROM-source consistency tests |
| `fixtures/sc1_5_crop400x480.png` | real 2026-11-22 capture, 400×480 crop (§6) |

The ROM links only `-lnds9`; it includes `<nds.h>` and links no TWL-HD
GX/`common/` code (asserted by `test_pattern.py`).

## 2. Pattern

Top screen: 256×192, 16-bit bitmap background (BG3, VRAM_A). Black background.
Paint order is the order in `pattern.json`; later elements overwrite earlier
ones.

| Name | Type | Rect (x, y, w, h) | Colour / content |
|---|---|---|---|
| `cross_v` | rect | 128, 0, 1, 192 | white column x=128 |
| `cross_h` | rect | 0, 96, 256, 1 | white row y=96 |
| `block3x3` | rect | 127, 95, 3, 3 | white, centred at (128, 96) |
| `hstrip` | alt_columns | 0, 40, 256, 8 | 1-px white/black columns, white at even x (period 2, phase 0) |
| `vstrip` | alt_rows | 40, 0, 8, 192 | 1-px white/black rows, white at even y (period 2, phase 0) |
| `gradient` | gradient | 0, 140, 256, 16 | 16 steps of 16 px, 8-bit grey 0,16,…,240 |
| `corner_tl` | rect | 1, 1, 8, 8 | red (255,0,0) |
| `corner_tr` | rect | 247, 1, 8, 8 | green (0,255,0) |
| `corner_bl` | rect | 1, 183, 8, 8 | blue (0,0,255) |
| `corner_br` | rect | 247, 183, 8, 8 | yellow (255,255,0) |
| `frame` | frame | 0, 0, 256, 192 | 1-px white border, drawn last |

Precedence consequences (intended): `vstrip` overwrites `hstrip` in the 8×8
overlap x=40..47, y=40..47; `gradient` overwrites `cross_v` in y=140..155 and
`vstrip` in x=40..47; `frame` is never broken, so it overwrites the gradient's
x=0 and x=255 pixels (and every other outermost pixel). This is why the
measured strip contrast (§6) can sit below 255 even when the alternation
survives.

Bottom screen: `consoleDemoInit()` (sub engine, VRAM_C, BG0) prints
`DS SCALE TEST` and the sha-independent id `ds-scale-test-1`.

## 3. BGR555 → expected RGB888 mapping

FACT — libnds macros quoted verbatim:

```
$ sed -n '131,137p' "$DEVKITPRO/libnds/include/nds/arm9/video.h"
// macro creates a 15 bit color from 3x5 bit components
/** \brief  Macro to convert 5 bit RGB components into a single 15 bit RGB triplet */
#define RGB15(r,g,b)  ((r)|((g)<<5)|((b)<<10))
#define RGB5(r,g,b)  ((r)|((g)<<5)|((b)<<10))
#define RGB8(r,g,b)  (((r)>>3)|(((g)>>3)<<5)|(((b)>>3)<<10))
/** \brief  Macro to convert 5 bit RGB components plus 1 bit alpha into a single 16 bit ARGB triplet */
#define ARGB16(a, r, g, b) ( ((a) << 15) | (r)|((g)<<5)|((b)<<10))
```

FACT — the 16-bit bitmap pixel format and the alpha/opaque bit, quoted
verbatim:

```
$ sed -n '443,452p' "$DEVKITPRO/libnds/include/nds/arm9/background.h"
//! Allowed background types, used in bgInit and bgInitSub.
typedef enum
{
	BgType_Text8bpp, 	//!< 8bpp Tiled background with 16 bit tile indexes and no allowed rotation or scaling
	BgType_Text4bpp, 	//!< 4bpp Tiled background with 16 bit tile indexes and no allowed rotation or scaling
	BgType_Rotation, 	//!< Tiled background with 8 bit tile indexes Can be scaled and rotated
	BgType_ExRotation, 	//!< Tiled background with 16 bit tile indexes Can be scaled and rotated
	BgType_Bmp8, 		//!< Bitmap background with 8 bit color values which index into a 256 color palette
	BgType_Bmp16 		//!< Bitmap background with 16 bit color values of the form aBBBBBGGGGGRRRRR (if 'a' is set the pixel will be rendered...if not the pixel will be transparent)
}BgType;
```

Mapping used by `gen_pattern.py` / `source/main.c`:

- 8-bit → 5-bit: `v5 = round(v8 * 31 / 255)`, the value that minimises
  `|((v5<<3)|(v5>>2)) - v8|` under the expansion below. 255→31 and 0→0 are
  exact. `RGB8` above covers only this 8→5 direction.
- Stored word: `ARGB16(1, r5, g5, b5)` — bit 15 set so the bitmap pixel is
  rendered (the quoted `BgType_Bmp16` comment).
- HYPOTHESIS — expected 8-bit display of a 5-bit channel is `v5<<3 | v5>>2`
  (31→255, 0→0). No quoted libnds header, libnds documentation or capture
  states this 5→8 expansion; it is to be confirmed by the capture in §8.
  Under that expansion the gradient steps 0,16,…,240 use 5-bit values
  0,2,4,6,8,10,12,14,16,18,19,21,23,25,27,29 and display as
  0,16,33,49,66,82,99,115,132,148,156,173,189,206,222,239. Step 144 is a
  tie: v5=17 displays 140 (4 from 144), v5=18 displays 148 (also 4), and
  `round(144*31/255) = round(17.506) = 18` chooses 18. The deviations (e.g.
  32→33, 240→239) are the 5-bit quantisation, not a pattern error.

## 4. Why 256×256 and only 192 rows

FACT — the DS 16-bit bitmap sizes have no 256×192 mode, quoted verbatim:

```
$ sed -n '484,487p' "$DEVKITPRO/libnds/include/nds/arm9/background.h"
	BgSize_B16_128x128 = ((0 << 14) | BIT(7) | BIT(2) | (4 << 16)),  /*!< 128 x 128 pixel 16 bit bitmap background */
	BgSize_B16_256x256 = ((1 << 14) | BIT(7) | BIT(2) | (4 << 16)),  /*!< 256 x 256 pixel 16 bit bitmap background */
	BgSize_B16_512x256 = ((2 << 14) | BIT(7) | BIT(2) | (4 << 16)),  /*!< 512 x 512 pixel 16 bit bitmap background */
	BgSize_B16_512x512 = ((3 << 14) | BIT(7) | BIT(2) | (4 << 16)),  /*!< 1024 x 1024 pixel 16 bit bitmap background */
```

FACT — VRAM_A is 128 KB and maps to main background slot 0, quoted verbatim:

```
$ sed -n '56,67p' "$DEVKITPRO/libnds/include/nds/arm9/video.h"
	VRAM banks can be mapped to specific addresses for specific purposes.  In our
	case, any of the 4 main banks and several of the smaller ones can be mapped to
	the main 2D background engine.(A B C and D banks are referred to collectivly as main
	because	they are 128KB and flexible in usage)

	<pre>
	vramSetBankA(VRAM_A_MAIN_BG);
	</pre>

	The above would map the 128KB of VRAM_A to 0x6000000 for use as main background
	graphics and maps (you can offset the mapping as well and the available offsets
	are defined in the VRAM_A_TYPE enumeration)
```

FACT — the mode and BG3 enable bit:

```
$ grep -n "DISPLAY_BG3_ACTIVE\|MODE_5_2D" "$DEVKITPRO/libnds/include/nds/arm9/video.h"
432:#define DISPLAY_BG3_ACTIVE    (1 << 11)
474:	MODE_5_2D    =  0x10005, /**< \brief  4 2D backgrounds */
```

So the ROM uses `MODE_5_2D | DISPLAY_BG3_ACTIVE`, `vramSetBankA(VRAM_A_MAIN_BG)`
and `bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0)`.
256×256×2 B = 131072 B = 128 KB exactly. Only rows y=0..191 are painted;
rows 192..255 remain black and are not part of the DS top-screen output.

FACT — `consoleDemoInit` target, quoted verbatim:

```
$ sed -n '222,228p' "$DEVKITPRO/libnds/include/nds/arm9/console.h"
/*!	\brief Initialize the console to a default state for prototyping.
	This function sets the console to use sub display, VRAM_C, and BG0 and enables MODE_0_2D on the
	sub display.  It is intended for use in prototyping applications which need print ability and not actual
	game use.  Print functionality can be utilized with just this call.
	\return A pointer to the current PrintConsole.
*/
PrintConsole* consoleDemoInit(void);
```

## 5. Build (BUILD evidence)

FACT — full command and raw complete output (clean build, devkitPro present;
`$DEVKITARM`, `$DEVKITPRO`); the header was already generated from
`pattern.json`:

```
$ cd tools/ds_scale_test && make clean && make
clean ...
main.c
linking ds-scale-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-scale-test.nds
```

That is the complete non-empty output of the clean build.

FACT — ROM hash:

```
$ sha256sum tools/ds_scale_test/ds-scale-test.nds
76afec0633e8783c46e1aa5b8eb797d5b7ceaa11a1cb51aceaf084382a2c042a  tools/ds_scale_test/ds-scale-test.nds
```

FACT — the produced file is a DS ROM with a stock ARM7 present (excerpt; the
header is written by `ndstool`):

```
$ ndstool -i tools/ds_scale_test/ds-scale-test.nds | head -24
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
Header information:
0x00	Game title               	HOMEBREW
0x0C	Game code                	####
0x10	Maker code               	
0x12	Unit code                	0x02
0x13	Device type              	0x00
0x14	Device capacity          	0x01 (2 Mbit)
0x15	reserved 1               	00000000000000
0x1E	ROM version              	0x00
0x1F	reserved 2               	0x00
0x20	ARM9 ROM offset          	0x4000
0x24	ARM9 entry address       	0x2004800
0x28	ARM9 RAM address         	0x2004000
0x2C	ARM9 code size           	0x10BD4
0x30	ARM7 ROM offset          	0x14C00
0x34	ARM7 entry address       	0x2380000
0x38	ARM7 RAM address         	0x2380000
0x3C	ARM7 code size           	0xB374
0x40	File name table offset   	0x20000
0x44	File name table size     	0x9
0x48	FAT offset               	0x20200
0x4C	FAT size                 	0x0
```

No local ARM7 source is needed. FACT — the `%.nds` rule and its ARM7 come from
devkitPro's `ds_rules`, quoted verbatim:

```
$ sed -n '10,13p;37,40p' "$DEVKITARM/ds_rules"
CALICO		:=	$(DEVKITPRO)/calico
LIBNDS		:=	$(DEVKITPRO)/libnds
_ARM7_ELF	:=	-7 $(CALICO)/bin/ds7_maine.elf
_DEF_ICON	:=	$(CALICO)/share/nds-icon.bmp
#---------------------------------------------------------------------------------
%.nds: %.elf
	$(SILENTCMD)ndstool -c $@ -9 $< $(_ARM7_ELF) -b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)" $(_ADDFILES)
	$(SILENTMSG) built ... $(notdir $@)
```

`include/pattern.h` is generated by the Makefile rule
`include/pattern.h: pattern.json gen_pattern.py`; the generated file is
committed so the unit test can diff it.

## 6. measure_scale.py

```
python3 tools/ds_scale_test/measure_scale.py <capture.png>
```

Input: a cc3dsfs 1280×720 root screenshot or a cropped top-screen PNG. Only
the top-left 400×240 region is analysed (rows 0–239), per the EQ-4b layout
HYPOTHESIS (`docs/reports/eq4b-cc3dsfs-ds-capture.md:78-82`).

- Markers are matched by colour with tolerance 24 per channel: white
  r,g,b ≥ 231; red r ≥ 231, g,b ≤ 24; and analogously green/blue/yellow.
- Each corner marker is the **connected block** of its colour nearest the
  corresponding corner of the white-frame bbox (tl/tr/bl/br). Blocks use
  8-connectivity, and same-colour blocks whose bounding boxes are at most
  4 capture px apart are merged first: a real capture can split one marker
  into alternating 1-px columns (see the fixture below). A marker counts as
  found only if such a block has 16..400 pixels — one 8-DS-px square is about
  7.5×10 capture px at the measured 0.9375/1.25, and a ragged 12×10 block on
  the synthetic 1.5625× capture. The nearest block (block centroid to frame
  corner) wins, so same-colour pollution elsewhere in the frame is ignored.
- The white frame's white-pixel bounding box is the displayed DS image.
  `bbox_x0..bbox_x1` / `bbox_y0..bbox_y1` are inclusive capture pixels.
- `scale_x = (bbox_x1-bbox_x0+1)/256`, `scale_y = (bbox_y1-bbox_y0+1)/192`
  (capture px per DS px); `offset_x`/`offset_y` are the bbox top-left.
- `scale_x_check`, `scale_y_check`, `offset_x_check`, `offset_y_check` are an
  independent cross-check: scale/offset derived from the four marker centres
  (the centre of each `pattern.json` corner rect mapped to capture) minus the
  frame-bbox values. They are reported, never fatal; on a correct capture the
  offsets differ by < 1 capture px and the scales are unitless near 0.
- `strip_h_contrast` / `strip_v_contrast` are the mean luminance difference
  between the expected-white and expected-black samples of the 1-px
  alternating strips, mapped through scale+offset. Near 0 ⇒ the 1-px
  alternation was destroyed by the resampling filter; a high value ⇒ it
  survived. The paint-order overlap in §2 also lowers the value slightly.
  This is a resampling **hint** (HYPOTHESIS), not a filter identification.
- The report is one stable `key=value` line per key:
  `frame_found, corner_red, corner_green, corner_blue, corner_yellow,
  bbox_x0, bbox_y0, bbox_x1, bbox_y1, scale_x, scale_y, offset_x, offset_y,
  strip_h_contrast, strip_v_contrast, scale_x_check, scale_y_check,
  offset_x_check, offset_y_check`.
- Exit 0 only when the frame and all four corners are found. Missing markers
  print the report and exit 1 with a one-line message such as
  `measure_scale: white frame not found in top-left 400x240 region`;
  unreadable input exits 2. PIL is required.

SYNTHETIC example (not hardware; 256×192 pattern nearest-neighbour scaled to
400×240 on a black canvas):

```
$ python3 tools/ds_scale_test/measure_scale.py /tmp/synth.png
frame_found=1
corner_red=1
corner_green=1
corner_blue=1
corner_yellow=1
bbox_x0=0
bbox_y0=0
bbox_x1=399
bbox_y1=239
scale_x=1.562500
scale_y=1.250000
offset_x=0
offset_y=0
strip_h_contrast=245.039062
strip_v_contrast=231.093750
scale_x_check=-0.001524
scale_y_check=0.002747
offset_x_check=0.195122
offset_y_check=-0.263736
```

FACT — the committed fixture is a verbatim copy of the EQ-4c capture crop
(400×480; §8 of the EQ-4b report is the capture procedure). Copy and hash:

```
$ cp "$HOME/3ds/hwtest-staging/cc3d-out/sc1_5_crop400x480.png" tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
$ sha256sum tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
badbe06090eb98b11e301640ef9098670da9a940b4d05a38b3857a035e9a1a1a  tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
```

FACT — the fixed analyser on that fixture, raw complete output:

```
$ python3 tools/ds_scale_test/measure_scale.py tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
frame_found=1
corner_red=1
corner_green=1
corner_blue=1
corner_yellow=1
bbox_x0=30
bbox_y0=0
bbox_x1=269
bbox_y1=239
scale_x=0.937500
scale_y=1.250000
offset_x=30
offset_y=0
strip_h_contrast=89.161784
strip_v_contrast=156.618924
scale_x_check=0.001524
scale_y_check=-0.002747
offset_x_check=0.304878
offset_y_check=0.263736
```

FACT — the pre-fix analyser on the same fixture fails; this is the behaviour
the fixture regression test locks out. Verified by saving a copy of
`measure_scale.py` before the change (it reads `pattern.json` from its own
directory, so that file was copied next to it):

```
$ cp tools/ds_scale_test/measure_scale.py /tmp/opencode/eq4c/measure_scale_old.py
$ cp tools/ds_scale_test/pattern.json /tmp/opencode/eq4c/pattern.json
$ python3 /tmp/opencode/eq4c/measure_scale_old.py tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
measure_scale: corner marker(s) not found: corner_green corner_blue
frame_found=1
corner_red=1
corner_green=0
corner_blue=0
corner_yellow=1
bbox_x0=30
bbox_y0=0
bbox_x1=269
bbox_y1=239
scale_x=0.937500
scale_y=1.250000
offset_x=30
offset_y=0
strip_h_contrast=89.161784
strip_v_contrast=156.618924
```

(exit status 1; the error line is stderr, the `key=value` lines are stdout.)
The old rule took the bbox of *all* pixels of a colour class, so pollution
inside tolerance moved the green/blue bboxes out of their quadrants; the new
nearest-block rule finds the four markers at the frame corners (§6).

## 7. Tests

```
make test-ds-scale-test      # or: make test
```

FACT — suite output (the two synthetic cases check scale/offset within 0.5
capture px; the not-found case exits non-zero; the two real-capture cases
assert the fixture's exact keys and exit 0; PIL absence skips with a labelled
reason, and a missing fixture skips the two fixture cases with a labelled
reason):

```
$ make test-ds-scale-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_scale_test -p 'test_*.py'
..........
----------------------------------------------------------------------
Ran 10 tests in 0.735s

OK
```

`test_measure_scale.py` keeps the two synthetic geometry cases (1.5625×
nearest-neighbour and 1.0× centred) and the not-found case unchanged. The two
added cases assert exactly the fixture output quoted in §6: all four markers,
`bbox 30,0,269,239`, `scale_x 0.9375`, `scale_y 1.25`, `offset 30,0`, and
`|*_check| < 1`; the subprocess case also requires exit status 0.

`test_pattern.py` regenerates `include/pattern.h` into a temp file and diffs it
against the committed header, checks every `pattern.json` element is inside
256×192, checks the required rects/colours/step parameters, and checks the ROM
source uses the static bitmap path (`MODE_5_2D`, `DISPLAY_BG3_ACTIVE`,
`BgType_Bmp16`, `BgSize_B16_256x256`, `VRAM_A_MAIN_BG`) with no GX code. It
also checks every element colour and every gradient table entry is the
minimal-error 5-bit value under the §3 expansion.

FACT — full root suite (tail; includes `test-ds-scale-test` as a prerequisite):

```
$ make test
...
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_scale_test -p 'test_*.py'
..........
----------------------------------------------------------------------
Ran 10 tests in 0.723s

OK
...
----------------------------------------------------------------------
Ran 364 tests in 91.816s

OK (skipped=42)
```

## 8. Hardware procedure (owner; NOT run in this task)

1. Build: `make -C tools/ds_scale_test`.
2. Copy `tools/ds_scale_test/ds-scale-test.nds` to the SD card's
   `/roms/nds/` directory through the same flow used for EQ-4b
   (`docs/reports/eq4b-cc3dsfs-ds-capture.md:67-68`). Set your SD mount in
   `$SD_ROOT` and copy; no path from this repository is hard-coded:
   `cp tools/ds_scale_test/ds-scale-test.nds "$SD_ROOT/roms/nds/"`.
3. Run `ds-scale-test.nds` from TWiLight Menu++ on the New 3DS and leave it on
   the static screen (the bottom screen shows `DS SCALE TEST` and
   `id: ds-scale-test-1`).
4. Capture with the existing helper, with the capture board attached:
   `CC3DSFS_BIN="$HOME/path/to/cc3dsfs-build/bin" OUT_DIR="$HOME/path/to/out" sh tools/cc3dsfs_capture/cc_shots.sh ds 12 5`
5. Analyse a 1280×720 root screenshot (or crop the top-left 400×240 yourself):
   `python3 tools/ds_scale_test/measure_scale.py "$HOME/path/to/out/ds_1.png"`.
6. Record the report next to the capture. Interpretation beyond the printed
   numbers (which scale the New 3DS uses, which filter) is UNVERIFIED until
   that run.

The ROM is **NOT to be run on hardware in this task**; step 4 needs the
cc3dsfs binary built out of tree (not in this repository) and the console.

## 9. Privacy

Every command here uses `$HOME` / `$DEVKITPRO` / `$DEVKITARM` / `$SD_ROOT`;
no literal home-directory path is added. Check:

```
$ grep -rn -E '/(home|Users)/' tools/ds_scale_test docs/reports/eq4c-ds-scale-measurement.md
(no output; exit status 1)
$ git diff -U0 docs/EQUALITY_CHECK_DESIGN.md | grep -E '^\+.*/(home|Users)/'
(no output; exit status 1)
```

`docs/EQUALITY_CHECK_DESIGN.md` already contains literal home paths on three
lines from earlier work (lines 5, 6 and 808; not quoted here); those lines are
not touched, and the second command shows that the appended Update blocks add
none.

## 10. Requirement traceability

Each line states one requirement from the task and its evidence. BUILD =
command output quoted above; UNIT = unit test.

- **REQ new directory** `tools/ds_scale_test/` with ROM source, Makefile,
  README — BUILD: files in §1; `git status` in REQ-scope below.
- **REQ host analysis script + unit test wired into `make test` like
  `test-cc3dsfs`** — BUILD: `measure_scale.py`, `test_measure_scale.py`,
  `test_pattern.py`; root `Makefile` adds `test-ds-scale-test` next to
  `test-cc3dsfs` and lists it in `test` prerequisites; §7 quote.
- **REQ do not change other code** — BUILD: only the two root-Makefile hunks
  (`.PHONY` line and `test-ds-scale-test` target/dependency); `git status`
  REQ-scope below.
- **REQ never weaken tests** — BUILD: only additive targets/files; the full
  suite still runs and is green (§7).
- **REQ privacy `$HOME`, no literal home paths** — BUILD: §9 grep has no
  output.
- **REQ do not create/remove git worktrees** — BUILD: no `git worktree`
  command was run.
- **REQ FACT only with command + raw output** — BUILD: every FACT block in
  §§3–7 and 10 is a command plus its quoted output.
- **REQ libnds/devkitARM ROM, no TWL-HD GX code** — BUILD: `Makefile` uses
  the same `DEVKITARM`/`DEVKITPRO`/`libnds`/`ds_rules` flow as `arm9v2/` and
  `-lnds9` only; `test_pattern.py` forbids `glInit`/`fifo_hook`/`twlhd_`/
  `arm9v2` in the ROM source; §7 quote.
- **REQ top screen 16-bit bitmap MODE_5_2D + BG3, VRAM_A, static, no 3D** —
  BUILD: `source/main.c` calls `videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE)`,
  `vramSetBankA(VRAM_A_MAIN_BG)`, `bgInit(3, BgType_Bmp16,
  BgSize_B16_256x256, 0, 0)`, then only `swiWaitForVBlank()`; §3–4 quotes.
- **REQ pattern (1) 1-px white frame** — BUILD: `frame` element in
  `pattern.json`; checked by `test_pattern.py`; §2 table.
- **REQ pattern (2) 8×8 corner squares red/green/blue/yellow** — BUILD:
  `corner_*` elements; checked by `test_pattern.py`; §2 table.
- **REQ pattern (3) 1-px centre cross + 3×3 white block at (128,96)** — BUILD:
  `cross_v`, `cross_h`, `block3x3`; checked by `test_pattern.py`; §2 table.
- **REQ pattern (4) horizontal/vertical 1-px alternating probes** — BUILD:
  `hstrip`/`vstrip` with period 2, phase 0; checked by `test_pattern.py`.
- **REQ pattern (5) 16-step grey gradient y=140..155** — BUILD: `gradient`
  with `step_px`/`step_value` 16; checked by `test_pattern.py`.
- **REQ exact 5-bit colours + README BGR555→RGB888 mapping** — BUILD: §3
  quoted macros, mapping and quantisation table; colours and the gradient
  table are generated with the minimal-error rule `round(v8*31/255)` and bit
  15 set.
- **REQ bottom console `DS SCALE TEST` + sha-independent id** — BUILD:
  `source/main.c` prints `DS SCALE TEST` and `PATTERN_ID`
  (`ds-scale-test-1` from `pattern.json`); the ID does not depend on the ROM
  hash.
- **REQ ROM build target, documented command, build output + sha256** — BUILD:
  §5 full command, raw output, `sha256sum`, `ndstool` excerpt.
- **REQ `pattern.json` machine-readable; host test verifies ROM constants
  match** — BUILD: `pattern.json`; `gen_pattern.py`; `test_pattern.py` diffs
  the regenerated header with the committed one (§7).
- **REQ measure_scale (a) frame/corners by colour tolerance 24** — BUILD:
  `TOLERANCE = 24` and `_near`/`_white` in `measure_scale.py`; synthetic cases
  pass (§7).
- **REQ measure_scale (b) bbox, scale, offset** — BUILD: report keys and
  definitions in §6; synthetic 1.5625/1.25 and 1.0 cases verify within 0.5
  capture px (§7).
- **REQ measure_scale (c) alternating-strip contrast resampling hint** —
  BUILD: `strip_h_contrast`/`strip_v_contrast` in §6 and the synthetic run.
- **REQ measure_scale (d) stable `key=value` report + non-zero clear error** —
  BUILD: §6 key list, exit codes and error message; `test_missing_markers_
  exits_nonzero` passes (§7).
- **REQ unit test synthesises PIL captures (1.5625/1.25 and 1.0 centred),
  verifies scale/offset within 0.5 capture px, labelled PIL skip** — BUILD:
  `test_measure_scale.py`; 10 tests green (§7; was 7 before the EQ-4c work).
- **REQ README hardware procedure using existing capture flow** — BUILD: §8
  references `tools/cc3dsfs_capture/cc_shots.sh` and uses `$HOME`/`$SD_ROOT`.
- **REQ ROM not run on hardware in this task** — BUILD: §8 and status line;
  no hardware command was run.
- **REQ `make test` stays green** — BUILD: §7 full-run tail, exit 0.
- **REQ (EQ-4c A) corner markers = connected colour block nearest the frame
  corner, documented rule, per-colour tolerance** — BUILD: `measure_scale.py`
  `_merge_groups`/`_find_marker` and the §6 rule; the fixture output quoted in
  §6 is the evidence.
- **REQ (EQ-4c A) scale/offset from the frame bbox cross-checked with the
  marker centres as `*_check`, < 1 px discrepancy not fatal** — BUILD: §6 key
  definitions; quoted `offset_*_check` 0.304878/0.263736 and
  `scale_*_check` 0.001524/−0.002747; exit rule unchanged (§6).
- **REQ (EQ-4c A) real fixture committed, exact keys asserted, synthetic
  tests unchanged, and a test that fails with the old behaviour** — BUILD:
  `fixtures/sc1_5_crop400x480.png` (sha256 in §6), the two fixture cases in
  `test_measure_scale.py`, and the quoted pre-fix failure in §6; §7 output.
- **REQ (EQ-4c B) measurement report with the raw data and FACT/HYPOTHESIS
  labels** — BUILD: `docs/reports/eq4c-ds-scale-measurement.md`; the §6 quotes
  are repeated verbatim there.
- **REQ (EQ-4c B) append-only Update blocks after each Result EQ-4b** — BUILD:
  `git diff docs/EQUALITY_CHECK_DESIGN.md` shows additions only, one block
  after each of the three `Result EQ-4b` paragraphs.
- **REQ (EQ-4c C) cc3dsfs source read with quoted commands; proposed command
  line; UNVERIFIED where the source does not decide** — BUILD:
  `docs/reports/eq4c-ds-scale-measurement.md` §6 quotes and the proposed
  command; `$HOME/3ds/cc3dsfs` unmodified.
- **REQ (EQ-4c) no hardware action, nothing sent to the console, cc3dsfs not
  modified or run, no `git worktree` command** — BUILD: no hardware or cc3dsfs
  command was run in this task; EQ-4c report §7 quotes
  `git -C "$HOME/3ds/cc3dsfs" status --short` with no output.
- **REQ scope: `git status` only new files under `tools/ds_scale_test/` plus
  root-Makefile hunks** — BUILD: below.

FACT — scope after the original construction task (historical; build artefacts
removed by `make clean`; the only modified tracked file was the root
`Makefile`):

```
$ git status --short --untracked-files=all
 M Makefile
?? tools/ds_scale_test/Makefile
?? tools/ds_scale_test/README.md
?? tools/ds_scale_test/gen_pattern.py
?? tools/ds_scale_test/include/pattern.h
?? tools/ds_scale_test/measure_scale.py
?? tools/ds_scale_test/pattern.json
?? tools/ds_scale_test/source/main.c
?? tools/ds_scale_test/test_measure_scale.py
?? tools/ds_scale_test/test_pattern.py
```

FACT — EQ-4c working-tree scope (`git status --short --untracked-files=all`,
quoted verbatim):

```
 M docs/EQUALITY_CHECK_DESIGN.md
 M tools/ds_scale_test/README.md
 M tools/ds_scale_test/measure_scale.py
 M tools/ds_scale_test/test_measure_scale.py
?? docs/reports/eq4c-ds-scale-measurement.md
?? tools/ds_scale_test/fixtures/sc1_5_crop400x480.png
```
