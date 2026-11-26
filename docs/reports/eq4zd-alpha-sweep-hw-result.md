<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4zd — real-hardware alpha sweep of the arm11 3dsx

**Labels.** **FACT** = quoted command with raw output (path placeholders are only `<staging>`, `<worktree>` and `<logfile>`); **SOURCE-READ** = a claim about source, cited by file/line; **HYPOTHESIS**/**UNVERIFIED** = not measured by the quoted output. No console was contacted in this task: the captures are the owner-reported hardware run, analysed offline. No production source changed.

## 1. Purpose and inputs

**SOURCE-READ.** `docs/reports/eq4zb-blend-residual.md:431-437` named a hardware sweep over the alphas where the EQ-4zb §3 deltas change; EQ-4zc produced the two frames and the measurer (`docs/reports/eq4zc-alpha-sweep.md:1-6`) and left the console run to the owner (`:395-407`). This task records that run: the owner's two 400x240 hardware crops of the committed EQ-4zc frames, measured with the unmodified `tools/eq4zc_alpha_sweep/measure_sweep.py` (`--mapping arm11 --azahar-correction off`), compared against the EQ-4zc emulator series, the six `tools/eq4zb_blend_model` candidates and the DS convention (`tools/ds_alpha_test/expected.py`). The two committed crops are the hardware FACTs; every table below derives from them.

Basis: ONE console; ONE capture session per frame (passive capture board, `cc3dsfs`, 1280x720 root screenshot, top-left 400x240 crop; each frame captured 3 times and the three crops byte-identical). The 3dsx that ran was `a7c315f9…`, built from `main` `dfaf611`, not rebuilt; the frames are the committed `tools/eq4zc_alpha_sweep/fixtures/` `.bin` files loaded as `sdmc:/twlhd_frame.bin` with A. Provenance and all hashes: `tools/eq4zd_sweep_hw/README.md`.

## 2. Fixtures (FACT)

**FACT — fixture sha256 and byte sizes** (copies of `<staging>/hw/hw_sweep_green_a.png` and `<staging>/hw/hw_sweep_blue_a.png`; each well under 100 KB).
```
$ sha256sum tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
6094a04de35a7fb8db97c4766c06a30ff3e2314118df78196a70c16852a56760  tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png
656b9910a2211d14ee89acb3f5ad658310bc857bfb917d7fd1b9343dd86116c5  tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png
$ wc -c tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
698 tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png
694 tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png
```
## 3. Measurement runs (FACT)

Both runs reuse `tools/eq4zc_alpha_sweep/measure_sweep.py` unchanged, hardware correction `off` (rows 0/239 are not uniform black on the capture board, so `auto` would be undetermined).

**FACT — green sweep, green channel.**
```
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json --mapping arm11 --azahar-correction off tools/eq4zd_sweep_hw/hw_eq4zd_sweep_green_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_sweep
basis=one 400x240 crop; quad interiors from the layout JSON; no model
image=400x240
layout=tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_green_layout.json
mapping=arm11 scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
presentation-row correction: not applied (--azahar-correction off)
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
quad2.majority_rgb=0,73,0
quad2.measured_green=73
quad2.measured_blue=0
quad2.classification=blended
quad3.alpha=12
quad3.rect_ds=133,48,161,87
quad3.interior=208,62,239,107
quad3.majority_rgb=0,105,0
quad3.measured_green=105
quad3.measured_blue=0
quad3.classification=blended
quad4.alpha=15
quad4.rect_ds=171,48,199,87
quad4.interior=256,62,287,107
quad4.majority_rgb=0,130,0
quad4.measured_green=130
quad4.measured_blue=0
quad4.classification=blended
quad5.alpha=16
quad5.rect_ds=209,48,237,87
quad5.interior=303,62,334,107
quad5.majority_rgb=0,138,0
quad5.measured_green=138
quad5.measured_blue=0
quad5.classification=blended
quad6.alpha=20
quad6.rect_ds=19,105,47,144
quad6.interior=66,133,97,178
quad6.majority_rgb=0,170,0
quad6.measured_green=170
quad6.measured_blue=0
quad6.classification=blended
quad7.alpha=24
quad7.rect_ds=57,105,85,144
quad7.interior=113,133,144,178
quad7.majority_rgb=0,203,0
quad7.measured_green=203
quad7.measured_blue=0
quad7.classification=blended
quad8.alpha=26
quad8.rect_ds=95,105,123,144
quad8.interior=161,133,192,178
quad8.majority_rgb=0,219,0
quad8.measured_green=219
quad8.measured_blue=0
quad8.classification=blended
quad9.alpha=28
quad9.rect_ds=133,105,161,144
quad9.interior=208,133,239,178
quad9.majority_rgb=0,235,0
quad9.measured_green=235
quad9.measured_blue=0
quad9.classification=blended
quad10.alpha=31
quad10.rect_ds=171,105,199,144
quad10.interior=256,133,287,178
quad10.majority_rgb=0,255,0
quad10.measured_green=255
quad10.measured_blue=0
quad10.classification=not blended
```
**FACT — blue sweep, blue channel.**
```
$ python3 tools/eq4zc_alpha_sweep/measure_sweep.py --layout tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_layout.json --mapping arm11 --azahar-correction off tools/eq4zd_sweep_hw/hw_eq4zd_sweep_blue_a.png > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
tool=measure_sweep
basis=one 400x240 crop; quad interiors from the layout JSON; no model
image=400x240
layout=tools/eq4zc_alpha_sweep/fixtures/eq4zc_sweep_blue_layout.json
mapping=arm11 scale_x=1.25 scale_y=1.25 offset_x=40 offset_y=0
presentation-row correction: not applied (--azahar-correction off)
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
quad1.majority_rgb=0,0,41
quad1.measured_green=0
quad1.measured_blue=41
quad1.classification=blended
quad2.alpha=8
quad2.rect_ds=95,48,123,87
quad2.interior=161,62,192,107
quad2.majority_rgb=0,0,74
quad2.measured_green=0
quad2.measured_blue=74
quad2.classification=blended
quad3.alpha=12
quad3.rect_ds=133,48,161,87
quad3.interior=208,62,239,107
quad3.majority_rgb=0,0,107
quad3.measured_green=0
quad3.measured_blue=107
quad3.classification=blended
quad4.alpha=15
quad4.rect_ds=171,48,199,87
quad4.interior=256,62,287,107
quad4.majority_rgb=0,0,132
quad4.measured_green=0
quad4.measured_blue=132
quad4.classification=blended
quad5.alpha=16
quad5.rect_ds=209,48,237,87
quad5.interior=303,62,334,107
quad5.majority_rgb=0,0,140
quad5.measured_green=0
quad5.measured_blue=140
quad5.classification=blended
quad6.alpha=20
quad6.rect_ds=19,105,47,144
quad6.interior=66,133,97,178
quad6.majority_rgb=0,0,173
quad6.measured_green=0
quad6.measured_blue=173
quad6.classification=blended
quad7.alpha=24
quad7.rect_ds=57,105,85,144
quad7.interior=113,133,144,178
quad7.majority_rgb=0,0,206
quad7.measured_green=0
quad7.measured_blue=206
quad7.classification=blended
quad8.alpha=26
quad8.rect_ds=95,105,123,144
quad8.interior=161,133,192,178
quad8.majority_rgb=0,0,222
quad8.measured_green=0
quad8.measured_blue=222
quad8.classification=blended
quad9.alpha=28
quad9.rect_ds=133,105,161,144
quad9.interior=208,133,239,178
quad9.majority_rgb=0,0,239
quad9.measured_green=0
quad9.measured_blue=239
quad9.classification=blended
quad10.alpha=31
quad10.rect_ds=171,105,199,144
quad10.interior=256,133,287,178
quad10.majority_rgb=0,0,255
quad10.measured_green=0
quad10.measured_blue=255
quad10.classification=not blended
```
These reproduce the owner-side preliminary values exactly: green `16,40,73,105,130,138,170,203,219,235,255`, blue `16,41,74,107,132,140,173,206,222,239,255`; alpha 31 `not blended`, every other quad `blended`.

## 4. Per-alpha tables

Emulator column from the committed EQ-4zc crops/report (`docs/reports/eq4zc-alpha-sweep.md:338-350`, both channels equal there). eq4zb candidates: all six `OUR_CANDIDATES` at `(alpha, 255, 0)` collapse to two values per point at alpha 1–15 — the lower group `exact_float_floor`, `alpha8_trunc_floor`, `alpha8_trunc_round`, the higher group `exact_float_round`, `alpha8_round_floor`, `alpha8_round_round` (`tools/eq4zb_blend_model/blend_model.py:139-146`). DS convention: green `expected8(a, 0, "A2", "6bit_round")`, blue `expected6_565(a, 0, "A2")[0]` (EQ-4i §5.1/§5.2; `tools/ds_alpha_test/expected.py:97-127`).

**Green and blue sweeps, side by side** (hw = the FACTs above; emu from `docs/reports/eq4zc-alpha-sweep.md:338-350`, both channels equal there; Δ = hw − DS).

| alpha | hw G | emu G | G cand (floor–round) | DS G | ΔG | hw B | emu B | B cand | DS B | ΔB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 16 | 16 | 15–16 | 12 | +4 | 16 | 16 | 15–16 | 8 | +8 |
| 4 | 40 | 40 | 39–40 | 36 | +4 | 41 | 40 | 39–40 | 33 | +8 |
| 8 | 73 | 72 | 71–72 | 69 | +4 | 74 | 72 | 71–72 | 66 | +8 |
| 12 | 105 | 104 | 103–104 | 101 | +4 | 107 | 104 | 103–104 | 99 | +8 |
| 15 | 130 | 128 | 127–128 | 125 | +5 | 132 | 128 | 127–128 | 123 | +9 |
| 16 | 138 | 135 | 135 | 134 | +4 | 140 | 135 | 135 | 132 | +8 |
| 20 | 170 | 167 | 167 | 166 | +4 | 173 | 167 | 167 | 165 | +8 |
| 24 | 203 | 199 | 199 | 198 | +5 | 206 | 199 | 199 | 198 | +8 |
| 26 | 219 | 215 | 215 | 215 | +4 | 222 | 215 | 215 | 214 | +8 |
| 28 | 235 | 231 | 231 | 231 | +4 | 239 | 231 | 231 | 231 | +8 |
| 31 | 255 | 255 | 255 | 255 | 0 | 255 | 255 | 255 | 255 | 0 |

**FACT — the eq4zb library at the 11 sweep alphas** (DS convention and the six candidates at `(alpha, 255, 0)`; each name's two cells are green then blue).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import sys
sys.path.insert(0, 'tools/eq4zb_blend_model')
import blend_model as m
alphas = [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
print("sweep_alpha,ds_green,ds_blue")
for a in alphas:
    print("%d,%d,%d" % (a, m.ds_green(a, backdrop5=0), m.ds_redblue(a, backdrop5=0)))
print("sweep_alpha," + ",".join("%s_g,%s_b" % (name, name) for name, _fn in m.OUR_CANDIDATES))
for a in alphas:
    print("%d,%s" % (a, ",".join(str(v) for _n, fn in m.OUR_CANDIDATES for v in (fn(a, 255, 0), fn(a, 255, 0)))))
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
**FACT — DS convention A2 at the same alphas** (green 6-bit; blue 5-bit 565 component).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import sys
sys.path.insert(0, 'tools/ds_alpha_test')
import expected as e
alphas = [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
print("sweep_alpha,ds_green_A2_6bit,ds_blue_A2_565")
for a in alphas:
    print("%d,%d,%d" % (a, e.expected8(a, 0, "A2", "6bit_round"),
                        e.expected6_565(a, 0, "A2")[0]))
EOF
exit=0
sweep_alpha,ds_green_A2_6bit,ds_blue_A2_565
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
```
The DS anchors agree with the only two DS hardware points: alpha-16 green `134` and alpha-4 blue `33` (`docs/reports/eq4w-blend-hw-result.md:268-290`; `docs/reports/eq4i-alpha-hw-result.md:683-706`).

## 5. Shape of the arm11 hardware response

Derived arithmetically from the FACT series in §3 (raw = `(a+1)/32*255`).

| alpha | raw | hw G − raw | hw B − raw | hw B − hw G |
|---|---|---|---|---|
| 1 | 15.94 | +0.06 | +0.06 | 0 |
| 4 | 39.84 | +0.16 | +1.16 | +1 |
| 8 | 71.72 | +1.28 | +2.28 | +1 |
| 12 | 103.59 | +1.41 | +3.41 | +2 |
| 15 | 127.50 | +2.50 | +4.50 | +2 |
| 16 | 135.47 | +2.53 | +4.53 | +2 |
| 20 | 167.34 | +2.66 | +5.66 | +3 |
| 24 | 199.22 | +3.78 | +6.78 | +3 |
| 26 | 215.16 | +3.84 | +6.84 | +3 |
| 28 | 231.09 | +3.91 | +7.91 | +4 |
| 31 | 255.00 | +0.00 | +0.00 | 0 |
- **Green** tracks `(a+1)/32*255` closely at low alpha, then runs above it by an increasing margin (+0.06 at alpha 1 to +3.91 at alpha 28) and lands exactly on 255 at alpha 31.
- **Blue is always above green** for the same alpha: the gap is 0 at alpha 1, then +1, +1, +2, +2, +2, +3, +3, +3, +4 — it grows monotonically to +4 at alpha 28, and returns to 0 at alpha 31. Blue runs above its raw value by up to +7.91.
- **Alpha 31 is exact** in both channels (`255`, `not blended`); the saturation is the same in hardware, emulator and model.
- The response is monotone and smooth; no quad is an outlier. The surplus is not a constant offset — it scales with the blend weight and vanishes only at full opacity.

## 6. Which modelled candidates are excluded

Max absolute error of each candidate vs the hardware series, per channel (derived from the §3 FACTs and the §4 candidates).

| candidate | max err green | max err blue |
|---|---|---|
| `exact_float_floor` | 4 | 8 |
| `exact_float_round` | 4 | 8 |
| `alpha8_trunc_floor` | 4 | 8 |
| `alpha8_trunc_round` | 4 | 8 |
| `alpha8_round_floor` | 4 | 8 |
| `alpha8_round_round` | 4 | 8 |
| DS A2 convention | 5 | 9 |

**All six source-derived candidates are excluded** by the 11-point series: each misses the hardware by at least 4 green / 8 blue levels somewhere. In particular the candidates that matched the emulator exactly (`exact_float_round` and `alpha8_round_round` per the EQ-4zc report's own list, `docs/reports/eq4zc-alpha-sweep.md:353-394`; `alpha8_round_floor` also equals the emulator series at these 11 alphas) are 135/135 at alpha 16 where the hardware is 138/140, so the emulator match does not carry over to the console. The candidates that really miss at alpha 1 are `exact_float_floor`, `alpha8_trunc_floor` and `alpha8_trunc_round` (15 where the hardware is 16). **The DS convention is also excluded** (5 green / 9 blue): the hardware is uniformly above it, not equal to it. No model was fitted and no candidate was tuned; these are the six library functions called as written.

## 7. Verdict on the residual and the formula HYPOTHESIS

**Where the gap is largest.** Against the DS convention the gap is largest at **alpha 15**: green `130` vs `125` = **+5**, blue `132` vs `123` = **+9** (green is also +5 at alpha 24: `203` vs `198`). Elsewhere it is a steady **+4 green / +8 blue**, collapsing to 0 at alpha 31. The +4/+8 pattern first seen at EQ-4za (hardware 138/41 vs DS 134/33) therefore holds across the whole series, not just at alpha 16/4.

**HYPOTHESIS (not a hardware finding).** The series is consistent, for all 22 points with max error 0, with a clamped linear gain:
`green = min(255, trunc(260·(a+1)/32))`, `blue = min(255, trunc(264·(a+1)/32))`.
That is the ideal `(a+1)/32·255` scaled by 260/255 (green, ≈ +1.96 %) and 264/255 (blue, ≈ +3.53 %) and saturated at 255. It is one simple formula that reproduces every point, but it is a description of the data, not evidence about the mechanism: it has no source basis and must not be read as the PICA blend law. **FACT — the formula at all 11 alphas.**
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import math
alphas = [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
hw_green = [16, 40, 73, 105, 130, 138, 170, 203, 219, 235, 255]
hw_blue = [16, 41, 74, 107, 132, 140, 173, 206, 222, 239, 255]
print("alpha,hw_green,min255_trunc260,hw_blue,min255_trunc264")
eg = eb = 0
for a, g, b in zip(alphas, hw_green, hw_blue):
    fg = min(255, int(math.floor(260 * (a + 1) / 32.0)))
    fb = min(255, int(math.floor(264 * (a + 1) / 32.0)))
    eg = max(eg, abs(fg - g))
    eb = max(eb, abs(fb - b))
    print("%d,%d,%d,%d,%d" % (a, g, fg, b, fb))
print("max_err_green=%d max_err_blue=%d" % (eg, eb))
EOF
exit=0
alpha,hw_green,min255_trunc260,hw_blue,min255_trunc264
1,16,16,16,16
4,40,40,41,41
8,73,73,74,74
12,105,105,107,107
15,130,130,132,132
16,138,138,140,140
20,170,170,173,173
24,203,203,206,206
26,219,219,222,222
28,235,235,239,239
31,255,255,255,255
max_err_green=0 max_err_blue=0
```
**HYPOTHESIS for the cause (cited source, not established).** The repository emits the blend weight `w = (a+1)/32` (`common/twlhd_gx_interp.c:395-397`) into `ADD(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` on a 32-bit RGBA8 target (`arm11/include/twlhd_blend_config.h:30-31`, `arm11/source/renderer.c:757-758`) with a lossless RGB8 display transfer (`arm11/source/renderer.c:47-52`). A surplus that grows with `w` and saturates at 255 is consistent with a rounding/gain step in the one invisible part of that path — the PICA float→8-bit conversion or the blend-unit rounding — or in the capture path, none of which appears in the repository and none of which the six candidates `tools/eq4zb_blend_model/blend_model.py:16-27,139-146` capture. The DS convention itself is 6-bit A2 with a 6→8 output rounding (`tools/ds_alpha_test/expected.py:97-127`); the hardware sitting *above* it rules out a pure DS-side explanation. The cause is **UNVERIFIED**; nothing here was fitted into a mechanism.

## 8. Proposal (text only, NOT applied)

**Smallest arm11-side change.** Pre-compensate the source alpha at the interpreter blend-weight site (`common/twlhd_gx_interp.c:395-397`, `gx_alpha_float`): map each DS alpha `a` to a compensated byte `a'` chosen so the data-derived response `min(255, trunc(gain·(a'+1)/32))` is closest to the DS A2 value. The green and blue sweeps are single-channel frames, so the table is per channel; a mixed scene shares one alpha byte and cannot use both. `a'=0` is deliberately not used: alpha 0 is the DS wireframe case and emits weight 0.0 (`common/twlhd_gx_interp.c:388-397`), so `a'` is restricted to 1..31.

**FACT — compensated byte, expected value and residual per level** (expected computed from the §7 formula; residual = expected − DS).
```
$ python3 - <<'EOF' > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
import math
alphas = [1, 4, 8, 12, 15, 16, 20, 24, 26, 28, 31]
ds_green = [12, 36, 69, 101, 125, 134, 166, 198, 215, 231, 255]
ds_blue = [8, 33, 66, 99, 123, 132, 165, 198, 214, 231, 255]
def resp(a, gain):
    return min(255, int(math.floor(gain * (a + 1) / 32.0)))
def best(ds, gain):
    return min(range(1, 32), key=lambda ap: (abs(resp(ap, gain) - ds), ap))
print("channel,ds_alpha,ds_value,comp_alpha,expected,residual")
for a, ds in zip(alphas, ds_green):
    ap = best(ds, 260)
    print("green,%d,%d,%d,%d,%+d" % (a, ds, ap, resp(ap, 260), resp(ap, 260) - ds))
for a, ds in zip(alphas, ds_blue):
    ap = best(ds, 264)
    print("blue,%d,%d,%d,%d,%+d" % (a, ds, ap, resp(ap, 264), resp(ap, 264) - ds))
print("max_residual_green=%d max_residual_blue=%d" % (
    max(abs(resp(best(d, 260), 260) - d) for d in ds_green),
    max(abs(resp(best(d, 264), 264) - d) for d in ds_blue)))
EOF
exit=0
channel,ds_alpha,ds_value,comp_alpha,expected,residual
green,1,12,1,16,+4
green,4,36,3,32,-4
green,8,69,7,65,-4
green,12,101,11,97,-4
green,15,125,14,121,-4
green,16,134,15,130,-4
green,20,166,19,162,-4
green,24,198,23,195,-3
green,26,215,25,211,-4
green,28,231,27,227,-4
green,31,255,31,255,+0
blue,1,8,1,16,+8
blue,4,33,3,33,+0
blue,8,66,7,66,+0
blue,12,99,11,99,+0
blue,15,123,14,123,+0
blue,16,132,15,132,+0
blue,20,165,19,165,+0
blue,24,198,23,198,+0
blue,26,214,25,214,+0
blue,28,231,27,231,+0
blue,31,255,30,255,+0
max_residual_green=4 max_residual_blue=8
```
- **Green gains nothing.** One alpha step moves the response by ≈8 levels, twice the 4-level gap, so the best `a'` merely flips the error from +4 to −4. Pre-compensation cannot bring green to the DS convention by whole alpha steps.
- **Blue can be made exact for alpha ≥ 4** with `a' = a−1` (33→33, 66→66, …, 231→231; alpha 31 → `a'=30` → 255). Alpha 1 cannot improve (needs `a'=0`, blocked by the wireframe case), and its residual stays +8.
- **Risk.** A per-alpha LUT in `gx_alpha_float` changes every blended polygon, not just the sweep; the response is derived from one console, one program and two frames; one vertex alpha cannot serve green and blue at once in a mixed scene (`a−1` helps blue but costs green 8 levels); and `a'=0` collides with the wireframe path. A per-channel fix would need a different mechanism (e.g. output scaling after the blend), which is a larger change than a vertex-alpha LUT.
- **Verification plan.** Re-run the same two sweep frames on the console with the identical procedure (passive capture board, `cc3dsfs`, 1280x720 root screenshot, top-left 400x240 crop, 3 byte-identical captures per frame), then re-measure with `measure_sweep.py --mapping arm11 --azahar-correction off` and compare against the §4 tables.
- **Recommendation: not worth doing as a blanket change.** The residual is at most 5 levels of green (≈ 2.0 % of 255) and 9 of blue (≈ 3.5 %); green cannot be improved by alpha pre-compensation at all, blue is exact only for `a ≥ 4`, and any interpreter LUT touches every blend in every scene. If the blue gap matters, run it as a blue-only diagnostic experiment, not as production behaviour.

## 9. Pointers, tests and size

- Pointers (append-only): `docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ_SCENES.md`, and the EQ-4zc console PLAN (`docs/reports/eq4zc-alpha-sweep.md:408`).
- `tools/eq4zd_sweep_hw/test_eq4zd_sweep_hw.py` pins both fixture sha256s and the 11+11 measured values; the negative tests measure the EQ-4zc emulator crops and assert they differ from the hardware series (alpha-16 green 135 vs 138, full-tuple inequality); wired into `make test` via `test-eq4zd-sweep-hw`.
- **FACT — size audit** (final edit; diff below 90000 bytes, report ≤ 450 lines).
```
$ { git add -N . && git diff main | wc -c; wc -l docs/reports/eq4zd-alpha-sweep-hw-result.md; } > <logfile> 2>&1; echo "exit=$?"; cat <logfile>
exit=0
40978
447
```
## 10. UNVERIFIED and extra runs

- **UNVERIFIED.** Other programs and frames; the colour behaviour of the capture path (the crops carry the board's own RGB, not a calibrated reference); DS values at these 11 alphas on real DS hardware — only alpha-16 green (`134`) and alpha-4 blue (`33`) were measured on a DS (EQ-4i/EQ-4w), the other nine DS points are the `expected.py` model; and the PICA blend internals (float→8-bit conversion, blend-unit rounding), which appear in no repository line. The §7 gain formula is a description of the 22 points, not a claim about the hardware.
- **Extra runs (disclosed).** Each hardware crop was measured 1x; the emulator crops were measured by the test suite only. The eq4zb library heredoc ran 1x and the `expected.py` heredoc 1x. The §5 shape and §6 max-error numbers were computed 1x each and are presented as tables over the quoted FACT series. The formula heredoc ran 2x: the first pass omitted the alpha-31 clamp and gave 260/264 at alpha 31; the quoted second pass adds `min(255, …)`. The compensation heredoc ran 2x: the first allowed `a'=0`; the quoted pass restricts to 1..31 because alpha 0 is the wireframe case. Fixture hashing and sizing each ran 1x (the suite re-hashes them on every run). `make test-eq4zd-sweep-hw` ran 2x (targeted, then as part of `make test`); `make test` ran 1x, before the final size audit; the size audit ran 7x (the quoted run is the last).
