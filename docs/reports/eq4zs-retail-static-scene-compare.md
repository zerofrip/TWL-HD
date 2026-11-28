# EQ-4zs: retail scene, DS hardware vs DeSmuME vs our arm11 render (settled camera)

Labels: FACT (measured here), HYPOTHESIS, UNVERIFIED. Only numbers are
recorded; no game data, frame dump or screenshot is part of the repository.
Follows `eq4zq-retail-gx-dump.md` (dump tool, `TWLHD_GX_OPT_DS_STACK`) and
`eq4zr-retail-frame-compare.md` (first comparison, `TWLHD_ARM11_GX_OPTS`).

## 1. Setup

- One retail DS title, one in-game scene reached by hand on both sides: a 3D
  scene filling the whole top screen, a controllable character in the
  centre, the lower screen static.
- Same procedure on both sides: reach the scene, press A, wait 30 s without
  input so the camera settles, then capture.
- DeSmuME side: out-of-tree patched `desmume-cli` (not in this repository)
  dumped 120 consecutive v2 frames on a trigger file and took full-screen
  shots of both displays; frame 19180 of the dump is compared with the shot
  taken at the middle of the window.
- Hardware: one New 3DS, DS mode, cc3dsfs top-screen window, four shots 2 s
  apart, crop of the 320x240 image area (DS 256x192 at 1.25x).
- Ours: `arm11/twlhd-arm11.3dsx` built with `-DTWLHD_ARM11_GX_OPTS=0x2u`,
  Azahar `azahar_host`, EQ-4p presentation-row correction applied, same crop.
- Metric: mean absolute difference per channel over all 320x240 pixels
  (DeSmuME 256x192 shots are enlarged bilinearly to 320x240).

## 2. Result with a settled camera

| pair | R | G | B |
|---|---|---|---|
| hardware 1 vs ours | 9.4 | 7.4 | 5.0 |
| hardware 2 vs ours | 9.2 | 7.2 | 4.9 |
| hardware 3 vs ours | 9.6 | 7.4 | 5.2 |
| hardware 4 vs ours | 9.0 | 7.1 | 4.8 |
| hardware 1 vs DeSmuME | 9.5 | 8.1 | 5.4 |
| ours vs DeSmuME | 8.0 | 6.3 | 4.8 |
| hardware 1 vs hardware 2 | 1.7 | 1.2 | 1.1 |
| hardware 1 vs hardware 3 | 0.7 | 0.6 | 0.5 |
| hardware 1 vs hardware 4 | 1.7 | 1.2 | 1.1 |
| hardware 2 vs hardware 3 | 1.8 | 1.2 | 1.2 |
| hardware 2 vs hardware 4 | 0.9 | 0.7 | 0.5 |
| hardware 3 vs hardware 4 | 1.8 | 1.2 | 1.2 |

Whole-image mean RGB: hardware (139.4, 191.9, 76.2), DeSmuME (139.4, 190.2,
75.9), ours (139.7, 190.4, 75.3).

- FACT: hardware vs ours is of the same size as DeSmuME vs ours (about one
  grey level more in R and G). Block means over a 4x4 grid agree within
  about 10 grey levels everywhere. The 40x40-cell mean absolute difference
  (hardware 1 vs ours, 8x6 cells, per-pixel mean over the three channels) is
  3..13 on 46 of the 48 cells, 4..13 on the cell rows 0..2 (pixel rows
  0..119), and 23..25 on the two cells where the character stands.
- HYPOTHESIS: the two high cells come from a different character
  pose/position in the three captures (not checked pixel by pixel).
- FACT: the six hardware-vs-hardware pairs differ by 0.5..1.8 per channel,
  far below the hardware-vs-ours values.
- HYPOTHESIS: that spread is capture noise and slow scene motion.

## 3. Correction of the earlier run

- FACT: in a first run with a shorter wait, the 40x40-cell mean absolute
  difference over cell rows 0..2 (pixel rows 0..119) was 22..82 for
  hardware vs ours and 3..21 for ours vs DeSmuME.
- UNVERIFIED: owner observation (not measured) that the hardware camera was
  still pulled back in that run.
- FACT: with the camera settled (section 2) the same measurement for
  hardware 1 vs ours is 4..13 over the same rows.
- HYPOTHESIS: the earlier difference was a scene-state difference (camera
  not settled on one side), not a rendering difference; the settled-camera
  result above is the evidence for it.
- FACT: the fog hypothesis from the first run (most polygons carry the
  POLYGON_ATTR fog bit) is withdrawn; the settled-camera measurement in
  section 2 (4..13 over cell rows 0..2) gives no support for it.

## 4. Not covered

- UNVERIFIED: fog, edge marking, toon and clear colour are not recorded by
  the dump tool (RSTA is empty), so a scene that uses them is not testable
  yet; this scene shows no evidence of a difference from them.
- UNVERIFIED: the cause of the remaining 4.8..9.6 grey-level gap between
  hardware and ours.
- HYPOTHESIS (for that gap): RGB565 quantisation of the capture path, 1.25x
  resampling and a single character pose; a pixel-exact frame needs a frozen
  character.
- One console, one scene, one session.
