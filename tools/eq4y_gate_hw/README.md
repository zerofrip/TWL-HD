<!-- SPDX-License-Identifier: Apache-2.0 -->
# eq4y_gate_hw — arm11 blend-gate real-hardware fixtures

Fixtures for the EQ-4y real-hardware run that closes the EQ-4x §7 console PLAN:
the arm11 `twlhd-arm11.3dsx` (built from `main` `2ce38d0`) loaded the recorded
scene-4 `twlhd_frame_v2.bin` and gated alpha blending on the recorded
`DISP3DCNT` bit 3. Result and full evidence:
`docs/reports/eq4y-gate-hw-result.md`.

**Scope of the evidence:** one New 3DS console, one capture session per frame,
one arm11 3dsx, one scene (`SCENE=4`, `STATIC_ANGLE=30`, DS mode). No claim here
generalises to other consoles, sessions, frames, programs or mid-frame blend
toggles.

## 1. Files

| Path | Purpose |
|---|---|
| `hw_eq4x_nb_a.png` | arm11 non-blend capture `a` (RSTA `0x04000060 = 0x10`, bit 3 clear) |
| `hw_eq4x_bl_a.png` | arm11 blend capture `a` (RSTA `0x04000060 = 0x18`, bit 3 set) |
| `test_eq4y_gate_hw.py` | pins the two fixture hashes and the measured per-quad numbers |
| `README.md` | this file |

Both PNGs are the owner's top-left 400x240 crops of 1280x720 root screenshots
(the shared `tools/ds_vtxcolor_test/capture_mapping.json` mapping: scale 1.25,
offset x=40). Each file is under 100 KB.

## 2. Fixture provenance and sha256

Both files are byte-identical copies of the owner's staging files
(`<staging>/hw/hw_eq4x_nb_a.png`, `<staging>/hw/hw_eq4x_bl_a.png`). In-repo
hashes:

```
deb38c18a5bb996062666032f028f85285082e1784f2fc6d1b2b430253ee4994  hw_eq4x_nb_a.png
b69fe759b2ea71e5478c161e62ab91e66e43cc753083ded41f687ee33cbc48de  hw_eq4x_bl_a.png
```

`test_eq4y_gate_hw.py` re-checks both hashes, so a changed fixture fails the
suite.

Provenance (OWNER-REPORTED run, offline analysis only): one New 3DS console;
each frame captured in its own session by a passive capture board through
`cc3dsfs` as a 1280x720 root screenshot, cropped to the top-left 400x240; each
frame was captured 3 times and the three crops were byte-identical (nothing
else varied). Each frame was loaded from SD as `sdmc:/twlhd_frame.bin` with the
A button (`docs/reports/eq4n-arm11-row-shift.md` §4); the SD file originally
there was backed up and restored owner-side.

Frame and 3dsx hashes (the recorded `twlhd_frame_v2.bin` inputs and the arm11
program that ran; not committed here):

```
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  frame_s4_noblend.bin   (RSTA 0x04000060 = 0x10)
ca471235e7087122d7690561e1cfdac5ffdc9875ef9c5722a90201409e910bf5  frame_s4_blend.bin     (RSTA 0x04000060 = 0x18)
738b5c1772e6c40cb25a67ed1c923eac98e450f5fbea6a07768bb7f676b62f60  twlhd-arm11.3dsx       (built from main 2ce38d0)
```

The run 3dsx (`738b5c17…`) is **NOT** the 3dsx hashed in EQ-4x §4
(`2dc7d386…`, HEAD `a0eea169…`); it was built from `main` `2ce38d0`, and the
build ID embeds the short HEAD (`arm11/Makefile:75-84`). It was not rebuilt in
this task.

## 3. Measurement

The measurement tool is the unmodified `tools/eq4w_blend_hw/measure_blend_hw.py`
(reused as is; no change was needed — its blend classification is
source-relative and its colour census is unconditional, so it takes these
captures directly, see `docs/reports/eq4y-gate-hw-result.md` §2):

```
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_nb_a.png
$ python3 tools/eq4w_blend_hw/measure_blend_hw.py tools/eq4y_gate_hw/hw_eq4x_bl_a.png
```

## 4. Tests

```
$ make test-eq4y-gate-hw
```

The fixture tests pin both sha256s and assert the EQ-4y FACT numbers: the
non-blend capture paints the alpha-16 green `(0,255,0)` and alpha-4 blue
`(0,0,255)` quads at `blended=no`; the blend capture dims them to green 142 and
blue 57 at `blended=yes`; both alpha-0 quads show `bright_pixels=12` in the
region and `interior_bright_pixels=1`. The target is wired into `make test`
like `tools/eq4w_blend_hw`.
