# tools/fixtures/eq4_hw_scenes — six EQ-4q static-angle scenes on real hardware

**Test data, not code**, committed so `tools/test_eq4_hw_compare.py` can
replay the EQ-4r comparison without the console, the external GPL-2.0-or-later
harness or an Azahar run; the numbers they reproduce are quoted in
`docs/reports/eq4r-static-scenes-hw-compare.md`.

Basis: **one console, one capture session per ROM**. HARDWARE = one 400x240
top-screen crop per ROM; the harness side (EMULATOR, DeSmuME) = the raw 3D
framebuffer at harness frame 242 (= ROM frame 240); `ours` (EMULATOR,
Azahar/PICA) = the 400x240 crop of frame 199 of `arm11/twlhd-arm11.3dsx`
replaying the DeSmuME `twlhd_frame_v2.bin` of the same ROM. No harness,
DeSmuME or Azahar source is present and no ROM is committed; the tooling stays
Apache-2.0, like `tools/fixtures/eq_scenes/README.md`. Scenes: `cube` =
baseline cube, `s1` = SCENE=1 triangle list, `s2` = SCENE=2 triangle strip,
`s3` = SCENE=3 quad strip, `s4` = SCENE=4 alpha quads (31/16/4/0), `c2` =
CUBES=2; all `STATIC_ANGLE=30`.

## Files and SHA-256

| File | SHA-256 | Bytes |
|---|---|---|
| `cube_a_top400x240.png` | `0f9e7aee19cec41fc00909e894f053717a3cca4088aa18efc5789527d2f6e9c0` | 5702 |
| `s1_a_top400x240.png` | `8af538df08a00169f9f50785974f8d20388e969fda525151be468db65cfee75a` | 2074 |
| `s2_a_top400x240.png` | `c01dc0ea69eb2a715af29743ac7e7d48407354bb4a56fff1d3f1a85287f338b9` | 1601 |
| `s3_a_top400x240.png` | `bb83ba0fef9514d896e659f21f5ad57edf507e92e6e55e8768f0ab71202d9399` | 1589 |
| `s4_a_top400x240.png` | `ecb998b2032ac77ab1be5ed298aeb51b2e4b63e8630cf27731ebcfa09fcc5ed9` | 3399 |
| `c2_a_top400x240.png` | `488a184d8c916fe34824b34adf1789aa01bbf467f012534fd9cdb23509b58de6` | 1524 |
| `harness_cube_st30_f240.png` | `373701e21dde8593a2cffe10e3afe4e6c622a853adc8bb9ed8b823da2b8f6d74` | 1273 |
| `harness_s1_st30_f240.png` | `d402948dafcff446c0bb53edbec8d6dfe156255a77b03d310ef21d38303edc25` | 1023 |
| `harness_s2_st30_f240.png` | `dfc44116d0e354b60a0451bd48ba3f78671def7034ae1a5b1cbe7d44e4cfe965` | 984 |
| `harness_s3_st30_f240.png` | `9cf18f061ebf3990939827af3815e91d9564de271f735aefc033064b28702b1c` | 972 |
| `harness_s4_st30_f240.png` | `6538dd758346eed149fe1f72ea3d7c8aa4f655a7b3dfed169a13bedb46e6b99b` | 667 |
| `harness_c2_st30_f240.png` | `28979f4ece54f165176dff586e1e0ef673a4f16fc8b5dc494caddae20043852b` | 409 |
| `ours_cube_st30_top400x240.png` | `c4a8b5d851c7b8b08adce25cc5617479194ad1c89e733a21fba2a9dbf693bcfe` | 1548 |
| `ours_s1_st30_top400x240.png` | `f2f806e308ea696c2f42a430889cb08aaf7d5b527365df23dfc070f5612f9a87` | 2011 |
| `ours_s2_st30_top400x240.png` | `cb73eed07345caae5712a7b79213f9d2ef334400b1ef8b17adb244fb522221fc` | 2021 |
| `ours_s3_st30_top400x240.png` | `915bdf7cc4f3e969455cc2e9b68508cdbe1b73944d2e5621a03c143f73a7ed9a` | 1999 |
| `ours_s4_st30_top400x240.png` | `23302ce8371c74ac52822d46d7637f0df06e18616f1698a287b1bb42ae5c0185` | 1183 |
| `ours_c2_st30_top400x240.png` | `b97df11cbae37fe9dc25edaffc22219508a99e607a97ceb4a0a14049aeb92162` | 1051 |

`harness_c2_st30_f240.png` is the EQ-4t c2 reference, produced from the **plain
static** ROM `twlhd-arm9v2-c2-st30.nds` (the ROM uploaded to the console, hash
in eq4t §2), which passes the harness at frame 242 with 12 polygons / 48
vertices — the static **autodump** c2 ROM still exits 1 with `ARM OP_BKPT
triggered` (NOT-COMPARABLE), so that pair is never a pass; the plain-static
reference is the substitute harness pair. See
`docs/reports/eq4t-c2-reference.md`. The six hardware hashes equal the
`hw/hw_<scene>_a.png` lines of the run's `SHA256SUMS`.

## ROMs (EMULATOR inputs; not committed)

The run's `SHA256SUMS` lists the six **autodump** ROMs only
(`AUTODUMP_FRAME=240`; each also has its scene/`STATIC_ANGLE=30` variable). The
plain hardware ROMs (same names without `-autodump`) are in the same directory
but **absent from the hash file** — stated, not verified.

| Autodump ROM | ROM SHA-256 |
|---|---|
| `twlhd-arm9v2-autodump-st30.nds` (cube) | `73e409360c1e35858d1dc0d7893415ed09b0778a63c6adf4aa9e1bc70294e6fd` |
| `twlhd-arm9v2-autodump-s1-st30.nds` | `7115212ff92690930369722ceee52ccf1f67cea21a22272629976b0ae32fdb23` |
| `twlhd-arm9v2-autodump-s2-st30.nds` | `cc476561e9f60f780bc379ec1fde6af23bd5342baa1c1028690242a6e8f877a2` |
| `twlhd-arm9v2-autodump-s3-st30.nds` | `3a39663a404bec615cbf21a3905127b99fafd7eae887fe5845efee411d4094f7` |
| `twlhd-arm9v2-autodump-s4-st30.nds` | `0f63a52595c3774aa1b7400b4c112c3062c08a6dd788f28939f14bd962629ff7` |
| `twlhd-arm9v2-autodump-c2-st30.nds` | `82e913696c003b54525c092f4f2e7f483ec52b805cea58366f85f6054d41f3e4` |

## How each picture was produced

Capture side (OWNER-REPORTED): each plain ROM was started in DS mode from
TWiLight Menu++ and the passive cc3dsfs board took three top-screen frames per
ROM (`tools/cc3dsfs_capture/cc_shots.sh`-style, `--enabled_both 0
--enabled_top 1 --scaling_top 1`, decoder `tools/cc3dsfs_capture/xwd2png.py`),
each cropped to the top-left 400x240. Capture **`a`** is used (rule fixed
before any comparison number was read).

Harness reference (EMULATOR), harness frame 242 = ROM frame 240:

```sh
$HOME/3ds/twlhd-desmume-harness/harness \
    "$HWSTAGE/roms/twlhd-arm9v2-autodump-<scene>-st30.nds" 242 \
    --out harness_<scene>.txt --ppm harness_<scene>.ppm
```

`harness_<scene>_st30_f240.png` is that 256x192 PPM converted losslessly
(`PIL Image.open(ppm).convert("RGB").save(png)`). The cube's ROM name has no
`-cube-` infix: `twlhd-arm9v2-autodump-st30.nds`.

Ours (EMULATOR), the same ROM: DeSmuME v2 capture then the Azahar libretro host:

```sh
DESMUME_CLI=<cli> tools/emulator/desmume_capture.sh <rom> <capdir>
cp <capdir>/twlhd_frame_v2.bin <capdir>/sd/twlhd_frame.bin
tools/azahar_host/build/azahar_host \
    --core "$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so" \
    --game arm11/twlhd-arm11.3dsx --frames 200 --press A:60-63 --shot 199 \
    --out <capdir>/azahar --sd-root <capdir>/sd
```

`ours_<scene>_st30_top400x240.png` is the top-left 400x240 crop of
`azahar_199.ppm`. The build used is `arm11/twlhd-arm11.3dsx`, sha256
`15c26a18744048687395bbaca504c5ec67279a938b46d67b82a39990212c244f`, built from
worktree HEAD `e853749ca3b689ba729ea7c71705dc7d48426fe2`.

Rebuild with `make arm11` and `make azahar-host` (no ROM, PPM or emulator log
is committed). The tests convert the committed PNGs back to PPM in a temporary
directory, so the reported numbers need no PPM in the tree.
