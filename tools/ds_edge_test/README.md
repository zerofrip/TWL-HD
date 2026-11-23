<!-- SPDX-License-Identifier: Apache-2.0 -->
# ds_edge_test — DS 3D frame-origin / edge-row probe

A static libnds DS ROM plus a host analyser that decide whether the exact
`+1.0000` DS px vertical placement of horizontal quad edges found in
`docs/reports/eq4j-3d-edge-position.md` is a **whole 3D-frame shift** in
DeSmuME/the harness or a **per-primitive / per-edge rasterisation offset**.

The ROM draws one opaque black full-screen backdrop and ten opaque white
polygons: bands touching the top and bottom scanlines and the left and right
columns, four isolated 32×32 interior squares, and one 1-pixel-high and one
1-pixel-wide interior line. Because a band touches row 0 / row 191 and columns
0 / 255, the frame origin itself becomes visible; the old `ds-alpha-test` ROM
could not show it. Real hardware is **not measured in this task**; the owner
runs §9. Labels follow `docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`.
Result and full evidence: `docs/reports/eq4k-edge-rows.md`.

## 1. Files

| Path | Purpose |
|---|---|
| `quads.json` | single source of truth: backdrop + 10 polygons |
| `gen_quads.py` | generates `include/quads.h` |
| `include/quads.h` | committed build-time output of `gen_quads.py` |
| `source/main.c` | ARM9 ROM: static opaque scene; bottom screen blank |
| `Makefile` | devkitARM/libnds build (copied from `ds_alpha_test`) |
| `measure_rows.py` | native-frame row/column analyser and capture-PNG edge reader |
| `capture_mapping.json` | committed copy of the EQ-4c DS→capture mapping (reused) |
| `test_quads.py` | geometry / header / ROM-source consistency |
| `test_measure_rows.py` | synthetic 0/+1/partial shift classification, determinism |

The ROM links only `-lnds9`; no TWL-HD GX/`common/` code (asserted by
`test_quads.py`). The built `.nds` is not committed (`*.nds` is in
`.gitignore`, like the sibling ROMs).

## 2. Scene and geometry

Nominal coverage is the half-open vertex rectangle `[x, x+w) × [y, y+h)` in DS
pixels (256×192). The DS rasteriser fill rule itself is not derived here
(`UNVERIFIED`, §11).

| item | rectangle | rows / cols of interest |
|---|---|---|
| `black` backdrop | `0,0,256,192` | opaque black `0,0,0`, id 1, z 0 |
| `top_band` | `0,0,256,16` | starts on scanline 0 |
| `bottom_band` | `0,176,256,16` | ends on scanline 191 |
| `left_band` | `0,80,16,32` | starts on column 0 |
| `right_band` | `240,80,16,32` | ends on column 255 |
| `sq1`..`sq4` | `32,32` / `160,32` / `32,128` / `160,128`, 32×32 | isolated interiors |
| `hline` | `64,96,128,1` | one interior row (96) |
| `vline` | `208,64,1,96` | one interior column (208) |

All ten polygons are white `31,31,31`, alpha 31 (opaque), z=+64, ids 2..11;
the backdrop is z=0. Rectangles do not overlap and leave at least one clear row
or column between neighbours (asserted by `test_quads.py`), so each item's
measured extent is unambiguous.

## 3. State settings (and why)

- `videoSetMode(MODE_0_3D)`, `glInit()`, `glViewport(0,0,255,191)`,
  `glOrthof32(0,256,192,0,-1<<12,1<<12)`, raw `glVertex3v16` vertices — the
  same integer pixel convention as `ds_alpha_test`/`ds_vtxcolor_test`, so the
  edits are comparable.
- `glDisable(GL_BLEND | GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS |
  GL_OUTLINE | GL_FOG)` — **every** 3D feature is off; polygons are opaque, so
  no alpha is involved. `GL_TOON_HIGHLIGHT` stays 0; lighting is per-polygon
  (`POLY_FORMAT_LIGHT0..3` stay 0).
- `glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(p->poly_id))` before
  every polygon; no fog. The POLYGON_ATTR depth-test bit stays 0.
- Clear: `glClearColor(0,0,0,31)`, `glClearPolyID(63)`,
  `glClearDepth(GL_MAX_DEPTH)`.
- Depth relation: backdrop z=0, white polygons z=+64. A larger source z maps to
  a smaller post-transform z, nearer under LESS, so the white stays visible
  (same proven relation as `ds_alpha_test`).

## 4. Build

```
$ make -C tools/ds_edge_test clean && make -C tools/ds_edge_test
$ sha256sum tools/ds_edge_test/ds-edge-test.nds
```

FACT — raw output and the `sha256` are quoted in report §2. `include/quads.h`
is regenerated from `quads.json` by the Makefile rule and is byte-identical to
the committed copy (`test_quads.py`).

## 5. `measure_rows.py`

```
python3 tools/ds_edge_test/measure_rows.py --native <frame.ppm>
python3 tools/ds_edge_test/measure_rows.py [--mapping M.json] <capture.png>
```

- `--native`: a native 256×192 P6 PPM (the DeSmuME harness `--ppm` output).
  Identity mapping. A pixel is white when `(R+G+B)/3 >= 128`. Prints the
  first/last white row and column, the white row/column runs, whether the four
  screen edges are painted, and per item the nominal rows/columns against the
  measured extent (`row_diff` = measured first row − nominal first row,
  `row_end_diff` = measured last row − nominal last row, and the column
  equivalents). Deterministic `key=value` output.
- Capture PNG: a 400×240 cc3dsfs crop, mapped with the committed
  `capture_mapping.json` (`scale 1.25, offset 40,0`). Sub-pixel edge positions
  come from `tools/ds_alpha_test/measure_edges.py` **by import**
  (`measure_image`), unmodified. It prints the per-edge DS-px estimates and a
  row classification.
- Pitfall: `measure_edges`' interior-plateau band (2..6 DS px inside) falls
  outside a 1-px line, so `hline`/`vline` edges are skipped there with a
  reason. Their exact row/column comes from the native run scan; do not quote
  sub-pixel numbers for them.

Classification from per-item first-row offsets: `none` (all nominal),
`uniform+1` (every item one row lower — a single 3D frame cannot separate a
whole-frame shift from a uniform per-primitive offset), `partial+1` (top/bottom
anchor bands nominal, other items one row lower), `mixed` otherwise. The native
run also prints `frame_vertical_shift`/`frame_horizontal_shift` from whether the
screen edges are painted, which is what separates a whole-frame shift from an
edge-level offset.

## 6. Tests

```
make test-ds-edge-test      # or: make test
```

Geometry bounds / edge-touching / no overlap / distinct ids, regenerated
`include/quads.h` byte-identical, ROM-source token checks, synthetic native
frames with a known 0-row and +1-row shift classified correctly, a
per-primitive (interior-only) synthetic frame classified `partial+1`, negative
controls, the classifier edge cases, and CLI determinism. No emulator numbers
are pinned: the out-of-tree harness PPM is not reproducible from committed
files.

## 7. Emulator result (summary)

EMULATOR (DeSmuME harness, frame 1): the frame origin is **not** shifted —
row 0, row 191, column 0 and column 255 are all painted. Horizontal edges whose
transformed vertex y carries a small positive fraction are placed one row lower
(top band 17 rows, squares `33..64` instead of `32..63`, `hline` 2 rows,
`bottom_band` `177..191`), while every vertical edge stays nominal. The harness
PPM has no row offset/flip/crop. This is a per-edge rasteriser effect
(`Ceil16_16` on the transformed vertex y), not a whole-frame shift. Real
hardware remains **NOT YET MEASURED**. Full output and source quotes: report
§3–§5.

## 8. Why the old ROM could not answer this

`ds-alpha-test`'s backdrops fill the whole screen and its quads sit at
`y=32..160`, so there is no white at row 0 or row 191. A whole-frame one-row
shift and a per-edge offset look identical there (eq4j §6). This ROM adds
full-width bands at both scanline extremes and a 1-px line, which exposes the
frame origin and the exact rasterised row.

## 9. Hardware procedure (owner; NOT run in this task)

The complete owner procedure is in `docs/reports/eq4k-edge-rows.md` §7
(build and record the `sha256sum`, copy to `$SD_ROOT/roms/nds/`, run from
TWiLight Menu++ in DS mode, capture through the EQ-4b board with cc3dsfs and
`cc_shots.sh`, analyse with `measure_rows.py`, record the raw report). The
hardware answer is **NOT YET MEASURED**.

## 10. Privacy

Every command uses `$HOME` / `$DEVKITPRO` / `$DEVKITARM` / `$SD_ROOT` /
`$CC3DSFS_BIN`; no literal home-directory path is added. Check after
`make clean` (built ELFs embed debug paths):

```
$ grep -rn -E '/(home|Users)/' tools/ds_edge_test
(no output; exit status 1)
```

## 11. Limits and hypotheses

- **Rasteriser fill rule (UNVERIFIED):** nominal coverage is stated as the
  half-open vertex rectangle `[x, x+w) × [y, y+h)`, the same convention as
  `ds_alpha_test`. The DS top-left/tie rule is not derived from quoted
  documentation. The measured DeSmuME result is a FACT only for this ROM, this
  harness and frame 1.
- **Whole-frame versus per-edge:** a uniform one-row offset of every item is
  ambiguous from pixels alone (both DeSmuME-side hypotheses predict it). This
  ROM avoids that ambiguity because row 0 is painted while the top band is one
  row taller than nominal, which a whole-frame shift cannot produce. The
  source read (`Ceil16_16` on the transformed vertex y) is the explanation.
- **Hardware direction (HYPOTHESIS):** whether the DS hardware also ceils its
  transformed y (and therefore whether hardware matches nominal, as eq4j
  measured) is exactly what the owner run decides. The prediction table is in
  report §6.
- **Capture noise:** the cc3dsfs path measured identity to ±3 8-bit levels
  (`tools/ds_vtxcolor_test/README.md` §13); `WHITE_THRESHOLD = 128` is far from
  both plateaus.
- Nothing in this task is a hardware FACT.

<!-- Requirement traceability lives in docs/reports/eq4k-edge-rows.md section 8. -->
