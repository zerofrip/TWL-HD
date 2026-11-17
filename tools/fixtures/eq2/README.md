# tools/fixtures/eq2 — EQ-2b fixture pair

Both files are **test data, not code**, committed so `tools/test_eq2_compare.py`
can run without the external GPL harness or an emulator.

## `harness_cube_f240.txt`

- origin: verbatim copy of `samples/cube_f240.txt` from the out-of-tree GPL
  harness `/home/one/3ds/twlhd-desmume-harness`, produced by a DeSmuME
  `fc504ee0` run of the default-cube demo ROM (`AUTODUMP_FRAME=240`, harness
  frame 242 = ROM frame 240). See that repository's `README.md` for the
  `geometry-dump 1` format.
- sha256: `e59254fe01b1d1229db94dab2d1534117fec0e672c58462dfe16edc3436a15c9`
- licence posture: the harness repository is GPL-2.0-or-later. This file is its
  program **output** (numbers with no code), copied as test evidence. No harness
  or DeSmuME source is present in this repository. TWL-HD's tooling remains
  Apache-2.0.

## `ours_cube_f240.txt`

- origin: `tests/host/build/gx_interp_cli --eq2 tests/host/fixtures/demo_frame_desmume_v2.bin`
  (format `twlhd-eq2-clip 1`, documented in `docs/EQ2_COMPARE.md`). The input
  v2 capture is the archived DeSmuME capture of the same default-cube frame
  (angle 61440, recorder frame 241, sha256
  `acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5`).
- sha256: `ccb6e61cf8f911ff652abdc67b010a15c7b4bd3d9553a08692651c43170f4c67`
- Regenerate with the command above; the output is deterministic.
