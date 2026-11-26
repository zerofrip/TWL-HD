<!-- SPDX-License-Identifier: Apache-2.0 -->
# eq4za_clear_hw — arm11 black-clear real-hardware fixtures

Fixtures for the EQ-4za real-hardware run that closes part of the EQ-4z §7
console PLAN: the arm11 `twlhd-arm11.3dsx` (built from `main` `bd58fab`) loaded
the recorded scene-4 frames with the new black `(0,0,0)` clear colour. Result
and full evidence: `docs/reports/eq4za-clear-black-hw-result.md`.

**Scope of the evidence:** one console, one capture session per frame,
one arm11 3dsx, one scene (`SCENE=4`, `STATIC_ANGLE=30`, DS mode). No claim here
generalises to other consoles, sessions, frames, programs or mid-frame toggles.

## 1. Files

| Path | Purpose |
|---|---|
| `hw_eq4za_nb_a.png` | arm11 non-blend capture `a` (RSTA `0x04000060 = 0x10`, blend bit clear) |
| `hw_eq4za_bl_a.png` | arm11 blend capture `a` (RSTA `0x04000060 = 0x18`, blend bit set) |
| `test_eq4za_clear_hw.py` | pins the two fixture hashes and the measured per-quad numbers |
| `README.md` | this file |

Both PNGs are the owner's top-left 400x240 crops of 1280x720 root screenshots
(the shared `tools/ds_vtxcolor_test/capture_mapping.json` mapping: scale 1.25,
offset x=40). Each file is under 100 KB.

## 2. Fixture provenance and sha256

Both files are byte-identical copies of the owner's staging files
(`<staging>/hw/hw_eq4z_nb_a.png`, `<staging>/hw/hw_eq4z_bl_a.png`). In-repo
hashes:

```
1b6c9aaa99265601d1f35f27d9025f33d663f75bcff40eef6b31ce7fb68568f8  hw_eq4za_nb_a.png
17245fa9ab9ff5ef129cf082af9a899e186b3a30dd23ebb4c148d78a145bcf3c  hw_eq4za_bl_a.png
```

`test_eq4za_clear_hw.py` re-checks both hashes, so a changed fixture fails the
suite.

Provenance (OWNER-REPORTED run, offline analysis only): one console;
each frame captured in its own session by a passive capture board through
`cc3dsfs` as a 1280x720 root screenshot, cropped to the top-left 400x240; each
frame was captured 3 times and the three crops were byte-identical (nothing
else varied). Each frame was loaded from SD as `sdmc:/twlhd_frame.bin` with the
A button (`docs/reports/eq4n-arm11-row-shift.md` §4); the SD file originally
there was backed up and restored owner-side.

Frame and 3dsx hashes (the recorded frame inputs and the arm11 program that ran;
not committed here):

```
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  frame_s4_noblend.bin   (RSTA 0x04000060 = 0x10)
ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5  frame_s4_blend.bin     (RSTA 0x04000060 = 0x18)
8e7e02f60729316a40cfd80d1cc6a99c8ee65deb4ae8c0f1d29eebcc5cc16fbc  twlhd-arm11.3dsx       (built from main bd58fab)
```

The run 3dsx (`8e7e02f6…`) was built from `main` `bd58fab` and was **not**
rebuilt in this task. The build ID embeds the short HEAD (`arm11/Makefile:75-84`).

## 3. Measurement

The measurement tool is the unmodified `tools/eq4w_blend_hw/measure_blend_hw.py`
(reused as is; no change was needed — its blend classification is
source-relative and its colour census is unconditional, so it takes these
captures directly, see `docs/reports/eq4za-clear-black-hw-result.md` §2):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4za_clear_hw/hw_eq4za_nb_a.png
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4za_clear_hw/hw_eq4za_bl_a.png
```

## 4. Tests

```
$ make test-eq4za-clear-hw
```

The fixture tests pin both sha256s and assert the EQ-4za FACT numbers: the
non-blend capture paints the alpha-16 green `(0,255,0)` and alpha-4 blue
`(0,0,255)` quads at `blended=no`; the blend capture dims them to green 138 and
blue 41 at `blended=yes`; the background clears to `(0,0,0)` (92968 pixels) and
both alpha-0 quads show `bright_pixels=12` in the region and
`interior_bright_pixels=1`. A negative test asserts that the older EQ-4y
`(16,16,24)` crop is not classified as a black background. The target is wired
into `make test` like `tools/eq4y_gate_hw`.
