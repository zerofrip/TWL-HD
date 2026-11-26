<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4zc — arm11 alpha-sweep frames, emulator verification, console plan

**Labels.** **FACT** = quoted command with raw output (path placeholders are only `<staging>`, `<worktree>` and `<logfile>`); **SOURCE-READ** = a claim about source, cited by file/line; **HYPOTHESIS**/**UNVERIFIED** = not measured by the quoted output. No console was contacted; no production source changed.
## 1. Purpose and inputs

**SOURCE-READ.** `docs/reports/eq4zb-blend-residual.md` §6 names the sweep: "scene-4-style blend frames loaded as `sdmc:/twlhd_frame.bin` with A, green and blue quads at several alphas over black — at least where the §3 table's deltas change: 1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31" (`:432-436`). The frames copy scene 4 (`arm9v2/source/demo_frame.cpp:296-307`; libnds `videoGL.h:189`): one `POLYGON_ATTR` per quad, alpha latched at `BEGIN_VTXS` (`common/twlhd_gx_interp.c:921,1069-1074`), weight `(a+1)/32` (`:395-397`), gate on RSTA `0x04000060` bit 3 (`arm11/include/twlhd_blend_gate.h:32-42,58-75`); black clear, clipfix default viewport `0,0,255,191`, x scale `m[1][0] = -0.8f * hw` (`common/twlhd_clipfix.c:12,40`).
## 2. Generator (FACT)

`gen_sweep_frames.py` writes two frames and two layout JSONs: a 6x2 grid of 11 quads (28x39 DS px, gaps 10/18, margins 19/48); every y edge is a multiple of 3, so `clip_y = 4096*(192 - y - 96)/96` is exact. Helpers come from `tools/test_twlhd_v2.py`; the RSTA chunk carries `(0x04000060, 0x18, 0, 2)`.

```
$ python3 tools/eq4zc_alpha_sweep/gen_sweep_frames.py <staging>/eq4zc > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
<staging>/eq4zc/eq4zc_sweep_green.bin
<staging>/eq4zc/eq4zc_sweep_green_layout.json
<staging>/eq4zc/eq4zc_sweep_blue.bin
<staging>/eq4zc/eq4zc_sweep_blue_layout.json
```
**FACT — Python validator on both frames** (the validator the tex frame generators/tests use).
```
$ for f in green blue; do python3 tools/twlhd_v2_dump.py validate <staging>/eq4zc/eq4zc_sweep_$f.bin; done > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
result=OK
entries=133
rsta=1
result=OK
entries=133
rsta=1
```
**FACT — C validator on both frames** (`tests/host/v2_dump_cli.c`, unfiltered).
```
$ cc -std=c11 -Wall -Wextra -Werror -I common tests/host/v2_dump_cli.c common/twlhd_v2_parse.c -o <staging>/v2_dump_cli > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
$ for f in green blue; do echo "== $f"; <staging>/v2_dump_cli tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_$f.bin; done > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
== green
result=OK
flags=1
frame=0
chunks=3
unknown=0
entries=133
rsta=1
meta_backend=2
meta_coverage=7
digest=4C5DCA42D7FB8298
count 0x00000029 11
count 0x00000020 11
count 0x00000040 11
count 0x00000023 88
count 0x00000041 11
count 0x00000050 1
== blue
result=OK
flags=1
frame=0
chunks=3
unknown=0
entries=133
rsta=1
meta_backend=2
meta_coverage=7
digest=C1ABE1F7DD4053D9
count 0x00000029 11
count 0x00000020 11
count 0x00000040 11
count 0x00000023 88
count 0x00000041 11
count 0x00000050 1
```
**FACT — RSTA entry** (blend bit 3 set, gx_index 0, width 2).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import sys; sys.path.insert(0, 'tools')
from twlhd_v2_dump import parse
for name in ('green', 'blue'):
    view = parse(open('tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_%s.bin' % name, 'rb').read())
    print('%s: rsta_count=%d gxcm_count=%d' % (name, len(view.rsta), len(view.gxcm)))
    for addr, value, gx_index, width in view.rsta:
        print('  addr=0x%08x value=0x%08x bit3=%d gx_index=%d width=%d' % (addr, value, (value >> 3) & 1, gx_index, width))
EOF
exit=0
green: rsta_count=1 gxcm_count=133
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
blue: rsta_count=1 gxcm_count=133
  addr=0x04000060 value=0x00000018 bit3=1 gx_index=0 width=2
```

Layouts list the 11 rectangles in alpha order in DS pixels inside 256x192, no mapping or model baked in (first green quad `[19, 48, 47, 87]` at alpha 1). `make test-eq4zc-alpha-sweep` asserts the byte-identical rerun, fixture match, layout order/bounds/separation, fixed-point vertices and both validators.
## 3. Host-side measurer (FACT)

`measure_sweep.py` reads the layout JSON, applies a preset and reports each quad's interior majority colour (`channel_mode`/`clamp_rect`/`interior_rect` from `tools/eq4w_blend_hw/measure_blend_hw.py`, after `azahar_presentation.apply_correction`); `blended` means the source channel is below 255 (2-pixel interior shrink per side).
```
$ make test-eq4zc-alpha-sweep > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools/eq4zc_alpha_sweep -p 'test_*.py'
..................
----------------------------------------------------------------------
Ran 18 tests in 0.248s

OK
```
The suite covers the synthetic positive, the required negative (full-colour quads classify `not blended`), both `--mapping` presets and the pinned crops.
## 4. EMULATOR verification (FACT)

**Build identity.** `make -C arm11` ran twice at HEAD `b9c317b`: the first invocation built `arm11/twlhd-arm11.3dsx`; the second reported it up to date and compiled nothing. At the build, HEAD was `b9c317b` with an untracked `tools/eq4zc_alpha_sweep/` present; `git status --short arm11 common` was empty, so the built paths were clean. The hashed 3dsx (sha256 `f773a716...`, build ID `b9c317b-diag`) is that first build's output, copied unchanged into the verification worktree (it is gitignored and absent from a fresh checkout) and hashed there; no rebuild produced it. HEAD `ee6a03f45bf2075297448819097221cfd784d183` differs from `b9c317b` only outside `arm11/` and `common/` (quoted diff below), and the EQ-4zc files sit outside those directories, so they do not affect the arm11 build. The build ID stamps the short HEAD plus an unconditional `-diag` suffix (`arm11/Makefile:75-84`).
```
$ { git add -N . && git status --short; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
 M docs/reports/eq4zc-alpha-sweep.md
 M tools/eq4zc_alpha_sweep/fixtures/README.md
 M tools/eq4zc_alpha_sweep/gen_sweep_frames.py
$ git rev-parse HEAD > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
ee6a03f45bf2075297448819097221cfd784d183
$ sha256sum arm11/twlhd-arm11.3dsx > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
f773a716eb9088ad1f2e8fc28beddc695514097c7f19b5980ccff3bc3c199f76  arm11/twlhd-arm11.3dsx
$ strings arm11/twlhd-arm11.3dsx | grep -E '^[0-9a-f]{7}(-diag)?$' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
b9c317b-diag
$ git diff b9c317b HEAD -- arm11 common > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
$ sed -n '75,84p' arm11/Makefile > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
# Build ID stamp for the ARM11 3DSX. Shown on the bottom-screen console so the
# owner can verify the right .3dsx started. Short git hash (always used, even on
# a dirty tree) with a "-diag" suffix; missing git resolves to "nogit-diag".
TWLHD_GIT_HASH := $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null)
ifeq ($(strip $(TWLHD_GIT_HASH)),)
TWLHD_BUILD_ID_STR := nogit-diag
else
TWLHD_BUILD_ID_STR := $(TWLHD_GIT_HASH)-diag
endif
CFLAGS += -DTWLHD_BUILD_ID=\"$(TWLHD_BUILD_ID_STR)\"
```

**FACT — SD inputs** (the two frames copied as `sdmc:/twlhd_frame.bin`).
```
$ sha256sum <staging>/eq4zc/sd-green/twlhd_frame.bin <staging>/eq4zc/sd-blue/twlhd_frame.bin > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
bef610d857f7bde8f65aaf9e6602aea183ec52d7aec689f06e1d4bed4bf06e11  <staging>/eq4zc/sd-green/twlhd_frame.bin
79be41bbe2c310e3a238b385571e07be9ac61e41f043c8be150e81f2e18efcc0  <staging>/eq4zc/sd-blue/twlhd_frame.bin
```
**Render output — not a FACT.** The two Azahar renders ran once each with the commands recorded here (not re-run; core `<staging>/core.so`, build `9c38437`). Their full logs (463 lines each, mostly core trace and `software framebuffer` lines) are not reproduced here; this report is capped at 450 lines, so the render output carries no FACT label.
```
$ tools/azahar_host/build/azahar_host --core <staging>/core.so --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <staging>/eq4zc/sweep-green --sd-root <staging>/eq4zc/sd-green > <logfile> 2>&1
$ tools/azahar_host/build/azahar_host --core <staging>/core.so --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out <staging>/eq4zc/sweep-blue --sd-root <staging>/eq4zc/sd-blue > <logfile> 2>&1
```
**FACT — top 400x240 crop** (heredoc; output byte-identical to the committed PNGs).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
from PIL import Image
for name in ('sweep-green', 'sweep-blue'):
    Image.open(f'<staging>/eq4zc/{name}_199.ppm').convert('RGB').crop((0, 0, 400, 240)).save(
        f'tools/eq4zc_alpha_sweep/fixtures/eq4zc_{name.replace("-", "_")}_top400x240.png')
EOF
exit=0
```
**FACT — measurer, explicit `--azahar-correction on`** (both committed crops, full unfiltered output).
```
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json --mapping arm11 --azahar-correction on tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_sweep
basis=one 400x240 crop; quad interiors from the layout JSON; no model
image=400x240
layout=tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json
mapping=arm11 scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
presentation-row correction: applied (--azahar-correction on (forced))
source_rgb=0,255,0 source_channel=1 interior_shrink=2
quad0.alpha=1
quad0.rect_ds=19,48,47,87
quad0.interior=66,62,97,107
quad0.majority_rgb=0,16,0
quad0.measured_green=16
quad0.measured_blue=0
quad0.classification=blended
quad1.alpha=4
quad1.rect_ds=57,48,85,87
quad1.interior=113,62,144,107
quad1.majority_rgb=0,40,0
quad1.measured_green=40
quad1.measured_blue=0
quad1.classification=blended
quad2.alpha=8
quad2.rect_ds=95,48,123,87
quad2.interior=161,62,192,107
quad2.majority_rgb=0,72,0
quad2.measured_green=72
quad2.measured_blue=0
quad2.classification=blended
quad3.alpha=12
quad3.rect_ds=133,48,161,87
quad3.interior=208,62,239,107
quad3.majority_rgb=0,104,0
quad3.measured_green=104
quad3.measured_blue=0
quad3.classification=blended
quad4.alpha=15
quad4.rect_ds=171,48,199,87
quad4.interior=256,62,287,107
quad4.majority_rgb=0,128,0
quad4.measured_green=128
quad4.measured_blue=0
quad4.classification=blended
quad5.alpha=16
quad5.rect_ds=209,48,237,87
quad5.interior=303,62,334,107
quad5.majority_rgb=0,135,0
quad5.measured_green=135
quad5.measured_blue=0
quad5.classification=blended
quad6.alpha=20
quad6.rect_ds=19,105,47,144
quad6.interior=66,133,97,178
quad6.majority_rgb=0,167,0
quad6.measured_green=167
quad6.measured_blue=0
quad6.classification=blended
quad7.alpha=24
quad7.rect_ds=57,105,85,144
quad7.interior=113,133,144,178
quad7.majority_rgb=0,199,0
quad7.measured_green=199
quad7.measured_blue=0
quad7.classification=blended
quad8.alpha=26
quad8.rect_ds=95,105,123,144
quad8.interior=161,133,192,178
quad8.majority_rgb=0,215,0
quad8.measured_green=215
quad8.measured_blue=0
quad8.classification=blended
quad9.alpha=28
quad9.rect_ds=133,105,161,144
quad9.interior=208,133,239,178
quad9.majority_rgb=0,231,0
quad9.measured_green=231
quad9.measured_blue=0
quad9.classification=blended
quad10.alpha=31
quad10.rect_ds=171,105,199,144
quad10.interior=256,133,287,178
quad10.majority_rgb=0,255,0
quad10.measured_green=255
quad10.measured_blue=0
quad10.classification=not blended
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_layout.json --mapping arm11 --azahar-correction on tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_top400x240.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_sweep
basis=one 400x240 crop; quad interiors from the layout JSON; no model
image=400x240
layout=tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_layout.json
mapping=arm11 scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
presentation-row correction: applied (--azahar-correction on (forced))
source_rgb=0,0,255 source_channel=2 interior_shrink=2
quad0.alpha=1
quad0.rect_ds=19,48,47,87
quad0.interior=66,62,97,107
quad0.majority_rgb=0,0,16
quad0.measured_green=0
quad0.measured_blue=16
quad0.classification=blended
quad1.alpha=4
quad1.rect_ds=57,48,85,87
quad1.interior=113,62,144,107
quad1.majority_rgb=0,0,40
quad1.measured_green=0
quad1.measured_blue=40
quad1.classification=blended
quad2.alpha=8
quad2.rect_ds=95,48,123,87
quad2.interior=161,62,192,107
quad2.majority_rgb=0,0,72
quad2.measured_green=0
quad2.measured_blue=72
quad2.classification=blended
quad3.alpha=12
quad3.rect_ds=133,48,161,87
quad3.interior=208,62,239,107
quad3.majority_rgb=0,0,104
quad3.measured_green=0
quad3.measured_blue=104
quad3.classification=blended
quad4.alpha=15
quad4.rect_ds=171,48,199,87
quad4.interior=256,62,287,107
quad4.majority_rgb=0,0,128
quad4.measured_green=0
quad4.measured_blue=128
quad4.classification=blended
quad5.alpha=16
quad5.rect_ds=209,48,237,87
quad5.interior=303,62,334,107
quad5.majority_rgb=0,0,135
quad5.measured_green=0
quad5.measured_blue=135
quad5.classification=blended
quad6.alpha=20
quad6.rect_ds=19,105,47,144
quad6.interior=66,133,97,178
quad6.majority_rgb=0,0,167
quad6.measured_green=0
quad6.measured_blue=167
quad6.classification=blended
quad7.alpha=24
quad7.rect_ds=57,105,85,144
quad7.interior=113,133,144,178
quad7.majority_rgb=0,0,199
quad7.measured_green=0
quad7.measured_blue=199
quad7.classification=blended
quad8.alpha=26
quad8.rect_ds=95,105,123,144
quad8.interior=161,133,192,178
quad8.majority_rgb=0,0,215
quad8.measured_green=0
quad8.measured_blue=215
quad8.classification=blended
quad9.alpha=28
quad9.rect_ds=133,105,161,144
quad9.interior=208,133,239,178
quad9.majority_rgb=0,0,231
quad9.measured_green=0
quad9.measured_blue=231
quad9.classification=blended
quad10.alpha=31
quad10.rect_ds=171,105,199,144
quad10.interior=256,133,287,178
quad10.majority_rgb=0,0,255
quad10.measured_green=0
quad10.measured_blue=255
quad10.classification=not blended
```
**FACT — per-alpha table** (green sweep green channel; blue sweep blue channel). Alpha 31 is the full-colour endpoint, classified `not blended`; every other quad is `blended`.

| alpha | measured green channel | measured blue channel |
|---|---|---|
| 1 | 16 | 16 |
| 4 | 40 | 40 |
| 8 | 72 | 72 |
| 12 | 104 | 104 |
| 15 | 128 | 128 |
| 16 | 135 | 135 |
| 20 | 167 | 167 |
| 24 | 199 | 199 |
| 26 | 215 | 215 |
| 28 | 231 | 231 |
| 31 | 255 | 255 |

Alpha-16 green `135` and alpha-4 blue `40` reproduce the EQ-4z scene-4 blend render exactly (`docs/reports/eq4z-clear-black.md:443-455`).
## 5. Model comparison (FACT)

**FACT — the eq4zb library at the 11 sweep alphas** (DS convention and the six `OUR_CANDIDATES` at `(alpha, 255, 0)`; each name's two cells are green then blue, equal because only the 255/0 source bytes matter).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import sys; sys.path.insert(0, 'tools/eq4zb_blend_model')
import blend_model as m
alphas = [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
print("sweep_alpha,ds_green,ds_blue")
for a in alphas: print("%d,%d,%d" % (a, m.ds_green(a, backdrop5=0), m.ds_redblue(a, backdrop5=0)))
print("sweep_alpha," + ",".join("%s_g,%s_b" % (name, name) for name, _fn in m.OUR_CANDIDATES))
for a in alphas: print("%d,%s" % (a, ",".join(str(v) for _n, fn in m.OUR_CANDIDATES for v in (fn(a, 255, 0), fn(a, 255, 0)))))
EOF
exit=0
sweep_alpha,ds_green,ds_blue
1,12,8
4,36,33
8,69,66
12,101,99
15,125,123
16,134,132
20,166,165
24,198,198
26,215,214
28,231,231
31,255,255
sweep_alpha,exact_float_floor_g,exact_float_floor_b,exact_float_round_g,exact_float_round_b,alpha8_trunc_floor_g,alpha8_trunc_floor_b,alpha8_trunc_round_g,alpha8_trunc_round_b,alpha8_round_floor_g,alpha8_round_floor_b,alpha8_round_round_g,alpha8_round_round_b
1,15,15,16,16,15,15,15,15,16,16,16,16
4,39,39,40,40,39,39,39,39,40,40,40,40
8,71,71,72,72,71,71,71,71,72,72,72,72
12,103,103,104,104,103,103,103,103,104,104,104,104
15,127,127,128,128,127,127,127,127,128,128,128,128
16,135,135,135,135,135,135,135,135,135,135,135,135
20,167,167,167,167,167,167,167,167,167,167,167,167
24,199,199,199,199,199,199,199,199,199,199,199,199
26,215,215,215,215,215,215,215,215,215,215,215,215
28,231,231,231,231,231,231,231,231,231,231,231,231
31,255,255,255,255,255,255,255,255,255,255,255,255
```
**Equal / differs.** The emulator equals `exact_float_round` and `alpha8_round_round` exactly on both channels at every alpha (16, 40, 72, 104, 128, 135, 167, 199, 215, 231, 255). Against the DS convention it equals both channels only at 28 and 31, green only at 26 (blue +1), and differs at 1 (+4/+8), 4 (+4/+7), 8 (+3/+6), 12 (+3/+5), 15 (+3/+5), 16 (+1/+3), 20 (+1/+2), 24 (+1/+1); blue is lower under the DS convention because red/blue get 5-bit output quantisation, green 6-bit (`tools/eq4zb_blend_model/blend_model.py:63-70`). Nothing was tuned.

**HYPOTHESIS (not a source claim).** Consistent with the PICA blend unit rounding half-up at the 8-bit target; the two coinciding candidates cannot be separated and the `*_floor` candidates are rejected at alphas 1, 4, 8, 12, 15.
## 6. Console PLAN (text only, NOT RUN)

**SOURCE-READ — load each frame.** `arm11/source/main.c:36` opens `sdmc:/twlhd_frame.bin`; A loads it (`:580-582`; procedure in `docs/reports/eq4n-arm11-row-shift.md` §4). On hardware the operator presses A after the program starts (the `--press A:60-63` window is the host emulator's schedule, not an arm11 frame window).

**SOURCE-READ — capture mapping.** "The top screen is the top-left 400×240 crop; the arm11 render is 400×240, so the crop maps 1:1" (`eq4o-hw-row-shift.md:27-29`); the DS content is scaled 1.25x with offset x=40 by the clipfix (`common/twlhd_clipfix.c:40`), so the `arm11` and `capture` presets agree and the tests pin both.

**Steps (UNVERIFIED, owner-run later).** Build at HEAD (`make -C arm11`); copy the 3dsx and `eq4zc_sweep_green.bin` as `sdmc:/twlhd_frame.bin` (back up any existing file); launch, press A; capture the top LCD with `tools/cc3dsfs_capture` as 1280x720 and take the top-left 400x240 crop; repeat with the blue frame. Measure with hardware correction `off` (`--layout` green, then blue):
```
python3 tools/eq4zc_alpha_sweep/measure_sweep.py \
  --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json \
  --mapping arm11 --azahar-correction off <staging>/capture_green.png
```
**Expected numbers (EMULATOR run, §4).** If hardware matches the emulator: green/blue `1=16, 4=40, 8=72, 12=104, 15=128, 16=135, 20=167, 24=199, 26=215, 28=231, 31=255`, every quad except alpha 31 `blended`. If it matches the DS convention, expect the §5 columns; the measurer chooses neither.
**Run on hardware, see EQ-4zd:** `docs/reports/eq4zd-alpha-sweep-hw-result.md`.
## 7. Fixtures, pointers, tests and size

**FACT — fixtures** (`tools/eq4zc_alpha_sweep/fixtures/`; hashes also in `fixtures/README.md`; hash listing 2 runs, identical output; byte sizes last, each file well under 100 KB).
```
$ (cd tools/eq4zc_alpha_sweep/fixtures && sha256sum *.bin *.json *.png) > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
79be41bbe2c310e3a238b385571e07be9ac61e41f043c8be150e81f2e18efcc0  eq4zc_sweep_blue.bin
bef610d857f7bde8f65aaf9e6602aea183ec52d7aec689f06e1d4bed4bf06e11  eq4zc_sweep_green.bin
7f0fd7e95220e5442454e3d0d74faf392a2fa166afe04875794ba6b4d081588d  eq4zc_sweep_blue_layout.json
40f5446c70a24702528bd66290bf898fa6b5957ff53bc85324efc68e78a220a5  eq4zc_sweep_green_layout.json
f36134ca69f3ea6f3af58ead5bba3243bd1859a8738b49f8015514c69069c196  eq4zc_sweep_blue_top400x240.png
5207f2eee3339581a6cc16f31a9f3acd209dd4ede2b5016083519e614ffe5800  eq4zc_sweep_green_top400x240.png
$ (cd tools/eq4zc_alpha_sweep/fixtures && sha256sum *.bin *.json *.png) > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
79be41bbe2c310e3a238b385571e07be9ac61e41f043c8be150e81f2e18efcc0  eq4zc_sweep_blue.bin
bef610d857f7bde8f65aaf9e6602aea183ec52d7aec689f06e1d4bed4bf06e11  eq4zc_sweep_green.bin
7f0fd7e95220e5442454e3d0d74faf392a2fa166afe04875794ba6b4d081588d  eq4zc_sweep_blue_layout.json
40f5446c70a24702528bd66290bf898fa6b5957ff53bc85324efc68e78a220a5  eq4zc_sweep_green_layout.json
f36134ca69f3ea6f3af58ead5bba3243bd1859a8738b49f8015514c69069c196  eq4zc_sweep_blue_top400x240.png
5207f2eee3339581a6cc16f31a9f3acd209dd4ede2b5016083519e614ffe5800  eq4zc_sweep_green_top400x240.png
$ (cd tools/eq4zc_alpha_sweep/fixtures && wc -c *.bin *.json *.png) > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
1176 eq4zc_sweep_blue.bin
1176 eq4zc_sweep_green.bin
1431 eq4zc_sweep_blue_layout.json
1432 eq4zc_sweep_green_layout.json
 691 eq4zc_sweep_blue_top400x240.png
 690 eq4zc_sweep_green_top400x240.png
6596 total
```
The tests regenerate into a temp directory and compare byte for byte. **Append-only pointers** (one line each): `docs/reports/eq4zb-blend-residual.md` §6, `docs/EQ_SCENES.md`, `docs/EQUALITY_CHECK_DESIGN.md`. The new target is wired into `make test` like the siblings; full `make test` exits 0 (aggregate `Ran 435 tests ... OK (skipped=110)`) and no existing test/tolerance changed.

**FACT — size audit** (final edit; diff below 120000 bytes).
```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zc-alpha-sweep.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
68386
450 docs/reports/eq4zc-alpha-sweep.md
```
## 8. UNVERIFIED and extra runs

- **UNVERIFIED.** No console, capture board or hardware was used; the plan is text only and the expected numbers are the emulator table, not a hardware measurement. Nothing here says a real 3DS blend unit matches Azahar. The two indistinguishable candidates are not separated by this sweep; the PNGs are byte-identical copies of the heredoc crops, and `test_measure_sweep.py` pins both sha256s and the per-alpha numbers.
- **Extra runs (disclosed).** Each frame was rendered 1x and cropped 1x. The measurer ran 2x per crop (same numbers). `make -C arm11` ran 2x at `b9c317b` (build, then a no-op re-invocation). Targeted suite: 7 runs. C validator: built and run 1x. Measurer on each committed crop: 1x. RSTA, crop and model heredocs: 3x each. SD-input hashes, whole-tree status, HEAD, 3dsx sha256 and strings, and `git diff b9c317b HEAD`: 1x each. Fixture hash listing: 2 runs, identical output; fixture size listing: 1 run. The hashed 3dsx is the `b9c317b` build output; no frame was re-rendered.
