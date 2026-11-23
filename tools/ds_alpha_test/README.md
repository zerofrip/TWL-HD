<!-- SPDX-License-Identifier: Apache-2.0 -->
# ds_alpha_test — DS polygon-alpha convention probe (a/31 vs (a+1)/32)

A static libnds DS ROM plus a host PNG analyser that decide which blend weight
the DS 3D engine uses for a translucent polygon with 5-bit polygon alpha `a`:
`A1 = a/31` versus `A2 = (a+1)/32`.

The ROM draws an opaque black backdrop (left half) and an opaque 5-bit-16 grey
backdrop (right half), each carrying seven translucent white quads (alpha
`a = 4, 8, 12, 16, 20, 24, 28`, distinct polygon ids). The analyser compares
the measured colour against A1/A2. Real hardware is **not measured in this
task**; the owner runs §9. Labels follow
`docs/reports/eq4b-cc3dsfs-ds-capture.md:11-14`: FACT = a command and its raw
output quoted; HYPOTHESIS / UNVERIFIED = not measured.

## 1. Files

| Path | Purpose |
|---|---|
| `quads.json` | single source of truth: backdrops + 14 translucent quads |
| `gen_quads.py` | generates `include/quads.h` |
| `include/quads.h` | committed build-time output of `gen_quads.py` |
| `source/main.c` | ARM9 ROM: static scene; bottom screen blank |
| `Makefile` | devkitARM/libnds build (copied from `ds_vtxcolor_test`) |
| `expected.py` | A1/A2 expected tables, 5/8/6-bit blend families |
| `measure_alpha.py` | capture-PNG / native-frame classifier |
| `capture_mapping.json` | EQ-4c measured DS→capture mapping (reused) |
| `test_quads.py` | geometry / header / ROM-source consistency |
| `test_expected.py` | pinned expected values and rounding |
| `test_measure_alpha.py` | synthetic-capture classification tests |

The ROM links only `-lnds9`; no TWL-HD GX/`common/` code (asserted by
`test_quads.py`).

## 2. Scene and geometry

`quads.json` is shared by the ROM generator and the measurement tool. Geometry
(DS pixels, 256×192): black backdrop `0,0,128,192` 5-bit `0,0,0` id 1; grey
backdrop `128,0,128,192` 5-bit `16,16,16` id 2 (both alpha 31, z=0); seven white
`31,31,31` quads per half at `x=2+18k` (left) / `x=130+18k` (right), `y=32,
w=16, h=128`, alpha `4..28`, ids 3..9 / 10..16, z=64. The 2-pixel gaps and
top/bottom strips show the backdrop, and each quad has its own polygon id so
results never blend into each other.

## 3. State settings (and why)

- `videoSetMode(MODE_0_3D)`, `glInit()` — standard libnds 3D init.
- `glEnable(GL_BLEND)` — **the only** DISP3DCNT bit set.
- `glDisable(GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS | GL_OUTLINE |
  GL_FOG)` — no texture, alpha test, antialiasing, edge marking, fog.
  `GL_TOON_HIGHLIGHT` stays 0; lighting is per-polygon (`POLY_FORMAT_LIGHT0..3`
  stay 0), so no toon/highlight shading is active.
- `glPolyFmt(POLY_ALPHA(p->alpha) | POLY_CULL_NONE | POLY_ID(p->poly_id))`
  before every polygon — per-polygon alpha and id, no culling, no fog (never
  sets `POLY_FOG`).
- The POLYGON_ATTR depth-test bit is never set (value 0); `glInit` sets
  `GFX_CONTROL = 0`.
- Clear: `glClearColor(0,0,0,31)` (opaque black), `glClearPolyID(63)`,
  `glClearDepth(GL_MAX_DEPTH)`.

FACT — the `videoGL.h` DISP3DCNT and `POLY_ALPHA`/`POLY_ID` excerpts and the
`GFX_CONTROL = 0` grep are quoted in report §1.1.

**Depth/z pitfall:** backdrops are at z=0 and quads at z=+64; a larger source z
maps to a smaller post-transform z (backdrops `0x40000000`, quads `0x3f000000`),
nearer under LESS. Verified only in the emulator (report §5); the DS hardware
direction is UNVERIFIED.

## 4. Projection: vertex (x, y) → DS pixel (x, y)

Same as `tools/ds_vtxcolor_test/source/main.c`: `glViewport(0,0,255,191)`,
`glOrthof32(0, 256, 192, 0, -1<<12, 1<<12)`, raw `glVertex3v16` vertices. The
1:1 mapping is the libnds GL2D convention; UNVERIFIED on hardware until §9, and
the ≥4-DS-pixel measurement margin absorbs a ±1 px shift.

## 5. Build

```
$ cd tools/ds_alpha_test && make clean && make
```

FACT — raw output, the `sha256` and the `ndstool -i` excerpt are in the report
§4 and §5.2. The built `.nds` is not committed (`*.nds` is in `.gitignore`, like
the sibling ROMs).

## 6. `expected.py`

```
python3 tools/ds_alpha_test/expected.py
```

Expansion is `v8 = (v5<<3)|(v5>>2)` for both source and backdrop. Weights are
A1 = `a/31`, A2 = `(a+1)/32`. Five variants are printed:

- `5bit_round`, `5bit_floor`: blend to a 5-bit value then expand to 8 bits —
  the physically likely path (the DS colour buffer is 5-bit).
- `8bit_round`, `8bit_floor`: expand first (`S8=255`, `B8=expand5(B5)`), blend
  in 8 bits, round-half-up or floor.
- `6bit_round`: DeSmuME-style 6-bit pipeline — `expand6(v5) = (v5<<1)|(v5>>4)`,
  A2 blend `v6 = ((a+1)*S6 + (31-a)*B6) >> 5` (A1 is the counterfactual),
  then output `round_half_up(v6*255/63)`; a **HYPOTHESIS** for real hardware.

Rounding is always round-half-up (`floor(x+0.5)`), never Python banker's
rounding. Pins, weak/strong lists and the full table are in the report §2.

## 7. `measure_alpha.py`

```
python3 tools/ds_alpha_test/measure_alpha.py [--native] <capture.png>
```

- Default: a cc3dsfs capture; the top-left 400×240 region is analysed with
  `capture_mapping.json` (`scale 1.25, offset 40,0`). `--native` treats the
  image as the identity-mapped 256×192 DS frame.
- Each quad patch is the centre 8×8 DS pixels (a **4 DS-pixel margin**, clearing
  the 1.25× resampling smear); the mean of the mapped pixels is reported.
- One `key=value` line per key: per-quad measured / A1/A2 per variant / nearest,
  each variant's verdict, the best-fitting family (`best_*`,
  `quad_<name>_dev_best=`) and the overall verdict; deterministic.
- Backdrop probes are 8×8 at DS (64,8) and (192,8); a missing backdrop or quad
  patch makes the run `NOT-COMPARABLE`.
- Rule: a quad votes when `|E_A1 - E_A2| >= 3`; a round variant decides when all
  voters agree and all quads are within `TOL = 8`; the overall verdict is the
  best-fitting family (minimum summed `|measured - expected|`), or `UNDECIDED`.
  `TOL = 8` is not the discrimination gate (max A1/A2 separation over black is
  7); `floor` variants are sensitivity checks only.
- Exit status: 0 decided, 1 `UNDECIDED`/`NOT-COMPARABLE`, 2 usage/PIL/input.

## 8. Tests

```
make test-ds-alpha-test      # or: make test
```

Geometry consistency (bounds, ≥16×16, no overlap, gaps, distinct polygon ids,
regenerated `include/quads.h` byte-identical), hand-pinned expected values, the
synthetic A1/A2 and 6-bit classifications, the best-fit verdict logic, a
flat-grey negative control, an unmappable image, determinism, and `--native`.
PIL is optional: capture tests skip with a labelled reason when it is missing.

## 9. Hardware procedure (owner; NOT run in this task)

The complete six-step owner procedure is in the report §7 (build and record the
`sha256sum`, copy to `$SD_ROOT/roms/nds/`, run from TWiLight Menu++ in DS mode,
capture via the EQ-4b board with cc3dsfs and `cc_shots.sh`, analyse with
`measure_alpha.py`, record the raw report). The hardware answer is **NOT YET
MEASURED**.

## 10. Privacy

Every command uses `$HOME` / `$DEVKITPRO` / `$DEVKITARM` / `$SD_ROOT` /
`$CC3DSFS_BIN`; no literal home-directory path is added. Check after
`make clean` (built ELFs embed debug paths):

```
$ grep -rn -E '/(home|Users)/' tools/ds_alpha_test
(no output; exit status 1)
```

## 11. Limits and hypotheses

- **Projection / rasterisation (UNVERIFIED):** the 1:1 vertex→pixel mapping is
  the libnds GL2D convention; DS half-pixel/Y-flip details could shift a quad a
  pixel. The 4-DS-pixel margin and backdrop check absorb it.
- **Depth/z (EMULATOR-verified, hardware HYPOTHESIS):** backdrops at z=0 and
  quads at z=+64 place the quads in front under LESS in the DeSmuME harness
  (post-transform z quads `0x3f000000`, backdrops `0x40000000`). Whether DS
  hardware shares this source-z direction is UNVERIFIED; a wrong direction
  would hide the quads, which the backdrop check would not catch.
- **Weak alphas:** a = 16..28 have a best-case separation of ≤ 4 levels; only
  a = 4, 8, 12 are strong, so the verdict votes only on separations ≥ 3.
- **6-bit pipeline (HYPOTHESIS, not hardware):** DeSmuME's 3D engine keeps
  6-bit-per-channel BGR666 colour, so it blends in 6-bit units; `6bit_round`
  models `(v<<1)|(v>>4)` → `((a+1)*S6 + (31-a)*B6)>>5` → `round(v6*255/63)`.
  On the harness frame it is the best-fitting family (`best_total_dev=9`), with
  1-level residuals at L24/R16/R20; they are consistent with the prescribed
  `round()` versus a `(v6<<2)|(v6>>4)` output stage (HYPOTHESIS/UNVERIFIED —
  that harness output stage is not quoted in this round). It is **NOT known to
  be what real hardware does**; hardware is NOT YET MEASURED.
- **Capture noise:** the cc3dsfs path measured identity ±3 8-bit levels
  (`tools/ds_vtxcolor_test/README.md` §13). The flat `TOL = 8` absorbs that.
- Nothing in this task is a hardware FACT.

<!-- Requirement traceability lives in docs/reports/eq4h-alpha-convention-test.md section 8. -->
