# tools/fixtures/eq_scenes — committed EQ-2/EQ-3 scene fixtures

Every file here is **test data, not code**, committed so
`tools/test_eq_scenes.py` can exercise the driver without the external GPL
harness, DeSmuME or an Azahar run. All numbers are **EMULATOR** (DeSmuME
`fc504ee0` and host arithmetic). The real run is described in
`docs/EQ_SCENES.md`.

The harness dumps are verbatim program output of the out-of-tree GPL-2.0-or-later
harness `/home/one/3ds/twlhd-desmume-harness` (`format geometry-dump 1`,
harness frame 242 = ROM frame 240). The our-side files are the output of
`tests/host/build/gx_interp_cli --eq2` on the DeSmuME `twlhd_frame_v2.bin`
capture of the same frame. No harness or DeSmuME source is present in this
repository; the tooling remains Apache-2.0, the same posture as
`tools/fixtures/eq2/README.md`.

## Fixture pairs

| File | SHA-256 |
|---|---|
| `harness_s1_f240.txt` | `73b29e3bf6550ea00aa07493dd5b430d25ddf55401f8a9861ac04d1f868bfa23` |
| `ours_s1_f240.txt` | `fc8cee07120c0d965e919ec00648e0be9c8e1ad60fa84c6111b7067d7bc3b530` |
| `harness_s2_f240.txt` | `6233364cf17b8c386e9ae244a45633e79a0e7d29908d5c3f28162ab66a8b6316` |
| `harness_s4_f240.txt` | `428ae5e0ac8e654985f3bc3209cc739d049db204b1f617e98ad419c8c83d3d5f` |
| `ours_s4_f240.txt` | `19ded23d37ce3c8394a4e9683c71d4054903e7598c1b0d08836abe3742e99777` |
| `harness_s5_f240.txt` | `7d6b34d41134ed31bedb818bdc6de2d43bff0becb513ed6efe58cd30e918a6a0` |
| `ours_s5_f240.txt` | `4002a9f5ab9ff3c03e2def0ad86a74e8e0c0e670a0f0aadb01d56ddfd88318c3` |
| `harness_c2_f240.txt` | `5d0497d03c9729243286f36cb499185cb8dc74543d41574a2dc59845e1dba0d9` |
| `manifest.json` | `aec5869a6876e61215c0075c5da82a6de295085d4b0e1bb948ef81535df0683f` |
| `manifest_pass.json` | `1b89eb0c91b9a8eb60fcdc899d853bb5e1396738d2b4bc325233eae4637b7c57` |

Build variables (arm9v2/Makefile) and ROM SHA-256 of the run:

| Scene | Build variables | ROM | ROM SHA-256 |
|---|---|---|---|
| scene1 | `SCENE=1 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s1.nds` | `5bb335e2b72daf81dbe4dbd9e6156e0da4d80016c66b44e847ec84d09fc49f42` |
| scene2 | `SCENE=2 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s2.nds` | `d84f210b6f92c6a5c7a35c76dcfcaa0a142524ae578f974812cb6ae04a4097ed` |
| scene4 | `SCENE=4 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s4.nds` | `a2297c91e9a2065191a8cb3b189c66fcfc81114864ff41cde38c37b4d0e62cf9` |
| scene5 | `SCENE=5 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-s5.nds` | `35a50aadc30dbf26d7fa389192a908024cffaa515614c378e389e3efd45d82bd` |
| cubes | `CUBES=2 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-c2.nds` | `703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15` |

The cube row in both manifests reuses the existing real pair in
`tools/fixtures/eq2/` and the existing real EQ-3 picture pair in
`tools/fixtures/eq3/`; they are not duplicated here.

`scene3` has no fixture: its ROM never presents a 3D frame under DeSmuME
(harness exit 1), so no reference dump exists. `scene2` and `cubes` have only
the harness dump: the documented DeSmuME capture aborted with `ARM OP_BKPT`
before the autodump frame on every attempt (no recorder limit is reached and the
cause is unresolved; `docs/EQ_SCENES.md` section 3), so no
our-side stream exists.
The driver reports both cases as NOT-COMPARABLE, never as a pass.

Regenerate the harness dump with:

```sh
/home/one/3ds/twlhd-desmume-harness/harness <rom.nds> 242 --out harness.txt --ppm harness.ppm
```

Regenerate our-side with the capture flow of `docs/EQ_SCENES.md` and:

```sh
tests/host/build/gx_interp_cli --eq2 twlhd_frame_v2.bin > ours.txt
```
