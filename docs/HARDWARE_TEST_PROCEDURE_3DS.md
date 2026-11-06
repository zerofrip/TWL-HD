# New 3DS — Triangle-Only Hardware Test: Final Procedure (FOR APPROVAL)

> **Status update (later; the text below is the original Run 1 procedure and is kept as written).**
> The owner ran this and later builds on the console themselves; results are recorded in section 12 and in
> `docs/PHASE4_HARDWARE_FINDINGS.md`. The assistant never connected to or executed anything on the console.
> Sections 6, 9 and the "HARDWARE UNVERIFIED" wording below describe the situation BEFORE those runs. Also historical: the staged copy
> `/home/one/3ds/hwtest-staging/twlhd-arm11.3dsx` and the `SHA256SUMS` statements in sections 2 and 8 (the file was later moved to
> `run2_artifacts/twlhd-arm11-run1-HUNG.3dsx`, and `SHA256SUMS` now lists only the newest artifacts), and the "staged copy remains" text in section 8.

**Status: PREPARED, NOT APPROVED. Nothing has been connected to, uploaded to, or executed on
the console.** The owner authorised *preparation only*. This document is the procedure that
needs explicit approval before the owner copies anything to the SD card.

Companion audit: `docs/HARDWARE_VALIDATION_3DS.md` (evidence and risks).
Everything about this app on a console is **HARDWARE UNVERIFIED** until a run is observed.

Evidence labels: **BUILD** (measured from the build here), **LOCAL** (read from this
repository), **UNVERIFIED** (assumption or general knowledge not checked here).

## 1. What this test is — and is not

| This test **is** | This test is **not** |
|---|---|
| One launch of the Phase 4B renderer showing its **built-in deterministic triangle** | Loading any capture file (no `twlhd_frame.bin` is placed on the SD card) |
| Copy by the owner with a card reader, run from the Homebrew Launcher, exit with `START` | Any network transfer, `3dslink`, FTP, or the network loader. **No connection to the console is made by me** |
| Observation of a static image and some text | RTCom, ucode injection, TWL_FIRM, TwlBg, DS/DSi software, CIA/title installation, firmware changes |

Safety gates, all unchanged: `TWLHD_RTCOM_UCODE_ENABLED` is `0` (`arm7/source/main.c:21`) —
and the ARM7/DS-side code is not part of the artifact at all. The only RTCom-related symbol
in the 3DS image is `rtcom_transport_init`, a stub that returns `NOT_IMPLEMENTED` and touches
no hardware (BUILD, `nm`). The artifact contains no `ucode`, `TwlBg`, `.cxi` or FIRM strings
(BUILD, `strings`).

## 2. The artifact (BUILD)

| Item | Value |
|---|---|
| File | `twlhd-arm11.3dsx` |
| Staged copy (outside the repository) | `/home/one/3ds/hwtest-staging/twlhd-arm11.3dsx` |
| Size | 165,640 bytes |
| SHA-256 | `3198426913e9bc38c2e011ff299f596e2b517ba739b843e3ce304506f10dcb33` |
| Checksum file | `/home/one/3ds/hwtest-staging/SHA256SUMS` (verified with `sha256sum -c`) |
| Built from | `main` at merge commit `c540fe2`; `arm11/` is unchanged by every branch pending merge |
| Reproducibility | three consecutive clean builds produced the identical hash; 0 warnings |

Re-verify on the owner's side before copying: `sha256sum -c SHA256SUMS` in the staging
directory must print `twlhd-arm11.3dsx: OK`.

## 3. SD-card destination

- Copy to: **`/3ds/twlhd-arm11.3dsx`** on the SD card (create `/3ds` if it does not exist).
  Placing a `.3dsx` there is the standard way the Homebrew Launcher lists it (**UNVERIFIED**
  for this console's launcher version).
- **Before the run, confirm there is no `twlhd_frame.bin` in the SD root.** Its absence is what
  keeps this a triangle-only test.
- Copy nothing else.

## 4. Pre-checks (owner)

1. Battery above ~50 % or the charger connected.
2. The Homebrew Launcher already starts and runs another app normally (this is the baseline;
   if it does not, stop — the problem is unrelated to this project).
3. The checksum matches (section 2).

## 5. Procedure

| Step | Action | Expected observation |
|---|---|---|
| 1 | Power off, remove the SD card, copy `twlhd-arm11.3dsx` to `/3ds/`, reinsert, power on | normal boot |
| 2 | Open the Homebrew Launcher and start **twlhd-arm11** (do **not** open the network loader) | the app starts within a few seconds |
| 3 | Look at the **top screen** | see section 6 |
| 4 | Look at the **bottom screen** | see section 6 |
| 5 | Wait about 10 seconds. Do **not** press `A` or `Y` | the image does not change (static) |
| 6 | Press `START` | the app exits cleanly back to the Homebrew Launcher |

Run it once. Whatever the outcome, stop, write down what was on the screen (a photo of each
screen helps), and report before any second attempt.

## 6. Expected output (LOCAL, from `arm11/source/main.c` and `renderer.c`)

**Top screen (400×240, 3D disabled):** a dark blue-grey background (clear colour
`0x101018`) with **one triangle**, static, no animation. The three vertices are red, green
and blue (red `(-0.8,-0.6)`, green `(0.8,-0.6)`, blue `(0.0,0.8)`, all at z = 0), and the
colour is blended smoothly across the face. It is drawn through a fixed *debug* camera that
looks at the triangle from an **oblique** direction (about 36° off the straight-on axis:
direction `(0.6, 0.4, 1.0)`, 45° field of view, distance 2.8 × bounding radius, face culling
off). So expect the triangle to look **centred but skewed/foreshortened**, with red on the
left, green on the right and blue toward the top (the left/right and up/down ordering is
kept; exact proportions are not a pass/fail criterion). The on-screen arrangement is read from
the source and has never been seen on a console (**UNVERIFIED**).

**Bottom screen (text console), exactly:**

```
TWL-HD Phase 4B/4C renderer
HARDWARE UNVERIFIED (build-only)

mode:   BUILT-IN TRIANGLE
source: built-in
camera: DEBUG CAMERA (fixed, not DS)

parse:  -- (no SD frame)
frame:  --  cmds: --
input verts: 3  tris: 1
render verts: 3
unsupported: mtx 0 vp 0 vtx 0 unk 0
ignored: 0 outside-begin
dropped: 0 incomplete

A=load SD  Y=builtin  START=exit
RTCom transport: not implemented
ready
```

The "HARDWARE UNVERIFIED" line is printed by the program and is unchanged by a successful
run; success is judged by observation, not by that line.

**Failure signatures that are still informative:**

| Observation | Meaning |
|---|---|
| bottom screen shows `mode:   RENDERER FAILED` and a last line `renderer: <reason>` | an init step failed on the device; the reason string names it (for example `C3D init failed`, `render target failed`, `shader load failed`) |
| top screen stays black but the text appears | GPU path problem; the text confirms the app itself is running |
| everything frozen | possible GPU/GSP synchronisation fault — see section 8 |

## 7. Buttons

| Button | Behaviour | In this test |
|---|---|---|
| `START` | exit to the launcher | **use** (step 6) |
| `A` | tries to read `sdmc:/twlhd_frame.bin` | **do not press** (out of scope; with no file it only shows an error message) |
| `Y` | back to the built-in triangle | not needed (already showing it) |
| `HOME`, `POWER` (short press) | handled by the system, **not** by this app: they suspend the app and resume it, and a resume re-runs libctru's `ptm:sysm` call (audit, section 4.2) | **do not press during the test** |
| anything else | the app has no handler for it | not needed |

## 8. Hang and recovery procedure

Treat as a hang if the screen is frozen or black **and** `START` does nothing for about
10 seconds.

1. Hold the **POWER** button for about 6 seconds until the console powers off (standard 3DS
   force-off; **UNVERIFIED** for this unit).
2. Wait ~10 seconds, then power on normally. No repair is expected: this project's code
   writes nothing to the SD card or to system storage (see the audit, section 4).
3. If the console shows an error or crash screen (for example from its custom firmware's
   exception handler), **photograph it and do not dismiss repeatedly**. Any dump file the
   system writes is the system's, not this app's; keep it and report it.
4. Optionally remove `/3ds/twlhd-arm11.3dsx` from the SD card (card reader) and the staged
   copy remains in `/home/one/3ds/hwtest-staging/`.
5. **Stop and report.** Do not retry until the result has been reviewed: a hang is itself an
   important result about the renderer's GPU synchronisation (audit, risk 1).

If the console does not boot at all after a force-off, that cannot be caused by this app
writing to system storage (it does not), but report it immediately with the symptoms.

## 9. Risks that remain (honest)

1. **The GPU/GSP synchronisation is unproven on hardware** (reasoned from upstream citro3d
   source). A hang requiring the force-off above is possible.
2. **The linked binary is more than this project's code.** libctru's runtime calls `ptm:sysm`
   at start-up/resume (`__ctru_speedup_config`, expected argument `0`) and initialises the
   infrared-rst service via `hidInit`; libctru's SD driver with its write paths is present although
   this project's code only ever selects its read-only branch (audit, section 4.2). The
   on-device effects of all of these are **UNVERIFIED**.
3. **Homebrew runs with the Homebrew Launcher environment's access.**
4. The 48-byte vertex stride with 28 bytes loaded is unconfirmed on hardware.

## 10. Evidence to keep and how results will be labelled

Photos of both screens, the observed text, the time to a visible triangle, and the outcome of
`START`. Only what is observed is recorded as **HARDWARE VERIFIED**, and only for this
console, this firmware and this build hash. Anything not observed stays **HARDWARE
UNVERIFIED**. Results are kept separate from **BUILD**, **UNIT** (host tests) and
**EMULATOR** labels.

## 11. What is being asked

Please approve (or amend) **this exact procedure**:

1. The artifact in section 2 (hash above), copied by you with a card reader to `/3ds/`.
2. A single run of the triangle-only test (sections 5–7).
3. The recovery procedure (section 8) and the remaining risks (section 9).

Until you approve it, nothing is sent to, copied onto, or run on the console, and no
connection to `192.168.1.32` is made.

## 12. Observed results (recorded as reported; append-only)

Labels in this section: **HARDWARE OBSERVED** = seen by the project owner on the console and
supplied as a photograph. It is not independently reproducible by me, and I did not
connect to or run anything on the console myself.

### Run 1 — owner-run, photograph of the bottom screen (2026-10-29)

Evidence file (kept outside the repository): `/home/one/3ds/hwtest-staging/evidence/run1_bottom_screen.jpg`
(3,671,283 bytes, SHA-256 `1f38283bac29e470810f1bc4916dd093eb2b9bdb5dff91da31d4b1ff65fcfcc7`).

**Observed (HARDWARE OBSERVED, photograph):** the bottom-screen console text is identical,
line for line, to the expected block in section 6, including the first line
`TWL-HD Phase 4B/4C renderer`, `mode:   BUILT-IN TRIANGLE`, `input verts: 3  tris: 1`,
`render verts: 3`, `RTCom transport: not implemented`, and the last line `ready`.

**What this establishes (observation + source reading):** the app started on the console and
`renderer_init()` returned success — in `arm11/source/main.c` the status `ready` is printed
only on that path, while a failure prints `mode:   RENDERER FAILED`. So citro3d
initialisation, render-target creation, shader load, uniform lookup and the vertex-buffer
allocation did not fail on this console.

**Not established (remains HARDWARE UNVERIFIED):**
- that the triangle was drawn on the top screen (not in the photograph);
- that the image stayed stable (the GPU-synchronisation risk, audit risk 1);
- that `START` returned to the launcher cleanly;
- which build was run (the hash of the copied file was not reported);
- any behaviour with a capture file, error paths, or longer running.

Questions put to the owner: top-screen appearance, stability, exit behaviour, hash of the
file used, which buttons were pressed.

### Run 1 — follow-up report by the owner (HARDWARE OBSERVED, text report, 2026-10-29)

The owner stated they ran the build with SHA-256 `3198426913e9bc38c2e011ff299f596e2b517ba739b843e3ce304506f10dcb33` (the owner's statement; not independently confirmed from the console): **the top screen stayed black,
the buttons did nothing, and a forced power-off (POWER hold) was needed. The console booted normally afterwards.**
This answers the open points above for that build: the triangle was **not** displayed, and the app did not stay responsive.
(Audit risk 1, a hang requiring a hard power-off, did occur. **Inference** from the later HARDWARE OBSERVED diagnostic results, not a direct observation: its cause was not the GPU-synchronisation scenario assumed in the audit — see below.)

### Runs 2–6 — owner-run control, diagnostic and fixed builds (HARDWARE OBSERVED, photographs; summary)

The evidence table, root cause and labels are in `docs/PHASE4_HARDWARE_FINDINGS.md`. Run 2 also re-showed the original symptom (the original build and a
build with a vertex-buffer cache flush both hung). Artifact hashes (of the files the assistant staged for each run, run by file name) and photographs are
kept **outside** the repository in the owner's staging area (`/home/one/3ds/hwtest-staging/`: `RUN2_RESULTS.md`, `evidence/`, `run*_artifacts/`). Hashes:

| Run | Artifact (SHA-256 prefix) |
|---|---|
| 1 | original app `3198426913e9…` (owner-stated) |
| 2 | official `simple_tri` control `f6cef9a1…`; diag v1 `4bd2297f…`; original + cache flush `064017f7…` |
| 3 | diag v2 `6dffde4f…` |
| 4 | diag v3 `f2f8c25b…` |
| 5 | diag v5 `a34c4d9b…` |
| 6 | fixed production app `2b824245…`; diag v6 `7d73ac64…` |

In short:
- An unmodified official `simple_tri` control rendered correctly, so the console, launcher and citro3d were not the problem.
- Diagnostic builds with a non-blocking GPU-completion probe showed every CPU-side call returning while the first submitted GPU frame never completed.
- Toggling features one at a time on the console isolated the cause to the vertex attribute buffer: declared attribute bytes (28) did not equal the
  48-byte vertex stride. Declaring the 20-byte gap as padding made the frame complete; removing the declaration made it hang again in the same session.
- The fixed production app then showed the built-in triangle, a 36-vertex cube and a 900-vertex frame from the SD card on the console, and a 3000-vertex
  synthetic case completed; with padding disabled it hung (contrast).
Not established: animation or long runs, textures, real DS captures, other vertex formats, and any DS-faithful rendering.
Nothing in this section is an assistant-run observation.

### Run 7 — Phase 5A-2 clip-space path (HARDWARE OBSERVED, photographs; summary)

Owner-run on 2026-11-03; evidence = photographs plus the owner's statement that "all tests ran
normally and ended normally". The photographs are kept **outside** the repository in the owner's
staging directory.

Artifact hashes (of the files staged for the run, run by file name):

| Artifact | SHA-256 |
|---|---|
| production app `twlhd-arm11-5a.3dsx` (main @ `1e9ed2d`) | `301da396fc9a4de1ba6378f559e8612ed53926b7629723c77316fc57ced33a8f` |
| v2 frame `tests/host/fixtures/demo_frame_desmume_v2.bin` | `acbdb48e9c0f5a5159ca58bc116d3d863fb822c0a3a79b1ad55aff0ac7934fa5` |

The v2 frame is a real DeSmuME capture of our own homebrew demo cube (124 GXCM + 4 RSTA).

Results:
- **T1 — built-in triangle (debug path, new build):** RGB triangle on the top screen, status
  `ready`. OK (photo).
- **T2 — v1 cube (debug path):** `SD: frame loaded, 36 verts -> 12 tris`, red/blue/magenta faces,
  as in Run 6. OK (photo).
- **T3 — v2 cube, first hardware use of the clip path** (second shader, 32-byte vertex with
  declared bytes == stride, `clipfix` uniform): the bottom screen shows `mode: SD v2 (DS MATRICES)`,
  `camera: DS matrices (v2 GXCM)`, `frame:241 gxcm:124 tris:12`, `vtx in: 24`, `render verts: 36`,
  counters `stk0 cap0 inc0 uns0 vd0`, `SD: v2 loaded, 36 verts -> 12 tris`. The top screen shows the
  cube seen from a corner: upper-left face red, upper-right yellow, lower cyan, not mirrored,
  horizontally centred, vertical centre slightly above screen middle; same arrangement as the
  Tanuki3DS emulator and the independent oracle. **No GPU hang.** Size measured roughly from the
  photo ~142×147 screen px (emulator 143×144) — a photo estimate, not exact. (photo)
- **T4 — Y after v2 (clip → debug):** built-in triangle, status `built-in triangle`. OK (photo).
- **T5 — A again (debug → clip):** reported OK by the owner; **no separate photo**.
- **T6 — truncated v2 file:** `v2 parse: ERR_SIZE`, built-in triangle stays, no freeze. OK (photo).

Correction to an earlier draft of this procedure: it predicted black side bars. That was wrong —
the clear colour covers the whole 400×240 top screen; only the geometry is confined to the centred
320×240 area.

**Conclusion:** HARDWARE OBSERVED: the clip-space path runs without a GPU hang for a 36-vertex v2 frame and draws the DS-matrix cube with the expected orientation/colour layout.

**Not covered:** DS viewport +1 / y1-wrap behaviour; captures other than this demo cube; large
frames on the clip path; many consecutive frames; long-run stability; textures/lighting/alpha
(5B, not approved).

### Run 8 — Phase 5A-2 clip path with a non-default DS viewport (HARDWARE OBSERVED, photographs; summary)

Owner-run on 2026-11-03; evidence = photographs. The photographs are kept **outside** the
repository in the owner's staging directory.

Artifact hashes (of the files staged for the run, run by file name):

| Artifact | SHA-256 |
|---|---|
| production app `twlhd-arm11-5a.3dsx` (main @ `16bfa41`; same 3dsx hash as Run 7, different commit) | `301da396fc9a4de1ba6378f559e8612ed53926b7629723c77316fc57ced33a8f` |
| CONTROL `twlhd_frame_v2_ctl_full.bin` | `79f90bc2…cbf4` |
| VARIANT `twlhd_frame_v2_vp.bin` | `e78d12e0…f6e2` |

Both frames are DeSmuME captures of the demo cube (frame 120, same rotation), 1152 B,
124 GXCM + 4 RSTA, byte-identical except the 3 bytes of the VIEWPORT word (offsets 52–54).
The CONTROL has viewport (0,0)-(255,191); the VARIANT has viewport (32,96)-(159,191)
(word `0xBF9F6020`), built with
`make -C arm9v2 AUTODUMP_FRAME=120 VP_X1=32 VP_Y1=96 VP_X2=159 VP_Y2=191`.
Because only the 3 viewport bytes differ, any picture change is attributable to the viewport.

Results:
- **T1 — built-in triangle (debug path, new build):** OK (photo).
- **T2 — CONTROL frame, viewport (0,0)-(255,191):** bottom screen shows `mode: SD v2 (DS MATRICES)`,
  `frame:121 gxcm:124 tris:12`, `vtx in: 24`, `render verts: 36`, counters `stk0 cap0 inc0 uns0 vd0`,
  `SD: v2 loaded, 36 verts -> 12 tris`. Top screen: red upper-left face, yellow right, cyan lower,
  not mirrored. Photo-estimated ~125 px wide, centre x ~192, y ~45–173; the oracle/emulator gives
  x 128–254, y 43–170. The photo is oblique, so the estimate is not exact. (photo)
- **T3 — VARIANT frame, viewport (32,96)-(159,191):** same bottom text, same zero counters. Same cube
  at about half size (~63 px wide), upper-left: photo-estimated x 124–188, y 24–87 versus
  oracle/emulator x 124–187, y 22–85 — within a few px. Same colours and orientation.
  **No GPU hang.** (photo)
- **T4 — Y after T3 (clip → debug):** built-in triangle again OK (photo); the following A press was
  reported OK by the owner, **no photo**.

**Conclusion:** HARDWARE OBSERVED for one frame and one non-default viewport: the 3DS draws the
`clipfix` mapping for viewport (32,96)-(159,191) (scale 0.5 plus offset) as the oracle and emulator
predict, within photo measurement error of a few px.

**Not covered:** real-DS behaviour of viewport +1 / y1 wrap (not testable on the 3DS; stays
EMULATOR(DeSmuME)/unverified); viewports with y1 > 191; captures other than this demo cube;
large frames on the clip path; many consecutive frames; long-run stability; textures/lighting/alpha
(5B, not approved).

### Run 9 — Phase 5A sequence playback (HARDWARE OBSERVED, photographs; summary)

Owner-run on 2026-11-04/05; evidence = photographs kept **outside** the repository in the owner's
staging directory.

Artifact hashes (of the files staged for the run, run by file name):

| Artifact | SHA-256 |
|---|---|
| production app `twlhd-arm11-seq.3dsx` (main @ `c40960f`) | `b7b14cff7f4783ca73dc90af44ecfdb5b57c7af9c432b2b6f1b6904a978d5e16` |

The sequence is 8 v2 frames `seq_000`–`seq_007`, 1152 B each: DeSmuME captures of the demo cube at
DS frames 30, 60, …, 240. Even indices use the full viewport; odd indices use the viewport
(32,96)-(159,191). The frames were copied to the SD folder `twlhd_seq/` and played with the **X**
key at about 3 frames/s; each step re-parses, re-interprets and re-uploads the clip-scene vertex
buffer. Playback wraps to `000` when the next file is missing.

Results:
- **T1 — built-in triangle:** OK (photo); the key legend shows `X=seq`.
- **T2 — A, single v2 frame (refactored load path):** bottom screen shows `mode: SD v2 (DS MATRICES)`,
  `frame:121 gxcm:124 tris:12`, `render verts: 36`, counters `stk0 cap0 inc0 uns0 vd0`,
  `SD: v2 loaded, 36 verts -> 12 tris`; full-viewport cube. OK (photo).
- **T3 — X sequence playback (three photos in one run):** `seq 003/008 loop 6` (small cube,
  upper-left), `seq 001/008 loop 13` (small red face, upper-left), `seq 000/008 loop 25` (large
  magenta face); counters zero. 25 passes ≈ 2.7 s/pass ≈ 67 s continuous (~200 re-uploads), no GPU
  hang, no visible glitches in the photos; sizes alternate full/small as expected. (photos)
- **T4 — A during playback:** playback stops and the single frame from `sdmc:/twlhd_frame.bin`
  stays. OK (photo).
- **T5 — Y:** built-in triangle, playback stopped. OK (photo). The X-restart and X-again (freeze)
  steps were reported OK by the owner, **no photos**.
- **T6 — truncated `seq_003` (error path), and optional T7 (no `seq_000.bin`):** owner statement
  only, **no photos**.

**Conclusion:** HARDWARE OBSERVED: sequence playback with per-frame re-parse, re-interpretation and
vertex-buffer re-upload on the clip path ran ~67 s (25 passes of 8 frames) with no GPU hang and no
visible glitches; the A and Y paths were unchanged; picture positions are consistent with the
emulator/oracle at photo level only (not pixel-exact).

**Not covered:** large frames (many vertices) on the clip path; runs much longer than ~1 minute;
captures other than the demo cube; DS-side viewport +1/y1-wrap; textures/lighting/alpha (5B, not
approved); T6/T7 and the X-restart/X-freeze steps have no photos.

### Run 10 — Phase 5A large frames on the clip path, 100 and 170 cubes (HARDWARE OBSERVED, photographs; summary)

Owner-run; evidence = 15 photographs kept **outside** the repository in the owner's staging
directory (`evidence/run10_01..15`); report dated 2026-11-06 ("全テスト正常動作正常終了です").

Artifact hashes (of the files staged for the run, run by file name):

| Artifact | SHA-256 |
|---|---|
| production app `twlhd-arm11-seq.3dsx` (same bytes as Run 9) | `b7b14cff7f4783ca…` |
| v2 frame `twlhd_frame_v2_c100.bin` (CUBES option, AUTODUMP_FRAME=10) | `8e53b4b3…` |
| v2 frame `twlhd_frame_v2_c170.bin` (CUBES option, AUTODUMP_FRAME=10) | `4904e26d…` |

Build: unchanged ARM11 `twlhd-arm11-seq.3dsx` (SHA-256 `b7b14cff7f4783ca…`, same bytes as
Run 9). Frames are DeSmuME captures of our own homebrew demo built with the CUBES option
(main @ `1c36d06`) at AUTODUMP_FRAME=10: `twlhd_frame_v2_c100.bin` (71688 B, 3600 output
vertices, 1200 tris, gxcm 8941) and `twlhd_frame_v2_c170.bin` (121528 B, 6120 vertices,
2040 tris, gxcm 15171). 6120 is the largest size the ARM11 clip scene accepts (buffer
capacity 6144).

Results:
- **T1 — single cube regression:** `render verts: 36`, 12 tris. OK (photo).
- **T2 — 100 cubes:** `SD: v2 loaded, 3600 verts -> 1200 tris`; grid with 6 rows, last row
  15 cubes, fully visible. OK (photo).
- **T3 — 170 cubes:** `SD: v2 loaded, 6120 verts -> 2040 tris`; 17×10 grid fully visible,
  edge cubes not cut off. Y (built-in triangle) then A again OK. (photo)
- **T4 — restart + A:** same 170-cube picture. OK (photo).
- **T5 — sequence `seq_000` (c100) / `seq_001` (c170):** alternates between the two grids,
  `seq: wrapped`; photos at loop 5, 7, 8, 9, 10 and at loop 43 and 52; no freeze, no stray
  triangles, no POWER-hold reported. Loop 52 at the last photo (~35 s inferred from the loop
  counter; elapsed time was not measured). (photos)

Observations, not failures:
- One transition photo (loop 8) shows faint ghost rows of the other grid (consistent with the
  camera exposure spanning two frames; unverified).
- Bottom-screen text is partly overwritten in two photos (console redraw while photographed;
  not investigated).

**Conclusion:** HARDWARE OBSERVED: no GPU hang for a 6120-vertex scene, and for alternating
3600/6120-vertex re-uploads over ~35 s. Picture correctness judged from photos only (grid
structure/counts); no pixel-exact comparison with the oracle.

**Not covered / still open:** runs much longer than ~1 min with large frames; captures other
than our demo cube; DS-side viewport +1/y1-wrap; the unexplained DeSmuME failures at
AUTODUMP_FRAME=120 (see `docs/EMULATOR_VALIDATION.md`); textures/lighting/alpha (Phase 5B,
not approved).
