<!-- SPDX-License-Identifier: Apache-2.0 -->
# eq4zd_sweep_hw — arm11 alpha-sweep real-hardware fixtures

Fixtures for the EQ-4zd real-hardware run of the EQ-4zc alpha-sweep frames:
the arm11 `twlhd-arm11.3dsx` loaded `eq4zc_sweep_green.bin` and
`eq4zc_sweep_blue.bin` as `sdmc:/twlhd_frame.bin` with A. Result and analysis:
`docs/reports/eq4zd-alpha-sweep-hw-result.md`.

**Scope of the evidence:** one console, one capture session per frame, one
arm11 3dsx, two scene-4-style sweep frames. No claim here generalises to other
consoles, sessions, frames, programs or capture paths.

## 1. Files

| Path | Purpose |
|---|---|
| `hw_eq4zd_sweep_green_a.png` | hardware capture, green sweep green channel |
| `hw_eq4zd_sweep_blue_a.png` | hardware capture, blue sweep blue channel |
| `test_eq4zd_sweep_hw.py` | pins both sha256s and the 11+11 measured values |
| `README.md` | this file |

Both PNGs are the owner's top-left 400x240 crops of 1280x720 root screenshots
(the shared `tools/ds_vtxcolor_test/capture_mapping.json` mapping: scale 1.25,
offset x=40; the `arm11` measurer preset is identical, see
`tools/eq4zc_alpha_sweep/measure_sweep.py:66-71`). Each file is under 100 KB.

## 2. Fixture provenance and sha256

Both files are byte-identical copies of the owner's staging files
(`<staging>/hw/hw_sweep_green_a.png`, `<staging>/hw/hw_sweep_blue_a.png`).
In-repo hashes:

```
6094a04de35a7fb8db97c4766c06a30ff3e2314118df78196a70c16852a56760  hw_eq4zd_sweep_green_a.png
656b9910a2211d14ee89acb3f5ad658310bc857bfb917d7fd1b9343dd86116c5  hw_eq4zd_sweep_blue_a.png
```

`test_eq4zd_sweep_hw.py` re-checks both hashes, so a changed fixture fails the
suite.

Provenance (OWNER-REPORTED run, offline analysis only): one console; each frame
captured in its own session by a passive capture board through `cc3dsfs` as a
1280x720 root screenshot, cropped to the top-left 400x240; each frame was
captured 3 times and the three crops were byte-identical (nothing else varied).

Frame and 3dsx hashes (the committed frame inputs and the arm11 program that
ran; the frames are the committed `tools/eq4zc_alpha_sweep/fixtures/` files):

```
bef610d857f7bde8f65aaf9e6602aea183ec52d7aec689f06e1d4bed4bf06e11  eq4zc_sweep_green.bin
79be41bbe2c310e3a238b385571e07be9ac61e41f043c8be150e81f2e18efcc0  eq4zc_sweep_blue.bin
a7c315f9aa8ad1afee8d0abcad56115735a938f11d80ff88a32936bb13208a96  twlhd-arm11.3dsx       (built from main dfaf611)
```

The run 3dsx (`a7c315f9…`) was built from `main` `dfaf611` and was **not**
rebuilt in this task.

## 3. Measurement

The measurement tool is the unmodified
`tools/eq4zc_alpha_sweep/measure_sweep.py`. Hardware crops need correction
`off` (rows 0/239 are not uniform black on the capture board, so `auto` would be
undetermined):

```
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py \
    --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json \
    --mapping arm11 --azahar-correction off \
    tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py \
    --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_layout.json \
    --mapping arm11 --azahar-correction off \
    tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png
```

The exact commands, exits and raw output are in the report.

## 4. Tests

```
$ make test-eq4zd-sweep-hw
```

The fixture tests pin both sha256s and the 11+11 measured values (green
`16,40,73,105,130,138,170,203,219,235,255`; blue
`16,41,74,107,132,140,173,206,222,239,255`; alpha 31 `not blended`, every other
quad `blended`). A negative test measures the committed EQ-4zc emulator crops
and asserts they differ from the hardware series (alpha-16 green 135 vs 138,
full-tuple inequality). The target is wired into `make test` like
`tools/eq4za_clear_hw`.
