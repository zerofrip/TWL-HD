# EQ-4zt: opt-in culling on a retail scene, hardware vs DeSmuME vs arm11

Labels: FACT (measured here), SOURCE-READ, HYPOTHESIS, UNVERIFIED. Only
numbers are recorded; no game data, frame dump or screenshot is part of the
repository. Same scene, capture and procedure as `eq4zs-retail-static-scene-compare.md`
(frame 19180 of the dump, four hardware shots, camera settled).

## 1. Setup

`arm11/twlhd-arm11.3dsx` built twice: `-DTWLHD_ARM11_GX_OPTS=0x2u` (DS stack,
no culling) and `0x3u` (DS stack + `TWLHD_GX_OPT_CULL`, EQ-4zp rule). Azahar
render, EQ-4p row correction, same 320x240 crop.

## 2. Result

- FACT: culling removes 707 of 1972 triangles (664 polygons, 104 undecided
  polygons drawn).
- FACT: it changes 467 of 76800 pixels of the render.
- FACT: mean absolute difference to hardware, averaged over the four shots
  and three channels: 7.174 without culling, 7.288 with culling (worse by
  0.114). DeSmuME's own shot is closer to the no-cull render than to the
  cull render as well (G 6.29 vs 6.62).
- FACT: on the 467 changed pixels the mean absolute difference to hardware
  is 15.2 without culling and 33.8 with culling.
- FACT: restricting culling by primitive type: triangle list and quad list
  0 px changed, quad strip only 52 px, triangle strip only 448 px (even
  triangles of a strip 421 px, odd 27 px). By POLYGON_ATTR mode: front-only
  (2) 451 px, back-only (1) 16 px, none (0) 0 px. Reversing the strip facing
  sign changes 72999 px, so the sign convention is not the cause.
- FACT: bisecting the 664 cull decisions (16 at a time, then singly) puts 410
  of the 467 pixels on one decision (index 444: the first triangle of a
  triangle strip, front-only, clip-space area -0.0670, w 21.06/23.79/21.11),
  13 and 3 pixels on two quad-strip decisions (back-only, areas 0.0009 and
  0.0005, w about 17.5); the rest is spread over seven other windows of 16 decisions.
- SOURCE-READ: DeSmuME `gfx3d.cpp:3341-3366` computes facing from all
  polygon vertices in screen space and shows front-only polygons when
  `facing >= 0`; its strip index toggle (`gfx3d.cpp:1694-1701`) yields the
  same triangle vertex order as our strip assembly. Applying its formula by
  hand to the decision-444 triangle (screen points about (16,57), (92,40),
  (83,59) on the 320x240 grid) gives `facing < 0`, i.e. back-facing, so
  DeSmuME's formula alone does not explain why that triangle is drawn.

## 3. Verdict

- The culling rule stays opt-in; arm11 culling is not enabled.
- FACT: 707 triangles are removed but only 467 pixels change; on those
  pixels the culled render is further from hardware (33.8 vs 15.2), and 410
  of them come from one triangle.
- HYPOTHESIS (inference from the lines above): almost all removed triangles
  are invisible in this scene, and a handful that hardware shows are removed
  too.
- UNVERIFIED: whether DeSmuME draws the decision-444 triangle (see 4).

## 4. Not covered

- UNVERIFIED: whether DeSmuME itself culls the decision-444 triangle (its
  shot suggests not). A log of DeSmuME's per-polygon culling for the same
  frame would settle it.
- HYPOTHESES, none tested: (a) the triangle is covered by a later polygon in
  hardware and DeSmuME only through depth/alpha rules we do not model;
  (b) POLYGON_ATTR state at that BEGIN differs between the dump prefix
  reconstruction and the real engine; (c) clipping changes the polygon
  before facing is computed.
- One console, one scene, one session.
