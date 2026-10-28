<!-- UNVERIFIED — no hardware was available during this task.
     These results have not been confirmed on real hardware. -->

# RTCom Hardware Test Procedure (Phase 3C)

## Disclosure

**UNVERIFIED.** No New Nintendo 3DS was available while this document and the
Phase 3B implementation were produced. Nothing in this procedure has been
executed on a console. The instructions below describe the intended test, the
expected observations, and the failure modes to look for; they are not a report
of results.

The code paths involved are:

- `arm7/` — ARM7-side RTCom driver and test harness.
- `arm11_ucode/` — bare-metal ARM11 payload uploaded into `TwlBg.cxi`.
- `arm9/source/main.c` — DS-side display of the result reported over PXI.

All three compile in this repository. That is the extent of the evidence.

### Ucode injection is disabled by default

`arm7/source/main.c` defines `TWLHD_RTCOM_UCODE_ENABLED 0`. In this
configuration the driver deliberately skips `rtcom_upload_ucode()` and
`rtcom_execute_ucode()` and reports code 5 (`RTCOM_TEST_UCODE_DISABLED`) to the
ARM9. This is a safety gate: the ARM11 entry/return contract has been verified
against reference sources but not independently exercised on hardware, so the
upload/execute path remains off until a hardware bring-up confirms it is safe.

A real hardware RTCom test must set `TWLHD_RTCOM_UCODE_ENABLED` to `1`, rebuild
`arm7/`, and also link the real ARM11 blob into the ARM7 binary (the build-only
stub is zero-length).

## Prerequisites

1. **New Nintendo 3DS** (the old 3DS has a different TWL_FIRM and is not
   supported by this procedure).
2. **TWPatch-generated `TwlBg.cxi` with RTCom enabled.** The file must be
   produced by TWPatch configured for RTCom; a stock `TwlBg.cxi` will not accept
   the upload.
3. **Installed `TwlBg.cxi`.** Place the generated file at:
   `sd:/_nds/TWiLightMenu/TwlBg/Widescreen.cxi`
   (TWiLight Menu's standard location). Confirm the path used by the installed
   TWiLight Menu build if it differs.
4. **A DS title compatible with nds-bootstrap ARM7 injection.** Not every title
   has a patch profile; use one known to load through nds-bootstrap.
5. **The ARM7 image built from this project.**
   ```
   make -C arm7
   ```
   produces `arm7/twlhd_arm7.elf`. Convert it to the flat binary the injection
   tool expects if your toolchain requires it.
6. **The ARM11 ucode blob built from this project.**
   ```
   make -C arm11_ucode
   ```
   produces `arm11_ucode/twlhd_ucode.uc11`. The ARM7 image must embed or load
   this blob before upload; the build-only stub is zero-length, so a hardware
   run requires the real blob to be linked in.
7. **A way to read the DS screen output.** The result is printed by the ARM9
   application; a camera or capture card is sufficient.

## Injection Procedure

This project does **not** vendor an injection tool. It describes the approach so
the test can be reproduced with the user's own copy of the tooling.

1. Obtain the DS ROM you intend to test.
2. Obtain the ARM7 replacement binary built above.
3. Use an nds-bootstrap patching tool that accepts a DS ROM plus an ARM7 binary
   and emits a patched NDS image. The TWiLight Menu ecosystem's
   `generate_patch.py`-style flow is the reference shape: ROM in, ARM7 binary
   in, patched NDS out.
4. Keep a backup of the original ROM. The patched image is derived output and
   should not be redistributed.
5. Place the patched NDS image where nds-bootstrap will load it (SD card, or the
   emulator/frontend path used by your setup).
6. Ensure the patched `TwlBg.cxi` is installed before launching the title.
7. Launch the patched title through nds-bootstrap, not through the home menu's
   normal DS boot path.

If the patch tool rejects the ARM7 binary, verify that it is a raw ARM7 image
and not an ELF wrapper. The `.elf` produced by `make -C arm7` is a link product;
the injection tool may need a flat binary extracted from it.

Because the exact command line depends on the user's tool version, it is
intentionally omitted. Following an outdated command line is worse than
following the tool's own help output.

## Expected On-Screen Output

The ARM9 application prints the latest RTCom status on the statistics console.
The status starts as:

```
RTCom: UNVERIFIED (build-only)
```

and changes when the ARM7 reports over PXI user channel 1. Expected values:

| ARM7 result | On-screen text | Meaning |
|-------------|----------------|---------|
| `RTCOM_OK` (0) | `RTCom: OK` | Handshake, upload, execute, and echo test all passed |
| `RTCOM_ERR_TIMEOUT` (1) | `RTCom: FAIL (1)` | An acknowledge poll never saw `IRQ_NETWORK` |
| `RTCOM_ERR_NOT_READY` (2) | `RTCom: FAIL (2)` | Service replied `IDLE`/`BUSY`, or all init retries failed |
| `RTCOM_ERR_UPLOAD_FAILED` (3) | `RTCom: FAIL (3)` | A byte exhausted its retries, or the finish command failed |
| Echo mismatch (4) | `RTCom: FAIL (data mismatch)` | A returned byte was not the bitwise complement of the sent byte |
| Ucode disabled (5) | `RTCom: UCODE DISABLED` | `TWLHD_RTCOM_UCODE_ENABLED` is 0; upload/execute were skipped |

Any other non-zero code prints `RTCom: FAIL (?)`.

If the screen never leaves `RTCom: UNVERIFIED (build-only)`, the ARM7 image is
not the one built here, or PXI user channel 1 is not reaching the ARM9
application. That is a wiring/build problem, not an RTCom verdict.

A `FAIL (1)` on the very first probe usually means the `TwlBg.cxi` in use does
not have RTCom enabled, or `REG_RCNT` bit 8 was not applied. A `FAIL (2)` after
a successful probe usually means the service is present but not ready within the
retry budget. A `FAIL (3)` points at the upload window or the frame gates.

## Throughput Estimation

Do not trust a single number. Measure it on the console and record the method.

Suggested method:

1. Read `cpuGetTiming()` (or capture the VBlank counter) immediately before the
   first byte of a known-length transfer.
2. Read it again immediately after the last acknowledgement.
3. Compute elapsed cycles (or frames), then divide the transferred byte count by
   elapsed time.

```
bytes_per_second ≈ bytes_transferred / ((t_end - t_start) / clock_hz)
```

Record the byte count, the timing source, and the clock assumption alongside the
result. `cpuGetTiming()` counts at the ARM9 bus clock; converting to seconds
requires the correct clock for the console model in use.

Label the result as an **order-of-magnitude estimate only**. It includes
acknowledge latency, polling overhead, and the bit delays, and it varies with
the service's frame gates. It is useful for confirming the analytical
conclusion in `docs/RTCOM_AUDIT.md` (full-frame transport is not feasible), not
for capacity planning.

For reference, the analytical model predicts:

- One acknowledged byte per exchange.
- Bit delays of 48 iterations during upload and 14 during steady state.
- About 16.7 seconds of setup (1000 frames at 60 Hz) before data can flow.
- A `TwlhdFrame` of roughly 72 KiB, i.e. tens of thousands of exchanges.

If a measured run contradicts the conclusion by a wide margin, re-check what was
actually measured before revising the architecture. A single short echo test is
not a frame-transfer benchmark.

## Result Recording Template

Copy this block into the test log when a run is performed:

```
Date:
Console model / region:
TwlBg.cxi source (TWPatch settings):
Title / ROM revision:
ARM7 image build (git describe):
ARM11 blob size (bytes):
Handshake result:
Upload result:
Execute result:
Echo test result:
On-screen text:
Measured bytes:
Measured elapsed time:
Notes:
```

## Disclosure

**UNVERIFIED.** This procedure has not been executed. The expected outputs are
derived from the implementation and the hardware facts in
`docs/RTCOM_AUDIT.md`, not from observation. Treat every success criterion above
as a hypothesis to confirm on a New 3DS, and record the actual result in the
template rather than assuming the expected one.

<!-- UNVERIFIED — no hardware was available during this task.
     These results have not been confirmed on real hardware. -->
