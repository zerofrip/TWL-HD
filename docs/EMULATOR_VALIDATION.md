<!-- SPDX-License-Identifier: Apache-2.0 -->
# Emulator validation of the Phase 4D v2 capture

How to reproduce the headless DeSmuME validation of the `arm9v2` v2 capture demo, what it proves, and what it does not.
DeSmuME is an **external, opt-in tool**: it is not vendored, not linked, and `make all` / `make test` never need it.

## Labels

- **EMULATOR VERIFIED** — the whole chain below ran on an emulator and the result matched. It is **not** HARDWARE VERIFIED.
- The chain: real ARM9 demo code (autodump build) → `arm9v2/include/twlhd_gx_shim.hpp` register proxy → `common/twlhd_gx_record.c` →
  `common/twlhd_v2_write.c` → libfat on the emulated CompactFlash → FAT image on the host → C parser (`tests/host/v2_dump_cli.c`) and
  Python parser (`tools/twlhd_v2_dump.py`) → comparison with the host expected stream (`tools/emulator/compare_capture.py`).
- A PASS shows that the capture pipeline and both parsers agree on real ARM9 code running on an emulated ARM9. It says nothing about real
  GX FIFO timing, polygon/vertex RAM pressure, VBlank behaviour or libfat on a real SD card.

## Result of the reference run (EMULATOR, 1 run on this machine, repeated: identical capture)

| Item | Value |
|---|---|
| DeSmuME | a local build outside this repository, `git describe`: `release_0_9_13-429-gfc504ee0` |
| Command | `DESMUME_CLI=<path> make emu-validate` → exit 0 |
| Capture | `twlhd_frame_v2.bin`, 1152 bytes, sha256 `acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5` (identical on a second run) |
| Parsers | C and Python: `OK (entries=124 rsta=4)` |
| Comparator | `PASS: 124 GXCM + 4 RSTA entries match for angle 61440` |

Observed numeric deviations of that capture from the analytic values (measured, not assumed):

| Matrix command | Max abs deviation | Tolerance used |
|---|---|---|
| 0x18 MTX_MULT_4x4 (perspective 70°, 256/192, 0.1, 100) | 0.00178 | 0.004 |
| 0x19 MTX_MULT_4x3 (identity + z = −4) | 0.0 | 0.001 |
| 0x1A MTX_MULT_3x3 (rotation X block, Y block at 61440) | 0.00017 | 0.002 |
The tolerances are **empirical margins** (perspective about 2.2× and rotation about 12× the observed deviation; the look-at matrix matched exactly; one
4.12 fixed-point quantum is 0.000244), not values derived from hardware. A swapped axis or flipped sign changes an element by 0.707 at this angle, so the check separates them by a wide margin.

### Measured effect of the SIGPIPE/SIGXFSZ launcher reset (EMULATOR)

Maintainer measurement on the machine above, one run per script version, with the capture script started by a
parent that ignores SIGINT and SIGQUIT (the `&`/`nohup`/supervisor environment):

| Script | `make emu-validate` | Observed |
|---|---|---|
| SIGINT/SIGQUIT reset only (before this round) | exit 2 | `DeSmuME did not exit on SIGINT within 30s; the FAT image may be unflushed/corrupt` |
| also resetting SIGPIPE/SIGXFSZ (current) | exit 0 | identical capture, sha256 `acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5`, `PASS: 124 GXCM + 4 RSTA` |

The old script's exit 2 is the measured historical result and is recorded verbatim, not reconciled with the current
watchdog path (which reports 1). This is EMULATOR evidence only; it is **not** HARDWARE VERIFIED.

## What the comparator checks (`tools/emulator/compare_capture.py`, stdlib only, deterministic)

1. v2 container parses; header flags are exactly FRAME_COMPLETE (no OVERFLOW); META backend = 1 (register proxy); META coverage = 0x7.
2. GXCM command-id sequence is identical entry for entry, and every non-math value is byte-identical.
3. Math commands are checked analytically, because the host mock cannot reproduce the DS divider/sqrt/trig tables:
   perspective closed form (libnds `glFrustumf32` write order); look-at identity + translation; rotation element-wise against the fixed
   libnds `glRotateXi` layout `[1,0,0, 0,c,s, 0,−s,c]` (first block) and `glRotateYi` layout `[c,0,−s, 0,1,0, s,0,c]` (second block), so
   axis, direction and sign are all distinguished, plus orthonormality and determinant as an extra net.
4. RSTA addr / gx_index / width and every value are identical, with one deliberate exception: libnds keeps the polygon id in the library global
   `glCurClearColor` (seeded by `glInit()` on the DS, zero on the host), so the **first** write to `0x04000350` may differ only in the polygon-id
   bits 24..29 (mask `0x3F000000`). Any other difference on that write, or any difference on a later `0x04000350` write, fails.

## Mutation / negative tests (run by `make test`; 82 Python tests in total; 2 of them need the devkitPro environment and are skipped without it)

`tools/test_emulator_compare.py` feeds the comparator wrong data and requires exit 1 with the right reason: value mismatch, command-sequence
mismatch, perspective/look-at/rotation wrong, negative rotation angle, rotation blocks swapped, wrong header flags / META backend / coverage,
entry-count mismatches, RSTA field/value mismatch, clear-colour handling (a polygon-id-only difference on the first write passes, a bit-30 difference
on the first write fails, a polygon-id-only difference on the second write fails; a separate test shows that a mutant comparator applying the exception to every
`0x04000350` write would wrongly ACCEPT that second-write data, which proves the data separates the two rules), truncated file, and tolerance pinning (perspective
+0.003 passes / +0.005 fails; look-at +0.0009 passes / +0.0011 fails). `tools/test_emulator_capture_script.py` drives
`desmume_capture.sh` with fake emulators: SIGINT success path, SIGINT-ignoring emulator escalating to SIGKILL with a "FAT image may be corrupt"
failure, early exit, the emulator's real exit status being reported after SIGINT, missing device log lines, invalid `DESMUME_TIMEOUT` and
`DESMUME_INT_GRACE` values (each tested on its own), unset or non-executable `DESMUME_CLI`, a `PATH` with the other
required tools but no `python3` (must fail with `required tool not found: python3`), a lightweight command-position scan (not a full shell parser) that finds no `timeout` command in the script and detects it in a set of
synthetic snippets, bash syntax. The same fake-emulator scenarios are also driven with the parent's SIGINT and SIGQUIT ignored (the `&`/`nohup`/
supervisor launch environment), and one test reads the fake emulator's `/proc/self/status` `SigIgn` mask in three launch
environments (no parent ignore, parent ignoring SIGINT+SIGQUIT, parent also ignoring SIGUSR1) to require SIGINT, SIGQUIT,
SIGPIPE and SIGXFSZ at their default disposition and, in the third case, that the unrelated SIGUSR1 ignore survives
(no over-reset). The fake-emulator test where the emulator exits with status 7 guards against a
hard-coded or sleeper-derived status; it does not reproduce the reap-order race in the grace phase. These show the validator rejects wrong data;
they do not replace the real run.

## Prerequisites and how to run

- A `desmume-cli` you built yourself (DeSmuME is GPL-2.0-or-later; keep it outside this repository). Set `DESMUME_CLI=/path/to/desmume-cli`.
- `mkfs.fat`, `7z`, `stdbuf`, bash ≥ 5.1 (`wait -n` with process ids), `python3`, a C compiler for the host tests, and the devkitPro/devkitARM environment
  for the ROM. Note: `make test` also runs `tools/test_emulator_capture_script.py`, whose fake-emulator tests need `mkfs.fat`, `7z` and `stdbuf`
  (they fail, not skip, when a tool is missing). `desmume_capture.sh` itself now requires `python3`: its `-I` launcher resets SIGINT, SIGQUIT,
  SIGPIPE and SIGXFSZ to SIG_DFL before exec'ing the emulator (python3 ignores SIGPIPE/SIGXFSZ at interpreter startup and exec preserves
  SIG_IGN), and the script fails explicitly when `python3` is missing (covered by a test). No real DeSmuME is needed for `make test`.
- `make emu-validate` builds `arm9v2` with `AUTODUMP_FRAME=240` into a **separate** ROM (`arm9v2/twlhd-arm9v2-autodump.nds`; the normal ROM is
  unchanged), builds the host expected stream (`make emu-expected`, angle 61440), runs `tools/emulator/desmume_capture.sh`, then
  `validate_with_parsers.sh`, then the comparator. It fails loudly if `DESMUME_CLI` is unset or not executable. It is never part of `all` or `test`.
- `AUTODUMP_FRAME` must be empty, `0`, or a positive decimal integer ≤ 65535; anything else is a build error.
- `VP_X1`/`VP_Y1`/`VP_X2`/`VP_Y2` are an opt-in viewport override for the same demo (**BUILD/UNIT only**; no EMULATOR or HARDWARE claim).
  All four must be set together as decimal integers with `0 ≤ x1 ≤ x2 ≤ 255` and `0 ≤ y1 ≤ y2 ≤ 191`; anything else is a build error, and
  leaving them unset keeps the normal ROM unchanged. Example:
  `make -C arm9v2 AUTODUMP_FRAME=120 VP_X1=32 VP_Y1=96 VP_X2=159 VP_Y2=191` builds
  `arm9v2/twlhd-arm9v2-vp-autodump.nds` in `build-vp-autodump/`. A viewport-only build is named `twlhd-arm9v2-vp.nds`; the recorded
  VIEWPORT word is `x1 | y1 << 8 | x2 << 16 | y2 << 24` (DS y is measured from the bottom), which `common/twlhd_clipfix.c` consumes
  as captured. `make test` also builds a variant-viewport host binary and checks the recorded VIEWPORT word against fixed literal
  words (default `0xBFFF0000u`; variant `0xBF9F6020u` passed via `-DTWLHD_TEST_EXPECT_VP_WORD`). When switching `AUTODUMP_FRAME`
  (or any other `-D` option) in the same working tree, the Makefile records the define set in `<BUILD>/flags.stamp` and removes stale
  objects automatically, so the old manual `rm -rf arm9v2/build-autodump` step is no longer needed.
- `CUBES=<n>` draws `n` demo cubes (1..170) in a 17-column grid for the same demo (**BUILD/UNIT only**; no EMULATOR or HARDWARE claim).
  The value must be a decimal integer in 1..170; anything else is a build error, and leaving it unset (or `1`) keeps the normal ROM
  unchanged. Example: `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64` builds `arm9v2/twlhd-arm9v2-autodump-c64.nds` in
  `build-autodump-c64/`. The limit of 170 is 6120 clip vertices (≤ the ARM11 `CLIP_VERTEX_CAPACITY` of 6144), fewer than 16384
  recorder entries, and below the 1 MiB v2 file size. `make test` also builds `-DTWLHD_DEMO_CUBES=170` and `=2` host binaries that
  check the recorded VTX_16 count and the interpreter's vertex output against fixed literals.
- `SCENE=<n>` replaces the cube with a flat primitive scene for the same demo (**BUILD/UNIT only**; no EMULATOR or HARDWARE claim).
  `SCENE=1` draws a triangle list (24 vertices / 8 triangles), `SCENE=2` a triangle strip (12 input vertices / 10 triangles),
  `SCENE=3` a quad strip (12 input vertices / 5 quads / 10 triangles) and `SCENE=4` four flat quads, one quad-list each, with a
  per-quad `POLYGON_ATTR` alpha of 31, 16, 4 and 0 (16 input vertices / 8 triangles; two translucent and one wireframe BEGIN group,
  alpha 31 counted as neither). The value must be empty, `0` or a decimal integer in 1..4; anything else is a build error, and
  leaving it unset (or `0`) keeps the normal ROM unchanged. The `<n>` suffix composes with the other options, e.g.
  `make -C arm9v2 AUTODUMP_FRAME=120 SCENE=2` builds `arm9v2/twlhd-arm9v2-autodump-s2.nds` in `build-autodump-s2/`; `SCENE=4`
  produces `twlhd-arm9v2-s4.nds` in `build-s4/`. `SCENE` > 0 together with `CUBES` > 1 is rejected by `demo_frame.cpp`
  (`#error`). Changing `SCENE` without `rm -rf` rebuilds cleanly because it is part of `<BUILD>/flags.stamp`, and `make clean`
  removes every `-s<N>` build directory and output file. `make test` also builds `-DTWLHD_DEMO_SCENE=1`, `=2`, `=3` and `=4` host
  binaries that check the recorded primitive type, VTX_16/COLOR/POLYGON_ATTR counts, the exact interpreted vertex-index order and
  frustum bounds at angles 0, 7680, 15360, 23040 and 30720, plus the per-vertex alpha literals for `SCENE=4`
  (1.0 / 0.53125 / 0.15625 / 0.0). The scene shapes draw at 2x scale, so the strip vertices reach |x| = 1.25.

## Emulator facts that shaped the harness

These are observations from earlier runs with the build above (measured on this machine); this repository's tests do not re-check them.

- `--cflash-image <file>` works (DLDI is auto-patched; the log must contain `Slot 2: MPCF Flash Card Device` and `AUTO-PATCHING DLDI`, which the
  script requires). `--cflash-path` was silently ignored.
- Headless operation: `SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy`, and `DISPLAY`/`WAYLAND_DISPLAY` removed for the process.
- The emulator must be stopped with SIGINT so it flushes the FAT image; a SIGKILLed run left a corrupt file entry. The script therefore has its own
  INT → grace → KILL watchdog and treats a run that needed SIGKILL as a hard failure. It does not use `timeout`, because on this machine
  `timeout` is uutils coreutils 0.10.0 and `-k` there prevented the `-s` signal from reaching the child.
- Background launchers (`&`, `nohup`, supervisors) start children with SIGINT/SIGQUIT ignored, and POSIX exec preserves SIG_IGN. bash cannot trap
  or reset a signal that was ignored on entry (`trap - INT QUIT` is a no-op there), so the emulator inherited the ignore and had to be SIGKILLed.
  python3 additionally ignores SIGPIPE and SIGXFSZ at interpreter startup, so a launcher that leaves them alone hands the emulator a changed
  output-pipe and file-size-limit behaviour (measured `SigIgn` bit for SIGXFSZ present, emulator hit `EFBIG` instead of dying at the limit).
  The script therefore exec's the emulator through a `python3 -I` wrapper that sets SIGINT, SIGQUIT, SIGPIPE and SIGXFSZ to SIG_DFL and then
  `execvp`s `stdbuf` → `desmume-cli`; every hop is an exec, so `$!` stays the emulator pid. (BUILD/UNIT evidence: launched with SIGINT ignored,
  the 4 fake-emulator SIGINT tests failed before this fix and pass after it, as do the added ignored-signal scenario and `SigIgn` disposition
  tests; the stricter disposition test also requires an inherited SIGUSR1 ignore to survive, and the mutant evidence is that dropping the
  SIGPIPE/SIGXFSZ resets fails it while resetting every signal fails the SIGUSR1 case.)
  Coverage caveat (measured by the independent review, EMULATOR): on this machine `stdbuf` is the uutils implementation, which itself resets SIGPIPE to the
  default before starting the emulator, so removing only the SIGPIPE reset is not caught here (SIGXFSZ is still caught). With GNU `stdbuf` the SIGPIPE reset
  is load-bearing; keep it. An earlier note that python3 "does not ignore SIGPIPE here" was wrong: it does, and `stdbuf` hides it.
- Input injection is not used: `--play-movie` is ignored by `desmume-cli` (the GTK/GTK2/Windows frontends do execute it), so the demo is
  built with an auto-dump frame instead of waiting for a key.

## Alpha blending of vertex alpha (EMULATOR, SCENE=4)

EMULATOR: Tanuki3DS, scene4 capture (SCENE=4, AUTODUMP_FRAME=10; DeSmuME capture 1128 B, sha256 prefix 2decd74dead1044f; four flat quads alpha 31/16/4/0 = interpreter vertex alpha 1.0 / 0.53125 / 0.15625 / 0.0, colours red/green/blue/white). OLD ARM11 build (b7b14cff…, interpreter alpha always 1.0) vs NEW ARM11 build (phase5b/alpha-cpu, NO ARM11 source change, only shared interpreter changed): 15220 of 1440015 screenshot bytes differ. Dominant colours old -> new: blue quad (0,0,240) 2088 px -> (0,0,48); green quad (0,240,0) -> (0,128,0); white quad (240,240,240) 1299 px -> gone (background). I.e. the existing renderer ALREADY alpha-blends vertex alpha with its default (citro3d) blend state; the exact blend state was never set explicitly by our code (arm11/source/renderer.c sets cull/depth/TexEnv explicitly, not blending). Frames whose alpha is 1.0 (every existing capture) are unchanged: gx_interp_cli vertex output is identical on main and on this branch for the Run 7-13 frames (only the stats line differs: ignored_no_effect one lower because POLYGON_ATTR is now processed). HARDWARE UNVERIFIED (needs an approved run). Depth/draw-order semantics of DS translucent polygons (DESMUME: depth write only if TranslucentDepthWrite bit, sort order) are NOT implemented, so overlapping translucent polygons may differ from DS.
HARDWARE (Run 17, numeric NTR capture, corrects Run 14): the real console also alpha-blends the alpha 16/4 quads, matching the standard blend within ~2 levels; the Run 14 "opaque" reading was a camera-photo misreading. Emulator and console agree. See `docs/HARDWARE_TEST_PROCEDURE_3DS.md` "Run 17".

## Limits

One fixed scenario (the demo's rotation angle 61440, one frame). Not covered: other angles, real GX timing, overflow behaviour under load, other
emulators, commercial software, and anything about the PICA200 renderer.

## Multi-cube captures (CUBES) in DeSmuME: measured behaviour (EMULATOR)

EMULATOR: DeSmuME 0.9.14, interpreter, JIT off by default. Measured by the supervisor (sequential runs, DESMUME_TIMEOUT 120, 60 for AF=10, clean rebuilds). The root cause of the failures below is NOT identified (unexplained); no cause is asserted here.

| n | AF=120 | AF=10 |
|---|---|---|
| 2 | 1912 B ok | not measured |
| 32 | 23272 B ok | not measured |
| 64 | no capture (ARM OP_BKPT) | 46056 B ok |
| 100 | no capture (undef instr) | 71688 B ok |
| 128 | 91624 B ok (11433 entries) | not measured |
| 129 | 92336 B ok (undef instr after write) | not measured |
| 169 | no capture (undef instr) | 120816 B ok |
| 170 | no capture (undef instr) | 121528 B ok |

AF=10 captures parse OK (entries: 100=8941, 169=15082, 170=15171; rsta=4 each); n=64 also OK at AF=60 (46056 B). tests/host/build/gx_interp_cli reports result=OK on the 100/169/170 captures. The recorder overflow limit (16384 entries) and ARM11 clip buffer (6144 output vertices) are not exceeded at n=170 (15171 entries; 6120 output vertices).

gdb (arm9gdb) on the n=64 AF=120 ROM (EMULATOR): the in-ROM v2 buffer at 0x0201c890 (46056 B, 5737 GXCM + 4 RSTA) dumped at the parse call and at the parse-error exit is byte-valid (`tools/twlhd_v2_dump.py validate`: result=OK, entries=5737, rsta=4 for both); entries 2957..2964 are well-formed VTX_16 pairs. Yet twlhd_v2_write's self-check call of twlhd_v2_parse returned ERR_GXCM_GROUP (17) at i=2959, j=2960, count=5737 (breakpoint at the group-failure return). Masking interrupts did not prevent it: CPSR I-bit set at dump entry, and separately IME=0, still returned 17, then control reached the reset vector (0x01FF853C). Adding more breakpoints inside the parse suppressed the fault. The bytes are valid but the ROM-side check fails only for this AUTODUMP_FRAME value; this is consistent with an emulator-side or timing-dependent cause but is not shown to be one. Not a finding about the ARM11 parser, which reads these files correctly on the 3DS (Runs 7-11) and on the host. Root cause UNEXPLAINED.

AF sweep (CUBES=64, EMULATOR): AF = 10, 20, 40, 60, 80, 100, 110, 119, 121, 140 each wrote 46056 B (parsers OK for 10 and 60; the rest not run through validate_with_parsers.sh); AF = 120 no capture (ROM fault). The failure is not "any AF >= some frame" and not a run-length effect: AF=119 and AF=121 pass, only AF=120 fails among those measured. Root cause UNEXPLAINED.

Host mock (tests/host dump_demo_frame, -DTWLHD_DEMO_CUBES=64 and 100, angles 2560, 30719, 30720, 30721) wrote 5737 (resp. 8941) GXCM + 4 RSTA entries with no error: the mock does not reproduce a malformed stream. It replaces libnds, so this does not rule out a device-side or emulator-side cause; the angle used by the ROM at frame 120 was not read (the list above is a guess, not evidence). Root cause UNEXPLAINED.

Follow-up (open): explain why the ROM-side parse fails only for AF=120 despite a byte-valid buffer (interrupt masking and extra breakpoints already tested); not a finding about the ARM11 parser.

Default builds (CUBES unset) are unaffected: make emu-validate PASS 124 GXCM + 4 RSTA at angle 61440. EMULATOR evidence only; not HARDWARE VERIFIED.

## 5B-2e-2 textured renderer path (Azahar libretro host)

**EMULATOR evidence only; not HARDWARE VERIFIED.** This is a PICA200 renderer
validation, separate from the DeSmuME ARM9 capture chain above. It uses the
external Azahar libretro core loaded at run time by
`tools/azahar_host/build/azahar_host`.

### Generator

`tools/gen_tex_frames.py` writes one or two deterministic v2 frames into an
output directory:

  twlhd_frame_v2_tex.bin     four quads with:
                               - 8x8 red/white checker
                               - 16x16 blue/white checker
                               - 16x8 green/white checker (non-square)
                               - 8x8 red/white checker reused from the first run
  twlhd_frame_v2_tex_ctl.bin same geometry, no texture state (untextured control)

Both frames end with SWAP_BUFFERS and set `FLAG_FRAME_COMPLETE`. The texture
frame uses DS 16bpp (format 7) TXDS/TXDT chunks and no palette, so the
`common/twlhd_pica_frame_prepare()` path decodes and swizzles them into RGBA8.

### Procedure

Build the host (needed once):

    make azahar-host

Generate the frames:

    python3 tools/gen_tex_frames.py /tmp/tex_frames

Run the textured frame. The ARM11 app looks for `sdmc:/twlhd_frame.bin`, so
copy the chosen frame under that name inside `--sd-root`:

    mkdir -p /tmp/tex_sd
    cp /tmp/tex_frames/twlhd_frame_v2_tex.bin /tmp/tex_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=/home/one/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/tex_shot --sd-root /tmp/tex_sd

Repeat with the control frame:

    cp /tmp/tex_frames/twlhd_frame_v2_tex_ctl.bin /tmp/tex_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=/home/one/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/tex_ctl_shot --sd-root /tmp/tex_sd

### Prediction and comparison

The host preparation path was verified separately: `twlhd_pica_frame_prepare()`
on the generated texture frame returns `OK` with 4 plan entries, 3 unique
textures and 1792 arena bytes; entry 3 shares texture index 0 with entry 0.

For the emulator comparison, sample the top screen (upper 400x240 of the
400x480 output) at the quad centres. The exact clip-space to screen mapping is
governed by `common/twlhd_clipfix.c` and the Azahar software renderer; the
prediction used here is therefore colour-in-region rather than per-texel:

| region (approx.) | textured frame | control frame | expected base colour |
|---|---|---|---|
| upper-left | green/white checker | solid green | green |
| upper-right | red/white checker | solid grey (reuse/control mismatch) | red (reuse) |
| lower-left | red/white checker | solid red | red |
| lower-right | blue/white checker | solid blue | blue |

The control frame uses solid red/blue/green/grey for the four quads; the
textured frame shows the same base colours in the same regions but with an
alternating checker pattern, which confirms that the texture data is being
sampled and MODULATEd with the white vertex colour. The checker alternation is
visible because the NEAREST filter preserves the 4x4-texel checker squares.

### Reference run (EMULATOR)

Machine-local run, 2026-11-13, with the external core
`/home/one/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`
(Azahar `9c38437`) and the commands above.

- `make azahar-host`: clean build, no warnings.
- Both runs produced 400x480 PPM screenshots at frame 199 and frame `last`.
- The textured screenshot shows red, green and blue checker patterns in the
  same screen regions where the control screenshot shows solid red, green and
  blue quads. No texture frame region shows the background clear colour
  `(16,16,24)` inside a quad boundary.
- Determinism: two consecutive textured runs produced identical sha256 for
  `tex_shot_199.ppm`:
  `cf036bb71ffc61bdde0efd5e072e1e7e44faf66583bcba8bd779565dd3ff5a18`.
- Control frame sha256: `tex_ctl_shot_199.ppm` =
  `10aaaaeeea48d6a601d17b1b0da8b1fed719f543b1f8ae0d3d802bb50e2f492e`.
- Top-screen colour diversity at the four quad centres: textured frame has
  exactly 2 distinct RGB values per region (checker pattern); control frame has
  exactly 1 distinct RGB value per region (solid quad).

Tolerance: because the software renderer's clip-space to screen mapping is not
verified down to the sub-pixel level, the comparison is qualitative
(colour-in-region and checker-vs-solid) rather than a per-texel byte match.
The tolerance is therefore stated as "per-quad colour and pattern correct";
any swapped, missing or solid-colour textured quad would be a failure.

### Limits

- Azahar's software renderer is not the real PICA200; it validates that the
  ARM11 code path produces coherent GPU commands, not that hardware will accept
  the texture layout or attribute permutation.
- The exact screen positions of the quads are not predicted here; the test
  only verifies that textures are bound and sampled.
- No depth, blending or wrap-mode behaviour is verified beyond the chosen
  MODULATE + NEAREST path.

## Licensing posture (not legal advice)

DeSmuME (GPL-2.0-or-later) is used only as an external program. TWL-HD (Apache-2.0) neither links nor vendors it. Separate repositories alone are
not assumed to settle licence compatibility; see `docs/EMULATOR_TRACER_FEASIBILITY.md`.
