<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4i — DS polygon-alpha on real hardware: the green channel says A2, `verdict=A1` is a model artefact

**Status:** REAL HARDWARE MEASURED (offline analysis of the owner's capture).
One console, one capture session, one ROM. The current tool's whole-RGB
`verdict=A1` on the hardware capture is a **classifier-model artefact**: the
green channel of the same pixels matches the DeSmuME-style 6-bit **A2**
expectation, and red/blue match the 5-bit quantisation of those same 6-bit
values. The evidence is in §3–§5; every number is quoted from a command run in
this round.

```
basis=one console, one capture session (4 shots from 2 distinct frames), one ROM
```

Labels follow `docs/reports/eq4h-alpha-convention-test.md`: **FACT** = a command
and its complete raw output quoted here; **SOURCE-READ** = quoted source
file:line; **HYPOTHESIS** / **UNVERIFIED** = not measured here.

**Question.** For a DS translucent polygon with 5-bit polygon alpha `a`
(1..30), is the blend weight `A1 = a/31` or `A2 = (a+1)/32`? EQ-4h could answer
it only for DeSmuME (EMULATOR). This round records the first real-hardware
capture and separates the per-channel behaviour from the whole-RGB verdict.

## 1. Setup (OWNER-REPORTED run; offline analysis only)

- **ROM launch — OWNER-REPORTED.** The owner ran `ds-alpha-test.nds` (built by
  EQ-4h from `tools/ds_alpha_test/`) on a **New 3DS in DS mode** via **TWiLight
  Menu++** on 2026-11-23. No hardware action or console access was performed in
  this task; the run is reported by the owner, not reproduced here.
- **Capture — OWNER-REPORTED.** A passive capture board through `cc3dsfs`
  recorded the **top screen unscaled** as four 1280×720 root screenshots
  `al1_1.png .. al1_4.png`. The 400×240 top-left crops are
  `al1_N_top400x240.png` in the same directory (`$HOME/3ds/hwtest-staging/cc3d-out/`).
- **Commands run in this round (offline):** `sha256sum`, a PIL pixel diff,
  and `measure_alpha.py` (current `main`, then the extended tool). The fixture
  is the only binary added to the repo; no xwd or 1280×720 file is committed.

## 2. Fixture integrity (REQ 1)

`al1_3_top400x240.png` was copied verbatim into
`tools/ds_alpha_test/fixtures/`. FACT — source and copy are byte-identical
(the tool prints the absolute source path; its home component is shown as
`$HOME`):

```
$ sha256sum "$HOME/3ds/hwtest-staging/cc3d-out/al1_3_top400x240.png" tools/ds_alpha_test/fixtures/al1_3_top400x240.png
d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232  $HOME/3ds/hwtest-staging/cc3d-out/al1_3_top400x240.png
d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232  tools/ds_alpha_test/fixtures/al1_3_top400x240.png
$ cmp "$HOME/3ds/hwtest-staging/cc3d-out/al1_3_top400x240.png" tools/ds_alpha_test/fixtures/al1_3_top400x240.png && echo identical
identical
$ ls -l tools/ds_alpha_test/fixtures/al1_3_top400x240.png
-rw-rw-r-- <user> <group> 4744 Nov 23 16:32 tools/ds_alpha_test/fixtures/al1_3_top400x240.png
```

**The four crops are not four identical shots.** They are two identical pairs.
FACT — `sha256sum` over the four crops and a PIL per-pixel diff (the diff
prints no paths):

```
$ sha256sum "$HOME"/3ds/hwtest-staging/cc3d-out/al1_*_top400x240.png
d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232  $HOME/.../al1_1_top400x240.png
9338d81b869872125bea08a4c51ab0337c744409f6093766d32a26f7ee598774  $HOME/.../al1_2_top400x240.png
d6267d821d8492ff5f91c58e1065af0094f054c39c18cdb55d6af81f78ef4232  $HOME/.../al1_3_top400x240.png
9338d81b869872125bea08a4c51ab0337c744409f6093766d32a26f7ee598774  $HOME/.../al1_4_top400x240.png

$ DIR="$HOME/3ds/hwtest-staging/cc3d-out" python3 - <<'PYEOF'
import os
from PIL import Image, ImageChops
d = os.environ["DIR"]
def load(n):
    im = Image.open(os.path.join(d, "al1_%s_top400x240.png" % n))
    im.load()
    return im
for a, b in (("1", "3"), ("2", "4"), ("1", "2")):
    diff = ImageChops.difference(load(a), load(b))
    px = diff.load()
    ndiff = 0
    maxdelta = 0
    for y in range(diff.height):
        for x in range(diff.width):
            r, g, bl = px[x, y]
            ndiff += 1 if (r or g or bl) else 0
            maxdelta = max(maxdelta, r, g, bl)
    print("al1_%s vs al1_%s bbox= %s ndiff= %d maxdelta= %d" % (
        a, b, diff.getbbox(), ndiff, maxdelta))
PYEOF
al1_1 vs al1_3 bbox= None ndiff= 0 maxdelta= 0
al1_2 vs al1_4 bbox= None ndiff= 0 maxdelta= 0
al1_1 vs al1_2 bbox= (42, 0, 359, 231) ndiff= 1246 maxdelta= 9
```

So `al1_1 == al1_3` and `al1_2 == al1_4` exactly, but the two frames differ on
1246 pixels by at most 9 levels. **Only `al1_3` is committed** (as the user task
requires), and it is one of the two distinct frames. As a robustness check the
extended tool was also run on the other distinct frame `al1_2`: it returns the
same `g_votes_A1=0 g_votes_A2=10 g_votes_ambiguous=4 g_channel_verdict=A2`
(fit `6bit_565` A2 R/G/B = 0/3/0), so the pair difference does not change the
result. FACT — raw lines of that run (relevant lines only, in output order):

```
$ PYTHONDONTWRITEBYTECODE=1 python3 -B measure_alpha.py "$HOME/3ds/hwtest-staging/cc3d-out/al1_2_top400x240.png"
verdict=A1
fit_6bit_565_A2_R=0
fit_6bit_565_A2_G=3
fit_6bit_565_A2_B=0
g_votes_A1=0
g_votes_A2=10
g_votes_ambiguous=4
g_channel_verdict=A2
verdict_note=channels disagree; G channel=A2 but verdict=A1; verdict= is not decisive when channels disagree
```

## 3. Current `measure_alpha.py` on the fixture (REQ 2, before extension)

FACT — complete raw output of the unmodified (`main`) tool on the committed
fixture:

```
$ cd tools/ds_alpha_test
$ PYTHONDONTWRITEBYTECODE=1 python3 -B measure_alpha.py fixtures/al1_3_top400x240.png
mode=capture
geometry_found=1
backdrop_black_measured=0,0,0
backdrop_grey_measured=132,134,132
quad_L4_measured=33,36,33
quad_L4_A1_5bit_round=33,33,33
quad_L4_A2_5bit_round=41,41,41
quad_L4_A1_5bit_floor=33,33,33
quad_L4_A2_5bit_floor=33,33,33
quad_L4_A1_8bit_round=33,33,33
quad_L4_A2_8bit_round=40,40,40
quad_L4_A1_8bit_floor=32,32,32
quad_L4_A2_8bit_floor=39,39,39
quad_L4_A1_6bit_round=32,32,32
quad_L4_A2_6bit_round=36,36,36
quad_L4_nearest=A1
quad_L8_measured=66,69,66
quad_L8_A1_5bit_round=66,66,66
quad_L8_A2_5bit_round=74,74,74
quad_L8_A1_5bit_floor=66,66,66
quad_L8_A2_5bit_floor=66,66,66
quad_L8_A1_8bit_round=66,66,66
quad_L8_A2_8bit_round=72,72,72
quad_L8_A1_8bit_floor=65,65,65
quad_L8_A2_8bit_floor=71,71,71
quad_L8_A1_6bit_round=65,65,65
quad_L8_A2_6bit_round=69,69,69
quad_L8_nearest=A1
quad_L12_measured=99,101,99
quad_L12_A1_5bit_round=99,99,99
quad_L12_A2_5bit_round=107,107,107
quad_L12_A1_5bit_floor=99,99,99
quad_L12_A2_5bit_floor=99,99,99
quad_L12_A1_8bit_round=99,99,99
quad_L12_A2_8bit_round=104,104,104
quad_L12_A1_8bit_floor=98,98,98
quad_L12_A2_8bit_floor=103,103,103
quad_L12_A1_6bit_round=97,97,97
quad_L12_A2_6bit_round=101,101,101
quad_L12_nearest=A1
quad_L16_measured=132,134,132
quad_L16_A1_5bit_round=132,132,132
quad_L16_A2_5bit_round=132,132,132
quad_L16_A1_5bit_floor=132,132,132
quad_L16_A2_5bit_floor=132,132,132
quad_L16_A1_8bit_round=132,132,132
quad_L16_A2_8bit_round=135,135,135
quad_L16_A1_8bit_floor=131,131,131
quad_L16_A2_8bit_floor=135,135,135
quad_L16_A1_6bit_round=130,130,130
quad_L16_A2_6bit_round=134,134,134
quad_L16_nearest=A1
quad_L20_measured=165,166,165
quad_L20_A1_5bit_round=165,165,165
quad_L20_A2_5bit_round=165,165,165
quad_L20_A1_5bit_floor=165,165,165
quad_L20_A2_5bit_floor=165,165,165
quad_L20_A1_8bit_round=165,165,165
quad_L20_A2_8bit_round=167,167,167
quad_L20_A1_8bit_floor=164,164,164
quad_L20_A2_8bit_floor=167,167,167
quad_L20_A1_6bit_round=162,162,162
quad_L20_A2_6bit_round=166,166,166
quad_L20_nearest=ambiguous
quad_L24_measured=198,199,198
quad_L24_A1_5bit_round=198,198,198
quad_L24_A2_5bit_round=198,198,198
quad_L24_A1_5bit_floor=198,198,198
quad_L24_A2_5bit_floor=198,198,198
quad_L24_A1_8bit_round=197,197,197
quad_L24_A2_8bit_round=199,199,199
quad_L24_A1_8bit_floor=197,197,197
quad_L24_A2_8bit_floor=199,199,199
quad_L24_A1_6bit_round=194,194,194
quad_L24_A2_6bit_round=198,198,198
quad_L24_nearest=ambiguous
quad_L28_measured=231,231,231
quad_L28_A1_5bit_round=231,231,231
quad_L28_A2_5bit_round=231,231,231
quad_L28_A1_5bit_floor=231,231,231
quad_L28_A2_5bit_floor=231,231,231
quad_L28_A1_8bit_round=230,230,230
quad_L28_A2_8bit_round=231,231,231
quad_L28_A1_8bit_floor=230,230,230
quad_L28_A2_8bit_floor=231,231,231
quad_L28_A1_6bit_round=227,227,227
quad_L28_A2_6bit_round=231,231,231
quad_L28_nearest=ambiguous
quad_R4_measured=148,150,148
quad_R4_A1_5bit_round=148,148,148
quad_R4_A2_5bit_round=148,148,148
quad_R4_A1_5bit_floor=140,140,140
quad_R4_A2_5bit_floor=148,148,148
quad_R4_A1_8bit_round=148,148,148
quad_R4_A2_8bit_round=151,151,151
quad_R4_A1_8bit_floor=147,147,147
quad_R4_A2_8bit_floor=151,151,151
quad_R4_A1_6bit_round=146,146,146
quad_R4_A2_6bit_round=150,150,150
quad_R4_nearest=A1
quad_R8_measured=165,166,165
quad_R8_A1_5bit_round=165,165,165
quad_R8_A2_5bit_round=165,165,165
quad_R8_A1_5bit_floor=156,156,156
quad_R8_A2_5bit_floor=165,165,165
quad_R8_A1_8bit_round=164,164,164
quad_R8_A2_8bit_round=167,167,167
quad_R8_A1_8bit_floor=163,163,163
quad_R8_A2_8bit_floor=166,166,166
quad_R8_A1_6bit_round=162,162,162
quad_R8_A2_6bit_round=166,166,166
quad_R8_nearest=A1
quad_R12_measured=181,182,181
quad_R12_A1_5bit_round=181,181,181
quad_R12_A2_5bit_round=181,181,181
quad_R12_A1_5bit_floor=173,173,173
quad_R12_A2_5bit_floor=181,181,181
quad_R12_A1_8bit_round=180,180,180
quad_R12_A2_8bit_round=182,182,182
quad_R12_A1_8bit_floor=179,179,179
quad_R12_A2_8bit_floor=181,181,181
quad_R12_A1_6bit_round=178,178,178
quad_R12_A2_6bit_round=182,182,182
quad_R12_nearest=ambiguous
quad_R16_measured=198,195,198
quad_R16_A1_5bit_round=198,198,198
quad_R16_A2_5bit_round=198,198,198
quad_R16_A1_5bit_floor=189,189,189
quad_R16_A2_5bit_floor=189,189,189
quad_R16_A1_8bit_round=195,195,195
quad_R16_A2_8bit_round=197,197,197
quad_R16_A1_8bit_floor=195,195,195
quad_R16_A2_8bit_floor=197,197,197
quad_R16_A1_6bit_round=194,194,194
quad_R16_A2_6bit_round=194,194,194
quad_R16_nearest=ambiguous
quad_R20_measured=214,211,214
quad_R20_A1_5bit_round=214,214,214
quad_R20_A2_5bit_round=214,214,214
quad_R20_A1_5bit_floor=206,206,206
quad_R20_A2_5bit_floor=206,206,206
quad_R20_A1_8bit_round=211,211,211
quad_R20_A2_8bit_round=213,213,213
quad_R20_A1_8bit_floor=211,211,211
quad_R20_A2_8bit_floor=212,212,212
quad_R20_A1_6bit_round=210,210,210
quad_R20_A2_6bit_round=210,210,210
quad_R20_nearest=ambiguous
quad_R24_measured=231,227,231
quad_R24_A1_5bit_round=231,231,231
quad_R24_A2_5bit_round=231,231,231
quad_R24_A1_5bit_floor=222,222,222
quad_R24_A2_5bit_floor=222,222,222
quad_R24_A1_8bit_round=227,227,227
quad_R24_A2_8bit_round=228,228,228
quad_R24_A1_8bit_floor=227,227,227
quad_R24_A2_8bit_floor=228,228,228
quad_R24_A1_6bit_round=227,227,227
quad_R24_A2_6bit_round=227,227,227
quad_R24_nearest=ambiguous
quad_R28_measured=247,243,247
quad_R28_A1_5bit_round=247,247,247
quad_R28_A2_5bit_round=247,247,247
quad_R28_A1_5bit_floor=239,239,239
quad_R28_A2_5bit_floor=239,239,239
quad_R28_A1_8bit_round=243,243,243
quad_R28_A2_8bit_round=243,243,243
quad_R28_A1_8bit_floor=243,243,243
quad_R28_A2_8bit_floor=243,243,243
quad_R28_A1_6bit_round=243,243,243
quad_R28_A2_6bit_round=243,243,243
quad_R28_nearest=ambiguous
variant_5bit_round_verdict=A1
variant_5bit_floor_verdict=UNDECIDED
variant_8bit_round_verdict=A1
variant_8bit_floor_verdict=UNDECIDED
variant_6bit_round_verdict=A2
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=5bit_round
best_convention=A1
best_total_dev=30
quad_L4_dev_best=3
quad_L8_dev_best=3
quad_L12_dev_best=2
quad_L16_dev_best=2
quad_L20_dev_best=1
quad_L24_dev_best=1
quad_L28_dev_best=0
quad_R4_dev_best=2
quad_R8_dev_best=1
quad_R12_dev_best=1
quad_R16_dev_best=3
quad_R20_dev_best=3
quad_R24_dev_best=4
quad_R28_dev_best=4
verdict=A1
```

The tool reports `verdict=A1` (`best_variant=5bit_round`, `best_convention=A1`,
`best_total_dev=30`), yet the measured values are visibly not equal across
channels: e.g. `quad_L4_measured=33,36,33`, `quad_L8_measured=66,69,66`. §4
extends the tool to look at each channel separately.

## 4. Extended tool — additive per-channel analysis (REQ 3)

`measure_alpha.py` and `expected.py` gained only new lines/helpers; every line
of the output in §3 is still emitted unchanged and in the same order (verified
by running the old tool and the new tool on the same fixture and checking the
old 196 lines are an in-order subsequence of the new output). Additions:

- **Per-channel rows** `quad_<name>_pc_{R,G,B}=measured=… A1=… A2=… near=…`.
  The expectations are per channel: **green** uses the `6bit_round` output
  `round(v6*255/63)`; **red and blue** use the `6bit_565` model
  `expand5(v6>>1)`. A channel votes A1/A2 only when its two distances differ by
  ≥ 2, else `ambiguous`.
- **`6bit_565` fit model** (`expected.expected6_565`): `R = B = expand5(v6>>1)`,
  `G = round_half_up(v6*255/63)`, with `v6` from `expected6`. This is a
  **MODEL** of a 565-quantised display path and a **HYPOTHESIS**; the origin of
  the red/blue 5-bit quantisation (the DS engine vs the 3DS display path) is
  **UNVERIFIED**. It is deliberately **not** in `expected.VARIANTS` /
  `measure_alpha.OVERALL_VARIANTS`, so the old scalar tables and the old
  `verdict=` are not perturbed.
- **Fit table** `fit_<variant>_<conv>_<channel>=<sum>`: summed
  |measured − expected| per channel, for every variant (including `6bit_565`)
  and both conventions.
- **`g_votes_A1`, `g_votes_A2`, `g_votes_ambiguous`, `g_channel_verdict`**
  (`A2` only when no quad votes A1 and ≥ 5 vote A2, symmetric for A1, else
  `UNDECIDED`; `NOT-COMPARABLE` when the run is not comparable).
- **`verdict_note`**: says the whole-RGB `verdict=` is not decisive when
  `g_channel_verdict` differs from it.

FACT — complete raw output of the extended tool on the same fixture:

```
$ PYTHONDONTWRITEBYTECODE=1 python3 -B measure_alpha.py fixtures/al1_3_top400x240.png
mode=capture
geometry_found=1
backdrop_black_measured=0,0,0
backdrop_grey_measured=132,134,132
quad_L4_measured=33,36,33
quad_L4_A1_5bit_round=33,33,33
quad_L4_A2_5bit_round=41,41,41
quad_L4_A1_5bit_floor=33,33,33
quad_L4_A2_5bit_floor=33,33,33
quad_L4_A1_8bit_round=33,33,33
quad_L4_A2_8bit_round=40,40,40
quad_L4_A1_8bit_floor=32,32,32
quad_L4_A2_8bit_floor=39,39,39
quad_L4_A1_6bit_round=32,32,32
quad_L4_A2_6bit_round=36,36,36
quad_L4_nearest=A1
quad_L4_pc_R=measured=33 A1=33 A2=33 near=ambiguous
quad_L4_pc_G=measured=36 A1=32 A2=36 near=A2
quad_L4_pc_B=measured=33 A1=33 A2=33 near=ambiguous
quad_L8_measured=66,69,66
quad_L8_A1_5bit_round=66,66,66
quad_L8_A2_5bit_round=74,74,74
quad_L8_A1_5bit_floor=66,66,66
quad_L8_A2_5bit_floor=66,66,66
quad_L8_A1_8bit_round=66,66,66
quad_L8_A2_8bit_round=72,72,72
quad_L8_A1_8bit_floor=65,65,65
quad_L8_A2_8bit_floor=71,71,71
quad_L8_A1_6bit_round=65,65,65
quad_L8_A2_6bit_round=69,69,69
quad_L8_nearest=A1
quad_L8_pc_R=measured=66 A1=66 A2=66 near=ambiguous
quad_L8_pc_G=measured=69 A1=65 A2=69 near=A2
quad_L8_pc_B=measured=66 A1=66 A2=66 near=ambiguous
quad_L12_measured=99,101,99
quad_L12_A1_5bit_round=99,99,99
quad_L12_A2_5bit_round=107,107,107
quad_L12_A1_5bit_floor=99,99,99
quad_L12_A2_5bit_floor=99,99,99
quad_L12_A1_8bit_round=99,99,99
quad_L12_A2_8bit_round=104,104,104
quad_L12_A1_8bit_floor=98,98,98
quad_L12_A2_8bit_floor=103,103,103
quad_L12_A1_6bit_round=97,97,97
quad_L12_A2_6bit_round=101,101,101
quad_L12_nearest=A1
quad_L12_pc_R=measured=99 A1=99 A2=99 near=ambiguous
quad_L12_pc_G=measured=101 A1=97 A2=101 near=A2
quad_L12_pc_B=measured=99 A1=99 A2=99 near=ambiguous
quad_L16_measured=132,134,132
quad_L16_A1_5bit_round=132,132,132
quad_L16_A2_5bit_round=132,132,132
quad_L16_A1_5bit_floor=132,132,132
quad_L16_A2_5bit_floor=132,132,132
quad_L16_A1_8bit_round=132,132,132
quad_L16_A2_8bit_round=135,135,135
quad_L16_A1_8bit_floor=131,131,131
quad_L16_A2_8bit_floor=135,135,135
quad_L16_A1_6bit_round=130,130,130
quad_L16_A2_6bit_round=134,134,134
quad_L16_nearest=A1
quad_L16_pc_R=measured=132 A1=132 A2=132 near=ambiguous
quad_L16_pc_G=measured=134 A1=130 A2=134 near=A2
quad_L16_pc_B=measured=132 A1=132 A2=132 near=ambiguous
quad_L20_measured=165,166,165
quad_L20_A1_5bit_round=165,165,165
quad_L20_A2_5bit_round=165,165,165
quad_L20_A1_5bit_floor=165,165,165
quad_L20_A2_5bit_floor=165,165,165
quad_L20_A1_8bit_round=165,165,165
quad_L20_A2_8bit_round=167,167,167
quad_L20_A1_8bit_floor=164,164,164
quad_L20_A2_8bit_floor=167,167,167
quad_L20_A1_6bit_round=162,162,162
quad_L20_A2_6bit_round=166,166,166
quad_L20_nearest=ambiguous
quad_L20_pc_R=measured=165 A1=165 A2=165 near=ambiguous
quad_L20_pc_G=measured=166 A1=162 A2=166 near=A2
quad_L20_pc_B=measured=165 A1=165 A2=165 near=ambiguous
quad_L24_measured=198,199,198
quad_L24_A1_5bit_round=198,198,198
quad_L24_A2_5bit_round=198,198,198
quad_L24_A1_5bit_floor=198,198,198
quad_L24_A2_5bit_floor=198,198,198
quad_L24_A1_8bit_round=197,197,197
quad_L24_A2_8bit_round=199,199,199
quad_L24_A1_8bit_floor=197,197,197
quad_L24_A2_8bit_floor=199,199,199
quad_L24_A1_6bit_round=194,194,194
quad_L24_A2_6bit_round=198,198,198
quad_L24_nearest=ambiguous
quad_L24_pc_R=measured=198 A1=198 A2=198 near=ambiguous
quad_L24_pc_G=measured=199 A1=194 A2=198 near=A2
quad_L24_pc_B=measured=198 A1=198 A2=198 near=ambiguous
quad_L28_measured=231,231,231
quad_L28_A1_5bit_round=231,231,231
quad_L28_A2_5bit_round=231,231,231
quad_L28_A1_5bit_floor=231,231,231
quad_L28_A2_5bit_floor=231,231,231
quad_L28_A1_8bit_round=230,230,230
quad_L28_A2_8bit_round=231,231,231
quad_L28_A1_8bit_floor=230,230,230
quad_L28_A2_8bit_floor=231,231,231
quad_L28_A1_6bit_round=227,227,227
quad_L28_A2_6bit_round=231,231,231
quad_L28_nearest=ambiguous
quad_L28_pc_R=measured=231 A1=231 A2=231 near=ambiguous
quad_L28_pc_G=measured=231 A1=227 A2=231 near=A2
quad_L28_pc_B=measured=231 A1=231 A2=231 near=ambiguous
quad_R4_measured=148,150,148
quad_R4_A1_5bit_round=148,148,148
quad_R4_A2_5bit_round=148,148,148
quad_R4_A1_5bit_floor=140,140,140
quad_R4_A2_5bit_floor=148,148,148
quad_R4_A1_8bit_round=148,148,148
quad_R4_A2_8bit_round=151,151,151
quad_R4_A1_8bit_floor=147,147,147
quad_R4_A2_8bit_floor=151,151,151
quad_R4_A1_6bit_round=146,146,146
quad_R4_A2_6bit_round=150,150,150
quad_R4_nearest=A1
quad_R4_pc_R=measured=148 A1=148 A2=148 near=ambiguous
quad_R4_pc_G=measured=150 A1=146 A2=150 near=A2
quad_R4_pc_B=measured=148 A1=148 A2=148 near=ambiguous
quad_R8_measured=165,166,165
quad_R8_A1_5bit_round=165,165,165
quad_R8_A2_5bit_round=165,165,165
quad_R8_A1_5bit_floor=156,156,156
quad_R8_A2_5bit_floor=165,165,165
quad_R8_A1_8bit_round=164,164,164
quad_R8_A2_8bit_round=167,167,167
quad_R8_A1_8bit_floor=163,163,163
quad_R8_A2_8bit_floor=166,166,166
quad_R8_A1_6bit_round=162,162,162
quad_R8_A2_6bit_round=166,166,166
quad_R8_nearest=A1
quad_R8_pc_R=measured=165 A1=165 A2=165 near=ambiguous
quad_R8_pc_G=measured=166 A1=162 A2=166 near=A2
quad_R8_pc_B=measured=165 A1=165 A2=165 near=ambiguous
quad_R12_measured=181,182,181
quad_R12_A1_5bit_round=181,181,181
quad_R12_A2_5bit_round=181,181,181
quad_R12_A1_5bit_floor=173,173,173
quad_R12_A2_5bit_floor=181,181,181
quad_R12_A1_8bit_round=180,180,180
quad_R12_A2_8bit_round=182,182,182
quad_R12_A1_8bit_floor=179,179,179
quad_R12_A2_8bit_floor=181,181,181
quad_R12_A1_6bit_round=178,178,178
quad_R12_A2_6bit_round=182,182,182
quad_R12_nearest=ambiguous
quad_R12_pc_R=measured=181 A1=181 A2=181 near=ambiguous
quad_R12_pc_G=measured=182 A1=178 A2=182 near=A2
quad_R12_pc_B=measured=181 A1=181 A2=181 near=ambiguous
quad_R16_measured=198,195,198
quad_R16_A1_5bit_round=198,198,198
quad_R16_A2_5bit_round=198,198,198
quad_R16_A1_5bit_floor=189,189,189
quad_R16_A2_5bit_floor=189,189,189
quad_R16_A1_8bit_round=195,195,195
quad_R16_A2_8bit_round=197,197,197
quad_R16_A1_8bit_floor=195,195,195
quad_R16_A2_8bit_floor=197,197,197
quad_R16_A1_6bit_round=194,194,194
quad_R16_A2_6bit_round=194,194,194
quad_R16_nearest=ambiguous
quad_R16_pc_R=measured=198 A1=198 A2=198 near=ambiguous
quad_R16_pc_G=measured=195 A1=194 A2=194 near=ambiguous
quad_R16_pc_B=measured=198 A1=198 A2=198 near=ambiguous
quad_R20_measured=214,211,214
quad_R20_A1_5bit_round=214,214,214
quad_R20_A2_5bit_round=214,214,214
quad_R20_A1_5bit_floor=206,206,206
quad_R20_A2_5bit_floor=206,206,206
quad_R20_A1_8bit_round=211,211,211
quad_R20_A2_8bit_round=213,213,213
quad_R20_A1_8bit_floor=211,211,211
quad_R20_A2_8bit_floor=212,212,212
quad_R20_A1_6bit_round=210,210,210
quad_R20_A2_6bit_round=210,210,210
quad_R20_nearest=ambiguous
quad_R20_pc_R=measured=214 A1=214 A2=214 near=ambiguous
quad_R20_pc_G=measured=211 A1=210 A2=210 near=ambiguous
quad_R20_pc_B=measured=214 A1=214 A2=214 near=ambiguous
quad_R24_measured=231,227,231
quad_R24_A1_5bit_round=231,231,231
quad_R24_A2_5bit_round=231,231,231
quad_R24_A1_5bit_floor=222,222,222
quad_R24_A2_5bit_floor=222,222,222
quad_R24_A1_8bit_round=227,227,227
quad_R24_A2_8bit_round=228,228,228
quad_R24_A1_8bit_floor=227,227,227
quad_R24_A2_8bit_floor=228,228,228
quad_R24_A1_6bit_round=227,227,227
quad_R24_A2_6bit_round=227,227,227
quad_R24_nearest=ambiguous
quad_R24_pc_R=measured=231 A1=231 A2=231 near=ambiguous
quad_R24_pc_G=measured=227 A1=227 A2=227 near=ambiguous
quad_R24_pc_B=measured=231 A1=231 A2=231 near=ambiguous
quad_R28_measured=247,243,247
quad_R28_A1_5bit_round=247,247,247
quad_R28_A2_5bit_round=247,247,247
quad_R28_A1_5bit_floor=239,239,239
quad_R28_A2_5bit_floor=239,239,239
quad_R28_A1_8bit_round=243,243,243
quad_R28_A2_8bit_round=243,243,243
quad_R28_A1_8bit_floor=243,243,243
quad_R28_A2_8bit_floor=243,243,243
quad_R28_A1_6bit_round=243,243,243
quad_R28_A2_6bit_round=243,243,243
quad_R28_nearest=ambiguous
quad_R28_pc_R=measured=247 A1=247 A2=247 near=ambiguous
quad_R28_pc_G=measured=243 A1=243 A2=243 near=ambiguous
quad_R28_pc_B=measured=247 A1=247 A2=247 near=ambiguous
variant_5bit_round_verdict=A1
variant_5bit_floor_verdict=UNDECIDED
variant_8bit_round_verdict=A1
variant_8bit_floor_verdict=UNDECIDED
variant_6bit_round_verdict=A2
model_6bit_rounding=blend floor; output round-half-up(v6*255/63)
best_variant=5bit_round
best_convention=A1
best_total_dev=30
quad_L4_dev_best=3
quad_L8_dev_best=3
quad_L12_dev_best=2
quad_L16_dev_best=2
quad_L20_dev_best=1
quad_L24_dev_best=1
quad_L28_dev_best=0
quad_R4_dev_best=2
quad_R8_dev_best=1
quad_R12_dev_best=1
quad_R16_dev_best=3
quad_R20_dev_best=3
quad_R24_dev_best=4
quad_R28_dev_best=4
verdict=A1
fit_5bit_round_A1_R=0
fit_5bit_round_A1_G=30
fit_5bit_round_A1_B=0
fit_5bit_round_A2_R=24
fit_5bit_round_A2_G=38
fit_5bit_round_A2_B=24
fit_5bit_floor_A1_R=59
fit_5bit_floor_A1_G=61
fit_5bit_floor_A1_B=59
fit_5bit_floor_A2_R=34
fit_5bit_floor_A2_G=36
fit_5bit_floor_A2_B=34
fit_8bit_round_A1_R=18
fit_8bit_round_A1_G=20
fit_8bit_round_A1_B=18
fit_8bit_round_A2_R=39
fit_8bit_round_A2_G=19
fit_8bit_round_A2_B=39
fit_8bit_floor_A1_R=26
fit_8bit_floor_A1_G=28
fit_8bit_floor_A1_B=26
fit_8bit_floor_A2_R=35
fit_8bit_floor_A2_G=15
fit_8bit_floor_A2_B=35
fit_6bit_round_A1_R=41
fit_6bit_round_A1_G=43
fit_6bit_round_A1_B=41
fit_6bit_round_A2_R=31
fit_6bit_round_A2_G=3
fit_6bit_round_A2_B=31
fit_6bit_565_A1_R=0
fit_6bit_565_A1_G=43
fit_6bit_565_A1_B=0
fit_6bit_565_A2_R=0
fit_6bit_565_A2_G=3
fit_6bit_565_A2_B=0
g_votes_A1=0
g_votes_A2=10
g_votes_ambiguous=4
g_channel_verdict=A2
verdict_note=channels disagree; G channel=A2 but verdict=A1; verdict= is not decisive when channels disagree
```

### 4.1 Per-quad table (measured RGB, green 6-bit A1/A2, ruling)

FACT — extracted from the output above (green channel only; `(tie)` marks a
quad where the 6-bit A1 and A2 expectations are identical, so it carries no
information; R and B are covered by the fit table):

| Quad | measured R,G,B | G A1 | G A2 | G ruling |
|---|---|---|---|---|
| L4 | 33,36,33 | 32 | 36 | A2 |
| L8 | 66,69,66 | 65 | 69 | A2 |
| L12 | 99,101,99 | 97 | 101 | A2 |
| L16 | 132,134,132 | 130 | 134 | A2 |
| L20 | 165,166,165 | 162 | 166 | A2 |
| L24 | 198,199,198 | 194 | 198 | A2 |
| L28 | 231,231,231 | 227 | 231 | A2 |
| R4 | 148,150,148 | 146 | 150 | A2 |
| R8 | 165,166,165 | 162 | 166 | A2 |
| R12 | 181,182,181 | 178 | 182 | A2 |
| R16 | 198,195,198 | 194 (tie) | 194 (tie) | ambiguous |
| R20 | 214,211,214 | 210 (tie) | 210 (tie) | ambiguous |
| R24 | 231,227,231 | 227 (tie) | 227 (tie) | ambiguous |
| R28 | 247,243,247 | 243 (tie) | 243 (tie) | ambiguous |

### 4.2 Fit per variant and channel

FACT — the `fit_*` lines above, rearranged (summed |measured − expected| over
all 14 quads, per channel):

| variant | conv | R | G | B |
|---|---|---|---|---|
| 5bit_round | A1 | 0 | 30 | 0 |
| 5bit_round | A2 | 24 | 38 | 24 |
| 6bit_round | A1 | 41 | 43 | 41 |
| 6bit_round | A2 | 31 | 3 | 31 |
| 6bit_565 | A1 | 0 | 43 | 0 |
| 6bit_565 | A2 | 0 | 3 | 0 |

`6bit_565` fits **R and B with deviation 0** (all 14 quads) and G with
deviation 3 (L24, R16, R20 are +1); `6bit_round` fits G with the same 3 but its
scalar R/B deviations are 31. No scalar family can fit "R/B 5-bit, G 6-bit",
which is exactly why the whole-RGB `verdict=` picks `5bit_round/A1` (R and B
deviation 0, G deviation 30).

## 5. Conclusions

### 5.1 Green channel (FACT, shown by the quoted output)

- `g_votes_A1=0`, `g_votes_A2=10`, `g_votes_ambiguous=4`,
  `g_channel_verdict=A2`.
- On the **10 quads where the 6-bit A1 and A2 expectations differ**, the
  measured green is nearer A2 on all 10 and nearer A1 on none; it **equals the
  A2 expectation exactly on 9** (L4, L8, L12, L16, L20, L28, R4, R8, R12) and
  is **1 level above** it on L24 (199 vs 198). Green equals the A1 expectation
  on none of these 10.
- The other **4 quads are ties** (R16, R20, R24, R28): the 6-bit A1 and A2
  expectations for green are identical there (194, 210, 227, 243), so they
  vote `ambiguous` and carry no information.
- Therefore: **the green channel of the hardware capture matches the DeSmuME
  6-bit A2 model on the quads that can decide, and never A1.**

### 5.2 Red and blue channels (FACT, shown by the quoted output)

- `fit_6bit_565_A2_R=0` and `fit_6bit_565_A2_B=0`: on **all 14 quads** the
  measured red and blue equal `expand5(v6>>1)` — the **5-bit quantisation of
  the same 6-bit A2 values** — exactly.
- The black-backdrop R/B values are `33,66,99,132,165,198,231` (= `expand5`
  of 4,8,12,16,20,24,28) and the grey-backdrop values `148,165,181,198,214,231,247`;
  both are 5-bit-quantised, not the raw 6-bit A2 values.

### 5.3 The artefact (FACT)

The whole-RGB classifier sums |measured − expected| over R, G and B together
and over a single scalar family. `5bit_round/A1` fits R and B exactly (both
deviation 0) but is 30 away on G; `6bit_round/A2` fits G (deviation 3) but is
31 away on R/B. The scalar family that minimises the total is therefore
`5bit_round/A1` (`best_total_dev=30`), giving `verdict=A1` — even though the G
channel on its own is decisively A2. This is a **classifier-model artefact**
(the families do not model a 5-bit R/B quantisation with a 6-bit G), not a
hardware A1 result. The extended output says so directly:

```
verdict_note=channels disagree; G channel=A2 but verdict=A1; verdict= is not decisive when channels disagree
```

### 5.4 Hypothesis

**HYPOTHESIS:** the DeSmuME-style 6-bit A2 blend describes the hardware green
channel because the underlying rasterisation is A2; the R/B 5-bit quantisation
is a separate display-path (or engine) quantisation. The **cause** of the 6-bit
G / 5-bit R/B split is **not** established here.

### 5.5 What is NOT shown

- Other consoles (one New 3DS only).
- Other alpha values (only a = 4..28 step 4; a = 1..3 and 29..30 untested).
- Textures, other source colours, other backdrop colours.
- Polygon-ID interactions, ordering, edge/AA effects.
- **Where the R/B 5-bit quantisation originates** (DS engine vs 3DS display
  path): UNVERIFIED.
- Whether the 3DS DS-mode display path is bit-identical to a DS/DSi; only this
  capture board path is measured.

## 6. Our own PICA path (SOURCE-READ, quoted from eq4h §6, not re-derived)

`docs/reports/eq4h-alpha-convention-test.md` §6(b) already established what our
conversion path does; quoted unchanged:

**(b) Our PICA conversion path: A2 = `(a+1)/32`.** SOURCE-READ —
`common/twlhd_gx_interp.c:395-397` computes the float alpha, applied per BEGIN
group at `:1074`, carried as `rgba[3]` (`common/twlhd_gx_interp.h:25`) and
blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA (`arm11/include/twlhd_blend_config.h:30-31`):

```
$ sed -n '395,397p' common/twlhd_gx_interp.c
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}
$ grep -n "g->rgba\[3\] = gx_alpha_float" common/twlhd_gx_interp.c
1074:        g->rgba[3] = gx_alpha_float(g->applied_alpha);
$ grep -n "rgba\[4\]" common/twlhd_gx_interp.h
25:    float rgba[4]; /* r, g, b, a in 0..1 (alpha from POLYGON_ATTR, see stats). */
$ grep -n "GPU_SRC_ALPHA\|GPU_ONE_MINUS_SRC_ALPHA" arm11/include/twlhd_blend_config.h
30:        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
31:        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
```

Alpha 0 is the DS wireframe case and is emitted as 0.0 (not a blend weight).
Rendered through PICA by `tools/azahar_host`. **Our path uses A2.**

## 7. Requirement traceability

- **REQ 1 fixture, sha256, four-crop check, only al1_3 kept, no xwd/1280×720** —
  §2; `sha256sum`/`cmp` quoted, PIL pair diff quoted, fixture is 4744 bytes.
- **REQ 2 current tool run quoted complete once** — §3.
- **REQ 3 additive per-channel analysis, `6bit_565`, fit table, `g_*`,
  `verdict_note`, old verdict kept + note** — §4; old 196 lines an in-order
  subsequence of the new output; `6bit_565` labelled MODEL/HYPOTHESIS with
  UNVERIFIED R/B origin in the tool docstring and output.
- **REQ 4 tests, never weakened** — `tools/ds_alpha_test/test_measure_alpha.py`
  gained four tests (565 A2→A2, 565 A1→A1, flat grey→NOT-COMPARABLE, real
  fixture→A2 with pinned `0/10/4`) plus a `_pc_vote` logic test;
  `make test-ds-alpha-test` = 48 tests OK (was 43); root `make test` = 394 tests
  OK, 69 skipped; no existing test modified or skipped.
- **REQ 5 report + append-only pointers + eq4h note** — this report; pointers in
  `docs/EQUALITY_CHECK_DESIGN.md` and `docs/EQ4_HW_COMPARE.md`; note appended to
  the eq4h report.
- **Rules: no literal home paths / user names, diff < 90 KB, every REQ with
  command evidence** — §9, §10.

## 8. Relationship to EQ-4h

EQ-4h measured the same convention in DeSmuME and, after the 6-bit variant was
added, got `best_variant=6bit_round`, `best_convention=A2`. This hardware run
reaches the same per-channel conclusion on real pixels. The eq4h report now
carries a note that its earlier "overall verdict A1" wording is the model
artefact explained here.

## 9. Privacy

Every command uses `$HOME`; the expanded home path and the owner/group in
`ls -l` are not reproduced (shown as `$HOME` and `<user> <group>`). The
`/(home|Users)/` grep over the touched paths is clean (§10).

## 10. Size audit

FACT — the total diff against `main` stays below the 90 KB (92,160-byte) limit:

```
$ git diff main | wc -c
54339
```

FACT — no literal home/user path in the touched paths:

```
$ grep -rn -E '/(home|Users)/' tools/ds_alpha_test docs/EQUALITY_CHECK_DESIGN.md docs/EQ4_HW_COMPARE.md docs/reports/eq4i-alpha-hw-result.md docs/reports/eq4h-alpha-convention-test.md
(no output; grep exit status 1 = no match)
```

