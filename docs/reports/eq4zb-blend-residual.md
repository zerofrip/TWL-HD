# EQ-4zb: where the arm11-vs-DS blend residual comes from (offline)

Offline analysis only. Basis: ONE console, ONE capture session per frame
(EQ-4za). Scene 4 is a blend-enabled frame at static angle 30 over a black
clear; the two translucent quads are green `(0,255,0)` alpha 16 and blue
`(0,0,255)` alpha 4. No production code is changed; §6 is text only.

Labels: **FACT** = command + redirected log + `exit=`; **SOURCE-READ** = quoted
source; **MODEL** = computed from quoted lines; **HYPOTHESIS** = guess without a
source derivation; **UNVERIFIED** = not established.

## 1. Question and measured inputs

Our arm11 console measures green 138 (alpha-16 quad) and blue 41 (alpha-4); the
DS hardware measures 134 and 33, a residual of green +4 / blue +8. Is it
derivable from the alpha -> PICA blend -> display path? §4 tabulates every input.

## 2. SOURCE-READ

### 2.1 Alpha path: the weight is a float, with no 8-bit conversion in our code

The DS 5-bit polygon alpha becomes the float weight `(a+1)/32` (alpha 0 is the
DS wireframe case, emitted as `0.0`), applied once per BEGIN group into
`rgba[3]`; nothing in `common/` or `arm11/` converts it to 8 bits.

SOURCE-READ — alpha path.

```
$ { echo '--- common/twlhd_gx_interp.c:387-397'; sed -n '387,397p' common/twlhd_gx_interp.c; echo '--- common/twlhd_gx_interp.c:1069-1077'; sed -n '1069,1077p' common/twlhd_gx_interp.c; echo '--- common/twlhd_gx_interp.c:1155-1160'; sed -n '1155,1160p' common/twlhd_gx_interp.c; echo '--- common/twlhd_gx_interp.c:399-405'; sed -n '399,405p' common/twlhd_gx_interp.c; echo '--- common/twlhd_gx_interp.h:21-26'; sed -n '21,26p' common/twlhd_gx_interp.h; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
--- common/twlhd_gx_interp.c:387-397
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
--- common/twlhd_gx_interp.c:1069-1077
        g->prim_emitted = 0;
        /* Apply the pending POLYGON_ATTR alpha and light mask once per BEGIN
         * group (DeSmuME gfx3d.cpp:2727-2729). */
        g->applied_alpha = g->pending_alpha;
        g->applied_light_mask = g->pending_light_mask;
        g->rgba[3] = gx_alpha_float(g->applied_alpha);
        if (g->applied_alpha == 0u) {
            g->stats->alpha_wireframe_polys++;
        } else if (g->applied_alpha < 31u) {
--- common/twlhd_gx_interp.c:1155-1160
    g.rgba[0] = g.rgba[1] = g.rgba[2] = 1.0f;
    g.pending_alpha = 31u; /* no-attr default: opaque. */
    g.applied_alpha = 31u;
    g.rgba[3] = gx_alpha_float(g.applied_alpha);

    g.proj_stack.capacity = PROJ_STACK_CAP;
--- common/twlhd_gx_interp.c:399-405
static void set_rgba_from_rgb555(GxInterp *g, uint32_t value) {
    g->rgba[0] = (float)(value & 0x1Fu) / 31.0f;
    g->rgba[1] = (float)((value >> 5) & 0x1Fu) / 31.0f;
    g->rgba[2] = (float)((value >> 10) & 0x1Fu) / 31.0f;
    /* rgba[3] is set from the applied POLYGON_ATTR alpha at BEGIN_VTXS. */
}

--- common/twlhd_gx_interp.h:21-26

/* 32 bytes: clip-space position plus latched RGBA. */
typedef struct {
    float clip[4]; /* x, y, z, w in DS clip space (no divide, no clip). */
    float rgba[4]; /* r, g, b, a in 0..1 (alpha from POLYGON_ATTR, see stats). */
} TwlhdGxVertex;
```

The weight reaches PICA as a float; the only float->8-bit conversion is inside
PICA fixed-function (invisible here) — the legitimate unknown. Untextured colour
is 5-bit/31 floats, so the alpha-16 green and alpha-4 blue sources are `1.0`.

### 2.2 Renderer: RGBA8 target, float vertex loaders, RGB8 lossless transfer

The target is 32-bit `GPU_RB_RGBA8`, **not** 16-bit, excluding any RGB565/RGB5A1
output-quantisation candidate (`arm11/source/renderer.c:757-758`); the transfer
is RGBA8 in, RGB8 out, no scaling, a lossless 8-bit copy. Vertex data is two
`GPU_FLOAT` loaders, the shader passes colour through (`mov outclr, inclr`), and
the blend is `GPU_BLEND_ADD` with `GPU_SRC_ALPHA`/`GPU_ONE_MINUS_SRC_ALPHA`.

SOURCE-READ — renderer, blend config and shader.

```
$ { echo '--- arm11/source/renderer.c:46-53'; sed -n '46,53p' arm11/source/renderer.c; echo '--- arm11/source/renderer.c:756-759'; sed -n '756,759p' arm11/source/renderer.c; echo '--- arm11/source/renderer.c:1534,1535,1539'; sed -n '1534,1535p;1539p' arm11/source/renderer.c; echo '--- arm11/source/renderer.c:1563-1576'; sed -n '1563,1576p' arm11/source/renderer.c; echo '--- arm11/source/renderer.c:1590-1593'; sed -n '1590,1593p' arm11/source/renderer.c; echo '--- arm11/include/twlhd_blend_config.h:27-34'; sed -n '27,34p' arm11/include/twlhd_blend_config.h; echo '--- arm11/include/twlhd_render_config.h:30-31'; sed -n '30,31p' arm11/include/twlhd_render_config.h; echo '--- arm11/shaders/twlhd_clip_vshader.v.pica:14-29'; sed -n '14,29p' arm11/shaders/twlhd_clip_vshader.v.pica; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
--- arm11/source/renderer.c:46-53

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

--- arm11/source/renderer.c:756-759
    renderer_trace("RenderTargetCreate");
    g_renderer.target = C3D_RenderTargetCreate(
        240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!g_renderer.target) {
--- arm11/source/renderer.c:1534,1535,1539
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 4); /* v0 = clip xyzw */
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4); /* v1 = rgba      */
    BufInfo_Add(buf_info, g_renderer.slots.active, 32, (s8)components, perm);
--- arm11/source/renderer.c:1563-1576
        if (diag->mode == 0) {
            blend = g_blend_enabled ? twlhd_blend_config_alpha()
                                    : twlhd_blend_config_opaque();
            if (!g_blend_enabled) {
                at = twlhd_alpha_test_gt_zero();
            }
        } else {
            blend = diag->blend;
        }

        C3D_AlphaBlend(blend.colorEq, blend.alphaEq,
                       blend.srcColor, blend.dstColor,
                       blend.srcAlpha, blend.dstAlpha);
        C3D_AlphaTest(at.enable, at.func, at.ref);
--- arm11/source/renderer.c:1590-1593
            C3D_TexEnvSrc(env, C3D_Alpha, diag->alpha_src, 0, 0);
            C3D_TexEnvOpAlpha(env, diag->alpha_op, 0, 0);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
            /* Constant colour is written every frame so a mode-1/2 constant
--- arm11/include/twlhd_blend_config.h:27-34
static inline TwlhdBlendConfig twlhd_blend_config_alpha(void) {
    TwlhdBlendConfig cfg = {
        GPU_BLEND_ADD, GPU_BLEND_ADD,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
    };
    return cfg;
}
--- arm11/include/twlhd_render_config.h:30-31
#ifndef TWLHD_RENDER_CLEAR_COLOR
#define TWLHD_RENDER_CLEAR_COLOR 0x000000FFu
--- arm11/shaders/twlhd_clip_vshader.v.pica:14-29
.fvec clipfix[4]

.out outpos position
.out outclr color

.alias inpos v0
.alias inclr v1

.proc main
	dp4 outpos.x, clipfix[0], inpos
	dp4 outpos.y, clipfix[1], inpos
	dp4 outpos.z, clipfix[2], inpos
	dp4 outpos.w, clipfix[3], inpos
	mov outclr, inclr
	end
.end
```

The clear colour is `0x000000FF`, black opaque (`twlhd_render_config.h:31`),
matching EQ-4za's `(0,0,0)`.

### 2.3 DS convention (EQ-4i) and the `expected.py` formulas

EQ-4i's §5.1/§5.2 verdict: DS green is the 6-bit A2 value expanded with
`round(v6*255/63)`; DS red/blue are `expand5(v6>>1)`. `expected.py` codes both.

SOURCE-READ — EQ-4i findings and the `expected.py` formula bodies.

```
$ { echo '--- tools/ds_alpha_test/expected.py:68-70,73,80,83-85,88-94,97,104-114,117,125-127'; sed -n '68,70p;73p;80p;83,85p;88,94p;97p;104,114p;117p;125,127p' tools/ds_alpha_test/expected.py; echo '--- docs/reports/eq4i-alpha-hw-result.md:683-686,695-696,698-705'; sed -n '683,686p;695,696p;698,705p' docs/reports/eq4i-alpha-hw-result.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
--- tools/ds_alpha_test/expected.py:68-70,73,80,83-85,88-94,97,104-114,117,125-127
def expand5(v5):
    """DS 5-bit -> 8-bit display expansion."""
    return (v5 << 3) | (v5 >> 2)
def expand6(v5):
    return (v5 << 1) | (v5 >> 4)
def round_half_up(x):
    """Round a non-negative float half-up (deterministic, not banker's)."""
    return int(math.floor(x + 0.5))
def weight(alpha, convention):
    """Blend weight for a 5-bit polygon alpha under A1 or A2."""
    if convention == "A1":
        return alpha / 31.0
    if convention == "A2":
        return (alpha + 1) / 32.0
    raise ValueError("convention must be A1 or A2: %r" % (convention,))
def expected6(alpha, backdrop5, convention, src5=31):
    if convention not in CONVENTIONS:
        raise ValueError("convention must be A1 or A2: %r" % (convention,))
    src6 = expand6(src5)
    back6 = expand6(backdrop5)
    if convention == "A2":
        alpha6 = alpha + 1
        v6 = (alpha6 * src6 + (32 - alpha6) * back6) >> 5
    else:
        w = weight(alpha, convention)
        v6 = int(math.floor(w * src6 + (1.0 - w) * back6))
    return max(0, min(63, v6))
def expected6_565(alpha, backdrop5, convention, src5=31):
    v6 = expected6(alpha, backdrop5, convention, src5)
    rb = expand5(v6 >> 1)
    return (rb, round_half_up(v6 * 255.0 / 63.0), rb)
--- docs/reports/eq4i-alpha-hw-result.md:683-686,695-696,698-705
### 5.1 Green channel (FACT, shown by the quoted output)

- `g_votes_A1=0`, `g_votes_A2=10`, `g_votes_ambiguous=4`,
  `g_channel_verdict=A2`.
- Therefore: **the green channel of the hardware capture matches the DeSmuME
  6-bit A2 model on the quads that can decide, and never A1.**

### 5.2 Red and blue channels (FACT, shown by the quoted output)

- `fit_6bit_565_A2_R=0` and `fit_6bit_565_A2_B=0`: on **all 14 quads** the
  measured red and blue equal `expand5(v6>>1)` — the **5-bit quantisation of
  the same 6-bit A2 values** — exactly.
- The black-backdrop R/B values are `33,66,99,132,165,198,231` (= `expand5`
  of 4,8,12,16,20,24,28) and the grey-backdrop values `148,165,181,198,214,231,247`;
  both are 5-bit-quantised, not the raw 6-bit A2 values.
```

### 2.4 Measured numbers used by the match table

SOURCE-READ — the decisive measured lines, quoted from their own reports.

```
$ { echo '--- docs/reports/eq4za-clear-black-hw-result.md:114,117,122,125'; sed -n '114p;117p;122p;125p' docs/reports/eq4za-clear-black-hw-result.md; echo '--- docs/reports/eq4z-clear-black.md:484,488'; sed -n '484p;488p' docs/reports/eq4z-clear-black.md; echo '--- docs/reports/eq4y-gate-hw-result.md:215,218,223,226'; sed -n '215p;218p;223p;226p' docs/reports/eq4y-gate-hw-result.md; echo '--- docs/reports/eq4y-gate-hw-result.md:264-265'; sed -n '264,265p' docs/reports/eq4y-gate-hw-result.md; echo '--- docs/reports/eq4x-blend-gate.md:583,586,591,594'; sed -n '583p;586p;591p;594p' docs/reports/eq4x-blend-gate.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
--- docs/reports/eq4za-clear-black-hw-result.md:114,117,122,125
quad1.majority_rgb=0,138,0
quad1.measured=138
quad2.majority_rgb=0,0,41
quad2.measured=41
--- docs/reports/eq4z-clear-black.md:484,488
quad1.majority_rgb=0,135,0
quad2.majority_rgb=0,0,40
--- docs/reports/eq4y-gate-hw-result.md:215,218,223,226
quad1.majority_rgb=0,142,8
quad1.measured=142
quad2.majority_rgb=8,12,57
quad2.measured=57
--- docs/reports/eq4y-gate-hw-result.md:264-265
| 1 (green) | 16 | G=255 | G=255 | G=134 | G=142 | 255 / 142 |
| 2 (blue) | 4 | B=255 | B=255 | B=33 | B=57 | 255 / 60 |
--- docs/reports/eq4x-blend-gate.md:583,586,591,594
quad1.majority_rgb=7,142,11
quad1.measured=142
quad2.majority_rgb=13,13,60
quad2.measured=60
```

The two runs over the identical `(16,16,24)` backdrop disagree on blue: EQ-4y 57
vs EQ-4x 60 (`eq4y:265`) — UNVERIFIED, not reconciled here.

## 3. MODEL

`tools/eq4zb_blend_model/blend_model.py` mechanically combines the §2
conversion/rounding choices into two candidate families; nothing is fitted.

DS convention (MODEL, from `expected.py` / EQ-4i):

- `v6 = ((a+1)*63 + (31-a)*B6) >> 5`, `B6 = expand6(B5)` (`:106-110`); green =
  `round_half_up(v6*255/63)`, red/blue = `expand5(v6>>1)` (`:126-127`, `:68-70`).

Our-path candidates (MODEL, from §2.1/§2.2): `exact_float_floor`/`_round` keep
`w=(a+1)/32` on the 8-bit RGBA8 target, then floor or round-half-up;
`alpha8_trunc_*`/`alpha8_round_*` model the invisible PICA float->8-bit alpha as
`trunc(w*255)` or `round(w*255)`, then blend `(A8*S + (255-A8)*B)/255` with floor
or round-half-up. No RGB565/RGB5A1 or transfer candidate exists: the target is
`GPU_RB_RGBA8` (`renderer.c:757-758`), the transfer RGB8 out of RGBA8 (`:47-52`).

FACT — model output: match table and the full 0..31 green/blue table.

```
$ python3 tools/eq4zb_blend_model/blend_model.py > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=eq4zb_blend_model
basis=offline model, no hardware, no network
measured_green_black=138 (eq4za)
measured_blue_black=41 (eq4za)
measured_green_grey=142 (eq4y/eq4x)
measured_blue_grey=57 (eq4y) / 60 (eq4x)
candidate,green_black,blue_black,green_grey,blue_grey
DS convention (EQ-4i),134,33,n/a,n/a
exact_float_floor,135,39,142,60
exact_float_round,135,40,143,60
alpha8_trunc_floor,135,39,142,59
alpha8_trunc_round,135,39,143,59
alpha8_round_floor,135,40,142,60
alpha8_round_round,135,40,143,60
alpha,ds_g,our_g,delta_g,ds_b,our_b,delta_b
0,4,0,-4,0,0,0
1,12,16,4,8,16,8
2,20,24,4,16,24,8
3,28,32,4,24,32,8
4,36,40,4,33,40,7
5,45,48,3,41,48,7
6,53,56,3,49,56,7
7,61,64,3,57,64,7
8,69,72,3,66,72,6
9,77,80,3,74,80,6
10,85,88,3,82,88,6
11,93,96,3,90,96,6
12,101,104,3,99,104,5
13,109,112,3,107,112,5
14,117,120,3,115,120,5
15,125,128,3,123,128,5
16,134,135,1,132,135,3
17,142,143,1,140,143,3
18,150,151,1,148,151,3
19,158,159,1,156,159,3
20,166,167,1,165,167,2
21,174,175,1,173,175,2
22,182,183,1,181,183,2
23,190,191,1,189,191,2
24,198,199,1,198,199,1
25,206,207,1,206,207,1
26,215,215,0,214,215,1
27,223,223,0,222,223,1
28,231,231,0,231,231,0
29,239,239,0,239,239,0
30,247,247,0,247,247,0
31,255,255,0,255,255,0
```

## 4. MATCH TABLE (honest)

| Candidate | Green, black | Blue, black | Green, `(16,16,24)` | Blue, `(16,16,24)` |
|---|---|---|---|---|
| measured DS hardware (EQ-4za) | 134 | 33 | n/a | n/a |
| measured arm11 console (EQ-4za/EQ-4y) | 138 | 41 | 142 | 57 |
| EQ-4z emulator over black | 135 | 40 | n/a | n/a |
| EQ-4x emulator over `(16,16,24)` | n/a | n/a | 142 | 60 |
| DS convention (EQ-4i model) | 134 | 33 | n/a | n/a |
| `exact_float_floor` | 135 | 39 | 142 | 60 |
| `exact_float_round` | 135 | 40 | 143 | 60 |
| `alpha8_trunc_floor` | 135 | 39 | 142 | 59 |
| `alpha8_trunc_round` | 135 | 39 | 143 | 59 |
| `alpha8_round_floor` | 135 | 40 | 142 | 60 |
| `alpha8_round_round` | 135 | 40 | 143 | 60 |

Verdict, stated plainly:

- **No source-derived candidate reproduces 138 / 41.** The best reach 135 / 40,
  so the console is +3 / +1 above every candidate; the float weight is not the
  residual cause. Closest: `exact_float_round` / `alpha8_round_*`. The DS
  convention reproduces DS 134 / 33, so the gap is on our side.
- Over `(16,16,24)` `exact_float_floor` and `alpha8_round_floor` reproduce the
  EQ-4x 142 / 60 (`alpha8_trunc_floor` gives 142 / 59) and green 142 matches the
  console, but console blue 57 (EQ-4y) is below every candidate (minimum 59) and
  is not 60 — the UNVERIFIED two-run inconsistency of §2.4.
- The `(16,16,24)` cells use 8-bit backdrop bytes directly; the DS convention is
  `n/a` there (DS was captured over black only, EQ-4i).

## 5. Full 0..31 green/blue table, read against the DS convention

The §3 FACT block is the complete table. At the measured alphas the structural
delta is +1 green (alpha 16) and +7 blue (alpha 4) against a residual of +4 and
+8, so quantisation explains at most 1 of 4 green and 7 of 8 blue levels: **+3
green / +1 blue remain unexplained**. Green is measured only at alpha 16, blue
only at alpha 4 (alpha 0 is not a blend, `common/twlhd_gx_interp.c:391-397`).

## 6. PROPOSAL (text only, NOT applied)

No candidate matches 138/41, so no source-derived change provably turns our
output into the DS values. The structural gap: our PICA path lacks the DS
per-channel output quantisation (6-bit green, 5-bit red/blue) at alphas 16 and 4.

- **Realism of a fix.** The clip path cannot post-quantise the blend output (a
  shader cannot read the destination colour); the target must stay `GPU_RB_RGBA8`
  (`renderer.c:757-758`) for the RGB8 transfer (`:47-52`). Exact DS reproduction
  needs the PICA blend-unit rounding (unexposed here) or a separate pass.
- **Measured diagnostic first (recommended).** Mode 1 ("alpha-as-gray", blending
  off, RGB = source alpha) reads the fragment-alpha byte for `w=17/32` and
  `w=5/32` off the grey level; mode 2 ("const-alpha-0.5", RGB unchanged, alpha =
  `0x80`) probes the blend unit with a known alpha. B cycles modes, A loads
  `sdmc:/twlhd_frame.bin`; both keys and configs are quoted next.

SOURCE-READ — diag modes 1/2, the blend-off setting and the B/A keys.

```
$ { echo '--- arm11/source/main.c:36'; sed -n '36p' arm11/source/main.c; echo '--- arm11/source/main.c:575-578'; sed -n '575,578p' arm11/source/main.c; echo '--- arm11/source/main.c:580-582'; sed -n '580,582p' arm11/source/main.c; echo '--- arm11/include/twlhd_diag_config.h:61-63,66,69-78,80'; sed -n '61,63p;66p;69,78p;80p' arm11/include/twlhd_diag_config.h; echo '--- arm11/include/twlhd_diag_config.h:85-88,105,107'; sed -n '85,88p;105p;107p' arm11/include/twlhd_diag_config.h; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
--- arm11/source/main.c:36
#define FRAME_PATH "sdmc:/twlhd_frame.bin"
--- arm11/source/main.c:575-578
        if (keys & KEY_B) {
            renderer_set_diag_mode(twlhd_diag_next(renderer_get_diag_mode()));
            update_console();
        }
--- arm11/source/main.c:580-582
        if (keys & KEY_A) {
            g_seq_on = 0;
            if (load_sd_frame(FRAME_PATH, &pending_vertex_count,
--- arm11/include/twlhd_diag_config.h:61-63,66,69-78,80
        /* Mode 1 "alpha-as-gray": blending OFF (opaque), RGB replaces with
         * source alpha (grey = raw fragment alpha), Alpha channel sources the
         * constant and outputs 1.0 so the grey value is fully visible. */
            .blend = {
                .srcColor = GPU_ONE,
                .dstColor = GPU_ZERO,
                .srcAlpha = GPU_ONE,
                .dstAlpha = GPU_ZERO,
            },
            .rgb_op = {
                GPU_TEVOP_RGB_SRC_ALPHA,
                GPU_TEVOP_RGB_SRC_ALPHA,
                GPU_TEVOP_RGB_SRC_ALPHA,
            },
            .alpha_src = GPU_CONSTANT,
--- arm11/include/twlhd_diag_config.h:85-88,105,107
        /* Mode 2 "const-alpha-0.5": explicit alpha blend, RGB unchanged,
         * Alpha channel sources the constant and outputs 0.5 (alpha byte 0x80
         * in bits 24-31). If the blend registers are effective the fragment
         * blends at 50%; if they are ineffective it will still draw opaque. */
            .alpha_src = GPU_CONSTANT,
            .constant_color = 0x80000000, /* alpha byte 0x80 -> output alpha 0.5 */
```

- **Hardware verification plan (same procedure as EQ-4x/EQ-4za).** Rebuild the
  arm11 3dsx; copy the scene-4 blend frame to `sdmc:/twlhd_frame.bin`; press A
  to load (`arm11/source/main.c:36`, `:580-582`); capture with EQ-4r §2 settings;
  run `python3 tools/eq4w_blend_hw/measure_blend_hw.py <capture>`; repeat with B
  selecting diag modes 1 and 2 (`:575-578`).
- **Additional frame set only if needed (described, NOT produced).** Modes 1/2
  leave one data point per channel (alpha 16 green, alpha 4 blue), too few to
  locate the PICA step. The needed set is arm11-side, not a DS-side capture:
  scene-4-style blend frames loaded as `sdmc:/twlhd_frame.bin` with A, green and
  blue quads at several alphas over black — at least where the §3 table's deltas
  change: 1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31 — measured with
  `python3 tools/eq4w_blend_hw/measure_blend_hw.py <capture>`. Not produced now.

**Update (2026-11-26):** EQ-4zc produced this frame set offline (`tools/eq4zc_alpha_sweep/`) and its Azahar verification; the hardware run remains for the owner — see `docs/reports/eq4zc-alpha-sweep.md`.

## 7. Unknowns and UNVERIFIED

- PICA's float->8-bit alpha conversion and blend-unit rounding appear in no
  repository line; every our-path candidate is a hypothesis about that step.
- The console 138/41 is +3/+1 above every source-derived candidate: UNVERIFIED.
- EQ-4y blue 57 vs EQ-4x blue 60 over the identical `(16,16,24)` background are
  UNVERIFIED; so is the DS-engine vs 3DS-display origin of DS R/B (EQ-4i §5.3).
- HYPOTHESIS, not source-derived and not a model: `round(w*260)` gives 138 and 41;
  no quoted line supports a 260 scale (transfer is 8-bit RGB, max 255), so it is a
  coincidence; nothing in the tool uses it.

## 8. Tool, tests and audit

- Model and tests: `tools/eq4zb_blend_model/{blend_model.py,test_blend_model.py}`
  (19 tests, loading `tools/ds_alpha_test/expected.py` by path); wired into
  `make test` via `test-eq4zb-blend-model`.

FACT — complete output of `make test-eq4zb-blend-model` (with `exit=$?`):

```
$ make test-eq4zb-blend-model > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4zb_blend_model -p 'test_*.py'
...................
----------------------------------------------------------------------
Ran 19 tests in 0.001s

OK
```

FACT — complete output of the aggregate discovery command (with `exit=$?`):

```
$ EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 ARM9V2_BLEND_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
ssssssss.....ssssssssssssssssssssssss..........................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssss...ssssssssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 435 tests in 89.795s

OK (skipped=110)
/tmp/tmp8xfixhwg/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

FACT — complete output of the size commands (with `exit=$?`):

```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zb-blend-residual.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
44590
497 docs/reports/eq4zb-blend-residual.md
```

