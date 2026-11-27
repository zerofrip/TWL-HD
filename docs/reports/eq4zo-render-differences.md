# EQ-4zo — Rendering differences found by the DS-mode hardware comparisons

Decision document. Written for the project owner. It collects what the real-hardware
comparisons of EQ-4za to EQ-4zn showed about the difference between the DS picture and
our render, ranks the differences and proposes what to change and what to leave.
No code is changed by this document.

**Basis for every number.** One New 3DS, DS mode, one capture board
(Optimize N3DS, 1280x720 screenshot, top-left 400x240 crop), one capture session per
ROM. The capture keeps 5 bits of red and blue and 6 bits of green
(`eq4zk-texmod-hw-result.md` §2: FACT as a property of the measured values, the
RGB565 cause is a HYPOTHESIS), so differences of one level (8 of 255 on red and
blue, 4 on green) are the smallest visible step. Labels: **FACT** = measured on
hardware in the cited report (the "Source" column names it; the later reports
EQ-4zi..4zn also pin their numbers in a committed test); **HYPOTHESIS** = a
judgement or an unmeasured explanation. Every Impact, Effort and Risk rating and
every recommendation in this document is a HYPOTHESIS.

## 1. What matches and what does not

| Feature | Source | DS hardware vs our pipeline | Label | Status |
|---|---|---|---|---|
| Clear colour | `eq4za-clear-black-hw-result.md` §8 | black clear works on hardware | FACT | closed |
| Polygon alpha blend | `eq4zd-alpha-sweep-hw-result.md` §7 (11 alpha values x green and blue = 22 sample points; `eq4zb`, `eq4zc` are its model and sweep design) | our render brighter by +4 green / +8 blue, at most +5 / +9 (both at alpha 15) | FACT | OPEN, recommendation: leave (§3.1) |
| 1-px lines | `eq4zf-1px-lines.md` §6 | hardware: exactly one capture row/column at the nominal place; ours PASS | FACT | closed |
| Partial viewport | `eq4zg-partial-viewport.md` §7 | no pixel outside the viewport on hardware, harness and ours | FACT | closed (no fix indicated) |
| Lighting (diffuse, ambient, specular, several lights) | `eq4zi-lit-hw-result.md` §2, §3, §3b | interpreter was 8..10 levels dark on diffuse quads; after the change all 21 measured quads within 4 of 255 | FACT | **changed on the host side** (§3.2) |
| Texture formats | `eq4zj-texfmt-hw-result.md` §2, §3 | 7 of 8 formats match (worst 7 of 255); I4 16x16 one cell above 28 (sampling position) | FACT | closed |
| Texture x vertex colour (MODULATE) | `eq4zk-texmod-hw-result.md` §3 | DS six-bit equation: 24 of 24 channels exact; our `GPU_MODULATE` model: 11 of 24, never more than one capture level off (max 6 of 255) | FACT | OPEN, recommendation: leave (§3.3) |
| Texture alpha, KeyColor0, wrap | `eq4zl-zm-zn-probes-hw-result.md` §2 | alpha and KeyColor0 exact; wrap tiles right, 2 to 8 seam cells per quad off | FACT | closed |
| Gouraud | `eq4zl-zm-zn-probes-hw-result.md` §3 | linear barycentric blend within 5.3 of 255 | FACT | closed |
| Back-face culling | `eq4zl-zm-zn-probes-hw-result.md` §4 | hardware draws 4 of the 8 probe quads; the host interpreter and the arm11 renderer draw all 8 (SOURCE-READ: `eq4ze-hw-coverage.md` F2, `renderer.c:897,1552`) | FACT (hardware), SOURCE-READ (ours) | **OPEN, recommendation: implement** (§3.4) |

Not covered by any comparison and known to differ by construction
(`docs/EQUALITY_CHECK_DESIGN.md:221-229`): fog, edge marking, antialiasing, toon
shading, polygon-ID/shadow, DECAL/TOON/SHADOW polygon modes, depth-EQUAL/w-buffer.

## 2. Ranking (every cell and every recommendation is a HYPOTHESIS)

Impact = how visible and how common in DS software; effort = work in this repository;
risk = chance that a change breaks something that is verified today.

| Rank | Difference | Impact | Effort | Risk | Recommendation |
|---|---|---|---|---|---|
| 1 | Culling bits of POLYGON_ATTR ignored | medium: wrong output for content that uses mode 0 (nothing drawn), cull-front or both-sided drawing; hidden only by the depth test for closed opaque meshes | medium | medium | implement in the interpreter, §3.4 |
| 2 | MODULATE one capture level off | low: at most 6 of 255, below every tolerance used so far (12 on flat colours) | high (needs a per-texel 6-bit rule the PICA combiner cannot express exactly) | high | leave, §3.3 |
| 3 | Alpha blend +4 green / +8 blue | low: at most 5 / 9 of 255 | medium; a lookup table in `gx_alpha_float` touches every blend | medium | leave (EQ-4zd §7 reached the same conclusion) |
| 4 | Wrap seams one or two capture pixels off | none visible, measurement limit | — | — | no action |
| 5 | Lighting model is a fit | low: 21 quads, one truncation scheme per term | — | — | keep the label HYPOTHESIS; re-test if a new content type shows a difference |

## 3. Details

### 3.1 Alpha blend (EQ-4zd, existing result)
FACT (`eq4zd-alpha-sweep-hw-result.md` §7): our render is steadily brighter than the DS
blend by +4 green / +8 blue, with a maximum of +5 green and +9 blue at alpha 15. The
same report concludes (its own analysis, HYPOTHESIS for the proposed remedy) that no
whole-alpha pre-compensation can fix green, that blue could be made exact for alpha >= 4
only, and that a lookup table in the interpreter changes every blend. That is not
repeated here; the recommendation to leave it stands (HYPOTHESIS).

### 3.2 Lighting (EQ-4zi), already changed
`common/twlhd_gx_interp.c` now truncates every lighting term (specular, diffuse,
ambient, each light) to 1/16 of a 5-bit level, sums over the lights and truncates the sum.
This is the only granularity of 1/8, 1/16 and 1/32 that fits all quads of lit1..lit6;
whether it is how the hardware really works is a HYPOTHESIS (unverified). Lighting is
computed on the CPU in the interpreter, so the arm11 renderer needs no change
(SOURCE-READ: `docs/reports/eq4zh-lit-rom.md`).

### 3.3 MODULATE (EQ-4zk)
SOURCE-READ: `arm11/source/renderer.c:1324-1333` sets one TexEnv stage `GPU_MODULATE` of
texture 0 and the primary colour. FACT (`eq4zk-texmod-hw-result.md` §3): the DS result
equals `((t6+1)*(v6+1)-1)>>6` on six-bit values for all 24 channels tried, and our
`GPU_MODULATE` model is at most one capture level (6 of 255) away. HYPOTHESIS (the
whole rest of this section, not tried on the combiner): the PICA combiner multiplies
8-bit values and no combiner function reproduces the DeSmuME rounding for every pair;
a software modulate per texel would mean leaving the GPU path, which the error does not
justify. Recommendation (HYPOTHESIS): leave it, keep the measurement as the reference if
a content type shows a visible banding difference.

### 3.4 Culling (EQ-4zn): proposal
**Measured (FACT):** counter-clockwise on screen is the front face; POLYGON_ATTR bit 7
(value 2) renders the front face, bit 6 (value 1) the back face, value 3 both, value 0
neither.
**Current state:** the interpreter latches only the alpha and the light mask
(`docs/reports/eq4ze-hw-coverage.md` F2) and `arm11/source/renderer.c:897` and `:1552`
force `GPU_CULL_NONE`; `docs/reports/5c4-black-screen-analysis.md` §"What the host
interpreter and the ARM11 PICA path do with cull bits" records this.
**Proposal (not applied):** do the facing test on the CPU in the interpreter, not on the
PICA. (1) Latch POLYGON_ATTR bits 6-7 at BEGIN_VTXS like alpha and light mask.
(2) For each assembled triangle compute the signed area in screen space after the
projection (the sign convention follows the probe: counter-clockwise = front). (3) Drop
the triangle when the attribute value excludes its facing. (4) Test with the eight
quads of the CULL probe as host unit tests (the capture pattern drawn / not drawn is the
expected result, fixture `tools/fixtures/eq4_hw_probes/hw_cull_top400x240.png`) plus a
host frame of the probe run through the interpreter. Doing it on the CPU keeps the PICA
winding convention (an open sign question in `5c4`) out of the picture and works for
every polygon type the interpreter already assembles.
**Open points before building it:** behaviour for polygons with `w <= 0` or partly
outside the clip volume (not measured), quads and strips of the real polygon types,
and the cost of one more per-polygon test (small).
**Risk:** a wrong sign would hide all geometry of a scene. Mitigation: the eight-case
probe and a switch that keeps the old draw-everything behaviour until hardware
captures of a real game scene agree.

## 4. What to do next (recommended order)

1. Implement the culling decision in the interpreter (§3.4), as a normal pipeline task
   with the probe as acceptance test, risk MEDIUM.
2. Leave MODULATE and the alpha residual as documented; do not change the arm11
   renderer for them.
3. If wanted later: comparisons that would change the ranking are a real game scene
   (to see whether culling or MODULATE is visible at all), the DECAL/TOON/SHADOW
   polygon modes and fog/edge marking, which differ by construction.
4. Re-run the lighting probes (lit1..lit6) only if a new content type shows a
   lighting difference.

## 5. Not shown by this document

No new measurement was made. The impact column is a judgement from feature frequency,
not a measurement. Every hardware number is one console and one capture session.
