<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTCom Transport Audit (Phase 3A)

**Status:** analysis complete; no hardware execution performed.
**Scope:** Phase 3A (transport feasibility and licensing audit) feeding Phase 3B
(ARM7 driver and ARM11 ucode blob).
**Document version:** 1.0

This document records what TWL-HD knows about RTCom, what it can prove, and what
it deliberately leaves unverified. Every claim carries one of three labels:

| Label | Meaning |
|-------|---------|
| `BUILD VERIFIED` | A `make`/compiler/linker command ran and exited 0 in this environment. |
| `UNIT VERIFIED` | A host-side test ran and passed in this environment. |
| `REFERENCE VERIFIED` | A fact or value checked against an installed header, official example, or reference repo, with a citation. |
| `HARDWARE UNVERIFIED` | Requires a New 3DS running a patched TWL_FIRM; not executed here. |

The reader should treat any statement without a label as explanatory context,
not as an established fact.

## Overview

RTCom is the name used in this project for the RTC serial-bus side channel that
connects the ARM7 and ARM11 while a DS or DSi title is running under TWL_FIRM.
The bus itself is the same 3-wire RTC interface the DS firmware uses to read the
real-time clock: a chip-select line, a clock line, and a bidirectional data
line, bit-banged through `REG_RTC`.

The channel is interesting because it is one of the very few paths that remain
usable from inside the DS compatibility environment. A DS title executes on the
ARM9, system services execute on the ARM7, and TWL_FIRM runs on the ARM11. The
ARM9 cannot touch the PICA200 rasteriser; RTCom lets ARM7-side code exchange
small messages with code running on the ARM11 inside `TwlBg.cxi`.

RTCom is not a general-purpose network. It carries one command byte and one
parameter/status byte per exchange, with an acknowledge round-trip for each
exchange. That constraint drives the bandwidth conclusion in this document.

The Phase 3 architecture is therefore split in two:

- `arm7/` — a driver linked into the DS title binary. It performs the 3-wire
  bit-bang, the initial handshake, the ucode upload, and the execution request.
- `arm11_ucode/` — a bare-metal ARM11 blob uploaded at runtime into a reserved
  window inside the already-running `TwlBg.cxi` process.

`docs/ARCHITECTURE.md` describes the same split for the phase roadmap.

## Reference Sources

Two public projects were studied for protocol facts and hardware addresses:

1. **RtcPwalker** — located locally at `/home/one/3ds/RtcPwalker`.
   - License: **GPLv3**.
   - Used for: confirmation that RTCom traffic uses the RTC registers at
     `0x4000138`, the opcode values, and the shape of the handshake.
   - Constraint: GPLv3 is copyleft. Incorporating even a small derived portion
     into an Apache-2.0 project would relicense the combined work. No code was
     copied, transliterated, or mechanically translated.

2. **Analog-Controls-for-NDS-Games-on-3DS** — a public repository with **no
   LICENSE file**.
   - License: none granted. Absence of a license means the default copyright
     applies; there is no permission to redistribute or prepare derivative
     works.
   - Used for: background reading only.
   - Constraint: no code, comments, or structure were copied.

**Clean-room statement.** Every C source file, header, and linker script added
in Phase 3 was written from the hardware facts recorded in this document:
register addresses, command-byte values, opcode values, and timing constants.
Those facts are not copyrightable expression. Function names, organisation, and
implementations are original to TWL-HD. Each source file carries an
`SPDX-License-Identifier: Apache-2.0` tag and a clean-room notice.

`docs/RTCOM_AUDIT.md` itself is Apache-2.0. The reference repositories remain
under their own terms and are not vendored, submoduled, or fetched by the build.

## Initialization / Handshake Sequence

The driver's entry point is `rtcom_init()`. The sequence below is the contract
implemented by `arm7/source/rtcom_driver.c`; the same sequence is what any
reimplementation must satisfy.

### Step 1 — Service discovery

1. Before any bus traffic the driver enters a critical section and saves
   `REG_RCNT`.
2. It clears any stale `IRQ_NETWORK` bit in `REG_IF`, then writes the exact
   reference value `0x8100` to `REG_RCNT` (`RCNT_MODE_GPIO | RCNT_SI_IRQ_ENABLE`),
   then clears `IRQ_NETWORK` in `REG_IF` a second time.  The second clear is
   required because enabling the SI/RTC IRQ path can itself raise
   `IRQ_NETWORK` as a side effect of turning the mode on.
3. The probe is a single-argument request: it writes ONLY the raw probe
   literal (`RTCOM_REQ_PROBE`, `0x01`) to register 113.  No register-112 write
   is performed for the probe, matching the reference's one-argument
   `request(request)` overload. Register 113 corresponds to RTC address `0x71`,
   with write command `0x6E` and read command `0x6F`.
4. Register 113 is written last and acts as the trigger; for single-argument
   requests the service latches only that byte. The probe value is the
   reference's `beginComm` probe byte; it is intentionally **not** one of the
   named service opcodes.
5. It polls `REG_IF` for the `IRQ_NETWORK` bit. Each poll is bounded by
   `RTCOM_ACK_TIMEOUT = 2,062,500` iterations, so the driver cannot spin
   forever on a missing service.
6. On an interrupt hit it acknowledges the interrupt and reads the status byte
   from register 113 using command byte `0x6F`.
7. A status of `RTCOM_STATUS_DONE` (`0x82`) means the service accepted the
   probe. A bus value is not itself proof of success; the status byte is.
8. On exit any pending `IRQ_NETWORK` bit is cleared, `REG_RCNT` is restored to
   the value saved at the start of the exchange, and the critical section is
   left.

### Step 2 — Retry policy

`rtcom_init()` allows `RTCOM_INIT_RETRIES = 10` complete attempts. The first
successful probe exchange returns `RTCOM_OK`. If all ten attempts time out or
return a non-`DONE` status, the function returns `RTCOM_ERR_NOT_READY`. This is
a terminal result: the ARM7 side reports it to the ARM9 and does not continue
to the upload stage.

### Step 3 — Payload upload

Once the service is discovered, the ARM11 payload is uploaded with the real
multi-step length-then-data protocol:

1. Reject `len == 0` or `len > 60 KiB` up front with `RTCOM_ERR_BAD_ARG`.
2. Enter the same critical-section / `REG_RCNT` save / double `REG_IF` clear
   wrapper used by `rtcom_init()`.
3. Send `RTCOM_OP_UPLOAD_UCODE` (`0x41`) with length byte 0 (`len & 0xFF`) as
   the parameter; wait for status `ACK` (`0x80`).
4. Send `RTCOM_OP_NEXT` (`0x81`) with length byte 1; wait for `ACK`.
5. Send `RTCOM_OP_NEXT` (`0x81`) with length byte 2; wait for `ACK`.
6. Send `RTCOM_OP_NEXT` (`0x81`) with length byte 3; wait for `ACK`.
7. For each payload byte, send `RTCOM_OP_NEXT` (`0x81`) with that byte as the
   parameter; wait for `ACK`. There is **no per-step retry**: a single failed
   step aborts this entire upload attempt.
8. After the last payload byte, send `RTCOM_OP_KEEPALIVE` (`0x83`) as a
   single-argument request (no register-112 write) without waiting for its
   acknowledge, then wait for status `DONE` (`0x82`) with a bounded timeout.
   This is the only upload step that expects `DONE`.
9. Send `RTCOM_OP_FINISH_UCODE` (`0x42`) as a single-argument request; wait for
   `ACK`. A failed finish exchange also aborts the attempt.
10. If any step in the attempt fails, the entire attempt is aborted, the bus
    state is restored, and the driver retries from step 2 up to
    `RTCOM_UPLOAD_RETRIES` times.  Only whole-attempt retries are permitted;
    resending a single step after the service has already advanced its receive
    cursor would shift every subsequent byte and corrupt the blob.
11. Clear any pending `IRQ_NETWORK` bit, restore `REG_RCNT`, and leave the
    critical section.

### Step 4 — Execution

`rtcom_execute_ucode(param)` sends `RTCOM_OP_EXECUTE_UCODE` (`0x44`) with the
supplied parameter and waits for `RTCOM_STATUS_ACK` (`0x80`). The parameter is
passed through to the uploaded blob. From that point the ARM11 blob owns its
own execution context; the ARM7 driver only exchanges further commands when the
blob's command handler is re-entered by the service.

### Step 5 — Timer gates

The reference service uses frame-counted gates rather than hardware timers for
the one-time upload path:

| Gate | Frames | Wall time at 60 Hz |
|------|--------|--------------------|
| `UploadCode` | 200 | ~3.3 s |
| Additional ready gate | 800 | ~13.3 s |
| `IrReady` (total) | 1000 | ~16.7 s |

A 200-frame gate covers the upload itself; 800 further frames bring the service
to the ready state, for 1000 frames in total, roughly 16.7 seconds after the
sequence starts. These are deliberate delays, not timeouts to be shortened. The
driver's own busy-wait timeout is a separate bound and must remain generous
enough to survive the upload window.

All of the above is `BUILD VERIFIED` for compilation and `UNIT VERIFIED` for the
host-side protocol tests. The actual handshake is `HARDWARE UNVERIFIED` until
run on hardware.

## Command Exchange Model

The ARM11 side is entered through `_start`, which immediately calls a C
handler. The contract is documented in the
**ARM11 ucode calling convention** subsection below.

### Opcodes

| Constant | Value | Direction | Purpose |
|----------|-------|-----------|---------|
| `RTCOM_REQ_PROBE` | `0x01` | ARM7 → ARM11 / service | Raw probe literal used only by `rtcom_init()`; **not** a named service opcode |
| `RTCOM_OP_KIL` | `0x00` | ARM7 → service | Kill/reset the RTCom service |
| `RTCOM_OP_UPLOAD_UCODE` | `0x41` | ARM7 → service | Begin ucode upload (param = first length byte) |
| `RTCOM_OP_FINISH_UCODE` | `0x42` | ARM7 → service | Make the uploaded blob executable |
| `RTCOM_OP_EXECUTE_UCODE` | `0x44` | ARM7 → service | Execute the uploaded blob (param passed through) |
| `RTCOM_OP_NEXT` | `0x81` | ARM7 → service | Continue a multi-step exchange |
| `RTCOM_OP_KEEPALIVE` | `0x83` | ARM7 → service | Sent once after the last upload `NEXT`, before waiting for `DONE` |
| `RTCOM_OP_SYNC_KIL` | `0xFE` | ARM7 → service | Synchronous kill; waits for `READY` |

### Status bytes

| Constant | Value | Meaning |
|----------|-------|---------|
| `RTCOM_STATUS_READY` | `0x00` | Service present / ready |
| `RTCOM_STATUS_ACK` | `0x80` | Previous command acknowledged |
| `RTCOM_STATUS_DONE` | `0x82` | Multi-step exchange completed |

The driver treats `ACK` (`0x80`) as the success byte for every normal single
request (`UPLOAD_UCODE`, each `NEXT`, `FINISH_UCODE`, and `EXECUTE_UCODE`).
`DONE` (`0x82`) is expected only as the immediate reply to the `rtcom_init()`
probe and as the reply that follows `KEEPALIVE` at the end of an upload. Any
other status is surfaced as `RTCOM_ERR_NOT_READY` so the caller can retry at a
higher level.

### Session scope and post-execute state

**`HARDWARE UNVERIFIED`** — session boundary between probe and upload.

The reference implementation keeps the probe (`beginComm`) and the upload in a
single continuous bus-lock / IRQ session.  This codebase currently ends the
session at the conclusion of `rtcom_init()` and starts a fresh session at the
start of `rtcom_upload_ucode()`.  That briefly re-enables interrupts between the
two calls, opening a window in which an ongoing service exchange could in
principle be disturbed.  The default gate-off configuration is unaffected
because `rtcom_upload_ucode()` is never called.  Future work should either
combine the two into one session or confirm on hardware that the gap is
harmless.

**`HARDWARE UNVERIFIED`** — post-`EXECUTE_UCODE` teardown.

`rtcom_execute_ucode()` waits for `ACK` and returns without performing the
reference's post-execute teardown sequence (`SYNC_KIL` + a done signal).  This
is only reachable when `TWLHD_RTCOM_UCODE_ENABLED` is 1, which is off by
default.  Calling it leaves the RTCom service in an undefined state from the
ARM7's perspective; it should not be considered safe for real hardware until
that teardown is implemented and verified.

**`HARDWARE UNVERIFIED`** — gate-off probe leaves the service open.

With the gate off, `rtcom_init()` performs the probe and, on success, restores
`REG_RCNT` without sending `SYNC_KIL` or any other RTCom traffic.  The service
may therefore remain in a "communicating" state after the probe-only default
run.  The consequences on real hardware are unknown; this invariant is
deliberately maintained because the gate-off path is required to emit only the
probe.

### Round-trip echo contract

The previous `RTCOM_OP_ECHO` / single-ack echo model is **not** reference-backed
and has been removed. The reference's `handleCommand1(u8 param, u32 stage)` uses
`stage` as a call-sequence index within one logical exchange, not as an opcode:
stage 0 is the first call after `EXECUTE_UCODE` dispatches to the blob, stage 1
is the second call, and so on. Data returned by the blob flows back through
register 112, not as the status byte in register 113.

A correct round-trip would therefore be:

1. `EXECUTE_UCODE(param)` and wait for `ACK` (stage 0 from the blob's point of
   view).
2. `NEXT(0)` (or another appropriate parameter) and wait for `ACK` (stage 1).
3. Read the returned byte from register 112 with a `rtcom_read_data()`-style
   helper.

This redesign is `REFERENCE VERIFIED` in shape but `HARDWARE UNVERIFIED`. To
avoid forcing a fragile implementation together, the round-trip test in
`arm7/source/main.c` currently reports `RTCOM_TEST_ROUNDTRIP_SKIPPED` in both
the gate-off and gate-on configurations. The ARM11 ucode stub therefore returns
a safe default (`0`) for every `param`/`stage` input rather than implementing a
stage-dependent payload.

The Phase 3B test message structure remains available for a future round-trip:

```c
typedef struct {
    u8 magic[4];   /* "TWLH" */
    u8 payload[4]; /* arbitrary test bytes */
} RtcomTestMsg;
```

## ARM11 ucode calling convention

**REFERENCE VERIFIED** against `arm11_ucode/source/ucode_entry.s` in both
reference repos (`RtcPwalker` and `Analog-Controls-for-NDS-Games-on-3DS`);
both files contain the same functional entry sequence.  TWL-HD's stub was
independently written from the documented functional contract
(`push`/`bl`/`pop`, `r0=param`, `r1=stage`, `r0=return`) and is not claimed to
be byte-for-byte identical to either reference.

The entry stub is:

```asm
push {r1-r12, lr}
bl   handleCommand
pop  {r1-r12, pc}
```

The matching C prototype used by the handler is:

```c
extern "C" int handleCommand(u8 param, u32 stage);
```

The contract is:

| Register | Role |
|----------|------|
| `r0` | First argument `param`; also holds the return value on exit |
| `r1` | Second argument `stage` |
| `r0` | Return value (`int`) |
| `r1-r12`, `lr` | Preserved by the entry stub (pushed before the C call, popped before return) |

The stub returns to its caller; it does **not** loop forever. An infinite loop
inside `TwlBg.cxi` would hang the background process, which is unacceptable
even for a disabled-by-default stub. The `push`/`pop` instructions require a
valid stack pointer in the TwlBg context at the moment `_start` is called; the
entry stub does not set up a stack itself, but it does not need one beyond
what the caller already provides.

The reference's `handleCommand1` uses `param` and `stage` to drive a small
state machine (`stage == 0` is the first call after `EXECUTE_UCODE`,
`stage == 1` is the first `NEXT` follow-up, etc.). TWL-HD's current stub
ignores both arguments and returns 0 for all inputs, because ucode injection
is gated off by default and the round-trip echo test is deliberately skipped
even when the gate is on (see `TWLHD_RTCOM_UCODE_ENABLED` in
`arm7/source/main.c`). This makes unexpected `param`/`stage` values harmless:
the function always returns a safe default and never branches to undefined
memory.

## Register Usage

### `0x4000138` — 3-wire RTC data register (`REG_RCNT_RTC`)

**REFERENCE VERIFIED** against Calico `calico/nds/arm7/gpio.h` and reference
`rtcom.cpp` pin macros.

| Bit | Name | Use |
|-----|------|-----|
| 0 | SIO | Bidirectional data line |
| 1 | SCK | Bit clock, generated by the driver |
| 2 | CS | Chip select; held high for the duration of a byte |
| 4 | SIO_OUT | Direction bit: 1 = SIO driven as output |
| 5 | SCK_OUT | Direction bit: 1 = SCK driven as output |
| 6 | CS_OUT | Direction bit: 1 = CS driven as output |

The prior draft had CS and SIO swapped and used bit 4 as a dedicated input.
Calico's header defines the data line on bit 0, the clock on bit 1 and the
chip-select on bit 2; the reference `rtcom.cpp` uses the same layout
(`SIO_0/SIO_1 = bit 4 | bit 0`, `SCK_0/SCK_1 = bit 5 | bit 1`,
`CS_0/CS_1 = bit 6 | bit 2`).

**Bit order** is MSB-first for both command and data bytes. The reference
`rtcTransferReversed()` is explicitly commented "high bit first" for the
command byte, parameter bytes, and result bytes, despite its name. The prior
draft sent data LSB-first; that has been corrected in `arm7/source/rtcom_driver.c`.
`rtcom_write_byte()` and `rtcom_read_byte()` are the only places that touch
these lines.

**Direction control during reads.** `rtcom_put_bit()` keeps `SIO_OUT` set
because the ARM7 drives SIO while shifting out a command or data byte.
`rtcom_get_bit()` clears `SIO_OUT` before asserting SCK to sample each bit,
so SIO is an input while the RTC chip drives the data. Leaving `SIO_OUT` set
during a read would create conflicting drivers on the bidirectional line and
corrupt the sampled bit.

**Label:** `REFERENCE VERIFIED` (SIO_OUT must be clear for input mode per the
Calico GPIO header) / `UNIT VERIFIED` (`ReadPathDirectionTest` in
`tools/test_rtcom_protocol.py` checks every data-phase write).

### RTC register 112 (`0x70`) — parameter byte

**REFERENCE VERIFIED** against reference `rtcom_requestAsync(u8 request, u8 param)`.

- Write command byte: `0x6C`
- Read command byte: `0x6D`

Register 112 receives the parameter byte. The prior draft wrote the opcode
here; the corrected driver writes the parameter here.

### RTC register 113 (`0x71`) — request / status byte

**REFERENCE VERIFIED** against reference `rtcom_requestAsync(u8 request, u8 param)`.

- Write command byte: `0x6E`
- Read command byte: `0x6F`

Register 113 receives the request/trigger byte and is written last, so the
service latches both bytes before acting. The driver reads the status/echo
byte from this same register. The asymmetry between the two registers is what
allows a command and its reply to be distinguished without a separate address
cycle.

### `0x4000134` — `REG_RCNT`

**REFERENCE VERIFIED** against Calico `calico/nds/arm7/gpio.h` and reference
`rtcom_beginComm()`.

`rtcom_init()` selects GPIO mode (`RCNT_MODE_GPIO`, bit 15) and enables the
SI/RTC IRQ path (`RCNT_SI_IRQ_ENABLE`, bit 8). The reference sets
`REG_RCNT = 0x8100`, i.e. exactly those two bits. Without them the poll in
`rtcom_poll_ack()` will never observe the interrupt.

### `REG_IF` — interrupt flags

**REFERENCE VERIFIED** against installed `nds/interrupts.h` and
`calico/nds/irq.h`.

`IRQ_NETWORK` is defined as `IRQ_RTC` in libnds; Calico defines
`IRQ_RTC = (1U << 7)`. The prior draft used bit 24, which is `IRQ_WIFI` in
legacy libnds, not `IRQ_NETWORK`. `IRQ_NETWORK` has always been `IRQ_RTC`
(bit 7) in the installed libnds 2.0/Calico toolchain.
`rtcom_poll_ack()` waits for that bit, then writes it back to acknowledge the
interrupt. Polling is used rather than an interrupt handler because the
handshake is synchronous and short-lived; an ISR would add state without
improving the timing.

The real `IRQ_NETWORK` response is `HARDWARE UNVERIFIED` on a console.

## Synchronization

RTCom synchronization is intentionally primitive:

- **Busy-wait polling.** `rtcom_poll_ack()` is a counted loop, not a blocking
  wait on a semaphore. Every call passes an explicit iteration bound. There is
  no path in the driver that waits without a bound.
- **Hard iteration-count timeouts.** The default acknowledge timeout is
  2,062,500 iterations. Exceeding it returns `RTCOM_ERR_TIMEOUT`; the caller
  decides whether to retry.
- **Deliberate delay asymmetry.** Bit timing uses
  `RTCOM_BIT_DELAY_SLOW = 48` iterations during the one-time upload.
  `RTCOM_BIT_DELAY_FAST = 14` is defined for future steady-state traffic but is
  not currently used.
- **Critical section.** Every top-level exchange (`rtcom_init()`,
  `rtcom_upload_ucode()`, `rtcom_execute_ucode()`) wraps the entire sequence in
  `enterCriticalSection()` / `leaveCriticalSection()` so no other code can
  observe a partial RTCom state or bit-bang the RTC bus concurrently.
- **REG_RCNT save/restore.** The value of `REG_RCNT` is saved before the
  exchange and restored after it. The driver only enables GPIO mode and the
  SI/RTC IRQ path for the duration of the exchange.
- **REG_IF clear-before-and-after-RCNT.** Any stale `IRQ_NETWORK` bit in
  `REG_IF` is acknowledged before configuring `REG_RCNT`, and acknowledged
  again immediately after writing `REG_RCNT`, before the first request is sent.
  The second clear is required because enabling the SI/RTC IRQ path can itself
  raise `IRQ_NETWORK` as a side effect of turning the mode on.
- **REG_IF clear on teardown and timeout.** `rtcom_bus_unlock()` clears any
  pending `IRQ_NETWORK` bit before restoring `REG_RCNT`, so it cannot fire once
  interrupts are re-enabled.  `rtcom_poll_ack()` also clears the bit on its
  timeout path, so a leftover flag from one timed-out exchange is not mistaken
  for the acknowledgement of a later exchange.
- **Single outstanding command.** The protocol has no command queue. A second
  opcode written before the first is acknowledged overwrites register 112. The
  driver therefore performs one request at a time and does not pipeline.

The upload window is the only place where the driver tolerates a long
operation. No interrupt-driven asynchronous path is implemented for ordinary
commands: the added complexity is not justified by a two-byte exchange.

## Error Handling

Errors are values, not resets. `RtcomStatus` has five members:

| Value | Condition | Caller action |
|-------|-----------|---------------|
| `RTCOM_OK` | Exchange completed and the expected status byte was received (`DONE` for the probe and post-upload wait, `ACK` for all other requests) | Continue |
| `RTCOM_ERR_TIMEOUT` | Acknowledge poll exhausted `RTCOM_ACK_TIMEOUT` | Retry or report |
| `RTCOM_ERR_NOT_READY` | Reply was not the expected status byte, or init retries exhausted | Retry or report |
| `RTCOM_ERR_UPLOAD_FAILED` | A byte exhausted its ten retries, the post-upload `DONE` wait failed, or finish failed | Abort upload |
| `RTCOM_ERR_BAD_ARG` | `rtcom_upload_ucode()` was called with `len == 0` or `len > 60 KiB` | Abort upload |

Rules enforced by the implementation:

- Every function returns a status. No function returns `void` and no function
  loops without a bound.
- `rtcom_init()` retries the whole probe exchange up to ten times.
- `rtcom_upload_ucode()` retries the whole upload attempt (from
  `UPLOAD_UCODE` through `FINISH_UCODE`) up to ten times.  Individual steps are
  **not** retried; resending a single step after the service has already
  advanced its receive cursor would corrupt the blob position.
- There is no exponential backoff and no infinite retry.
- A failed initialization does not attempt an upload. A failed upload does not
  attempt execution.
- The ARM7 reports each stage result to the ARM9 over the PXI user channel so a
  failure is visible on screen instead of hanging silently.

The RtcPwalker README describes its own RTCom implementation as **"SLOW and
UNSTABLE"**. That characterisation is consistent with this design: the channel
is a bit-banged serial line sharing silicon with clock hardware, and the service
side has frame-granularity gates. TWL-HD does not claim to have fixed those
properties. It claims only to bound every wait and to surface every failure.

Hardware behaviour remains `HARDWARE UNVERIFIED`. In particular, the real
`IRQ_NETWORK` response and the read-back timing of `SIO` must be confirmed on a
New 3DS before the driver can be called functional.

## TWL_FIRM / TwlBg.cxi Execution Model

**Label: `REFERENCE VERIFIED` (architectural analysis).**

A DS title under TWL_FIRM does not own the ARM11. The ARM11 runs `TwlBg.cxi`,
the TWL_FIRM background process that provides the compatibility layer and
compositing. RTCom ucode is not a separate application: it is a payload
uploaded at runtime into a reserved execution window inside that already-running
process.

The window is fixed:

- **Base:** `0x00201000`
- **Size:** 60 KiB (`0xF000` bytes)
- **Owner:** the running `TwlBg.cxi` process, not the DS title

The ARM7 driver uploads the blob with the `UPLOAD_UCODE` / `NEXT` /
`KEEPALIVE` / `FINISH_UCODE` sequence described in the upload section, then
sends `RTCOM_OP_EXECUTE_UCODE`. From that moment the blob executes on the ARM11
inside TwlBg's address space.

This has a direct consequence for repository layout. The existing `arm11/`
directory is a libctru `.3dsx` application. It is launched independently from
the 3DS home menu, it has its own process and address space, and it is not
running when a DS title is active under TWL_FIRM. It has **zero relationship**
to this transport and can never be the receiving end of RTCom traffic.

The correct Phase 3 shape is therefore:

- ARM7-side driver in the DS title binary (`arm7/`), and
- a bare-metal ARM11 ucode blob (`arm11_ucode/`) linked at `0x00201000` and
  uploaded into TwlBg.

`arm11/source/rtcom_transport.c` remains a Phase 4/5 stub for the standalone
renderer; it is not the RTCom endpoint. `docs/ARCHITECTURE.md` was corrected to
say so.

### Ucode injection safety gate

`arm7/source/main.c` gates the actual `rtcom_upload_ucode()` /
`rtcom_execute_ucode()` calls behind `#define TWLHD_RTCOM_UCODE_ENABLED 0`.
The default is off because the entry/return contract, while verified against
the reference sources, has not been independently reviewed and exercised on
hardware. When the gate is off the driver still performs `rtcom_init()`,
reports `RTCOM_TEST_UCODE_DISABLED` (code 5) for the upload/execute stage and
`RTCOM_TEST_ROUNDTRIP_SKIPPED` (code 6) for the echo stage to the ARM9, and
continues with the diagnostics path.

**Safety invariant.** With `TWLHD_RTCOM_UCODE_ENABLED` at 0, the only RTCom
request emitted by a full `main()` run is the `rtcom_init()` probe
(`RTCOM_REQ_PROBE`, `0x01`). No `UPLOAD_UCODE`, `FINISH_UCODE`,
`EXECUTE_UCODE`, `NEXT`, or `KEEPALIVE` request is ever sent. The ARM9 status
report uses the PXI user channel and sends nothing to RTCom. This invariant is
enforced by the `#if TWLHD_RTCOM_UCODE_ENABLED` blocks in `arm7/source/main.c`.

The upload mechanism is `REFERENCE VERIFIED` (architectural analysis) because it
follows from the address space and process model. The byte-level protocol inside
the window is `HARDWARE UNVERIFIED`; the Phase 3B ARM11 blob is a stub that
deliberately does not pretend otherwise.

## Bandwidth Conclusion

**Label: `HARDWARE UNVERIFIED` on hardware — conclusion is analytical.**

The channel's cost model is easy to state:

1. One command byte and one parameter byte per exchange.
2. Every exchange requires an acknowledge round-trip: write command, poll
   interrupt, read status.
3. Every bit costs 14–48 loop iterations of delay, plus the register writes
   themselves.
4. Channel setup alone consumes roughly 16.7 seconds: 200 frames for upload and
   1000 frames total before the service is ready at 60 Hz.

A `TwlhdFrame` is about **72 KiB**: 1024 commands at a nominal 72 bytes per
command, plus the fixed header. Moving one frame requires on the order of
73,000 bytes. At one acknowledged byte per exchange, that is on the order of
73,000 round-trips per frame. Even under optimistic assumptions about loop
timing, this is four to five orders of magnitude slower than a 60 Hz frame
budget allows.

**Full `TwlhdFrame` transport over RTCom is NOT FEASIBLE.** This is not a
tuning problem. The channel is a byte-at-a-time mailbox with a per-byte
acknowledge, and the frame is tens of thousands of bytes. No amount of batching
changes the fact that RTCom was designed for clock reads and small service
messages, not for graphics streams.

This conclusion is why Phase 3D exists. Phase 3D must change the transfer model
before Phase 4 can consume captured geometry: candidate directions include
on-ARM11 reconstruction from a compact command description, aggressive
decimation of the captured stream, or a different transport entirely. Phase 3B
ships the driver and the upload path because they are prerequisites for any of
those directions; it does **not** ship frame transport.

The estimate is analytical. No byte-rate measurement exists because no hardware
run was performed. The order of magnitude, however, is robust: the per-byte
acknowledge is part of the protocol, not an implementation detail.

## Licensing Constraint

TWL-HD is licensed **Apache-2.0** (see `LICENSE`). The reference sources are not
compatible with that licence:

- **RtcPwalker is GPLv3.** GPLv3 requires derivative works to be distributed
  under GPLv3. Copying code into an Apache-2.0 project, or translating it line
  by line, would make the combined work a GPLv3 derivative and would violate the
  Apache-2.0 grant for the copied portion.
- **Analog-Controls-for-NDS-Games-on-3DS has no LICENSE file.** With no licence
  granted, the default is "all rights reserved". There is no permission to copy,
  modify, or redistribute.

Therefore:

- No line of code in `arm7/`, `arm11_ucode/`, or any other Phase 3 file is
  copied or transliterated from either repository.
- Only non-copyrightable hardware facts were reused: register addresses,
  opcode values, status values, and timing constants.
- The implementation, naming, structure, comments, and error model are original
  to TWL-HD.
- Future contributors must not copy from either repository. If a future change
  needs behaviour that is only expressed in GPLv3 code, it must be re-derived
  from hardware documentation or a permissively licensed source.

The `.c`, `.h`, and `.ld` files carry SPDX identifiers and a clean-room notice
so the provenance is visible at the point of use.

## Verification Summary

| Claim | Label | Evidence |
|-------|-------|----------|
| RTCom uses the 3-wire RTC bus at `0x4000138` | `REFERENCE VERIFIED` | Calico `calico/nds/arm7/gpio.h` and reference behaviour |
| Opcode and status values in the tables above | `REFERENCE VERIFIED` | Hardware facts recorded in this audit |
| IRQ_NETWORK = IRQ_RTC = bit 7 | `REFERENCE VERIFIED` | `nds/interrupts.h`, `calico/nds/irq.h` |
| RTC pin mapping (SIO=0, SCK=1, CS=2; dir bits 4/5/6) | `REFERENCE VERIFIED` | Calico `calico/nds/arm7/gpio.h`; reference `rtcom.cpp` |
| Command and data bytes are MSB-first | `REFERENCE VERIFIED` | Reference `rtcom.cpp` `rtcTransferReversed()` comments/loop |
| SIO_OUT is clear during read data sampling (SIO input mode) | `REFERENCE VERIFIED` / `UNIT VERIFIED` | Calico GPIO direction-bit semantics; `tools/test_rtcom_protocol.py` `ReadPathDirectionTest` |
| Register 112 = parameter, register 113 = request/trigger | `REFERENCE VERIFIED` | Reference `rtcom_requestAsync(u8 request, u8 param)` |
| TwlBg reserves 60 KiB at `0x00201000` for ucode | `REFERENCE VERIFIED` | Reference `a11ucode.ld` linker scripts in `RtcPwalker/arm7_rtcom_patch/arm11_ucode/a11ucode.ld` and `Analog-Controls-for-NDS-Games-on-3DS` both set `ORIGIN = 0x00201000` |
| ARM11 ucode entry preserves r1-r12,lr and returns | `REFERENCE VERIFIED` | Reference `ucode_entry.s` in both reference repos |
| `arm11/` cannot receive RTCom traffic | `REFERENCE VERIFIED` (architectural analysis) | Separate process, not running under TWL_FIRM |
| Initialization/upload control flow is bounded | `BUILD VERIFIED` / `UNIT VERIFIED` | Successful ARM7 build; host protocol tests pass |
| Critical section around each top-level exchange | `BUILD VERIFIED` / `REFERENCE VERIFIED` | `enterCriticalSection()` / `leaveCriticalSection()` from libnds/Calico |
| REG_RCNT saved/restored around each exchange | `BUILD VERIFIED` | Implemented in `rtcom_bus_lock()` / `rtcom_bus_unlock()` |
| Stale REG_IF IRQ_NETWORK cleared before probe | `BUILD VERIFIED` | Implemented in `rtcom_bus_lock()` |
| REG_RCNT written as exact reference value `0x8100` | `BUILD VERIFIED` / `REFERENCE VERIFIED` | `rtcom_configure_rcnt()` writes `RCNT_MODE_GPIO | RCNT_SI_IRQ_ENABLE` |
| REG_IF IRQ_NETWORK cleared before and after RCNT write | `BUILD VERIFIED` / `UNIT VERIFIED` | Implemented in `rtcom_bus_lock()`; tested in `tools/test_rtcom_protocol.py` |
| REG_IF IRQ_NETWORK cleared on unlock and on poll timeout | `BUILD VERIFIED` | Implemented in `rtcom_bus_unlock()` / `rtcom_poll_ack()` |
| Upload retries whole attempts, not individual steps | `UNIT VERIFIED` | `tools/test_rtcom_protocol.py` upload retry policy test |
| Single-argument probe/finish/keepalive (no register-112 write) | `BUILD VERIFIED` / `REFERENCE VERIFIED` | Implemented via `rtcom_send_request_113_*` helpers |
| Opcode/status constants pinned to reference values | `UNIT VERIFIED` | `tools/test_rtcom_protocol.py` `ReferencePinningTest` |
| Custom ARM7 binary is packaged into `twlhd-rtcom.nds` | `BUILD VERIFIED` | `ndstool -i` shows ARM7 size 0x15D0; default `.nds` has 0xB374 |
| Full `TwlhdFrame` transport is not feasible | `HARDWARE UNVERIFIED` — analytical | Per-byte acknowledge model versus 72 KiB frame |
| Handshake succeeds on a real console | `HARDWARE UNVERIFIED` | No hardware available |
| Multi-step upload sequence on real hardware | `HARDWARE UNVERIFIED` | Protocol shape is `REFERENCE VERIFIED`; no hardware run |
| Round-trip echo (EXECUTE+NEXT+read-112) | `REFERENCE VERIFIED` shape / `HARDWARE UNVERIFIED` | Design documented; test skipped pending implementation |
| Bit-bang timing (48/14 iterations) is correct | `HARDWARE UNVERIFIED` | Constants recorded; not measured |

## Verification

Results recorded after the Phase 3 repair was performed in this worktree.
Labels follow the definitions at the top of this document.

| # | Check | Result |
|---|-------|--------|
| 1 | `make -C arm9` (clean) | `BUILD VERIFIED` — exit 0; `twlhd-arm9.nds` produced |
| 2 | `make -C arm11` (clean) | `BUILD VERIFIED` — exit 0; `twlhd-arm11.3dsx` produced |
| 3 | `make -C arm7` (clean) | `BUILD VERIFIED` — exit 0; `twlhd_arm7.elf` produced |
| 4 | `make -C arm11_ucode` (clean) | `BUILD VERIFIED` — exit 0; `twlhd_ucode.elf` and `.uc11` produced |
| 5 | `make rtcom_rom` | `BUILD VERIFIED` — exit 0; `twlhd-rtcom.nds` produced |
| 6 | `ndstool -i twlhd-rtcom.nds` ARM7 size | `BUILD VERIFIED` — 0x15D0 (5584 bytes); default `.nds` ARM7 is 0xB374 (45940 bytes) |
| 7 | `python3 -I -m unittest discover -s tools` | `UNIT VERIFIED` — 13 tests pass |
| 8 | `arm11_ucode/source/ucode_entry.s` compiles | `BUILD VERIFIED` — assembled and linked into `.elf`/`uc11` |
| 9 | `arm11_ucode/Makefile` entry-point check | `BUILD VERIFIED` — check runs and passes; `.uc11` disassembly starts with `push {r1-r12,lr}` / `bl` / `pop {r1-r12,pc}` |
| 10 | Negative control (broken linker order) | `BUILD VERIFIED` — check fails with "_start is not at base address 0x00201000"; restored to fixed order |
| 11 | Round-trip test skipped pending EXECUTE+NEXT+read-112 implementation | `BUILD VERIFIED` — `arm7/source/main.c` reports `RTCOM_TEST_ROUNDTRIP_SKIPPED` for both gate-off and gate-on paths |
| 12 | Opcode/status reference-pinning test | `UNIT VERIFIED` — `ReferencePinningTest` in `tools/test_rtcom_protocol.py` passes |
| 13 | Clean build (`make clean && make && make rtcom_rom`) | `BUILD VERIFIED` — exit 0, no warnings |
| 14 | Upload retry policy (whole-attempt, no per-step resend) | `UNIT VERIFIED` — `UploadRetryPolicyTest` in `tools/test_rtcom_protocol.py` passes |
| 15 | `REG_IF` cleared before and after `REG_RCNT` write | `UNIT VERIFIED` — `BusLockIrqOrderingTest` in `tools/test_rtcom_protocol.py` passes |
| 16 | Gate-off probe-only invariant | `BUILD VERIFIED` — with `TWLHD_RTCOM_UCODE_ENABLED` 0, `arm7/source/main.c` calls only `rtcom_init()`; upload/execute/round-trip are gated off |

Nothing in this document is claimed as working on real hardware. The ARM7
driver, the multi-step upload path, and the round-trip echo design are all
`HARDWARE UNVERIFIED` until a New 3DS run is recorded in
`docs/RTCOM_HARDWARE_TEST.md`.
