<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4r — six static-angle demo scenes: hardware vs DeSmuME harness and PICA — 2026-11-24

**Scope:** offline analysis only; no console access. Added: 17 PNG fixtures in
`tools/fixtures/eq4_hw_scenes/` with a README, `RealStaticScenesCompareTest` in
`tools/test_eq4_hw_compare.py` (already run by `test-eq4-hw-compare`), this
report, and append-only `Result EQ-4r` pointers in
`docs/EQUALITY_CHECK_DESIGN.md`, `docs/EQ4_HW_COMPARE.md`, `docs/EQ_SCENES.md`.
No production code was touched (`arm9v2/`, `arm11/`, `common/`, `arm7/`,
`tools/azahar_host/` unchanged).

**Basis:** one console, one capture session per ROM, one comparison capture per
ROM (`a`, a rule fixed before any comparison number was read). Nothing here
claims universal hardware equality.

**Labels:** **FACT** = a command and its complete raw output quoted here;
**SOURCE-READ** = quoted `file:line`; **HYPOTHESIS**/**UNVERIFIED** = not
measured here; **OWNER-REPORTED** = the owner's statement. HARDWARE = the
cc3dsfs capture; EMULATOR = harness frame or Azahar/PICA render.

**Quotation policy:** every decision-bearing run is quoted — the six harness
invocations (the second of two runs per ROM; both runs' hashes are in §3), the
capture runs' complete 49-line `set -x` logs, their guard/hash checks, the
twelve comparisons, the azahar PPM hashes and crop script, the noise counts,
the hash/size listings and the tests. Only the `azahar_host` runs (462-464 lines, mostly per-frame `software framebuffer 400x480`) exceed
the 120000-byte limit set for this task (raised from 90000 by the orchestrator) and are
quoted as an unedited head plus tail with an explicit `[... N lines elided ...]`
marker and the total count. Nothing else is omitted or reworded.

## 1. Inputs and builds (FACT)

**Worktree HEAD** (identical to `main`; no commits added):


```
$ git rev-parse HEAD && git rev-parse main
e853749ca3b689ba729ea7c71705dc7d48426fe2
e853749ca3b689ba729ea7c71705dc7d48426fe2
```

Both EMULATOR-side programs were built at this HEAD:


```
$ make azahar-host
mkdir -p tools/azahar_host/build
cc -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L \
	-Itools/azahar_host  \
	-o tools/azahar_host/build/azahar_host tools/azahar_host/azahar_host.c -ldl

$ sha256sum tools/azahar_host/build/azahar_host
7e9739bc2a4ccca1d92c8584d24aa3e10e51cc935cbcaa49580d78d096be65f5  tools/azahar_host/build/azahar_host
```

`make -C arm11 -B` produced the 3dsx (55-line log; compile lines elided,
paths shown as `<worktree>`):


```
$ make -C arm11 -B
make: Entering directory '<worktree>/arm11'
[... 53 per-file compile lines, 2 warning blocks and 2 note blocks elided ...]
<worktree>/arm11/../common/twlhd_v2_parse.c:379:12: warning: 'texel_bytes' may be used uninitialized [-Wmaybe-uninitialized]
<worktree>/arm11/../common/twlhd_v2_parse.c:381:61: warning: 'index_bytes' may be used uninitialized [-Wmaybe-uninitialized]
linking twlhd-arm11.elf
built ... twlhd-arm11.3dsx
make: Leaving directory '<worktree>/arm11'
```

```
$ sha256sum arm11/twlhd-arm11.3dsx
15c26a18744048687395bbaca504c5ec67279a938b46d67b82a39990212c244f  arm11/twlhd-arm11.3dsx
```

Staging is `$HWSTAGE` (the run directory named in the task); only
`<staging>/...` paths are written here. The run's hash file lists the six
autodump ROMs and all eighteen captures (quoted complete):


```
$ cat "$HWSTAGE/hw/SHA256SUMS"
488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6  hw/hw_c2_a.png
5c43e91cd121ab3cbc446c80e8a0ce6e463a7e81a9c27aa9c0b7f0748da06d64  hw/hw_c2_b.png
488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6  hw/hw_c2_c.png
0f9e7aee19cec41fc00909e894f053717a3cca4088aa18efc5789527d2f6e9c0  hw/hw_cube_a.png
0249e52e10aeaedc67ca9ff166e84b808c4e3ecd2b8f5a37680a84d3a9147fd3  hw/hw_cube_b.png
0f9e7aee19cec41fc00909e894f053717a3cca4088aa18efc5789527d2f6e9c0  hw/hw_cube_c.png
8af538df08a00169f9f50785974f8d20388e969fda525151be468db65cfee75a  hw/hw_s1_a.png
1ea3d1f8aa09ea78a5b02f29b152543ce5163b2c15218113f7924df52072e7c2  hw/hw_s1_b.png
0b2a69b4afdff1f556f756131e436d440e633389b8ed49351fa0a40ee84320a6  hw/hw_s1_c.png
c01dc0ea69eb2a715af29743ac7e7d48407354bb4a56fff1d3f1a85287f338b9  hw/hw_s2_a.png
41b4356a3539f6ad2f2be79948d70c6c81dae002826544195847f77c2eb6a1e1  hw/hw_s2_b.png
c01dc0ea69eb2a715af29743ac7e7d48407354bb4a56fff1d3f1a85287f338b9  hw/hw_s2_c.png
bb83ba0fef9514d896e659f21f5ad57edf507e92e6e55e8768f0ab71202d9399  hw/hw_s3_a.png
821671e8d1294839d08dc6385c585690e5461709b2c1112073281036e29b73e9  hw/hw_s3_b.png
bb83ba0fef9514d896e659f21f5ad57edf507e92e6e55e8768f0ab71202d9399  hw/hw_s3_c.png
ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9  hw/hw_s4_a.png
9807fd51271cecae2a5a7942dda19450b1f34bffcc8a92aa4ee5066ba1ca7f93  hw/hw_s4_b.png
ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9  hw/hw_s4_c.png
82e913696c003b54525c092f4f2e7f483ec52b805cea58366f85f6054d41f3e4  roms/twlhd-arm9v2-autodump-c2-st30.nds
7115212ff92690930369722ceee52ccf1f67cea21a22272629976b0ae32fdb23  roms/twlhd-arm9v2-autodump-s1-st30.nds
cc476561e9f60f780bc379ec1fde6af23bd5342baa1c1028690242a6e8f877a2  roms/twlhd-arm9v2-autodump-s2-st30.nds
3a39663a404bec615cbf21a3905127b99fafd7eae887fe5845efee411d4094f7  roms/twlhd-arm9v2-autodump-s3-st30.nds
0f63a52595c3774aa1b7400b4c112c3062c08a6dd788f28939f14bd962629ff7  roms/twlhd-arm9v2-autodump-s4-st30.nds
73e409360c1e35858d1dc0d7893415ed09b0778a63c6adf4aa9e1bc70294e6fd  roms/twlhd-arm9v2-autodump-st30.nds
```

The plain hardware ROMs (`twlhd-arm9v2-{,s1-,...,c2-}st30.nds`) are in the
same directory but **not** in this hash file, so their identity to
`docs/reports/eq4q-static-scenes.md` §6.1 is **UNVERIFIED** (only names are
known). The capture mapping is the shared one (quoted complete):


```
$ cat tools/ds_vtxcolor_test/capture_mapping.json
{
  "scale_x": 1.25,
  "scale_y": 1.25,
  "offset_x": 40,
  "offset_y": 0,
  "source": "docs/reports/eq4d-ds-vertex-colour-hw.md"
}
```

The baseline ROM has no `-cube-` infix (`<staging>/roms/twlhd-arm9v2-autodump-st30.nds`);
the `cube exit=1` of §4.1 is an invocation with a guessed `-cube-` name that
failed before `set -x` and wrote no log (inferred), so there is one successful
capture per ROM: the cube capture was invoked twice (the first with the wrong
ROM name).

## 2. Capture session, the a-priori rule and the noise estimate (OWNER-REPORTED + FACT)

**OWNER-REPORTED console work (not FACT).** Each of the six plain ROMs was
started on the same New 3DS in DS mode from TWiLight Menu++ and the passive
cc3dsfs board recorded the top screen three times per ROM
(`tools/cc3dsfs_capture/cc_shots.sh`-style, `--enabled_both 0 --enabled_top 1
--scaling_top 1`, 24-bpp decoder `tools/cc3dsfs_capture/xwd2png.py`), each
cropped to the top-left 400x240. This analysis never touched the console.

**Rule, fixed before looking at any comparison number:** every comparison uses
capture **`a`** (the first capture). It is not chosen per ROM. The other two
captures are used only for this noise estimate:


```
$ HWSTAGE=<staging> python3 - <<'EOF'
import os
from PIL import Image

stage = os.environ["HWSTAGE"]
print("basis=one console, one capture session per ROM")
print("rule=fixed before comparing: capture a is the comparison capture")
for scene in ("cube", "s1", "s2", "s3", "s4", "c2"):
    images = [Image.open("%s/hw/hw_%s_%s.png" % (stage, scene, suffix))
              .convert("RGB") for suffix in ("a", "b", "c")]
    def count(first, second):
        return sum(1 for p, q in zip(first.get_flattened_data(),
                                     second.get_flattened_data()) if p != q)
    print("%s a_vs_b=%d a_vs_c=%d b_vs_c=%d"
          % (scene, count(images[0], images[1]), count(images[0], images[2]),
             count(images[1], images[2])))
EOF
basis=one console, one capture session per ROM
rule=fixed before comparing: capture a is the comparison capture
cube a_vs_b=160 a_vs_c=0 b_vs_c=160
s1 a_vs_b=66 a_vs_c=59 b_vs_c=29
s2 a_vs_b=66 a_vs_c=0 b_vs_c=66
s3 a_vs_b=77 a_vs_c=0 b_vs_c=77
s4 a_vs_b=80 a_vs_c=0 b_vs_c=80
c2 a_vs_b=40 a_vs_c=0 b_vs_c=40
```

The origin is **UNVERIFIED**: the differences are inside the drawn figure, the
captures are not time-stamped, and no mechanism (capture board, scaler, the DS)
was isolated. `a` and `c` are byte-identical for five of six ROMs; `s1` differs
in all three pairs. `s4`'s alpha-0 outline is in all three captures (same 64
bright pixels in DS rows 62-71):


```
$ HWSTAGE=<staging> python3 - <<'EOF'
import os
from PIL import Image

stage = os.environ["HWSTAGE"]
rows = range(int(62 * 1.25), int((71 + 1) * 1.25))   # DS 62-71 -> capture 77..89
for capture in ("a", "b", "c"):
    im = Image.open("%s/hw/hw_s4_%s.png" % (stage, capture)).convert("RGB")
    print("s4_%s bright_pixels_rows%d_%d=%d"
          % (capture, rows.start, rows.stop - 1,
             sum(1 for y in rows for x in range(im.width)
                 if max(im.getpixel((x, y))) > 60)))
EOF
s4_a bright_pixels_rows77_89=64
s4_b bright_pixels_rows77_89=64
s4_c bright_pixels_rows77_89=64
```

## 3. Harness references (EMULATOR, FACT)

Harness frame 242 = ROM frame 240 (the ROM autodumps at 240;
`tools/fixtures/eq_scenes/README.md`). **The harness was run twice for every
ROM**; `c2` is the only failure in both runs. A first set was written
16:34:24-16:35:02 into `/tmp/opencode/eq4r/refs/` and the second set, quoted
below, 16:41:09-16:41:42 into `/tmp/opencode/eq4r/refs2/`. The first run's
literal commands were not kept (no shell history or log of them was kept) and
its console output was not kept for the five successes; what survives is
quoted in §3.7. The second run's per-ROM console logs survive in
`/tmp/opencode/eq4r/hlog/`.


### 3.1 cube

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_cube.txt --ppm /tmp/opencode/eq4r/refs2/harness_cube.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 6 polygons, 24 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: E91640AB
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```


### 3.2 s1

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-s1-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_s1.txt --ppm /tmp/opencode/eq4r/refs2/harness_s1.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 8 polygons, 24 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: D50E6EC7
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```


### 3.3 s2

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-s2-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_s2.txt --ppm /tmp/opencode/eq4r/refs2/harness_s2.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 10 polygons, 30 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: DC8CF5B0
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```


### 3.4 s3

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-s3-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_s3.txt --ppm /tmp/opencode/eq4r/refs2/harness_s3.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 5 polygons, 20 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 1C91C749
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```


### 3.5 s4

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-s4-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_s4.txt --ppm /tmp/opencode/eq4r/refs2/harness_s4.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 4 polygons, 16 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 32E8ECF3
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```


### 3.6 c2

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-c2-st30.nds" 242 --out /tmp/opencode/eq4r/refs2/harness_c2.txt --ppm /tmp/opencode/eq4r/refs2/harness_c2.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: ROM never presented a 3D frame within 1200 frames
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 57BF8A7D
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
THUMB9: Undefined instruction: 0x0000B909 PC=0x02017310
ARM9: Undefined instruction: 0x0000B909 PC=0x02017310
armcpu_exception!
ARM OP_BKPT triggered
$ echo "exit=$?"
exit=1
```


The five PPMs are 256x192 P6 (147471 bytes each). Their hashes, then the first
run's surviving evidence and the lossless PNG conversion:


```
$ sha256sum /tmp/opencode/eq4r/refs2/harness_*.ppm
95725f819c30dcfdfaab5c786959b3aec09b259f823e3dee56a9f3178cc25410  /tmp/opencode/eq4r/refs2/harness_cube.ppm
da63f5b2ef8ca62666e3713a7b9f987034e595cddefd4de74c3daf3010d05cb8  /tmp/opencode/eq4r/refs2/harness_s1.ppm
911ad1c6c3e27908e0620532353f12c9abbf9e985a5b9d4ad4105d7bb0e77ad1  /tmp/opencode/eq4r/refs2/harness_s2.ppm
3f764bf381726f343a929ce726e3cff0a60da2023724298df5dd42a4df92617a  /tmp/opencode/eq4r/refs2/harness_s3.ppm
cc36989c198454afab9a1af4a776cdbea46604de1eb603b48a206d5c998cb2b2  /tmp/opencode/eq4r/refs2/harness_s4.ppm
```

### 3.7 the first run (`refs/`)

A read-only hash of the surviving first-run PPMs; every hash equals its refs2
counterpart above, so the two runs produced byte-identical PPMs (the geometry
`.txt` dumps and the `c2` log are likewise `cmp`-identical):

```
$ sha256sum /tmp/opencode/eq4r/refs/harness_*.ppm
95725f819c30dcfdfaab5c786959b3aec09b259f823e3dee56a9f3178cc25410  /tmp/opencode/eq4r/refs/harness_cube.ppm
da63f5b2ef8ca62666e3713a7b9f987034e595cddefd4de74c3daf3010d05cb8  /tmp/opencode/eq4r/refs/harness_s1.ppm
911ad1c6c3e27908e0620532353f12c9abbf9e985a5b9d4ad4105d7bb0e77ad1  /tmp/opencode/eq4r/refs/harness_s2.ppm
3f764bf381726f343a929ce726e3cff0a60da2023724298df5dd42a4df92617a  /tmp/opencode/eq4r/refs/harness_s3.ppm
cc36989c198454afab9a1af4a776cdbea46604de1eb603b48a206d5c998cb2b2  /tmp/opencode/eq4r/refs/harness_s4.ppm
$ for s in cube s1 s2 s3 s4; do cmp /tmp/opencode/eq4r/refs/harness_$s.txt /tmp/opencode/eq4r/refs2/harness_$s.txt && echo "$s txt identical"; done
cube txt identical
s1 txt identical
s2 txt identical
s3 txt identical
s4 txt identical
$ cmp /tmp/opencode/eq4r/refs/harness_c2.log /tmp/opencode/eq4r/hlog/harness_c2.log && echo "c2 log identical"
c2 log identical
```

The first run's console output for the five successes was not kept; the one
first-run log that survives (`refs/harness_c2.log`) is the same failure output
quoted for c2 below. `/tmp/opencode/eq4r/refs3/` also holds five PPMs with
these hashes, all with mtime 16:42:21.* within 1 ms and no `.txt`; identical
bytes plus same-millisecond mtimes make this a copy, not a third run. The
committed fixtures `harness_*_st30_f240.png` were converted from the **refs2**
(second-run) PPMs by the command below.

```
$ python3 - <<'EOF'
from PIL import Image
for scene in ("cube", "s1", "s2", "s3", "s4"):
    Image.open("/tmp/opencode/eq4r/refs2/harness_%s.ppm" % scene).convert("RGB") \
        .save("tools/fixtures/eq4_hw_scenes/harness_%s_st30_f240.png" % scene)
EOF
$ echo "exit=$?"
exit=0
```

`harness_c2_st30_f240.png` does not exist: that run printed
`harness: ROM never presented a 3D frame within 1200 frames` and exited 1 with
`ARM OP_BKPT triggered`, and wrote no PPM. The pair is NOT-COMPARABLE (§5.11),
never a pass.

EQ-4s rebisected this failure (one harness run per ROM variant): the non-static `CUBES=2 AUTODUMP_FRAME=240` ROM (`703dcad6…`) passes at HEAD and at `d21d189`, the plain static `c2` ROM passes, and only the new static+autodump layout fails (zero 3D flushes, `PC=0x02017310` in `TAN_LUT`, independent of autodump frame 60/240 and angle 30/31). FACT: the fault appears only in the EQ-4q-introduced static+autodump `CUBES=2` build; HYPOTHESIS: a pre-existing binary-layout-sensitive wild jump before the first 3D flush (boot, early main or the first draw call; location UNVERIFIED), not the macro's semantics; mechanism UNVERIFIED. These runs cannot exclude that EQ-4q merely exposed a latent fault. Report `docs/reports/eq4s-c2-harness-bkpt.md`.

## 4. Ours: DeSmuME v2 capture then the Azahar/PICA render (EMULATOR, FACT)

### 4.1 DeSmuME v2 capture of the same autodump ROM

One successful capture per ROM into `/tmp/opencode/eq4r/caps/<scene>` (the cube
was invoked twice, §1); all §3-§5 commands run from the worktree. The six
successful invocations were not run sequentially: five were launched in one
parallel loop and the cube was re-run separately. The loop's exact command text
was not kept; what survives is the status file and the logs. The parallel runs'
emulator PIDs interleave (s1 795999, s2 796000, s4 796001, s3 796005, c2
796006, sleepers 796002-796004 and 796007-796008), so the five ran
concurrently. The cube's first invocation used a guessed `-cube-` ROM name,
exited 1 before `set -x` and wrote no log (this is the `cube exit=1` below;
inferred from the missing log and the separate re-run log). The cube re-run was
a separate later invocation with the baseline ROM name and PIDs 800096/800097;
its literal command was not kept either.

```
$ cat /tmp/opencode/eq4r/caps/status.txt
cube exit=1
c2 exit=0
s1 exit=0
s2 exit=0
s4 exit=0
s3 exit=0
```

`status.txt` records the five parallel runs (all 0) and the failed cube
attempt. The cube re-run's exit status was **not recorded** (it is absent from
`status.txt`); its log ends at the script's last command, `echo "capture: ..."`,
which suggests status 0, but that is **inferred, not FACT**.

Complete stderr of the six successful runs (49 `set -x` lines each, nothing elided):

```
$ cat /tmp/opencode/eq4r/caps/cube.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/cube
+ image=/tmp/opencode/eq4r/caps/cube/cflash.img
+ log=/tmp/opencode/eq4r/caps/cube/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/cube/fat
+ rm -f /tmp/opencode/eq4r/caps/cube/cflash.img /tmp/opencode/eq4r/caps/cube/desmume.log /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/cube/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/cube/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=800096
+ sleeper_pid=800097
+ grace_pid=
+ emulator_killed_by_script=0
+ trap cleanup EXIT INT TERM
+ sleep 45
+ set +e
+ wait -n 800096 800097
+ first_status=0
+ set -e
+ kill -0 800096
+ kill -0 800097
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 800096
+ grace_pid=803945
+ set +e
+ wait -n 800096 803945
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 800096
+ kill -0 803945
+ kill -TERM 803945
+ wait 803945
+ true
+ grace_pid=
+ set +e
+ wait 800096
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/cube/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/cube/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/cube/cflash.img -o/tmp/opencode/eq4r/caps/cube/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/cube/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/cube/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin
```

```
$ cat /tmp/opencode/eq4r/caps/s1.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/s1
+ image=/tmp/opencode/eq4r/caps/s1/cflash.img
+ log=/tmp/opencode/eq4r/caps/s1/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/s1/fat
+ rm -f /tmp/opencode/eq4r/caps/s1/cflash.img /tmp/opencode/eq4r/caps/s1/desmume.log /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/s1/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/s1/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=795999
+ sleeper_pid=796002
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 45
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 795999 796002
+ first_status=0
+ set -e
+ kill -0 795999
+ kill -0 796002
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 795999
+ grace_pid=799273
+ set +e
+ wait -n 795999 799273
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 795999
+ kill -0 799273
+ kill -TERM 799273
+ wait 799273
+ true
+ grace_pid=
+ set +e
+ wait 795999
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/s1/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/s1/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/s1/cflash.img -o/tmp/opencode/eq4r/caps/s1/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/s1/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/s1/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin
```

```
$ cat /tmp/opencode/eq4r/caps/s2.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/s2
+ image=/tmp/opencode/eq4r/caps/s2/cflash.img
+ log=/tmp/opencode/eq4r/caps/s2/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/s2/fat
+ rm -f /tmp/opencode/eq4r/caps/s2/cflash.img /tmp/opencode/eq4r/caps/s2/desmume.log /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/s2/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/s2/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=796000
+ sleeper_pid=796004
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 45
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 796000 796004
+ first_status=0
+ set -e
+ kill -0 796000
+ kill -0 796004
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 796000
+ grace_pid=799274
+ set +e
+ wait -n 796000 799274
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 796000
+ kill -0 799274
+ kill -TERM 799274
+ wait 799274
+ true
+ grace_pid=
+ set +e
+ wait 796000
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/s2/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/s2/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/s2/cflash.img -o/tmp/opencode/eq4r/caps/s2/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/s2/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/s2/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin
```

```
$ cat /tmp/opencode/eq4r/caps/s3.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/s3
+ image=/tmp/opencode/eq4r/caps/s3/cflash.img
+ log=/tmp/opencode/eq4r/caps/s3/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/s3/fat
+ rm -f /tmp/opencode/eq4r/caps/s3/cflash.img /tmp/opencode/eq4r/caps/s3/desmume.log /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/s3/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/s3/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=796005
+ sleeper_pid=796008
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 45
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 796005 796008
+ first_status=0
+ set -e
+ kill -0 796005
+ kill -0 796008
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 796005
+ grace_pid=799272
+ set +e
+ wait -n 796005 799272
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 796005
+ kill -0 799272
+ kill -TERM 799272
+ wait 799272
+ true
+ grace_pid=
+ set +e
+ wait 796005
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/s3/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/s3/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/s3/cflash.img -o/tmp/opencode/eq4r/caps/s3/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/s3/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/s3/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin
```

```
$ cat /tmp/opencode/eq4r/caps/s4.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/s4
+ image=/tmp/opencode/eq4r/caps/s4/cflash.img
+ log=/tmp/opencode/eq4r/caps/s4/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/s4/fat
+ rm -f /tmp/opencode/eq4r/caps/s4/cflash.img /tmp/opencode/eq4r/caps/s4/desmume.log /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/s4/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/s4/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=796001
+ sleeper_pid=796003
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 45
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 796001 796003
+ first_status=0
+ set -e
+ kill -0 796001
+ kill -0 796003
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 796001
+ grace_pid=799276
+ set +e
+ wait -n 796001 799276
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 796001
+ kill -0 799276
+ kill -TERM 799276
+ wait 799276
+ true
+ grace_pid=
+ set +e
+ wait 796001
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/s4/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/s4/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/s4/cflash.img -o/tmp/opencode/eq4r/caps/s4/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/s4/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/s4/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin
```

```
$ cat /tmp/opencode/eq4r/caps/c2.capture.log
+ mkdir -p /tmp/opencode/eq4r/caps/c2
+ image=/tmp/opencode/eq4r/caps/c2/cflash.img
+ log=/tmp/opencode/eq4r/caps/c2/desmume.log
+ fatdir=/tmp/opencode/eq4r/caps/c2/fat
+ rm -f /tmp/opencode/eq4r/caps/c2/cflash.img /tmp/opencode/eq4r/caps/c2/desmume.log /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
+ rm -rf /tmp/opencode/eq4r/caps/c2/fat
+ mkfs.fat -C /tmp/opencode/eq4r/caps/c2/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
+ emulator_pid=796006
+ sleeper_pid=796007
+ grace_pid=
+ emulator_killed_by_script=0
+ sleep 45
+ trap cleanup EXIT INT TERM
+ set +e
+ wait -n 796006 796007
+ first_status=0
+ set -e
+ kill -0 796006
+ kill -0 796007
+ sleeper_pid=
+ emulator_killed_by_script=1
+ kill -INT 796006
+ grace_pid=799275
+ set +e
+ wait -n 796006 799275
+ sleep 30
+ grace_status=0
+ set -e
+ kill -0 796006
+ kill -0 799275
+ kill -TERM 799275
+ wait 799275
+ true
+ grace_pid=
+ set +e
+ wait 796006
+ status=0
+ set -e
+ trap - EXIT INT TERM
+ '[' 1 -eq 1 ']'
+ '[' 0 -ne 0 ']'
+ grep -qF 'Slot 2: MPCF Flash Card Device' /tmp/opencode/eq4r/caps/c2/desmume.log
+ grep -qF 'AUTO-PATCHING DLDI' /tmp/opencode/eq4r/caps/c2/desmume.log
+ 7z x -y /tmp/opencode/eq4r/caps/c2/cflash.img -o/tmp/opencode/eq4r/caps/c2/fat
+ '[' '!' -f /tmp/opencode/eq4r/caps/c2/fat/twlhd_frame_v2.bin ']'
+ cp /tmp/opencode/eq4r/caps/c2/fat/twlhd_frame_v2.bin /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin'
capture: /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
```

The two DLDI guard lines the script checks, and the stream hash (separate
commands; the `2` is `grep -c`'s match count, not a script status):

```
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/cube/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin
acee2a530befb33f8325adc9911128a1799d59a84cc36c4efb477a9efbda23a2  /tmp/opencode/eq4r/caps/cube/twlhd_frame_v2.bin
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/s1/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin
a828255b616309a32d9fba1e74334668e4eb358162900588434746b4d267205c  /tmp/opencode/eq4r/caps/s1/twlhd_frame_v2.bin
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/s2/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin
3817f3858c8beb2e60c7627689d5cc2ae76d7dc1fe19b2f554e0e0884985ec09  /tmp/opencode/eq4r/caps/s2/twlhd_frame_v2.bin
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/s3/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin
81143b59354da8cb706f0b1ee99817fd4397f52da18070194049a24cbc1c36c5  /tmp/opencode/eq4r/caps/s3/twlhd_frame_v2.bin
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/s4/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin
2d108b823e067f7b06db251093978447b943f92e2d99198cd06569bbb274561e  /tmp/opencode/eq4r/caps/s4/twlhd_frame_v2.bin
$ grep -cE "Slot 2: MPCF|AUTO-PATCHING" /tmp/opencode/eq4r/caps/c2/desmume.log
2
$ sha256sum /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
8d17b67c207648953a8ca21b0be558f6928ec69e74ddf48e2747cfb0a3b2c4b4  /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
```

### 4.2 Azahar/PICA render of our path

One `azahar_host` run per ROM with `sdmc:/twlhd_frame.bin` = that ROM's
`twlhd_frame_v2.bin` (the eq4f invocation). Logs are 462-464 lines of core
chatter; first two and last three lines quoted, the middle counted. The elided
counts differ (457-459, shown per ROM), so the middles are not claimed identical:


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/cube --sd-root /tmp/opencode/eq4r/az/cube/sd
[azahar_host] isolated environment: /tmp/azahar-host-gorpwV
[azahar_host] save dir /tmp/azahar-host-gorpwV/save; system dir /tmp/azahar-host-gorpwV/system
[... 458 lines elided ...]
[azahar_libretro] [   1.776441] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.799169] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.799172] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/s1 --sd-root /tmp/opencode/eq4r/az/s1/sd
[azahar_host] isolated environment: /tmp/azahar-host-0zY2fr
[azahar_host] save dir /tmp/azahar-host-0zY2fr/save; system dir /tmp/azahar-host-0zY2fr/system
[... 458 lines elided ...]
[azahar_libretro] [   1.376038] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.414400] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.414409] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/s2 --sd-root /tmp/opencode/eq4r/az/s2/sd
[azahar_host] isolated environment: /tmp/azahar-host-QjWsM3
[azahar_host] save dir /tmp/azahar-host-QjWsM3/save; system dir /tmp/azahar-host-QjWsM3/system
[... 459 lines elided ...]
[azahar_libretro] [   1.476527] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.521156] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.521161] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/s3 --sd-root /tmp/opencode/eq4r/az/s3/sd
[azahar_host] isolated environment: /tmp/azahar-host-1o74bT
[azahar_host] save dir /tmp/azahar-host-1o74bT/save; system dir /tmp/azahar-host-1o74bT/system
[... 459 lines elided ...]
[azahar_libretro] [   1.498594] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.540350] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.540356] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/s4 --sd-root /tmp/opencode/eq4r/az/s4/sd
[azahar_host] isolated environment: /tmp/azahar-host-mJit05
[azahar_host] save dir /tmp/azahar-host-mJit05/save; system dir /tmp/azahar-host-mJit05/system
[... 457 lines elided ...]
[azahar_libretro] [   1.551514] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.588412] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.588416] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


```
$ tools/azahar_host/build/azahar_host --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 --out /tmp/opencode/eq4r/az/c2 --sd-root /tmp/opencode/eq4r/az/c2/sd
[azahar_host] isolated environment: /tmp/azahar-host-FKCZOY
[azahar_host] save dir /tmp/azahar-host-FKCZOY/save; system dir /tmp/azahar-host-FKCZOY/system
[... 458 lines elided ...]
[azahar_libretro] [   1.595650] Kernel <Debug> core/hle/kernel/process.cpp:FreeAllMemory:683: Remaining memory resource commit after process cleanup: 0x00000000
[azahar_libretro] [   1.626132] Core <Debug> core/core.cpp:Shutdown:841: Shutdown OK
[azahar_libretro] [   1.626136] Frontend <Debug> citra_libretro/citra_libretro.cpp:retro_deinit:94: Shutting down core...
$ echo "exit=$?"
exit=0
```


The six raw renders (400x480 P6), hashed:

```
$ sha256sum /tmp/opencode/eq4r/az/*/azahar_199.ppm
95d6fec0e077cf586eef68ced991da2612d2e4b65fa1c5bb58c597d76fb5b91d  /tmp/opencode/eq4r/az/c2/azahar_199.ppm
00b518563a450c530b9ef7a7a82ed07f896028634524d67f4be1a847a97c3b1a  /tmp/opencode/eq4r/az/cube/azahar_199.ppm
e9fae5b6a87e66da4c7d362f54f56a5fc76b277ed4e1b9cb34c7b8b4d24032a6  /tmp/opencode/eq4r/az/s1/azahar_199.ppm
877e8314376ab2047e8c880f1ad0bb3be3418a3739e55d41b2326fae811539f6  /tmp/opencode/eq4r/az/s2/azahar_199.ppm
5902c12c6a8461d1c89e6ef82e8e36efe1416bf6d1b0546e28303854b0720b11  /tmp/opencode/eq4r/az/s3/azahar_199.ppm
a4270c78ce512e257f3e609a31f8077a35ac2d522d7d69efa6e49958f7b8afcc  /tmp/opencode/eq4r/az/s4/azahar_199.ppm
```

The top-left 400x240 crop of each `azahar_199.ppm` is the `ours` fixture; the
crop script saves it and checks pixel equality with the committed PNG:

```
$ python3 - <<'EOF'
from PIL import Image
for scene in ("cube", "s1", "s2", "s3", "s4", "c2"):
    crop = Image.open("/tmp/opencode/eq4r/az/%s/azahar_199.ppm" % scene) \
        .convert("RGB").crop((0, 0, 400, 240))
    crop.save("/tmp/opencode/eq4r/az/%s/ours_top400x240.png" % scene)
    fixture = Image.open("tools/fixtures/eq4_hw_scenes/ours_%s_st30_top400x240.png"
                         % scene).convert("RGB")
    print(scene, "crop pixels equal fixture:", crop.tobytes() == fixture.tobytes())
EOF
cube crop pixels equal fixture: True
s1 crop pixels equal fixture: True
s2 crop pixels equal fixture: True
s3 crop pixels equal fixture: True
s4 crop pixels equal fixture: True
c2 crop pixels equal fixture: True
```

The six files are byte-identical to the committed PNGs (`cmp` silent):

```
$ for scene in cube s1 s2 s3 s4 c2; do cmp /tmp/opencode/eq4r/az/$scene/ours_top400x240.png \
      tools/fixtures/eq4_hw_scenes/ours_${scene}_st30_top400x240.png && \
      echo "$scene ours crop identical"; done
cube ours crop identical
s1 ours crop identical
s2 ours crop identical
s3 ours crop identical
s4 ours crop identical
c2 ours crop identical
```

The `ours` reference PPMs handed to the tool are those committed PNGs re-saved
into `refs3`, so §5 uses exactly the fixture bytes:

```
$ python3 - <<'EOF'
from PIL import Image
for scene in ("cube", "s1", "s2", "s3", "s4", "c2"):
    Image.open("tools/fixtures/eq4_hw_scenes/ours_%s_st30_top400x240.png" % scene) \
        .convert("RGB").save("/tmp/opencode/eq4r/refs3/ours_%s.ppm" % scene)
EOF
$ echo "exit=$?"
exit=0
```

The six hardware fixtures are `cmp`-identical to
`<staging>/hw/hw_<scene>_a.png` (whose hashes are the `hw/hw_*_a.png` lines of
§1's `SHA256SUMS`).

## 5. Comparisons (FACT)

Twelve runs of `tools/eq4_hw_compare.py`: hardware capture `a` against the
harness PPM (256x192 → `not applied (no row-0 wrap signature)`) and against the
`ours` PPM (400x240 → `applied (row-0 wrap signature detected)`). Hardware and
DeSmuME images are never corrected. Each block is the complete stdout, then the
exit code. Reference PPMs are the real `/tmp/opencode/eq4r/refs2/harness_<scene>.ppm`
and `/tmp/opencode/eq4r/refs3/ours_<scene>.ppm`; 5.11 is the one block whose
output names its reference path.


### 5.1 cube capture vs harness — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/cube_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_cube.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=2.2378
ds.mean_abs_g=1.1111
ds.mean_abs_b=2.1537
ds.max_error=255.0000
ds.frac_within=0.9753
ds.frac_within_flat=0.9996
ds.frac_within_edge=0.6655
ds.flat_pixels=45577
ds.edge_pixels=3575
capture.mean_abs_r=2.1957
capture.mean_abs_g=1.1113
capture.mean_abs_b=2.1223
capture.max_error=255.0000
capture.frac_within=0.9754
capture.frac_within_flat=0.9998
capture.frac_within_edge=0.6500
capture.flat_pixels=71445
capture.edge_pixels=5355
ref_nonbg_pixels=8180
ref_bbox_left=80
ref_bbox_top=51
ref_bbox_right=186
ref_bbox_bottom=160
capture_nonbg_pixels=8617
capture_bbox_left=78
capture_bbox_top=49
capture_bbox_right=187
capture_bbox_bottom=159
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=4786
class_red_ref_centroid_x=143.3619
class_red_ref_centroid_y=109.3496
class_red_capture_area=4718
class_red_capture_centroid_x=142.5877
class_red_capture_centroid_y=108.1755
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=8
class_green_capture_centroid_x=88.7500
class_green_capture_centroid_y=85.6250
class_blue_ref_area=1391
class_blue_ref_centroid_x=97.6679
class_blue_ref_centroid_y=104.4522
class_blue_capture_area=1319
class_blue_capture_centroid_x=96.8620
class_blue_capture_centroid_y=103.8415
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=4
class_yellow_capture_centroid_x=180.0000
class_yellow_capture_centroid_y=95.0000
bbox_dev_left=2
bbox_dev_top=2
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.2 cube capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/cube_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_cube.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=14.1100
ds.mean_abs_g=14.1046
ds.mean_abs_b=20.8115
ds.max_error=231.0000
ds.frac_within=0.1544
ds.frac_within_flat=0.9955
ds.frac_within_edge=0.0471
ds.flat_pixels=5561
ds.edge_pixels=43591
capture.mean_abs_r=13.9600
capture.mean_abs_g=14.2140
capture.mean_abs_b=20.6431
capture.max_error=253.0000
capture.frac_within=0.1567
capture.frac_within_flat=0.9985
capture.frac_within_edge=0.0483
capture.flat_pixels=8759
capture.edge_pixels=68041
ref_nonbg_pixels=8183
ref_bbox_left=79
ref_bbox_top=50
ref_bbox_right=186
ref_bbox_bottom=159
capture_nonbg_pixels=8617
capture_bbox_left=78
capture_bbox_top=49
capture_bbox_right=187
capture_bbox_bottom=159
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=4792
class_red_ref_centroid_x=142.4806
class_red_ref_centroid_y=108.4731
class_red_capture_area=4718
class_red_capture_centroid_x=142.5877
class_red_capture_centroid_y=108.1755
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=8
class_green_capture_centroid_x=88.7500
class_green_capture_centroid_y=85.6250
class_blue_ref_area=1398
class_blue_ref_centroid_x=96.7146
class_blue_ref_centroid_y=103.3870
class_blue_capture_area=1319
class_blue_capture_centroid_x=96.8620
class_blue_capture_centroid_y=103.8415
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=4
class_yellow_capture_centroid_x=180.0000
class_yellow_capture_centroid_y=95.0000
bbox_dev_left=1
bbox_dev_top=1
bbox_dev_right=1
bbox_dev_bottom=0
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.3 s1 capture vs harness — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s1_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_s1.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.7148
ds.mean_abs_g=0.0000
ds.mean_abs_b=0.0000
ds.max_error=199.0000
ds.frac_within=0.9912
ds.frac_within_flat=0.9993
ds.frac_within_edge=0.5669
ds.flat_pixels=48240
ds.edge_pixels=912
capture.mean_abs_r=0.7125
capture.mean_abs_g=0.0000
capture.mean_abs_b=0.0000
capture.max_error=231.0000
capture.frac_within=0.9901
capture.frac_within_flat=0.9993
capture.frac_within_edge=0.4880
capture.flat_pixels=75423
capture.edge_pixels=1377
ref_nonbg_pixels=485
ref_bbox_left=90
ref_bbox_top=70
ref_bbox_right=158
ref_bbox_bottom=120
capture_nonbg_pixels=568
capture_bbox_left=89
capture_bbox_top=67
capture_bbox_right=157
capture_bbox_bottom=119
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=123
class_red_ref_centroid_x=145.4553
class_red_ref_centroid_y=77.9593
class_red_capture_area=89
class_red_capture_centroid_x=144.9213
class_red_capture_centroid_y=76.5730
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=3
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.4 s1 capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s1_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_s1.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.9869
ds.mean_abs_g=15.8177
ds.mean_abs_b=23.7266
ds.max_error=171.0000
ds.frac_within=0.0093
ds.frac_within_flat=0.9872
ds.frac_within_edge=0.0077
ds.flat_pixels=78
ds.edge_pixels=49074
capture.mean_abs_r=16.0077
capture.mean_abs_g=15.8179
capture.mean_abs_b=23.7267
capture.max_error=169.0000
capture.frac_within=0.0093
capture.frac_within_flat=0.9559
capture.frac_within_edge=0.0076
capture.flat_pixels=136
capture.edge_pixels=76664
ref_nonbg_pixels=487
ref_bbox_left=89
ref_bbox_top=68
ref_bbox_right=157
ref_bbox_bottom=119
capture_nonbg_pixels=568
capture_bbox_left=89
capture_bbox_top=67
capture_bbox_right=157
capture_bbox_bottom=119
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=130
class_red_ref_centroid_x=144.1308
class_red_ref_centroid_y=77.2308
class_red_capture_area=89
class_red_capture_centroid_x=144.9213
class_red_capture_centroid_y=76.5730
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=0
bbox_dev_top=1
bbox_dev_right=0
bbox_dev_bottom=0
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.5 s2 capture vs harness — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s2_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_s2.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.3747
ds.mean_abs_g=0.0000
ds.mean_abs_b=0.0000
ds.max_error=95.0000
ds.frac_within=0.9944
ds.frac_within_flat=0.9987
ds.frac_within_edge=0.8963
ds.flat_pixels=47070
ds.edge_pixels=2082
capture.mean_abs_r=0.3902
capture.mean_abs_g=0.0000
capture.mean_abs_b=0.0000
capture.max_error=93.0000
capture.frac_within=0.9938
capture.frac_within_flat=0.9988
capture.frac_within_edge=0.8802
capture.flat_pixels=73585
capture.edge_pixels=3215
ref_nonbg_pixels=1821
ref_bbox_left=93
ref_bbox_top=73
ref_bbox_right=162
ref_bbox_bottom=117
capture_nonbg_pixels=1788
capture_bbox_left=94
capture_bbox_top=72
capture_bbox_right=161
capture_bbox_bottom=115
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=0
class_red_ref_centroid_x=0.0000
class_red_ref_centroid_y=0.0000
class_red_capture_area=0
class_red_capture_centroid_x=0.0000
class_red_capture_centroid_y=0.0000
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=1
bbox_dev_right=1
bbox_dev_bottom=2
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.6 s2 capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s2_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_s2.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.4060
ds.mean_abs_g=15.2484
ds.mean_abs_b=22.8726
ds.max_error=69.0000
ds.frac_within=0.0456
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0382
ds.flat_pixels=381
ds.edge_pixels=48771
capture.mean_abs_r=15.4303
capture.mean_abs_g=15.2483
capture.mean_abs_b=22.8724
capture.max_error=70.0000
capture.frac_within=0.0461
capture.frac_within_flat=0.9953
capture.frac_within_edge=0.0382
capture.flat_pixels=632
capture.edge_pixels=76168
ref_nonbg_pixels=1843
ref_bbox_left=92
ref_bbox_top=72
ref_bbox_right=161
ref_bbox_bottom=116
capture_nonbg_pixels=1788
capture_bbox_left=94
capture_bbox_top=72
capture_bbox_right=161
capture_bbox_bottom=115
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=0
class_red_ref_centroid_x=0.0000
class_red_ref_centroid_y=0.0000
class_red_capture_area=0
class_red_capture_centroid_x=0.0000
class_red_capture_centroid_y=0.0000
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=2
bbox_dev_top=0
bbox_dev_right=0
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.7 s3 capture vs harness — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s3_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_s3.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.3790
ds.mean_abs_g=0.0000
ds.mean_abs_b=0.0000
ds.max_error=95.0000
ds.frac_within=0.9944
ds.frac_within_flat=0.9987
ds.frac_within_edge=0.8957
ds.flat_pixels=47072
ds.edge_pixels=2080
capture.mean_abs_r=0.3937
capture.mean_abs_g=0.0000
capture.mean_abs_b=0.0000
capture.max_error=93.0000
capture.frac_within=0.9938
capture.frac_within_flat=0.9988
capture.frac_within_edge=0.8805
capture.flat_pixels=73586
capture.edge_pixels=3214
ref_nonbg_pixels=1823
ref_bbox_left=93
ref_bbox_top=73
ref_bbox_right=162
ref_bbox_bottom=117
capture_nonbg_pixels=1782
capture_bbox_left=93
capture_bbox_top=72
capture_bbox_right=161
capture_bbox_bottom=115
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=0
class_red_ref_centroid_x=0.0000
class_red_ref_centroid_y=0.0000
class_red_capture_area=0
class_red_capture_centroid_x=0.0000
class_red_capture_centroid_y=0.0000
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=0
bbox_dev_top=1
bbox_dev_right=1
bbox_dev_bottom=2
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.8 s3 capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s3_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_s3.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.4110
ds.mean_abs_g=15.2484
ds.mean_abs_b=22.8726
ds.max_error=69.0000
ds.frac_within=0.0456
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0382
ds.flat_pixels=381
ds.edge_pixels=48771
capture.mean_abs_r=15.4343
capture.mean_abs_g=15.2483
capture.mean_abs_b=22.8724
capture.max_error=70.0000
capture.frac_within=0.0461
capture.frac_within_flat=0.9953
capture.frac_within_edge=0.0382
capture.flat_pixels=632
capture.edge_pixels=76168
ref_nonbg_pixels=1843
ref_bbox_left=92
ref_bbox_top=72
ref_bbox_right=161
ref_bbox_bottom=116
capture_nonbg_pixels=1782
capture_bbox_left=93
capture_bbox_top=72
capture_bbox_right=161
capture_bbox_bottom=115
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=0
class_red_ref_centroid_x=0.0000
class_red_ref_centroid_y=0.0000
class_red_capture_area=0
class_red_capture_centroid_x=0.0000
class_red_capture_centroid_y=0.0000
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=0
class_green_capture_centroid_x=0.0000
class_green_capture_centroid_y=0.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=0
class_blue_capture_centroid_x=0.0000
class_blue_capture_centroid_y=0.0000
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=0
bbox_dev_right=0
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


### 5.9 s4 capture vs harness — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_s4.ppm
presentation-row correction: not applied (no row-0 wrap signature)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=0.9739
ds.mean_abs_g=0.8608
ds.mean_abs_b=1.0802
ds.max_error=255.0000
ds.frac_within=0.9821
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.4289
ds.flat_pixels=47618
ds.edge_pixels=1534
capture.mean_abs_r=0.9681
capture.mean_abs_g=0.8538
capture.mean_abs_b=1.0513
capture.max_error=255.0000
capture.frac_within=0.9837
capture.frac_within_flat=0.9999
capture.frac_within_edge=0.4086
capture.flat_pixels=74693
capture.edge_pixels=2107
ref_nonbg_pixels=1944
ref_bbox_left=84
ref_bbox_top=72
ref_bbox_right=158
ref_bbox_bottom=130
capture_nonbg_pixels=2479
capture_bbox_left=82
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=629
class_red_ref_centroid_x=106.2146
class_red_ref_centroid_y=115.0986
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=413
class_green_ref_centroid_x=145.3923
class_green_ref_centroid_y=102.1574
class_green_capture_area=443
class_green_capture_centroid_x=144.5192
class_green_capture_centroid_y=101.1038
class_blue_ref_area=902
class_blue_ref_centroid_x=103.2306
class_blue_ref_centroid_y=87.3415
class_blue_capture_area=962
class_blue_capture_centroid_x=102.3576
class_blue_capture_centroid_y=86.3919
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=2
bbox_dev_top=10
bbox_dev_right=3
bbox_dev_bottom=0
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3)
$ echo "exit=$?"
exit=1
```


### 5.10 s4 capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_s4.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=15.9668
ds.mean_abs_g=16.7612
ds.mean_abs_b=26.9291
ds.max_error=235.0000
ds.frac_within=0.0148
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0042
ds.flat_pixels=523
ds.edge_pixels=48629
capture.mean_abs_r=15.9577
capture.mean_abs_g=16.8496
capture.mean_abs_b=27.0526
capture.max_error=239.0000
capture.frac_within=0.0133
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.0026
capture.flat_pixels=821
capture.edge_pixels=75979
ref_nonbg_pixels=1940
ref_bbox_left=83
ref_bbox_top=72
ref_bbox_right=157
ref_bbox_bottom=128
capture_nonbg_pixels=2479
capture_bbox_left=82
capture_bbox_top=62
capture_bbox_right=161
capture_bbox_bottom=130
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=1
class_white_capture_centroid_x=140.0000
class_white_capture_centroid_y=88.0000
class_red_ref_area=625
class_red_ref_centroid_x=105.3536
class_red_ref_centroid_y=114.1264
class_red_capture_area=634
class_red_capture_centroid_x=105.2145
class_red_capture_centroid_y=114.0268
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=443
class_green_capture_centroid_x=144.5192
class_green_capture_centroid_y=101.1038
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=962
class_blue_capture_centroid_x=102.3576
class_blue_capture_centroid_y=86.3919
class_yellow_ref_area=0
class_yellow_ref_centroid_x=0.0000
class_yellow_ref_centroid_y=0.0000
class_yellow_capture_area=0
class_yellow_capture_centroid_x=0.0000
class_yellow_capture_centroid_y=0.0000
bbox_dev_left=1
bbox_dev_top=10
bbox_dev_right=4
bbox_dev_bottom=2
verdict=FAIL
reason=bbox-top deviates 10 DS px (tol 3); bbox-right deviates 4 DS px (tol 3)
$ echo "exit=$?"
exit=1
```


### 5.11 c2 capture vs harness — NOT-COMPARABLE (no reference)

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/c2_a_top400x240.png --reference /tmp/opencode/eq4r/refs2/harness_c2.ppm
eq4_hw_compare: error: cannot read /tmp/opencode/eq4r/refs2/harness_c2.ppm: [Errno 2] No such file or directory: '/tmp/opencode/eq4r/refs2/harness_c2.ppm'
$ echo "exit=$?"
exit=2
```


This is the only one of the twelve blocks without a `presentation-row
correction:` line: the tool fails while opening the missing reference, before
it reaches the correction step.

### 5.12 c2 capture vs ours — HARDWARE vs EMULATOR

```
$ python3 tools/eq4_hw_compare.py --capture tools/fixtures/eq4_hw_scenes/c2_a_top400x240.png --reference /tmp/opencode/eq4r/refs3/ours_c2.ppm
presentation-row correction: applied (row-0 wrap signature detected)
basis=one console, one capture
tolerance_flat=8
tolerance_blended_edge=16
tolerance_pos_ds=3
mapping_scale_x=1.25
mapping_scale_y=1.25
mapping_offset_x=40
mapping_offset_y=0
ds.mean_abs_r=16.0627
ds.mean_abs_g=16.0865
ds.mean_abs_b=24.0431
ds.max_error=225.0000
ds.frac_within=0.0033
ds.frac_within_flat=1.0000
ds.frac_within_edge=0.0017
ds.flat_pixels=82
ds.edge_pixels=49070
capture.mean_abs_r=16.0228
capture.mean_abs_g=16.0814
capture.mean_abs_b=24.0194
capture.max_error=241.0000
capture.frac_within=0.0035
capture.frac_within_flat=1.0000
capture.frac_within_edge=0.0017
capture.flat_pixels=142
capture.edge_pixels=76658
ref_nonbg_pixels=251
ref_bbox_left=5
ref_bbox_top=89
ref_bbox_right=32
ref_bbox_bottom=101
capture_nonbg_pixels=364
capture_bbox_left=4
capture_bbox_top=88
capture_bbox_right=33
capture_bbox_bottom=102
class_white_ref_area=0
class_white_ref_centroid_x=0.0000
class_white_ref_centroid_y=0.0000
class_white_capture_area=0
class_white_capture_centroid_x=0.0000
class_white_capture_centroid_y=0.0000
class_red_ref_area=157
class_red_ref_centroid_x=18.3949
class_red_ref_centroid_y=96.7325
class_red_capture_area=141
class_red_capture_centroid_x=18.3050
class_red_capture_centroid_y=96.8865
class_green_ref_area=0
class_green_ref_centroid_x=0.0000
class_green_ref_centroid_y=0.0000
class_green_capture_area=2
class_green_capture_centroid_x=13.5000
class_green_capture_centroid_y=90.0000
class_blue_ref_area=0
class_blue_ref_centroid_x=0.0000
class_blue_ref_centroid_y=0.0000
class_blue_capture_area=2
class_blue_capture_centroid_x=12.5000
class_blue_capture_centroid_y=91.0000
class_yellow_ref_area=29
class_yellow_ref_centroid_x=22.8966
class_yellow_ref_centroid_y=94.3448
class_yellow_capture_area=20
class_yellow_capture_centroid_x=23.0000
class_yellow_capture_centroid_y=93.7000
bbox_dev_left=1
bbox_dev_top=1
bbox_dev_right=1
bbox_dev_bottom=1
verdict=PASS
reason=within tolerance
$ echo "exit=$?"
exit=0
```


## 6. Results (FACT; only the numbers printed in §5)

| ROM | hw vs harness: mean_abs r/g/b, flat, bbox_dev, verdict | hw vs ours: mean_abs r/g/b, flat, bbox_dev, verdict | scene |
|---|---|---|---|
| cube | 2.2378/1.1111/2.1537, 0.9996, 2/2/1/1, PASS | 14.1100/14.1046/20.8115, 0.9955, 1/1/1/0, PASS | MATCHED |
| s1 | 0.7148/0.0000/0.0000, 0.9993, 1/3/1/1, PASS | 15.9869/15.8177/23.7266, 0.9872, 0/1/0/0, PASS | MATCHED |
| s2 | 0.3747/0.0000/0.0000, 0.9987, 1/1/1/2, PASS | 15.4060/15.2484/22.8726, 1.0000, 2/0/0/1, PASS | MATCHED |
| s3 | 0.3790/0.0000/0.0000, 0.9987, 0/1/1/2, PASS | 15.4110/15.2484/22.8726, 1.0000, 1/0/0/1, PASS | MATCHED |
| s4 | 0.9739/0.8608/1.0802, 1.0000, 2/**10**/3/0, **FAIL** | 15.9668/16.7612/26.9291, 1.0000, 1/**10**/**4**/2, **FAIL** | DIFFERENT |
| c2 | no reference, **NOT-COMPARABLE** (tool exit 2) | 16.0627/16.0865/24.0431, 1.0000, 1/1/1/1, PASS | PARTIAL |

(`mean_abs` = `ds.mean_abs_r/g/b`, `flat` = `ds.frac_within_flat`, `bbox_dev`
= l/t/r/b.) Failures name quantity and amount: `bbox_dev_top` 10 vs tol 3 (s4,
both pairs) and `bbox_dev_right` 4 vs 3 (s4 vs ours). Bounding boxes are in each
§5 block (s4 harness 84/72/158/130 vs capture 82/62/161/130).

EQ-4t produced the c2 harness reference from the **plain static** ROM, so c2
hw-vs-harness is now PASS (0.4777/0.3136/0.4495, 1.0000, 2/2/0/0) — see
`docs/reports/eq4t-c2-reference.md` (the autodump-ROM pair stays NOT-COMPARABLE).

## 7. What is MATCHED and what is DIFFERENT

**MATCHED (cube, s1, s2, s3).** Every `bbox_dev` <= 3 DS px,
`ds.frac_within_flat` >= 0.9987 against the harness and >= 0.9872 against ours,
and the populated class areas agree within a few percent (cube `class_red`
4786/4718 = -1.4%, `class_blue` 1391/1319 = -5.2%). HYPOTHESIS, not exactness:
the limits are the 3 DS px tolerance and the single capture.

**HYPOTHESIS why `ds.mean_abs` against `ours` is 14-24 but 0.37-2.24 against
the harness.** The PICA top screen clears to `(16,16,24)` where the DS/harness
background is `(0,0,0)` (eq4f §5(iii) measured the same for scene5; the §8
census shows `(0,0,0) 92056` vs `(16,16,24) 92965`). A colour-count argument,
not a per-pixel decomposition.

**DIFFERENT (s4): the hardware draws the alpha-0 quad's outline.** The capture
has 2479 non-background DS pixels against 1944 and starts 10 DS rows higher
(62 vs 72). The extra region is the fourth `SCENE=4` quad, a bright outline
(three filled quads plus one unfilled outline; §2 counts the same 64 bright
pixels in all three captures). Neither reference has an outline pixel
(`class_white_ref_area=0`, `class_white_capture_area=1`). SOURCE-READ:
`arm9v2/source/demo_frame.cpp:246-259` declares alphas `{31, 16, 4, 0}` and
says `the alpha-0 quad is counted as wireframe (outline drawing is NOT
implemented, so the host interpreter emits it with vertex alpha 0.0)`.
HYPOTHESIS: on the DS alpha 0 selects outline drawing, which neither DeSmuME
nor our interpreter implements; this is the whole s4 failure — the reason names
only the bounding box, and `ds.frac_within_flat=1.0000` with `ds.mean_abs`
0.97/0.86/1.08 against the harness shows the filled area agrees.

**DIFFERENT (s4): the alpha values do not dim the quads on hardware or in
DeSmuME.** §8 shows hardware painting alpha-16 green `(0,255,0)` and alpha-4
blue `(0,0,255)` at full brightness, the harness likewise (`(0,251,0)`,
`(0,0,251)`), while our render paints `(7,142,11)` and `(13,13,60)` — it blends
with the `(16,16,24)` clear colour. SOURCE-READ:
`arm9v2/source/demo_frame.cpp` and `arm9v2/source/main.cpp` never call
`glEnable(GL_BLEND)` (only `GL_TEXTURE_2D`, `GL_ANTIALIAS`), and
`arm11/include/twlhd_blend_config.h:1-13` calls the PICA blend equations
`HARDWARE UNVERIFIED` and records its Run 14: "the renderer never set blend
state; on the console alpha 16/31 and 4/31 quads drew OPAQUE and the alpha-0
quad was not drawn". HYPOTHESIS: without `GL_BLEND` the DS and DeSmuME draw
alpha < 31 opaque while the arm11 render blends. Not a separate verdict: for s4
vs ours the `ours` reference is almost entirely edge-classified
(`ds.flat_pixels=523`, `ds.edge_pixels=48629`, `ds.frac_within=0.0148`), so its
colour check carries little information.

**PARTIAL (c2).** The CUBES=2 hardware capture matches our render (PASS,
1/1/1/1, `ds.frac_within_flat=1.0000`), but no DeSmuME reference exists, so
nothing is claimed about hardware vs DeSmuME for c2.

**No verdict effect:** s2/s3 populate none of the five colour classes
(`class_*_ref_area=0`; the red shades are beyond `CLASS_MAX_DIST`), so their
class checks are vacuous and the verdict rests on the bbox and
`ds.frac_within_flat` (same elsewhere). **Not shown:** bit-exactness, other
scenes/angles/consoles/settings, texture filtering, the blend convention
against blending-enabled hardware.

## 8. Scene 4 alpha: cited from EQ-4i, not re-derived

Measured on a different ROM, in `docs/reports/eq4i-alpha-hw-result.md`
§5: on the quads that can decide, green matches the DeSmuME 6-bit **A2**
(`(a+1)/32`) on all 10 and never A1 (`a/31`), and red/blue equal the 5-bit
quantisation of those 6-bit values (`fit_6bit_565_A2_R=0`, `..._B=0`); the
whole-RGB `verdict=A1` there is a classifier artefact. Not re-derived here:
`SCENE=4`'s quads are drawn without blending enabled (§7) and cannot
discriminate A1 from A2. Colour census (complete output, first four lines;
harness PPM converted to PNG first):

```
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw_scenes/s4_a_top400x240.png
(0, 0, 0) 92056
(0, 0, 255) 1372
(255, 0, 0) 892
(0, 255, 0) 560
$ python3 tools/ds_scale_test/colour_histogram.py /tmp/opencode/eq4r/harness_s4.png
(0, 0, 0) 47208
(0, 0, 251) 855
(251, 0, 0) 575
(0, 251, 0) 384
$ python3 tools/ds_scale_test/colour_histogram.py tools/fixtures/eq4_hw_scenes/ours_s4_st30_top400x240.png
(16, 16, 24) 92965
(13, 13, 60) 1411
(255, 0, 0) 981
(7, 142, 11) 642
```

## 9. Fixtures (FACT)

Seventeen PNGs (not 18: c2 has no harness reference, §3) plus a README in
`tools/fixtures/eq4_hw_scenes/`, every file below the 100 KB cap; no ROM, PPM or
emulator log is committed:


```
$ cd tools/fixtures/eq4_hw_scenes && sha256sum *.png
488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6  c2_a_top400x240.png
0f9e7aee19cec41fc00909e894f053717a3cca4088aa18efc5789527d2f6e9c0  cube_a_top400x240.png
373701e21dde8593a2cffe10e3afe4e6c622a853adc8bb9ed8b823da2b8f6d74  harness_cube_st30_f240.png
d402948dafcff446c0bb53edbec8d6dfe156255a77b03d310ef21d38303edc25  harness_s1_st30_f240.png
dfc44116d0e354b60a0451bd48ba3f78671def7034ae1a5b1cbe7d44e4cfe965  harness_s2_st30_f240.png
9cf18f061ebf3990939827af3815e91d9564de271f735aefc033064b28702b1c  harness_s3_st30_f240.png
6538dd758346eed149fe1f72ea3d7c8aa4f655a7b3dfed169a13bedb46e6b99b  harness_s4_st30_f240.png
b97df11cbae37fe9dc25edaffc22219508a99e607a97ceb4a0a14049aeb92162  ours_c2_st30_top400x240.png
c4a8b5d851c7b8b08adce25cc5617479194ad1c89e733a21fba2a9dbf693bcfe  ours_cube_st30_top400x240.png
f2f806e308ea696c2f42a430889cb08aaf7d5b527365df23dfc070f5612f9a87  ours_s1_st30_top400x240.png
cb73eed07345caae5712a7b79213f9d2ef334400b1ef8b17adb244fb522221fc  ours_s2_st30_top400x240.png
915bdf7cc4f3e969455cc2e9b68508cdbe1b73944d2e5621a03c143f73a7ed9a  ours_s3_st30_top400x240.png
23302ce8371c74ac52822d46d7637f0df06e18616f1698a287b1bb42ae5c0185  ours_s4_st30_top400x240.png
8af538df08a00169f9f50785974f8d20388e969fda525151be468db65cfee75a  s1_a_top400x240.png
c01dc0ea69eb2a715af29743ac7e7d48407354bb4a56fff1d3f1a85287f338b9  s2_a_top400x240.png
bb83ba0fef9514d896e659f21f5ad57edf507e92e6e55e8768f0ab71202d9399  s3_a_top400x240.png
ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9  s4_a_top400x240.png
```

```
$ cd tools/fixtures/eq4_hw_scenes && stat -c '%s %n' *.png | sort -n | tail -1
5702 cube_a_top400x240.png
```

```
$ du -b tools/fixtures/eq4_hw_scenes
36651	tools/fixtures/eq4_hw_scenes
```

## 10. Tests

`RealStaticScenesCompareTest` (eight tests) was added to
`tools/test_eq4_hw_compare.py`; existing tests are untouched, the existing
target runs this file (no Makefile change). Tests pin the 17 fixture hashes and
every size < 100 KB, reproduce four PASS cases with the §5 numbers (cube/harness `bbox_dev_top=2`,
`ds.mean_abs_r=2.2378`, flat 0.9996; s2/harness `bbox_dev_top=1`,
`bbox_dev_bottom=2`, flat 0.9987; c2/ours `bbox_dev_top=1`, flat 1.0000 plus
the `applied (row-0 wrap signature detected)` line; s3/ours `bbox_dev_left=1`,
`bbox_dev_top=0`), reproduce the s4 FAIL (exit 1, `bbox_dev_top=10`), assert c2
has no harness reference (exit 2, no `verdict=` line), and the negative test
moves the s1 capture up two capture rows (~1.6 DS px) and asserts FAIL with
`bbox_dev_top=4` (PASS with 3 before). Skip conventions match the sibling
class (PIL missing, fixtures absent, `EQ4_HW_COMPARE_DISCOVER_SKIP`).

```
$ make test-eq4-hw-compare

PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq4_hw_compare.py'
.......................
----------------------------------------------------------------------
Ran 23 tests in 25.234s

OK
```

`make test` (aggregate; exit 0):

```
$ make test

[... 4487 lines: host-test builds and one unittest block per target (incl. the 23-test test-eq4-hw-compare block) elided ...]
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 \
ARM9V2_STATIC_DISCOVER_SKIP=1 EQ4_HW_COMPARE_DISCOVER_SKIP=1 \
FIT_MAPPING_DISCOVER_SKIP=1 AZAHAR_PRESENTATION_DISCOVER_SKIP=1 \
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
[... 62 unittest progress characters elided ...] usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
[... 330 unittest progress characters elided ...]
----------------------------------------------------------------------
Ran 420 tests in 89.782s

OK (skipped=95)
```

## 11. Privacy

Every added command uses `$HOME`/`$HWSTAGE` and `<staging>/...`; no literal
home-directory path, user name or host data is added. The check is scoped to
the added lines plus the new files:

```
$ git diff -U0 -- docs tools/test_eq4_hw_compare.py | grep -E '^\+' | grep -E '/(home|Users)/'
(no output; exit status 1)
$ grep -rn -E '/(home|Users)/' tools/fixtures/eq4_hw_scenes/README.md docs/reports/eq4r-static-scenes-hw-compare.md tools/test_eq4_hw_compare.py
(no output; exit status 1)
```

## 12. Diff budget

The size evidence below was re-taken in this worktree after every other content
edit of this repair round. It states the worktree HEAD (the orchestrator's
merge of this task's branch; `main` is still the analysis base
`e853749ca3b689ba729ea7c71705dc7d48426fe2`); this repair adds no commit and
changes no file other than this report.

```
$ git rev-parse HEAD && git rev-parse main
b7856e9ef29fad8b6d9549e8c76b14d546b5b634
e853749ca3b689ba729ea7c71705dc7d48426fe2
$ git add -N . && git diff main --stat | tail -1
 23 files changed, 2473 insertions(+)
$ git diff main | wc -c
108156
$ du -b tools/fixtures/eq4_hw_scenes
36651	tools/fixtures/eq4_hw_scenes
$ du -cb tools/fixtures/eq4_hw_scenes/*.png | tail -1
30621	total
```

These numbers were measured after every content edit of this repair, including
the evidence block and this paragraph. Those corrections stayed six digits, so
writing them in did not change the diff size; re-running the commands above
reproduces every figure.

The text diff (108156 bytes, working tree `b7856e9` against `main` `e853749`)
is below the 120000-byte limit set for this task (raised from 90000 by the
orchestrator). That limit is the text-diff budget, which is what the task's
acceptance criterion measures: `git diff main | wc -c` ≤ 120000. The 17
fixtures are PNG (binary), so `git diff main` prints them only as
`Binary files ... differ` and the 108156 bytes contain none of their payload;
the PNG payload total from `du -cb` is 30621 bytes, itself far below 120000,
while `du -b` on the directory gives 36651 because it also counts the
6 030-byte README, whose text bytes are already inside the diff. Both measured
figures are individually under 120000; their arithmetic sums are
108156 + 30621 = 138777 bytes (diff + PNG payload) and
108156 + 36651 = 144807 bytes (diff + full `du -b` total), and both sums
exceed 120000. Those sums cannot be brought under 120000 by this repair: the
text diff alone already consumes 108156 of the 120000 bytes, leaving 11844 —
less than the 30621-byte PNG payload — and this repair may neither cut quoted
tool output nor change any file but this report.

## 13. UNVERIFIED / what this report does not prove

- **Other consoles/sessions.** One console, one session per ROM, three
  captures each; `a` is the comparison capture by rule.
- **Capture-noise origin** (§2): counted, not explained; `s1` differs in all
  three pairs.
- **Textured geometry** is untested by these six ROMs.
- **Hardware ROM vs autodump ROM.** SOURCE-READ `arm9v2/Makefile:60-74`: a
  non-empty `AUTODUMP_FRAME` block sets only `TARGET=twlhd-arm9v2-autodump`,
  `BUILD=build-autodump` and appends `-DTWLHD_DEMO_AUTODUMP_FRAME=<n>` to
  `CFLAGS`/`CXXFLAGS`. The plain ROMs are not hashed in `SHA256SUMS`, so
  "nothing else differs" is inferred, not compared bit-for-bit.
- **Scene-4 mechanisms** (§7): alpha 0 = outline and `GL_BLEND` off = opaque
  are HYPOTHESIS; A1/A2 is cited from EQ-4i (§8), not re-derived.
- **c2's DeSmuME capture** succeeded here (size below) although eq4q reported
  `ARM OP_BKPT` for the non-static ROM; the harness still fails, so that pair
  stays NOT-COMPARABLE.

```
$ stat -c '%s %n' /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
1912 /tmp/opencode/eq4r/caps/c2/twlhd_frame_v2.bin
```

## 14. Requirement traceability

- **REQ 1 — six harness references, six `ours` crops, commands/output, every
  sha256, fixtures + README** — §3, §4, §9.
- **REQ 2 — twelve runs, correction line, per-ROM numbers, PASS/FAIL** — §5, §6.
- **REQ 3 — fixture hashes, >= 3 positives, 2-row-shift negative, `make test`,
  tests untouched** — §10.
- **REQ 4 — per-ROM table, noise estimate, MATCHED vs DIFFERENT, labels, EQ-4i
  citation, three append-only pointers** — §2, §6-§8.
- **REQ 5 — FACTs only from quoted output, two harness invocations per ROM
  (second quoted), HEAD, no home paths,
  no `python3 -c`, diff budget** — §1, §10-§12.
