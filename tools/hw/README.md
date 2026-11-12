# Phase 5B-1 lighting test tooling (`tools/hw`)

HOST-ONLY, stdlib-only tooling for the owner-approved on-console check of
Phase 5B-1 CPU lighting (`common/twlhd_gx_interp.c`, `gx_interp_cli`).
Nothing in this directory opens a socket or contacts a console; the tests run
offline.

Labels: **BUILD/UNIT** for the tools and tests. **EMULATOR** for the numbers
recorded at the end of this file (headless Azahar, local files only). No
HARDWARE run has been made from this directory.

## Contents

| File | Purpose |
|---|---|
| `gen_lit_frames.py` | write the three deterministic v2 frames |
| `predict_lit.py` | run `gx_interp_cli`, emit per-quad RGBA + top-screen centres as JSON |
| `compare_ppm.py` | compare a 400x240 top-screen P6 PPM with that JSON |
| `../../docs/reports/hw-lit-tooling-mutation-report.md` | mutation evidence |
| `../../tools/test_hw_lit_*.py` | unittest coverage (discovered by `-s tools`) |

## Generate the frames

```sh
python3 tools/hw/gen_lit_frames.py /tmp/hw-lit/frames
```

Writes exactly:

- `twlhd_frame_v2_lit_a.bin` — 2x2 quads, one white directional light,
  normals at 0 deg / 60 deg / 90 deg (grazing) / facing away. Diffuse
  (22,22,22), ambient (4,4,4), no specular, no emission. The 90 deg and
  facing-away quads are both ambient-only by the DeSmuME equation
  (`fixed_diffuse = max(0, -dot(L,N))`), so they render the same colour.
  This is expected and is asserted by the tests.
- `twlhd_frame_v2_lit_b.bin` — red diffuse+ambient, emission only, specular
  through a 128-entry shininess table, and a red+blue two-light quad.
  Predicted colours: (197,0,0), (33,230,33), (239,239,239), (173,0,148).
- `twlhd_frame_v2_lit_ctl.bin` — same geometry with flat `COLOR` and no
  lighting commands: (255,0,0), (0,255,0), (0,0,255), (255,255,0).

The generator is deterministic (fixed command lists, no randomness); writing
twice gives byte-identical files. All three pass `twlhd_v2_dump.parse` and the
C parser (`tests/host/v2_dump_cli.c`).

## Predict the on-screen result

```sh
python3 tools/hw/predict_lit.py \
    --cli tests/host/build/gx_interp_cli \
    --out /tmp/hw-lit/lit_a.json \
    /tmp/hw-lit/frames/twlhd_frame_v2_lit_a.bin
```

`gx_interp_cli` is built by `make test` (Makefile:481-482). Without `--out`
the JSON is printed to stdout. The document contains the frame path, the
effective viewport, the clear colour `(16,16,24,255)`, a background patch
centre, and per quad: `rgba8`, `clip_centre`, `ndc_centre`, `pixel_centre`.
Colour 8-bit values are `round(channel/31*255)`; alpha 31 becomes 255.

## Compare a capture

```sh
python3 tools/hw/compare_ppm.py /tmp/hw-lit/lit_a.ppm /tmp/hw-lit/lit_a.json
```

Takes a **top-screen** P6 PPM (400x240; the format `tools/ntr_capture` writes),
samples a 9x9 patch mean at each predicted quad centre and at the background
centre, prints measured vs expected and per-channel deviation, and exits 0
only if every channel is within `--tol` (default 12 levels). Exit 1 means a
patch is out of tolerance; exit 2 means a malformed/short PPM, bad JSON, or a
patch that does not fit inside the image.

NTR captures come from a JPEG stream and carry about +-2 levels (Run 17), so
keep the default 12-level tolerance; do not tighten it below that.

An Azahar PPM is 400x480 (both screens stacked, top screen first): crop the
first 240 rows before calling `compare_ppm.py`.

## Commands the hardware procedure will use

Run these after the supervisor's procedure document has been approved; this
file does not define the run itself.

```sh
# 1. Build and generate (host)
make test                                     # builds gx_interp_cli
python3 tools/hw/gen_lit_frames.py /tmp/hw-lit/frames

# 2. Stage ONE frame at a time on the SD root, then launch the 5B-1 build and
#    press A to load sdmc:/twlhd_frame.bin (arm11/source/main.c:32).
cp /tmp/hw-lit/frames/twlhd_frame_v2_lit_a.bin <SD-root>/twlhd_frame.bin

# 3. Capture the top screen with the NTR-HR path (400x240 PPM)
tools/ntr_capture/build/ntr_capture ... --screen top ...   # see its README

# 4. Predict and compare
python3 tools/hw/predict_lit.py --cli tests/host/build/gx_interp_cli \
    --out /tmp/hw-lit/lit_a.json \
    /tmp/hw-lit/frames/twlhd_frame_v2_lit_a.bin
python3 tools/hw/compare_ppm.py /tmp/hw-lit/lit_a.ppm /tmp/hw-lit/lit_a.json
# repeat for lit_b and lit_ctl
```

The build must be one that includes 5B-1 lighting (`NORMAL` computes the lit
colour). Old builds silently ignore `NORMAL`/lighting state.

## Derived pixel mapping

Sources (file:line is the evidence):

| Step | Source |
|---|---|
| interpreter emits DS clip space | `common/twlhd_gx_interp.c:546-562` |
| clipfix matrix rows | `common/twlhd_clipfix.c:12-43` (used at `arm11/source/renderer.c:849-850`) |
| shader applies it (clip path only) | `arm11/shaders/twlhd_clip_vshader.v.pica:22-28` |
| render target is 240x400 | `arm11/source/renderer.c:671-672` |
| clear colour `0x101018FF` = (16,16,24,255) | `arm11/source/renderer.c:38` |
| portrait framebuffer rotated -90 deg for the 400x240 top screen | `tools/ntr_capture/README.md:151-155` |

For a DS clip coordinate `(x, y)` (default full DS viewport, `hw=hh=1`,
`cx=cy=0`):

```
x' =  hh*y + cy
y' = -0.8*(hw*x + cx)
fb_x = 120*(x' + 1)          # render target width 240
fb_y = 200*(1 - y')          # render target height 400
sx = fb_y                    # top-screen PPM is landscape 400x240
sy = 239 - fb_x
```

Collapsing the default viewport gives `sx = 200 + 160*x`,
`sy = 119 - 120*y`: the 256-wide DS picture occupies the centred 320-pixel
region (40-pixel bars), with the DS top edge at the screen top and no mirror.
A non-default viewport uses `hw=width/256`, `hh=height/192`,
`cx=(2*x1+width)/256-1`, `cy=(2*y1+height)/192-1` (with the DS `y1 > 191`
wrap to `y1-255`), exactly as `common/twlhd_clipfix.c`.

### Verification status: EMULATOR-VERIFIED

1. **Run 7/8 documented extents (offline, in-repo fixture).** Predicting
   `tests/host/fixtures/demo_frame_desmume_v2.bin` puts the six judged quad
   centres at average `sx = 200.2`, `sy = 118.8` (horizontally centred,
   slightly above the middle), with red upper-left, yellow upper-right and
   cyan lower — the Run 7 arrangement (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:
   285-287, 320-321`). Every centre stays inside the centred 320-pixel band.
   The Run 8 non-default viewport (32,96)-(159,191) predicted left/top edges
   123/25 land next to the documented 124/22; its right/bottom cannot be
   compared exactly because that capture is not in the repository.
2. **Run 17 clear colour.** The predictor emits (16,16,24), matching Run 17
   (`docs/HARDWARE_TEST_PROCEDURE_3DS.md:775-787`) and `RENDER_CLEAR_COLOR`.
3. **Azahar (2026-11-12, headless, host-only, external core
   `/home/one/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`).**
   - `tools/azahar_host/smoke.sh` rendered the staged `scene4_alpha` frame.
     Sampling the top 400x240 rows at the predicted centres gave
     `(255,0,0)`, `(7,142,11)`, `(13,13,60)`, `(16,16,24)` for the
     alpha 31/16/4/0 quads; those match the standard alpha-blend predictions
     and the Run 17 values within 1 level.
   - After building the current ARM11 app (`make arm11`), each generated
     frame was loaded and screenshotted. `compare_ppm.py` reported
     `result=PASS` with **0/0/0 deviation on every quad and the background**
     for `lit_a`, `lit_b` and `lit_ctl`.
   This is emulator evidence only; the console is still HARDWARE UNVERIFIED.
