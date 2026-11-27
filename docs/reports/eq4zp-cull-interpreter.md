# EQ-4zp — Culling in the host interpreter (opt-in)

Implements §3.4 of `docs/reports/eq4zo-render-differences.md`. Status: implemented
and host-tested; **not enabled** in the arm11 renderer (default behaviour is
unchanged, see §4).

## 1. What changed

- `common/twlhd_gx_interp.h/.c`: new `twlhd_gx_interpret_opts(..., flags)` and the
  flag `TWLHD_GX_OPT_CULL`. `twlhd_gx_interpret()` and `twlhd_gx_interpret_ex()`
  call it with flags 0, so existing callers (`arm11/source/main.c`) behave exactly
  as before. Two new stats fields: `culled_polygons`, `cull_undecided`.
- With the flag the interpreter latches POLYGON_ATTR bits 6-7 (applied at
  BEGIN_VTXS like alpha and light mask; default 3 when no POLYGON_ATTR was
  seen) and drops a polygon whose facing the value excludes. Rule, from the
  hardware probe (FACT, `docs/reports/eq4zl-zm-zn-probes-hw-result.md` §4):
  counter-clockwise on screen is the front face; value 3 draws both, 2 only front,
  1 only back, 0 neither.
- The facing is decided from the first three vertices of the polygon in assembly
  order after the divide by w (SOURCE-READ analogue: the DS decides per polygon;
  strips alternate winding, which the interpreter already compensates when it
  emits odd strip triangles; a quad strip uses the polygon order 0,1,3,2).
  Polygons with a vertex at `w <= 0` and zero-area polygons are drawn (not
  measured; the former counted in `cull_undecided`).
- `tests/host/gx_interp_cli.c`: `--cull`, adds `culled_polygons` and
  `cull_undecided` to the stats line only with that flag (the default output is
  byte-identical).
- `tools/hw/gen_cull_frames.py`: the v2 frame of the CULL probe (eight white
  quads, quad-list polygons).

## 2. Tests

- `tests/host/test_gx_interp.c`, eight new tests (all four attribute values for a
  front and a back triangle, off by default, no POLYGON_ATTR, quad list, strip
  alternation, quad strip order, `w <= 0`, degenerate polygon).
- `tools/test_hw_cull.py`: the probe frame through `gx_interp_cli --cull` gives
  DRAWN, CULLED, DRAWN, CULLED, CULLED, DRAWN, DRAWN, CULLED, equal to the hardware
  result and (with Pillow) to the classification of the committed capture
  `tools/fixtures/eq4_hw_probes/hw_cull_top400x240.png`; without `--cull` all eight
  quads are emitted (negative test), and the hardware result with the windings
  exchanged does not match (negative test).
- `make test` passes and `make arm11` still builds.

## 3. HYPOTHESIS / UNVERIFIED

- Only the probe (flat quads, identity matrices) was compared with hardware.
  Culling of lit or textured meshes, of polygons crossing the clip volume or with
  `w <= 0`, and of strips on hardware is not measured; the rules for them are the
  analogous ones, not measured facts.
- Hardware decides facing for strips and quad strips by its own vertex order;
  the interpreter's choice (strip triangles as emitted, quad strip 0,1,3) follows
  the DS vertex order but was not probed on hardware.

## 4. What is not done

The arm11 renderer does not pass the flag, so its picture is unchanged. Enabling
it is one call-site change in `arm11/source/main.c` (flag argument of the
interpret call). It should wait for a hardware comparison of a real scene (the
mitigation named in `eq4zo-render-differences.md` §3.4), because a wrong sign or a
frame whose POLYGON_ATTR cull bits are 0 (the early demo frames) would hide
geometry.
