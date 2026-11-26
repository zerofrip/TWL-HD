# tools/fixtures/eq4_hw_vp — one EQ-4zg partial-viewport frame, three arms

**Test data, not code**, committed so `tools/eq4_hw_vp/test_measure_vp.py` can
replay the EQ-4zg measurement without the console, the out-of-tree DeSmuME
harness (recorded in `docs/reports/eq4k-edge-rows.md` §3) or the out-of-tree
Azahar core (recorded in `docs/reports/eq4m-pica-edge-rows.md` §2.2). The
numbers they reproduce and every command that produced them are in
`docs/reports/eq4zg-partial-viewport.md`.

Basis: **one console, one capture session**. HARDWARE = one 400x240 top-screen
crop of the DS ROM `twlhd-arm9v2-vp-s4-st30.nds` (`SCENE=4`, `STATIC_ANGLE=30`,
viewport bytes `(32,96)-(159,191)`, y from the bottom); the harness side
(EMULATOR, DeSmuME) = the raw 256x192 3D framebuffer at harness frame 242
(= ROM frame 240) of the twin `twlhd-arm9v2-vp-autodump-s4-st30.nds`; `ours`
(EMULATOR, Azahar/PICA) = the top-left 400x240 crop of frame 199 of
`arm11/twlhd-arm11.3dsx` replaying that twin's DeSmuME `twlhd_frame_v2.bin`.
No harness, DeSmuME or Azahar source is present and no ROM is committed; the
tooling stays Apache-2.0, like `tools/fixtures/eq4_hw_scenes/README.md`.

## Files and SHA-256

| File | SHA-256 | Bytes |
|---|---|---|
| `hw_vp_s4_a_top400x240.png` | `8d108cea597d40a8ade862573b039c24892c33d9bd715990519c29ef960a6f33` | 1883 |
| `harness_vp_s4_st30_f240.png` | `1a21e797c34b4ed1b9920ba21bc5aeae23762fc92028e85deeedffe86ccfec3b` | 461 |
| `ours_vp_s4_st30_top400x240.png` | `52be0c6fa49f08e13fb3db4327d1f34b1383898a58c75f903d69711b888dd188` | 583 |

The hardware PNG is byte-identical (`cmp`) to `<staging>/hw/hw_vp_s4_a.png`
and carries the run's `a` capture (the a-priori rule "first capture"). The
run staged only `a` and `b`; the third capture was reported byte-identical to
`a` but is not present in `<staging>`, so that equality is not re-checked here.

## ROMs (EMULATOR inputs; not committed)

| ROM | SHA-256 |
|---|---|
| `twlhd-arm9v2-vp-s4-st30.nds` (hardware) | `439e121711140d73054de34ba776a19e67e982db2e5867f06ddf1d2c9c9c553b` |
| `twlhd-arm9v2-vp-autodump-s4-st30.nds` (harness + v2 capture) | `51a18d0d5c037078a304c1f63048bae2de496b2de4bdd80269c89996fbc3e057` |

## How each picture was produced

Capture side (OWNER-REPORTED, console work not done by this tooling): the
plain ROM was started in DS mode from TWiLight Menu++ and the passive cc3dsfs
board recorded the top screen three times (`tools/cc3dsfs_capture/cc_shots.sh`
-style), each cropped to the top-left 400x240.

Harness reference (EMULATOR), frame 242 = ROM frame 240:

```sh
harness "$HWSTAGE/roms/twlhd-arm9v2-vp-autodump-s4-st30.nds" 242 \
    --out harness_vp.txt --ppm harness_vp.ppm
```

`harness_vp_s4_st30_f240.png` is that 256x192 PPM converted losslessly
(`PIL Image.open(ppm).convert("RGB").save(png)`); the PPM SHA-256 is
`0c81d42386fb5835c3c7f175312e97f20b656858250fc1cdc85aec03d319de99`.

Ours (EMULATOR), the same twin ROM: DeSmuME v2 capture then the Azahar libretro
host (`--azahar-correction on` is applied at comparison time, not baked into
the fixture):

```sh
DESMUME_CLI=<cli> tools/emulator/desmume_capture.sh <rom> <capdir>
cp <capdir>/twlhd_frame_v2.bin <capdir>/sd/twlhd_frame.bin
tools/azahar_host/build/azahar_host \
    --core azahar_libretro.so --game arm11/twlhd-arm11.3dsx \
    --frames 200 --press A:60-63 --shot 199 \
    --out <capdir>/azahar --sd-root <capdir>/sd
```

`ours_vp_s4_st30_top400x240.png` is the top-left 400x240 crop of
`azahar_199.ppm` (PPM SHA-256
`0bbd7826204021d4c7b48a25941446ac54121b4f0af2d6c0407c8778e65057a8`;
crop pixel SHA-256
`3a6337f0d669791563137cdb0e0c5902a1f3df744e543d84478f2288dc63a308`).
The captured frame `twlhd_frame_v2.bin` is
`f917aa858459e376b4aa1eb4455d751cd9bad95fa96ba6b98029e84ecbd7bfb3`.
The build used is `arm11/twlhd-arm11.3dsx`, sha256
`d9fdb54c275bfb64dfea2d3ff45a9b70cc1cab0b5437f33e46346ce180db1dee`, built
from worktree HEAD `0824c3ade0af281f9972b7aeb65ba2bbb15a8278` with a clean
`arm11/ common/ arm7/ arm9v2/` status.

Rebuild with `make -C arm11` and `make azahar-host` (no ROM, PPM or emulator
log is committed). The tests read the committed PNGs directly.
