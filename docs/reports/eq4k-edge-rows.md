# EQ-4k — 3D edge rows: is DeSmuME's +1 a whole-frame shift or a per-edge offset?

**Basis: offline ROM source, one build, one DeSmuME harness render, one native
PPM, one test run.** No hardware action and no console access in this round.
Every number below is taken from a command quoted in this report and run in
this round. Labels follow `docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`:
FACT = a command and its raw output quoted; EMULATOR = a DeSmuME/harness
measurement; SOURCE-READ = a quoted `file:line`; HYPOTHESIS / UNVERIFIED = not
measured here.

## 1. Question and hypotheses

EQ-4j (`docs/reports/eq4j-3d-edge-position.md` §6–§8) found that DeSmuME places
every left/right quad edge exactly nominal but every top/bottom quad edge
exactly `+1.0000` DS px below nominal, while hardware edges sit about `1.07`
DS px above DeSmuME's. Its open question is whether this is:

* **H1 — whole-frame shift in DeSmuME/the harness:** the whole 3D frame is
  displaced one row, so every polygon (including a top-scanline marker) lands
  one row low; or
* **H2 — per-primitive / per-edge rasterisation offset in DeSmuME:** only the
  rasterised horizontal edges are placed one row low, while the frame origin
  and vertical edges are unchanged.

The old `ds-alpha-test` ROM cannot decide: its backdrops fill the screen and
its quads sit at `y=32..160`, so there is no white at row 0 or row 191
(EQ-4j §7). This round adds such a ROM, measures it in DeSmuME, reads the
harness/DeSmuME source, and prepares the owner's hardware run.

## 2. ROM design and build (FACT)

`tools/ds_edge_test/` follows the `tools/ds_alpha_test/` template:
`quads.json` is the single source of truth, `gen_quads.py` emits the committed
`include/quads.h`, and the ROM is one static libnds scene. The geometry is the
half-open vertex rectangle `[x, x+w) × [y, y+h)` in DS pixels (the same
convention as `ds_alpha_test`; the DS fill rule is not derived — UNVERIFIED,
§9):

| item | rectangle | purpose |
|---|---|---|
| `black` | `0,0,256,192` | opaque black backdrop, id 1, z 0 |
| `top_band` | `0,0,256,16` | touches scanline 0 |
| `bottom_band` | `0,176,256,16` | touches scanline 191 |
| `left_band` | `0,80,16,32` | touches column 0 |
| `right_band` | `240,80,16,32` | touches column 255 |
| `sq1`..`sq4` | `32,32` / `160,32` / `32,128` / `160,128`, 32×32 | isolated interiors |
| `hline` | `64,96,128,1` | one interior row (96) |
| `vline` | `208,64,1,96` | one interior column (208) |

All ten polygons are white `31,31,31`, alpha 31, z=+64, polygon ids 2..11.
State: `videoSetMode(MODE_0_3D)`, `glInit()`, `glViewport(0,0,255,191)`,
`glOrthof32(0,256,192,0,-1<<12,1<<12)`, raw `glVertex3v16`;
`glDisable(GL_BLEND | GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS |
GL_OUTLINE | GL_FOG)`; `glClearColor(0,0,0,31)`, `glClearPolyID(63)`,
`glClearDepth(GL_MAX_DEPTH)`; `glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE |
POLY_ID(id))`. No alpha, texture, fog, edge marking or antialiasing is used.
The backdrop z=0 / white z=+64 relation is the proven `ds_alpha_test` relation
(larger source z is nearer under LESS).

FACT — clean rebuild from repo sources only, `sha256`, and the regenerated
header diff:

```
$ make -C tools/ds_edge_test clean && make -C tools/ds_edge_test
make: Entering directory '.../tools/ds_edge_test'
clean ...
make: Leaving directory '.../tools/ds_edge_test'
make: Entering directory '.../tools/ds_edge_test'
main.c
linking ds-edge-test.elf
Nintendo DS rom tool 2.3.1 - Jan  8 2024
by Rafael Vuijk, Dave Murphy, Alexei Karpenko
built ... ds-edge-test.nds
make: Leaving directory '.../tools/ds_edge_test'
$ sha256sum tools/ds_edge_test/ds-edge-test.nds
3d417c162d5067f1febf7f119182ede6fd8a88e56c28c30853b67857fd0a2706  tools/ds_edge_test/ds-edge-test.nds
$ python3 tools/ds_edge_test/gen_quads.py tools/ds_edge_test/quads.json "$O/quads_regen.h"
$ diff -q tools/ds_edge_test/include/quads.h "$O/quads_regen.h" && echo header_identical=yes
header_identical=yes
```

The built `.nds` is not committed (`*.nds` is in `.gitignore`, matching the
sibling ROMs).

## 3. Emulator measurement (EMULATOR / FACT)

FACT — render frame 1 with the out-of-tree harness (command shape from
`docs/reports/eq4h-alpha-convention-test.md` §5) and measure the native PPM:

```
$ O=$(mktemp -d)
$ "$HOME/3ds/twlhd-desmume-harness/harness" tools/ds_edge_test/ds-edge-test.nds 1 --out "$O/h.txt" --ppm "$O/h.ppm" >"$O/out" 2>"$O/err"; echo "harness_exit=$?"
harness_exit=0
$ grep -E "harness:" "$O/err"
harness: captured frame 1: 11 polygons, 44 vertices (first non-empty 3D frame: 1)
$ sha256sum "$O/h.ppm"
d653c89e1cd676090b0a4f1b2633db30804f60637d7bbf0d595d31d340ed309c  /tmp/tmp.../h.ppm
$ python3 tools/ds_edge_test/measure_rows.py --native "$O/h.ppm"
mode=native
size=256x192
white_threshold=128.0
first_white_row=0
last_white_row=191
first_white_col=0
last_white_col=255
white_row_runs=0-16,33-160,177-191
white_col_runs=0-255
frame_top_row_white=1
frame_bottom_row_white=1
frame_left_col_white=1
frame_right_col_white=1
frame_vertical_shift=none
frame_horizontal_shift=none
item=top_band nominal_rows=0-15 nominal_cols=0-255 measured_rows=0-16 measured_cols=0-255 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=17
item=bottom_band nominal_rows=176-191 nominal_cols=0-255 measured_rows=177-191 measured_cols=0-255 row_diff=1 row_end_diff=0 col_diff=0 col_end_diff=0 row_count=15
item=left_band nominal_rows=80-111 nominal_cols=0-15 measured_rows=81-112 measured_cols=0-15 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=right_band nominal_rows=80-111 nominal_cols=240-255 measured_rows=81-112 measured_cols=240-255 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq1 nominal_rows=32-63 nominal_cols=32-63 measured_rows=33-64 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq2 nominal_rows=32-63 nominal_cols=160-191 measured_rows=33-64 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq3 nominal_rows=128-159 nominal_cols=32-63 measured_rows=129-160 measured_cols=32-63 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=sq4 nominal_rows=128-159 nominal_cols=160-191 measured_rows=129-160 measured_cols=160-191 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=32
item=hline nominal_rows=96-96 nominal_cols=64-191 measured_rows=96-97 measured_cols=64-191 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=2
item=vline nominal_rows=64-159 nominal_cols=208-208 measured_rows=65-160 measured_cols=208-208 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=96
classification=mixed
```

### 3.1 (i) Top/bottom/left/right anchors

FACT from the output above:

* `first_white_row=0` and `frame_top_row_white=1`: **row 0 is white**, so the
  top band starts at row 0, not row 1.
* `last_white_row=191` and `frame_bottom_row_white=1`: the bottom band ends at
  row 191. It is not lost, but it is **clipped**: `bottom_band` measures
  `177..191` (`row_diff=1`, `row_count=15`) instead of `176..191`; the row that
  would be 192 is outside the framebuffer.
* `first_white_col=0`, `last_white_col=255`, `frame_left_col_white=1`,
  `frame_right_col_white=1`: the left band starts at column 0 and the right
  band ends at column 255; both columns are unchanged.

### 3.2 (ii) Interior squares and 1-px lines

FACT from the same output:

* every 32×32 square is one row lower than nominal on both its top and bottom
  edge (`sq1`..`sq4` measured `33..64` vs nominal `32..63`/`128..159`, with
  `row_diff=1` and `row_end_diff=1`; width is unchanged);
* `hline` (nominal row 96) measures **two** rows, `96..97`
  (`row_count=2`, `row_end_diff=1`), and `vline` (nominal rows `64..159`)
  measures `65..160` (`row_diff=1`, `row_end_diff=1`) while its column stays
  `208..208` (`col_diff=0`, `col_end_diff=0`);
* the left/right bands also move one row down (`80..111` nominal →
  `81..112` measured) without moving in x;
* the top band is one row **taller** than nominal: `0..16` (17 rows) instead
  of `0..15` (16 rows), i.e. its top edge stayed at 0 and its bottom edge moved
  one row down.

So the offset is not a coherent translation of whole items: the horizontal
edges move, and some top edges at exact y values (0 and 96) do not.

### 3.3 (iii) Whole-frame shift versus per-primitive/per-edge offset

What each outcome would look like in `measure_rows.py --native`:

* **whole-frame shift of +1 row** — every polygon moves down, including the
  top band: `first_white_row=1`, `frame_top_row_white=0`, `top_band`
  `1..16` (16 rows), and a `uniform+1` classification;
* **per-primitive/per-edge offset** — the frame origin is unchanged, so row 0
  can still be white, while horizontal edges sit one row lower:
  `first_white_row=0`, `frame_vertical_shift=none`, top band `0..16`
  (17 rows), squares `33..64`, lines as in §3.2;
* **neither** — every item nominal (`classification=none`, top band `0..15`).

The data show the **per-primitive/per-edge** picture, not a whole-frame shift:
row 0 is painted and the top band is one row taller, which a whole-frame
translation cannot produce. The horizontal end edges of every interior item are
`+1` (`row_end_diff=1`) while all vertical edges are nominal. The coarse
`classification=mixed` token is the tool honestly reporting that the first-row
offset alone is not uniform (top band and `hline` start on their nominal first
row because those particular vertices land on an integer after the transform);
the per-edge columns show the mechanism. §4–§5 give the source explanation.

## 4. Harness PPM construction (SOURCE-READ)

SOURCE-READ — `$HOME/3ds/twlhd-desmume-harness/src/harness.cpp:386-399`:

```
    const size_t fw = CurrentRenderer->GetFramebufferWidth();
    const size_t fh = CurrentRenderer->GetFramebufferHeight();
    const Color4u8* fb = CurrentRenderer->GetFramebuffer32();
    if (fb == NULL || fw != 256 || fh != 192) {
      fprintf(stderr, "harness: no readable 256x192 3D framebuffer\n");
      NDS_DeInit();
      return 1;
    }
    ppm_rgb.resize(256 * 192 * 3);
    for (size_t i = 0; i < 256 * 192; ++i) {
      ppm_rgb[i * 3 + 0] = (char)expand6to8(fb[i].r);
      ppm_rgb[i * 3 + 1] = (char)expand6to8(fb[i].g);
      ppm_rgb[i * 3 + 2] = (char)expand6to8(fb[i].b);
      // Alpha is dropped: PPM P6 has no alpha channel.
    }
```

SOURCE-READ — `harness.cpp:435`:

```
    ok = fputs("P6\n256 192\n255\n", ppm) >= 0;
```

The PPM raster is `fb[0..49151]` in linear order (raster row = `i / 256`) with
no row offset, no flip and no crop; the header is the canonical 15-byte
`P6\n256 192\n255\n`. Therefore the one-row effect is **not** introduced by the
harness: it is already in DeSmuME's 3D framebuffer.

## 5. DeSmuME rasteriser path (SOURCE-READ / EMULATOR)

SOURCE-READ — `$HOME/3ds/desmume/desmume/src/gfx3d.cpp:3290` and `:3309-3311`
(the viewport transform and y flip):

```
				vtx64.y = ((vtx64.y + vtx64.w) * ((s64)theViewport.height << 16)) / (2 * vtx64.w);
...
			vtx64.y += ((s64)theViewport.y << 16);
			vtx64.y = (192LL << 16) - vtx64.y;
			vtx64.y *= hScalar;
```

SOURCE-READ — `$HOME/3ds/desmume/desmume/src/rasterize.cpp:116-124`
(`Ceil16_16`), `:2127` (applied to the vertex y), and `:891` (pixel row):

```
static FORCEINLINE s32 Ceil16_16(s32 fixed_16_16)
{
	s32 ReturnValue;
	s32 Numerator = fixed_16_16 - 1 + 65536;
	
	if (Numerator >= 0)
	{
		ReturnValue = Numerator / 65536;
...
			const s32 y = Ceil16_16(vtx.position.y);
...
	size_t adr = (pLeft.y * framebufferWidth) + xStart;
```

FACT — the harness geometry dump prints the post-transform 16.16 vertex y for
each polygon; the relevant excerpts (polygon id from `attr` bits 24-29):

```
$ sed -n '14,45p;66,97p;170,201p;482,513p;534,565p' "$O/h.txt"
poly 0 verts 4 prim quad vtxformat quads rawpoly 1
attr 0x021F00C0
texparam 0x00000000
texpalette 0x00000000
viewport 0 0 256 192
viewport-word 0xBFFF0000
rawverts 4 4 5 6 7
vtx 0
pos-x-16.16 16777216
pos-y-16.16 1049088
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 1
pos-x-16.16 0
pos-y-16.16 1049088
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 2
pos-x-16.16 0
pos-y-16.16 0
poly 1 verts 4 prim quad vtxformat quads rawpoly 5
attr 0x061F00C0
texparam 0x00000000
texpalette 0x00000000
viewport 0 0 256 192
viewport-word 0xBFFF0000
rawverts 4 20 21 22 23
vtx 0
pos-x-16.16 4194304
pos-y-16.16 4194816
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 1
pos-x-16.16 2097152
pos-y-16.16 4194816
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 2
pos-x-16.16 2097152
pos-y-16.16 2098176
poly 3 verts 4 prim quad vtxformat quads rawpoly 9
attr 0x0A1F00C0
texparam 0x00000000
texpalette 0x00000000
viewport 0 0 256 192
viewport-word 0xBFFF0000
rawverts 4 36 37 38 39
vtx 0
pos-x-16.16 12582912
pos-y-16.16 6357504
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 1
pos-x-16.16 4194304
pos-y-16.16 6357504
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 2
pos-x-16.16 4194304
pos-y-16.16 6291456
poly 9 verts 4 prim quad vtxformat quads rawpoly 0
attr 0x011F00C0
texparam 0x00000000
texpalette 0x00000000
viewport 0 0 256 192
viewport-word 0xBFFF0000
rawverts 4 0 1 2 3
vtx 0
pos-x-16.16 16777216
pos-y-16.16 12582912
pos-z-0.31 1073741824
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 0
col-g-6bit 0
col-b-6bit 0
col-a-u8 0
vtx 1
pos-x-16.16 0
pos-y-16.16 12582912
pos-z-0.31 1073741824
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 0
col-g-6bit 0
col-b-6bit 0
col-a-u8 0
vtx 2
pos-x-16.16 0
pos-y-16.16 0
poly 10 verts 4 prim quad vtxformat quads rawpoly 2
attr 0x031F00C0
texparam 0x00000000
texpalette 0x00000000
viewport 0 0 256 192
viewport-word 0xBFFF0000
rawverts 4 8 9 10 11
vtx 0
pos-x-16.16 16777216
pos-y-16.16 12582912
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 1
pos-x-16.16 0
pos-y-16.16 12582912
pos-z-0.31 1056964608
pos-w-20.12 4096
tex-s-12.4 0
tex-t-12.4 0
col-r-6bit 63
col-g-6bit 63
col-b-6bit 63
col-a-u8 0
vtx 2
pos-x-16.16 0
pos-y-16.16 11535360
```

Decoding: `top_band` (id 2, poly 0) has y `1049088/65536 = 16.007812` and the
next vertex `0`; `sq1` (id 6, poly 1) has y `4194816/65536 = 64.007812` and
`2098176/65536 = 32.015625`; `hline` (id 10, poly 3) has y
`6357504/65536 = 97.007812` and `6291456/65536 = 96`; the backdrop (id 1,
poly 9) has y `12582912/65536 = 192` and `0`; `bottom_band` (id 3, poly 10)
has y `12582912/65536 = 192` and `11535360/65536 = 176.015625`. Every quoted
x value is an exact integer (e.g. `4194304/65536 = 64`). `Ceil16_16` maps
`16.007812` to 17, `32.015625` to 33, `64.007812` to 65, `96` to 96,
`97.007812` to 98 and `176.015625` to 177; the rasteriser fills rows
`[ceil(y_top), ceil(y_bottom))` (`rasterize.cpp:891` writes row `pLeft.y`).
That reproduces the measured extents quoted in §3: top band rows `0..16`,
squares `33..64`, `hline` rows `96..97`, bottom band `177..191` (clipped at
row 191 because `ceil(192) = 192` is outside the framebuffer); vertical edges
are unchanged in §3 and the quoted x values are exact integers.

**Answer: the one-row offset has a source explanation.** The transformed vertex
y carries a small positive fractional part for most row coordinates
(`1/128` or `1/64` in these samples), and DeSmuME's `Ceil16_16` rounds every
vertex coordinate up before rasterising. The frame origin is not moved, and
vertical coordinates are exact, which is why EQ-4j saw horizontal edges
`+1.0000` and vertical edges `0.0000`. This is SOURCE-READ plus EMULATOR
output for this ROM and this DeSmuME revision; whether the DS hardware uses the
same ceil convention is exactly what the owner's run decides (HYPOTHESIS,
NOT YET MEASURED).

## 6. Prediction table for the owner's hardware run

Hardware result: **NOT YET MEASURED**. The table states what a successful
hardware capture analysed with `measure_rows.py` should show under each
hypothesis. "Nominal" means the hardware picture matches `quads.json`:
`top_band` rows `0..15` (16 rows), `bottom_band` `176..191`, squares
`32..63`/`128..159`, `hline` one row (96), `vline` `64..159`, all frame edges
painted.

| hypothesis | what the hardware capture shows |
|---|---|
| whole-frame shift in DeSmuME only | nominal: row 0 white, top band 16 rows, squares 32..63, `hline` 1 row, `vline` 64..159. H1 predicts the same hardware picture as H2. |
| per-primitive/per-edge offset in DeSmuME only | nominal: identical to the row above. H1 and H2 cannot be separated by the hardware capture because both are DeSmuME-only; they differ only inside DeSmuME, which §3–§5 already resolves as per-edge. |
| both shifted (hardware shares the DeSmuME convention) | the hardware frame mirrors the DeSmuME frame: row 0 white, `top_band` `0..16` (17 rows), squares `33..64`, `hline` `96..97`, `bottom_band` `177..191`, `vline` `65..160`, vertical edges nominal. |
| neither shifted | hardware nominal; the DeSmuME frame would also have to be nominal. The §3 EMULATOR data already contradict "neither" (row_end_diff=+1 on every horizontal end edge), so this row is excluded unless the harness measurement is wrong. |

The decisive hardware discriminator is the top band: 16 rows starting at 0
indicates hardware is nominal relative to the vertex rectangle (consistent with
EQ-4j's ~`1.07` DS px hardware-above-DeSmuME difference); 17 rows starting at
0 means hardware shares DeSmuME's ceil convention.

## 7. Hardware procedure (owner; NOT run in this task)

1. Build and record the ROM hash:
   ```
   make -C tools/ds_edge_test clean && make -C tools/ds_edge_test
   sha256sum tools/ds_edge_test/ds-edge-test.nds
   ```
2. Copy it to the SD card (set `$SD_ROOT`):
   ```
   cp tools/ds_edge_test/ds-edge-test.nds "$SD_ROOT/roms/nds/"
   ```
3. Run it from TWiLight Menu++ in DS mode and leave it on the static screen
   (top shows the white bands/squares/lines on black; bottom is blank).
4. Capture the top screen through the EQ-4b board with cc3dsfs and the
   `cc_shots.sh` xwd→png loop, reusing the EQ-4h/cc3dsfs command shapes (state
   them; do not run them here):
   ```
   xvfb-run -a -s "-screen 0 1280x720x24" "$CC3DSFS_BIN" \
     --auto_connect --no_audio --no_auto_save \
     --enabled_both 0 --enabled_top 1 --scaling_top 1
   CC3DSFS_BIN="$HOME/path/to/cc3dsfs-build/bin/cc3dsfs" \
     OUT_DIR="$HOME/path/to/out" sh tools/cc3dsfs_capture/cc_shots.sh edge 3 5
   ```
5. Analyse each top-screen capture (400×240 crop, committed mapping):
   ```
   python3 tools/ds_edge_test/measure_rows.py "$HOME/path/to/out/edge_2.png"
   ```
6. Record the raw report and the resulting `classification`, `item=top_band
   measured_rows`/`row_count`, and `frame_vertical_shift`. Hardware is **NOT
   YET MEASURED** in this task.

## 8. Requirement traceability

- **REQ 1 — new `tools/ds_edge_test/` static 256×192 ROM, opaque, JSON geometry,
  bands on all four edges, four squares, 1-px line pair** — FACT §2; build
  `harness_exit=0`, `sha256 3d417c...`; `quads.json`/`include/quads.h`; tests
  `test_quads.py` (geometry bounds, edge-touching, no overlap, distinct ids,
  header byte-identical).
- **REQ 2 — `measure_rows.py` native first/last rows/cols, runs, per-item
  differences; capture PNG sub-pixel via `measure_edges` import** — §3 output;
  `test_measure_rows.py` classification/determinism tests; the capture path
  imports `measure_edges.measure_image` and leaves that file unmodified, and is
  exercised by two 400×240 synthetic-frame CLI tests (nominal →
  `classification=none`, +1 row → `uniform+1`) with one run quoted in §10.
- **REQ 3 — EMULATOR render and answer (i)(ii)(iii), full commands/output;
  harness SOURCE-READ; source explanation** — §3, §4, §5.
- **REQ 4 — tests wired into the root Makefile and `make test`, geometry,
  synthetic 0/+1/partial, negative control, no weakened tests, no pinned
  emulator numbers** — `make test-ds-edge-test` (25 tests OK) and `make test`
  (394 tests OK, 69 skipped) quoted in §10; `test_measure_rows.py` has the
  synthetic 0/+1/partial cases, negative controls, determinism, the two
  capture-PNG CLI cases and the `classification_note` presence/absence checks.
- **REQ 5 — report with prediction table, owner procedure (not run), hardware
  NOT YET MEASURED, append-only pointers, privacy grep, diff budget, `.nds` not
  committed** — this report §6–§10; pointers in `docs/EQUALITY_CHECK_DESIGN.md`
  and `docs/EQ4_HW_COMPARE.md`.
- **REQ 6 — `$HOME`/repo-relative commands, quoted `ls -l` owner/group as
  `<user> <group>`** — all commands above use `$HOME`/`$O`/repo-relative
  paths; no `ls -l` output is quoted.

## 9. Labels, limits and hypotheses

- **FACT:** §2 build/header, §3 native PPM output, §5 geometry dump (all
  quoted above).
- **EMULATOR:** every §3/§5 pixel and vertex number is DeSmuME + the
  out-of-tree harness for this ROM and frame 1.
- **SOURCE-READ:** §4 harness PPM construction; §5 `gfx3d.cpp` viewport/y-flip
  and `rasterize.cpp` ceil/row path.
- **HYPOTHESIS:** the per-edge ceil is the cause of EQ-4j's +1; hardware is
  expected to be nominal until measured.
- **UNVERIFIED:** the DS hardware rasteriser's own rounding/fill rule; the
  exact DS top-left fill rule itself (nominal coverage is stated as the
  half-open vertex rectangle); other DeSmuME revisions, other renderers,
  textured/non-axis-aligned geometry, and the PICA arm.
- The classifier is deliberately coarse. A uniform `+1` of every item cannot
  separate H1 from H2 from pixels alone; this ROM's top-band height (17 rows)
  and the always-painted frame edges are what make the answer extra-visible,
  and the harness dump/`Ceil16_16` source read confirms the mechanism.
- `measure_edges`' interior-plateau band (2..6 px inside) cannot measure a 1-px
  line; `hline`/`vline` are therefore resolved by the native run/row scan, not
  by sub-pixel capture numbers.

## 10. Tests, size audit and privacy

FACT — the new target and the aggregate suite:

```
$ make test-ds-edge-test
.........................
----------------------------------------------------------------------
Ran 25 tests in 0.552s

OK
$ make test
...
Ran 394 tests in 87.782s

OK (skipped=69)
```

FACT — the capture-PNG path on a 400×240 frame rendered from `quads.json`
through `capture_mapping.json` (`scale 1.25, offset 40,0`); this is the path
the owner's hardware capture uses (REQ 2 evidence). One nominal run, then the
`+1`-row run:

```
$ O=$(mktemp -d)
$ python3 -c 'import sys; sys.path.insert(0, "tools/ds_edge_test"); import test_measure_rows as t; t.render_capture().save(sys.argv[1])' "$O/c.png"
$ python3 tools/ds_edge_test/measure_rows.py "$O/c.png"
mode=capture
mapping scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
white_threshold=128.0
backdrop black level=0.0000
edge rect=black kind=backdrop side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=black kind=backdrop side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=top nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=top_band kind=quad side=bottom nominal=16.0000 hw=16.0000 hw-nominal=+0.0000
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 hw=176.0000 hw-nominal=+0.0000
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 hw=16.0000 hw-nominal=+0.0000
edge rect=left_band kind=quad side=top nominal=80.0000 hw=80.0000 hw-nominal=+0.0000
edge rect=left_band kind=quad side=bottom nominal=112.0000 hw=112.0000 hw-nominal=+0.0000
edge rect=right_band kind=quad side=left nominal=240.0000 hw=240.0000 hw-nominal=+0.0000
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 hw=80.0000 hw-nominal=+0.0000
edge rect=right_band kind=quad side=bottom nominal=112.0000 hw=112.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=left nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=right nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=top nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq1 kind=quad side=bottom nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=left nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=right nominal=192.0000 hw=192.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=top nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq2 kind=quad side=bottom nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=left nominal=32.0000 hw=32.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=right nominal=64.0000 hw=64.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=top nominal=128.0000 hw=128.0000 hw-nominal=+0.0000
edge rect=sq3 kind=quad side=bottom nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=left nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=right nominal=192.0000 hw=192.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=top nominal=128.0000 hw=128.0000 hw-nominal=+0.0000
edge rect=sq4 kind=quad side=bottom nominal=160.0000 hw=160.0000 hw-nominal=+0.0000
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
item=top_band row_offset=+0.0000
item=bottom_band row_offset=+0.0000
item=left_band row_offset=+0.0000
item=right_band row_offset=+0.0000
item=sq1 row_offset=+0.0000
item=sq2 row_offset=+0.0000
item=sq3 row_offset=+0.0000
item=sq4 row_offset=+0.0000
classification=none
$ python3 -c 'import sys; sys.path.insert(0, "tools/ds_edge_test"); import test_measure_rows as t; t.render_capture(default_shift=1).save(sys.argv[1])' "$O/c1.png"
$ python3 tools/ds_edge_test/measure_rows.py "$O/c1.png" | tail -9
item=top_band row_offset=+0.8000
item=bottom_band row_offset=+0.8000
item=left_band row_offset=+0.8000
item=right_band row_offset=+0.8000
item=sq1 row_offset=+0.8000
item=sq2 row_offset=+0.8000
item=sq3 row_offset=+0.8000
item=sq4 row_offset=+0.8000
classification=uniform+1
```

FACT — the total diff against `main` (quoted command, output below):

```
$ git diff main | wc -c
97604
```

The built `.nds` is not part of the diff (`*.nds` is ignored, line 8 of
`.gitignore`).

FACT — privacy check:

```
$ grep -rn -E '/(home|Users)/' tools/ds_edge_test
(no output; exit status 1)
```
