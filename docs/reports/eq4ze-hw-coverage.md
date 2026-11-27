<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4ze — GX/GPU feature coverage matrix and ranked hardware plan

**Scope:** offline survey and planning only. No console was contacted, no ROM was
built or run, and no production source changed. Every hardware statement below is
a citation of an existing report, not a new measurement.

**Labels.** **FACT** = a quoted command with its raw output (path placeholders are
only `<logfile>` and `<worktree>`). **SOURCE-READ** = a claim about source or a
document, cited by `file:line`. **HYPOTHESIS** / **UNVERIFIED** = not measured by
any quoted output. Every comparison that does exist carries `basis=one console,
one capture`; nothing here claims universal hardware equality.

## 1. What this is, and the two hardware populations

The owner asked to widen the hardware comparison to textured scenes and
non-axis-aligned polygons "in recommended order". This report answers that with
one matrix (what is already known per GX/GPU feature) and one ranked list of the
next experiments (what is worth a console step next).

Two hardware populations appear below and are never mixed:

* **DS-mode** — DS software started from TWiLight Menu++ on a New 3DS and
  captured with the cc3dsfs board (or NTR). This is the only population that can
  show a **DS picture vs our picture** difference: EQ-4d, EQ-4f, EQ-4r, EQ-4i,
  EQ-4l, EQ-4w, EQ-4y, EQ-4za, EQ-4zd.
* **TWL-mode** — the arm11 renderer as a 3DS-mode app on the same console (Runs
  7-13, 18-21). It validates our render on real PICA200; it says nothing about
  the DS picture (`docs/EQUALITY_CHECK_DESIGN.md:78,210`). Cells below that cite
  only a TWL-mode run are therefore labelled UNVERIFIED for column (d).

Already covered, and therefore **not to be re-proposed** (details in §3):

| Area | Existing comparison | Verdict quote |
|---|---|---|
| Rotated textured polygons (scene 5, `STATIC_ANGLE=30`) | EQ-4f hardware vs DeSmuME harness and vs our PICA render | FACT `verdict=PASS` (`docs/reports/eq4f-scene5-static-hw-compare.md:292`), `ds.frac_within_flat=0.9986` (`:391`, PICA arm `:448`) — F9 |
| Rotated flat-colour polygons (cube, s1, s2, s3 at `STATIC_ANGLE=30`) | EQ-4r, twelve runs | FACT `MATCHED` for cube/s1/s2/s3, `DIFFERENT` for s4, `PARTIAL` for c2 (`docs/reports/eq4r-static-scenes-hw-compare.md:1905-1910`) — F9 |
| Textured scene 5 (8x8 I4 palette) | EQ-4f (geometry/colour), EQ-4e (colour census) | FACT `verdict=PASS` (F9); EQ-4e qualitative only, SOURCE-READ (`docs/reports/eq4e-scene5-hw-capture.md:22`) |
| Polygon alpha / blend enable | EQ-4i (convention), EQ-4w/4y (gate), EQ-4za/4zd (series) | FACT `g_channel_verdict=A2` (`docs/reports/eq4i-alpha-hw-result.md:110`); `quad1.measured=138`/`quad2.measured=41` (`docs/reports/eq4za-clear-black-hw-result.md:117,125`) — F11 |
| Screen-edge rows and 1-px geometry | EQ-4l hardware rows; hline/vline left UNMEASURED | FACT `UNMEASURED` (`docs/reports/eq4l-edge-rows-hw-result.md:296-297`) — F10 |

## 2. Evidence index (FACT)

**F1 — interpreter command coverage.** 37 command ids (`39` macro lines minus the
two mask macros), 37 `case` labels, and an unknown id is a hard stream error.

```
$ { grep -c '^#define TWLHD_V2_CMD_' common/twlhd_v2_format.h; grep -c 'case TWLHD_V2_CMD_' common/twlhd_gx_interp.c; grep -c '^#define TWLHD_V2_CMD_ID_MASK\|^#define TWLHD_V2_CMD_NONRAW_MASK' common/twlhd_v2_format.h; sed -n '1102,1103p' common/twlhd_gx_interp.c; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
39
37
2
    default:
        return TWLHD_GX_ERR_STREAM;
```

**F2 — `POLYGON_ATTR` consumers: alpha bits and light mask only.**

```
$ sed -n '917,923p;1072,1079p' common/twlhd_gx_interp.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
    case TWLHD_V2_CMD_POLYGON_ATTR:
        /* Latch only; BEGIN_VTXS copies pending -> applied (DeSmuME
         * gfx3d.cpp:2681, 2729-2731). Alpha = bits 16..20, light mask =
         * bits 0..3. */
        g->pending_alpha = (params[0] >> 16) & 0x1Fu;
        g->pending_light_mask = params[0] & 0xFu;
        break;
        g->applied_alpha = g->pending_alpha;
        g->applied_light_mask = g->pending_light_mask;
        g->rgba[3] = gx_alpha_float(g->applied_alpha);
        if (g->applied_alpha == 0u) {
            g->stats->alpha_wireframe_polys++;
        } else if (g->applied_alpha < 31u) {
            g->stats->alpha_translucent_polys++;
        }
```

**F3 — alpha weight `(a+1)/32`; alpha 0 counted but never drawn as an outline.**

```
$ sed -n '387,397p' common/twlhd_gx_interp.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
/*
 * DS raw alpha -> output vertex alpha.
 *
 * TO BE VERIFIED: DeSmuME's blend weight is (alpha+1)/32 (rasterize.cpp:490-494);
 * hardware/GBATEK were not checked, and alpha/31 is the alternative. Alpha 0
 * means wireframe on the DS: it is counted as alpha_wireframe_polys, but
 * outline-only drawing is NOT implemented (the polygon is emitted with 0.0).
 */
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}
```

**F4 — commands that are ignored or unsupported.**

```
$ sed -n '996,1000p;1052,1059p' common/twlhd_gx_interp.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
    case TWLHD_V2_CMD_BOX_TEST:
    case TWLHD_V2_CMD_POS_TEST:
    case TWLHD_V2_CMD_VEC_TEST:
        g->stats->ignored_no_effect++;
        break;
    case TWLHD_V2_CMD_VTX_DIFF:
        g->stats->unsupported++;
        if (g->in_begin) {
            /* VTX_DIFF inside a primitive corrupts assembly; callers must know
             * the resulting geometry is incomplete. */
            g->stats->geometry_incomplete++;
        }
        break;
```

**F5 — lighting and texture-state commands are latched (no GPU lighting).**

```
$ sed -n '930,943p;981,994p' common/twlhd_gx_interp.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
    case TWLHD_V2_CMD_DIF_AMB:
        g->diffuse = params[0] & 0xFFFFu;
        g->ambient = (params[0] >> 16) & 0xFFFFu;
        if ((params[0] & 0x8000u) != 0u) {
            /* Bit 15 sets the vertex colour to the diffuse RGB555. */
            set_rgba_from_rgb555(g, params[0] & 0x7FFFu);
        }
        break;

    case TWLHD_V2_CMD_SPE_EMI:
        g->specular = params[0] & 0xFFFFu;
        g->emission = (params[0] >> 16) & 0xFFFFu;
        break;

    case TWLHD_V2_CMD_TEX_COORD:
        /* Latch only; applied per vertex at VTX time (DeSmuME
         * gfx3d.cpp:1397-1414, transform at 1579-1611). */
        g->tex_s = sign_extend16(params[0]);
        g->tex_t = sign_extend16(params[0] >> 16);
        break;

    case TWLHD_V2_CMD_TEXIMAGE_PARAM:
        g->teximage_param = params[0];
        break;

    case TWLHD_V2_CMD_PLTT_BASE:
        g->pltt_base = params[0];
        break;
```

**F6 — the renderer's fixed per-frame state.** TexEnv is MODULATE only; depth is
one fixed test; culling is off.

```
$ sed -n '1332,1333p;1469,1472p;1542,1543p;1551,1552p' arm11/source/renderer.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
                C3D_TexSetWrap(tex, (GPU_TEXTURE_WRAP_PARAM)e->wrap_s,
                               (GPU_TEXTURE_WRAP_PARAM)e->wrap_t);
                C3D_TexSetFilter(tex, (GPU_TEXTURE_FILTER_PARAM)e->filter,
                                (GPU_TEXTURE_FILTER_PARAM)e->filter);
    C3D_RenderTargetClear(g_renderer.target, C3D_CLEAR_ALL,
                          TWLHD_RENDER_CLEAR_COLOR, 0);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
```

**F7 — all seven texture formats are decoded; format 0 is rejected.**

```
$ sed -n '56,60p;163p;190p;200,202p;228p;286p;296p' common/twlhd_tex_decode.c > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
    case 1u: return 32u;  /* A3I5 */
    case 2u: return 4u;   /* I2 */
    case 3u: return 16u;  /* I4 */
    case 4u: return 256u; /* I8 */
    case 6u: return 8u;   /* A5I3 */
    if (format == 0u) return TWLHD_TEX_ERR_FORMAT;
    case 1u: /* A3I5: bits 0-4 index, 5-7 alpha. texcache.cpp:1524-1525. */
    case 2u: /* I2: four indices per byte, low bits first. */
    case 3u: /* I4: two per byte, low nibble first. */
    case 4u: { /* I8: one per byte. */
    case 5u: { /* 4x4 compressed: texcache.cpp:1752, 1789-1843. */
    case 6u: /* A5I3: bits 0-2 index, 3-7 alpha. texcache.cpp:1745-1746. */
    default: /* 7: 16bpp. texcache.cpp:2007-2026. */
```

**F8 — the arm9v2 switches the plan can reuse.**

```
$ { grep -n '^AUTODUMP_FRAME\|^SCENE_STRIPPED\|^CUBES_STRIPPED\|^STATIC_ANGLE_STRIPPED\|^BLEND_STRIPPED\|^DIAGSCREEN_STRIPPED\|^VP_X1_STRIPPED' arm9v2/Makefile; grep -n '^SCENE_OK\|^CUBES_OK\|^STATIC_ANGLE_OK\|^BLEND_OK' arm9v2/Makefile; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
60:AUTODUMP_FRAME	?=	0
61:AUTODUMP_FRAME_STRIPPED	:=	$(strip $(AUTODUMP_FRAME))
63:AUTODUMP_FRAME_STRIPPED	:=	0
66:AUTODUMP_FRAME_OK := $(shell echo '$(AUTODUMP_FRAME_STRIPPED)' | grep -Eq '^[1-9][0-9]*$$' && test $(AUTODUMP_FRAME_STRIPPED) -le 65535 && echo ok)
84:VP_X1_STRIPPED	:=	$(strip $(VP_X1))
116:CUBES_STRIPPED	:=	$(strip $(CUBES))
139:SCENE_STRIPPED	:=	$(strip $(SCENE))
162:BLEND_STRIPPED	:=	$(strip $(BLEND))
186:STATIC_ANGLE_STRIPPED	:=	$(strip $(STATIC_ANGLE))
206:DIAGSCREEN_STRIPPED	:=	$(strip $(DIAGSCREEN))
208:DIAGSCREEN_STRIPPED	:=	0
118:CUBES_OK := $(shell echo '$(CUBES_STRIPPED)' | grep -Eq '^[1-9][0-9]*$$' && test $(CUBES_STRIPPED) -le 170 && echo ok)
141:SCENE_OK := $(shell echo '$(SCENE_STRIPPED)' | grep -Eq '^[0-5]$$' && echo ok)
164:BLEND_OK := $(shell echo '$(BLEND_STRIPPED)' | grep -Eq '^1$$' && echo ok)
188:STATIC_ANGLE_OK := $(shell echo '$(STATIC_ANGLE_STRIPPED)' | grep -Eq '^(0|[1-9][0-9]*)$$' && test $(STATIC_ANGLE_STRIPPED) -le 359 && echo ok)
```

**F9 — DS-mode verdict lines (EQ-4f, EQ-4r).** Column (d) rows 1, 2, 5, 9 quote these.

```
$ { sed -n '292p;391p;448p' docs/reports/eq4f-scene5-static-hw-compare.md; sed -n '1905,1910p;1935p' docs/reports/eq4r-static-scenes-hw-compare.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
verdict=PASS
ds.frac_within_flat=0.9986
verdict=PASS
| cube | 2.2378/1.1111/2.1537, 0.9996, 2/2/1/1, PASS | 14.1100/14.1046/20.8115, 0.9955, 1/1/1/0, PASS | MATCHED |
| s1 | 0.7148/0.0000/0.0000, 0.9993, 1/3/1/1, PASS | 15.9869/15.8177/23.7266, 0.9872, 0/1/0/0, PASS | MATCHED |
| s2 | 0.3747/0.0000/0.0000, 0.9987, 1/1/1/2, PASS | 15.4060/15.2484/22.8726, 1.0000, 2/0/0/1, PASS | MATCHED |
| s3 | 0.3790/0.0000/0.0000, 0.9987, 0/1/1/2, PASS | 15.4110/15.2484/22.8726, 1.0000, 1/0/0/1, PASS | MATCHED |
| s4 | 0.9739/0.8608/1.0802, 1.0000, 2/**10**/3/0, **FAIL** | 15.9668/16.7612/26.9291, 1.0000, 1/**10**/**4**/2, **FAIL** | DIFFERENT |
| c2 | no reference, **NOT-COMPARABLE** (tool exit 2) | 16.0627/16.0865/24.0431, 1.0000, 1/1/1/1, PASS | PARTIAL |
**DIFFERENT (s4): the hardware draws the alpha-0 quad's outline.** The capture
```

**F10 — screen-edge verdict lines (EQ-4l).** Column (c)/(d) rows 1 and 6 and §4 experiment 1 quote these.

```
$ sed -n '262,263p;296,297p;322p;334,336p' docs/reports/eq4l-edge-rows-hw-result.md > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
item=hline nominal_rows=96-96 nominal_cols=64-191 measured_rows=96-97 measured_cols=64-191 row_diff=0 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=2
item=vline nominal_rows=64-159 nominal_cols=208-208 measured_rows=65-160 measured_cols=208-208 row_diff=1 row_end_diff=1 col_diff=0 col_end_diff=0 row_count=96
| hline (1-px high) | **UNMEASURED** — skipped, `plateau contrast 0.00 below 10.00` / `interior band falls outside the capture` | `measured_rows=96-97` (2 rows), `row_end_diff=1` |
| vline (1-px wide) | **UNMEASURED** — skipped, same reasons | `measured_rows=65-160`, `row_diff=1` |
- **Every measurable hardware edge is at nominal within about 0.1 DS px.**
- **The hardware rasterises the vertex rectangle as the half-open `[y0,y1)`,
  while DeSmuME adds one row on horizontal edges (`Ceil16_16`).** That
  per-edge `+1` in DeSmuME — not a hardware shift — is the likely cause of the
```

**F11 — alpha/blend verdict lines (EQ-4i, EQ-4w, EQ-4y, EQ-4za, EQ-4zd).** Column (c)/(d) row 4 and §1 quote these.

```
$ { sed -n '97p;110p' docs/reports/eq4i-alpha-hw-result.md; sed -n '132p;140p' docs/reports/eq4w-blend-hw-result.md; sed -n '218p;226p' docs/reports/eq4y-gate-hw-result.md; sed -n '117p;125p' docs/reports/eq4za-clear-black-hw-result.md; sed -n '333p' docs/reports/eq4zd-alpha-sweep-hw-result.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
same `g_votes_A1=0 g_votes_A2=10 g_votes_ambiguous=4 g_channel_verdict=A2`
g_channel_verdict=A2
quad1.majority_rgb=0,134,0
quad2.majority_rgb=0,0,33
quad1.measured=142
quad2.measured=57
quad1.measured=138
quad2.measured=41
**All six source-derived candidates are excluded** by the 11-point series: each misses the hardware by at least 4 green / 8 blue levels somewhere. In particular the candidates that matched the emulator exactly (`exact_float_round` and `alpha8_round_round` per the EQ-4zc report's own list, `docs/reports/eq4zc-alpha-sweep.md:353-394`; `alpha8_round_floor` also equals the emulator series at these 11 alphas) are 135/135 at alpha 16 where the hardware is 138/140, so the emulator match does not carry over to the console. The candidates that really miss at alpha 1 are `exact_float_floor`, `alpha8_trunc_floor` and `alpha8_trunc_round` (15 where the hardware is 16). **The DS convention is also excluded** (5 green / 9 blue): the hardware is uniformly above it, not equal to it. No model was fitted and no candidate was tuned; these are the six library functions called as written.
```

**F12 — cross-check report lines (EQ-3, PHASE5B, PHASE5A).** Column (c) rows 2, 7-13, 18 and 27 quote these.

```
$ { sed -n '199p' docs/EQ3_COMPARE.md; sed -n '844p;852p;96p;98p' docs/PHASE5B_PLAN.md; sed -n '67p' docs/PHASE5A_V2_RENDERER_DESIGN.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
**colour 4 per channel (tol 8)**. Result: **PASS, exit 0**. No tolerance was
**STATUS (2026-11-12): implemented BUILD/UNIT; EMULATOR (Azahar) and HARDWARE OBSERVED (Run 18) for the colour path.**
red light/emission/specular/two lights; unlit control) match the host interpreter's prediction within 1 level; Azahar: deviation 0.
**HARDWARE OBSERVED (Run 20, 2026-11-14 16:11-16:15, owner-approved, assistant-executed
- Multi-format frame (fmt 1 A3I5, 2 I2, 3 I4, 4 I8, 5 4x4 with block modes 0-3,
| 5A-3 | Hardware run procedure (new owner approval required) | HARDWARE | **Met for one frame (Run 7, HARDWARE OBSERVED), for one non-default DS viewport (Run 8, HARDWARE OBSERVED), for sequence playback (Run 9, HARDWARE OBSERVED), for large frames up to 6120 vertices (Run 10, HARDWARE OBSERVED), for a 5-minute run of the large-frame sequence (Run 11, HARDWARE OBSERVED), for a 15-minute run of the large-frame sequence (Run 12, HARDWARE OBSERVED), and for other primitive types (triangle list type 0, triangle strip type 2, quad strip type 3) (Run 13, HARDWARE OBSERVED):** the cube rendered from a 36-vertex v2 frame with no GPU hang; a (32,96)-(159,191) viewport placed it as the oracle/emulator predict (photo estimate); one 8-frame sequence with per-frame-changing data ran ~67 s (25 passes, ~200 re-uploads) with no GPU hang; 100/170-cube frames (3600/6120 vertices) rendered with no GPU hang, including alternating 3600/6120-vertex re-uploads over ~35 s; the same alternating re-upload ran 5 min 05 s (251 loops, ~500 uploads) with no GPU hang or visible degradation; it ran 15 min 24 s (756 loops, ~1500 uploads) at a constant rate with no GPU hang or visible degradation; and triangle-list (24 vertices, 8 tris), triangle-strip (30 output vertices, 10 tris) and quad-strip (30 output vertices, 10 tris) frames loaded and drew with the expected outline and vertex order within ~3–5 px. Not covered: DS-side primitive semantics (strip winding, quad vertex order, DS quad colour interpolation), mixed primitives in one frame, strips longer than 12 vertices, runs longer than ~15 min with large frames, other captures, the DS-side viewport +1/y1-wrap (not testable on the 3DS), viewports with y1 > 191, 5B |
```

**F13 — arm9v2 BLEND switch block, the model for the §4 diff proposals.** Proposal blocks below copy this full conditional shape (validation `_OK` line, `ifneq`/`ifeq`/`$(error)`/`endif` wrap); the file is not changed.
```
$ sed -n '162,173p' arm9v2/Makefile > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
BLEND_STRIPPED	:=	$(strip $(BLEND))
ifneq ($(BLEND_STRIPPED),)
BLEND_OK := $(shell echo '$(BLEND_STRIPPED)' | grep -Eq '^1$$' && echo ok)
ifeq ($(BLEND_OK),)
$(error BLEND must be empty or 1 (got: $(BLEND)))
endif
TARGET		:=	$(TARGET)-bl
BUILD		:=	$(BUILD)-bl
CFLAGS		+=	-DTWLHD_DEMO_BLEND=1
# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
CXXFLAGS	+=	-DTWLHD_DEMO_BLEND=1
endif
```

**F14 — EQ-4d vertex-colour verdict and measurement (DS mode).** Column (d) row 3 and §4 experiment 6 quote these.
```
$ sed -n '116,124p;136,141p' docs/reports/eq4d-ds-vertex-colour-hw.md > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
$ python3 tools/ds_vtxcolor_test/measure_vtxcolor.py tools/ds_vtxcolor_test/fixtures/vt1b_2_top400x240.png
geometry_found=1
qA_rgb=255,255,255
qB_rgb=255,255,255
qC_rgb=0,0,0
qD_rgb=132,134,132
qE_rgb=255,0,0
bg_rgb=66,69,66
default_colour_class=same_as_qB
- **FACT:** on this capture, quad A (no colour command) measures
  (255,255,255), identical to the explicit white quad B; C black, D grey, E
  red.
- **HYPOTHESIS:** the initial current vertex colour of this DS hardware path
  (after the loader/TWiLight start, first frame, 3D cleared) is **white**,
  agreeing with DeSmuME and disagreeing with melonDS.
```

## 3. Coverage matrix

Column (a) is `common/twlhd_gx_interp.c`; (b) is the arm11 render path
(`arm11/source/renderer.c` or the module named); (c) is the emulator/cross-check
arm already on record outside the DS comparison (ours vs DeSmuME, a PICA-only
hardware run, or a source-vs-DeSmuME study); (d) is the **DS-mode** comparison;
(e) is the risk for common DS software; (f) is the cost of a hardware test. "TWL-resolved" is shorthand for a
run that used the arm11 renderer on the console in 3DS mode. Every (a)-(d) cell
carries its own label. The (e) column is **HYPOTHESIS** by construction (a
frequency judgement, not a measurement) and the (f) column is **HYPOTHESIS** plan
text except where it says FACT/SOURCE-READ and cites F8 or an existing fixture.

| # | Feature | (a) Interpreter | (b) arm11 render | (c) Emulator/cross-check arm (which report) | (d) DS-mode comparison | (e) Risk | (f) HW-test cost |
|---|---|---|---|---|---|---|---|
| 1 | Flat-colour polygons, axis-aligned | SOURCE-READ supported: primitive assembly `common/twlhd_gx_interp.c:636-655` (quads split to triangles `:684-703`) | SOURCE-READ `C3D_DrawArrays(GPU_TRIANGLES, ...)` `arm11/source/renderer.c:1480-1481` | FACT `item=hline ... row_count=2` / `item=vline ... row_count=96` `docs/reports/eq4l-edge-rows-hw-result.md:262-263` (F10) | FACT `- **Every measurable hardware edge is at nominal within about 0.1 DS px.**` `docs/reports/eq4l-edge-rows-hw-result.md:322` (F10) | LOW — fill works on DS within 0.1 DS px | SOURCE-READ no new ROM: `tools/ds_edge_test/quads.json:29,31` and `tools/ds_edge_test/fixtures/ed1_3_top400x240.png` exist |
| 2 | Rotated (non-axis-aligned) polygons | SOURCE-READ supported: 4x4/4x3/3x3 matrix commands `common/twlhd_gx_interp.c:823-909` | SOURCE-READ vertex transform in the clip shader (`arm11/source/renderer.c:1313-1314`) | FACT `**colour 4 per channel (tol 8)**. Result: **PASS, exit 0**.` `docs/EQ3_COMPARE.md:199` (F12) | FACT `MATCHED` cube/s1/s2/s3, `DIFFERENT` s4, `PARTIAL` c2 at st30 `docs/reports/eq4r-static-scenes-hw-compare.md:1905-1910` (F9) | LOW — six static scenes captured: four `MATCHED` inside 3 DS px, s4 `DIFFERENT` (alpha-0 outline), c2 `PARTIAL` (no reference) | FACT covered (EQ-4r, F9); do not repeat |
| 3 | Vertex colours (gouraud) | SOURCE-READ supported: `COLOR` latches per vertex `common/twlhd_gx_interp.c:913-915` | SOURCE-READ per-vertex rgba attribute `arm11/source/renderer.c:1534-1535` | FACT s1 `MATCHED` row `docs/reports/eq4r-static-scenes-hw-compare.md:1906` (F9) | FACT `**FACT:** on this capture, quad A (no colour command) measures (255,255,255), identical to the explicit white quad B; C black, D grey, E red.` `docs/reports/eq4d-ds-vertex-colour-hw.md:136-141` (F14); HYPOTHESIS initial current vertex colour white (`:139-141`) and interior gradient not sampled (F14: one median per quad) | MEDIUM — per-vertex values match, interpolation untested | new `GRAD=1` switch + 1 capture |
| 4 | Polygon alpha + blend enable | SOURCE-READ alpha latch `common/twlhd_gx_interp.c:917-923` and weight `:395-397`; blend enable rides in RSTA and is not read by the interpreter (`docs/reports/eq4u-scene4-alpha-why.md:45-48`) | SOURCE-READ blend gate `arm11/source/renderer.c:1573`, `arm11/include/twlhd_blend_gate.h` | FACT `g_channel_verdict=A2` `docs/reports/eq4i-alpha-hw-result.md:97,110` (F11) | FACT `g_channel_verdict=A2` (`eq4i:110`); `quad1.majority_rgb=0,134,0`/`quad2.majority_rgb=0,0,33` (`eq4w:132,140`); `quad1.measured=142`/`quad2.measured=57` (`eq4y:218,226`); `quad1.measured=138`/`quad2.measured=41` (`eq4za:117,125`); `**All six source-derived candidates are excluded**` (`eq4zd:333`) — F11 | MEDIUM — gate works; 4/8-level arithmetic residual | FACT covered (EQ-4v/4w/4x/4y/4z/4zc/4zd), F11 |
| 5 | Polygon alpha 0 wireframe / outline | SOURCE-READ counted only `common/twlhd_gx_interp.c:387-393`, `:1072-1079` (F2/F3) | UNVERIFIED no outline pass; alpha-0 fragments hidden on the opaque path (`docs/reports/eq4x-blend-gate.md:1302`) | HYPOTHESIS none | FACT `**DIFFERENT (s4): the hardware draws the alpha-0 quad's outline.**` `docs/reports/eq4r-static-scenes-hw-compare.md:1935`; s4 row `**FAIL**` / `DIFFERENT` `:1909` (F9) | HIGH — wireframe/outline ROMs lose that geometry | FACT covered (EQ-4r s4, F9) |
| 6 | 1-px lines / wireframe / outline edges | SOURCE-READ supported as degenerate quads: `hline`/`vline` in `tools/ds_edge_test/quads.json:29,31`; no line primitive exists in GX | SOURCE-READ rasterised as triangles; the DS fill rule is UNVERIFIED, no PICA fill-rule study exists (`docs/reports/eq4k-edge-rows.md:530-531`) | FACT `item=hline ... row_count=2` / `item=vline ... row_count=96` `docs/reports/eq4l-edge-rows-hw-result.md:262-263` (F10) | FACT `**UNMEASURED** — skipped` for hline and vline `docs/reports/eq4l-edge-rows-hw-result.md:296-297`; bands `Every measurable hardware edge is at nominal within about 0.1 DS px.` `:322` (F10) | MEDIUM — 1-px UI lines; PICA vs DS fill rule untested | SOURCE-READ no new ROM: `tools/ds_edge_test/quads.json:29,31`; offline re-measure, fallback 1 capture |
| 7 | Texture format A3I5 | FACT decoded `common/twlhd_tex_decode.c:190` (F7) | SOURCE-READ upload + bind path `common/twlhd_pica_frame.c:345-346`, `arm11/source/renderer.c:1469-1470` | FACT PICA-only full-alpha texels `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode; only the PICA arm exists | MEDIUM — translucent texel alpha untested | new `TEXFMT=1` switch + 1 capture |
| 8 | Texture format 4-colour (I2) | FACT decoded `common/twlhd_tex_decode.c:200` (F7) | SOURCE-READ same bind path | FACT PICA-only `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode | LOW — rare format | same `TEXFMT` switch |
| 9 | Texture format 16-colour (I4) | FACT decoded `common/twlhd_tex_decode.c:201` (F7) | SOURCE-READ same bind path | FACT PICA-only, 16x16 I4 `docs/PHASE5B_PLAN.md:844-852` (F12) | FACT `verdict=PASS` 8x8 I4 scene5 `docs/reports/eq4f-scene5-static-hw-compare.md:292` (F9) | LOW — the one format already matched on DS | extend `TEXFMT` with a 16x16 I4 quad |
| 10 | Texture format 256-colour (I8) | FACT decoded `common/twlhd_tex_decode.c:202-226` (F7) | SOURCE-READ same bind path | FACT PICA-only `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode | MEDIUM — common for sprites/tiles | same `TEXFMT` switch |
| 11 | Texture format 4x4 compressed | FACT decoded `common/twlhd_tex_decode.c:228-284` (F7) | SOURCE-READ same bind path | FACT PICA-only, block modes 0-3 `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode | MEDIUM — block offsets/modes complex | same `TEXFMT` switch |
| 12 | Texture format A5I3 | FACT decoded `common/twlhd_tex_decode.c:286-294` (F7) | SOURCE-READ same bind path | FACT PICA-only `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode | MEDIUM — translucent texel alpha | same `TEXFMT` switch |
| 13 | Texture format direct 16-bit | FACT decoded `common/twlhd_tex_decode.c:296-300` (F7) | SOURCE-READ same bind path | FACT PICA-only `docs/PHASE5B_PLAN.md:844-852` (F12) | UNVERIFIED DS mode | MEDIUM — most common format, DS-mode never compared | same `TEXFMT` switch |
| 14 | Texture wrap / repeat / flip | SOURCE-READ mapping `common/twlhd_pica_tex.c:102-125` (repeat+flip -> mirrored, flip alone -> clamp) | FACT `C3D_TexSetWrap` `arm11/source/renderer.c:1469-1470` (F6) | SOURCE-READ PICA-only all eight quads pass (`docs/EMULATOR_VALIDATION.md:776-788`) | UNVERIFIED DS mode; Run 21 was TWL-resolved | MEDIUM — tiled surfaces rely on repeat | new `TEXWRAP=1` switch + 1 capture |
| 15 | Texture filtering / magnification | UNVERIFIED none: DS GX exposes no min/mag filter; sampler defaults UNVERIFIED `docs/DS_GX_RESEARCH.md:732` | SOURCE-READ forced `NEAREST` `common/twlhd_tex_draw_plan.c:99` | UNVERIFIED none | UNVERIFIED none | LOW — nearest is the DS model, no state to diverge | SOURCE-READ no new state to compare (`common/twlhd_tex_draw_plan.c:99`); HYPOTHESIS none needed |
| 16 | Texture transform / texcoord modes | SOURCE-READ all four modes `common/twlhd_gx_interp.c:715-784` | SOURCE-READ uses the computed s/t per vertex | SOURCE-READ unit golden only; no DS/DeSmuME comparison | UNVERIFIED modes 2/3 explicitly not shown on hardware `docs/PHASE5B_PLAN.md:873-874` | MEDIUM — projected/reflection textures | new `TEXGEN=2` switch + 1 capture |
| 17 | TexEnv: modulate / decal / toon / highlight | UNVERIFIED none: `POLYGON_ATTR` mode bits 4-5 are not latched (F2; `docs/DS_GX_RESEARCH.md:26`) | SOURCE-READ MODULATE only `arm11/source/renderer.c:1332-1333` (F6) | UNVERIFIED none | UNVERIFIED none | HIGH — DECAL/TOON-ROMs differ by construction | new `TEXPOLY` switch + renderer work |
| 18 | Lighting (diffuse / specular / emission) | SOURCE-READ supported `common/twlhd_gx_interp.c:930-943` (F5), lights/shininess `:944-979` | SOURCE-READ per-vertex colours only; no GPU lighting | FACT PICA-only, `match the host interpreter's prediction within 1 level` `docs/PHASE5B_PLAN.md:96-100` (F12) | UNVERIFIED DS mode; Run 18 was TWL-resolved | MEDIUM — DeSmuME-derived equations, DS never measured | new `LIT=1` switch + 1 capture |
| 19 | Fog | UNVERIFIED none: fog registers ride in RSTA, never read (`docs/reports/eq4u-scene4-alpha-why.md:45-48`); `docs/EQUALITY_CHECK_DESIGN.md:222` | UNVERIFIED none | UNVERIFIED none | UNVERIFIED none | HIGH visual — most 3D titles fog | ROM + renderer work; deferred |
| 20 | Edge marking | UNVERIFIED none (`docs/EQUALITY_CHECK_DESIGN.md:223`) | UNVERIFIED none | UNVERIFIED none | UNVERIFIED none | MEDIUM-HIGH — stylish 3D relies on it | ROM + renderer work; deferred |
| 21 | Antialiasing (edge blend) | UNVERIFIED none (`docs/EQUALITY_CHECK_DESIGN.md:223`) | UNVERIFIED none; PICA MSAA not enabled | UNVERIFIED none | UNVERIFIED none | MEDIUM — edges differ sub-pixel only | ROM + renderer work; deferred |
| 22 | Toon shading | UNVERIFIED none: mode bits 4-5 not latched (F2) | UNVERIFIED none | UNVERIFIED none | UNVERIFIED none | MEDIUM — cel-shaded titles | ROM + renderer work; deferred |
| 23 | z-buffer vs w-buffer | UNVERIFIED n/a (rasteriser) | SOURCE-READ z-buffer `GPU_GREATER` `arm11/source/renderer.c:1551` (F6) | HYPOTHESIS may differ `docs/EQUALITY_CHECK_DESIGN.md:126,220` | UNVERIFIED none | MEDIUM — ordering mostly preserved; coplanar cases differ | new `DEPTH=1` switch + 1 capture |
| 24 | Depth-test modes (EQUAL / LESS) | SOURCE-READ bit 14 `DepthEqualTest_Enable` is not latched (F2; `docs/DS_GX_RESEARCH.md:33`) | SOURCE-READ one fixed test `arm11/source/renderer.c:1551` (F6) | UNVERIFIED none | UNVERIFIED none | HIGH — EQUAL is used for coplanar decals | new `DEPTH=1` switch + renderer work |
| 25 | Culling | SOURCE-READ `SurfaceCullingMode` bits 6-7 are not latched (F2; `docs/DS_GX_RESEARCH.md:38-40`) | SOURCE-READ `C3D_CullFace(GPU_CULL_NONE)` `arm11/source/renderer.c:1552` (F6) | SOURCE-READ cull settled on TWL only `docs/EQUALITY_CHECK_DESIGN.md:210` | UNVERIFIED DS mode; `docs/reports/5c4-black-screen-analysis.md:442-462` is TWL-resolved | MEDIUM — back faces drawn; visible with transparency | FACT `DIAGSCREEN=9 STATIC_ANGLE=30` exists (F8) + 1 capture |
| 26 | Polygon ID / shadow | SOURCE-READ `PolygonID` bits 24-29 are not latched (F2; `docs/DS_GX_RESEARCH.md:36`) | UNVERIFIED none | UNVERIFIED none | UNVERIFIED none | HIGH — shadow-volume ROMs draw shadows visibly | ROM + renderer work; deferred |
| 27 | Viewport and clipping (partial viewport, near/far) | SOURCE-READ `VIEWPORT` latched `common/twlhd_gx_interp.c:1094-1100`; clipfix uses it `common/twlhd_clipfix.c:18-40` | SOURCE-READ clipfix scales into the viewport, no PICA scissor `docs/PHASE5A_V2_RENDERER_DESIGN.md:40,48-49` | FACT non-default viewport on PICA (Run 8) `docs/PHASE5A_V2_RENDERER_DESIGN.md:67` (F12) | UNVERIFIED DS mode; no partial-viewport DS capture exists | MEDIUM — split-screen/HUD viewports are common | FACT `VP_X1..Y2` exist (F8) + 1 capture |
| 28 | Display-list / command coverage | FACT 37/37 ids have a case; unknown id -> `TWLHD_GX_ERR_STREAM` (F1); ignored `POS/BOX/VEC_TEST` and unsupported `VTX_DIFF` (F4) | UNVERIFIED n/a | SOURCE-READ v2 parse/write/port tests (`make test`, `tools/test_twlhd_v2.py:2`) | UNVERIFIED n/a (not a picture feature) | LOW-MEDIUM — `VTX_DIFF` is rare; raw-FIFO entries are skipped (`common/twlhd_gx_interp.c:1186-1187`) | SOURCE-READ none: command coverage quoted (F1/F4); `make test` exists (`tools/test_twlhd_v2.py:2`) |
| 29 | Multiple screens / capture modes | UNVERIFIED n/a | SOURCE-READ top screen only `arm11/source/renderer.c:764`; bottom is a console `arm11/source/main.c:506` | UNVERIFIED none | UNVERIFIED none | MEDIUM — titles that use 3D on both screens lose the lower view | renderer work + capture; deferred |

## 4. Ranked plan (next 5-8 experiments)

Ranking = value (how common the feature is in DS software x how likely our render
deviates) / cost (new ROM switch? existing tool? console steps). Every experiment
lists the exact ROM variant, the capture, the pass criterion with an existing
tool, the effort, and the risk of finding a deviation. Effort counts ROM builds
plus console steps for the owner (upload + one capture session).

### 1. 1-px line and wireframe fill rule (recommended first)

* **ROM variant:** none new. `tools/ds_edge_test/` already draws the `hline`
  (64,96,128x1) and `vline` (208,64,1x96) quads (`quads.json:29,31`); the
  committed fixture `tools/ds_edge_test/fixtures/ed1_3_top400x240.png` is the
  capture of record.
* **Capture:** re-analyse that fixture. Only if the 1.25x resampling still
  defeats it, take one new capture of the same ROM.
* **Pass criterion:** white run length 1 and the run start at the nominal
  row/column within 1 DS px, using `tools/ds_edge_test/measure_rows.py` with a
  row-run probe (text-only diff; the existing edge fitter skips these two items,
  `docs/reports/eq4l-edge-rows-hw-result.md:296-297`).

Proposal, not applied — row-run probe in `measure_rows.py`:
```diff
--- a/tools/ds_edge_test/measure_rows.py
+++ b/tools/ds_edge_test/measure_rows.py
@@
+        elif arg == "--row-runs":
+            row_runs = True   # run start/length for hline/vline
```

* **Effort:** 0 ROM builds, ~1 offline run; fallback 1 owner capture session.
* **Deviation risk:** HYPOTHESIS — DeSmuME, not hardware, adds one row on
  horizontal edges (`docs/reports/eq4l-edge-rows-hw-result.md:334-336`, F10);
  hardware edges are nominal (F10) and PICA's fill rule on 1-px geometry is
  untested.
* **Why first:** it closes a gap the edge report names explicitly, with no new
  ROM and no console step if the fixture resolves it.

**Result (EQ-4zf):** the row-run probe and the hardware/DeSmuME/our-render comparison are in `docs/reports/eq4zf-1px-lines.md`.

### 2. DS-mode lighting

* **ROM variant:** new `LIT=1` switch (text-only diff: one Makefile block plus a
  lit-quad draw path in `arm9v2/source/demo_frame.cpp`), reusing the four-normal
  layout and the colours of `tools/hw/gen_lit_frames.py` so the existing
  predictor applies. Build the plain and `AUTODUMP_FRAME` twins.

Proposal, not applied — `LIT` switch (F13 shape):
```diff
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@
+LIT_STRIPPED := $(strip $(LIT))
+ifneq ($(LIT_STRIPPED),)
+LIT_OK := $(shell echo '$(LIT_STRIPPED)' | grep -Eq '^1$$' && echo ok)
+ifeq ($(LIT_OK),)
+$(error LIT must be empty or 1 (got: $(LIT)))
+endif
+TARGET := $(TARGET)-lit
+BUILD := $(BUILD)-lit
+CFLAGS += -DTWLHD_DEMO_LIT=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS += -DTWLHD_DEMO_LIT=1
+endif
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@
+#ifdef TWLHD_DEMO_LIT
+    draw_lit_quad();   /* four normals per tools/hw/gen_lit_frames.py */
+#endif
```

* **Capture:** top 400x240 cc3dsfs crop of the plain static ROM.
* **Pass criterion:** `tools/hw/compare_ppm.py` against the JSON from
  `tools/hw/predict_lit.py`; every quad within tolerance 12.
* **Effort:** 2 ROM builds, 1 DeSmuME harness run, 1 owner upload + capture.
* **Deviation risk:** MEDIUM — the equations are DeSmuME-derived and only the
  PICA arm (Run 18) was measured, never the DS.

**Implemented (2026-11-26):** EQ-4zh added the `LIT=1/2` switch and the host-side proof; the console capture is still PLAN; see `docs/reports/eq4zh-lit-rom.md`.

### 3. Partial viewport

* **ROM variant:** existing switches, no new code:
  `make -C arm9v2 SCENE=4 VP_X1=32 VP_Y1=96 VP_X2=159 VP_Y2=191 STATIC_ANGLE=30`
  plus the `AUTODUMP_FRAME` twin for the harness reference (`arm9v2/Makefile:84-107`).
* **Capture:** one top 400x240 crop.
* **Pass criterion:** `tools/eq4_hw_compare.py` against the harness reference:
  `ds.frac_within_flat >= 0.95`, every `bbox_dev <= 3`, and the non-background
  region confined to x 32..159 / y 96..191.
* **Effort:** 2 ROM builds, 1 harness run, 1 capture.
* **Deviation risk:** HIGH — our render scales geometry into the viewport but
  does not scissor, so geometry outside it can bleed (design note
  `docs/PHASE5A_V2_RENDERER_DESIGN.md:48-49`).

**Result (EQ-4zg):** the partial-viewport hardware/DeSmuME/our-render comparison is in `docs/reports/eq4zg-partial-viewport.md`.

### 4. DS-mode texture formats

* **ROM variant:** new `TEXFMT=1` switch (text-only diff: Makefile block plus an
  eight-quad draw path) reproducing the grid of
  `tools/gen_tex_format_frames.py` (A3I5, I2, I4, I8, 4x4 modes 0-3, A5I3,
  16bpp, 16x16 I4).

Proposal, not applied — `TEXFMT` switch (F13 shape):
```diff
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@
+TEXFMT_STRIPPED := $(strip $(TEXFMT))
+ifneq ($(TEXFMT_STRIPPED),)
+TEXFMT_OK := $(shell echo '$(TEXFMT_STRIPPED)' | grep -Eq '^1$$' && echo ok)
+ifeq ($(TEXFMT_OK),)
+$(error TEXFMT must be empty or 1 (got: $(TEXFMT)))
+endif
+TARGET := $(TARGET)-texfmt
+BUILD := $(BUILD)-texfmt
+CFLAGS += -DTWLHD_DEMO_TEXFMT=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS += -DTWLHD_DEMO_TEXFMT=1
+endif
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@
+#ifdef TWLHD_DEMO_TEXFMT
+    draw_tex_format_grid();   /* grid per tools/gen_tex_format_frames.py */
+#endif
```

* **Capture:** one top 400x240 crop.
* **Pass criterion:** `tools/check_tex_formats.py` at its default tolerance 28
  (cross-checked with `tools/measure_tex_formats_deviation.py`).
* **Effort:** 1 switch, 1 ROM build, 1 capture; the oracle already exists.
* **Deviation risk:** MEDIUM — the decoder is source-derived and I4 already
  matches on DS, but six formats have never been compared on DS.

### 5. Texture + vertex colour MODULATE

* **ROM variant:** new `TEXMOD=1` switch: scene 5's textured quad with four
  different vertex colours (e.g. red/green/blue/white) instead of white — a
  text-only diff of `draw_scene5_textured_quad` (`arm9v2/source/demo_frame.cpp:190-213`).

Proposal, not applied — `TEXMOD` switch (F13 shape):
```diff
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@
+TEXMOD_STRIPPED := $(strip $(TEXMOD))
+ifneq ($(TEXMOD_STRIPPED),)
+TEXMOD_OK := $(shell echo '$(TEXMOD_STRIPPED)' | grep -Eq '^1$$' && echo ok)
+ifeq ($(TEXMOD_OK),)
+$(error TEXMOD must be empty or 1 (got: $(TEXMOD)))
+endif
+TARGET := $(TARGET)-texmod
+BUILD := $(BUILD)-texmod
+CFLAGS += -DTWLHD_DEMO_TEXMOD=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS += -DTWLHD_DEMO_TEXMOD=1
+endif
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@
+#ifdef TWLHD_DEMO_TEXMOD
+    use_modulate_vertex_colours();   /* red/green/blue/white corners */
+#endif
```

* **Capture:** one top 400x240 crop.
* **Pass criterion:** sample the four texel cells with
  `tools/check_tex_shot.py --snap` (or a `compare_ppm.py` JSON) at the expected
  cell colours; tolerance 12.
* **Effort:** 1 ROM build, 1 capture.
* **Deviation risk:** MEDIUM — MODULATE is implemented, but DS modulation
  rounding with a non-white vertex colour is untested.

### 6. Gouraud interpolation across one primitive

* **ROM variant:** new `GRAD=1` switch: one large triangle at
  `STATIC_ANGLE=0` with red/green/blue vertex colours. Scene 1 uses 1-level red
  steps (`arm9v2/source/demo_frame.cpp:225-243`), too small to measure.
  EQ-4d (F14) measured only one flat median per quad, so this does not repeat
  it.

Proposal, not applied — `GRAD` switch (F13 shape):
```diff
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@
+GRAD_STRIPPED := $(strip $(GRAD))
+ifneq ($(GRAD_STRIPPED),)
+GRAD_OK := $(shell echo '$(GRAD_STRIPPED)' | grep -Eq '^1$$' && echo ok)
+ifeq ($(GRAD_OK),)
+$(error GRAD must be empty or 1 (got: $(GRAD)))
+endif
+TARGET := $(TARGET)-grad
+BUILD := $(BUILD)-grad
+CFLAGS += -DTWLHD_DEMO_GRAD=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS += -DTWLHD_DEMO_GRAD=1
+endif
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@
+#ifdef TWLHD_DEMO_GRAD
+    draw_grad_triangle();   /* red/green/blue corners */
+#endif
```

* **Capture:** one top 400x240 crop.
* **Pass criterion:** a 3x3 interior sample grid compared with a barycentric
  prediction from the three corner colours; tolerance 12.

Proposal, not applied — triangle mode in `measure_blend_hw.py`:
```diff
--- a/tools/eq4w_blend_hw/measure_blend_hw.py
+++ b/tools/eq4w_blend_hw/measure_blend_hw.py
@@
+        elif arg == "--triangle":
+            triangle = True   # barycentric 3x3 grid, no scene-4 rects
```
* **Effort:** 1 ROM build, 1 capture, 1 small tool diff.
* **Deviation risk:** MEDIUM — per-vertex values match; the DS interpolates in
  fixed point, PICA in float.

### 7. Texture wrap / repeat / flip in DS mode

* **ROM variant:** new `TEXWRAP=1` switch: the four wrap quads of
  `tools/gen_tex_behaviour_frames.py` (repeat S/T, clamp, mirrored repeat,
  flip-only) over a small checker.

Proposal, not applied — `TEXWRAP` switch (F13 shape):
```diff
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@
+TEXWRAP_STRIPPED := $(strip $(TEXWRAP))
+ifneq ($(TEXWRAP_STRIPPED),)
+TEXWRAP_OK := $(shell echo '$(TEXWRAP_STRIPPED)' | grep -Eq '^1$$' && echo ok)
+ifeq ($(TEXWRAP_OK),)
+$(error TEXWRAP must be empty or 1 (got: $(TEXWRAP)))
+endif
+TARGET := $(TARGET)-texwrap
+BUILD := $(BUILD)-texwrap
+CFLAGS += -DTWLHD_DEMO_TEXWRAP=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS += -DTWLHD_DEMO_TEXWRAP=1
+endif
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@
+#ifdef TWLHD_DEMO_TEXWRAP
+    draw_wrap_quads();   /* repeat/clamp/mirror/flip checker */
+#endif
```

* **Capture:** one top 400x240 crop.
* **Pass criterion:** `tools/check_tex_behaviour.py` cell-by-cell at tolerance
  28, with a text-only `--clear` option (the checker still assumes the old
  `(16,16,24)` clear).

Proposal, not applied — `--clear` option in `check_tex_behaviour.py`:
```diff
--- a/tools/check_tex_behaviour.py
+++ b/tools/check_tex_behaviour.py
@@
+    parser.add_argument("--clear", default="16,16,24",
+                        help="R,G,B clear colour for the new ROM")
     args = parser.parse_args(argv)
```
* **Effort:** 1 ROM build, 1 capture, 1 tool diff.
* **Deviation risk:** MEDIUM — repeat/mirror are used for tiled surfaces; only
  the PICA arm (Run 21) is recorded.

### 8. Back-face culling

* **ROM variant:** existing switches, no new code:
  `make -C arm9v2 DIAGSCREEN=9 STATIC_ANGLE=30` (`arm9v2/Makefile:205-225`;
  cull-back is set at `arm9v2/source/demo_frame.cpp:445`) plus the
  `AUTODUMP_FRAME` twin for the harness reference.
* **Capture:** one top 400x240 crop.
* **Pass criterion:** `tools/eq4_hw_compare.py` against the harness reference;
  additionally diff the capture against the existing `CULL_NONE` cube capture
  to show which pixels the cull removes.
* **Effort:** 2 ROM builds, 1 harness run, 1 capture.
* **Deviation risk:** **HYPOTHESIS** — the renderer never culls, so a difference
  is likely, but not certain: a closed opaque cube with a depth test may look the
  same with or without culling; the measurement is how many pixels it costs.

### Deferred candidates (costed, not proposed)

Fog, edge marking, antialiasing, toon shading, polygon ID/shadow, DECAL/TOON/
HIGHLIGHT TexEnv modes, texture-transform modes 2/3, depth-EQUAL/w-buffer and
multiple screens all need renderer work before a hardware capture can be
interpreted, and for fog/edge/AA/toon/polygon-ID the mismatch is certain by
construction (`docs/EQUALITY_CHECK_DESIGN.md:221-229`, `:769`). They are listed
in the matrix with their cost but deliberately not ranked into the eight above.

## 5. What this report does not show

* **UNVERIFIED:** every experiment's pass criterion is an offline proposal; no
  tool run was executed against a new capture, and no console-side validation of
  the proposed ROM variants was made.
* **UNVERIFIED:** the risk column is a judgement from feature frequency, not a
  measurement.
* **UNVERIFIED:** for cells whose only hardware evidence is a TWL-mode run (Runs
  7-13, 18-21), the DS picture is untested; those cells say so.
* **Limits of the baseline:** one console, one capture per existing item, one
  New 3DS, no DS Phat/Lite; nothing here generalises across consoles.

## 6. Requirement traceability

| Requirement | Where |
|---|---|
| (1) matrix with all listed feature rows and columns (a)-(f), each cell labelled and backed | §3 (29 rows), §2 (FACT sources) |
| (2) ranked 5-8 plan with ROM variant, capture, pass criterion, effort, deviation risk | §4 (8 experiments) |
| (2) existing coverage of rotated and textured scenes cited, no duplication | §1 table, §3 rows 2 and 9 |
| (2) named first experiment with rationale | §4 experiment 1 |
| (3) append-only pointers in `docs/EQUALITY_CHECK_DESIGN.md` and `docs/EQ_SCENES.md` | those two files, last line each |
| (4) rules: placeholders, no home paths, no `python3 -c`, commands run once, diff and size quoted | §2 (F1-F14), §7 |
| no `common/`, `arm11/`, `arm9v2/`, `arm7/`, `tests/` change; no ROM, no console | scope above; `git status --short` lists docs only |

## 7. Size audit

The command was executed once more after the final edit; that run printed the
values below.

```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4ze-hw-coverage.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
46639
682 docs/reports/eq4ze-hw-coverage.md
```


**Result (EQ-4zj):** the DS-mode texture-format grid (TEXFMT=1) and its hardware comparison are in `docs/reports/eq4zj-texfmt-hw-result.md`.

**Result (EQ-4zk):** the DS MODULATE probe (TEXMOD=1) and its hardware comparison are in `docs/reports/eq4zk-texmod-hw-result.md`.

**Result (EQ-4zl/4zm/4zn):** the texture-behaviour, Gouraud and culling probes (TEXBEH/GRAD/CULL=1) and their hardware comparison are in `docs/reports/eq4zl-zm-zn-probes-hw-result.md`.
