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
- sha256: `32d583f3eb6558353667bd41a9d437014c9ddf59625380082536f6cc0274ba06`
- full screenshot sha256 (400x480): `7c93689dbdf507b72acc49fdb48fb17699c18e098d3f2638d3b4f62e97a32f31`
- update (2026-11-25): regenerated after EQ-4z changed the arm11 clear colour
  from `(16,16,24)` to `(0,0,0)`; only the background class changed, faces are
  unchanged.  Recorded build HEAD `39b30c23277315086173fd4cf3f7dc8b89584e70`,
  dirty worktree, build ID `39b30c2-diag`; the full-screenshot sha256 above is
  that render.  The committed `32d583f3…` crop is byte-identical to the top
  400x240 rows of the later clean build (HEAD
  `3cd3bb0b6c795defb27a0848d8e20c54832d5377`, build ID `3cd3bb0-diag`, full
  screenshot sha256
  `e3730b865a733bbb2beb04895e900c17b024a119de70919784155ee87d0df25a`); the two
  full screenshots differ only in the bottom 240 rows, so the fixture does not
  depend on which build supplied it.  The fixture is stored uncorrected; use
  `--azahar-correction on` to apply the presentation-row correction, or place
  an already-corrected sidecar marker `<ppm>.azahar-corrected` to prevent a
  double correction.  See `docs/reports/eq4z-clear-black.md` §8.
- licence posture: TWL-HD's own renderer output, captured by the repository's
  own Apache-2.0 host; no Azahar code or content is vendored here.
