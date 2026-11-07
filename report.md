# Implementation Report

## Status
IMPLEMENTED (BUILD/UNIT only; no console connection was made)

## Files Changed
- `Makefile`: added `ntr-capture` to `.PHONY`; added one `HOST_TESTS` entry
  plus the `tests/host/build/test_ntr_capture_util` rule (header-only, same
  `HOST_CFLAGS`/ASan/UBSan); added the opt-in `ntr-capture` target after the
  `azahar-host` block (with `NTRVIEWER_DIR` and the documented libjpeg
  fallback); added `rm -rf tools/ntr_capture/build` to `clean`. `all:` and the
  rest of `test:` are untouched.
- `tools/ntr_capture/ntr_capture_util.h` (new): header-only TCP command
  builders (heartbeat / remote-play 901), UDP header parser, per-screen
  reassembly FSM, 90° CCW rotation, P6 PPM header builder.
- `tools/ntr_capture/ntr_capture.c` (new): CLI, dry-run hex path, TCP
  connect/send, UDP bind/receive loop, libjpeg decode, rotation, PPM write,
  timestamped stderr logging, exit codes 0/1/2.
- `tools/ntr_capture/README.md` (new): build/run, protocol with viewer
  `file:line` citations, dry-run hex vs viewer bytes, UNVERIFIED items, limits,
  licence note, console-policy reference, mutation table.
- `tests/host/test_ntr_capture_util.c` (new): 11 test groups with literal
  expectations (dropped packet, duplicate, out-of-order, last packet, 3×2
  rotation, PPM header, TCP bytes).
- `report.md` (this file).

## Behavior Implemented
- Connects TCP to `host:8000`, sends the viewer's exact two packets: heartbeat
  `cmd 0 seq 0`, then remote-play `cmd 901 seq 1` (84 bytes each, little-endian
  struct layout from `ntr_hb.c:9-19,160-179,327,377-395`).
- `--screen top|bottom|both` maps to `args[0] = (top_priority << 8) | factor`
  (top/bottom force factor 0, both uses `--priority-factor`, default 1);
  `--quality` → `args[1]`; `--bandwidth` → `args[2] = Mbps*128*1024`;
  `args[3] = 1404036572`; `args[4] = udp_port`; args 5/6 zero (audio/full
  width off).
- Binds UDP 8001 (configurable), reassembles top/bottom independently with the
  viewer's 4-byte header semantics: identity is bytes 0-2 with the end bit
  cleared, non-end packets must be 1444 bytes, packet numbers wrap by 256,
  missing packet at the end discards the frame, duplicates are ignored.
- Decodes JPEG to RGB with system libjpeg-turbo, validates 240×400 (top) /
  240×320 (bottom), rotates 90° CCW (same orientation as the viewer's SDL
  `-90` at `ui_renderer_sdl.c:450`), writes `P6` files.
- `--dry-run` builds and prints both 84-byte packets and ports without opening
  any socket, exit 0.
- Usage errors exit 2 (one line); runtime errors exit 1 (timestamped one line);
  every console-facing action logs a timestamped stderr line.
- No stop command exists in the viewer; the client logs that it closes the
  sockets only (`ntr_hb.c:26-37,205-213`).

## Verification
1. `make ntr-capture` — clean build, no warnings:
   ```
   note: /home/one/3ds/ntrviewer-hr/jpeg_turbo needs nasm (missing) ...
         using system libjpeg-turbo
   cc -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L ... -ljpeg
   ```
2. `make test` — exit 0; the new test runs inside the host suite:
   ```
   == tests/host/build/test_ntr_capture_util
   ok tcp/remote-play ... ok ppm/header
   11 test(s) passed
   ...
   Ran 86 tests in 40.514s
   OK
   ```
3. Mutation table: 12 mutants (2 per helper), each applied to a copy of the
   header, unchanged test rebuilt with the same flags, `make test` red
   (`rc=2`) every time; header restored, `git diff` of the header empty,
   baseline green. Full verbatim first-failure lines are in
   `tools/ntr_capture/README.md` ("Mutation evidence").
4. Synthetic end-to-end (localhost only, no console): fake TCP server on
   127.0.0.1:18000 received exactly 168 bytes starting
   `78 56 34 12 ...` and `85 03 00 00`; fake UDP sender delivered 240×400 and
   240×320 JPEG frames; client exited 0 and wrote `cap_top_0.ppm` (400×240)
   and `cap_bottom_0.ppm` (320×240). Sampled pixels matched the CCW mapping
   within JPEG tolerance, e.g. top `(10,5)` got `(233,10,127)` vs expected
   `(234,10,128)`.
5. `--dry-run` exit 0, no sockets; usage errors exit 2 (`--host` missing, bad
   IPv4, quality 5); dry-run hex matches the viewer expressions byte for byte.
6. `make -n test | grep -i ntr-capture` empty; `make -n clean` shows
   `rm -rf tools/ntr_capture/build`; `git status` shows only the intended new
   files and the Makefile edit after removing the untracked build directory.
   No `.tmp_linktest.c`, `.orig` or `.rej` files exist.

## Deviations
1. The contract names `/tmp/twlhd-ntr`; this worker's assigned worktree is
   `/tmp/codex-20261107-151104-888446/worktree` (same repository/baseline
   `6e71619`). All edits are in the assigned worktree.
2. `HOST_TESTS` received exactly one added line for the new test; acceptance
   criterion 1 requires `make test` to run it. No other list entry changed.
3. The viewer's bundled `jpeg_turbo` cannot build standalone here: `nasm` is
   absent and `jpeg_turbo/jconfig.h:22` hard-codes `WITH_SIMD 1` with no
   no-SIMD switch. The target falls back to the system libjpeg-turbo headers as
   the contract allows, prints the exact reason, and fails loudly if the
   headers are missing. Nothing from the viewer is vendored.
4. Attempt 1's worktree (`/tmp/codex-20261107-142051-695678`) was
   permission-denied, so no attempt-1 code or bytes were reused. All protocol
   bytes were re-derived from `/home/one/3ds/ntrviewer-hr`.

## Blockers / Risks
- No console run: acceptance of the exact command order, real UDP frame ids,
  downsample bits, and JPEG dimensions remain UNVERIFIED (flagged in the
  README). A real run should use `--dry-run` first, then `--frames 1`.
- Downsample 2/3 frames are skipped with a log line; only full-resolution
  JPEG Compat is implemented.
- The output orientation mirrors the viewer's SDL renderer; the other viewers
  were checked by inspection, not by comparing PPMs with a running viewer.
- The synthetic end-to-end test and mutation harness live outside the repo
  (`/tmp/opencode/*`); only their results are recorded here.
