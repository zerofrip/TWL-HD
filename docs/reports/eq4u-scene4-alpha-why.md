<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4u — scene 4 alpha: why the DS paints it opaque and our render blends

**Status:** OFFLINE SOURCE-READ only. No console access, no emulator run, no
build, no production-code change in this round. The fix is **PROPOSED**, not
applied. Every in-tree claim is a quoted command; every out-of-tree claim is
labelled as a read of a third-party tree, not hardware truth.

```
basis=in-tree source (arm9v2, common, arm11, tools) + third-party sources ($DEVKITPRO libnds headers, the DeSmuME checkout) + the existing hardware measurements in docs/reports/eq4r-static-scenes-hw-compare.md, eq4h, eq4i and Run 17 of docs/HARDWARE_TEST_PROCEDURE_3DS.md
```

Labels follow `docs/reports/eq4h-alpha-convention-test.md:11-14`: **FACT** = a
command and its complete raw output quoted here (or, where marked "cited FACT",
a number already printed as FACT in the named in-repo report, quoted unchanged
and not re-derived); **SOURCE-READ** = a quoted source `file:line`; **PLAN** = a
proposal, not applied; **HYPOTHESIS** / **UNVERIFIED** = not measured here.

**Question.** `docs/reports/eq4r-static-scenes-hw-compare.md` §6-§8 found that
for `SCENE=4` (four quads, `POLY_ALPHA` 31/16/4/0) the real hardware capture and
the DeSmuME harness paint the alpha-16 and alpha-4 quads (almost) opaque and the
alpha-0 quad as an outline, while our arm11/Azahar render blends the quads with
the clear colour and does not draw the alpha-0 quad as an outline. Is our render
wrong relative to the DS for this ROM — and for what class of real DS programs —
or is the test ROM unrepresentative? Three things are separated below: (1) the
alpha-blend-enable (`DISP3DCNT` bit 3) semantics, (2) `POLY_ALPHA` 0 = wireframe
semantics, (3) which of the two our capture/recording/render chain carries.

## 0. Short answer

- **This ROM:** our render differs from the DS. The DS paints `alpha 16` and
  `alpha 4` at full brightness and draws the `alpha 0` quad as an outline; our
  render blends `16`/`4` with the `(16,16,24)` PICA clear and makes the `alpha 0`
  quad invisible (FACT, `eq4r` §8 census quoted in §4.4 below). The blend
  difference is explained, at the source level, by the blend-enable bit: the ROM
  never sets `GL_BLEND`, and DeSmuME's own blend path copies the source opaque
  when the enable is clear (SOURCE-READ, §4). That the DS behaves the same way is
  a **HYPOTHESIS** supported by the hardware capture and the independent DeSmuME
  agreement, never by a hardware register read here.
- **The alpha-0 outline** is a second, independent defect: `POLY_ALPHA` 0 selects
  wireframe/outline (SOURCE-READ, §3), hardware draws an outline (FACT, `eq4r`
  §2/§7), and **neither** DeSmuME's software rasteriser nor our renderer draws
  one (SOURCE-READ, §4/§5). Our chain loses it because the interpreter emits
  vertex alpha `0.0` and the renderer blends it to nothing.
- **Our chain:** the blend-enable state *is* recorded (the shim maps
  `GFX_CONTROL` to a recording port and `DISP3DCNT` is on the RSTA whitelist)
  but is **dropped after parsing**: the interpreter never reads `view->rsta`, and
  the renderer always applies alpha blending. So even a ROM that calls
  `glEnable(GL_BLEND)` would render with the same always-on blend — the state is
  carried but not consumed (§5).
- **Class of real programs:** for this ROM the render is *wrong* (blend-disabled
  class), but the ROM is an *unrepresentative* state combination — it asks for
  translucency (`POLY_ALPHA<31`) while leaving blending disabled. For programs
  that do enable blending, our A2 weight and `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`
  equation are consistent with the hardware green-channel measurements of
  `eq4i` (cited, not re-derived), with red/blue and the end-to-end path
  UNVERIFIED beyond that one console/ROM.

## 1. (a) Our demo ROM's GL setup for scene 4 — FACT

FACT — the scene-4 data, including the four raw alphas:

```
$ sed -n '246,259p' arm9v2/source/demo_frame.cpp
/* Scene 4: four flat quads in one quad-list each, with one POLYGON_ATTR per
 * BEGIN so the interpreter latches alpha 31, 16, 4 and 0. Four distinct solid
 * colours; the alpha-0 quad is counted as wireframe (outline drawing is NOT
 * implemented, so the host interpreter emits it with vertex alpha 0.0). */
static const int16_t scene_alpha_vertices[4][4][2] = {
    {{-2400, -1600}, { -400, -1600}, { -400, -200}, {-2400,  -200}},
    {{  400, -1600}, { 2400, -1600}, { 2400,  -200}, {  400,  -200}},
    {{-2400,   200}, { -400,   200}, { -400,  1600}, {-2400,  1600}},
    {{  400,   200}, { 2400,   200}, { 2400,  1600}, {  400,  1600}},
};
static const uint8_t scene_alpha[4] = {31u, 16u, 4u, 0u};
static const uint8_t scene_colors[4][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
};
```

FACT — the draw path: one `glPolyFmt(POLY_ALPHA(...))` per quad, no enable:

```
$ sed -n '293,308p' arm9v2/source/demo_frame.cpp
static void draw_scene_alpha_quads(void) {
    int q;
    int i;

    for (q = 0; q < 4; q++) {
        glPolyFmt(POLY_ALPHA(scene_alpha[q]) | POLY_CULL_NONE);
        glBegin(GL_QUADS);
        for (i = 0; i < 4; i++) {
            glColor3b(scene_colors[q][0], scene_colors[q][1],
                      scene_colors[q][2]);
            glVertex3v16((v16)scene_alpha_vertices[q][i][0],
                         (v16)scene_alpha_vertices[q][i][1], 0);
        }
        glEnd();
    }
}
```

FACT — the whole ROM never mentions `GL_BLEND`:

```
$ grep -rn GL_BLEND arm9v2/
exit=1
```

FACT — the only `glEnable` calls in the ROM are `GL_TEXTURE_2D` (scene 5) and
`GL_ANTIALIAS`; the per-frame state re-issue sets antialiasing, not blending:

```
$ grep -rn glEnable arm9v2/
arm9v2/source/main.cpp:173:    glEnable(GL_ANTIALIAS);
arm9v2/source/main.cpp:201:    glEnable(GL_ANTIALIAS);
arm9v2/source/demo_frame.cpp:204:    glEnable(GL_TEXTURE_2D);
arm9v2/source/demo_frame.cpp:352:    glEnable(GL_ANTIALIAS);
arm9v2/source/demo_frame.cpp:408:    glEnable(GL_ANTIALIAS);
arm9v2/source/demo_frame.cpp:459:    glEnable(GL_ANTIALIAS);
```

FACT — the default init path for this ROM (the `#else` branch is the default
`DIAGSCREEN=0` path, `main.cpp:16-18`; a SCENE-only build takes it):

```
$ sed -n '211,216p' arm9v2/source/main.cpp
#else
    videoSetMode(MODE_0_3D);
    consoleDemoInit(); /* statistics console on the sub screen */

    glInit(); /* library-internal writes are not proxied: INITIAL_STATE_UNKNOWN */
#endif
```

FACT — every scene draws with the state re-issue at the top of the frame body:

```
$ sed -n '350,354p' arm9v2/source/demo_frame.cpp
#if TWLHD_DEMO_SCENE != 0
    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
```

So for scene 4 the ROM sets `GL_ANTIALIAS` (DISP3DCNT bit 4) and never sets
`GL_BLEND` (bit 3); each quad's only alpha state is the `POLY_ALPHA` field in
`GFX_POLY_FORMAT`. `glInit` (SOURCE-READ, §2) zeroes `GFX_CONTROL`, so the blend
bit is clear at runtime.

## 2. (b) libnds behaviour — SOURCE-READ of a third-party header

**SOURCE-READ of the installed devkitPro libnds tree (`$DEVKITPRO`), not
hardware truth and not a hardware measurement.** Quoted only to show what the
calls in §1 compile to.

FACT (third-party header) — `GL_BLEND` is `DISP3DCNT` bit 3, `GL_ANTIALIAS` is
bit 4:

```
$ sed -n '247,254p;531,535p;770,776p;779,784p' "$DEVKITPRO/libnds/include/nds/arm9/videoGL.h"
	GL_TEXTURE_2D      = (1<<0),  /*!< enable/disable textures on the geometry engine */
	GL_TOON_HIGHLIGHT  = (1<<1),  /*!< enable = Highlight shading; disable = Toon shading */
	GL_ALPHA_TEST      = (1<<2),  /*!< whether to use the alpha threshold set in glAlphaFunc() */
	GL_BLEND           = (1<<3),  /*!< enable/disable alpha blending */
	GL_ANTIALIAS       = (1<<4),  /*!< nable/disable edge antialiasing; polygons must have different polygon IDs for the effect to work and the rear plane must be clear */
	GL_OUTLINE         = (1<<5),  /*!< enable/disable edge coloring; the high 3bits of the polygon ID determine the color; glSetOutlineColor() sets the available colors */
	GL_FOG_ONLY_ALPHA  = (1<<6),  /*!< enable = fade into background?; disable = don't fade? */
	GL_FOG             = (1<<7),  /*!< enables/disables fog */
/*! \fn  u32 POLY_ALPHA(u32 n)
\brief used in glPolyFmt() to set the alpha level for the following polygons, set to 0 for wireframe mode
\param n the level of alpha (0-31)
\return value for hw register*/
 u32 POLY_ALPHA(u32 n) { return (u32)((n) << 16); }
/*! \fn  void glPolyFmt(u32 params)
\brief Set the parameters for polygons rendered on the current frame<BR>
<A HREF="http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes">GBATEK http://problemkaputt.de/gbatek.htm#ds3dpolygonattributes</A>
\param params the paramters to set for the polygons for the current frame. valid paramters are enumerated in GL_POLY_FORMAT_ENUM and in the functions POLY_ALPHA() and POLY_ID() */
void glPolyFmt(u32 params) {
	GFX_POLY_FORMAT = params;
}
/*! \fn  void glEnable(int bits)
\brief Enables various gl states (blend, alpha test, etc..)
\param bits bit mask of desired attributes, attributes are enumerated in DISP3DCNT_ENUM */
void glEnable(int bits) {
	GFX_CONTROL |= bits;
}
```

FACT (third-party header comment, out-of-tree libnds source checkout) —
`glInit` sets `GFX_CONTROL = 0`:

```
$ sed -n '123,126p' "$HOME/3ds/libnds/source/arm9/videoGL_base.c"

	// reset the control bits
	GFX_CONTROL = 0;

```

Consequences (SOURCE-READ, third-party): `glEnable(GL_BLEND)` is a read-modify-
write of `DISP3DCNT` (bit 3) via `GFX_CONTROL |= bits`; the `POLY_ALPHA` header
text names `0` as "wireframe mode". The hardware meaning of the register write is
not established by this header — it is the writer's interface, not a register
specification. (GBATEK was not consulted in this round.)

## 3. (c) DeSmuME — SOURCE-READ of a third-party tree, not hardware truth

**SOURCE-READ of the local DeSmuME checkout (`$HOME/3ds/desmume`), the harness's
reference emulator. This is DeSmuME's reading of the DS, not hardware truth.**
The checkout/commit used by the existing reports is `docs/DS_GX_RESEARCH.md:5`;
the same tree is read here.

FACT (third-party source) — the blend path is gated on `DISP3DCNT`'s blend-enable
bit, and with the enable clear an `alpha<31` source is **copied opaque**; an
`alpha==0` source is skipped:

```
$ sed -n '477,497p' "$HOME/3ds/desmume/desmume/src/rasterize.cpp"
static FORCEINLINE void alphaBlend(const bool isAlphaBlendingEnabled, const Color4u8 inSrc, Color4u8 &outDst)
{
	if (inSrc.a == 0)
	{
		return;
	}
	
	if (inSrc.a == 31 || outDst.a == 0 || !isAlphaBlendingEnabled)
	{
		outDst = inSrc;
	}
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
```

FACT (third-party source) — the call site passes the enable bit, and a fragment
whose alpha is 0 is discarded before the blend:

```
$ sed -n '740,748p;770,773p' "$HOME/3ds/desmume/desmume/src/rasterize.cpp"
	this->_shade<ISSHADOWPOLYGON>((PolygonMode)polyAttr.Mode, vtxColor, texCoord, shaderOutput);
	
	// handle alpha test
	if ( shaderOutput.a == 0 ||
	    (renderState.DISP3DCNT.EnableAlphaTest && (shaderOutput.a < renderState.alphaTestRef)) )
	{
		return;
	}
	
		
		//alpha blending and write color
		alphaBlend((renderState.DISP3DCNT.EnableAlphaBlending != 0), shaderOutput, dstColor);
		
```

FACT (third-party source) — alpha 0 *is* recognised as "wireframe", alpha 31 as
"opaque":

```
$ sed -n '4177,4186p' "$HOME/3ds/desmume/desmume/src/gfx3d.cpp"
bool GFX3D_IsPolyWireframe(const POLY &p)
{
	return (p.attribute.Alpha == 0);
}

bool GFX3D_IsPolyOpaque(const POLY &p)
{
	return (p.attribute.Alpha == 31);
}

```

FACT (third-party source) — the *software* rasteriser never calls that predicate,
so it does not draw an outline; it only skips the polygon:

```
$ grep -n 'GFX3D_IsPolyWireframe' "$HOME/3ds/desmume/desmume/src/rasterize.cpp"
exit=1
```

Interpretation (SOURCE-READ, this tree only): DeSmuME's soft rasteriser keeps an
alpha-0 polygon invisible (fragment alpha 0 → early return) and does **not** draw
the DS outline; `GFX3D_IsPolyWireframe` is consumed by the OpenGL renderers
(`OGLRender.cpp`/`OGLRender_3_2.cpp` in the same tree), not by the software path
the harness capture uses. This is consistent with the harness result in `eq4r` §7
(`class_white_ref_area=0`).

## 4. (d) Our chain: record → parse → interpret → render

### 4.1 The blend-enable write *is* recorded

FACT — the ROM's libnds helpers are compiled against the recording shim, whose
`GFX_CONTROL` is a recording port at the `DISP3DCNT` address:

```
$ sed -n '192,193p' arm9v2/include/twlhd_gx_shim.hpp
#undef GFX_CONTROL
#define GFX_CONTROL (::twlhd_shim::port<0x04000060u, 2u>())
```

FACT — a whitelisted register write is appended as an RSTA entry:

```
$ sed -n '138,144p' common/twlhd_gx_record.c
        return;
    }

    if (twlhd_v2_rsta_whitelisted(addr, width)) {
        append_rsta(rec, addr, mask_to_width(value, width), width);
        return;
    }
```

FACT — `DISP3DCNT` is on the whitelist, as the range `0x04000060..0x04000061`:

```
$ sed -n '13p;30,35p' common/twlhd_v2_parse.c
#define RSTA_DISP3DCNT_START 0x04000060u
 * per-range labels.
 */
static const RstaRange g_rsta_ranges[] = {
    {RSTA_DISP3DCNT_START, 0x04000062u}, /* DISP3DCNT                 */
    {RSTA_EDGE_START,       0x04000340u}, /* edge colours + clear id   */
    {RSTA_ALPHA_START,      0x04000342u}, /* alpha test                */
```

FACT — the recorder is armed at the top of every frame, then the frame body (and
its `glEnable(GL_ANTIALIAS)` at `demo_frame.cpp:352`) runs inside that window:

```
$ sed -n '240,243p' arm9v2/source/main.cpp
    while (1) {
#if TWLHD_DEMO_DIAGSCREEN != 2 && TWLHD_DEMO_DIAGSCREEN < 5
        twlhd_gx_rec_arm(rec);
#endif
```

**SOURCE-READ chain, conclusion:** a `GL_BLEND` enable in ROM code would reach
`twlhd_gx_rec_hook` as a `DISP3DCNT` (0x04000060) RSTA entry. The default ROM
does not set `GL_BLEND`, but it does set `GL_ANTIALIAS` every frame inside the
armed window, so a captured scene-4 frame should carry a `DISP3DCNT` RSTA entry
with bit 4 set and bit 3 clear. **UNVERIFIED** for any particular capture file:
no v2 frame is committed in the repo, so the entry was not inspected. Library-
internal writes from `glInit` are explicitly *not* proxied (`main.cpp:172`
comment, §1), so the initial `GFX_CONTROL = 0` is not recorded; the frame file
declares `INITIAL_STATE_UNKNOWN`.

### 4.2 The parser exposes RSTA, the interpreter and renderer ignore it

FACT — no RSTA access anywhere in the interpreter:

```
$ grep -rn rsta common/twlhd_gx_interp.c common/twlhd_gx_interp.h
exit=1
```

FACT — and none in the arm11 program either:

```
$ grep -rn rsta arm11/source/main.c arm11/source/renderer.c arm11/include/
exit=1
```

FACT — the accessors exist but have no caller outside `twlhd_v2_parse`:

```
$ grep -rn twlhd_v2_rsta_addr arm9v2 arm11 common arm7 tools
common/twlhd_v2_parse.h:95:uint32_t twlhd_v2_rsta_addr(const TwlhdV2View *view, uint32_t index);
common/twlhd_v2_parse.c:106:int twlhd_v2_rsta_addr_in_range(uint32_t addr) {
common/twlhd_v2_parse.c:710:uint32_t twlhd_v2_rsta_addr(const TwlhdV2View *view, uint32_t index) {
common/twlhd_v2_format.h:152:int twlhd_v2_rsta_addr_in_range(uint32_t addr);
```

So the blend-enable information is carried by the file and dropped by the
consumer: the interpreter emits the per-BEGIN vertex alpha unconditionally.

FACT — the interpreter's alpha rule and the wireframe counter:

```
$ sed -n '390,398p;1070,1080p' common/twlhd_gx_interp.c
 * TO BE VERIFIED: DeSmuME's blend weight is (alpha+1)/32 (rasterize.cpp:490-494);
 * hardware/GBATEK were not checked, and alpha/31 is the alternative. Alpha 0
 * means wireframe on the DS: it is counted as alpha_wireframe_polys, but
 * outline-only drawing is NOT implemented (the polygon is emitted with 0.0).
 */
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}

        /* Apply the pending POLYGON_ATTR alpha and light mask once per BEGIN
         * group (DeSmuME gfx3d.cpp:2727-2729). */
        g->applied_alpha = g->pending_alpha;
        g->applied_light_mask = g->pending_light_mask;
        g->rgba[3] = gx_alpha_float(g->applied_alpha);
        if (g->applied_alpha == 0u) {
            g->stats->alpha_wireframe_polys++;
        } else if (g->applied_alpha < 31u) {
            g->stats->alpha_translucent_polys++;
        }
        break;
```

### 4.3 The renderer always blends, gated on nothing

FACT — the clip path sets depth, cull and alpha blend explicitly every frame; the
blend comes from the diagnostic mode table, not from any capture state:

```
$ sed -n '1534,1550p' arm11/source/renderer.c
     * run first or on citro3d defaults (Run 14: default blend does not blend
     * on the console — docs/HARDWARE_TEST_PROCEDURE_3DS.md Run 14). */
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    {
        const TwlhdDiagConfig *diag = twlhd_diag_config(g_diag_mode);
        TwlhdAlphaTestConfig at = twlhd_alpha_test_disabled();

        /* Mode 0: alpha blend + primary colour/alpha (identical to old path).
         * Mode 1: opaque + grey=fragment-alpha (alpha leak / fragment-vs-vertex
         *         alpha check).
         * Mode 2: alpha blend + constant alpha 0x80 (blend-register check). */
        C3D_AlphaBlend(diag->blend.colorEq, diag->blend.alphaEq,
                       diag->blend.srcColor, diag->blend.dstColor,
                       diag->blend.srcAlpha, diag->blend.dstAlpha);
        C3D_AlphaTest(at.enable, at.func, at.ref);
```

FACT — mode 0 (the default) is standard source-alpha blending:

```
$ sed -n '35,50p' arm11/include/twlhd_diag_config.h
        /* Mode 0 "normal": explicit alpha blend, TexEnv passes primary vertex
         * colour/alpha through unchanged. Function is GPU_REPLACE for both
         * channels, so the RGB operands and the alpha source/operand are the
         * identity/default values; the constant is reset to a known value so
         * a mode-1/2 constant cannot leak into mode 0. */
        [0] = {
            .mode = 0,
            .blend = {
                .colorEq = GPU_BLEND_ADD,
                .alphaEq = GPU_BLEND_ADD,
                .srcColor = GPU_SRC_ALPHA,
                .dstColor = GPU_ONE_MINUS_SRC_ALPHA,
                .srcAlpha = GPU_SRC_ALPHA,
                .dstAlpha = GPU_ONE_MINUS_SRC_ALPHA,
            },
            .rgb_op = {
```

FACT — the blend file itself records that the factors are a choice, not a
console measurement (and its Run-14 sentence is superseded; see §5.1):

```
$ sed -n '1,13p' arm11/include/twlhd_blend_config.h
// SPDX-License-Identifier: Apache-2.0
/*
 * Header-only, pure-data blend/alpha-test configurations for the clip-space
 * render path and the DEBUG render path.
 *
 * HARDWARE UNVERIFIED: blend equations/factors are the standard PICA
 * "modulative/translucent" choice, not a console measurement. See
 * docs/HARDWARE_TEST_PROCEDURE_3DS.md section 12 Run 14 (lines 566-622):
 * the renderer never set blend state; on the console alpha 16/31 and 4/31
 * quads drew OPAQUE and the alpha-0 quad was not drawn. Run 15 approval is
 * still required to confirm this explicit blend state matches hardware.
 */
#pragma once
```

### 4.4 What the render produces for the four quads

- `alpha 31` → `gx_alpha_float` returns `1.0` → opaque red. Matches every
  reference (FACT from the `eq4r` §8 census: hardware/ours both `(255,0,0)`).
- `alpha 16` → `(16+1)/32 = 0.53125` → blended with the `(16,16,24)` clear.
  `eq4r` §8 census: ours `(7,142,11)`.
- `alpha 4` → `(4+1)/32 = 0.15625` → ours `(13,13,60)`.
- `alpha 0` → `0.0` → blend weight 0 → the fragment equals the destination;
  `eq4r` §8 census has **no** white entry for ours. The outline is not rendered
  anywhere: `docs/EQUALITY_CHECK_DESIGN.md:227` lists "Wireframe/outline-only
  alpha 0" as "known not implemented".

Cited FACT (`docs/reports/eq4r-static-scenes-hw-compare.md` §8, quoted there
from the three `colour_histogram.py` runs; not re-run here) — the same scene
from hardware, harness and ours:

```
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
(0, 0, 0) 92056
(0, 0, 255) 1372
(255, 0, 0) 892
(0, 255, 0) 560
$ python3 tools/ds_scale_test/colour_histogram.py /tmp/opencode/eq4r/harness_s4.png
(0, 0, 0) 47208
(0, 0, 251) 855
(251, 0, 0) 575
(0, 251, 0) 384
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw_scenes/ours_s4_st30_top400x240.png
(16, 16, 24) 92965
(13, 13, 60) 1411
(255, 0, 0) 981
(7, 142, 11) 642
```

Read directly: hardware and harness have pure green/blue at full brightness;
ours has the blended `(7,142,11)` / `(13,13,60)` and no white.

## 5. Verdict, per mechanism

### 5.1 (1) Alpha-blend enable — `DISP3DCNT` bit 3

- **SOURCE-READ (third-party libnds header):** `GL_BLEND = (1<<3)` is the
  "enable/disable alpha blending" bit; `glEnable` ORs it into `GFX_CONTROL` (§2).
- **SOURCE-READ (third-party DeSmuME):** its rasteriser passes
  `DISP3DCNT.EnableAlphaBlending` into the blend and, when it is clear,
  `outDst = inSrc` — i.e. an `alpha<31` source is written opaque (§3).
- **HYPOTHESIS:** the DS does the same, so with the bit clear the hardware paints
  the alpha-16 and alpha-4 quads opaque. Support: the hardware capture paints
  them at full brightness (`eq4r` §8 census) and the independent DeSmuME agrees
  (`eq4r` §5.9, harness `(0,251,0)`/`(0,0,251)`); the mechanism itself is
  DeSmuME's reading, not a hardware register read. `eq4r` §7 already states this
  as a HYPOTHESIS.
- **FACT:** our ROM leaves the bit clear (`grep GL_BLEND` → no hits; `glInit`
  zeroes `GFX_CONTROL`) and our renderer blends regardless (§4).
- **Precision note:** the DS-side "opaque" observation is a *digital capture-
  board* measurement (`eq4r` §2: passive board, 24-bpp decoder), not a camera
  photo, so the Run 17 correction — which retracts the "console does not blend"
  readings of Runs 14-16 as camera-photo misreadings — does **not** apply to it.
  Run 17 is about *our 3DS renderer*, and it confirms that renderer blends
  correctly on the console; that is the opposite side of this comparison.

### 5.2 (2) `POLY_ALPHA` 0 = wireframe / outline

- **SOURCE-READ (third-party header):** `POLY_ALPHA` doc: "set to 0 for wireframe
  mode" (§2).
- **SOURCE-READ (third-party DeSmuME):** `GFX3D_IsPolyWireframe` is
  `Alpha == 0` (§3); its *software* rasteriser never calls it and instead drops
  alpha-0 fragments (`shaderOutput.a == 0` early return; `alphaBlend` returns on
  `inSrc.a == 0`), so DeSmuME draws no outline (§3).
- **FACT:** hardware draws an outline. `eq4r` §7 (quoted): "the hardware draws
  the alpha-0 quad's outline … the capture has 2479 non-background DS pixels
  against 1944 and starts 10 DS rows higher (62 vs 72)"; `eq4r` §2 quantifies
  "same 64 bright pixels in DS rows 62-71" in all three captures.
- **FACT:** our chain does not draw one: alpha 0 → `0.0` →
  `docs/EQUALITY_CHECK_DESIGN.md:227` "Wireframe/outline-only alpha 0 … known
  not implemented".
- **UNVERIFIED:** the outline's colour source on hardware (polygon colour vs the
  `GL_OUTLINE`/edge-colour mechanism). `eq4r` measured a bright (white-quad)
  outline but did not separate the two.

### 5.3 (3) What the chain carries

| State | Recorded? | Carried in the v2 view? | Consumed? |
|---|---|---|---|
| `DISP3DCNT` blend enable | Yes, if written by ROM code through the shim (RSTA whitelist) — SOURCE-READ §4.1 | Yes (`view->rsta`, accessors exist) | **No** — interpreter and renderer never read RSTA (FACT §4.2) |
| `POLY_ALPHA` | Yes (`GFX_POLY_FORMAT` is a GX command word) | Yes | Yes — latched at BEGIN, emitted as `rgba[3]` |
| `POLY_ALPHA == 0` wireframe | The raw 0 is carried | Yes | Only counted (`alpha_wireframe_polys`); drawn as alpha `0.0`, no outline |

So the gap is not "`DISP3DCNT` is never recorded" — the recording path exists and
the whitelist covers the register — it is that **no consumer derives or applies
the blend-enable state**, and the renderer's blend is unconditional. The alpha-0
outline is a different gap: the information needed to draw it (a polygon that is
wireframe plus its edges) is carried, but no edge pass exists.

### 5.4 Answer for this ROM and for the class

- **This ROM (FACT):** our render differs from the DS — opaque-vs-blended for
  alpha 16/4, outline-vs-absent for alpha 0 (`eq4r` §6/§8).
- **Is our render wrong? (HYPOTHESIS, mechanism SOURCE-READ):** yes for the
  blend-disabled class — a program that writes `POLY_ALPHA<31` while leaving
  `DISP3DCNT` bit 3 clear gets opaque polygons on the DS and blended polygons
  from us. Both the alpha-0 outline and the always-on blend are defects for this
  class.
- **Is the ROM unrepresentative? (HYPOTHESIS):** yes in the sense that it pairs
  a translucent request (`POLY_ALPHA<31`) with a disabled blend engine — a state
  combination the demo ROMs in `arm9v2/` never use (`grep GL_BLEND` over
  `arm9v2/` is empty), whereas the alpha-test ROM of `eq4h` §1.1 *does*
  `glEnable(GL_BLEND)`. Whether real DS software commonly leaves the blend bit
  clear while varying `POLY_ALPHA` is **UNVERIFIED** (no corpus survey).
- **For programs that do enable blending (cited, not re-derived):** `eq4i` §5.1
  is FACT for the *green* channel of the blend-enabled `ds-alpha-test` ROM — it
  matches the DeSmuME 6-bit A2 (`(a+1)/32`) on all ten deciding quads and A1 on
  none; §5.2 has red/blue equal to the 5-bit quantisation of those values, and
  §5.4 leaves the origin of the 6-bit-G / 5-bit-R/B split UNVERIFIED. Our
  interpreter emits exactly that A2 weight (`gx_alpha_float`) and the renderer
  uses `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`. So for the blend-enabled class our
  *convention* agrees with the one hardware measurement that exists; the
  end-to-end render is UNVERIFIED beyond one console, one ROM and the green
  channel. **Do not** read `eq4i` as proof for scene 4: scene 4 never enables
  blending and its quads cannot discriminate A1 from A2 (`eq4r` §8, same
  sentence).

## 6. Proposed changes — PLAN, not applied

Nothing below is applied. File names are proposals; sizes are qualitative
because no numbers were measured for them.

### 6.1 (i) Carry the blend-enable state to the renderer — PLAN

The state is already in the file (§4.1). Two minimal carriers, both text-only
here:

1. **Renderer-side flag (recommended smallest):** in `arm11/source/main.c`,
   before feeding the interpreter, walk `twlhd_v2_rsta_count`/`twlhd_v2_rsta_addr`
   `/twlhd_v2_rsta_value` for `addr == 0x04000060`, take bit 3 of the last write
   at or before the frame's first draw (or track it per BEGIN using the RSTA
   `gx_index` ordering the accessor already exposes), and select
   `twlhd_blend_config_alpha()` vs `twlhd_blend_config_opaque()` in
   `renderer.c` instead of the unconditional mode-0 `C3D_AlphaBlend`.
   Cost: a small arm11-side state struct/field, one branch at the existing
   `C3D_AlphaBlend` call, and the already-written `twlhd_blend_config_opaque()`
   helper. **It does not change the public vertex-alpha stream**, so the
   `gx_interp` goldens and `tools/gx_interp_oracle.py` stay as they are.
   **Update (2026-11-25):** implemented as EQ-4x; see
   `docs/reports/eq4x-blend-gate.md`.
2. **Interpreter-side flag:** derive the same bit inside
   `twlhd_gx_interp.c` from `view->rsta` and add a per-polygon flag to the
   emitted plan. This changes the interpreter's output contract and therefore
   requires updating (not weakening) the C goldens and the Python oracle, and it
   forces the renderer to keep two blend states anyway. Larger, and it touches
   the tests this task must not weaken.

Semantics to pin in either case: when the enable is clear, `alpha<31` must be
drawn opaque; `alpha==31` is already opaque; `alpha==0` is the wireframe case
(6.2), independent of the enable.

### 6.2 (ii) Alpha-0 outline drawing — size only, NOT implemented

What it needs, from the sources read here:

- A per-polygon wireframe flag reaching the renderer (the interpreter already
  knows `applied_alpha == 0` and counts it; today it only emits alpha `0.0`).
- Edge extraction for the primitive types the demo emits (quads today; the
  general case is triangles/strips), producing line segments instead of, or in
  addition to, filled triangles.
- A second render pass in `arm11/source/renderer.c` with `GPU_LINES` (citro3d
  line primitives) and the same attribute layout, plus its own blend/depth
  state. One-dot polygons and `GL_OUTLINE` edge colouring are further, separate
  items (`docs/EQUALITY_CHECK_DESIGN.md:227`).
- New goldens for the edge stream (C unit tests plus the Python oracle) and a
  hardware capture to check line width/colour, which is not part of this task.

It spans the interpreter output contract, the renderer's draw pass and the
golden tests; it is **not** a cheap follow-up and is not implemented here. The
measured target (from `eq4r` §2) is a wireframe outline of the alpha-0 quad, not
a filled white quad.

### 6.3 (iii) Hardware falsification test — PLAN, not run

Goal: separate "the blend-enable bit explains the opaque alpha 16/4" from "some
other mechanism". Build a scene-4 variant that enables `GL_BLEND` and compare its
hardware capture with the existing scene-4 capture.

**Build switch diff (PROPOSED, not applied).** Modelled on the existing `SCENE`
block (`arm9v2/Makefile:139-151`), appended after it:

```
--- a/arm9v2/Makefile
+++ b/arm9v2/Makefile
@@ -150,6 +150,23 @@
 endif
 endif
 
+#---------------------------------------------------------------------------------
+# Optional blend-enable build: BLEND=1 adds glEnable(GL_BLEND) to the scene
+# draw path so a ROM can be compared with the blend bit set. Unset leaves
+# TARGET, BUILD and CFLAGS untouched (default ROMs stay byte-identical).
+#---------------------------------------------------------------------------------
+BLEND_STRIPPED	:=	$(strip $(BLEND))
+ifneq ($(BLEND_STRIPPED),)
+BLEND_OK := $(shell echo '$(BLEND_STRIPPED)' | grep -Eq '^[01]$$' && echo ok)
+ifeq ($(BLEND_OK),)
+$(error BLEND must be empty, 0 or 1 (got: $(BLEND)))
+endif
+ifneq ($(BLEND_STRIPPED),0)
+TARGET		:=	$(TARGET)-bl
+BUILD		:=	$(BUILD)-bl
+CFLAGS		+=	-DTWLHD_DEMO_BLEND=1
+# CXXFLAGS was expanded from CFLAGS with := above, so it needs the define too.
+CXXFLAGS	+=	-DTWLHD_DEMO_BLEND=1
+endif
+endif
+
 #---------------------------------------------------------------------------------
 # Optional fixed-angle build: STATIC_ANGLE=0..359 replaces the frame-counter
```

**Source hook diff (PROPOSED, not applied).** In `arm9v2/source/demo_frame.cpp`,
next to the per-frame state re-issue (after line 352 `glEnable(GL_ANTIALIAS);`):

```
--- a/arm9v2/source/demo_frame.cpp
+++ b/arm9v2/source/demo_frame.cpp
@@ -350,6 +350,9 @@
     /* Re-issue all state needed to replay this frame independently. */
     glEnable(GL_ANTIALIAS);
+#ifdef TWLHD_DEMO_BLEND
+    /* Falsification build: enable DS alpha blending for scene 4. */
+    glEnable(GL_BLEND);
+#endif
     glClearColor(0, 0, 0, 31);
```

Build command (PLAN): `make -C arm9v2 SCENE=4 STATIC_ANGLE=30 BLEND=1` → a ROM
named with the `-s4-st30-bl` suffixes; capture it exactly as `eq4r` §2 captured
`twlhd-arm9v2-s4-st30.nds` (same console, same board, capture `a`).

**What falsifies the mechanism.** With `GL_BLEND` set, if the missing enable is
the cause, the alpha-16 and alpha-4 quads must stop being full brightness and
dim toward the black backdrop by the source-alpha weight. Expected pixel values
are the `eq4h` §2 rows for a white source over a black backdrop (every channel
behaves the same, so the green source of alpha 16 and the blue source of alpha 4
use the same numbers). Cited FACT (`docs/reports/eq4h-alpha-convention-test.md`
§2, `python3 tools/ds_alpha_test/expected.py`, quoted there; not re-run here),
columns in variant order `5bit_round,5bit_floor,8bit_round,8bit_floor,6bit_round`:

```
a= 4 A1=33/33/33/32/32 A2=41/33/40/39/36 sep=5bit_round=8/5bit_floor=0/8bit_round=7/8bit_floor=7/6bit_round=4
a=16 A1=132/132/132/131/130 A2=132/132/135/135/134 sep=5bit_round=0/5bit_floor=0/8bit_round=3/8bit_floor=4/6bit_round=4
```

- alpha 16: the 6-bit A2 column is `134`; the A1 5-bit columns are `132`.
  `eq4i` §5.1 measured hardware green matching the 6-bit A2 model, so **134** is
  the prediction.
- alpha 4: the 6-bit A2 column is `36`; the A1 5-bit columns are `33`. A2
  predicts **36**.

Confirmation: alpha-16 green and alpha-4 blue are dimmed and near the A2 column
→ the blend-enable bit explains the opaque capture, and our chain's always-on
blend is the defect. Refutation: they stay at full brightness with the enable
set → the opacity has another cause (e.g. the DS ignores the enable for these
quads, or the capture path clamps) and the hypothesis is wrong.

Also record the alpha-0 quad in the same capture. Prediction from the separation
in §5: the outline should be unaffected by the blend enable (the wireframe path
is a different mechanism). If enabling blending removes or fills it, the two
mechanisms interact and the separation in §5 must be revised.

Not covered by this test: whether our chain *consumes* the new flag (this is a
DS-side falsification only), other consoles, other angles, textured polygons, the
A2-vs-A1 red/blue origin.

Implemented as EQ-4v: `BLEND=1` Makefile switch, guarded `glEnable(GL_BLEND)`,
tests in `tools/test_arm9v2_blend.py`, and the capture plan in
`docs/reports/eq4v-blend-rom.md`.

**Follow-up (EQ-4w, 2026-11-25):** the ROM was run on hardware (one console, one
capture session): alpha-16 green `255` -> `134` (prediction 134) and alpha-4
blue `255` -> `33` (prediction 36; the 3-level difference is consistent with
EQ-4i's R/B 5-bit quantisation as HYPOTHESIS only), so this falsification test
supports the blend-enable mechanism on that console/session. Report:
`docs/reports/eq4w-blend-hw-result.md`; tool: `tools/eq4w_blend_hw/`.

## 7. Tests

Report-only round: no production code changed, so **no test was added, modified
or weakened**, and nothing was wired into `make test`. The proposed changes in
§6 would each need tests if implemented (see the size notes there).

## 8. Requirement traceability

- **REQ 1(a) demo GL setup, `GL_BLEND`/`DISP3DCNT`/`POLY_FORMAT` bits for scene 4,
  quoted lines** — §1 (five FACT blocks: data, draw path, `grep GL_BLEND`
  empty, `grep glEnable`, init and per-frame state).
- **REQ 1(b) libnds behaviour for `glEnable(GL_BLEND)`/`glPolyFmt`, marked
  third-party SOURCE-READ** — §2 (installed `$DEVKITPRO` header and the
  out-of-tree libnds source; ≤ a few relevant lines; no file copied in).
- **REQ 1(c) DeSmuME blend-enable and alpha-0 wireframe, marked its reading, not
  hardware truth** — §3 (`rasterize.cpp:477-497`, `:740-748`, `:770-773`,
  `gfx3d.cpp:4177-4186`, plus the software-rasteriser grep).
- **REQ 1(d) our chain: recorder, interpreter, renderer, alpha-0** — §4 (shim
  port, recorder append, whitelist, `rsta` greps, `gx_alpha_float`, BEGIN
  application, renderer blend calls).
- **REQ 2 labelled FACT/SOURCE-READ/HYPOTHESIS/UNVERIFIED verdict, and the
  consequence for blend-enabled programs cited from eq4h/eq4i not re-derived** —
  §5 (5.1-5.4), with the `eq4i` citation in 5.4 and the Run 17 precision note.
- **REQ 3 proposals: (i) blend-enable carrier, (ii) alpha-0 outline sized not
  implemented, (iii) falsification plan with the `BLEND=1` Makefile diff and
  eq4h/eq4i numbers** — §6 (6.1, 6.2, 6.3). All marked PLAN.
- **REQ 4 report path and append-only pointers** — this file plus one line in
  `docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ_SCENES.md` and at the end of
  `eq4r-static-scenes-hw-compare.md` §7.
- **REQ 5 rules: quoted commands, no home/user/host data, no inline `python3`
  expressions, no
  production-code change, diff budget, tests untouched** — §1-§6 commands are
  quoted with output; paths use `$HOME`/`$DEVKITPRO`/`<desmume>`; §9 scans for
  home paths; §10 quotes the diff size; §7 states no test changed; no file
  outside the four docs changed (§11 `git status`).

## 9. Privacy

The report contains no absolute home-directory path, user name, e-mail address,
host name or key material. Third-party paths are written as `$HOME/3ds/...` and
`$DEVKITPRO/...`. No ROM, BIOS or media file is added. The scan is in §11.

## 10. Size and scope audit

Commands are quoted in §11 (run after the final edit).

- Diff budget: `git add -N . && git diff main | wc -c` (quoted in §11), target
  `< 90000`.
- Production directories untouched: `git diff --stat` shows only the four doc
  files (quoted in §11).
- Home-path scan: `grep -rn -E '/(home|Users)/'` over the touched docs is empty
  (quoted in §11).

## 11. Audit command output

FACT — the whole change is these four documentation files:

```
$ git diff --stat main
 docs/EQUALITY_CHECK_DESIGN.md                 |   2 +
 docs/EQ_SCENES.md                             |  10 +
 docs/reports/eq4r-static-scenes-hw-compare.md |   2 +
 docs/reports/eq4u-scene4-alpha-why.md         | 849 ++++++++++++++++++++++++++
 4 files changed, 863 insertions(+)
```

FACT — no production directory appears in the worktree status:

```
$ git status --short
 M docs/EQUALITY_CHECK_DESIGN.md
 M docs/EQ_SCENES.md
 M docs/reports/eq4r-static-scenes-hw-compare.md
 A docs/reports/eq4u-scene4-alpha-why.md
```

FACT — the home-path scan over the four touched files is empty:

```
$ grep -rn -E '/(home|Users)/' docs/reports/eq4u-scene4-alpha-why.md docs/EQUALITY_CHECK_DESIGN.md docs/EQ_SCENES.md docs/reports/eq4r-static-scenes-hw-compare.md
exit=1
```

FACT — diff size against `main`, run after the final edit:

```
$ git add -N . && git diff main | wc -c
43793
```

## 12. What this does not prove

- That the DS blends exactly like DeSmuME when the enable is set: the A2 model
  is DeSmuME's and `eq4i` measures it only on the green channel of one ROM on
  one console; red/blue and the end-to-end path are UNVERIFIED.
- That the DS draws alpha<31 opaque when the bit is clear *because* of the bit:
  HYPOTHESIS from hardware-capture + DeSmuME agreement, not a register read.
- The outline colour mechanism, line width, and whether AA edges interact with
  it.
- Whether real DS software commonly leaves the blend bit clear (no corpus
  survey).
- Anything beyond this console/session, other scenes, or textured polygons.
