<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4s — `ARM OP_BKPT` on the static `CUBES=2` autodump ROM in the harness

**Scope:** OFFLINE investigation, EMULATOR only. No console, SD card, network,
harness or DeSmuME source was touched. Nothing in `arm9v2/`, `arm11/`,
`common/`, `arm7/`, `tools/azahar_host/` or `tools/fixtures/` is modified by
this report; the only repository additions are this file and three append-only
pointer lines. Build outputs stayed in ignored `arm9v2/build-*` directories;
the scratch ROMs live under `/tmp/opencode/eq4s/` (`<scratch>`).

**Labels.** FACT = read at the cited `file:line`, or a command and its complete
raw output quoted below. SOURCE-READ = a source line that was read but not
executed here. HYPOTHESIS = an inference from facts that was not measured.
UNVERIFIED = no local evidence exists / it was not run. Emulator facts are
facts about the *model*, never about hardware.

**Evidence paths.**
- Harness: `$HOME/3ds/twlhd-desmume-harness/harness`, source
  `$HOME/3ds/twlhd-desmume-harness/src/harness.cpp` (read only).
- ROMs: `<staging>/roms/` with `HWSTAGE` in the shell environment; scratch
  copies and per-variant logs under `/tmp/opencode/eq4s/` (`<scratch>`).
- HEAD of this worktree at the start: `3d48a6f5d4af2d4916b041ba7d39339233b6aa7c`.

---

## 0. Result in one table

Harness invocation: `harness <rom.nds> <frame> --out <txt> --ppm <ppm>`, one
run per ROM variant, from the worktree. Harness frame `N` = ROM frame `N-2`
(`tools/fixtures/eq_scenes/README.md`: frame 242 = ROM frame 240).

| # | ROM variant (build) | ROM SHA-256 | harness frame | exit | outcome |
|---|---|---|---|---|---|
| i | staging `twlhd-arm9v2-autodump-c2-st30.nds` (`CUBES=2 STATIC_ANGLE=30 AUTODUMP_FRAME=240`) | `82e91369…` | 242 | **1** | no 3D frame in 1200 frames, `Undefined instruction 0x0000B909 PC=0x02017310`, then `ARM OP_BKPT triggered` |
| ii | current main `make -C arm9v2 CUBES=2 AUTODUMP_FRAME=240` → `twlhd-arm9v2-autodump-c2.nds` | `703dcad6…` | 242 | **0** | captured frame 242: 12 polygons, 48 vertices |
| iii | base `d21d189` worktree, same build | `703dcad6…` | 242 | **0** | captured frame 242: 12 polygons, 48 vertices |
| iv | `make -C arm9v2 CUBES=2 STATIC_ANGLE=30 AUTODUMP_FRAME=60` | `1ecf41b6…` | 62 | **1** | same fault, same PC `0x02017310` |
| v | `make -C arm9v2 CUBES=2 STATIC_ANGLE=30` (no AUTODUMP) | `43e39eb8…` | 242 | **0** | captured frame 242: 12 polygons, 48 vertices |
| v2 | `make -C arm9v2 CUBES=2 STATIC_ANGLE=31 AUTODUMP_FRAME=240` | `5add7865…` | 242 | **1** | same fault, same PC `0x02017310` |

`82e91369…`/`703dcad6…`/`43e39eb8…` are abbreviated only in this table; the
full hashes and the `sha256sum` commands are in §3. Variants ii and iii are
**byte-identical** ROMs (`703dcad6…`), and that hash equals the committed EQ-3b
fixture ROM (`tools/fixtures/eq_scenes/README.md:48`, `cubes` row).

**One-line verdict (details in §5).** FACT: the fault appears only in the
EQ-4q-introduced static+autodump `CUBES=2` build; it is not caused by the angle
value (`30`/`31`) or the autodump frame (`60`/`240`); before the first 3D flush
the autodump trigger is not reached, while the static angle may or may not have
executed in the first frame — UNVERIFIED; before it, the guest executes
`TAN_LUT` data at `0x02017310`. HYPOTHESIS: a **pre-existing,
binary-layout-sensitive wild jump before the first 3D flush (boot, early main
or the first draw call; location UNVERIFIED) (b)**. The mechanism is UNVERIFIED;
a DeSmuME-side contribution (c) cannot be excluded, and these runs cannot
exclude that EQ-4q merely exposed a latent fault (no quoted run shows a failure
in a ROM built before EQ-4q; variants ii and iii both pass).

---

## 1. SOURCE-READ — the paths that were suspected

### 1.1 The static macro and the `CUBES>1` draw path

`arm9v2/source/demo_frame.cpp:312-316`:

```
#ifdef TWLHD_DEMO_STATIC_ANGLE_DEG
/* Fixed modelview angle for every draw path (STATIC_ANGLE=0..359). */
#define TWLHD_DEMO_STATIC_ANGLE_BIN \
    ((int)((unsigned)TWLHD_DEMO_STATIC_ANGLE_DEG * 32768u / 360u))
#endif
```

The `CUBES>1` branch of `twlhd_demo_draw_frame` is
`arm9v2/source/demo_frame.cpp:449-500`; the loop body with the static angle is
`:478-497`:

```
478:     for (i = 0; i < TWLHD_DEMO_CUBES; i++) {
...
483:         glPushMatrix();
484:         glTranslatef32(x, y, 0);
485: #if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
486:         /* Fixed modelview angle for every draw path. */
487:         glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
488:         glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
489: #else
490:         glRotateXi((int)angle);
491:         glRotateYi((int)angle);
492: #endif
493:         glScalef32(floattof32(0.12), floattof32(0.12), floattof32(0.12));
494:         glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE); /* mirrors the single-cube path */
495:         draw_cube();
496:         glPopMatrix(1);
497:     }
498:
499:     glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
500: #endif
```

SOURCE-READ: the draw path calls `glFlush(0)` exactly once per frame, at
`:499`. A ROM that never flushes therefore never *finishes* its first
`twlhd_demo_draw_frame` call; the static angle is used only inside the loop
that precedes that flush.

### 1.2 The autodump trigger and its guards

`arm9v2/source/main.cpp:38-41` (default is off):

```
/* Opt-in test build: dump the frame automatically (0 disables it). */
#ifndef TWLHD_DEMO_AUTODUMP_FRAME
#define TWLHD_DEMO_AUTODUMP_FRAME 0
#endif
```

`arm9v2/source/main.cpp:240-259` (main loop):

```
240:     while (1) {
...
244:         twlhd_demo_draw_frame((unsigned)angle);
245:         swiWaitForVBlank();
...
253: #if TWLHD_DEMO_AUTODUMP_FRAME > 0
254:         if (frame == TWLHD_DEMO_AUTODUMP_FRAME) {
255:             (void)dump_frame();
256:             iprintf("AUTODUMP @ frame %d (test build)\n",
257:                     TWLHD_DEMO_AUTODUMP_FRAME);
258:         }
259: #endif
```

`arm9v2/source/main.cpp:100-107` (overflow / completeness guards):

```
100:     if ((twlhd_gx_rec_flags(rec) & TWLHD_V2_FLAG_OVERFLOW) != 0u) {
101:         iprintf("frame overflowed; dump refused\n");
102:         return -1;
103:     }
104:     if ((twlhd_gx_rec_flags(rec) & TWLHD_V2_FLAG_FRAME_COMPLETE) == 0u) {
105:         iprintf("no complete frame to dump\n");
106:         return -1;
107:     }
```

SOURCE-READ: both paths return `-1`; neither traps or writes out of bounds.

`arm9v2/source/main.cpp:135-142` — libfat is initialised only inside
`dump_frame`, on first use:

```
135:     /* libfat is initialised on the first dump so boot stays unaffected. */
136:     if (!g_fat_ready) {
137:         if (!fatInitDefault()) {
138:             iprintf("fatInitDefault failed\n");
139:             return -1;
140:         }
141:         g_fat_ready = 1;
142:     }
```

SOURCE-READ: nothing in `main()` calls `fatInitDefault()` unless `dump_frame()`
is called, and `dump_frame()` is called from `main.cpp:250` (KEY_A) or
`main.cpp:255` (the autodump trigger). At boot neither has run.

### 1.3 Nothing in `arm9v2/` or `common/` deliberately traps

```
$ grep -rnE '\bbkpt\b|__builtin_trap|\bassert\s*\(|\babort\s*\(' arm9v2/source common; echo "grep_exit=$?"
grep_exit=1
```

FACT: no match. The only `assert`-like tokens are compile-time
`_Static_assert`/`static_assert` (`common/twlhd_gx_interp.c:16-18`,
`common/twlhd_frame_format.h:60`, `common/twlhd_gx_record.h:79,85`), which emit
no code. There is no guest-side `bkpt`, no `abort()` and no `__builtin_trap`
in the ROM source.

### 1.4 How the harness decides "never presented a 3D frame"

`$HOME/3ds/twlhd-desmume-harness/src/harness.cpp:331-347` (read only):

```
331:    // A flush during this frame sets viewer3D.frameNumber to the DeSmuME frame
...
334:    if (view3d->flushes != flushes_before) {
335:      saw_any_flush = true;
...
345:  if (!captured) {
346:    if (!saw_any_flush) {
347:      fprintf(stderr,
348:              "harness: ROM never presented a 3D frame within %ld frames\n", budget);
```

FACT (harness source): `harness: ROM never presented a 3D frame within 1200
frames` means `saw_any_flush == false`, i.e. **zero** `gfx3d` flushes in the
whole 1200-frame budget — not merely "no non-empty frame". The failing ROM
never reaches the `glFlush(0)` at `demo_frame.cpp:499`, so the fault is before
that flush; whether it is in boot, early `main` or the first draw call is
UNVERIFIED. It also means the run length is the full `budget` (1200 frames),
because the emulator keeps running after the BKPT (bkpt-diagnosis §4).

---

## 2. Method

```
$ git rev-parse HEAD && git rev-parse main
3d48a6f5d4af2d4916b041ba7d39339233b6aa7c
3d48a6f5d4af2d4916b041ba7d39339233b6aa7c
```

Each variant was built once, copied to `<scratch>`, hashed, and run in the
harness **once**; no run was repeated. Commands below are verbatim; the
harness is invoked as `$HOME/3ds/twlhd-desmume-harness/harness <rom> <frame>
--out <txt> --ppm <ppm>` (`tools/fixtures/eq_scenes/README.md:65`, eq4r §3).
The core's own banner is mixed into the same streams as the harness message
(§3.6 of eq4r); the outputs are quoted whole and unedited.

---

## 3. Bisect runs (FACT)

### 3.1 Run (i) — the failing staging ROM

```
$ $HOME/3ds/twlhd-desmume-harness/harness "$HWSTAGE/roms/twlhd-arm9v2-autodump-c2-st30.nds" 242 --out /tmp/opencode/eq4s/v_i/harness_c2_st30.txt --ppm /tmp/opencode/eq4s/v_i/harness_c2_st30.ppm
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

The staging ROM's SHA-256 (FACT):

```
$ sha256sum "$HWSTAGE/roms/twlhd-arm9v2-autodump-c2-st30.nds"
82e913696c003b54525c092f4f2e7f483ec52b805cea58366f85f6054d41f3e4  <staging>/roms/twlhd-arm9v2-autodump-c2-st30.nds
```

A local rebuild from current main reproduces the staging bytes exactly, so the
staging ROM is not a stale build (build only, no second harness run):

```
$ make -C arm9v2 CUBES=2 STATIC_ANGLE=30 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-c2-st30.nds
$ sha256sum arm9v2/twlhd-arm9v2-autodump-c2-st30.nds
82e913696c003b54525c092f4f2e7f483ec52b805cea58366f85f6054d41f3e4  arm9v2/twlhd-arm9v2-autodump-c2-st30.nds
```

### 3.2 Run (ii) — non-static `CUBES=2 AUTODUMP_FRAME=240`, current main

Build and hash:

```
$ make -C arm9v2 CUBES=2 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-c2.nds
$ cp arm9v2/twlhd-arm9v2-autodump-c2.nds /tmp/opencode/eq4s/v_ii/
$ sha256sum /tmp/opencode/eq4s/v_ii/twlhd-arm9v2-autodump-c2.nds
703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15  /tmp/opencode/eq4s/v_ii/twlhd-arm9v2-autodump-c2.nds
```

`703dcad6…` is the committed EQ-3b fixture ROM SHA-256
(`tools/fixtures/eq_scenes/README.md:48`, `cubes` row) — FACT: the current-main
non-static build is byte-identical to the ROM that worked in EQ-3b:

```
$ grep -n 703dcad6 tools/fixtures/eq_scenes/README.md
48:| cubes | `CUBES=2 AUTODUMP_FRAME=240` | `twlhd-arm9v2-autodump-c2.nds` | `703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15` |
```

Run:

```
$ $HOME/3ds/twlhd-desmume-harness/harness /tmp/opencode/eq4s/v_ii/twlhd-arm9v2-autodump-c2.nds 242 --out /tmp/opencode/eq4s/v_ii/harness_c2.txt --ppm /tmp/opencode/eq4s/v_ii/harness_c2.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 12 polygons, 48 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 33A1AD3F
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

FACT: the non-static AUTODUMP+CUBES=2 ROM passes at HEAD, exactly as in EQ-3b.

### 3.3 Run (iii) — the same non-static build at base `d21d189`

A clean detached worktree at the EQ-4q base commit (verified clean), built,
run once, then removed (§7):

```
$ git worktree add /tmp/opencode/eq4s/base-d21d189 d21d189
Preparing worktree (detached HEAD d21d189)
HEAD is now at d21d189 EQ-4p: visible presentation-row correction for azahar output in comparison tools
$ git -C /tmp/opencode/eq4s/base-d21d189 status --porcelain
$ git -C /tmp/opencode/eq4s/base-d21d189 rev-parse HEAD
d21d18953e4452a52a85afc668f5b858a517b73e
$ make -C /tmp/opencode/eq4s/base-d21d189/arm9v2 CUBES=2 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-c2.nds
$ sha256sum /tmp/opencode/eq4s/base-d21d189/arm9v2/twlhd-arm9v2-autodump-c2.nds
703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15  /tmp/opencode/eq4s/base-d21d189/arm9v2/twlhd-arm9v2-autodump-c2.nds
$ cp /tmp/opencode/eq4s/base-d21d189/arm9v2/twlhd-arm9v2-autodump-c2.nds /tmp/opencode/eq4s/v_iii/
$ $HOME/3ds/twlhd-desmume-harness/harness /tmp/opencode/eq4s/v_iii/twlhd-arm9v2-autodump-c2.nds 242 --out /tmp/opencode/eq4s/v_iii/harness.txt --ppm /tmp/opencode/eq4s/v_iii/harness.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 12 polygons, 48 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 33A1AD3F
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

FACT: variant (iii) is byte-identical to (ii) and also passes, so the
non-static AUTODUMP+CUBES=2 combination is not a regression since `d21d189`
(it did not fail there either). `d21d189` predates the `CUBES` static-angle
macro, so a static build cannot exist at this commit; variant (iii) is
necessarily non-static.

### 3.4 Run (iv) — the same static build with `AUTODUMP_FRAME=60`

Justification: the failure in (i) happened before any 3D flush (§1.4), but
only a run can show that the trigger frame is not involved. Same static build,
autodump at ROM frame 60, harness frame 62.

```
$ make -C arm9v2 CUBES=2 STATIC_ANGLE=30 AUTODUMP_FRAME=60
...
built ... twlhd-arm9v2-autodump-c2-st30.nds
$ cp arm9v2/twlhd-arm9v2-autodump-c2-st30.nds /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds
$ sha256sum /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds
1ecf41b6691174d1de25c5e66b1f9916cd6371b539d35603a283d2a87757f8b3  /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds
$ $HOME/3ds/twlhd-desmume-harness/harness /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds 62 --out /tmp/opencode/eq4s/v_iv/harness.txt --ppm /tmp/opencode/eq4s/v_iv/harness.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: ROM never presented a 3D frame within 1200 frames
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: BAFFE0DB
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
BackupDevice: DeSmuME .dsv save file not found. Trying to load a .sav file.
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

FACT: with a different ROM (`ROM crc BAFFE0DB` vs `57BF8A7D`) and a shorter
request, the failure is identical, at the same PC. (The Makefile drops stale
objects when the define set changes — `arm9v2/Makefile:221-230`,
`flags.stamp` — so this is a full rebuild, not a mixed build.)

### 3.5 Run (v) — plain static `CUBES=2`, no autodump

Justification: separates the STATIC variable from AUTODUMP.

```
$ make -C arm9v2 CUBES=2 STATIC_ANGLE=30
...
built ... twlhd-arm9v2-c2-st30.nds
$ sha256sum arm9v2/twlhd-arm9v2-c2-st30.nds
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  arm9v2/twlhd-arm9v2-c2-st30.nds
$ cp arm9v2/twlhd-arm9v2-c2-st30.nds /tmp/opencode/eq4s/v_v/
$ $HOME/3ds/twlhd-desmume-harness/harness /tmp/opencode/eq4s/v_v/twlhd-arm9v2-c2-st30.nds 242 --out /tmp/opencode/eq4s/v_v/harness.txt --ppm /tmp/opencode/eq4s/v_v/harness.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: captured frame 242: 12 polygons, 48 vertices (first non-empty 3D frame: 2)
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: B3240B7C
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
BackupDevice: DeSmuME .dsv save file not found. Trying to load a .sav file.
CPU mode: Interpreter
File doesn't appear to have a secure area.
WIFI: MAC Address = 00:09:BF:12:34:56
WIFI: Emulation level is OFF.
$ echo "exit=$?"
exit=0
```

The rebuilt plain static ROM is byte-identical to the staging plain ROM
(`<staging>/roms/twlhd-arm9v2-c2-st30.nds`, FACT, same hash as the local build
quoted above):

```
$ sha256sum "$HWSTAGE/roms/twlhd-arm9v2-c2-st30.nds"
43e39eb87558fb1019699c33d8cd333ecf191ba747532bf163fabfc4b4ec2940  <staging>/roms/twlhd-arm9v2-c2-st30.nds
```

FACT: STATIC+CUBES=2 without AUTODUMP passes; the autodump build variable is
required for the failure.

### 3.6 Run (v2) — static autodump `CUBES=2` with `STATIC_ANGLE=31`

Justification: the identical fault across `STATIC_ANGLE=30` and `31` separates
the macro's *value* from the macro's *presence*; it does not exclude the macro's
presence or code shape.

```
$ make -C arm9v2 CUBES=2 STATIC_ANGLE=31 AUTODUMP_FRAME=240
...
built ... twlhd-arm9v2-autodump-c2-st31.nds
$ cp arm9v2/twlhd-arm9v2-autodump-c2-st31.nds /tmp/opencode/eq4s/v_v_st31/
$ sha256sum /tmp/opencode/eq4s/v_v_st31/twlhd-arm9v2-autodump-c2-st31.nds
5add7865155d70af9ec7757bc8777ab448b15a2a0c82d85e3a9a44ef25b379c9  /tmp/opencode/eq4s/v_v_st31/twlhd-arm9v2-autodump-c2-st31.nds
$ $HOME/3ds/twlhd-desmume-harness/harness /tmp/opencode/eq4s/v_v_st31/twlhd-arm9v2-autodump-c2-st31.nds 242 --out /tmp/opencode/eq4s/v_v_st31/harness.txt --ppm /tmp/opencode/eq4s/v_v_st31/harness.ppm
xcb_connection_has_error() returned true
xcb_connection_has_error() returned true
Failed to set format: Invalid argument
harness: ROM never presented a 3D frame within 1200 frames
Microphone init failed.
DeSmuME 0.9.14 git#0 x64-JIT SSE2
SoftRasterizer: Running using 16 additional threads. (Multithreading enabled.)

ROM game code: ####
ROM crc: 2088FB41
ROM developer: Homebrew

Slot1 auto-selected device type: Retail MC+ROM
Slot2 auto-selected device type: PassME (0x08)
BackupDevice: DeSmuME .dsv save file not found. Trying to load a .sav file.
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

### 3.7 What the fault address is (SOURCE-READ/FACT on the built ELF)

`arm-none-eabi-addr2line` and `objdump` on
`arm9v2/twlhd-arm9v2-autodump-c2-st30.elf` (read during the EQ-4s runs; the
artefact is not present in the working tree at repair time), which is the
variant (iv) ELF
(`CUBES=2 STATIC_ANGLE=30 AUTODUMP_FRAME=60`, §3.4): the frame-240 (i) ELF was
overwritten by the later build under the same output name. The fault PC
`0x02017310` is identical in runs (i) and (iv) (§3.1, §3.4).

```
$ arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c2-st30.elf 0x02017310
__free_from_arm
??:?
$ arm-none-eabi-objdump -d --start-address=0x020172f0 --stop-address=0x02017330 arm9v2/twlhd-arm9v2-autodump-c2-st30.elf
...
Disassembly of section .main:

020172f0 <TAN_LUT+0xac>:
 20172f0:	33 95 00 00 70 99 00 00 be 9d 00 00 1c a2 00 00     3...p...........
 2017300:	8b a6 00 00 0d ab 00 00 a2 af 00 00 4b b4 00 00     ............K...
 2017310:	09 b9 00 00 dc bd 00 00 c6 c2 00 00 c8 c7 00 00     ................
 2017320:	e3 cc 00 00 18 d2 00 00 67 d7 00 00 d3 dc 00 00     ................g.......
```

The same address range in the variant (v2) f240 ELF
(`arm9v2/twlhd-arm9v2-autodump-c2-st31.elf`, §3.6):

```
$ arm-none-eabi-nm -n arm9v2/twlhd-arm9v2-autodump-c2-st31.elf | awk '$1>="02017000" && $1<="02017600"'
02017008 t g_rsta_ranges
02017048 t texel_eighths.1
02017068 t palette_by_format.0
02017088 t CSWTCH.30
020171c4 t dotab_stdout
02017244 T TAN_LUT
02017448 T SIN_LUT
0201754c T default_font_bin
```

SOURCE-READ/FACT about the model: in the variant (iv) ELF, `0x02017310` is
`TAN_LUT+0xCC`, a data word `0x0000B909`; the harness's `THUMB9: Undefined
instruction: 0x0000B909 PC=0x02017310` is the CPU executing that half-word as
Thumb code. `addr2line` maps it to `__free_from_arm` only as the nearest
preceding function symbol, and the quoted block shows `__free_from_arm` /
`??:?` with no file:line — i.e. there is no real function at that address. The
passing plain static (v) ELF's `TAN_LUT` is quoted below; the jump target is a
**data address** in both builds, and in the runs the failing
static+autodump builds execute it while the passing builds do not (§3.1-§3.6).
That the same `TAN_LUT+0xCC` offset is also the target in the frame-240 (i)
image specifically is INFERENCE/HYPOTHESIS: only the PC (`0x02017310`) is common
to (i) and (iv), and the (i) ELF was overwritten before it could be read.

```
$ arm-none-eabi-nm -n arm9v2/twlhd-arm9v2-c2-st30.elf | awk '$1>="02017200" && $1<="02017600"'
02017230 T TAN_LUT
02017434 T SIN_LUT
02017538 T default_font_bin
```

### 3.8 The image difference between the passing and failing static builds

Plain static `c2` (`43e39eb8…`) vs the static autodump build (`1ecf41b6…`),
first 512-byte header window and whole-file byte count:

```
$ cmp -l <(head -c 512 /tmp/opencode/eq4s/v_v/twlhd-arm9v2-c2-st30.nds) <(head -c 512 /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds); echo "cmp_exit=$?"
                 45 114 144
                109  75 323
                110 331 147
                351  12 117
                352 236   3
cmp_exit=1
$ cmp -l /tmp/opencode/eq4s/v_v/twlhd-arm9v2-c2-st30.nds /tmp/opencode/eq4s/v_iv/twlhd-arm9v2-autodump-c2-st30-f60.nds | wc -l
94286
```

FACT: the two static builds differ in the header (offset `44` decimal = `0x2C`,
the ARM9 size field, and four checksum-adjacent bytes) and in `94286` file
bytes; the failing build is not the passing build plus a runtime flag.

---

## 4. Attribution

### 4.1 FACT (from the quoted runs and source)

1. The staging static autodump `CUBES=2` ROM fails exactly as EQ-4r recorded:
   `THUMB9/ARM9: Undefined instruction: 0x0000B909 PC=0x02017310`,
   `armcpu_exception!`, `ARM OP_BKPT triggered`, exit 1, no PPM (§3.1). The
   surviving EQ-4r log has the same lines and `ROM crc: 57BF8A7D`:

   ```
   $ grep -nE 'crc|Undefined|BKPT' <scratch-eq4r>/hlog/harness_c2.log; echo "exit=$?"
   10:ROM crc: 57BF8A7D
   19:THUMB9: Undefined instruction: 0x0000B909 PC=0x02017310
   20:ARM9: Undefined instruction: 0x0000B909 PC=0x02017310
   22:ARM OP_BKPT triggered
   exit=0
   ```
2. The non-static autodump `CUBES=2` ROM is **byte-identical** to the EQ-3b
   fixture ROM (`703dcad6…`) and passes at HEAD and at base `d21d189`
   (`12 polygons, 48 vertices`, §3.2-§3.3).
3. The plain static `CUBES=2` ROM passes (`12 polygons, 48 vertices`, §3.5).
4. The failing ROM never flushes (`harness: ROM never presented a 3D frame
   within 1200 frames` = zero `saw_any_flush`, §1.4), so the fault precedes
   the first `glFlush(0)` (`demo_frame.cpp:499`); the autodump trigger
   (`main.cpp:254`) is not reached (it follows the draw call at
   `main.cpp:244`); whether the static-rotation loop of the first frame ran
   before the fault is UNVERIFIED.
5. Two different autodump frames (`240` vs `60`) and two different static
   angles (`30` vs `31`) fail with the identical fault PC (§3.4, §3.6). The
   fault is therefore independent of the trigger frame and of the angle value.
6. In the variant (iv) ELF the fault PC is inside `TAN_LUT` data, not a
   function (§3.7); the same location in the frame-240 (i) image is HYPOTHESIS
   (only the PC `0x02017310` is common to (i) and (iv)).

### 4.2 HYPOTHESIS

- HYPOTHESIS: the fault is a **pre-existing, binary-layout-sensitive latent
  wild jump before the first 3D flush (boot, early main or the first draw
  call; location UNVERIFIED)**. The build variable that flips it is
  `AUTODUMP_FRAME=240` **on top of** `STATIC_ANGLE=30 CUBES=2`; neither alone
  fails (§3.2, §3.5). The added autodump block changes `main`'s code and
  therefore the linked image (FACT: the two static images differ in the header
  size field and in `94286` bytes, §3.8), which shifts the image; a latent bug
  then branches into `TAN_LUT`.
- The fault class matches the one documented in `docs/reports/bkpt-diagnosis.md`
  §3.1/§4: a wild branch in the newlib/libnds boot path whose trigger depends
  on the scene-specific binary layout, reached through `consoleDemoInit()`
  (`arm9v2/source/main.cpp:210`) → libnds `consoleInit` → newlib `setvbuf` →
  calico rmutex code. Here the visible shape differs (the CPU executes data at
  `0x02017310` rather than branching to 0), so the earlier measurement does not
  prove this mechanism; it is an HYPOTHESIS only.

### 4.3 UNVERIFIED

- The exact instruction/return address that branches to `0x02017310`. No
  instrumented DeSmuME (or gdb-stub) build was available, and the task forbids
  touching the DeSmuME trees / creating a new scratch build. A PC ring or
  register dump at the first `PC==0x02017310` would settle it.
- Whether hardware runs this exact ROM (the static **autodump** image). The
  owner's hardware run was the **non-autodump** static ROM
  (`twlhd-arm9v2-c2-st30.nds`), which also passes in the harness (§3.5), so it
  neither confirms nor contradicts the emulator for the failing image.
- Whether a rebuilt `d21d189` *static* ROM would fail: impossible, the
  `CUBES` static macro does not exist at that commit.

---

## 5. Verdict (a / b / c)

**FACT: the fault appears only in the EQ-4q-introduced static+autodump `CUBES=2`
build; it is not caused by the angle value (`30`/`31`) or the autodump frame
(`60`/`240`); before the first 3D flush the autodump trigger is not reached,
while the static angle may or may not have executed in the first frame —
UNVERIFIED. HYPOTHESIS: a pre-existing, binary-layout-sensitive wild jump before
the first 3D flush (boot, early main or the first draw call; location
UNVERIFIED), (b).** (a) is excluded as a semantic cause and (c) is left
UNVERIFIED. These runs cannot exclude that EQ-4q merely exposed a latent fault:
no quoted run shows a failure in a ROM built before EQ-4q, variants (ii) and
(iii) both pass, and only the static+autodump builds introduced in EQ-4q fail.

- **(a) regression of EQ-4q's macro: NO (as a code defect).** The plain static
  `CUBES=2` ROM runs the same static-rotation code and passes (§3.5), and the
  failure is identical for `STATIC_ANGLE=30` and `31`; that 30/31 result does
  not exclude the macro's presence or code shape. What EQ-4q/EQ-4r *did* add is
  a new ROM variant (static + autodump); that variant is the binary layout that
  reproduces the fault — the "which change flips it" FACT.
- **(b) problem of the AUTODUMP+CUBES=2 combination or of the invocation: NO
  for the combination as such, NO for frame/run length; HYPOTHESIS: a latent
  fault predating EQ-4q.** The non-static AUTODUMP+CUBES=2
  ROM passes and is byte-identical to the EQ-3b fixture ROM, at HEAD and at
  `d21d189`; the failing ROM fails identically at harness frame 242 and 62 with
  autodump 240 and 60. No quoted run shows this fault in a ROM built before
  EQ-4q, so whether it predates EQ-4q or was introduced/exposed by it remains
  UNVERIFIED.
- **(c) harness/DeSmuME limitation: UNVERIFIED, not needed to explain the
  observations.** The visible failure is DeSmuME executing guest data after a
  wild jump; that class of failure was shown in bkpt-diagnosis to be sensitive
  to emulator-side load state, so a DeSmuME contribution is plausible. But no
  quoted run here requires it, and there is no hardware run of the failing ROM.

"Which change flips it" is FACT (adding `AUTODUMP_FRAME` to the static
`CUBES=2` build). "Why" is HYPOTHESIS/UNVERIFIED (a binary-layout-sensitive
wild jump before the first 3D flush (boot, early main or the first draw call; location UNVERIFIED); the mechanism is not
measured).

---

## 6. Proposed fix / workaround (PROPOSED, NOT APPLIED)

**No `arm9v2` diff is justified by the evidence.** The bisect does not show a
defect in the static-angle computation or in the autodump logic; the failing
ROM crashes before the first `glFlush(0)` (`demo_frame.cpp:499`); the autodump
trigger (`main.cpp:254`) is not reached (it follows the draw call at
`main.cpp:244`), and whether the static-rotation loop of the first frame ran
before the fault is UNVERIFIED. Inventing a source change to "fix the layout"
would be a guess.

**Workaround for the EQ compare (proposed, not implemented):** produce the
`c2` static harness reference from the **non-autodump** static ROM
`twlhd-arm9v2-c2-st30.nds` (build `make -C arm9v2 CUBES=2 STATIC_ANGLE=30`,
sha `43e39eb8…`), which passes at frame 242 with 12 polygons / 48 vertices
(§3.5). The draw path and the fixed `STATIC_ANGLE` are identical; only the
autodump trigger (a file-dump side effect at ROM frame 240) differs, and the
harness reference is the frame-242 geometry/PPM, which the trigger does not
affect. Keep the static-**autodump** `c2` harness cell NOT-COMPARABLE (as EQ-4r
already does) instead of treating it as a pass, and add the plain-static
reference as the substitute harness pair. The existing `c2` PASS against "ours"
(v2 capture + Azahar/PICA render, EQ-4r §9) is unaffected.

If the static-autodump ROM itself must run in the harness, the untested
candidate is to remove the layout difference by making the autodump call site
independent of the build define (e.g. a runtime-initialised frame variable
instead of a compile-time `#if`); this is HYPOTHESIS, not evidence, and is not
proposed as a change.

---

## 7. Repository hygiene (FACT)

- No helper script was needed: the investigation is build + one harness run per
  variant + `sha256sum`/`addr2line`; no parser or fixture is added, so
  `tools/eq4s_c2_bkpt/` is intentionally absent and no test wiring is added.
  `make test` is therefore unchanged; no existing test was weakened.
- The base worktree was removed after variant (iii):

```
$ git worktree remove --force /tmp/opencode/eq4s/base-d21d189; echo "remove_exit=$?"
remove_exit=0
```

After removal, `git worktree list` no longer contains
`/tmp/opencode/eq4s/base-d21d189`:

```
$ git worktree list | grep -c base-d21d189; echo "exit=$?"
0
exit=1
```

- `git status --porcelain` before writing this report was empty; the build
  outputs (`arm9v2/build-*`, `*.nds`, `*.elf`, `*.guard*`, `*.map`) are ignored
  by `.gitignore`.
- Commit guard: all added repository text uses `$HOME`, `<staging>` and
  `/tmp/opencode/eq4s/` (`<scratch>`); no home directory, user name, host name,
  key or copied proprietary text is added.

Diff budget (`< 90000` required):

```
$ git add -N . && git diff main | wc -c
39246
```

Build artifacts are untracked/ignored, so the counted text is this report plus
the three append-only pointer lines.
