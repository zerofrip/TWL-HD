# Headless Azahar libretro host

<!-- SPDX-License-Identifier: Apache-2.0 -->

A small, opt-in host for the external Azahar libretro core. It loads a `.3dsx`
in the core, runs a fixed number of frames with scripted button presses, and
writes PPM screenshots. It exists to reproduce the ARM11 demo deterministically
on a PC without touching the console.

This is **BUILD/EMULATOR tooling only**. It is not part of `make all` or
`make test`, it does not talk to the network or the console, and nothing from
Azahar is vendored, linked or compiled into this repository. The core is
`dlopen()`ed at run time from a path you provide.

## Build

```sh
make azahar-host
```

This produces `tools/azahar_host/build/azahar_host`. The default build needs
only a C compiler, libc and `-ldl`; it uses the minimal, hand-written
`azahar_host_libretro_min.h` mirror of the libretro ABI.

To compile against the real, external `libretro.h` instead (preferred when you
have the Azahar tree):

```sh
make azahar-host LIBRETRO_INCLUDE=$HOME/3ds/azahar/externals/libretro-common/libretro-common/include
```

Nothing under `tools/azahar_host/build/` is tracked; `make clean` removes it.

## Run

```sh
AZAHAR_LIBRETRO=/path/to/azahar_libretro.so \
  tools/azahar_host/build/azahar_host \
    --game /path/to/demo.3dsx \
    --frames 200 \
    --press A:60-63 \
    --shot 199 \
    --out /tmp/shot
```

Options:

| Option | Meaning |
|---|---|
| `--core <so>` | Azahar libretro core; falls back to `AZAHAR_LIBRETRO`. |
| `--game <file>` | Content to load (required). |
| `--frames <n>` | Number of frames to run, numbered from 0 (required). |
| `--press NAME:a-b` | Hold a button on frames `a..b` inclusive. Repeatable. |
| `--shot f1,f2,...` | Write `<out>_<frame>.ppm` for those frames. |
| `--out <prefix>` | PPM prefix, default `azahar_shot`. |
| `--sd-root <dir>` | Recursively copy `<dir>` to `sdmc:/` in the isolated temp root. |
| `--sys-dir <dir>` | Use `<dir>` as the libretro system directory (3DS NAND/BannerData). Default: auto-created empty temp dir. |

Button names: `A`, `B`, `X`, `Y`, `L`, `R`, `START`, `SELECT`, `UP`, `DOWN`,
`LEFT`, `RIGHT`. A press spec is exactly the Tanuki `CTREMU_KEYS` semantics
(`60-63:A` there, `A:60-63` here), both ends inclusive.

The final frame is always written as `<out>_last.ppm` in addition to any
`--shot` files. Images are P6 RGB24, top-down, `255` maximum.

Exit codes: `2` for usage/parse errors, `1` for runtime failures (missing
files, `dlopen`/`dlsym` failure, `retro_load_game` failure, no frame from the
first `retro_run`, unwritable PPM). Each failure prints one clear line.

## Isolated environment and the SD mapping

Before `dlopen()` the tool creates a fresh temporary root (printed as
`isolated environment: ...`) and sets:

- `HOME=<root>/home`
- `XDG_DATA_HOME=<root>/data`
- `XDG_CONFIG_HOME=<root>/config`
- `XDG_CACHE_HOME=<root>/cache`

It also hands the core `<root>/save` as the libretro save directory and
`<root>/system` as the system directory. This happens **before** `dlopen()` /
`retro_init()`, so the core cannot read or write a real user profile.

The exact `sdmc:/` mapping, verified in
`src/citra_libretro/core_settings.cpp` (`ParseStorageOptions`) and
`src/common/file_util.cpp` (`SetUserPath`):

1. The host answers `citra_use_libretro_save_path=LibRetro Default` and
   `GET_SAVE_DIRECTORY` with `<root>/save`.
2. `ParseStorageOptions` appends `Azahar/`, creates it, and calls
   `FileUtil::SetUserPath("<root>/save/Azahar/")`.
3. `SetUserPath` sets `UserPath::SDMCDir` to `<user dir>/sdmc/`, i.e.
   **`<root>/save/Azahar/sdmc/`**.

`--sd-root` is recursively copied to `<root>/save/Azahar/sdmc/` before
`retro_load_game()`. The caller's directory is only read, never modified.
`use_virtual_sd` is answered `enabled`, so `sdmc:/foo.bin` resolves there.

## Fixed core option answers

Every answered option is logged to stderr as
`[azahar_host] core option <key>=<value>`; the run log also contains
`core option values accepted`. Unknown keys return "not set", so the core uses
its own defaults for them.

| Key | Value | Why |
|---|---|---|
| `citra_graphics_api` | `Software` | No GL/Vulkan context, deterministic output. |
| `citra_use_cpu_jit` | `disabled` | Removes JIT/thread nondeterminism. |
| `citra_layout_option` | `default` | Both screens, stacked (400x480 at 1x). |
| `citra_swap_screen` | `Top` | Fixed orientation. |
| `citra_use_virtual_sd` | `enabled` | `sdmc:/` maps into the temp root. |
| `citra_resolution_factor` | `1` | Native 400x240 per screen. |
| `citra_region_value` | `Auto` | No region prompt. |
| `citra_language_value` | `English` | Fixed system language. |
| `citra_use_libretro_save_path` | `LibRetro Default` | Use the save dir above. |
| `citra_use_disk_shader_cache` | `disabled` | No shader-cache side effects. |

The core reports `av_info` geometry `400x480 @ 60 fps` (top screen above the
bottom screen), and `SET_GEOMETRY` repeats that. `SET_PIXEL_FORMAT` is accepted
for XRGB8888 only. `SET_HW_RENDER` is explicitly refused.

## Smoke test

```sh
AZAHAR_LIBRETRO=/path/to/azahar_libretro.so tools/azahar_host/smoke.sh [outdir]
```

The script is opt-in and is **not** run by `make test`. It exits 2 with
`AZAHAR_LIBRETRO not set` when the variable is unset. Otherwise it runs the
staging demo `$HOME/3ds/hwtest-staging/twlhd-arm11-blend.3dsx` for 200
frames with `--press A:60-63`, staging
`frames/twlhd_frame_v2_scene4_alpha.bin` as `sdmc:/twlhd_frame.bin` in a temp
SD root under `<outdir>/sd`. It then checks the PPM exists, that its pixel
payload matches the dimensions in its own P6 header (no hard-coded
resolution), and that it contains at least two distinct RGB values. `python3`
is required and checked, like the DeSmuME capture script.

## Licence posture

The repository side (this directory and the host test) is Apache-2.0, matching
the rest of the tree. Azahar is GPL-2.0-or-later (individual files may be
GPL-3.0-or-later). The core remains **external and unvendored**: it is not
compiled, linked or distributed here, only loaded with `dlopen()` at run time.
No Azahar source, header or object is copied into this repository; the minimal
libretro mirror in `azahar_host_libretro_min.h` is transcribed from the public
libretro ABI, not copied from Azahar.

## Limitations

- **Software renderer only.** No OpenGL/Vulkan path is provided; hardware
  renderer requests are refused.
- **No audio.** The sample and batch callbacks are no-ops; audio state is
  ignored.
- **No GPU hang modelling.** The emulated GPU cannot hang the way real
  hardware can; a clean run here says nothing about hardware safety.
- **Not a predictor of the real GPU.** In Run 14 the emulator (Tanuki3DS)
  alpha-blended the scene-4 frame while the real console drew the alpha 16/4
  quads opaque and omitted the alpha 0 quad. Run 15 set the blend state
  explicitly and the console still did not blend. Emulator output and hardware
  output are separate evidence.
- The temp root (including the staged SD copy and the core's user dir) is left
  behind for inspection; the path is printed at start-up.
- Both screens are one 400x480 framebuffer in the `default` layout. Changing
  the layout option changes that geometry; this tool fixes it to `default`.

## Mutation evidence

The host unit test (`tests/host/build/test_azahar_host_ppm`, run by
`make test`) covers all three helpers with literal expectations. Two mutants
per helper were applied to a copy of `azahar_host_util.h`; the unchanged test
source was compiled against each mutant with the same
`-Wall -Wextra -Werror` + ASan/UBSan flags.

Baseline (unmutated header): `8 test(s) passed`, exit 0.

| Helper | Mutant | Before | After (verbatim first failing check) |
|---|---|---|---|
| `xrgb8888_to_rgb24` | (a) ignore pitch, assume `4*width` | PASS | exit 1: `test.c:56: expected 0x30 == 48, got 238` |
| `xrgb8888_to_rgb24` | (b) swap B and R channels | PASS | exit 1: `test.c:32: expected (values[k] >> 16) & 0xffu == 17, got 51` |
| `parse_button_spec` | (c) treat `to` as exclusive | PASS | exit 1: `test.c:107: expected 63 == 63, got 62` |
| `parse_button_spec` | (d) wrong button table entry (`A` -> `B`) | PASS | exit 1: `test.c:105: expected 8 == 8, got 0` |
| `parse_frame_list` | (e) accept a trailing comma | PASS | exit 1: `test.c:168: expected -1 == -1, got 1` |
| `parse_frame_list` | (f) capacity off-by-one (reject a full list) | PASS | exit 1: `test.c:177: expected 2 == 2, got -1` |

## Reference run (EMULATOR)

Machine-local run, 2026-11-07, with the external core
`$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`
(`Azahar 9c38437`) and the staging inputs listed above.

- `make azahar-host`: clean build, no warnings.
- `ldd tools/azahar_host/build/azahar_host`: only `libc.so.6` (plus the
  loader); no Azahar, GL or X11 libraries.
- Core options answered: the ten keys in the table above; log line
  `core option values accepted (10 lookups answered)`.
- `av_info geometry 400x480 @ 60.000 fps`; every `software framebuffer` line
  is `400x480`.
- Smoke PPM: `shot_199.ppm`, 400x480, 576015 bytes (15-byte P6 header +
  576000 pixel bytes), 6 distinct RGB values. The final frame is also written
  as `shot_last.ppm`, byte-identical.
- Determinism: two consecutive 200-frame smoke runs produced the same
  sha256 for both PPMs:
  `06d5eb226afbc3b1cd3e6c3e3016baaa8d21c18d75e9f43ca70c112b563db07a`.
- `make test`: green, 86 Python tests, all host tests including
  `test_azahar_host_ppm` (8 groups).

The six distinct colours include solid red (the alpha-31 quad), a blended
green (alpha 16), a faint blue (alpha 4) and no white quad (alpha 0), matching
the emulator behaviour described in Run 14 — and, per Run 15, **not** the
console behaviour.
