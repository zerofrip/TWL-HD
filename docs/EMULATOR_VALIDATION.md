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
5. TXDS/TXDT/PLDT chunks are parsed by `twlhd_v2_dump.parse` but are not inspected by the existing-mode comparator; a non-texture capture that is
   otherwise valid remains PASS when those chunks are present.

## Mutation / negative tests (run by `make test`; 294 Python tests in total after 5C-3, measured baseline before it: 253; 3 of them need the devkitPro environment and are skipped without it)

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
- `DIAGSCREEN=<n>` (`n` = 1..9) produces an opt-in hardware-bisection variant of
  the `arm9v2` demo that changes exactly one suspected display factor at a time
  (**BUILD/UNIT only**; `HARDWARE UNVERIFIED`). `0`/unset leaves the normal ROM
  byte-identical. The `-diag<n>` suffix composes with `SCENE`, `AUTODUMP_FRAME`,
  `VP_*` and `CUBES`. See `docs/reports/5c4-black-screen-analysis.md` for the
  analysis, variant semantics and build hashes.
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
  **Demo ROMs must set the polygon cull bits:** on DS hardware `POLY_ALPHA(31)` alone leaves cull mode 0, which renders no
  face. Every `arm9v2` demo draw path now issues `glPolyFmt(... | POLY_CULL_NONE)`, so `SCENE=1..4`, the default cube,
  `CUBES` variants, `AUTODUMP_FRAME` and the `VP` variant all record `POLYGON_ATTR` word `0x001F00C0` (or the alpha-scene
  equivalents `0x001F00C0` / `0x001000C0` / `0x000400C0` / `0x000000C0`). `DIAGSCREEN=6/7` already used `POLY_CULL_NONE`;
  `DIAGSCREEN=9` keeps `POLY_CULL_BACK` by design. This was a **BUILD/UNIT** follow-up; no emulator or hardware run was done.

### Impact of the 5C-4 cull-bit follow-up on literals and hashes (BUILD only)

`POLY_ALPHA(a) = a << 16`; `POLY_CULL_NONE = 3 << 6 = 0xC0`.

| Source site (`arm9v2/source/demo_frame.cpp`) | Variant(s) | Old `POLYGON_ATTR` word | New `POLYGON_ATTR` word |
|---|---|---|---|
| `:298` `glPolyFmt(POLY_ALPHA(scene_alpha[q]))` | `SCENE=4` q0/q1/q2/q3 | `0x001F0000` / `0x00100000` / `0x00040000` / `0x00000000` | `0x001F00C0` / `0x001000C0` / `0x000400C0` / `0x000000C0` |
| `:370` shared scene branch | `SCENE=1,2,3` | `0x001F0000` | `0x001F00C0` |
| `:370` shared scene branch | `SCENE=5` | `0x001F00C0` | unchanged (already fixed in round 4) |
| `:325` | `DIAGSCREEN=6` | `0x001F00C0` | unchanged |
| `:413` | `DIAGSCREEN=7` | `0x001F00C0` | unchanged |
| `:415` | `DIAGSCREEN=9` | `0x001F0080` (`POLY_CULL_BACK`) | unchanged by design |
| `:421` default single-cube `#else` | default ROM, `AUTODUMP_FRAME`, `VP`, `DIAGSCREEN=1..5,8` | `0x001F0000` | `0x001F00C0` |
| `:466` multi-cube loop | `CUBES=2..170` per cube | `0x001F0000` | `0x001F00C0` |

Updated dependent literals:

- `tools/test_emulator_compare.py:95` fixture `0x29` entry: `0x001F0000` → `0x001F00C0` (`31<<16 = 0x001F0000`; `3<<6 = 0xC0`; OR = `0x001F00C0`).
- `tests/host/test_demo_frame.cpp` self-contained assertion: default/`CUBES`/`VP`/`SCENE=1..3,5` pin `0x001F00C0`; `SCENE=4` pins the four alpha words above.
- `tools/emulator/compare_capture.py` has no hard-coded `POLYGON_ATTR` value or tolerance; the regression test in `tools/test_emulator_compare.py` proves a cull-bits-0 word is rejected as `gxcm-value`.
- `tests/host/fixtures/demo_frame_desmume_v2.bin` is an archived DeSmuME capture with the old default-cube cull-bits-0 word. It is kept as evidence; the interpreter-output tests that consume it assert only alpha/lighting, which are unchanged by cull bits.

Expected ROM hash changes (everything whose draw path gained `0xC0` changes; `s5`, `diag6`, `diag7` and `diag9` stay the same):
`twlhd-arm9v2.nds`, `twlhd-arm9v2-s1.nds`, `twlhd-arm9v2-s2.nds`, `twlhd-arm9v2-s3.nds`, `twlhd-arm9v2-s4.nds`, `twlhd-arm9v2-c64.nds`, `twlhd-arm9v2-autodump.nds`, `twlhd-arm9v2-vp.nds`, `twlhd-arm9v2-diag1.nds` through `diag5.nds`, and `twlhd-arm9v2-diag8.nds`. `DIAGSCREEN=1..4` and `DIAGSCREEN=8` change because they draw the default cube path; `DIAGSCREEN=5` changes because it draws the default cube through the same branch.

## Textured scene (SCENE=5) run, stage 5C-3 tooling

**Label rules.** Everything described here is **BUILD/UNIT** tooling and tests.
A real DeSmuME run of the textured ROM is **EMULATOR** evidence, and it is
recorded by the owner's run in this file — this task does not run an emulator
and makes no EMULATOR or HARDWARE claim. DS hardware is the separate 5C-4 step.

Build the textured autodump ROM, capture it, cross-check both parsers, and
compare the texture chunks against the known arrays:

    DESMUME_CLI=/path/to/desmume-cli make emu-validate-tex

`DESMUME_CLI` setup is the same as for `emu-validate` (a self-built
`desmume-cli` outside this repository; see "Prerequisites and how to run"
above). The target fails loudly if `DESMUME_CLI` is unset or not executable.
It builds `make -C arm9v2 SCENE=5 AUTODUMP_FRAME=240` →
`arm9v2/twlhd-arm9v2-autodump-s5.nds`, captures into the **separate** directory
`tests/host/build/emu/capture-tex` (the `emu-validate` artifacts in
`tests/host/build/emu/capture` are never touched), runs
`tools/emulator/validate_with_parsers.sh`, then
`tools/emulator/compare_texture_capture.py`.

What the texture comparator checks (exit 0 PASS / 1 FAIL / 2 usage, one reason
per line):

1. v2 container parses; file flags are **exactly** FRAME_COMPLETE. OVERFLOW
   means the recorder dropped a texture key: for this scene a texture drop is a
   FAIL (the file flag is set by any dropped key; `rec->flags` is untouched).
   META backend is 1 (register proxy) and META coverage is 0x7.
2. Exactly one `TXDS` record. Its key equals the `TEXIMAGE_PARAM` and
   `PLTT_BASE` words the demo writes (`3 << 26` and `0`; texel base =
   `(param & 0xFFFF) << 3`, palette base = `pltt << 4`), its sizes equal the
   shared formula (32 texel bytes, 32 palette bytes, no index block), and the
   `TXDT`/`PLDT` bytes equal the known `scene5_texel`/`scene5_palette` arrays.
3. The GXCM stream has the structural shape the scene produces: exactly one
   `POLYGON_ATTR` with the literal `0x001F00C0` (`POLY_ALPHA(31) |
   `POLY_CULL_NONE`) before `BEGIN_VTXS`, one `TEXIMAGE_PARAM` with the literal
   param, one `PLTT_BASE` with the literal pltt, `POLYGON_ATTR`/
   `TEXIMAGE_PARAM`/`PLTT_BASE` all before exactly one `BEGIN_VTXS(GL_QUADS)`,
   four `(TEX_COORD, VTX_16, VTX_16)` triples inside the BEGIN group, one
   `END_VTXS` after it, and `SWAP_BUFFERS` last. The perspective/look-at/rotation
   commands are checked analytically with the same helpers as
   `compare_capture.py` (same demo camera path).

**Justified limitation:** the host expected stream does not support SCENE=5
(`tests/host/dump_demo_frame.cpp` is a non-texture build and its mock has no
texture/VRAM ports), so the command-id sequence is compared structurally plus
against the exact known key values (`POLYGON_ATTR`, `TEXIMAGE_PARAM`,
`PLTT_BASE`) instead of entry for entry against `expected.txt`. A wrongly
ordered non-key command outside the checked shape is therefore not caught by
this comparator; the cross-checked literals, the matrix checks and
`docs/reports/5c3-mutation-report.md` bound that gap.

**What a PASS means:** the PASS proves the recorder, the v2 serializer and the
source-buffer capture path worked **in DeSmuME only** — the capture contains the
exact key, texel and palette bytes the demo registered, and both parsers accept
it. It says nothing about DS hardware, VRAM readback, real SD/FAT behaviour or
GX timing; those are the separate 5C-4 hardware step.

The expected arrays live in exactly one place
(`tools/emulator/compare_texture_capture.py`); a unit test parses the
`scene5_texel`/`scene5_palette` initialisers and the `param`/`pltt` literals out
of `arm9v2/source/demo_frame.cpp` and fails if they change. Negative tests
(flipped TXDT/PLDT byte, wrong param/pltt key, wrong `POLYGON_ATTR` / cull bits
set to 0, two/zero TXDS records, wrong TXDT length, missing PLDT, OVERFLOW set,
truncated chunk) each fail with their specific reason. The unchanged non-texture comparator accepts a textured capture
as long as its flags, META, GXCM and RSTA still match the expected non-texture
stream, because `compare_capture.py` does not inspect TXDS/TXDT/PLDT;
`tools/test_emulator_texture_compare.py` pins this behaviour with a texture-free
control run and a same-capture-plus-valid-TXDS/TXDT/PLDT run, both exit 0 PASS.
`tools/test_emulator_texture_run.py` drives this target's script path end to end
with a fake emulator (positive and corrupted capture; no real DeSmuME).

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

`tools/gen_tex_frames.py` writes deterministic v2 frames into an output
directory:

  twlhd_frame_v2_tex.bin     four quads with:
                               - 8x8 red/white checker
                               - 16x16 blue/white checker
                               - 16x8 green/white checker (non-square)
                               - 8x8 red/white checker reused from the first run
  twlhd_frame_v2_tex_ctl.bin same geometry, no texture state (untextured control)
  twlhd_frame_v2_tex_over.bin three distinct 1024x1024 I2 keys sharing the
                               same TXDT/PLDT bytes; intentionally over the
                               8 MiB practical arena budget

The first two frames are used for the screenshot comparison below. The
over-budget frame is used for the hardware fallback probe described in
`docs/PHASE5B_PLAN.md`.

All frames end with SWAP_BUFFERS and set `FLAG_FRAME_COMPLETE`. The texture
frame uses DS 16bpp (format 7) TXDS/TXDT chunks and no palette, so the
`common/twlhd_pica_frame_prepare()` path decodes and swizzles them into RGBA8.
The over-budget frame uses DS I2 (format 2) with a PLDT palette chunk.
The texture frame emits `CMD_COLOR RGB555_WHITE` (0x8FFF) before every textured
run; `tests/host/build/gx_interp_cli` on the generated frame confirms all 24
vertices have RGBA `(1, 1, 1, 1)`, so the chosen MODULATE TexEnv shows the
texels unchanged.

### Procedure

Build the host (needed once):

    make azahar-host

Generate the frames:

    python3 tools/gen_tex_frames.py /tmp/tex_frames

Run the textured frame. The ARM11 app looks for `sdmc:/twlhd_frame.bin`, so
copy the chosen frame under that name inside `--sd-root`:

    mkdir -p /tmp/tex_sd
    cp /tmp/tex_frames/twlhd_frame_v2_tex.bin /tmp/tex_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/tex_shot --sd-root /tmp/tex_sd

Repeat with the control frame:

    cp /tmp/tex_frames/twlhd_frame_v2_tex_ctl.bin /tmp/tex_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/tex_ctl_shot --sd-root /tmp/tex_sd

### Prediction and comparison

The host preparation path was verified separately: `twlhd_pica_frame_prepare()`
on the generated texture frame returns `OK` with 4 plan entries, 3 unique
textures and 1792 arena bytes; entry 3 shares texture index 0 with entry 0.

The repeatable, scripted check is `tools/check_tex_shot.py` (stdlib only). It
reads the P6 PPM, samples four fixed rectangles on the top screen, and performs
two independent checks:

1. **Colour check:** counts the distinct colours in each rectangle and checks
them against the expected checker (textured frame: base colour + white) or
solid colour (control frame: one colour) within a per-channel tolerance.

2. **Structure check (textured frames only):** measures the checker cell period
along x and y inside each quad, compares it with the period predicted from the
measured quad size and the known texture size, checks that the non-square
16x8 region has a clearly different x/y cell ratio from the square regions,
checks that the two 8x8 regions (UR and LL, which share a texture) have the
same cell pattern, and checks that the top-left checker cell of every region
matches the texture's base colour (a vertically mirrored render shows white
there instead).

Sample rectangles (top screen 400x240, x left-to-right, y top-to-bottom):

| region | x range | y range | textured expectation | control expectation | texture size |
|---|---|---|---|---|---|
| UL | 70-170 | 75-110 | green + white | solid green | 16x8 |
| UR | 270-340 | 75-110 | red + white | solid grey | 8x8 |
| LL | 70-170 | 150-185 | red + white | solid red | 8x8 |
| LR | 270-340 | 150-185 | blue + white | solid blue | 16x16 |

Tolerances:
- per-channel RGB distance `<= 40` for colour checks;
- measured cell period within `25 %` (or `1 px`, whichever is larger) of the
  period predicted from the quad size / texture size;
- non-square region ratio must differ from the average square-region ratio by
  more than `0.15`;
- reused-texture phase must match within the RGB tolerance;
- top-left cell phase must match the expected base colour within the RGB
  tolerance (5x5 mean, robust to the known ~1 px hardware shift).

Run the checker after generating both screenshots:

    python3 tools/check_tex_shot.py /tmp/tex_shot_199.ppm --frame tex
    python3 tools/check_tex_shot.py /tmp/tex_ctl_shot_199.ppm --frame ctl

A matching textured frame prints `colour OK`, `structure OK`, `ratio OK` and
`reuse OK` and exits 0; a mismatch exits non-zero. Synthetic match/mismatch
cases are in `tools/test_check_tex_shot.py` and run as part of `make test`.

### Over-budget fallback probe

Generate a frame that exceeds the practical arena budget:

    python3 tools/gen_tex_frames.py /tmp/tex_frames --over-budget 3

The resulting `twlhd_frame_v2_tex_over.bin` has three distinct
`(teximage_param, pltt_base)` keys, each describing a 1024x1024 I2 texture.
`twlhd_pica_frame_arena_needed()` reports 12 MiB, which is above the 8 MiB
practical cap. On hardware this frame should trigger the budget fallback in
`arm11/source/main.c` and render as solid colours without crashing. The probe
is not exercised in the Azahar screenshot comparison above.

### Root cause of the identical-screenshot symptom

The earlier reference run showed `tex_shot_199.ppm` byte-identical to
`tex_ctl_shot_199.ppm`. The trace readout added for this round shows the ARM11
code *did* reach the textured path: the bottom screen records
`tex staged OK: 4 entries, 3 textures` and the trace ring at frame 199 includes
`tex commit`, `tex bind`, and `clip tex DrawArrays`. The path was therefore
being exercised, but the textured screenshot still matched the untextured
control.

The real cause is in `arm11/source/renderer.c`: `commit_pending_textures()`
swaps the active texture set back into the pending slot and frees it on the
next commit. In the old code the set was freed while Citro3d unit 0 still held
the pointer from the previous `C3D_TexBind(0, tex)`. From that frame onward the
bound texture memory was invalid, so the MODULATE TexEnv sampled effectively
white texels and the output collapsed to the per-vertex base colour — exactly
the same solid colours produced by the untextured control frame.

The fix is `C3D_TexBind(0, NULL)` before every free of a possibly-bound set
(`commit_pending_textures()`, the commit block in `draw_clip_internal()`,
`renderer_set_clip_scene_textured()` pending replacement, `renderer_shutdown()`,
and at the end of `draw_clip_textured_entries()`), plus the same lifecycle
clear/commit in `renderer_set_geometry()` and the DEBUG draw branch. After the
fix the texture set stays valid while bound, the screenshots differ, and
the checker passes. This is EMULATOR evidence only; it is not HARDWARE VERIFIED.

### Reference run (EMULATOR)

Reproduced in this sandbox, 2026-11-14, with the external core
`$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so`
(Azahar `9c38437`) and the commands above.

- `make azahar-host`: clean build, no warnings.
- Both runs produced 400x480 PPM screenshots at frame 199 and frame `last`.
- Textured frame `tex_shot_199.ppm` sha256:
  `58003d653e8d8c18b08af47ffcf3f6316460735e57a440fdeb9bc1f4a22f896a`.
- Control frame `tex_ctl_shot_199.ppm` sha256:
  `b1be377150b726bff4b43a7f85f407988ff3d310301c67c6d693693a37f4fa7c`.
- The two screenshots differ; the texture path is exercised.
- Bottom-screen readout at frame 199 shows the staging verdict
  `tex staged OK: 4 entries, 3 textures` and the trace ring includes
  `tex commit`, `tex bind`, and `clip tex DrawArrays`, confirming the
  textured shader path ran. The control screenshot still prints no `T:`
  trace-ring line because the renderer trace hook is only installed while a
  textured frame is staged. The sha256s differ from the previous reference
  run because the screenshots were regenerated with the repair-round-5
  binary; the checker output and the control console layout are unchanged.
- `check_tex_shot.py` output for the textured screenshot:

      UL colour OK: 2 distinct colours: (0, 255, 0) (255, 255, 255)
      UR colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
      LL colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
      LR colour OK: 2 distinct colours: (0, 0, 255) (255, 255, 255)
      UL structure OK: quad=125x66 cells=4x2 predicted_period=31.2x33.0 measured_period=31.0x32.5
      UR structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
      LL structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
      LR structure OK: quad=125x66 cells=4x4 predicted_period=31.2x16.5 measured_period=31.0x16.2
      UL ratio OK: UL=0.95 square_avg=1.91 diff=0.95
      UR==LL reuse OK: period=True phase=True

- `check_tex_shot.py` output for the control screenshot:

      UL colour OK: 1 distinct colours: (0, 255, 0)
      UR colour OK: 1 distinct colours: (132, 132, 132)
      LL colour OK: 1 distinct colours: (255, 0, 0)
      LR colour OK: 1 distinct colours: (0, 0, 255)

### Limits

- Azahar's software renderer is not the real PICA200; it validates that the
  ARM11 code path produces coherent GPU commands, not that hardware will accept
  the texture layout or attribute permutation.
- The exact screen positions of the quads are not predicted here; the test
  only verifies that textures are bound and sampled.
- No depth, blending or wrap-mode behaviour is verified beyond the chosen
  MODULATE + NEAREST path.

### Multi-format frame (5B-2e-2 follow-up)

**BUILD/UNIT/EMULATOR evidence; HARDWARE NOT RUN.** This subsection adds one
frame that exercises every DS texture format the renderer can decode (1 A3I5,
2 I2, 3 I4, 4 I8, 5 compressed 4x4 with block modes 0-3, 6 A5I3, 7 16bpp
reference) plus a 16x16 I4 quad, and a checker that compares every texel cell
against colours computed by the independent Python oracle
(`tools/tex_decode_oracle.py`).

Generator and frame:

    python3 tools/gen_tex_format_frames.py /tmp/fmt_frames
    python3 tools/gen_tex_format_frames.py --describe   # quad -> format -> grid

`twlhd_frame_v2_formats.bin` (3136 bytes) holds eight non-overlapping quads in
a 4x2 grid of the 400x240 top screen with white vertex colours, so MODULATE
shows the texels unchanged:

| quad | format | size | texture |
|---|---|---|---|
| 0 | 1 A3I5 | 8x8 | 8-colour cycle, alpha 7 |
| 1 | 2 I2 | 8x8 | palette entries 1-3 (index 0 unused) |
| 2 | 3 I4 | 8x8 | 15-colour cycle (index 0 unused) |
| 3 | 4 I8 | 8x8 | 15-colour cycle (index 0 unused) |
| 4 | 5 4x4 | 8x8 | one block per mode 0/1/2/3, distinct palette windows |
| 5 | 6 A5I3 | 8x8 | 8-colour cycle, alpha 31 |
| 6 | 7 16bpp | 8x8 | 8-colour cycle, bit 15 set |
| 7 | 3 I4 | 16x16 | 15-colour diagonal ramp |

Every texel is fully opaque (palette bit 15 set; the decoder masks it for
formats 1-6). Colour-0 choice: TEXIMAGE_PARAM bit 29 is clear **and** palette
index 0 is never a visible colour in formats 2/3/4; formats 1/5/6/7 do use
palette entry 0 (they ignore KeyColor0). The four 4x4 blocks use palette
windows at byte offsets 0/8/12/20 and never reference the transparent slot 3
of modes 0/1.

Clip coordinates are derived from the desired top-screen rectangles with the
emulator-observed mapping: the DS viewport is pillar-boxed into the central
320 pixels, so `x_screen = 200 + 160 * (x_clip / 4096)` and
`y_screen = (1 - y_clip / 4096) * 120`. The 320-wide region also keeps every
vertex inside `x_clip = +-4096`, so PICA does not clip the quads.

Checker:

    python3 tools/check_tex_formats.py /tmp/fmt_shot_199.ppm [--tolerance 24]

It accepts a 400x240 PPM or a 400x480 PPM (top screen = upper 240 rows), finds
each quad's bounding box inside its grid cell, samples the centre of every
texel cell with a 3x3 mean and compares against the oracle's expected RGB with
a per-channel tolerance (default 24, JPEG-capture friendly). Whole-quad shifts
`dx, dy in {-1, 0, 1}` are tried and the best is reported. It exits non-zero
if any cell, or a whole quad, is missing or outside tolerance. The expected
texel-row orientation is pinned: **decoded row 0 is the top screen edge of the
quad**, matching the corrected PICA memory layout (memory tile row 0 = image
top row). There is no flip search.

Emulator run (same procedure as the 5B-2e-2 reference run above; the core was
rebuilt from Azahar `9c38437` at the path below and the ARM11 3dsx is the
current normal build):

    make azahar-host
    make -C arm11
    python3 tools/gen_tex_format_frames.py /tmp/fmt_frames
    mkdir -p /tmp/fmt_sd
    cp /tmp/fmt_frames/twlhd_frame_v2_formats.bin /tmp/fmt_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/fmt_shot --sd-root /tmp/fmt_sd
    python3 tools/check_tex_formats.py /tmp/fmt_shot_199.ppm

Before the fix (old build with the bottom-up swizzle, `sha256
b59899148c1ea7a34202caccf0776a084f0fc022f4f12cf608e33b96ad5ad48c`), every
quad showed decoded row 0 at the bottom screen edge. The old
`tools/check_tex_formats.py` pinned that mirrored orientation and reported:

    quad 0 fmt=1 A3I5 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(44, 11, 107, 110) OK
    quad 1 fmt=2 I2 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(116, 11, 179, 110) OK
    quad 2 fmt=3 I4 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(188, 11, 251, 110) OK
    quad 3 fmt=4 I8 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(260, 11, 355, 110) OK
    quad 4 fmt=5 4x4 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(44, 131, 107, 230) OK
    quad 5 fmt=6 A5I3 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(116, 131, 179, 230) OK
    quad 6 fmt=7 16bpp 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(188, 131, 251, 230) OK
    quad 7 fmt=3 I4 16x16 cells=256/256 worst=0 shift=(0,0) bbox=(260, 131, 355, 230) OK

With the corrected build the same procedure produces
`/tmp/fmt_shot_199.ppm` (`sha256
2794811712560fc5707489137f9604606aefdfe74c83f1409d5c5f725840ad8e`) and the
new top-pin checker reports:

    quad 0 fmt=1 A3I5 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(44, 11, 107, 110) OK
    quad 1 fmt=2 I2 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(116, 11, 179, 110) OK
    quad 2 fmt=3 I4 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(188, 11, 251, 110) OK
    quad 3 fmt=4 I8 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(260, 11, 355, 110) OK
    quad 4 fmt=5 4x4 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(44, 131, 107, 230) OK
    quad 5 fmt=6 A5I3 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(116, 131, 179, 230) OK
    quad 6 fmt=7 16bpp 8x8 cells=64/64 worst=0 shift=(0,0) bbox=(188, 131, 251, 230) OK
    quad 7 fmt=3 I4 16x16 cells=256/256 worst=0 shift=(0,0) bbox=(260, 131, 355, 230) OK

FINDING and FIX (orientation): textures were rendering vertically mirrored in
Azahar (emulator) because `common/twlhd_pica_tex.c` stored PICA
texture memory with a vertical row flip (`vy = height-1-y`) while keeping
`v = 1 - t/height`. The top vertices (`t=0`, `v=1`) therefore sampled memory
row 0, which the old swizzle filled with the image bottom row, so decoded row 0
appeared at the bottom quad edge. Re-reading the sources showed the old
interpretation of Azahar's "linear buffer is bottom-up" comment was wrong:
Azahar `texture_codec.h:892-897` processes the GL bottom-up linear buffer so
that its tile walk starts tile row 0 at linear row `height - 8`, and
`DecodeTile` (:577-604) maps in-tile row `y` to linear row `7 - y` within the
tile. Because the tile base is at linear row `height - 8`, tile row 0 lands on
linear row `height - 1`, i.e. the image top row. Panda3DS's
`Texture::decodeTexture` (`textures.cpp:250-263`) walks `v` top-to-bottom over
texture memory and passes rows to `glTexSubImage2D` in that order; the first
row given to `glTexSubImage2D` is the bottom row (v=0) of the GL texture, so
memory row 0 lands at GL v=0/bottom. Panda3DS therefore points to the **old**
orientation while Azahar's tile walk points to the new one — the sources are
ambiguous/conflicting. Fix (A) rests on the Azahar tile-walk reading plus the
emulator observation of the multi-format frame whose corners follow DS
semantics (`t=0` at the top vertices); Panda3DS partly conflicts. Hardware
evidence for the orientation rule itself is still missing: Run 19 hardware
showed the upright picture of a frame whose bottom-up corner order
(build `f89d3bd`) compensated the old renderer's row flip, i.e. on hardware
old-code + compensated-frame equals the emulator's old-code +
compensated-frame picture. Fix (A): removed the
row flip in the swizzle and unswizzle, kept `v = 1 - t/height`;
`tools/gen_tex_frames.py` was also corrected because its `QUADS` corner order
had been compensating for the old flip (it now lists corners top-left,
top-right, bottom-right, bottom-left, matching
`tools/gen_tex_format_frames.py`).

The standard textured frame (`tools/check_tex_shot.py --frame tex`) now passes
including the new top-left cell phase check; the fixed screenshot is
`/tmp/tex_shot_199.ppm` (`sha256
1da0f3f63af5085667645cc9684f572b56861f3c9b69ba0946f1caaddbfff049`). The old
mirrored standard-frame screenshot would fail the new phase check (top-left
cell is white instead of the base colour).

**Remaining caveat:** HARDWARE verification of the corrected orientation with a
DS-correct frame: Run 20, NOT yet run. Run 19 hardware showed the upright
picture of a frame whose bottom-up corner order compensated the old renderer's
row flip (build `f89d3bd`), so the hardware evidence for the orientation rule
itself is still missing.

UNIT evidence: `tools/test_tex_formats.py` (run by `make test`) covers
determinism, the v2 parser, byte-exact `tex_decode_cli` agreement with the
oracle, `twlhd_pica_frame_prepare()` (8 entries, 8 textures), the checker's
pass/fail cases and hand-computed literal colours per format.
`pica_frame_cli` cannot read a v2 frame file: it builds a synthetic frame from
a stdin description, so the test feeds it the generator's records and three
vertices per quad and asserts `result=OK`.

### Behaviour frame (translucency, colour 0, wrap)

**BUILD/UNIT/EMULATOR evidence; HARDWARE NOT RUN.** This frame exercises the
three texture behaviours not previously shown on screen: translucent texels
(A3I5/A5I3 alpha below full), colour-0 transparency (TEXIMAGE_PARAM bit 29 on
formats 2/3/4) and the DS wrap bits 16-19 (repeat / clamp / mirrored repeat /
flip-only clamp). `tools/check_tex_behaviour.py` compares every sampled cell
against colours computed independently from `tools/tex_decode_oracle.py` plus
its own wrap function; expectations were NOT adjusted after the run.

Generator and checker:

    python3 tools/gen_tex_behaviour_frames.py /tmp/beh_frames
    python3 tools/gen_tex_behaviour_frames.py --describe   # layout + expected
    python3 tools/check_tex_behaviour.py /tmp/beh_shot_199.ppm [--tolerance 28]

`twlhd_frame_v2_behaviour.bin` (2776 bytes, sha256
`22cc1db3ce4275130d56323aea67278d3098d92368f9ccc4918195cc21f9ac93`) holds eight
non-overlapping quads in a 4x2 grid of the 400x240 top screen, white vertex
colour, polygon alpha 31:

| quad | role | texture |
|---|---|---|
| 0 | A3I5 alpha | alpha 7/5/3/0 in column pairs, rows identical |
| 1 | A5I3 alpha | alpha 31/20/10/0 in column pairs, rows identical |
| 2 | I4 key0 on | bit 29 set; index 0 transparent, index 1 cyan |
| 3 | I4 key0 off | same bytes/palette, bit 29 clear; index 0 white |
| 4 | repeat ST | bits 16,17; texcoords 0..2 both axes |
| 5 | clamp ST | bits 16-19 clear; texcoords -0.5..1.5 |
| 6 | mirror ST | bits 16-19; texcoords 0..2 |
| 7 | flip-only clamp | bits 18,19 only (DS flip without repeat = clamp) |

Q4-Q7 share one asymmetric opaque 8x8 16bpp ramp: R = x*4, G = y*4, a unique
marker B=31 at (0,0) and B=16 at (7,7). Translucent expectations are
`texel_rgb*a + clear*(1-a)` with `clear = (16,16,24)` and the oracle's 0..255
alpha; colour-0 texels decode to alpha 0 and therefore also blend to clear.
Wrap expectations use the DS-wrapped texel coordinate computed from the DS
t/s texel coordinate (repeat = mod, mirrored repeat = triangle about h, clamp =
min/max), with decoded row 0 pinned at the top quad edge. Cells are mapped from
the generator's known rect, not from a bbox search, because transparent texels
render exactly clear and would shrink the bbox (the bbox is still reported).

Emulator run (same procedure as the multi-format frame above; the ARM11 3dsx
is the current normal build):

    make azahar-host
    make -C arm11
    python3 tools/gen_tex_behaviour_frames.py /tmp/beh_frames
    mkdir -p /tmp/beh_sd
    cp /tmp/beh_frames/twlhd_frame_v2_behaviour.bin /tmp/beh_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/beh_shot --sd-root /tmp/beh_sd
    python3 tools/check_tex_behaviour.py /tmp/beh_shot_199.ppm

`/tmp/beh_shot_199.ppm` is 400x480, sha256
`0d58504c1b54d6499035aee58fa9111a54c3d47948acfe577b7f9d7f0c936a3e`. Verbatim
checker output (exit 1):

    quad 0 role=A3I5 alpha cells=64/64 worst=0 shift=(0,0) bbox=(44, 11, 91, 110) OK
    quad 1 role=A5I3 alpha cells=64/64 worst=0 shift=(0,0) bbox=(116, 11, 163, 110) OK
    quad 2 role=I4 key0 on cells=64/64 worst=0 shift=(0,0) bbox=(188, 11, 251, 110) OK
    quad 3 role=I4 key0 off cells=64/64 worst=0 shift=(0,0) bbox=(260, 11, 355, 110) OK
    quad 4 role=repeat ST cells=128/256 worst=255 shift=(0,0) bbox=(44, 131, 107, 230) FAIL
      cell(0,8) expected=(0, 0, 255) measured=(0, 231, 0)
      cell(1,8) expected=(33, 0, 0) measured=(33, 231, 0)
      cell(2,8) expected=(66, 0, 0) measured=(66, 231, 0)
      cell(3,8) expected=(99, 0, 0) measured=(99, 231, 0)
      cell(4,8) expected=(132, 0, 0) measured=(132, 231, 0)
      cell(5,8) expected=(165, 0, 0) measured=(165, 231, 0)
      cell(6,8) expected=(198, 0, 0) measured=(198, 231, 0)
      cell(7,8) expected=(231, 0, 0) measured=(231, 231, 132)
      ... more mismatches omitted
    quad 5 role=clamp ST cells=256/256 worst=0 shift=(0,0) bbox=(116, 131, 179, 230) OK
    quad 6 role=mirror ST cells=144/256 worst=255 shift=(0,0) bbox=(188, 131, 251, 230) FAIL
      cell(0,9) expected=(0, 198, 0) measured=(0, 231, 0)
      cell(1,9) expected=(33, 198, 0) measured=(33, 231, 0)
      cell(2,9) expected=(66, 198, 0) measured=(66, 231, 0)
      cell(3,9) expected=(99, 198, 0) measured=(99, 231, 0)
      cell(4,9) expected=(132, 198, 0) measured=(132, 231, 0)
      cell(5,9) expected=(165, 198, 0) measured=(165, 231, 0)
      cell(6,9) expected=(198, 198, 0) measured=(198, 231, 0)
      cell(7,9) expected=(231, 198, 0) measured=(231, 231, 132)
      ... more mismatches omitted
    quad 7 role=flip-only clamp cells=256/256 worst=0 shift=(0,0) bbox=(260, 131, 355, 230) OK
    exit 1

**PASSING roles.** Q0/Q1 (A3I5 and A5I3 translucency, alpha 7/5/3/0 and
31/20/10/0) match the blend prediction exactly, worst channel deviation 0 —
the alpha-0 pairs render as clear, so translucency and the 0..255 alpha
expansion agree on screen. Q2 (I4 with bit 29 set) renders every index-0 texel
as clear, so colour-0 transparency is applied by the decode/blend path. Q3
(bit 29 clear) shows the bright palette colour at index 0. Q5 (clamp) and Q7
(flip-only clamp) match: DS flip without repeat behaves as clamp.

**FINDING (emulator): negative texture coordinates sample one texel off.**
Q4 (REPEAT) matches 128/256 cells and Q6 (MIRRORED_REPEAT) 144/256. In both,
every failing cell has DS `t > height`, i.e. PICA `v = 1 - t/h < 0`; the S axis
(`u` up to 2, always positive and > 1) repeats/mirrors correctly, and all rows
with `v >= 0` pass. In the negative half the measured texel row is one row
away from the prediction, not the same row: Q4 at `t=8.5` predicts row 0 but
measures row 7, at `t=9.5` predicts 1 measures 0, ... at `t=15.5` predicts 7
measures 6; Q6 at `t=9.5` predicts row 6 but measures 7, at `t=10.5` predicts 5
measures 6, ... at `t=15.5` predicts 0 measures 1. That is the signature of a
texel index that truncates a negative coordinate toward zero instead of
flooring it before applying the wrap mode (truncation equals floor for the
positive half, so only `v < 0` is shifted).

Layer diagnosis: **emulator (Azahar software rasterizer)**, not the DS rule,
the prepare path or the renderer blend/TexEnv. Evidence: (1)
`pica_frame_cli` on the same records reports `texture 4 ... wrap_s=2 wrap_t=2`
and `texture 6 ... wrap_s=3 wrap_t=3` (`twlhd_pica_wrap_from_teximage`), and
the vertex 12-14 `v` values are `1.0, 1.0, -1.0`, i.e. the negative half comes
from the DS `t > h` texcoords through the pinned `v = 1 - t/h` mapping; (2) the
renderer passes those wrap codes straight to `C3D_TexSetWrap`; (3) the positive
half of the same axes wraps correctly. A real PICA200 `GPU_REPEAT` /
`GPU_MIRRORED_REPEAT` floors the wrapped coordinate, so this is an emulator
sampling error, not an expectation change. No tolerance or expected colour was
altered.

**HARDWARE OBSERVED (Run 21, 2026-11-14 16:49-16:51, owner-approved):** the same
frame on the real New 3DS (`twlhd-arm11-tex2.3dsx`, main `e29aa33`, arm11
unchanged through `d3279ed`; `tools/check_tex_behaviour.py` at its default
tolerance 28 on the 400x240 NTR capture) passes EVERY cell of ALL eight quads:
Q0 A3I5 64/64 (worst 2), Q1 A5I3 64/64 (3), Q2 colour-0 on 64/64 (3), Q3 colour-0
off 64/64 (3), Q4 REPEAT 256/256 (26), Q5 CLAMP 256/256 (23), Q6 MIRRORED_REPEAT
256/256 (22), Q7 flip-only 256/256 (4). The worst deviations 22-26 in Q4-Q6 are
JPEG blur on small cells and stay inside the tolerance. So the negative-
coordinate off-by-one is an Azahar emulator error: the real PICA200 floors the
coordinate and renders the predicted wrap, and no change to `common/` or
`arm11/` is needed. Details: `hwtest-staging/RUN2_RESULTS.md` (Run 21). Not
shown on hardware: texture-matrix modes 2/3, other palette bases, performance,
large-texture memory behaviour.

UNIT evidence: `tools/test_tex_behaviour.py` (run by `make test`) covers the
generator determinism (2776 bytes), v2 parse (`result=OK`), `pica_frame_cli`
prepare (`entry_count=8`, `texture_count=8`, wrap codes 2/0/3/0 for Q4-Q7),
the checker's perfect/noise/1-px-shift pass cases, the mandated fail cases
(translucent drawn opaque, colour-0 not applied, repeat drawn clamped, mirror
drawn as repeat, flip-only drawn as mirrored, vertically flipped wrap quad,
missing quad) and hand-computed literal blend/wrap colours.

## Hardware captures (Run 19)

**HARDWARE OBSERVED (Run 19, 2026-11-14), re-checked with the current
`tools/check_tex_shot.py`; BUILD/UNIT only.** The Run 19 captures are P6 PPM
400x240 top-screen-only images (verified header: `P6\n400 240\n255\n`), while
the pre-existing checker demanded 400x480. The script now accepts a 400x240
top-screen capture (or the old 400x480 with the top screen in the upper 240
rows) and has a `--snap` option that replaces every top-screen pixel with the
nearest colour of a fixed palette before any check: `(0,255,0)`, `(255,0,0)`,
`(0,0,255)`, `(255,255,255)`, `(16,16,24)` clear, `(132,132,132)` and
`(128,128,128)` control grey, `(0,0,0)` (squared RGB distance, ties to the
earlier entry). The UR==LL reuse check now samples texel cell centres with a
3x3 majority and searches a +-1 px sample shift.

Control frame:

    $ python3 tools/check_tex_shot.py \
        $HOME/3ds/hwtest-staging/ntr_live/run19/ctl_top_0.ppm --frame ctl --snap
    UL colour OK: 1 distinct colours: (0, 255, 0)
    UR colour OK: 1 distinct colours: (132, 132, 132)
    LL colour OK: 1 distinct colours: (255, 0, 0)
    LR colour OK: 1 distinct colours: (0, 0, 255)
    exit 0

Textured frame:

    $ python3 tools/check_tex_shot.py \
        $HOME/3ds/hwtest-staging/ntr_live/run19/tex_top_0.ppm --frame tex --snap
    UL colour OK: 2 distinct colours: (0, 255, 0) (255, 255, 255)
    UR colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
    LL colour OK: 2 distinct colours: (255, 0, 0) (255, 255, 255)
    LR colour OK: 2 distinct colours: (0, 0, 255) (255, 255, 255)
    UL structure OK: quad=125x66 cells=4x2 predicted_period=31.2x33.0 measured_period=31.0x32.5
    UR structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
    LL structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
    LR structure OK: quad=125x66 cells=4x4 predicted_period=31.2x16.5 measured_period=31.0x16.2
    UL ratio OK: UL=0.95 square_avg=1.91 diff=0.95
    UR==LL reuse OK: period=True cells=4/4 shift=(0,0)
    UL phase OK: got=(0, 255, 0) want=(0, 255, 0)
    UR phase OK: got=(255, 0, 0) want=(255, 0, 0)
    LL phase OK: got=(255, 0, 0) want=(255, 0, 0)
    LR phase OK: got=(0, 0, 255) want=(0, 0, 255)
    exit 0

Note on the phase line: this Run 19 capture does **not** fail phase. Direct
pixel sampling shows the base colour in the top-left cell centre of every quad
(green/red/red/blue above), i.e. the captured picture is upright, and the
pristine `e29aa33` checker reports the same `phase OK` lines on a padded copy
(so this is not an artefact of the new code). Reason: Run 19 ran build
`f89d3bd`, whose `tools/gen_tex_frames.py` used the bottom-up corner order
(`git show f89d3bd:tools/gen_tex_frames.py`:
`((-3840,-2560),(-640,-2560),(-640,-320),(-3840,-320))`, texture `t=0` at the
bottom-left vertex). That order compensated the old renderer's row flip, so the
old renderer with the Run 19 frame is upright on screen; the mirror only became
visible with the top-down corner order that `e29aa33` introduced. The phase
check was not weakened: it still rejects a vertically mirrored checker
(`test_vertically_flipped_phase_fails`).

To show the phase check catching the old mirror on real hardware pixels, the
old renderer's picture for the corrected (top-down) frame is derived from the
capture by mirroring each quad vertically inside its measured bounding box
(the only difference between the two frame corner orders). The derivation is
reproducible, stdlib only, no repository fixture:

    $ python3 - <<'PY'
    import sys
    sys.path.insert(0, "tools")
    import check_tex_shot as c
    w, h, data = c.parse_ppm(
        "$HOME/3ds/hwtest-staging/ntr_live/run19/tex_top_0.ppm")
    src = bytes(data)
    for _name, x0, x1, y0, y1, *_rest in c.REGIONS:
        qx0, qx1, qy0, qy1 = c.find_quad_bounds(
            src, w, h, (x0 + x1) // 2, (y0 + y1) // 2, c.CLEAR, c.TOLERANCE)
        for y in range(qy0, qy1 + 1):
            for x in range(qx0, qx1 + 1):
                s = ((qy0 + qy1 - y) * w + x) * 3
                d = (y * w + x) * 3
                data[d:d + 3] = src[s:s + 3]
    open("/tmp/run19_tex_mirrored.ppm", "wb").write(
        b"P6\n%d %d\n255\n" % (w, h) + bytes(data))
    PY

The derived image (sha256
`cb57ff9806967f60c529a9e953627de8de3d63382dd4737b6fba3adbb7cbf824`, /tmp only)
keeps the same quad bounds and periods and fails exactly and only on phase:

    $ python3 tools/check_tex_shot.py /tmp/run19_tex_mirrored.ppm --frame tex --snap
    UL colour OK: 2 distinct colours: (255, 255, 255) (0, 255, 0)
    UR colour OK: 2 distinct colours: (255, 255, 255) (255, 0, 0)
    LL colour OK: 2 distinct colours: (255, 255, 255) (255, 0, 0)
    LR colour OK: 2 distinct colours: (255, 255, 255) (0, 0, 255)
    UL structure OK: quad=125x66 cells=4x2 predicted_period=31.2x33.0 measured_period=31.0x32.5
    UR structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
    LL structure OK: quad=125x66 cells=2x2 predicted_period=62.5x33.0 measured_period=62.0x32.5
    LR structure OK: quad=125x66 cells=4x4 predicted_period=31.2x16.5 measured_period=31.0x16.2
    UL ratio OK: UL=0.95 square_avg=1.91 diff=0.95
    UR==LL reuse OK: period=True cells=4/4 shift=(0,0)
    UL phase FAIL: got=(255, 255, 255) want=(0, 255, 0)
    UR phase FAIL: got=(255, 255, 255) want=(255, 0, 0)
    LL phase FAIL: got=(255, 255, 255) want=(255, 0, 0)
    LR phase FAIL: got=(255, 255, 255) want=(0, 0, 255)
    exit 1

The over-budget capture is not a tex/ctl picture (`over_top_0.ppm` is clear
colour plus one white region), so it is not compared with this checker; it is
compared separately against the emulator fallback as recorded in
`docs/PHASE5B_PLAN.md` ("4743 white pixels on both, mean abs diff 0.71"). For
the record it fails the tex check as expected:

    $ python3 tools/check_tex_shot.py \
        $HOME/3ds/hwtest-staging/ntr_live/run19/over_top_0.ppm --frame tex --snap
    UL colour FAIL: 1 distinct colours: (16, 16, 24)
    UR colour FAIL: 1 distinct colours: (16, 16, 24)
    LL colour FAIL: 2 distinct colours: (16, 16, 24) (255, 255, 255)
    LR colour FAIL: 1 distinct colours: (16, 16, 24)
    exit 1

Emulator regeneration with the current (fixed) build was possible:

    make azahar-host
    make -C arm11
    python3 tools/gen_tex_frames.py /tmp/tex_frames
    mkdir -p /tmp/tex_sd
    cp /tmp/tex_frames/twlhd_frame_v2_tex.bin /tmp/tex_sd/twlhd_frame.bin
    AZAHAR_LIBRETRO=$HOME/3ds/azahar-build-libretro/bin/Release/azahar_libretro.so \
      tools/azahar_host/build/azahar_host \
        --game arm11/twlhd-arm11.3dsx \
        --frames 200 --press A:60-63 --shot 199 \
        --out /tmp/tex_shot --sd-root /tmp/tex_sd
    python3 tools/check_tex_shot.py /tmp/tex_shot_199.ppm --frame tex

The regenerated emulator screenshot (`/tmp/tex_shot_199.ppm`, sha256
`ca1aac5708bebf0235540201ba62545f4f618af6db69ccb82f36fb151dcd9626`) passes
every line including `UL/UR/LL/LR phase OK`, with the same quad bounds and cell
periods as the Run 19 hardware capture. The control capture
(`/tmp/tex_ctl_shot_199.ppm`, sha256
`e4789ed7617ae9d66d1624d49884f9ad2b0ab2107c2963e18a57b339fa59ae7f`) passes
`--frame ctl` (exit 0). UNIT evidence: `tools/test_check_tex_shot.py` covers the
400x240 input, the `--snap` JPEG-noise case (fails without, passes with), a
1-px shifted picture, and the negative cases (mirrored, vertically flipped,
permuted cells, UR different from LL, solid region, wrong control colour).

## Licensing posture (not legal advice)

DeSmuME (GPL-2.0-or-later) is used only as an external program. TWL-HD (Apache-2.0) neither links nor vendors it. Separate repositories alone are
not assumed to settle licence compatibility; see `docs/EMULATOR_TRACER_FEASIBILITY.md`.
