<!-- SPDX-License-Identifier: Apache-2.0 -->
# eq4zc_alpha_sweep — arm11 alpha-sweep frames and Azahar crops

Fixtures for EQ-4zc, the frame set proposed in
`docs/reports/eq4zb-blend-residual.md` §6: green and blue solid quads at
alphas `1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31`, drawn as scene-4-style
blend-enabled quad lists over the arm11 black clear. Result and full evidence:
`docs/reports/eq4zc-alpha-sweep.md`.

**Scope of the evidence:** one arm11 3dsx, one Azahar core build, one
emulator render per frame. No claim here generalises to a console, a capture
session or a different program.

## 1. Files

| Path | Purpose |
|---|---|
| `eq4zc_sweep_green.bin` | 11 solid `(0,255,0)` quads, one `POLYGON_ATTR` alpha each, `RSTA 0x04000060 = 0x18` |
| `eq4zc_sweep_green_layout.json` | the 11 quad rectangles in DS pixels, alpha order |
| `eq4zc_sweep_blue.bin` | the same 11 quads in `(0,0,255)` |
| `eq4zc_sweep_blue_layout.json` | the blue layout |
| `eq4zc_sweep_green_top400x240.png` | Azahar top-left 400x240 crop of the green render |
| `eq4zc_sweep_blue_top400x240.png` | Azahar top-left 400x240 crop of the blue render |

Every file is under 100 KB. The PNGs are committed because each is well under
that limit; `test_measure_sweep.py` pins both sha256s.

## 2. sha256

```
79be41bbe2c310e3a238b385571e07be9ac61e41f043c8be150e81f2e18efcc0  eq4zc_sweep_blue.bin
7f0fd7e95220e5442454e3d0d74faf392a2fa166afe04875794ba6b4d081588d  eq4zc_sweep_blue_layout.json
f36134ca69f3ea6f3af58ead5bba3243bd1859a8738b49f8015514c69069c196  eq4zc_sweep_blue_top400x240.png
bef610d857f7bde8f65aaf9e6602aea183ec52d7aec689f06e1d4bed4bf06e11  eq4zc_sweep_green.bin
40f5446c70a24702528bd66290bf898fa6b5957ff53bc85324efc68e78a220a5  eq4zc_sweep_green_layout.json
5207f2eee3339581a6cc16f31a9f3acd209dd4ede2b5016083519e614ffe5800  eq4zc_sweep_green_top400x240.png
```

`test_gen_sweep_frames.py` regenerates the frames and layouts and asserts the
fixtures are byte-identical and the rerun is deterministic; a changed fixture
fails the suite.

## 3. Provenance

Frames and layouts: written by
`python3 tools/eq4zc_alpha_sweep/gen_sweep_frames.py <staging>/eq4zc` at HEAD
`b9c317b260b2d559d5ff81044d60fc5d07ad0c09`; the generator uses only the
committed v2 helpers and has no network or home-path input. The two SD roots
used for the renders held these exact bytes as `twlhd_frame.bin`
(`bef610d8…` green, `79be41bb…` blue).

Renders (EMULATOR, each frame once): the current arm11 3dsx built once from
this HEAD with `make -C arm11` (sha256 `f773a716eb9088ad1f2e8fc28beddc695514097c7f19b5980ccff3bc3c199f76`,
build ID `b9c317b-diag`), rendered by `tools/azahar_host/build/azahar_host`
(shape in `docs/reports/eq4z-clear-black.md` §5) with the external Azahar
libretro core `<staging>/core.so` (core build `9c38437`). Each render wrote a 400x480
PPM; the PNG here is its top-left 400x240 crop (the emulator's top screen maps
1:1 to the render, `docs/reports/eq4o-hw-row-shift.md:27-29`). The DS content
inside the render is scaled 1.25x with offset x=40 by the clipfix matrix
(`common/twlhd_clipfix.c:40`).

Measurements: `python3 tools/eq4zc_alpha_sweep/measure_sweep.py --mapping
arm11 --azahar-correction on` on each crop; `measure_sweep.py` reports the
interior majority colour and applies the presentation-row correction
(`tools/azahar_presentation.py`). The exact commands, exits and raw output are
in the report.

## 4. Tests

```
$ make test-eq4zc-alpha-sweep
```

The suite regenerates the frames, validates them with the Python and C v2
parsers, checks the layout geometry and fixed-point vertices, and measures
both committed crops against the per-alpha numbers in the report.
