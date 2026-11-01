# New 3DS — Triangle-Only Hardware Test: Final Procedure (FOR APPROVAL)

> **Status update (later; the text below is the original Run 1 procedure and is kept as written).**
> The owner ran this and later builds on the console themselves; results are recorded in section 12 and in
> `docs/PHASE4_HARDWARE_FINDINGS.md`. The assistant never connected to or executed anything on the console.
> Sections 6, 9 and the "HARDWARE UNVERIFIED" wording below describe the situation BEFORE those runs.

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

The same build (SHA-256 `3198426913e9bc38c2e011ff299f596e2b517ba739b843e3ce304506f10dcb33`) was run: **the top screen stayed black,
the buttons did nothing, and a forced power-off (POWER hold) was needed. The console booted normally afterwards.**
This answers the open points above for that build: the triangle was **not** displayed, and the app did not stay responsive.
(Audit risk 1, a hang requiring a hard power-off, did occur; its cause was not the GPU-synchronisation scenario assumed in the audit — see below.)

### Runs 2–6 — owner-run diagnostic builds (HARDWARE OBSERVED, photographs; summary)

Details, build hashes, the evidence table and the labels are in `docs/PHASE4_HARDWARE_FINDINGS.md`. In short:
- An unmodified official `simple_tri` control rendered correctly, so the console, launcher and citro3d were not the problem.
- Diagnostic builds with a non-blocking GPU-completion probe showed every CPU-side call returning while the first submitted GPU frame never completed.
- Toggling features one at a time on the console isolated the cause to the vertex attribute buffer: declared attribute bytes (28) did not equal the
  48-byte vertex stride. Declaring the 20-byte gap as padding made the frame complete; removing the declaration made it hang again in the same session.
- The fixed production app then showed the built-in triangle, a 36-vertex cube and a 900-vertex frame from the SD card on the console, and a 3000-vertex
  synthetic case completed; with padding disabled it hung (contrast).
Not established: animation or long runs, textures, real DS captures, other vertex formats, and any DS-faithful rendering.
Nothing in this section is an assistant-run observation.
