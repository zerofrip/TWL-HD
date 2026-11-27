# EQ-4zr: first retail-game frame, DS hardware vs DeSmuME vs our arm11 render

Labels: FACT (measured here), HYPOTHESIS, UNVERIFIED. Only numbers are
recorded; no game data, frame dump or screenshot is part of the repository.

## 1. Setup

- One retail DS title (ROM kept outside the repository, not distributed),
  a static-looking early scene: a rotating icon with a few small sparkles on
  the 3D (bottom) screen.
- Dump: out-of-tree DeSmuME patch (see `eq4zq-retail-gx-dump.md`), frame 4080,
  v2 file, interpreted with `TWLHD_GX_OPT_DS_STACK`: 285 vertices, 4 textures
  (4x4 compressed 32x32, two 16x16 4-colour, one 64x256 A5I3).
- Hardware: one New 3DS, DS mode, cc3dsfs capture of the bottom-screen window
  (320x240 = DS 256x192 at 1.25x), three shots 2 s apart.
- Ours: `arm11/twlhd-arm11.3dsx` built with
  `-DTWLHD_ARM11_GX_OPTS=0x2u`, run in Azahar (`azahar_host`, `--press A:60-63`),
  top 400x240 crop columns 40..359, EQ-4p presentation-row correction applied.

## 2. Finding: the default arm11 build cannot replay a retail frame

FACT: with the default interpreter flags `twlhd_gx_interpret_ex` returns
`ERR_STACK` on retail frames (`MTX_POP 0` etc.), so `arm11/source/main.c`
falls back to its built-in scene. Frame 1500 and frame 4080 rendered to the
byte-identical image (same md5) before the change. `main.c` now passes
`TWLHD_ARM11_GX_OPTS` (default `0u`, behaviour unchanged) to
`twlhd_gx_interpret_opts`; building with `0x2u` makes the two frames render
differently and as the actual scene.

## 3. Numbers (foreground = pixels brighter than the background by sum > 60)

Whole screen, hardware moves (sparkles spread over x 75..242, y 82..179), so
the comparison is restricted to the icon box x 130..189, y 84..139 (320x240
capture coordinates):

| image | foreground px | centroid (x,y) | mean RGB |
|---|---|---|---|
| hardware shot 1 | 1206 | 158.9, 117.5 | 217,192,63 |
| hardware shot 2 | 1235 | 158.6, 114.8 | 213,188,58 |
| hardware shot 3 | 1239 | 159.2, 109.9 | 212,187,57 |
| DeSmuME | 1082 | 160.1, 116.9 | 234,206,64 |
| ours (arm11 + Azahar) | 901 | 159.9, 115.9 | 234,207,69 |

- FACT: ours and DeSmuME agree on position (centroid within 1.0 px) and mean
  colour (within 5 per channel). Both texture formats used by the icon
  (4x4 compressed, 4-colour) decode to the same hue as on hardware.
- FACT: hardware mean colour is about 7% lower in all channels than both
  renders, and its centroid moves 7.6 px in y over three shots (icon bobbing,
  animation phase differs from the dumped frame).
- FACT: background: hardware (8,12,8), DeSmuME (12,12,12), ours (16,16,16).
  The hardware value equals the RGB565 quantisation of 12 (5/6/5 bits), which
  supports the existing capture-quantisation HYPOTHESIS. Our background is the
  fixed arm11 clear colour; `CLEAR_COLOR` is not replayed from the dump (not
  recorded by the dump tool).
- UNVERIFIED: the 7% brightness gap (sparkle dilution, animation phase, or a
  real hardware difference); a pixel-exact comparison needs a frozen scene.
- UNVERIFIED: one console, one scene, one session.

## 4. Next

A frozen scene (no animation) is needed for pixel-level comparison; candidates
are the first static title/menu screens of the same title.
