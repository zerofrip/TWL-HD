<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4l — 3D edge rows on real hardware: the frame-origin ROM lands nominal, unlike DeSmuME

**Status:** REAL HARDWARE MEASURED (offline analysis of the owner's capture).
The owner ran `ds-edge-test.nds` on a real New 3DS in DS mode; the 400×240
top-left crop of the capture shows the **top band 16 rows starting at the first
capture row**, the **bottom band reaching the last capture row**, and every
measurable edge at nominal within about 0.1 DS px. This is the opposite of
DeSmuME, which puts the top band at 17 rows and shifts horizontal end edges
`+1`. Every number below is quoted from a command run in this round.

```
basis=one console, one capture session (4 shots), one ROM
```

Labels follow `docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`: **FACT** = a
command and its complete raw output quoted here; **OWNER-REPORTED** = the
owner's statement, not reproduced here; **HYPOTHESIS** / **UNVERIFIED** = not
measured by the quoted output.

**Question.** EQ-4k predicted what a hardware capture should show under each
hypothesis (`docs/reports/eq4k-edge-rows.md` §6). The decisive discriminator is
the top band: 16 rows starting at row 0 means hardware is nominal relative to
the half-open vertex rectangle; 17 rows means hardware shares DeSmuME's
`Ceil16_16` convention. This report records which one the hardware shows.

## 1. Setup (OWNER-REPORTED run; offline analysis only)

- **ROM launch — OWNER-REPORTED.** The owner ran `ds-edge-test.nds` (built from
  `tools/ds_edge_test/`, the EQ-4k ROM) on a **New 3DS in DS mode** via
  **TWiLight Menu++** on 2026-11-23. No hardware action, console access, or
  emulator run was performed in this task; the run is reported by the owner,
  not reproduced here.
- **Capture — OWNER-REPORTED.** A passive capture board through `cc3dsfs`
  recorded the **top screen unscaled** as four 1280×720 root screenshots
  `ed1_1.png .. ed1_4.png`. The 400×240 top-left crops are
  `ed1_N_top400x240.png` in `$HOME/3ds/hwtest-staging/cc3d-out/`.
- **Log (FACT, quoted):** `ed1.log` records the board connection:

```
[cc3dsfs] File $HOME/.config/cc3dsfs/cc3dsfs.cfg load failed.
Defaults re-loaded.
Setting vertical sync not supported
[cc3dsfs] Connected to Opt. N3DS - USB 0
```

- **Commands run in this round (offline):** `sha256sum`, `cmp`, a PIL per-pixel
  diff, a PIL raw-pixel probe, and `measure_rows.py`. Only the `ed1_3` crop is
  added to the repo; no xwd or 1280×720 file is committed.

## 2. Fixture integrity (REQ 1)

`ed1_3_top400x240.png` was copied verbatim into
`tools/ds_edge_test/fixtures/`. FACT — source and copy are byte-identical:

```
$ sha256sum "$HOME/3ds/hwtest-staging/cc3d-out/ed1_3_top400x240.png" tools/ds_edge_test/fixtures/ed1_3_top400x240.png
1379c2bd00e14c2f485448fcda9d50b3ff711df277baf5edabc9786e099f075c  $HOME/3ds/hwtest-staging/cc3d-out/ed1_3_top400x240.png
1379c2bd00e14c2f485448fcda9d50b3ff711df277baf5edabc9786e099f075c  tools/ds_edge_test/fixtures/ed1_3_top400x240.png
$ cmp "$HOME/3ds/hwtest-staging/cc3d-out/ed1_3_top400x240.png" tools/ds_edge_test/fixtures/ed1_3_top400x240.png && echo identical
identical
$ ls -l tools/ds_edge_test/fixtures/ed1_3_top400x240.png
-rw-rw-r-- <user> <group> 2455 Nov 23 22:33 tools/ds_edge_test/fixtures/ed1_3_top400x240.png
```

**The four crops are not four identical shots.** FACT — `sha256sum` over the
four crops shows two byte-identical pairs (`ed1_2 == ed1_4`; `ed1_1` and
`ed1_3` are distinct):

```
$ sha256sum "$HOME"/3ds/hwtest-staging/cc3d-out/ed1_*_top400x240.png
b9fe84de38b25d12a8cb13de2a3a644dd1dbcae013ac6b996b247d89fe4e86a0  $HOME/.../ed1_1_top400x240.png
70c8465778797931ecd5cf3c39cd55562cbfc28ab6653170e67cc9e92a2b1c9c  $HOME/.../ed1_2_top400x240.png
1379c2bd00e14c2f485448fcda9d50b3ff711df277baf5edabc9786e099f075c  $HOME/.../ed1_3_top400x240.png
70c8465778797931ecd5cf3c39cd55562cbfc28ab6653170e67cc9e92a2b1c9c  $HOME/.../ed1_4_top400x240.png
```

FACT — a PIL per-pixel diff of each crop against the chosen `ed1_3` (the diff
prints no paths; `ndiff` is the number of pixels whose max channel delta is
non-zero, `maxdelta` the largest such delta):

```
$ DIR="$HOME/3ds/hwtest-staging/cc3d-out" python3 - <<'PYEOF'
import os
from PIL import Image, ImageChops
d = os.environ["DIR"]
def load(n):
    im = Image.open(os.path.join(d, "ed1_%s_top400x240.png" % n))
    im.load()
    return im.convert("RGB")
base = load("3")
for n in ("1", "2", "4"):
    other = load(n)
    diff = ImageChops.difference(base, other)
    px = diff.load()
    ndiff = 0
    maxdelta = 0
    for y in range(diff.height):
        for x in range(diff.width):
            r, g, bl = px[x, y]
            ndiff += 1 if (r or g or bl) else 0
            maxdelta = max(maxdelta, r, g, bl)
    print("ed1_%s vs ed1_3 bbox= %s ndiff= %d maxdelta= %d" % (
        n, diff.getbbox(), ndiff, maxdelta))
print("ed1_3 vs ed1_3 bbox= %s ndiff= %d maxdelta= %d" % (
    ImageChops.difference(base, base).getbbox(), 0, 0))
PYEOF
ed1_1 vs ed1_3 bbox= (40, 37, 360, 218) ndiff= 1268 maxdelta= 9
ed1_2 vs ed1_3 bbox= (41, 37, 360, 218) ndiff= 850 maxdelta= 9
ed1_4 vs ed1_3 bbox= (41, 37, 360, 218) ndiff= 850 maxdelta= 9
ed1_3 vs ed1_3 bbox= None ndiff= 0 maxdelta= 0
```

So `ed1_2 == ed1_4`, and the two distinct frames (`ed1_1`, `ed1_3`) differ
only on the polygon interiors, by at most 9 levels — the video-capture noise
floor, not a geometry change. The classification below does not depend on which
of the distinct frames is used; `ed1_3` is committed as the user task requires
and is a representative frame of this capture session.

## 3. Full `measure_rows.py` run on the fixture (REQ 2)

FACT — complete raw output (exit status 0), quoted once:

```
$ python3 tools/ds_edge_test/measure_rows.py tools/ds_edge_test/fixtures/ed1_3_top400x240.png
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
edge rect=top_band kind=quad side=bottom nominal=16.0000 hw=15.9241 hw-nominal=-0.0759
edge rect=bottom_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=bottom_band kind=quad side=top nominal=176.0000 hw=175.9241 hw-nominal=-0.0759
edge rect=bottom_band kind=quad side=bottom nominal=192.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=left nominal=0.0000 skipped=on the DS screen boundary (capture crop)
edge rect=left_band kind=quad side=right nominal=16.0000 hw=15.9240 hw-nominal=-0.0760
edge rect=left_band kind=quad side=top nominal=80.0000 hw=79.9241 hw-nominal=-0.0759
edge rect=left_band kind=quad side=bottom nominal=112.0000 hw=111.9241 hw-nominal=-0.0759
edge rect=right_band kind=quad side=left nominal=240.0000 hw=239.9242 hw-nominal=-0.0758
edge rect=right_band kind=quad side=right nominal=256.0000 skipped=on the DS screen boundary (capture crop)
edge rect=right_band kind=quad side=top nominal=80.0000 hw=79.9242 hw-nominal=-0.0758
edge rect=right_band kind=quad side=bottom nominal=112.0000 hw=111.9239 hw-nominal=-0.0761
edge rect=sq1 kind=quad side=left nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq1 kind=quad side=right nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq1 kind=quad side=top nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq1 kind=quad side=bottom nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq2 kind=quad side=left nominal=160.0000 hw=159.9242 hw-nominal=-0.0758
edge rect=sq2 kind=quad side=right nominal=192.0000 hw=191.9240 hw-nominal=-0.0760
edge rect=sq2 kind=quad side=top nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq2 kind=quad side=bottom nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq3 kind=quad side=left nominal=32.0000 hw=31.9242 hw-nominal=-0.0758
edge rect=sq3 kind=quad side=right nominal=64.0000 hw=63.9240 hw-nominal=-0.0760
edge rect=sq3 kind=quad side=top nominal=128.0000 hw=127.9242 hw-nominal=-0.0758
edge rect=sq3 kind=quad side=bottom nominal=160.0000 hw=159.9240 hw-nominal=-0.0760
edge rect=sq4 kind=quad side=left nominal=160.0000 hw=159.9242 hw-nominal=-0.0758
edge rect=sq4 kind=quad side=right nominal=192.0000 hw=191.9240 hw-nominal=-0.0760
edge rect=sq4 kind=quad side=top nominal=128.0000 hw=127.9242 hw-nominal=-0.0758
edge rect=sq4 kind=quad side=bottom nominal=160.0000 hw=159.9240 hw-nominal=-0.0760
edge rect=hline kind=quad side=left nominal=64.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=right nominal=192.0000 skipped=interior band falls outside the capture
edge rect=hline kind=quad side=top nominal=96.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=hline kind=quad side=bottom nominal=97.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=left nominal=208.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=right nominal=209.0000 skipped=plateau contrast 0.00 below 10.00
edge rect=vline kind=quad side=top nominal=64.0000 skipped=interior band falls outside the capture
edge rect=vline kind=quad side=bottom nominal=160.0000 skipped=interior band falls outside the capture
item=top_band row_offset=-0.0759
item=bottom_band row_offset=-0.0759
item=left_band row_offset=-0.0759
item=right_band row_offset=-0.0758
item=sq1 row_offset=-0.0758
item=sq2 row_offset=-0.0758
item=sq3 row_offset=-0.0758
item=sq4 row_offset=-0.0758
classification=none
```

FACT — every measurable item sits `0.0758`–`0.0761` DS px above its nominal
edge; `int(round(-0.076)) == 0`, so `measure_rows.classify_offsets` returns
`none` (`measure_rows.py:129-143`). `hline`/`vline` are legitimately skipped
(§5). The mapping printed is the committed `capture_mapping.json`
(`scale 1.25, offset 40,0`).

## 4. Absolute pixel checks (REQ 2, independent of the fitted offset)

These raw pixel reads are taken straight from the crop, so the conclusions
below do not depend on the `-0.076` fitted per-item offset. FACT — PIL probe
(raw RGB tuples):

```
$ python3 - <<'PYEOF'
from PIL import Image
import os
p = os.path.join(os.environ["HOME"], "3ds/hwtest-staging/cc3d-out/ed1_3_top400x240.png")
im = Image.open(p).convert("RGB")
px = im.load()
print("size=%dx%d" % im.size)
print("col100 rows 0..4:", [px[100, r] for r in range(0, 5)])
print("col100 rows 236..239:", [px[100, r] for r in range(236, 240)])
print("row120 cols 37..41:", [(c, px[c, 120]) for c in range(37, 42)])
print("row120 cols 355..361:", [(c, px[c, 120]) for c in range(355, 362)])
print("sq1 top edge col100 rows 37..41:", [(r, px[100, r]) for r in range(37, 42)])
print("sq1 bottom edge col100 rows 77..81:", [(r, px[100, r]) for r in range(77, 82)])
PYEOF
size=400x240
col100 rows 0..4: [(255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
col100 rows 236..239: [(255, 255, 255), (255, 255, 255), (255, 255, 255), (255, 255, 255)]
row120 cols 37..41: [(37, (0, 0, 0)), (38, (0, 0, 0)), (39, (0, 0, 0)), (40, (255, 255, 255)), (41, (255, 255, 255))]
row120 cols 355..361: [(355, (255, 255, 255)), (356, (255, 255, 255)), (357, (255, 255, 255)), (358, (255, 255, 255)), (359, (255, 255, 255)), (360, (0, 0, 0)), (361, (0, 0, 0))]
sq1 top edge col100 rows 37..41: [(37, (8, 8, 8)), (38, (0, 0, 0)), (39, (41, 40, 41)), (40, (255, 255, 255)), (41, (255, 255, 255))]
sq1 bottom edge col100 rows 77..81: [(77, (247, 247, 247)), (78, (255, 255, 255)), (79, (214, 215, 214)), (80, (0, 0, 0)), (81, (0, 0, 0))]
```

What each result implies (no dependence on the fitted offset; the DS columns
and rows are the committed mapping inverted, `capture = offset + 1.25·ds`,
`offset 40,0`):

- **Frame top/bottom are painted (col 100).** Rows 0–4 are all white
  `(255,255,255)` and rows 236–239 are all white. So the top band covers the
  **first** capture rows and the bottom band covers the **last** capture rows.
  If the hardware picture were shifted down by one DS row, the first capture
  rows would show the black backdrop; if shifted up, the last rows would be
  black. They are not — the frame origin is anchored at the crop's own edges.
- **Left/right band edges at row 120.** Row 120 is DS row 96 (`120 / 1.25`).
  Cols 37–39 are black `(0,0,0)`, col 40 is white → the left band begins at
  capture col 40 = DS col 0 (`40 + 1.25·0`). Cols 355–359 are white, col 360 is
  black → the right band ends at capture col 359 = DS col 255, with black
  starting at col 360 = DS col 256 (`40 + 1.25·256`). Both vertical extents are
  exactly the half-open `[0,256)` columns, with no horizontal shift.
- **sq1 top edge (col 100, inside the square).** Row 38 is black `(0,0,0)`, row
  39 is a transition `(41,40,41)`, row 40 is white `(255,255,255)`. Under the
  mapping the nominal sq1 top edge DS row 32 is at capture row 40
  (`1.25·32`), so the white square starts at the row a nominal rectangle
  requires; a `+1` DS-row drop would put the transition near capture row 41.
- **sq1 bottom edge (col 100).** Row 78 is white `(255,255,255)`, row 79 is a
  transition `(214,215,214)`, row 80 is black `(0,0,0)`. The nominal sq1 bottom
  edge DS row 64 is at capture row 80 (`1.25·64`); the square ends one capture
  row above it (row 78/79), again nominal within about 0.1 DS px, with no `+1`.

## 5. Hardware vs DeSmuME comparison (REQ 3)

The DeSmuME numbers are quoted verbatim from
`docs/reports/eq4k-edge-rows.md` (this report does not re-derive them). The
`measure_rows.py --native` item lines (§3 there):

```
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

The §6 prediction table (`docs/reports/eq4k-edge-rows.md:448-458`), quoted:

```
| hypothesis | what the hardware capture shows |
|---|---|
| whole-frame shift in DeSmuME only | nominal: row 0 white, top band 16 rows, squares 32..63, `hline` 1 row, `vline` 64..159. H1 predicts the same hardware picture as H2. |
| per-primitive/per-edge offset in DeSmuME only | nominal: identical to the row above. H1 and H2 cannot be separated by the hardware capture because both are DeSmuME-only; they differ only inside DeSmuME, which §3–§5 already resolves as per-edge. |
| both shifted (hardware shares the DeSmuME convention) | the hardware frame mirrors the DeSmuME frame: row 0 white, `top_band` `0..16` (17 rows), squares `33..64`, `hline` `96..97`, `bottom_band` `177..191`, `vline` `65..160`, vertical edges nominal. |
| neither shifted | hardware nominal; the DeSmuME frame would also have to be nominal. The §3 EMULATOR data already contradict "neither" (row_end_diff=+1 on every horizontal end edge), so this row is excluded unless the harness measurement is wrong. |
```

```
The decisive hardware discriminator is the top band: 16 rows starting at 0
indicates hardware is nominal relative to the vertex rectangle (consistent with
EQ-4j's ~`1.07` DS px hardware-above-DeSmuME difference); 17 rows starting at
0 means hardware shares DeSmuME's ceil convention.
```

Compact comparison (hardware = this fixture, §3/§4; DeSmuME = eq4k §3):

| item | hardware (this round) | DeSmuME (eq4k §3) |
|---|---|---|
| top band | 16 rows starting at capture row 0; bottom edge `hw=15.9241` vs nominal 16 | `measured_rows=0-16` → 17 rows, `row_end_diff=1` |
| sq1 (32×32) | top `31.9242`, bottom `63.9240`, left `31.9242`, right `63.9240` (all nominal within 0.08) | `measured_rows=33-64`, `row_diff=1`, `row_end_diff=1` |
| sq2 (32×32) | top `31.9242`, bottom `63.9240`, left `159.9242`, right `191.9240` | `measured_rows=33-64`, `row_diff=1`, `row_end_diff=1` |
| sq3 (32×32) | top `127.9242`, bottom `159.9240`, left `31.9242`, right `63.9240` | `measured_rows=129-160`, `row_diff=1`, `row_end_diff=1` |
| sq4 (32×32) | top `127.9242`, bottom `159.9240`, left `159.9242`, right `191.9240` | `measured_rows=129-160`, `row_diff=1`, `row_end_diff=1` |
| left/right bands | top `79.9241`/`79.9242`, bottom `111.9241`/`111.9239`, inner edges `15.9240`/`239.9242` | `measured_rows=81-112`, `row_diff=1` |
| bottom band | top edge `175.9241` vs nominal 176; the band reaches the last capture row (white at rows 236–239) | `measured_rows=177-191`, `row_diff=1` |
| hline (1-px high) | **UNMEASURED** — skipped, `plateau contrast 0.00 below 10.00` / `interior band falls outside the capture` | `measured_rows=96-97` (2 rows), `row_end_diff=1` |
| vline (1-px wide) | **UNMEASURED** — skipped, same reasons | `measured_rows=65-160`, `row_diff=1` |
| columns | vertical edges within `0.08` of nominal; `col` extents whole | every `col_diff=0`, `col_end_diff=0` |

**Why `hline`/`vline` are UNMEASURED on hardware:** `measure_edges` measures a
50 % crossing between two locally measured plateaus, but its interior-plateau
band sits 2–6 DS px inside the rectangle; a 1-px line has no such band, so the
tool skips those edges (`plateau contrast 0.00 below 10.00`) or reports the
interior band outside the capture. These are the same skip reasons the eq4k
synthetic run printed (`docs/reports/eq4k-edge-rows.md:609-616`) and the
documented pitfall in `tools/ds_edge_test/README.md` §5: 1-px line placement
cannot be read from the resampled capture, only from the native scan (which
needs DeSmuME, not hardware). So the hardware hline/vline position is
**UNMEASURED** here, and these rows are not used for any conclusion.

## 6. Conclusions

### FACT (shown by the quoted output above)

- **The hardware top band is 16 rows starting at the first capture row.** The
  bottom edge measures `hw=15.9241` against nominal 16 (`-0.0759`), and the raw
  probe shows capture rows 0–4 white at col 100 (the band reaches the crop's
  first rows). This matches the "nominal" rows of the eq4k §6 table and
  contradicts the "both shifted" row (`0..16`, 17 rows).
- **The hardware bottom band reaches the last capture row.** Its top edge is
  `hw=175.9241` against nominal 176, and rows 236–239 are white at col 100.
- **Every measurable hardware edge is at nominal within about 0.1 DS px.**
  `measure_rows.py` prints `hw-nominal` between `-0.0758` and `-0.0761` for
  every measurable edge, and all eight `row_offset` values round to 0
  (`classification=none`). The raw checks independently place the frame
  edges and sq1 edges at the exact DS columns/rows the half-open vertex
  rectangle requires.
- **This is in contrast to DeSmuME**, whose quoted §3 output has a 17-row top
  band (`0-16`), squares `33-64`, and `+1` horizontal end edges (all quoted in
  §5).

### HYPOTHESIS

- **The hardware rasterises the vertex rectangle as the half-open `[y0,y1)`,
  while DeSmuME adds one row on horizontal edges (`Ceil16_16`).** That
  per-edge `+1` in DeSmuME — not a hardware shift — is the likely cause of the
  1-DS-px `dy`-negative offset recorded by EQ-4f/EQ-4g/EQ-4j. EQ-4j states:

```
**HYPOTHESIS.** The hardware rasteriser places axis-aligned quad edges at
nominal while DeSmuME's places the horizontal edges one DS pixel lower and the
vertical edges on nominal, so the ~1 px dy-negative offset of EQ-4f/EQ-4g is a
real vertical placement difference **between hardware and DeSmuME**, not a
hardware shift, not a capture-mapping artefact and not resampling blur (it fits
"`dy` negative for all class centroids, `dx` mixed").
```

  EQ-4g and EQ-4f say the same thing about the 1-2 DS px offset being real and
  not a mapping artefact (`eq4g-capture-offset.md` §5 HYPOTHESIS; `eq4f` §5
  HYPOTHESIS: "the capture sits systematically above BOTH references — `dy` is
  negative in all 20 class/angle/reference pairs"). This round pins the
  hardware end at nominal for this ROM's axis-aligned quads, which is the
  missing anchor EQ-4j listed as UNVERIFIED ("which side is displaced").

### UNVERIFIED (what this round does NOT show)

- **Other consoles and sessions.** One New 3DS, one capture session (4 shots),
  one ROM. No second console, board, or session is measured here.
- **Other geometry classes.** Textured and non-axis-aligned polygons are not
  tested by this ROM; EQ-4j's UNVERIFIED list also covers these. 1-px lines are
  UNMEASURED on hardware (§5).
- **What our own PICA path does.** Not exercised by this round. EQ-4k lists
  "the DS hardware rasteriser's own rounding/fill rule ... textured/
  non-axis-aligned geometry, and the PICA arm" as UNVERIFIED
  (`eq4k-edge-rows.md:520-542`); this hardware result removes only the hardware
  side of that list for axis-aligned opaque quads. EQ-4j's own UNVERIFIED list
  (quoted):

```
**UNVERIFIED.** This probe cannot separate an absolute hardware raster shift
from the fitted mapping offset: `offset_x/offset_y` were fitted against another
ROM's nominal geometry, so a uniform hardware raster shift would be absorbed
there. Only the cross-ROM mapping consistency (§3.5's alternative-mapping run)
and the hw-minus-DeSmuME difference are measured. Nor can it say *which side* is
displaced: the ROM has no horizontal edge at the very top/bottom scanline (the
backdrops fill the screen), so a whole-frame one-row DeSmuME shift is
indistinguishable from rasterising the quads one row low. It also does not
cover other consoles/sessions, the true resampler kernel, textured or
non-axis-aligned geometry, the PICA arm for this ROM, or the DS rasteriser's
rounding convention.
```

- **The exact resampler kernel**, and separation of the mapping-relative
  residuals from an absolute hardware raster shift, remain as EQ-4j left them.

## 7. Tests (REQ 4)

New file `tools/ds_edge_test/test_fixture_hw.py`, auto-discovered by the
existing `make test-ds-edge-test` target (root `Makefile:637-638`) — no Makefile
change. It drives the committed fixture through the real CLI and pins literals
from §3; a missing fixture fails (verified), and only PIL absence skips the
pixel part, after the presence assertion. FACT — the suite grew from 25 to 28
tests:

```
$ make test-ds-edge-test
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/ds_edge_test -p 'test_*.py'
............................
----------------------------------------------------------------------
Ran 28 tests in 0.708s

OK
```

FACT — root `make test` (the aggregate target, which re-runs the edge suite) passes; the per-target progress and host-CLI stderr above the summary are omitted (`...`):

```
$ make test
...
----------------------------------------------------------------------
Ran 394 tests in 83.742s

OK (skipped=69)
```

FACT — a temporarily renamed fixture makes the fixture test FAIL (run directly,
then restored; sha256 re-checked):

```
$ mv tools/ds_edge_test/fixtures/ed1_3_top400x240.png /tmp/ed1_3_hold.png
$ (cd tools/ds_edge_test && PYTHONDONTWRITEBYTECODE=1 python3 -B test_fixture_hw.py FixtureCliTest)
...
AssertionError: False is not true : missing fixture .../tools/ds_edge_test/fixtures/ed1_3_top400x240.png; it is committed, not optional
----------------------------------------------------------------------
Ran 2 tests in 0.001s
FAILED (failures=2)
$ mv /tmp/ed1_3_hold.png tools/ds_edge_test/fixtures/ed1_3_top400x240.png
$ sha256sum tools/ds_edge_test/fixtures/ed1_3_top400x240.png
1379c2bd00e14c2f485448fcda9d50b3ff711df277baf5edabc9786e099f075c  tools/ds_edge_test/fixtures/ed1_3_top400x240.png
```

The negative control `render_capture(default_shift=1)` is asserted **not**
`classification=none` (it prints `classification=uniform+1`). No existing test
in `test_measure_rows.py` was modified, weakened, or skipped.

## 8. Requirement traceability

- **REQ 1 fixture, sha256/cmp, four-crop diff, only ed1_3 kept, no
  xwd/1280×720** — §2; hashes equal, `cmp identical`, PIL per-pixel diff
  quoted, fixture 2455 bytes, no raw capture committed.
- **REQ 2 full tool output and absolute pixel checks** — §3 (complete output,
  `classification=none`, all offsets `-0.0758..-0.0761`) and §4 (raw pixels +
  the down/up-shift implication without the fitted offset).
- **REQ 3 hardware-vs-DeSmuME table, quoted eq4k §3/§6, hline/vline
  UNMEASURED** — §5.
- **REQ 4 tests, never weakened, missing fixture FAILS, +1 not none** — §7;
  `make test-ds-edge-test` = 28 tests OK (was 25); missing-fixture run fails;
  `test_measure_rows.py` untouched.
- **REQ 5 report + append-only pointers + eq4k note** — this report; pointers
  appended to `docs/EQUALITY_CHECK_DESIGN.md` and `docs/EQ4_HW_COMPARE.md`; a
  §6-pointer note appended at the end of `docs/reports/eq4k-edge-rows.md`.
- **Rules: no literal home paths / user names, diff < 90 KB, every REQ with
  in-round command evidence** — §1/§9, §10.

## 9. Privacy

Every command uses `$HOME`; the expanded home path and the owner/group in
`ls -l` are not reproduced (shown as `$HOME` and `<user> <group>`). The
`/(home|Users)/` grep over the touched paths is clean (§10).

## 10. Size audit

FACT — the total diff against `main` stays below the 90 KB (92,160-byte) limit:

```
$ git diff main | wc -c
41686
```

FACT — no literal home/user path in the touched paths:

```
$ grep -rn -E '/(home|Users)/' tools/ds_edge_test docs/EQUALITY_CHECK_DESIGN.md docs/EQ4_HW_COMPARE.md docs/reports/eq4l-edge-rows-hw-result.md docs/reports/eq4k-edge-rows.md
(no output; grep exit status 1 = no match)
```
