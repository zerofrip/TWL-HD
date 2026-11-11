# DS GX research: POLYGON_ATTR, primitive assembly, quad rasterisation

Status: research only. No source, test or Makefile change accompanies this file.
Answers item (b) of `docs/PHASE5B_PLAN.md` section 8 (`PHASE5B_PLAN.md:220`);
it does not implement 5B-0.

Sources: DeSmuME checkout `/home/one/3ds/desmume`, commit `fc504ee0` (2026-10-25),
whose line numbers are used below; devkitPro libctru headers under
`/opt/devkitpro/libctru/include/3ds/gpu/`. Labels follow
`docs/PHASE5A_V2_RENDERER_DESIGN.md:30`: **VERIFIED** = read at the cited line,
**UNVERIFIED** = not checked against hardware/register docs, **CHECK** = needs
verification. **GBATEK was not consulted in this run**; see section E.

## A. DS POLYGON_ATTR (cmd 0x29)

**VERIFIED:** DeSmuME defines the register as a `u32 value` union with bit fields
(`gfx3d.h:203-287`), ending `} POLYGON_ATTR; // 0x040004A4` (`gfx3d.h:287`).
Bit table **VERIFIED** from `gfx3d.h:208-231`:

| Bit(s) | Field | Meaning (header text) | Source |
|---|---|---|---|
| 0 | `Light0` | Light 0 enable | `gfx3d.h:208` |
| 1 | `Light1` | Light 1 enable | `gfx3d.h:209` |
| 2 | `Light2` | Light 2 enable | `gfx3d.h:210` |
| 3 | `Light3` | Light 3 enable | `gfx3d.h:211` |
| 4-5 | `Mode` | 0=Modulate, 1=Decal, 2=Toon/Highlight, 3=Shadow | `gfx3d.h:212-216` |
| 6 | `BackSurface` | Back surface 0=Hide, 1=Render | `gfx3d.h:217` |
| 7 | `FrontSurface` | Front surface 0=Hide, 1=Render | `gfx3d.h:218` |
| 8-10, 21-23, 30-31 | — | unused | `gfx3d.h:220,228,231` |
| 11 | `TranslucentDepthWrite_Enable` | 0=Keep, 1=Replace depth | `gfx3d.h:221` |
| 12 | `FarPlaneIntersect_Enable` | 0=Hide, 1=Render/clipped | `gfx3d.h:222` |
| 13 | `OneDotPolygons_Enable` | 0=Hide, 1=Render | `gfx3d.h:223` |
| 14 | `DepthEqualTest_Enable` | 0=Less, 1=Equal | `gfx3d.h:224` |
| 15 | `Fog_Enable` | 0=Disable, 1=Enable | `gfx3d.h:225` |
| 16-20 | `Alpha` | 5-bit alpha | `gfx3d.h:227` |
| 24-29 | `PolygonID` | 6-bit polygon ID | `gfx3d.h:230` |

**VERIFIED:** bits 6-7 are also a 2-bit `SurfaceCullingMode` (`gfx3d.h:265-269`):
`0=Cull front and back, 1=Cull front, 2=Cull back, 3=No culling`. Culling and the
"render this face" bits are therefore one encoding: bit 6 = render back, bit 7 =
render front.

**VERIFIED:** alpha 0 = wireframe, 31 = opaque, otherwise translucent
(`return (p.attribute.Alpha == 0);`, `gfx3d.cpp:4179`; `== 31`, `gfx3d.cpp:4184`;
combined at `gfx3d.cpp:4193`). Alpha is a polygon property, not a vertex one.

**VERIFIED latching:** the command stores only a pending value —
`regPolyAttrPending.value = param; // Only applied on BEGIN_VTXS`
(`gfx3d.cpp:2681`); BEGIN copies it to applied and passes it to the engine
(`gfx3d.cpp:2729-2731`); the engine keeps it in `_polyAttribute` (`gfx3d.cpp:1424`)
and stamps each completed polygon (`gfx3d.cpp:1786`). **CHECK:** a mid-BEGIN
POLYGON_ATTR write is therefore ignored until the next BEGIN; DeSmuME's own comment
says "This won't be activated until the next begin" (`gfx3d.cpp:2676`).

**VERIFIED DeSmuME cull rule:** facing is a shoelace-style sum over the projected
polygon, computed after the viewport transform and its y flip
(`vtx64.y = (192LL << 16) - vtx64.y;`, `gfx3d.cpp:3310`); `facing < 0` is
back-facing (`gfx3d.cpp:3349`). Visibility is a lookup keyed by (back-facing, mode)
(`gfx3d.cpp:3352-3363`):

```
static const bool visibleFunction[2][4] = {
    //always false, backfacing, !backfacing, always true
    { false, false, true, true },
```

**VERIFIED caveat:** DeSmuME warns "due to a late change of a y-coord flipping, our
winding order is wrong" (`rasterize.cpp:24-26`), so its `facing < 0` sign is an
emulator convention, not a hardware measurement. **UNVERIFIED:** the hardware
front/back sign. **CHECK** against GBATEK before encoding any cull decision.

**Alpha special values (VERIFIED in DeSmuME, hardware UNVERIFIED):** alpha 0 is
discarded, 31 is opaque; blending uses weight `alpha+1` over 32
(`const u8 alpha = inSrc.a + 1;`, `invAlpha = 32 - alpha`, `>> 5`;
`rasterize.cpp:490-494`). Alpha is never interpolated: it is loaded from the polygon
attribute (`polyAttr.Alpha`, `rasterize.cpp:825`; `(float)polyAttr.Alpha / 31.0f`,
`rasterize.cpp:844`) with a zero per-pixel step (`rasterize.cpp:864`).
**UNVERIFIED:** the hardware blend weight at the extremes.

## B. BEGIN_VTXS assembly, types 0-3

Mode is `param & 3` (`gfx3d.cpp:1416`), same as ours (`common/twlhd_gx_interp.c:648`).
DeSmuME resets strip state on every BEGIN (`_isGeneratingFirstPolyOfStrip = true`,
`gfx3d.cpp:1431-1432`); END clears only the count
(`_inBegin = false; _vtxCount = 0;`, `gfx3d.cpp:1435-1438`). Strips cannot leak
across BEGINs, matching our reset (`common/twlhd_gx_interp.c:649-651`).

**VERIFIED / DIFFERENT:** DeSmuME does not split quads at assembly time. Mode 1
becomes one `POLYGON_TYPE_QUAD` with four indices (`gfx3d.cpp:1661-1676`); mode 3
also emits a `QUAD`, with corners reordered to `(0,1,3,2)`:
`poly.vertIndexes[2] = this->_vtxIndex[3]; // Note that the vertex index differs here.`
(`gfx3d.cpp:1726-1735`). We emit `(0,1,2)+(0,2,3)` for mode 1
(`common/twlhd_gx_interp.c:383-388`) and `(0,1,3)+(0,3,2)` for mode 3
(`common/twlhd_gx_interp.c:421-426`) — exactly a split of DeSmuME's quad along its
0-3 diagonal.

**VERIFIED / SAME winding:** triangle strips alternate the index order as we do.
DeSmuME emits the first triangle in window order, then replaces index 1 (or index 0)
before each subsequent triangle (`gfx3d.cpp:1694-1714`), giving
`(v0,v1,v2)`, `(v2,v1,v3)`, `(v2,v3,v4)`, ... — the same as our `(0,1,2)` then
`(1,0,2)` on the rolling window (`common/twlhd_gx_interp.c:398-413`). Both keep the
last two vertices.

**VERIFIED / DIFFERENT at END:** DeSmuME's END leaves the strip window and toggle
alone (only `_vtxCount`, `gfx3d.cpp:1435-1438`); ours resets
`prim_count/prim_tri_index/prim_emitted` (`common/twlhd_gx_interp.c:360-362`). No
observable difference on a well-formed stream because the next BEGIN clears both.

**VERIFIED divergence:** `AddCurrentVertexToList` has no BEGIN check
(`gfx3d.cpp:1521-1530`), so DeSmuME assembles a vertex written outside BEGIN/END with
the previous `_vtxFormat`; we drop it and count `ignored_outside_begin`
(`common/twlhd_gx_interp.c:448-450`). Hardware behaviour is **UNVERIFIED** (E4).

| Mode | DeSmuME output | Our output | Verdict |
|---|---|---|---|
| 0 triangles | `POLYGON_TYPE_TRIANGLE` per 3 (`gfx3d.cpp:1644-1658`) | tri per 3 (`common/twlhd_gx_interp.c:368-377`) | SAME geometry |
| 1 quads | one `QUAD` (`gfx3d.cpp:1661-1676`) | tris `(0,1,2)+(0,2,3)` (`common/twlhd_gx_interp.c:380-393`) | DIFFERENT carrier, same corners |
| 2 tri strip | tri polygon, alternating indices (`gfx3d.cpp:1682-1715`) | tris with alternating emit (`common/twlhd_gx_interp.c:396-415`) | SAME sequence |
| 3 quad strip | `QUAD` reordered `(0,1,3,2)` (`gfx3d.cpp:1718-1745`) | tris `(0,1,3)+(0,3,2)` (`common/twlhd_gx_interp.c:418-434`) | DIFFERENT carrier, split of same quad |

Remainders differ only in accounting: DeSmuME silently keeps the partial list in
`_vtxCount`; we report `dropped_incomplete` (`common/twlhd_gx_interp.c:342-358`).

## C. How DeSmuME rasterises and interpolates quads

**VERIFIED:** the quad survives clipping as a 4-vertex polygon. Clipping reads the
fourth index only for a quad
(`(rawPoly.type == POLYGON_TYPE_QUAD) ? &gList.rawVtxList[rawPoly.vertIndexes[3]] : NULL`,
`gfx3d.cpp:3265`) and preserves the type
(`cpType = GFX3D_GenerateClippedPoly<CLIPPERMODE>(polyIndex, rawPoly.type, ...)`,
`gfx3d.cpp:3269`). The rasteriser switches on it:
`case 4: this->_sort_verts<ISFRONTFACING, 4>(); break;` (`rasterize.cpp:1202`), in
a routine commented "can handle any convex N-gon up to octagons"
(`rasterize.cpp:1190`). A DS quad is one 4-gon fill, **not** two triangles.
**VERIFIED.**

**VERIFIED interpolation model:** attributes are premultiplied by 1/w at precalc
(`precalc.invWPosition = (1LL << W_PRECISION) / (s64)vtx.position.w;`,
`rasterize.cpp:2139`; colour at `rasterize.cpp:2140`), interpolated linearly along
each edge and scanline (`edge_fx_fl`, `rasterize.cpp:154-280`; span setup
`rasterize.cpp:819-880`), then divided by the interpolated 1/w per pixel
(`vtxColorInterpolated.r / invWInterpolated`, `rasterize.cpp:987-989`). That is
perspective-correct for colour, texture coordinates and depth. **VERIFIED.**
**UNVERIFIED:** how a non-planar quad's fourth corner is actually filled; no single
scanline model can decide that from this code.

**VERIFIED:** alpha is the one attribute not interpolated — constant
`polyAttr.Alpha` for every pixel (`rasterize.cpp:825,844`). **CHECK:** whether
hardware likewise treats polygon alpha as constant; DS COLOR masks to 15-bit RGB
(`param & 0x00007FFF`, `gfx3d.cpp:1354`), and DeSmuME forces vertex-colour alpha to 0
(`this->_vtxColor666X.a = 0;`, `gfx3d.cpp:1573`).

**Open question (C):** our split diagonal differs from DeSmuME's single 4-gon. For a
convex, planar quad the two should agree; for non-planar or non-convex quads they need
not. Whether hardware splits the quad is **UNVERIFIED** (E2), so the
`(0,1,3)/(0,3,2)` split's colour interpolation along the diagonal is **UNVERIFIED**.

## D. Facts and questions for Phase 5B-0 (alpha + cull)

- **VERIFIED:** POLYGON_ATTR has no effect today; it sits in `ignored_no_effect`
  beside NORMAL and TEX_COORD (`common/twlhd_gx_interp.c:583-597`); the plan records
  the gap (`docs/PHASE5B_PLAN.md:84-86`).
- **VERIFIED:** vertex alpha is forced to 1.0 in `set_rgba_from_rgb555`
  (`common/twlhd_gx_interp.c:290-295`, line 294) and copied through
  (`common/twlhd_gx_interp.c:318-321`); the header says "alpha is always 1"
  (`common/twlhd_gx_interp.h:24-25`, line 25).
- **VERIFIED:** RGB uses `value / 31.0f` (`common/twlhd_gx_interp.c:291-293`).
  DeSmuME's 5-bit alpha uses blend weight `(alpha+1)/32`
  (`rasterize.cpp:490-494`), so `/31` and `(a+1)/32` disagree for every value except
  31. **CHECK:** which convention 5B-0 should adopt; GBATEK was not consulted.
- **VERIFIED:** POLYGON_ATTR is per polygon and latched at BEGIN in DeSmuME
  (`gfx3d.cpp:2681`, `gfx3d.cpp:2729-2731`). **UNVERIFIED:** hardware's exact latch
  point (DeSmuME's BEGIN assumption is not a measurement).
- **VERIFIED:** cull is bits 6-7 with the four values at `gfx3d.h:265-269`, applied
  only after viewport projection against a signed area (`gfx3d.cpp:3349-3363`), never
  earlier. **CHECK:** mode 0 (cull both) has no `GPU_CULL_*` counterpart
  (`enums.h:305-307`).
- **VERIFIED:** the interp→renderer carrier is
  `renderer_set_clip_scene(vertices, count, viewport)`
  (`arm11/source/renderer.c:813-833`); only vertices and viewport cross, so alpha and
  cull have nowhere to travel until new state is added there.
- **VERIFIED:** the clip path hard-codes `C3D_CullFace(GPU_CULL_NONE);` every frame
  (`arm11/source/renderer.c:996-999`, line 999) under "HARD RULE: set render state
  explicitly on every clip frame" (`renderer.c:996-997`); depth is
  `C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);` (`renderer.c:998`). TexEnv is
  `GPU_PRIMARY_COLOR` + `GPU_REPLACE` (`renderer.c:1005-1006`) and no blend state is
  configured anywhere in the file: alpha alone will not blend today. **VERIFIED.**
- **VERIFIED enum names:** `GPU_CULL_NONE = 0`, `GPU_CULL_FRONT_CCW = 1`,
  `GPU_CULL_BACK_CCW = 2` (`/opt/devkitpro/libctru/include/3ds/gpu/enums.h:305-307`).
  **CHECK:** the header's wording ("Front, counter-clockwise") does not pin the
  winding convention; confirm against citro3d/hardware.
- **VERIFIED mapping-space fact:** `clipfix` swaps x and y with a sign
  (`x' = hh*y + cy*w`, `y' = -0.8*(hw*x + cx*w)`, `common/twlhd_clipfix.h:11-13`;
  rows `common/twlhd_clipfix.c:39-42`). Its linear 3x3 determinant is
  `0.4 * hh * hw > 0`: a rotation+scale, **not** a mirror, so winding order is
  preserved through clipfix. It does not answer whether the DS front face becomes CCW
  on PICA (E3).

## E. Open questions and demo-scene tests

1. **UNVERIFIED:** hardware POLYGON_ATTR semantics beyond DeSmuME's bit table.
   GBATEK was not consulted in this run; re-verify section A before encoding a bit.
2. **UNVERIFIED:** does hardware rasterise a quad as one polygon or two triangles, and
   how is a non-planar quad filled? Our split is a choice, not a measurement.
3. **UNVERIFIED:** the front-face winding convention after clipfix's x/y swap and the
   PICA viewport y orientation, needed for the `GPU_CULL_*` mapping.
4. **UNVERIFIED:** are vertices outside BEGIN..END ignored by hardware? DeSmuME accepts
   them (`gfx3d.cpp:1521-1530`); we drop them (`common/twlhd_gx_interp.c:448-450`).
5. **UNVERIFIED:** hardware alpha scale/blend formula; DeSmuME uses `(alpha+1)/32`
   (`rasterize.cpp:490-494`), we use `/31` (`common/twlhd_gx_interp.c:291`).
6. **UNVERIFIED:** whether hardware alpha is constant per polygon or interpolated;
   DeSmuME treats it as constant (`rasterize.cpp:825,844`).

Demo scenes that would settle 3 and 5 (no code proposed here):
- Cube with distinct front/back colours, alpha 31, run with each surface-culling value
  0-3; photograph every face to pin the enum mapping.
- One quad, four distinct vertex colours, one non-planar corner, alpha 31; checks (C).
- Same quad at alpha 16, 1 and 30; compare blended results against a reference to pin
  the alpha scale (question 5).
- A polygon emitted after END_VTXS and before the next BEGIN_VTXS; checks question 4.

## Lighting (DeSmuME gfx3d.cpp, desmume/src) — 5B-1 research, 2026-11-08

Source read: DeSmuME `gfx3d.cpp` (cited as `gfx3d.cpp:LINE`). Labels: **READ** = read from the DeSmuME source in this repository checkout; **CHECK** = not yet confirmed against GBATEK or real DS output. Nothing here is a hardware fact; DeSmuME is an emulator.

**STATUS (2026-11-08): implemented BUILD/UNIT, HARDWARE UNVERIFIED.** The
interpreter reproduces the integer equations below; host unit tests and golden
streams compare against an independent Python oracle (`tools/gx_interp_oracle.py`).
Still CHECK: GBATEK cross-check of the specular exponent/rounding; NORMAL outside
BEGIN..END (implemented as applied, unverified on hardware); initial material
colours, the half-vector zero-length branch and the shininess word-counter reset
(implemented from DeSmuME source only).

### Command decoding (READ)
- NORMAL (0x21), `SetNormal` (`gfx3d.cpp:1217-1224`): 3 signed 10-bit components (bits 0-9, 10-19, 20-29), each sign-extended then `* 8`, i.e. 1 sign + 9 fractional bits widened to 20.12 fixed point (4096 = 1.0, so a 10-bit 0x1FF = 511/512*4096).
- LIGHT_VECTOR (0x32), `SetLightDirection` (`gfx3d.cpp:1179-1193`): same 10-bit unpack; light number in bits 30-31. The direction is multiplied by the *current vector matrix* (`MATRIXMODE_POSITION_VECTOR`, upper-left 3x3) **at command time**, not at vertex time. The vector matrix is the one used in matrix mode 2 (position+vector).
- NORMAL is transformed with the same 3x3 of the current vector matrix at the NORMAL command (`gfx3d.cpp:1233-1235`); there is no renormalization.
- LIGHT_COLOR (0x33), `SetLightColor` (`gfx3d.cpp:1195-1199`): bits 0-14 = RGB555, bits 30-31 = light number.
- DIF_AMB (0x30), `SetDiffuseAmbient` (`gfx3d.cpp:1158-1167`): low 16 bits diffuse RGB555 (bits 0-14), bit 15 = "set vertex colour to diffuse" (calls `SetVertexColor(param)`), high 16 bits ambient RGB555 (bits 16-30).
- SPE_EMI (0x31), `SetSpecularEmission` (`gfx3d.cpp:1169-1173`): low 16 bits specular RGB555 with bit 15 = "use shininess table", high 16 bits emission RGB555.
- SHININESS (0x34), `SetShininess` (`gfx3d.cpp:1201-1215`): 32 words, each packing 4 table bytes; 128 entries total; the table is applied only after the 32nd word.
- Light enable mask: `regPolyAttrApplied.LightMask` (POLYGON_ATTR bits 0-3, `gfx3d.h:208-211`), applied on BEGIN_VTXS (`gfx3d_glBegin`, `gfx3d.cpp:2727`).

### Per-vertex lighting equation (READ, `gfx3d.cpp:1217-1331`), run at every NORMAL command

Colour channels are 5-bit integers. For each enabled light i (0..3), with N = transformed normal (20.12), L = transformed light direction (20.12):
1. `fixed_diffuse = max(0, -dot(L, N))`, where `dot` = `sfx32_shiftdown(sum fx32_mul)` (20.12 result, `gfx3d.cpp:476-483`).
2. Half vector: `H = normalize(L + (0, 0, -4096))` (line of sight (0,0,-1), `gfx3d.cpp:2117-2150`); the length uses `sqrt(dot(h,h)) << 6` and a shifted integer division (not an exact float normalize). The code uses `-H` (`_vecLightDirectionHalfNegative`).
3. `dot_h = dot(-H, N)`; if `dot_h > 0`: `shin = 2*mul_fixed32(dot_h,dot_h) - 4096` (cos(2a) from cos(a); comment says "gbatek is almost right but not quite"), else 0; clamped to [0, 4095].
4. If SPE_EMI bit 15 is set: `shin = shininessTable[shin >> 5] << 4` (7-bit index, 8-bit entries, back to 12 fractional bits).
5. Per channel c: `spec = (specular[c] * light[c] * shin) >> 17`; `diff = (diffuse[c] * light[c] * fixed_diffuse) >> 17`; `amb = (ambient[c] * light[c]) >> 5`; `colour[c] += spec + diff + amb`.
The vertex colour starts as the emission colour and is clamped with `min(31, x)` per channel (no lower clamp needed); the result **replaces** the current vertex colour (`SetVertexColor`, alpha untouched in this path). Without NORMAL commands the colour is whatever COLOR / DIF_AMB bit 15 set.

### Consequences for the 3DS port (analysis, not DeSmuME facts)
- Lighting is per NORMAL command on the geometry engine, so a CPU implementation in the interpreter fits: latch material/lights/shininess, and at each NORMAL compute the colour that the following VTX commands will inherit. TwlhdGxVertex stays 32 B.
- Needed interpreter state: vector matrix (3x3) as of the NORMAL / LIGHT_VECTOR command, 4 light directions already transformed at LIGHT_VECTOR time, 4 light colours, diffuse/ambient/specular/emission, a 128-entry shininess table, light mask from the latched POLYGON_ATTR.
- CHECK items (state after 5B-1 implementation, BUILD/UNIT only): (a) vector-matrix semantics — resolved from source: the interpreter keeps a separate vector matrix with matrix mode 2 dual-applying except SCALE, and mode 1 leaving it untouched; (b) `sfx32_shiftdown`/`fx32_mul` rounding — resolved from source (64-bit product, arithmetic `>>12`, saturating 3x3 accumulate); (c) GBATEK cross-check of the specular exponent — **still CHECK** (DeSmuME comment: GBATEK is slightly off); (d) integer arithmetic order (>>17, >>5) — reproduced and golden-tested; (e) light mask latched at BEGIN while NORMAL reads the applied mask — implemented; (f) NORMAL outside BEGIN/END — implemented as applied (global colour latch in DeSmuME), **still CHECK** on hardware.
- Test plan hint (UNIT): an independent Python oracle that re-implements the integer equations above, golden streams for one light diffuse-only, ambient-only, emission-only, specular with and without the shininess table, light mask bits, clamping at 31; mutation tests for each shift constant.

## Textures (5B-2 research)
Status: research plus the host-side 5B-2b interpreter (BUILD/UNIT only; no
EMULATOR or HARDWARE run). Sources are the DeSmuME checkout cited at the top
(commit `fc504ee0`); GBATEK was not consulted. Labels: **VERIFIED** = read at
the cited line, **READ** = derived from the cited source, **UNVERIFIED** = not
checked against hardware or register docs.

### TEXIMAGE_PARAM (0x2A) and PLTT_BASE (0x2B)
**VERIFIED:** command ids `GFX3D_TEXIMAGE_PARAM = 0x2A`, `GFX3D_PLTT_BASE = 0x2B`
(`gfx3d.h:57-58`); IOREG words at `0x040004A8` / `0x040004AC` (`gfx3d.h:508-509`).
Both latch immediately in their handlers (`gfx3d.cpp:1375-1395`), unlike
POLYGON_ATTR which is deferred to BEGIN_VTXS.

Bit table, little-endian branch (`gfx3d.h:289-358`):

| Bits | Field | Meaning |
|---|---|---|
| 0-15 | `VRAMOffset` | texture data base, in units of 8 bytes |
| 16 | `RepeatS_Enable` | 0 = clamp, 1 = repeat |
| 17 | `RepeatT_Enable` | 0 = clamp, 1 = repeat |
| 18 | `MirroredRepeatS_Enable` | flip S, interacts with bit 16 |
| 19 | `MirroredRepeatT_Enable` | flip T, interacts with bit 17 |
| 20-22 | `SizeShiftS` | width = 8 << N (8..1024) |
| 23-25 | `SizeShiftT` | height = 8 << N (8..1024) |
| 26-28 | `PackedFormat` | 0 none, 1 A3I5, 2 I2, 3 I4, 4 I8, 5 4x4, 6 A5I3, 7 16bpp |
| 29 | `KeyColor0_Enable` | use palette index 0 as transparent |
| 30-31 | `TexCoordTransformMode` | 0 none, 1 texcoord, 2 normal, 3 vertex |

**VERIFIED:** bits 16-19 are also exposed as a 4-bit `TextureWrapMode`
(`gfx3d.h:346-357`) which the software rasteriser consumes
(`rasterize.cpp:527`). Decoder (`rasterize.cpp:1814-1843`): bit 0 = repeat S,
bit 1 = repeat T, bit 2 = flip S, bit 3 = flip T. A flip bit with its repeat bit
clear behaves as clamp (cases 4, 8 and 0xC are identical to case 0).

**VERIFIED:** `KeyColor0_Enable` becomes `_isPalZeroTransparent` only for
I2/I4/I8 (`texcache.cpp:429-436`); A3I5/A5I3/4x4/16bpp ignore it.
PLTT_BASE is stored as a raw `u32` (`gfx3d.cpp:1392-1395`) and used without
masking: palette byte address = `PLTT_BASE << 3` for I2, `PLTT_BASE << 4` for all
other formats (`texcache.cpp:438`).

### Texture memory addressing and capture payload
**VERIFIED** from `TextureStore::TextureStore` (`texcache.cpp:412-499`):
- width = `8 << SizeShiftS`, height = `8 << SizeShiftT` (`texcache.cpp:422-423`);
  DS dimensions are therefore always powers of two in 8..1024.
- `_packAddress = VRAMOffset << 3`; `_packSize = width*height*texSizes[PackedFormat] >> 2`
  (`texcache.cpp:426-427`) with `texSizes[] = {0,4,1,2,4,1,4,8}`
  (`texcache.cpp:414`), i.e. bytes/texel 1, 0.25, 0.5, 1, 0.25, 1, 2 for formats 1-7.
- texture RAM is 4 slots x 128 KB: `slot = (ofs >> 17) & 3`, physical address =
  `textureSlotAddr[slot] + (ofs & 0x1FFFF)`, wrapping across slots
  (`texcache.cpp:144-165`).
- palette RAM is 6 slots x 16 KB: `slot = (ofs >> 14) & 7`; slot values 6-7 have 5
  subtracted (wrap into 1-2) (`texcache.cpp:169-197`).
- 4x4 only: `indexBase = ((VRAMOffset & 0xC000) == 0x8000) ? 0x30000 : 0x20000`,
  `indexOffset = (VRAMOffset & 0x3FFF) << 2`, index size = `width*height >> 3`
  (`texcache.cpp:443-446`).

**READ:** minimum payload to reproduce one texture: TEXIMAGE_PARAM + PLTT_BASE
(8 B), raw texel data (`_packSize`), palette (`_paletteSize`; 0 for 16bpp,
format 5 reads a per-block palette window instead of a fixed table) and, for
4x4, the index block (`_packIndexSize`).

### TEX_COORD (0x22) and coordinate transform
**VERIFIED:** `gfx3d_glTexCoord(param)` forwards the word to
`SetTextureCoordinates2s16` (`gfx3d.cpp:2635-2639`, overload
`gfx3d.cpp:1397-1403`). It is two packed s16 values (S low half, T high half) in
s1.11.4. The setter latches `_texCoord16` and raises
`_doesTransformedTexCoordsNeedUpdate` (`gfx3d.cpp:1405-1414`).

**VERIFIED:** the transform runs in `AddCurrentVertexToList` at VTX time
(`gfx3d.cpp:1579-1611`) and is re-evaluated for every vertex in VertexSource
mode even when the flag is clear (`gfx3d.cpp:1579-1580`). Modes come from
`TexCoordTransformMode` (`gfx3d.h:124-129`):
- 0 none: `s = texCoord.s`, `t = texCoord.t` (raw latch passed on).
- 1 texcoord source (`gfx3d.cpp:1578-1593`):
  `s' = (s32)(((s64)Mul32x16(mtx[0], s) + (s64)Mul32x16(mtx[4], t) +
  (s64)mtx[8] + (s64)mtx[12]) >> 12)`, and the same with rows
  `mtx[1]/[5]/[9]/[13]` for `t'`; `s/t` are the raw sign-extended s1.11.4
  values (no promotion), the m8/m9 and m12/m13 terms are raw 20.12 addends,
  the sum is 64-bit, the shift is arithmetic `>>12`, the result is cast to
  s32 and converted to floats by `/16.0f` (same units as mode 0). A 20.12
  translation of 4096 therefore moves **+1 raw s1.11.4 unit = 1/16 texel**,
  not 1 texture unit.
- 2 normal source: rows 0/1 x (nx,ny,nz) + (s<<24, t<<24), `>> 24`.
- 3 vertex source: rows 0/1 x (vx,vy,vz) + (s<<24, t<<24), `>> 24`.

**READ:** all modes are integer fixed point with no normalisation; the texture
matrix is `MATRIXMODE_TEXTURE` as latched at VTX time, so matrix changes between
TEX_COORD and VTX are observed.

### Implementation status 5B-2b (host interpreter)

Implemented in `common/twlhd_gx_interp.c` / `.h`; **BUILD/UNIT only**, no
EMULATOR or HARDWARE verification:

- **VERIFIED (host tests only, not hardware):** TEX_COORD (0x22),
  TEXIMAGE_PARAM (0x2A) and PLTT_BASE (0x2B) are latched, each with no
  `ignored_no_effect` increment. TEXIMAGE_PARAM/PLTT_BASE are latched
  immediately; TEX_COORD and the transform mode are sampled at each VTX
  command. `twlhd_gx_interpret_ex()` reports them per output vertex in a
  parallel `TwlhdGxTexVertex` (`float s,t; uint32_t teximage_param;
  uint32_t pltt_base;`, 16 B). `TwlhdGxVertex` stays 32 B (static asserts) and
  `twlhd_gx_interpret()` is a `tex_out == NULL` wrapper.
- **READ:** TEX_COORD is two packed s16 (S low, T high), s1.11.4 -> float
  value/16.0. Format 0 (no texture / TEXIMAGE_PARAM 0) still receives an
  entry, with `teximage_param == 0`; the consumer decides.
- **VERIFIED (formula against DeSmuME `desmume/src/gfx3d.cpp:1578-1593`;
  host tests only, not hardware):** mode 1 applies texture-matrix rows 0/1 to
  the **raw** latched s1.11.4 coordinate (no `<<8` promotion):
  `s' = (s32)(((s64)Mul32x16(m[0], s) + (s64)Mul32x16(m[4], t) +
  (s64)m[8] + (s64)m[12]) >> 12)`, and the same with
  `m[1]/m[5]/m[9]/m[13]` for `t'`. The m8/m9 and m12/m13 addends are raw
  20.12 words (no FX_ONE multiply), the sum is 64-bit, the shift is
  arithmetic, the result wraps to int32 and the output floats are
  `value / 16.0f` (same units as mode 0). Column-major `m[col*4+row]`.
  A 20.12 translation of 4096 is +1 raw s1.11.4 unit = 1/16 texel, **not**
  1.0 texture unit (earlier text here was wrong; repair round 1).
- **IMPLEMENTED (BUILD/UNIT):** MTX_MODE 3 now has its own 20.12 texture
  matrix and a **one-entry shared storage slot**, verified in the DeSmuME
  checkout. PUSH copies into `_mtxStackTexture[0]` (`gfx3d.cpp:861`), POP copies
  out of it (`gfx3d.cpp:928`; an earlier note said :927, the blank line
  before it), STORE copies into it (`gfx3d.cpp:965`) and RESTORE copies out of
  it (`gfx3d.cpp:996`): all four commands share the **one** slot, and only the
  PUSH/POP depth counter is separate. The slot is initialised to identity
  (`MatrixInit(_mtxStackTexture[0])`, `gfx3d.cpp:532`), so a mode-3
  RESTORE/POP before any STORE/PUSH yields identity, not zeros. LOAD_4x4/4x3,
  MULT_4x4/4x3/3x3, IDENTITY, SCALE, TRANS, PUSH, POP, STORE and RESTORE work
  on it; the lighting vector matrix is untouched. PUSH overflows to
  `stack_errors`/`ERR_STACK` (DeSmuME sets `gxstat.se` and keeps going,
  `gfx3d.cpp:861-871`); POP ignores its parameter, restores slot 0 and reports
  underflow as `stack_errors`/`ERR_STACK` (`gfx3d.cpp:918-929`); STORE/RESTORE
  in mode 3: index ignored, always slot 0, no `ERR_STACK` (`gfx3d.cpp:950-1000`).
- **GAP:** modes 2 (normal source) and 3 (vertex source) are **not
  implemented**. Those vertices get the raw latched s,t and every VTX command
  in such a mode increments `TwlhdGxStats.tex_transform_unsupported`; the
  counter is per VTX command and is also incremented outside BEGIN..END, so it
  is a stream property.
- **GAP (known divergence, not addressed):** the projection, position and
  vector stacks still start zeroed (the `memset` in `twlhd_gx_interpret_ex`),
  whereas DeSmuME MatrixInits them: `_mtxStackProjection[0]`,
  `_mtxStackPosition[i]`, `_mtxStackPositionVector[i]` and
  `_mtxStackTexture[0]` (`gfx3d.cpp:530-533`). Only the texture slot is
  identity-initialised here (repair round 4). The zero start of the projection,
  position and vector stacks is **unpinned by any test**; it was left unchanged
  only to keep 5B-2b small.
- `tex_out[i]` describes `out[i]`: same ordering (including quad splitting and
  strip winding swaps) and the capacity check drops the triangle from both
  arrays, so no texture entry is written past `out_count`.
- Colour/lighting/alpha behaviour is unchanged.

### Texture formats 1-7
**VERIFIED:** `NDSTextureFormat` (`gfx3d.h:111-120`) and per-format unpacking
(`texcache.cpp:622-668`, signatures `texcache.h:185-191`):

| Fmt | Bits/texel | Palette entries | Alpha | Source |
|---|---|---|---|---|
| 1 A3I5 | 8 | 32 | texel bits 5-7, 0 transparent, 7 opaque | `texcache.cpp:1524-1525` |
| 2 I2 | 2 | 4 | entry opaque; index 0 optionally transparent | `texcache.cpp:429-436` |
| 3 I4 | 4 | 16 | same | |
| 4 I8 | 8 | 256 | same | |
| 5 4x4 | 2 + index block | 4 per block, reads palette RAM | block mode 0 = 3 colours + transparent | `texcache.cpp:1789-1843` |
| 6 A5I3 | 8 | 8 | texel bits 3-7, 0 transparent, 31 opaque | `texcache.cpp:1745-1746` |
| 7 16bpp | 16 | none | bit 15: 1 = opaque, 0 = transparent | `texcache.cpp:2007-2026` |

**VERIFIED:** palette entries are 16-bit RGB555 with bit 15 ignored
(`src & 0x7FFF`, `utils/colorspacehandler/colorspacehandler.h:128-131`).
The I-formats carry no texel alpha; 4x4 block modes 1 and 3 derive extra
colours from the first two (mode 1: a per-channel average as the third colour
only; mode 3: 5:3 and 3:5 blends as the third and fourth colours)
(`texcache.cpp:1805-1851`). 4x4 unpacking receives only the first 128 KB slot of texel data and warns when it overruns the slot (`texcache.cpp:646-654`).
**VERIFIED (format 5 palette):** the PAL4X4 macro (`texcache.cpp` ~1752) reads
palette RAM per block: `palAddress = PLTT_BASE<<4 + pal1offset`,
`pal1offset = (blockword & 0x3FFF)<<1`, consumed by the unpacker at
`texcache.cpp:1789-1843`. `paletteSizeList[5] = 0` (`texcache.cpp:418/441`)
sizes only the texture-cache copy, not the palette read, so it is not evidence
that 4x4 has no palette. The offset counts 16-bit entries while the unpacker
reads bytes, so the highest byte index is `0x3FFF*4 + 7 = 65,539` and a capture
therefore records a caller-chosen even palette window in `[2, 65540]` bytes
starting at byte `PLTT_BASE<<4` (the record has no start field). DeSmuME wraps
palette reads across the six 16 KiB palette slots (`(addr >> 14) & 7`), so a
decoder must treat the captured window as contiguous and error beyond it rather
than wrap; colours beyond it are a later decoder-stage error.

### Decoder (5B-2c)
**IMPLEMENTED (BUILD/UNIT only; no EMULATOR or HARDWARE verification):**
`common/twlhd_tex_decode.c` / `.h` decode one TXDS record (formats 1-7) into a
caller-owned RGBA8 buffer (row-major, top row first, R,G,B,A byte order). Pure
C11, no malloc, no libm. Every formula below was re-verified against the
DeSmuME checkout (`desmume/src/texcache.cpp`) for 5B-2c; where the earlier text
of this document disagreed with the source, the source won and the text was
corrected.

Dimensions and validation:
- `width = 8 << ((param>>20)&7)`, `height = 8 << ((param>>23)&7)`
  (`texcache.cpp:422-423`), each 8..1024, so `width*height <= 1 MiB` and the
  uint32 size computation cannot overflow.
- Texel/index/palette sizes come from `twlhd_v2_tex_sizes()` and
  `twlhd_v2_tex_palette_size_ok()`; the decoder duplicates no size formula. The
  fixed palette tables are `paletteSizeList[] = {0,32,4,16,256,0,8,0}` 16-bit
  entries (`texcache.cpp:417`), i.e. 64/8/32/512/16 bytes for formats 1/2/3/4/6.

Component expansion (`utils/colorspacehandler/colorspacehandler.cpp:113,151`):
`common/` has no integer 5-bit -> 8-bit helper (the only 5-bit paths are the
float `/31.0f` normalisations in `twlhd_vertex_convert.c:41-43` and
`twlhd_gx_interp.c:394-396`, a different consumer), so the formula is pinned in
the decoder and tested as a literal table:
- 5-bit -> 8-bit: `(c<<3)|(c>>2)` (`material_5bit_to_8bit`:
  `00 08 10 18 21 ... F7 FF`).
- 3-bit -> 8-bit: `(a<<5)|(a<<2)|(a>>1)` (`material_3bit_to_8bit`:
  `00 24 49 6D 92 B6 DB FF`; `a=2` gives 73, not 72).
- RGB555 -> RGBA8: R = bits 0-4, G = bits 5-9, B = bits 10-14, each through
  the 5-bit table; in the 32-bit 8888 word R is the low byte
  (`colorspacehandler.h:110-114,126-129`). Bit 15 of a palette entry is ignored
  by every palette path: A3I5/A5I3 mask `& 0x7FFF` (`texcache.cpp:1524,1745`),
  PAL4X4 masks `& 0x7FFF` (`texcache.cpp:1752`), I2/I4/I8 go through
  `ColorspaceConvert555To8888Opaque` which masks (`colorspacehandler.h:126-129`),
  and 16bpp goes through the same `CONVERT` macro (`texcache.cpp:50,2023`).

Per format (all verified against the checkout):
- **Format 1 A3I5** (`texcache.cpp:1505,1522-1528`): 1 byte/texel; palette
  index = bits 0-4, alpha = bits 5-7 through `material_3bit_to_8bit`; alpha 0
  keeps the RGB bytes and sets A=0, alpha 7 is opaque. `KeyColor0` ignored
  (`texcache.cpp:429-436`).
- **Format 6 A5I3** (`texcache.cpp:1730,1743-1749`): index = bits 0-2, alpha =
  bits 3-7 through `material_5bit_to_8bit`; same alpha-0/opaque rule;
  `KeyColor0` ignored.
- **Formats 2/3/4 I2/I4/I8** (`texcache.cpp:1057-1112,1341-1386,1389-1405`):
  sub-byte packing is **low bits = first texel**: I2 `*srcData & 3`, `>>2 & 3`,
  `>>4 & 3`, `>>6 & 3`; I4 low nibble then high nibble; I8 one byte per texel.
  Every palette entry is opaque. `KeyColor0` applies only when
  `_isPalZeroTransparent` is set, which `TextureStore` does only for I2/I4/I8
  (`texcache.cpp:429-436`); with it set, index 0 is written as `0x00000000`
  and all other indices as opaque palette colours.
- **Format 5 4x4** (`texcache.cpp:1752,1755-1885`): `(width/4)*(height/4)`
  blocks, row-major across the texture (inner x loop, outer y loop,
  `texcache.cpp:1763-1789`), one 32-bit texel word and one 16-bit index word
  per block. Index word: `pal1offset = (word & 0x3FFF) << 1` counts 16-bit
  entries and `mode = word >> 14` (2 bits, so only modes 0-3 exist; the
  `default` arm at `:1855-1858` is unreachable). PAL4X4 reads byte
  `palAddress + (pal1offset+k)*2`, i.e. byte offset `(word & 0x3FFF)*4 + 2k`
  from the window start. Block colours (`texcache.cpp:1795-1858`):
  - mode 0: entries 0-2 opaque, colour 3 = `0x00000000`.
  - mode 1: entries 0-1 opaque, colour 2 = per-8-bit-channel average
    `(a+b)>>1` of colours 0/1 with alpha 255 (`texcache.cpp:1813-1815`,
    little-endian arm), colour 3 = `0x00000000`.
  - mode 2: entries 0-3 opaque.
  - mode 3: entries 0-1 opaque; colours 2/3 are the 5:3 and 3:5 blends of the
    **expanded 8-bit** channels, `(r0*5+r1*3)>>6` / `(r0*3+r1*5)>>6`, folded
    back to 5 bits and re-expanded through the 5-bit table
    (`texcache.cpp:1843-1851`).
  - Texels: each of the four bytes of the block word is one row
    (`currRow = (currBlock >> (sy*8)) & 0xFF`, `texcache.cpp:1876`), leftmost
    texel in bits 0-1, then 2-3, 4-5, 6-7 (`texcache.cpp:1878-1881`).
  - Palette window: the captured contiguous PLDT block is the only palette the
    decoder has; every read beyond it is an error, including the third colour
    of a mode-0 block and the fourth of a mode-2 block. A block therefore needs
    `(word & 0x3FFF)*4 + 2*entries <= palette_size` bytes; reads never wrap.
- **Format 7 16bpp** (`texcache.cpp:2007-2026`): one 16-bit LE RGB555 word per
  texel; bit 15 set -> opaque `CONVERT(c)` (RGB from bits 0-14), clear -> fully
  transparent `0x00000000`. No palette; `KeyColor0` ignored.

Decoder guarantees: errors are an explicit `TwlhdTexResult` enum (NULL, format
0, texel/index size, palette size, output size, palette range); the output
buffer is never written on an error path (the 4x4 palette window is pre-scanned
over the whole index block before any pixel is written). A3I5/A5I3 alpha 0
keeps the RGB bytes and only clears A, matching `COLOR555TO8888(c, 0)`; the
I-format key colour, 4x4 modes 0/1 colour 3 and 16bpp transparent texels write
`0,0,0,0` exactly as DeSmuME does. Host tests:
`tests/host/test_tex_decode.c`; independent oracle `tools/tex_decode_oracle.py`
cross-checked through `tests/host/tex_decode_cli.c` by
`tools/test_tex_decode_golden.py`; mutation evidence in
`docs/reports/5b2c-mutation-report.md`.

### Texture application (TexEnv / polygon modes)
**VERIFIED:** `PolygonMode` is `MODULATE=0, DECAL=1, TOONHIGHLIGHT=2, SHADOW=3`
(`gfx3d.h:94-99`); the mode is POLYGON_ATTR bits 4-5 (`gfx3d.h:212-216`, latched at
BEGIN as in section A). `_shade` implements it (`rasterize.cpp:562-655`); the
lookup tables are built in `rasterize.cpp:2074-2085` as
`modulate_table[i][j] = ((i+1)*(j+1)-1) >> 6` and
`decal_table[a][i][j] = (i*a + j*(31-a)) >> 5`.
- **READ** MODULATE: `rgb = tex_rgb * vtx_rgb / 63`, `a = tex_a * vtx_a / 31`
  (six-bit components; `rasterize.cpp:578-586`).
- **READ** DECAL: `rgb = tex_rgb*tex_a/31 + vtx_rgb*(1 - tex_a/31)`, `a = vtx_a`
  (`rasterize.cpp:596-611`); without a texture the vertex colour is returned.
- **READ** TOONHIGHLIGHT: toon mode multiplies tex RGB by the toon colour;
  highlight mode adds the toon colour to the modulated RGB
  (`rasterize.cpp:612-641`).
- **READ** SHADOW: output is the vertex colour, texture ignored
  (`rasterize.cpp:645-648`).
- **VERIFIED:** with `!IsSamplingEnabled()`, MODULATE/TOONHIGHLIGHT substitute a
  synthetic white `{63,63,63,31}` (`rasterize.cpp:573-574`).

### Mapping to PICA200 (analysis, not DeSmuME facts)
- **READ** Capture format: `TXDS`/`TXDT`/`PLDT` are already reserved and unimplemented
  (`docs/TWLHD_V2_FORMAT.md:90-98`); stage 5B-2 is planned as a format extension
  plus a texcoord attribute (`docs/PHASE5B_PLAN.md:144-177`). Payload layout is
  UNVERIFIED until that approval.
- **READ** Wrap: DS bits 16-19 (repeat/flip per axis) should map to PICA `GPU_TEXTURE_WRAP_S/T`: clamp -> `GPU_CLAMP_TO_EDGE`, repeat -> `GPU_REPEAT`, repeat+flip -> `GPU_MIRRORED_REPEAT`. Exact PICA mirror/flip behaviour is **UNVERIFIED** (not stated in project docs).
- **READ** Size: DS textures are powers of two (8..1024) by construction, so NPOT support
  is not required. PICA maximum texture size and mipmap support are **UNVERIFIED**.
- **READ** Format conversion: I2/I4/I8/A3I5/A5I3, 16bpp RGB555+1-bit and 4x4 must be
  decoded offline to a PICA upload format. RGBA8 is the safe first choice (no
  paletted PICA textures exist). Whether RGB565, ETC1 or ETC1A4 is preferable is
  **UNVERIFIED**; project docs only name PICA `C3D_TexEnv` as the conceptual
  TexEnv analog (`docs/PHASE4A_PICA200_FEASIBILITY.md:113-116`).
- **UNVERIFIED** TexEnv: MODULATE/DECAL map to TexEnv configurations; TOONHIGHLIGHT and SHADOW
  likely need shader-side emulation. **UNVERIFIED** against citro3d.

### Open questions for 5B-2
- Capture only the bytes a texture references, or the whole 128 KB / 16 KB slots
  (bank spanning makes partial capture harder)?
- Which TEXIMAGE_PARAM/PLTT_BASE is bound at each polygon when they change
  mid-BEGIN, and is TEX_COORD per-vertex or mutable per primitive?
- Confirm on hardware that DS sizes are always `8 << SizeShift` (max 1024) and
  that TEXIMAGE_PARAM bits 16-19 cover all mirrored-repeat combinations.
- Is palette index 0 transparency (KeyColor0_Enable) honoured for all I-formats,
  and is palette bit 15 really unused on hardware?
- Which PICA upload format (RGBA8 vs RGB565/ETC1) and sampler state (filter,
  mipmap) should the renderer use?
- Texcoords as a new vertex attribute (stride change, re-opens the Run 5 risk) or
  a separate buffer?
- Payload layout of `TXDS`/`TXDT`/`PLDT`: per-texture records vs raw VRAM dumps,
  and how to key textures to the polygons that use them.
