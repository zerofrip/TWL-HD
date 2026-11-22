<!-- SPDX-License-Identifier: Apache-2.0 -->
# ds_vtxcolor_test — static DS top-screen default-vertex-colour probe

A static libnds DS ROM plus a host PNG analyser that together measure the
**initial "current vertex colour"** of the real DS 3D geometry engine: the
value the first vertex of the first polygon uses when the ROM issues no
`glColor` first.

This is the open question recorded in
`docs/reports/scene5-colour-diagnosis.md` and
`docs/reports/melonds-scene5-check.md`: DeSmuME resets the current vertex
colour to white, melonDS to black, and the real New 3DS in DS mode was observed
**not black** (photo) with the exact value **UNVERIFIED**.

**Status of this task:** BUILD/UNIT only. The ROM was **not run on hardware**
and was **not run in an emulator**. Build, source quoting and the analyser are
verified on the host; every statement about real hardware display/capture is
**HYPOTHESIS / UNVERIFIED** until the owner runs §9. Labels follow
`docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`: FACT = a command and its raw
output quoted here; HYPOTHESIS / UNVERIFIED = not measured.

## 1. Files

| Path | Purpose |
|---|---|
| `quads.json` | single source of truth: id, clear colour, five quads |
| `gen_quads.py` | generates `include/quads.h`; 8-bit <-> 5-bit helpers |
| `include/quads.h` | committed build-time output of `gen_quads.py` |
| `source/main.c` | ARM9 ROM: static five-quad 3D scene + bottom console text |
| `Makefile` | devkitARM/libnds build; `VARIANT=2` selects the reset variant |
| `capture_mapping.json` | EQ-4c measured DS→capture mapping constants |
| `measure_vtxcolor.py` | capture-PNG analyser, stable `key=value` report |
| `test_measure_vtxcolor.py` | synthetic-capture classification tests + mapping cross-check |
| `test_quads.py` | quads.json ↔ header ↔ ROM-source consistency tests |

The ROM links only `-lnds9`; it includes `<nds.h>` and links no TWL-HD
GX/`common/` code (asserted by `test_quads.py`).

## 2. Scene

Top screen: 256×192 3D output (`MODE_0_3D`), static, no animation. Clear
colour is 5-bit `(8,8,8)` (mid-grey; under the 5→8 expansion of
`tools/ds_scale_test/README.md` §3 it displays as `(66,66,66)`), so black and
white quads are both distinguishable from the background.

| Quad | Rect (x, y, w, h) | DS pixels | Colour command before its vertices |
|---|---|---|---|
| `qA` | 16, 32, 64, 64 | x 16..79, y 32..95 | **none** — default current vertex colour |
| `qB` | 96, 32, 64, 64 | x 96..159, y 32..95 | `glColor(RGB15(31,31,31))` |
| `qC` | 176, 32, 64, 64 | x 176..239, y 32..95 | `glColor(RGB15(0,0,0))` |
| `qD` | 16, 112, 64, 64 | x 16..79, y 112..175 | `glColor(RGB15(16,16,16))` |
| `qE` | 96, 112, 64, 64 | x 96..159, y 112..175 | `glColor(RGB15(31,0,0))` |

`source/main.c` emits the quads in that order inside one `glBegin(GL_QUADS)`
list in the very first submitted frame. Colour writes are guarded by
`q->has_colour`, so quad A's four vertices are the first vertices after
`glBegin` and no `glColor`/`GFX_COLOR` command precedes them. Texturing, fog,
edge marking and antialiasing are disabled with
`glDisable(GL_TEXTURE_2D | GL_FOG | GL_OUTLINE | GL_ANTIALIAS)`. Lighting is
per-polygon on the DS (`POLY_FORMAT_LIGHT0..3`, which stay 0), so
`glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE)` does not enable it.

The list is submitted **once** (`draw_quads(); glFlush(0);`) and the ROM then
idles in `swiWaitForVBlank()`. Re-submitting the same list every frame would
leave the GX current vertex colour at qE's red from the previous frame, so quad
A would no longer show the default colour. With no further `GFX_FLUSH` the
first frame stays displayed (HYPOTHESIS until §9; the DS 3D layer scans out the
last swapped buffer).

Bottom screen: `consoleDemoInit()` prints `DS VTXCOLOR TEST`, the
sha-independent id `ds-vtxcolor-test-1`, and the variant label
`first-frame` or `after-empty-frame`.

## 3. libnds: no colour write before the first vertex

FACT — the only writers of the vertex-colour register in the linked libnds
headers are the two `glColor*` inline functions. Full command and raw output:

```
$ grep -n "GFX_COLOR" "$DEVKITPRO/libnds/include/nds/arm9/video.h" "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"
/opt/devkitpro/libnds/include/nds/arm9/video.h:730:#define GFX_COLOR             (*(vu32*) 0x04000480)
/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:379:#define FIFO_COLOR				REG2ID(GFX_COLOR) /*!< \brief packed command for vertex color directly<BR><A HREF="http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes">GBATEK http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes</A> */
/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:569: void glColor3b(u8 red, u8 green, u8 blue) { GFX_COLOR = (vu32)RGB15(red>>3, green>>3, blue>>3); }
/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:575: void glColor(rgb color) { GFX_COLOR = (vu32)color; }
```

`glInit` and `glBegin` are absent from that list. FACT — `glInit` body
(installed `$DEVKITPRO/libnds` ships headers and libraries but not sources, so
this is the matching source tree at `$HOME/3ds/libnds`; the installed
`videoGL.h` is byte-identical, quoted after the body):

```
$ sed -n '106,146p' "$HOME/3ds/libnds/source/arm9/videoGL_base.c"
void glInit(void) {
//---------------------------------------------------------------------------------
	powerOn(POWER_3D_CORE | POWER_MATRIX);	// enable 3D core & geometry engine

	// Initialize texture VRAM manager (if needed)
	_glInitGlobals();

	while (GFX_STATUS & (1<<27)); // wait till gfx engine is not busy

	// Clear the FIFO
	GFX_STATUS |= (1<<29);

	// Clear overflows from list memory
	glResetMatrixStack();

	// prime the vertex/polygon buffers
	glFlush(0);

	// reset the control bits
	GFX_CONTROL = 0;

	// reset the rear-plane(a.k.a. clear color) to black, ID=0, and opaque
	glCurClearColor = 0;
	glClearColor(0,0,0,31);
	glClearPolyID(0);

	// reset the depth to it's max
	glClearDepth(GL_MAX_DEPTH);

	GFX_TEX_FORMAT = 0;
	GFX_POLY_FORMAT = 0;

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
}
```

FACT — installed header and the quoted source header are identical:

```
$ diff -q "$HOME/3ds/libnds/include/nds/arm9/videoGL.h" "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"; echo "exit=$?"
exit=0
```

FACT — the libnds source tree that contains `glInit` never names `GFX_COLOR`
raw (the colour writers are header inlines only):

```
$ grep -rn "GFX_COLOR" "$HOME/3ds/libnds/source"; echo "exit=$?"
exit=1
```

FACT — `glBegin` only writes the primitive-mode register:

```
$ sed -n '549p;554p' "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"
 void glBegin(GL_GLBEGIN_ENUM mode) { GFX_BEGIN = mode; }
 void glEnd(void) { GFX_END = 0; }
```

`glClearColor`/`glClearPolyID` write `GFX_CLEAR_COLOR`, and `glPolyFmt` writes
`GFX_POLY_FORMAT`; neither is `GFX_COLOR`. **Outcome:** no libnds init path
issues a vertex-colour write, so the ROM uses the standard `glInit` order and
does **not** bypass it.

## 4. Orthographic projection: vertex (x, y) → DS pixel (x, y)

The ROM calls:

```c
glViewport(0, 0, 255, 191);
glMatrixMode(GL_PROJECTION);
glLoadIdentity();
glOrthof32(0, 256, 192, 0, -1 << 12, 1 << 12);
glMatrixMode(GL_MODELVIEW);
glLoadIdentity();
```

and submits raw integer vertices (`glVertex3v16((v16)x, (v16)y, 0)`, through
`vertex_px()`).

FACT — libnds GL2D uses exactly this convention and documents the mapping:

```
$ sed -n '28,38p' "$HOME/3ds/libnds/source/arm9/gl2d.c"
	
	Direct copy of glVertex3v16()
	I made this since VideoGL don't have a
	glVertex#i() wrappers

******************************************************************************/
static inline void gxVertex3i(v16 x, v16 y, v16 z)
{
	GFX_VERTEX16 = (y << 16) | (x & 0xFFFF);
	GFX_VERTEX16 = ((u32)(u16)z);
}
$ sed -n '126,140p' "$HOME/3ds/libnds/source/arm9/gl2d.c"
	every vert or the modelview matrix by the same
	amount to make it work.  That's gonna give us
	lots of overflows and headaches.  So we "scale down"
	and use an all integer value.

******************************************************************************/
void SetOrtho( void )		
{

	glMatrixMode( GL_PROJECTION );     // set matrixmode to projection
	glLoadIdentity();				 // reset
	glOrthof32( 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, -1 << 12, 1 << 12 );  // downscale projection matrix
	
}

```

Reasoning: `glOrthof32` takes its extents as plain pixel counts (`0, 256` and
`192, 0`), and the GL2D wrapper writes the vertex value unshifted. The
projection therefore maps vertex x 0..256 to the 256-pixel viewport width and
the `bottom=192, top=0` order flips Y so that `+y` is **down** the DS screen
(the DS framebuffer's Y axis), i.e. vertex `(x, y)` lands on pixel `(x, y)`.
Quad corners at integer coordinates therefore rasterise on pixel boundaries
(x 16..79 means vertices at 16 and 80). This exact 1:1 result is
**UNVERIFIED** on hardware until §9; the centre 16×16 patch median absorbs a
±1-pixel mapping error.

## 5. Build (BUILD evidence)

FACT — full command and raw complete output, default variant (clean build):

```
$ cd tools/ds_vtxcolor_test && make clean && make
clean ...
main.c
linking ds-vtxcolor-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-vtxcolor-test.nds
```

FACT — default-variant ROM hash and the code-size header line:

```
$ sha256sum tools/ds_vtxcolor_test/ds-vtxcolor-test.nds
15c4e48dacfa2f1c08b2b408e28a571cf9f3f30fa0770e51240c65d42cddfce7  tools/ds_vtxcolor_test/ds-vtxcolor-test.nds
$ ndstool -i tools/ds_vtxcolor_test/ds-vtxcolor-test.nds | head -24
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
0x2C	ARM9 code size           	0x10A64
0x30	ARM7 ROM offset          	0x14C00
0x34	ARM7 entry address       	0x2380000
0x38	ARM7 RAM address         	0x2380000
0x3C	ARM7 code size           	0xB374
0x40	File name table offset   	0x20000
0x44	File name table size     	0x9
0x48	FAT offset               	0x20200
0x4C	FAT size                 	0x0
```

FACT — the `VARIANT=2` build reuses the same filename, so it must be built
after a clean; full command and raw complete output:

```
$ cd tools/ds_vtxcolor_test && make clean && make VARIANT=2
clean ...
main.c
linking ds-vtxcolor-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-vtxcolor-test.nds
```

FACT — variant-2 ROM hash (different from the default variant) and code size
(`0x10AF4` vs `0x10A64`):

```
$ sha256sum tools/ds_vtxcolor_test/ds-vtxcolor-test.nds
4862ae4db81eee9abaa4ec8207f2934f25f7bfb5ce25e988613a274c06255fca  tools/ds_vtxcolor_test/ds-vtxcolor-test.nds
$ ndstool -i tools/ds_vtxcolor_test/ds-vtxcolor-test.nds | sed -n '12,17p'
0x1F	reserved 2               	0x00
0x20	ARM9 ROM offset          	0x4000
0x24	ARM9 entry address       	0x2004800
0x28	ARM9 RAM address         	0x2004000
0x2C	ARM9 code size           	0x10AF4
0x30	ARM7 ROM offset          	0x14C00
```

Variant selection is compiled in: the bottom console prints `first-frame` for
the default build and `after-empty-frame` for `VARIANT=2`. The build artefacts
(`*.nds`, `*.elf`, `build/`, `*.arm9`) are ignored by the existing
`.gitignore`. `include/quads.h` is generated by
`include/quads.h: quads.json gen_quads.py` and committed so the unit test can
diff it.

## 6. `VARIANT=2` semantics and what it does not prove

`VARIANT=2` adds, after the normal `glInit`/projection setup and before the
first quad list:

```c
glResetMatrixStack();
glFlush(0);          /* submit a display list with zero vertices */
swiWaitForVBlank();
swiWaitForVBlank();  /* ensure one fully empty frame was displayed */
```

`glInit` already wrote `GFX_CONTROL = 0`, so this is the minimal ARM9-reachable
"reset + empty frame" available through libnds. It lets the owner compare the
**first frame's** default vertex colour with the value after **one empty
displayed frame**.

What it does **not** prove: this is not a power-on reset of the GX state. The
ROM cannot know what the loader / TWiLight Menu++ / nds-bootstrap wrote to the
GX registers before `main()` ran. Whatever that pre-state is, it is part of
what both variants measure (HYPOTHESIS, §11).

## 7. `measure_vtxcolor.py`

```
python3 tools/ds_vtxcolor_test/measure_vtxcolor.py <capture.png>
```

Input: a cc3dsfs 1280×720 root screenshot or a cropped top-screen PNG. Only
the top-left 400×240 region is analysed, as in `measure_scale.py`.

- Shared constants live in `capture_mapping.json`: `scale_x=1.25`,
  `scale_y=1.25`, `offset_x=40`, `offset_y=0` (corrected 2026-11-23; see
  `docs/reports/eq4d-ds-vertex-colour-hw.md`). The mapping is
  `capture_x = offset_x + (ds_x + 0.5)·scale_x − 0.5` (and the same for y);
  DS pixel (x, y) appears at capture (40 + 1.25x, 1.25y). `measure_scale.py`
  is not modified. `test_measure_vtxcolor.py` asserts the constants equal what
  `measure_scale.py` measures on a synthetic uniform-1.25 capture.
- For each quad A..E the analyser maps the **centre 16×16 DS-pixel patch**
  through the mapping and takes the median R, G, B over the covered capture
  pixels. The median absorbs the 1.25 non-integer nearest-neighbour resampling.
- The background patch is the centre 16×16 patch at DS `(128, 8)` — above all
  quads, inside 0..255×0..191.
- Report, one `key=value` per line, in this order:
  `geometry_found`, `qA_rgb`, `qB_rgb`, `qC_rgb`, `qD_rgb`, `qE_rgb`,
  `bg_rgb`, `default_colour_class`. RGB values are `R,G,B` decimal.
- A quad counts as found when its median differs from the background median by
  at least 16 in any channel. Exit status: **0** when the background and all
  five quads are found; **1** when they are not (report still printed, one
  stderr line lists the missing names); **2** for usage, missing PIL, or
  unreadable input.
- Classification (`TOLERANCE = 12` per channel, checked in this order):
  `same_as_qB`, `same_as_qC`, `same_as_qD`, `same_as_qE` (qA vs the *measured*
  reference median), then `black` (within 12 of (0,0,0)), `white` (within 12
  of (255,255,255)), `grey` (max channel − min channel ≤ 12), else `other`.
  The relative classes are robust to small capture deviations; the absolute
  black/white classes necessarily use ideal 8-bit values. The raw medians are
  always printed, so the owner can reclassify.

SYNTHETIC example (not hardware). The first command writes a synthetic capture
into `$TMPDIR` by inverting the mapping with quad A set to white; the second
analyses it:

```
$ python3 -c 'import sys; sys.path.insert(0, "tools/ds_vtxcolor_test"); import test_measure_vtxcolor as t; t.synthetic_capture((255,255,255)).save(sys.argv[1])' "$TMPDIR/vtx_synth_white.png"
$ python3 tools/ds_vtxcolor_test/measure_vtxcolor.py "$TMPDIR/vtx_synth_white.png"
geometry_found=1
qA_rgb=255,255,255
qB_rgb=255,255,255
qC_rgb=0,0,0
qD_rgb=132,132,132
qE_rgb=255,0,0
bg_rgb=66,66,66
default_colour_class=same_as_qB
```

(exit status 0.)

## 8. Tests

```
make test-ds-vtxcolor-test      # or: make test
```

FACT — suite output (15 tests; the two additions are the real-fixture case and
the synthetic `measure_scale.py` cross-check, replacing the old
mis-decoded-fixture cross-check):

```
$ make test-ds-vtxcolor-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_vtxcolor_test -p 'test_*.py'
...............
----------------------------------------------------------------------
Ran 15 tests in 0.739s

OK
```

FACT — the analyser on the corrected 2026-11-23 capture
(`fixtures/vt1b_2_top400x240.png`, sha256
`f48c9f1be832516466b45e74dd35728f64aa0c5963587c078642a2bf3a8acb1c`), raw
complete output:

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

- `test_measure_vtxcolor.py` synthesises 400×240 captures in memory with PIL
  (nearest/inverse mapping through the same constants and the `v5<<3|v5>>2`
  expansion) and checks: A==B white ⇒ `same_as_qB`, A==C black ⇒
  `same_as_qC`, A=(200,200,200) ⇒ `grey`, the reference medians and
  background, a subprocess success case (report keys + exit 0), a flat capture
  ⇒ non-zero exit with `geometry not found`, the real fixture above (within
  tolerance 12), and the `measure_scale.py` cross-check on a synthetic
  uniform-1.25 capture. PIL absence skips the class with the labelled reason
  `PIL is not available; measure_vtxcolor capture tests skipped`; a missing
  fixture skips only the fixture case with its own label.
- `test_quads.py` regenerates `include/quads.h` into a temp file and diffs the
  committed header, checks every quad is inside 256×192 and in the required
  A..E order/rect/colour, checks qA has no colour, checks each 5-bit channel
  is the minimal-error value under `v5<<3|v5>>2`, and checks the ROM source
  uses the libnds 3D path (`MODE_0_3D`, `glInit`, `glBegin`, `glColor`,
  `glVertex3v16`, `glOrthof32`, `consoleDemoInit`, `DSVTX_ID`,
  `swiWaitForVBlank`) with no TWL-HD markers (`fifo_hook`, `twlhd_`,
  `arm9v2`, `twlhd_gx_interp`) and with the colour write guarded after the
  start of the quad loop.

FACT — full root suite tail (the new target is a prerequisite of `test`):

```
$ make test 2>&1 | tail -n 12
...............................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 364 tests in 89.751s

OK (skipped=42)
/tmp/tmpc1mpzkdt/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

## 9. Hardware procedure (owner; NOT run in this task)

1. Build both variants, cleaning between them:
   - `make -C tools/ds_vtxcolor_test clean && make -C tools/ds_vtxcolor_test`
   - `make -C tools/ds_vtxcolor_test clean && make -C tools/ds_vtxcolor_test VARIANT=2`
   Record each sha256 (§5) and rename the second copy if both are kept, e.g.
   `ds-vtxcolor-test-2.nds`; the bottom-screen `variant:` line is the
   authoritative label.
2. Copy a ROM to the SD card's `/roms/nds/` directory through the existing
   EQ-4b flow. Set the SD mount in `$SD_ROOT`:
   `cp tools/ds_vtxcolor_test/ds-vtxcolor-test.nds "$SD_ROOT/roms/nds/"`.
3. Run it from TWiLight Menu++ on the New 3DS in DS mode and leave it on the
   static screen (bottom shows `DS VTXCOLOR TEST`, the id and the variant).
4. Capture the top screen. `tools/cc3dsfs_capture/cc_shots.sh` launches
   cc3dsfs itself without the EQ-4c layout flags (`cc_shots.sh:12`), so either
   run cc3dsfs with those flags and use the script's xwd→png loop, or record
   the exact invocation used. The flags actually used for the 2026-11-23
   `vt1b` capture (the `cc_shots_flags.sh vt1b 3 3` equivalent) were
   `--enabled_both 0 --enabled_top 1 --scaling_top 1`:
   ```
   xvfb-run -a -s "-screen 0 1280x720x24" "$CC3DSFS_BIN" --auto_connect --no_audio --no_auto_save --enabled_both 0 --enabled_top 1 --scaling_top 1
   ```
   with `CC3DSFS_BIN="$HOME/path/to/cc3dsfs-build/bin/cc3dsfs"` and
   `OUT_DIR="$HOME/path/to/out" sh tools/cc3dsfs_capture/cc_shots.sh ...` for
   the screenshot loop. No literal home path is recorded here.
5. Analyse each shot:
   `python3 tools/ds_vtxcolor_test/measure_vtxcolor.py "$HOME/path/to/out/<shot>.png"`.
6. Repeat with the `VARIANT=2` ROM to compare "first frame" with "after an
   empty displayed frame". Also repeat on a DS Phat/Lite if available
   (`scene5-colour-diagnosis.md:217`).

Record the raw report next to each capture. Interpretation — which value real
hardware uses, and whether the emulators match — is **UNVERIFIED** until these
runs. The hardware answer is `default_colour_class` plus the raw medians; it is
recorded by the owner later, not in this task.

## 10. Privacy

Every command here uses `$HOME` / `$DEVKITPRO` / `$DEVKITARM` / `$SD_ROOT` /
`$CC3DSFS_BIN`; no literal home-directory path is added. Check (run after
`make clean`, because built ELFs embed debug paths):

```
$ grep -rn -E '/(home|Users)/' tools/ds_vtxcolor_test
(no output; exit status 1)
```

## 11. Limits and hypotheses

- **First-frame retention (HYPOTHESIS):** the ROM submits the scene once and
  then only waits for VBlanks. This is required because the GX current vertex
  colour persists across display lists, so re-submitting quad A would show
  qE's red, not the default. That the DS keeps scanning out the last swapped
  3D buffer with no further `GFX_FLUSH` is expected but **UNVERIFIED** until
  the capture.
- **Pre-loader GX state (HYPOTHESIS):** the ROM cannot know what TWiLight /
  nds-bootstrap / the loader left in the GX registers before `main()`.
  Whatever that state is, it is part of what is measured. `VARIANT=2` only
  compares first frame vs after an empty displayed frame; it is not a
  power-on reset.
- **Projection/rasterisation (UNVERIFIED):** the 1:1 vertex→pixel claim is
  proven only by the libnds GL2D convention quoted in §4. DS half-pixel and Y
  flip details could shift a quad by one pixel; the centre 16×16 medians are
  tolerant to that.
- **Capture colour path:** the 2026-11-23 `vt1b_2` fixture shows an identity
  path on the sampled quads (white 255,255,255; grey 132,134,132). The old
  "non-identity path (DS white → (255,251,255), black → 0..8)" reading came
  from a mis-decoded capture and is superseded; `same_as_qX` still compares
  against the measured reference quads, and the raw medians are always
  printed.
- **`cc_shots.sh` flags:** plain `cc_shots.sh` may produce a layout different
  from the 400×240 assumption; the analyser then exits 1 with a clear
  "geometry not found" message.
- **Variant filename:** both variants produce `ds-vtxcolor-test.nds`; clean
  between builds or rename the second, and use the console variant label
  and the quoted sha256s to identify what was measured.
- Nothing in this task is a hardware FACT.

## 12. Requirement traceability

Each line states one requirement from the task and its evidence. BUILD =
command output quoted above; UNIT = unit test.

- **REQ new directory** `tools/ds_vtxcolor_test/` with ROM source, Makefile,
  README, `measure_vtxcolor.py`, unit test — BUILD: §1 file table;
  `git status` in the scope line below.
- **REQ modelled on `tools/ds_scale_test/`, test wired into `make test` like
  `test-ds-scale-test` / `test-cc3dsfs`** — BUILD: same devkitARM/`ds_rules`
  Makefile flow, committed generated header, `key=value` analyser, labelled
  PIL skip; root `Makefile` adds `test-ds-vtxcolor-test` and lists it in the
  `test` prerequisites; §8 output.
- **REQ do not change other code** — BUILD: only the two root-Makefile hunks
  (`.PHONY` line, new target + `test` prereq); every other path is new; scope
  line below.
- **REQ never weaken tests** — BUILD: additive target/files only; the existing
  suites are untouched and the full root suite still runs; §8 output.
- **REQ privacy `$HOME`, no literal home paths** — BUILD: §10 grep has no
  output.
- **REQ do not create/remove git worktrees; no hardware/emulator action** —
  BUILD: no `git worktree`, hardware or emulator command was run in this task.
- **REQ FACT only with full command + raw output; hardware HYPOTHESIS/
  UNVERIFIED** — BUILD: every FACT block in §§3–8 and 10 is a command and its
  quoted output; §9/§11 label every hardware statement UNVERIFIED.
- **REQ libnds/devkitARM ROM, no TWL-HD GX code; top screen 3D only; bottom
  console `DS VTXCOLOR TEST` + ROM id** — BUILD: `Makefile` uses
  `DEVKITARM`/`DEVKITPRO`/`libnds`/`ds_rules`, `-lnds9` only; `source/main.c`
  uses `MODE_0_3D` + `glInit` and `consoleDemoInit` prints the title and
  `DSVTX_ID`; `test_quads.py` forbids TWL-HD markers; §8.
- **REQ static, no animation; standard init; mid-grey clear `(8,8,8)`;
  texturing/lighting/fog/edge marking/AA disabled; alpha 31, cull none;
  orthographic (x,y)→pixel** — BUILD: `source/main.c` and §2/§4; the display
  list is submitted once and the ROM then idles, so quad A keeps the default
  colour instead of qE's leftover red.
- **REQ A default at 16..79, 32..95 with no colour command before it; B white
  96..159; C black 176..239; D grey16 16..79,112..175; E red31
  96..159,112..175, in that order** — BUILD: `quads.json`, `include/quads.h`,
  `source/main.c`; `test_quads.py` checks the exact order/rects/colours and
  the guarded colour write.
- **REQ check and quote libnds `glInit`/`glBegin`/init colour writes; bypass
  if found; state unknowable loader pre-state under limits** — BUILD: §3 grep
  + `glInit` body + header diff; outcome: no bypass needed; §11 limit.
- **REQ `make VARIANT=2`: deliberate minimal reset + one empty displayed
  frame; label what it does/does not prove** — BUILD: `Makefile`
  `-DDSVTX_VARIANT_RESET=1`, `source/main.c` block, §5 both builds/hashes, §6
  semantics and limits.
- **REQ `measure_vtxcolor.py`: 400×240 top-left region, shared mapping
  constants json (1.25/1.25, offset 40,0), centre 16×16 medians for A..E and
  background, `key=value` lines, `default_colour_class` from the listed
  domain at tolerance 12, non-zero only on geometry** — BUILD: §7 spec and
  synthetic output; `test_measure_vtxcolor.py` synthetic cases.
- **REQ unit tests synthesise PIL PNGs, labelled skip when PIL missing, cases
  A==B / A==C / distinct grey, verify report** — BUILD: §8; three named cases
  plus reference/background, subprocess, geometry-failure and fixture
  cross-check.
- **REQ mapping cross-check against `measure_scale.py` on the committed
  fixture (or labelled skip)** — BUILD: `test_mapping_matches_measure_scale_on
  _synthetic_capture`; §8. (Corrected 2026-11-23: the cross-check now runs on
  a synthetic uniform-1.25 capture because the old committed fixture was
  mis-decoded input; see §13.)
- **REQ quote build output, ROM sha256 and `ndstool -i` excerpt; document the
  owner hardware procedure (SD copy, TWiLight, capture flags, measure)** —
  BUILD: §5 and §9.
- **REQ ROM not run on hardware in this task** — BUILD: status line and §9;
  no hardware command was run.
- **REQ `make test` stays green** — BUILD: §8 full-suite tail.
- **REQ hardware answer recorded later, not in this task (acceptance 8)** —
  BUILD: §9 final paragraph; not measured here.

FACT — working-tree scope (`git status --short --untracked-files=all`, after
removing build artefacts; the only modified tracked file is the root
`Makefile`):

```
$ git status --short --untracked-files=all
 M Makefile
?? tools/ds_vtxcolor_test/Makefile
?? tools/ds_vtxcolor_test/README.md
?? tools/ds_vtxcolor_test/capture_mapping.json
?? tools/ds_vtxcolor_test/gen_quads.py
?? tools/ds_vtxcolor_test/include/quads.h
?? tools/ds_vtxcolor_test/measure_vtxcolor.py
?? tools/ds_vtxcolor_test/quads.json
?? tools/ds_vtxcolor_test/source/main.c
?? tools/ds_vtxcolor_test/test_measure_vtxcolor.py
?? tools/ds_vtxcolor_test/test_quads.py
```

## 13. Erratum (2026-11-23)

The 2026-11-22 capture was decoded wrongly: `tools/cc3dsfs_capture/xwd2png.py`
treated the 24-bpp XWD as 32-bpp BGRX, which squeezes the picture horizontally
by 3/4. All earlier mapping/colour statements taken from that capture are
superseded. Corrected facts, from the 2026-11-23 `vt1b` capture (a single
capture, so the 1.25× uniform scale and the x=40 offset are single-capture
basis):

- `capture_mapping.json` is now `scale_x=1.25, scale_y=1.25, offset_x=40,
  offset_y=0`.
- The real fixture `fixtures/vt1b_2_top400x240.png` measures qA=white,
  qB=white, qC=black, qD=(132,134,132), qE=red, background (66,69,66) — see
  §8.
- The old `sc1_5_crop400x480.png` numbers (0.9375 x-scale, 30-px offset,
  "DS white (255,251,255)", "0.9375/1.25") came from mis-decoded input and are
  regression-pinned only, not hardware measurements.

Report: `docs/reports/eq4d-ds-vertex-colour-hw.md`.
