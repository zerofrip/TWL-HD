<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4h — DS polygon-alpha convention (a/31 vs (a+1)/32): build + offline checks

**Status:** BUILD / OFFLINE + EMULATOR MEASURED; not run on hardware. Both
first-attempt blockers are fixed (§5): the faulting `iprintf(...)` call is
removed and the quads are at z=+64 (in front of the z=0 backdrops under LESS).
Built unmodified from the repo, the ROM renders in DeSmuME via the out-of-tree
harness and the classifier returns a verdict. The result is EMULATOR (DeSmuME)
only; real hardware is **NOT YET MEASURED**. Labels follow
`docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`: **FACT** = a command and its
complete raw output quoted here; **SOURCE-READ** = quoted source file:line;
**HYPOTHESIS** / **UNVERIFIED** = not measured.

**Question.** For a DS translucent polygon with 5-bit polygon alpha `a`
(1..30), is the blend weight `A1 = a/31` or `A2 = (a+1)/32`? Which does
DeSmuME use, and which does our own PICA conversion path use?

## 1. What was built

New directory `tools/ds_alpha_test/` (modelled on `tools/ds_vtxcolor_test/`):
static libnds ROM `source/main.c` → `ds-alpha-test.nds`, shared
`quads.json`/`gen_quads.py` → `include/quads.h`, `expected.py`,
`measure_alpha.py`, the reused `capture_mapping.json`, and three test modules
(tool README §1).

Geometry (DS pixels, 256×192) is defined once in `quads.json` and summarised in
the tool README §2: two opaque backdrops plus seven translucent white quads per
half (alpha `4..28`, distinct polygon ids, quads at z=+64).

### 1.1 State settings (and why)

FACT — the DISP3DCNT bits named in the installed header:

```
$ sed -n '247,254p' "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"
	GL_TEXTURE_2D      = (1<<0),  /*!< enable/disable textures on the geometry engine */
	GL_TOON_HIGHLIGHT  = (1<<1),  /*!< enable = Highlight shading; disable = Toon shading */
	GL_ALPHA_TEST      = (1<<2),  /*!< whether to use the alpha threshold set in glAlphaFunc() */
	GL_BLEND           = (1<<3),  /*!< enable/disable alpha blending */
	GL_ANTIALIAS       = (1<<4),  /*!< nable/disable edge antialiasing; polygons must have different polygon IDs for the effect to work and the rear plane must be clear */
	GL_OUTLINE         = (1<<5),  /*!< enable/disable edge coloring; the high 3bits of the polygon ID determine the color; glSetOutlineColor() sets the available colors */
	GL_FOG_ONLY_ALPHA  = (1<<6),  /*!< enable = fade into background?; disable = don't fade? */
	GL_FOG             = (1<<7),  /*!< enables/disables fog */
```

FACT — POLY_ALPHA / POLY_ID write GFX_POLY_FORMAT:

```
$ sed -n '531,544p' "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"
/*! \fn  u32 POLY_ALPHA(u32 n)
\brief used in glPolyFmt() to set the alpha level for the following polygons, set to 0 for wireframe mode
\param n the level of alpha (0-31)
\return value for hw register*/
 u32 POLY_ALPHA(u32 n) { return (u32)((n) << 16); }

GL_STATIC_INL
/*! \fn  u32 POLY_ID(u32 n)
\brief used in glPolyFmt() to set the Polygon ID for the following polygons
\param n the ID to set for following polygons (0-63)
\return value for hw register
*/
 u32 POLY_ID(u32 n) { return (u32)((n)<<24); }
```

FACT — `glInit` zeroes GFX_CONTROL (so every listed feature starts disabled):

```
$ grep -n "GFX_CONTROL = 0" "$HOME/3ds/libnds/source/arm9/videoGL_base.c"
125:	GFX_CONTROL = 0;
```

Set in `source/main.c`: `MODE_0_3D`, `glInit`, then `glEnable(GL_BLEND)` (the
only DISP3DCNT bit set) and `glDisable(GL_TEXTURE_2D | GL_ALPHA_TEST |
GL_ANTIALIAS | GL_OUTLINE | GL_FOG)`; `GL_TOON_HIGHLIGHT` and lighting
(`POLY_FORMAT_LIGHT0..3`) stay 0. Each polygon is preceded by
`glPolyFmt(POLY_ALPHA(p->alpha) | POLY_CULL_NONE | POLY_ID(p->poly_id))`
(`POLY_FOG` never set); the POLYGON_ATTR depth-test bit is never set (0).
Clear: `glClearColor(0,0,0,31)`, `glClearPolyID(63)`, `glClearDepth(GL_MAX_DEPTH)`.

**Depth/z (EMULATOR-verified):** backdrops at z=0 and quads at z=+64; a larger
source z maps to a smaller post-transform z (`0x40000000` vs `0x3f000000`),
nearer under LESS, so all 14 quads survive. The z=−64 state is kept in §5.1;
only the emulator direction is measured, the hardware convention is UNVERIFIED.

## 2. Expected tables (`expected.py`)

Formula: 5-bit `v5` expands to 8 bits with `v8 = (v5<<3)|(v5>>2)`; weights
A1 = `a/31`, A2 = `(a+1)/32`, source 5-bit 31. The five variants print their
formulas in the output header below; `6bit_round` models DeSmuME's internal
6-bit pipeline (`expand6(v5) = (v5<<1)|(v5>>4)`, A2 blend
`v6 = ((a+1)*S6 + (31-a)*B6) >> 5`, output `round_half_up(v6*255/63)`), is
derived from §6(a), and is a HYPOTHESIS for real hardware. Rounding is always
round-half-up (`floor(x+0.5)`), never Python banker's rounding.

FACT — full output (`min=` is the minimum separation; the fifth column is the
new `6bit_round` variant):

```
$ python3 tools/ds_alpha_test/expected.py
DS 5->8 expansion: v8 = (v5 << 3) | (v5 >> 2)
A1 weight w = a/31 ; A2 weight w = (a+1)/32 ; source colour 31,31,31
5bit_* : b5 = w*31 + (1-w)*B5 -> round/floor -> expand5
8bit_* : S8 = expand5(31) = 255 ; B8 = expand5(B5) ; e8 = w*S8 + (1-w)*B8 -> round/floor
6bit_round : v6 = (v5 << 1) | (v5 >> 4) ; A2 v6 = ((a+1)*S6 + (31-a)*B6) >> 5 (blend floored) ; A1 v6 = floor((a/31)*S6 + ((31-a)/31)*B6) (counterfactual, no source basis)
6bit_round output rounding: round-half-up(v6*255/63) = floor(x + 0.5)
6bit_round source: DeSmuME BGR666 path (material_5bit_to_6bit / material_6bit_to_8bit, utils/colorspacehandler/colorspacehandler.cpp:97-134) -- HYPOTHESIS for real hardware, NOT a hardware measurement
rounding: round-half-up = floor(x + 0.5)

== backdrop black (5-bit 0 -> 8-bit 0) ==
a= 4 A1=33/33/33/32/32 A2=41/33/40/39/36 sep=5bit_round=8/5bit_floor=0/8bit_round=7/8bit_floor=7/6bit_round=4
a= 8 A1=66/66/66/65/65 A2=74/66/72/71/69 sep=5bit_round=8/5bit_floor=0/8bit_round=6/8bit_floor=6/6bit_round=4
a=12 A1=99/99/99/98/97 A2=107/99/104/103/101 sep=5bit_round=8/5bit_floor=0/8bit_round=5/8bit_floor=5/6bit_round=4
a=16 A1=132/132/132/131/130 A2=132/132/135/135/134 sep=5bit_round=0/5bit_floor=0/8bit_round=3/8bit_floor=4/6bit_round=4
a=20 A1=165/165/165/164/162 A2=165/165/167/167/166 sep=5bit_round=0/5bit_floor=0/8bit_round=2/8bit_floor=3/6bit_round=4
a=24 A1=198/198/197/197/194 A2=198/198/199/199/198 sep=5bit_round=0/5bit_floor=0/8bit_round=2/8bit_floor=2/6bit_round=4
a=28 A1=231/231/230/230/227 A2=231/231/231/231/231 sep=5bit_round=0/5bit_floor=0/8bit_round=1/8bit_floor=1/6bit_round=4
   (columns follow variant order 5bit_round,5bit_floor,8bit_round,8bit_floor,6bit_round)

== backdrop grey (5-bit 16 -> 8-bit 132) ==
a= 4 A1=148/140/148/147/146 A2=148/148/151/151/150 sep=5bit_round=0/5bit_floor=8/8bit_round=3/8bit_floor=4/6bit_round=4
a= 8 A1=165/156/164/163/162 A2=165/165/167/166/166 sep=5bit_round=0/5bit_floor=9/8bit_round=3/8bit_floor=3/6bit_round=4
a=12 A1=181/173/180/179/178 A2=181/181/182/181/182 sep=5bit_round=0/5bit_floor=8/8bit_round=2/8bit_floor=2/6bit_round=4
a=16 A1=198/189/195/195/194 A2=198/189/197/197/194 sep=5bit_round=0/5bit_floor=0/8bit_round=2/8bit_floor=2/6bit_round=0
a=20 A1=214/206/211/211/210 A2=214/206/213/212/210 sep=5bit_round=0/5bit_floor=0/8bit_round=2/8bit_floor=1/6bit_round=0
a=24 A1=231/222/227/227/227 A2=231/222/228/228/227 sep=5bit_round=0/5bit_floor=0/8bit_round=1/8bit_floor=1/6bit_round=0
a=28 A1=247/239/243/243/243 A2=247/239/243/243/243 sep=5bit_round=0/5bit_floor=0/8bit_round=0/8bit_floor=0/6bit_round=0
   (columns follow variant order 5bit_round,5bit_floor,8bit_round,8bit_floor,6bit_round)

== best-case separation over all variants/backdrops ==
a= 4 best=8 min=0 strong
a= 8 best=9 min=0 strong
a=12 best=8 min=0 strong
a=16 best=4 min=0 WEAK
a=20 best=4 min=0 WEAK
a=24 best=4 min=0 WEAK
a=28 best=4 min=0 WEAK
weak discriminators (best separation < 5): a=16, a=20, a=24, a=28
strong discriminators: a=4, a=8, a=12
below WEAK_SEPARATION under minimum separation: a=4, a=8, a=12, a=16, a=20, a=24, a=28
```

Hand-pinned FACT: a=16 over black, 8-bit round A1 = 132, A2 = 135; 5-bit round
A1 = A2 = 132. 6-bit round A1 = 130, A2 = 134; and a=4 over black 6-bit A2 = 36
(`v6 = (5*63)>>5 = 9`, `round(9*255/63) = 36`). In 5-bit units the A1/A2
difference is `(31-a)/32 < 1`, so `5bit_floor` truncates it away over black
(separation 0 for every a) — expected physics, not a bug. The **minimum**
separation (`min=`) is 0 for **every** alpha; only the best-case column
(`best=`) separates a = 4, 8, 12 (strong) from a = 16..28 (weak).

## 3. Measurement tool and offline classifier checks

`measure_alpha.py [--native] <capture.png>` reads the top-left 400×240 cc3dsfs
region via `capture_mapping.json` (or the native 256×192 frame), takes the
**mean** of the centre 8×8 DS-pixel patch (4-DS-pixel margin), and reports per
quad {measured, A1/A2 per variant, nearest} plus each variant's verdict and the
overall verdict. Rules: a quad votes when `|E_A1-E_A2| >= 3`; a round variant
decides when all voters agree and all quads are within the flat `TOL=8`; the
overall verdict is the best-fitting variant family (minimum summed
`|measured-expected|`) or `UNDECIDED`. `TOL=8` is the agreement tolerance,
**not** the discrimination gate (the largest separation over black is 7).

FACT — synthetic captures built from `expected.py` (A1 and A2, 8-bit-round
build), the flat-grey negative control, and determinism. Paths go via
`sys.argv` and `O` is defined below; every command is complete:

```
$ O=$(mktemp -d)
$ python3 - "$O" <<'EOF'
import sys
sys.path.insert(0, "tools/ds_alpha_test")
import test_measure_alpha as t
o = sys.argv[1]
t.synthetic_capture("A1").save(o + "/syn_a1.png")
t.synthetic_capture("A2").save(o + "/syn_a2.png")
t.synthetic_native("A2").save(o + "/syn_native_a2.png")
EOF
$ python3 -c 'from PIL import Image; import sys; Image.new("RGB", (400, 240), (128, 128, 128)).save(sys.argv[1])' "$O/flat_grey.png"
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/syn_a1.png" | grep -E '^(mode|geometry_found|verdict|variant_|best_|model_)'
mode=capture
geometry_found=1
variant_5bit_round_verdict=A1
variant_5bit_floor_verdict=A2
variant_8bit_round_verdict=A1
variant_8bit_floor_verdict=A1
variant_6bit_round_verdict=UNDECIDED
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=8bit_round
best_convention=A1
best_total_dev=0
verdict=A1
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/syn_a2.png" | grep -E '^(mode|geometry_found|verdict|variant_|best_|model_)'
mode=capture
geometry_found=1
variant_5bit_round_verdict=A2
variant_5bit_floor_verdict=A2
variant_8bit_round_verdict=A2
variant_8bit_floor_verdict=A2
variant_6bit_round_verdict=A2
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=8bit_round
best_convention=A2
best_total_dev=0
verdict=A2
$ python3 tools/ds_alpha_test/measure_alpha.py --native "$O/syn_native_a2.png" | grep -E '^(mode|geometry_found|verdict|variant_|best_|model_)'
mode=native
geometry_found=1
variant_5bit_round_verdict=A2
variant_5bit_floor_verdict=A2
variant_8bit_round_verdict=A2
variant_8bit_floor_verdict=A2
variant_6bit_round_verdict=A2
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=8bit_round
best_convention=A2
best_total_dev=0
verdict=A2
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/flat_grey.png" >"$O/fg.out" 2>"$O/fg.err"; echo "flat_exit=$?"
flat_exit=1
$ cat "$O/fg.err"
measure_alpha: NOT-COMPARABLE: backdrops or quads not located in this image
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/syn_a1.png" >"$O/a1out1.txt"
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/syn_a1.png" >"$O/a1out2.txt"
$ diff -q "$O/a1out1.txt" "$O/a1out2.txt" && echo identical
identical
```

The `5bit_floor` row on the A1 build says A2 because an 8-bit-round A1 value can
coincide with a 5-bit-floor A2 value (e.g. grey a=4: 148), so the floor variants
are reported but do not vote.

Per-quad rows (A1 build), FACT — `quad_L4` rows:

```
$ python3 tools/ds_alpha_test/measure_alpha.py "$O/syn_a1.png" | grep -E '^quad_L4'
quad_L4_measured=33,33,33
quad_L4_A1_5bit_round=33,33,33
quad_L4_A2_5bit_round=41,41,41
quad_L4_A1_5bit_floor=33,33,33
quad_L4_A2_5bit_floor=33,33,33
quad_L4_A1_8bit_round=33,33,33
quad_L4_A2_8bit_round=40,40,40
quad_L4_A1_8bit_floor=32,32,32
quad_L4_A2_8bit_floor=39,39,39
quad_L4_A1_6bit_round=32,32,32
quad_L4_A2_6bit_round=36,36,36
quad_L4_nearest=A1
quad_L4_dev_best=0
```

## 4. Build (FACT)

```
$ cd tools/ds_alpha_test && make clean && make
clean ...
main.c
linking ds-alpha-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-alpha-test.nds
$ sha256sum tools/ds_alpha_test/ds-alpha-test.nds
28d1b08620ff1641d5a89a58dc917e036bb74e60b918c944a6d46afdf3663fea  tools/ds_alpha_test/ds-alpha-test.nds
$ ndstool -i tools/ds_alpha_test/ds-alpha-test.nds | sed -n '1,16p'
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
0x2C	ARM9 code size           	0x10CB4
```

The ROM links only `-lnds9`; no TWL-HD `common/`/`arm9v2` code is linked (asserted
by `test_quads.py`). The `.nds` is not committed (`*.nds` is in `.gitignore`).

FACT — suite:

```
$ make test-ds-alpha-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_alpha_test -p 'test_*.py'
...........................................
----------------------------------------------------------------------
Ran 43 tests in 1.178s

OK
```

FACT — the root `make test` ran the new target and stayed green (command:
`make test > /tmp/alpha_make_test.log 2>&1; echo "EXIT=$?" >> /tmp/alpha_make_test.log`;
raw log tail; the aggregate discovery still reports 394 because it does not
descend into `tools/ds_alpha_test`, whose 43 tests run in the explicit target):

```
Ran 394 tests in 85.956s

OK (skipped=69)
/tmp/tmpt16w3iq0/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
EXIT=0
```

## 5. EMULATOR arm — DeSmuME (FACT)

The repair removes the two first-attempt blockers: the `iprintf(...)` call is
gone (the bottom console stays blank), and the quads are at z=+64, in front of
the z=0 backdrops under LESS. Built unmodified from the repo sources, the ROM
presents a frame and the classifier returns a verdict. All of this is
**EMULATOR (DeSmuME)**.

FACT — the ROM rebuilds with the same `sha256` (`83e06e97…c01`), the harness
captures frame 1 with `harness_exit=0` and all 16 polygons / 64 vertices, and
the post-transform z is `0x3f000000` (quads) versus `0x40000000` (backdrops);
smaller is nearer under the default LESS test. Every command and its complete
output is quoted once, in §5.2.

FACT (repair 2, history) — the repair-2 complete `--native` output measured the
same pixels as §5.2's block (backdrops 0 and 134; quads L4 36, L8 69, L12 101,
L16 134, L20 166, L24 199, L28 231 and R4 150, R8 166, R12 182, R16 195,
R20 211, R24 227, R28 243), and every 5-bit/8-bit expected value in it appears
unchanged in §5.2's output. Its classifier lines are the evidence for the
model-artefact explanation in §5.2 and §8:

```
variant_5bit_round_verdict=A1
variant_5bit_floor_verdict=A2
variant_8bit_round_verdict=UNDECIDED
variant_8bit_floor_verdict=A2
verdict=A1
```

### 5.2 Corrected measurement interpretation (repair 3)

The repair-2 verdict above is **wrong as an interpretation**: the classifier's
families did not include DeSmuME's 6-bit path, so the `A1` it reported is a
**classifier-model artefact**. Under the 5-bit/8-bit families no variant
reproduces the measured frame: the old winner (`5bit_round`, A1) is off by
+1..+3 8-bit levels over black and −3..−4 over grey (measured − expected), and
the grey backdrop is 134 against `expand5(16) = 132` (+2) — the A1 label appears
only because every quad fell inside the flat `TOL = 8`.

SOURCE-READ (§6(a)) — DeSmuME keeps 3D colour in an internal BGR666 buffer
(`rasterize.cpp:1487`) and blends in 6-bit channels; the `rasterize.cpp:488-497`
and `colorspacehandler.cpp:98-101`/`:126-129` quotes are in §6(a). The 6→8
conversion applied on output reads:

```
$ sed -n '181,188p' "$HOME/3ds/desmume/desmume/src/utils/colorspacehandler/colorspacehandler.h"
FORCEINLINE u32 ColorspaceConvert6665To8888(Color4u8 srcColor)
{
	Color4u8 outColor;
	outColor.r = material_6bit_to_8bit[((SWAP_RB) ? srcColor.b : srcColor.r)];
	outColor.g = material_6bit_to_8bit[srcColor.g];
	outColor.b = material_6bit_to_8bit[((SWAP_RB) ? srcColor.r : srcColor.b)];
	outColor.a = material_5bit_to_8bit[srcColor.a];
	
```

The `material_6bit_to_8bit` table is exactly `(v6<<2)|(v6>>4)`. The tool models
the 5→6 step as `(v5<<1)|(v5>>4)`; DeSmuME's table is `2*v+1` for `v>=1` and 0
for 0 (it agrees at the values used here: 0, 16, 31). **This 6-bit model is
derived from DeSmuME's source and is NOT known to be what real hardware does
(HYPOTHESIS).**

FACT — rebuild (same `sha256`), harness re-run, and the complete new classifier
output:

```
$ cd tools/ds_alpha_test && make clean && make
clean ...
main.c
linking ds-alpha-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-alpha-test.nds
$ sha256sum tools/ds_alpha_test/ds-alpha-test.nds
83e06e97e7b0e8d8e148da9aa2aa36b1238376e385fa187e6957073f90223c01  tools/ds_alpha_test/ds-alpha-test.nds
$ O=$(mktemp -d)
$ "$HOME/3ds/twlhd-desmume-harness/harness" tools/ds_alpha_test/ds-alpha-test.nds 1 --out "$O/h.txt" --ppm "$O/h.ppm" >"$O/out" 2>"$O/err"; echo "harness_exit=$?"
harness_exit=0
$ grep -E "harness:" "$O/err"
harness: captured frame 1: 16 polygons, 64 vertices (first non-empty 3D frame: 1)
$ awk '/^pos-z/{print $2}' "$O/h.txt" | sort -u | xargs -n1 printf '%#x\n'
0x3f000000
0x40000000
$ python3 - "$O/h.ppm" "$O/frame.png" <<'EOF'
import sys
from PIL import Image
Image.open(sys.argv[1]).convert("RGB").save(sys.argv[2])
EOF
$ python3 tools/ds_alpha_test/measure_alpha.py --native "$O/frame.png"; echo "measure_exit=$?"
mode=native
geometry_found=1
backdrop_black_measured=0,0,0
backdrop_grey_measured=134,134,134
quad_L4_measured=36,36,36
quad_L4_A1_5bit_round=33,33,33
quad_L4_A2_5bit_round=41,41,41
quad_L4_A1_5bit_floor=33,33,33
quad_L4_A2_5bit_floor=33,33,33
quad_L4_A1_8bit_round=33,33,33
quad_L4_A2_8bit_round=40,40,40
quad_L4_A1_8bit_floor=32,32,32
quad_L4_A2_8bit_floor=39,39,39
quad_L4_A1_6bit_round=32,32,32
quad_L4_A2_6bit_round=36,36,36
quad_L4_nearest=A1
quad_L8_measured=69,69,69
quad_L8_A1_5bit_round=66,66,66
quad_L8_A2_5bit_round=74,74,74
quad_L8_A1_5bit_floor=66,66,66
quad_L8_A2_5bit_floor=66,66,66
quad_L8_A1_8bit_round=66,66,66
quad_L8_A2_8bit_round=72,72,72
quad_L8_A1_8bit_floor=65,65,65
quad_L8_A2_8bit_floor=71,71,71
quad_L8_A1_6bit_round=65,65,65
quad_L8_A2_6bit_round=69,69,69
quad_L8_nearest=tie
quad_L12_measured=101,101,101
quad_L12_A1_5bit_round=99,99,99
quad_L12_A2_5bit_round=107,107,107
quad_L12_A1_5bit_floor=99,99,99
quad_L12_A2_5bit_floor=99,99,99
quad_L12_A1_8bit_round=99,99,99
quad_L12_A2_8bit_round=104,104,104
quad_L12_A1_8bit_floor=98,98,98
quad_L12_A2_8bit_floor=103,103,103
quad_L12_A1_6bit_round=97,97,97
quad_L12_A2_6bit_round=101,101,101
quad_L12_nearest=A1
quad_L16_measured=134,134,134
quad_L16_A1_5bit_round=132,132,132
quad_L16_A2_5bit_round=132,132,132
quad_L16_A1_5bit_floor=132,132,132
quad_L16_A2_5bit_floor=132,132,132
quad_L16_A1_8bit_round=132,132,132
quad_L16_A2_8bit_round=135,135,135
quad_L16_A1_8bit_floor=131,131,131
quad_L16_A2_8bit_floor=135,135,135
quad_L16_A1_6bit_round=130,130,130
quad_L16_A2_6bit_round=134,134,134
quad_L16_nearest=A2
quad_L20_measured=166,166,166
quad_L20_A1_5bit_round=165,165,165
quad_L20_A2_5bit_round=165,165,165
quad_L20_A1_5bit_floor=165,165,165
quad_L20_A2_5bit_floor=165,165,165
quad_L20_A1_8bit_round=165,165,165
quad_L20_A2_8bit_round=167,167,167
quad_L20_A1_8bit_floor=164,164,164
quad_L20_A2_8bit_floor=167,167,167
quad_L20_A1_6bit_round=162,162,162
quad_L20_A2_6bit_round=166,166,166
quad_L20_nearest=ambiguous
quad_L24_measured=199,199,199
quad_L24_A1_5bit_round=198,198,198
quad_L24_A2_5bit_round=198,198,198
quad_L24_A1_5bit_floor=198,198,198
quad_L24_A2_5bit_floor=198,198,198
quad_L24_A1_8bit_round=197,197,197
quad_L24_A2_8bit_round=199,199,199
quad_L24_A1_8bit_floor=197,197,197
quad_L24_A2_8bit_floor=199,199,199
quad_L24_A1_6bit_round=194,194,194
quad_L24_A2_6bit_round=198,198,198
quad_L24_nearest=ambiguous
quad_L28_measured=231,231,231
quad_L28_A1_5bit_round=231,231,231
quad_L28_A2_5bit_round=231,231,231
quad_L28_A1_5bit_floor=231,231,231
quad_L28_A2_5bit_floor=231,231,231
quad_L28_A1_8bit_round=230,230,230
quad_L28_A2_8bit_round=231,231,231
quad_L28_A1_8bit_floor=230,230,230
quad_L28_A2_8bit_floor=231,231,231
quad_L28_A1_6bit_round=227,227,227
quad_L28_A2_6bit_round=231,231,231
quad_L28_nearest=ambiguous
quad_R4_measured=150,150,150
quad_R4_A1_5bit_round=148,148,148
quad_R4_A2_5bit_round=148,148,148
quad_R4_A1_5bit_floor=140,140,140
quad_R4_A2_5bit_floor=148,148,148
quad_R4_A1_8bit_round=148,148,148
quad_R4_A2_8bit_round=151,151,151
quad_R4_A1_8bit_floor=147,147,147
quad_R4_A2_8bit_floor=151,151,151
quad_R4_A1_6bit_round=146,146,146
quad_R4_A2_6bit_round=150,150,150
quad_R4_nearest=A2
quad_R8_measured=166,166,166
quad_R8_A1_5bit_round=165,165,165
quad_R8_A2_5bit_round=165,165,165
quad_R8_A1_5bit_floor=156,156,156
quad_R8_A2_5bit_floor=165,165,165
quad_R8_A1_8bit_round=164,164,164
quad_R8_A2_8bit_round=167,167,167
quad_R8_A1_8bit_floor=163,163,163
quad_R8_A2_8bit_floor=166,166,166
quad_R8_A1_6bit_round=162,162,162
quad_R8_A2_6bit_round=166,166,166
quad_R8_nearest=A2
quad_R12_measured=182,182,182
quad_R12_A1_5bit_round=181,181,181
quad_R12_A2_5bit_round=181,181,181
quad_R12_A1_5bit_floor=173,173,173
quad_R12_A2_5bit_floor=181,181,181
quad_R12_A1_8bit_round=180,180,180
quad_R12_A2_8bit_round=182,182,182
quad_R12_A1_8bit_floor=179,179,179
quad_R12_A2_8bit_floor=181,181,181
quad_R12_A1_6bit_round=178,178,178
quad_R12_A2_6bit_round=182,182,182
quad_R12_nearest=ambiguous
quad_R16_measured=195,195,195
quad_R16_A1_5bit_round=198,198,198
quad_R16_A2_5bit_round=198,198,198
quad_R16_A1_5bit_floor=189,189,189
quad_R16_A2_5bit_floor=189,189,189
quad_R16_A1_8bit_round=195,195,195
quad_R16_A2_8bit_round=197,197,197
quad_R16_A1_8bit_floor=195,195,195
quad_R16_A2_8bit_floor=197,197,197
quad_R16_A1_6bit_round=194,194,194
quad_R16_A2_6bit_round=194,194,194
quad_R16_nearest=ambiguous
quad_R20_measured=211,211,211
quad_R20_A1_5bit_round=214,214,214
quad_R20_A2_5bit_round=214,214,214
quad_R20_A1_5bit_floor=206,206,206
quad_R20_A2_5bit_floor=206,206,206
quad_R20_A1_8bit_round=211,211,211
quad_R20_A2_8bit_round=213,213,213
quad_R20_A1_8bit_floor=211,211,211
quad_R20_A2_8bit_floor=212,212,212
quad_R20_A1_6bit_round=210,210,210
quad_R20_A2_6bit_round=210,210,210
quad_R20_nearest=ambiguous
quad_R24_measured=227,227,227
quad_R24_A1_5bit_round=231,231,231
quad_R24_A2_5bit_round=231,231,231
quad_R24_A1_5bit_floor=222,222,222
quad_R24_A2_5bit_floor=222,222,222
quad_R24_A1_8bit_round=227,227,227
quad_R24_A2_8bit_round=228,228,228
quad_R24_A1_8bit_floor=227,227,227
quad_R24_A2_8bit_floor=228,228,228
quad_R24_A1_6bit_round=227,227,227
quad_R24_A2_6bit_round=227,227,227
quad_R24_nearest=ambiguous
quad_R28_measured=243,243,243
quad_R28_A1_5bit_round=247,247,247
quad_R28_A2_5bit_round=247,247,247
quad_R28_A1_5bit_floor=239,239,239
quad_R28_A2_5bit_floor=239,239,239
quad_R28_A1_8bit_round=243,243,243
quad_R28_A2_8bit_round=243,243,243
quad_R28_A1_8bit_floor=243,243,243
quad_R28_A2_8bit_floor=243,243,243
quad_R28_A1_6bit_round=243,243,243
quad_R28_A2_6bit_round=243,243,243
quad_R28_nearest=ambiguous
variant_5bit_round_verdict=A1
variant_5bit_floor_verdict=A2
variant_8bit_round_verdict=UNDECIDED
variant_8bit_floor_verdict=A2
variant_6bit_round_verdict=A2
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=6bit_round
best_convention=A2
best_total_dev=9
quad_L4_dev_best=0
quad_L8_dev_best=0
quad_L12_dev_best=0
quad_L16_dev_best=0
quad_L20_dev_best=0
quad_L24_dev_best=3
quad_L28_dev_best=0
quad_R4_dev_best=0
quad_R8_dev_best=0
quad_R12_dev_best=0
quad_R16_dev_best=3
quad_R20_dev_best=3
quad_R24_dev_best=0
quad_R28_dev_best=0
verdict=A2
measure_exit=0
```

Corrected verdict (EMULATOR, DeSmuME): **A2**, decided by the best-fitting
variant family **`6bit_round`** (`best_total_dev=9`, `measure_exit=0`). With the
prescribed output `round(v6*255/63)` the only nonzero per-channel deviations of
the winning family are L24 +1, R16 +1 and R20 +1 (each quad sums to
`quad_<name>_dev_best=3` over its three equal channels); every other quad is
exact. Those three 1-level residuals are consistent with DeSmuME's
`(v6<<2)|(v6>>4)` output stage (HYPOTHESIS/UNVERIFIED — the harness output
stage is not quoted in this round): the prescribed `round(v6*255/63)` gives L24
198, R16 194, R20 210 against measured 199, 195, 211. The model is a HYPOTHESIS
for hardware; real hardware remains **NOT YET MEASURED**.

### 5.1 Previous round (history — pre-repair blockers, now fixed)

The first attempt did not render: the committed source faulted before its 3D
frame and the quads were depth-rejected; both are fixed in §5. The out-of-tree
harness is the PPM-capable path (the repo's `desmume_capture.sh` needs an arm9v2
autodump this ROM never writes; `desmume-cli --help` has no image output).
Kept evidence:

FACT — the committed ROM presented no frame:

```
$ O=$(mktemp -d)
$ "$HOME/3ds/twlhd-desmume-harness/harness" tools/ds_alpha_test/ds-alpha-test.nds 1 --out "$O/h.txt" --ppm "$O/h.ppm" >"$O/out" 2>"$O/err"; echo "harness_exit=$?"
harness_exit=1
$ grep -nE "harness:|ARM OP_BKPT" "$O/err"
4:harness: requested frame 1 was not presented within 1200 frames
19:ARM OP_BKPT triggered
```

A scratch control with the two-line `iprintf(...)` call deleted rendered, but
the quads equalled their backdrops (depth-rejected); its harness geometry dump:

```
$ awk '/^pos-z/{print $2}' "$O/h.txt" | sort -u | xargs -n1 printf '%#x\n'
0x40000000
0x41000000
```

A second control that also set every quad `"z": 64` made them visible. The sign
was the cause: z=−64 landed at `0x41000000`, farther than the `0x40000000`
backdrops; the repair flips to +64 (post-transform `0x3f000000`, nearer). No
EMULATOR pixel measurement existed in the previous round; §5 is the first one.
Hardware remains **NOT YET MEASURED**.

## 6. SOURCE-READ findings

**(a) DeSmuME software rasteriser: A2 = `(a+1)/32`.** SOURCE-READ — the local
DeSmuME checkout was readable:

```
$ sed -n '488,497p' "$HOME/3ds/desmume/desmume/src/rasterize.cpp"
	else
	{
		const u8 alpha = inSrc.a + 1;
		const u8 invAlpha = 32 - alpha;
		outDst.r = (alpha*inSrc.r + invAlpha*outDst.r) >> 5;
		outDst.g = (alpha*inSrc.g + invAlpha*outDst.g) >> 5;
		outDst.b = (alpha*inSrc.b + invAlpha*outDst.b) >> 5;
		outDst.a = std::max(inSrc.a, outDst.a);
	}
}
$ grep -n "NDSColorFormat_BGR666_Rev" "$HOME/3ds/desmume/desmume/src/rasterize.cpp"
1487:	_format = NDSColorFormat_BGR666_Rev;
```

`alpha = a+1`, divide by `32` (`>>5`), so the DeSmuME *weight* is A2, and the
blend operates on its internal **6-bit** BGR666 channels
(`_format = NDSColorFormat_BGR666_Rev`, quoted above). SOURCE-READ — the 5→6
expansion and the 6→8 output stage:

```
$ sed -n '98,101p' "$HOME/3ds/desmume/desmume/src/utils/colorspacehandler/colorspacehandler.cpp"
// 5-bit to 6-bit conversions use this formula -- dst = (src == 0) ? 0 : (2*src) + 1
// Reference GBATEK: http://problemkaputt.de/gbatek.htm#ds3dtextureblending
CACHE_ALIGN const u8 material_5bit_to_6bit[64] = {
	0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
$ sed -n '126,129p' "$HOME/3ds/desmume/desmume/src/utils/colorspacehandler/colorspacehandler.cpp"
CACHE_ALIGN const u8 material_6bit_to_8bit[64] = {
	0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C,
	0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
	0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D,
```

DeSmuME's 5→6 step is `2*v+1` for `v>=1` (0 for 0); the tool models it as
`(v<<1)|(v>>4)`, which agrees at the values this test uses (0, 16, 31). The
6→8 table is exactly `(v6<<2)|(v6>>4)`. **This 6-bit pipeline model is derived
from DeSmuME's source and is a HYPOTHESIS for real hardware, not a hardware
measurement.** This matches the repo record `docs/DS_GX_RESEARCH.md:72-77`;
SOURCE-READ, not a hardware measurement.

**(b) Our PICA conversion path: A2 = `(a+1)/32`.** SOURCE-READ —
`common/twlhd_gx_interp.c:395-397` computes the float alpha, applied per BEGIN
group at `:1074`, carried as `rgba[3]` (`common/twlhd_gx_interp.h:25`) and
blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA (`arm11/include/twlhd_blend_config.h:30-31`):

```
$ sed -n '395,397p' common/twlhd_gx_interp.c
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}
$ grep -n "g->rgba\[3\] = gx_alpha_float" common/twlhd_gx_interp.c
1074:        g->rgba[3] = gx_alpha_float(g->applied_alpha);
$ grep -n "rgba\[4\]" common/twlhd_gx_interp.h
25:    float rgba[4]; /* r, g, b, a in 0..1 (alpha from POLYGON_ATTR, see stats). */
$ grep -n "GPU_SRC_ALPHA\|GPU_ONE_MINUS_SRC_ALPHA" arm11/include/twlhd_blend_config.h
30:        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
31:        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
```

Alpha 0 is the DS wireframe case and is emitted as 0.0 (not a blend weight).
Rendered through PICA by `tools/azahar_host`. **Our path uses A2.**

**(c) The v1 frame path does not implement polygon alpha at all.** SOURCE-READ
— `common/twlhd_vertex_convert.c:44` forces vertex alpha to 1.0:

```
$ grep -n "out\[3\] = 1.0f" common/twlhd_vertex_convert.c
44:    out[3] = 1.0f;
```

So the v1 frame format is fully opaque; it has no polygon-alpha blend to
classify. Only the v2/GX interp path above carries the A2 weight.

**Summary (SOURCE-READ):** DeSmuME = A2, blending in 6-bit BGR666 channels (the
`6bit_round` model); our PICA v2 path = A2; our v1 frame path = no polygon
alpha. The 6-bit model is a HYPOTHESIS for hardware, which is **NOT YET
MEASURED**.

## 7. Hardware procedure (owner; NOT run in this task)

1. Build: `make -C tools/ds_alpha_test clean && make -C tools/ds_alpha_test`;
   record `sha256sum tools/ds_alpha_test/ds-alpha-test.nds`.
2. Copy to the SD card (set the mount in `$SD_ROOT`):
   `cp tools/ds_alpha_test/ds-alpha-test.nds "$SD_ROOT/roms/nds/"`.
3. Run it from TWiLight Menu++ on the New 3DS in DS mode; leave it on the
   static screen (top shows the backdrops and quads, bottom is blank).
4. Capture the top screen through the EQ-4b board: run cc3dsfs with the EQ-4c
   layout flags and the `cc_shots.sh` xwd→png loop:
   ```
   xvfb-run -a -s "-screen 0 1280x720x24" "$CC3DSFS_BIN" \
     --auto_connect --no_audio --no_auto_save \
     --enabled_both 0 --enabled_top 1 --scaling_top 1
   CC3DSFS_BIN="$HOME/path/to/cc3dsfs-build/bin/cc3dsfs" \
     OUT_DIR="$HOME/path/to/out" sh tools/cc3dsfs_capture/cc_shots.sh al 3 5
   ```
   (`cc_shots.sh` starts cc3dsfs without the layout flags; record the exact
   invocation.)
5. Analyse each shot with
   `python3 tools/ds_alpha_test/measure_alpha.py "$HOME/path/to/out/al_2.png"`
   (`--native` for an already-native 256×192 frame).
6. Record the raw report; the verdict and per-quad rows are **NOT YET
   MEASURED** in this task.

## 8. Requirement traceability

- **REQ new ROM, same toolchain, no repo-ROM dependency** — FACT §4.
- **REQ backdrops + 7 quads per half, distinct ids, stated state** — §1.1,
  `quads.json`/`include/quads.h`, `test_quads.py`.
- **REQ single-source geometry JSON** — shared by `gen_quads.py` and
  `measure_alpha.py`; `test_quads.py` diffs the header.
- **REQ A1/A2 tables, round/floor, separation, weak alphas** — FACT §2; the
  `6bit_round` family derives from §6(a) and is a HYPOTHESIS.
- **REQ measure tool (margin mean, verdicts, NOT-COMPARABLE, deterministic)** —
  FACT §3; overall verdict = best-fitting family.
- **REQ EMULATOR arm labelled** — §5 EMULATOR: verdict **A2** via
  `best_variant=6bit_round` (§5.2); the repair-2 `A1` stays as history, the
  classifier-model artefact; §5.1 keeps the pre-repair history.
- **REQ SOURCE-READ of DeSmuME and our PICA path** — §6.
- **REQ tests in root `make test`, never weaken tests** — `test-ds-alpha-test`
  target + `test` prerequisite; 43 tests OK, additive only.
- **REQ report + append-only pointers; hardware NOT YET MEASURED** — this
  report, the two appends, the status line.

## 9. Privacy

Every command uses `$HOME` / `$DEVKITPRO` / `$DEVKITARM` / `$SD_ROOT` /
`$CC3DSFS_BIN`; the `/(home|Users)/` grep check and its clean output are in the
tool README §10.

## 10. Size audit (repair 4)

FACT — the total diff against `main` is below the 117,760-byte hard limit:

```
$ git diff main | wc -c
117609
```

## 11. Note (2026-11-23): the hardware `verdict=A1` artefact

EQ-4i measured the same ROM on real hardware (one New 3DS DS-mode run, one
capture session, one ROM) and shows that the whole-RGB `verdict=A1` discussed
above is a classifier-model artefact on hardware too: the green channel of the
capture matches the 6-bit **A2** expectation on the decisive quads while
red/blue match the 5-bit quantisation of those 6-bit values, so no scalar
family can fit and the total-deviation vote lands on `5bit_round/A1`. This note
is appended only; the body above is unchanged. See
`docs/reports/eq4i-alpha-hw-result.md`.
