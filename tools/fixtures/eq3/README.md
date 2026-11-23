# tools/fixtures/eq3 — EQ-3b picture fixture pair

Both files are **test data, not code**, committed so `tools/test_eq3_compare.py`
can run without the external GPL harness or an emulator. All numbers here are
**EMULATOR** results.

## `harness_cube_f240.ppm`

- origin: verbatim byte-for-byte copy of `samples/cube_f240.ppm` from the
  out-of-tree GPL harness `$HOME/3ds/twlhd-desmume-harness` (DeSmuME
  `fc504ee0` software rasteriser, harness frame 242 = ROM frame 240). See that
  repository's `samples/README.md`: 256x192 binary P6, 15-byte header
  `P6\n256 192\n255\n` plus 147456 raster bytes, RGB666 buffer expanded by
  `(v<<2)|(v>>4)`.
- size: 147471 bytes
- sha256: `79b3ee488c29005f6cb9d8f5720313555894928b441f5e1886f3b26485fb83ba`
- licence posture: the harness repository is GPL-2.0-or-later. This file is its
  program **output** (pixels with no code), copied as test evidence, the same
  posture as `tools/fixtures/eq2/README.md`. No harness or DeSmuME source is
  present in this repository.

## `ours_cube_f240_top.ppm`

- origin: the upper 400x240 rows of the 400x480 top+bottom screenshot from the
  headless Azahar libretro host (`tools/azahar_host`, software renderer,
  core `$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`)
  running `arm11/twlhd-arm11.3dsx` with
  `tests/host/fixtures/demo_frame_desmume_v2.bin` as `sdmc:/twlhd_frame.bin`
  and `--press A:60-63 --shot 199`. The frame is the same default-cube frame
  as EQ-2b: recorded frame 241 (recorder frame index), which is ROM frame 240
  and harness frame 242.
- crop rule: the tool accepts 400x240 or 400x480; the fixture stores the
  400x240 top screen to stay under the 300 KB fixture-cap precedent
  (`tools/fixtures/README.md`). Header `P6\n400 240\n255\n` plus 288000 raster
  bytes.
- size: 288015 bytes
- sha256: `134a6a8b3f2d76f0b795bd3f198b75cdaa06a0606603b8f607bbf69d41df3120`
- full screenshot sha256 (400x480): `61412c8753571357bf0a053cba7c16948525df60580b089d223649b975f89f81`
  (reproduced byte-identically on a second run).
- licence posture: TWL-HD's own renderer output, captured by the repository's
  own Apache-2.0 host; no Azahar code or content is vendored here.
