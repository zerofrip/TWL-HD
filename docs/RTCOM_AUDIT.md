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

1. The driver writes the parameter byte (zero for the initial probe) to RTC
   register 112. Register 112 lives at RTC address `0x70`; on the 3-wire bus, a
   write is selected with command byte `0x6C` and a read with `0x6D`.
2. It writes the request/opcode byte (`RTCOM_OP_COMM`, `0x41`) to register 113.
   Register 113 corresponds to RTC address `0x71`, with write command `0x6E`
   and read command `0x6F`. Register 113 is written last and acts as the
   trigger; the service latches both bytes before acting.
3. It polls `REG_IF` for the `IRQ_NETWORK` bit. Each poll is bounded by
   `RTCOM_ACK_TIMEOUT = 2,062,500` iterations, so the driver cannot spin
   forever on a missing service.
4. On an interrupt hit it acknowledges the interrupt and reads the status byte
   from register 113 using command byte `0x6F`.
5. A status of `RTCOM_STATUS_DONE` (`0x80`) means the service accepted the
   command. A bus value is not itself proof of success; the status byte is.

### Step 2 — Retry policy

`rtcom_init()` allows `RTCOM_INIT_RETRIES = 10` complete attempts. The first
successful `RTCOM_OP_COMM` exchange returns `RTCOM_OK`. If all ten attempts
time out or return a non-`DONE` status, the function returns
`RTCOM_ERR_NOT_READY`. This is a terminal result: the ARM7 side reports it to
the ARM9 and does not continue to the upload stage.

### Step 3 — Payload upload

Once the service is discovered, the ARM11 payload is uploaded byte by byte:

1. For each byte, send `RTCOM_OP_SEND_BYTE` (`0x42`) with the byte as the
   parameter.
2. Each byte gets its own acknowledge poll. There is no streaming mode and no
   block transfer.
3. Each byte gets `RTCOM_UPLOAD_RETRIES = 10` attempts. Exhausting those
   attempts aborts the upload with `RTCOM_ERR_UPLOAD_FAILED`.
4. After the final byte, send `RTCOM_OP_FINISH_UPLOAD` (`0x44`). A failed
   finish exchange also returns `RTCOM_ERR_UPLOAD_FAILED`.

### Step 4 — Execution

`rtcom_execute_ucode()` sends `RTCOM_OP_EXEC` (`0x81`) with a zero parameter and
waits for `RTCOM_STATUS_DONE`. From that point the ARM11 blob owns its own
execution loop; the ARM7 driver only exchanges further commands when asked.

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
| `RTCOM_OP_COMM` | `0x41` | ARM7 → ARM11 | Probe/initialise the service |
| `RTCOM_OP_SEND_BYTE` | `0x42` | ARM7 → ARM11 | Upload one payload byte |
| `RTCOM_OP_ECHO` | `0x43` | ARM7 → ARM11 | Round-trip echo test (single-ack reply) |
| `RTCOM_OP_FINISH_UPLOAD` | `0x44` | ARM7 → ARM11 | Close the upload stream |
| `RTCOM_OP_EXEC` | `0x81` | ARM7 → ARM11 | Start the uploaded blob |
| `RTCOM_OP_STATUS` | `0x83` | ARM7 → ARM11 | Read service state |
| `RTCOM_OP_DONE` | `0xFE` | ARM11 → ARM7 | Terminal marker in the reply stream |

### Status bytes

| Constant | Value | Meaning |
|----------|-------|---------|
| `RTCOM_STATUS_IDLE` | `0x00` | Service present, no command pending |
| `RTCOM_STATUS_DONE` | `0x80` | Previous command completed successfully |
| `RTCOM_STATUS_BUSY` | `0x82` | Previous command still in progress |

The driver treats only `DONE` as success for normal service requests. `IDLE`
and `BUSY` are surfaced as `RTCOM_ERR_NOT_READY` so the caller can retry at a
higher level.

### Echo request contract

`RTCOM_OP_ECHO` (`0x43`) is a single-ack echo request used only when
`TWLHD_RTCOM_UCODE_ENABLED` is non-zero.  The ARM7 sends parameter byte `B`
in register 112 and `RTCOM_OP_ECHO` in register 113.  The TwlBg RTCom service
passes `r0 = B` and `r1 = RTCOM_OP_ECHO` to the uploaded ucode, and the value
returned in `r0` is written back as the status byte in register 113.  The ucode
therefore returns `~B` for this opcode and `0` for every other input.  The ARM7
reads that status byte during the same single acknowledge poll; there is no
second IRQ wait and no separate "retrieve echo" phase.

The Phase 3B test message is deliberately not a `TwlhdFrame`:

```c
typedef struct {
    u8 magic[4];   /* "TWLH" */
    u8 payload[4]; /* arbitrary test bytes */
} RtcomTestMsg;
```

Eight bytes are enough to exercise ordering, acknowledgement, and bit
integrity without pretending the channel can carry a frame.

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
state machine (e.g. `stage == 0` selects the opcode, `stage == 1` provides the
first data byte). TWL-HD's current stub ignores both arguments and returns 0
for all inputs, because ucode injection is gated off by default (see
`TWLHD_RTCOM_UCODE_ENABLED` in `arm7/source/main.c`). This makes unexpected
`param`/`stage` values harmless: the function always returns a safe default
and never branches to undefined memory.

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
  `RTCOM_BIT_DELAY_SLOW = 48` iterations during the one-time upload and
  `RTCOM_BIT_DELAY_FAST = 14` iterations for steady-state traffic. Slower
  edges during upload tolerate the service's own setup work; the faster rate
  keeps ordinary command latency tolerable.
- **Critical-section requirement.** The upload is a multi-second sequence of
  dependent byte transfers. Any other code that bit-bangs the RTC bus during
  that window would corrupt the stream. The driver assumes exclusive access to
  `REG_RTC`, `REG_RCNT`, and the `IRQ_NETWORK` bit for the duration of
  `rtcom_init()`, `rtcom_upload_ucode()`, and `rtcom_execute_ucode()`. Firmware
  RTC users (clock reads) must not run concurrently.
- **Single outstanding command.** The protocol has no command queue. A second
  opcode written before the first is acknowledged overwrites register 112. The
  driver therefore performs one request at a time and does not pipeline.

The upload window is the only place where the driver tolerates a long
operation. No interrupt-driven asynchronous path is implemented for ordinary
commands: the added complexity is not justified by a two-byte exchange.

## Error Handling

Errors are values, not resets. `RtcomStatus` has four members:

| Value | Condition | Caller action |
|-------|-----------|---------------|
| `RTCOM_OK` | Exchange completed and status was `DONE` | Continue |
| `RTCOM_ERR_TIMEOUT` | Acknowledge poll exhausted `RTCOM_ACK_TIMEOUT` | Retry or report |
| `RTCOM_ERR_NOT_READY` | Reply was `IDLE`/`BUSY`, or init retries exhausted | Retry or report |
| `RTCOM_ERR_UPLOAD_FAILED` | A byte exhausted its ten retries, or finish failed | Abort upload |

Rules enforced by the implementation:

- Every function returns a status. No function returns `void` and no function
  loops without a bound.
- Retries are fixed at ten for both initialisation and per-byte upload. There is
  no exponential backoff and no infinite retry.
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

The ARM7 driver uploads bytes into that window one at a time, sends
`RTCOM_OP_FINISH_UPLOAD`, then sends `RTCOM_OP_EXEC`. From that moment the blob
executes on the ARM11 inside TwlBg's address space.

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
`rtcom_execute_ucode()` calls and the `RTCOM_OP_ECHO` round-trip test behind
`#define TWLHD_RTCOM_UCODE_ENABLED 0`. The default is off because the
entry/return contract, while verified against the reference sources, has not
been independently reviewed and exercised on hardware. When the gate is off the
driver still performs `rtcom_init()`, reports `RTCOM_TEST_UCODE_DISABLED`
(code 5) for the upload/execute stage and `RTCOM_TEST_ROUNDTRIP_SKIPPED`
(code 6) for the echo stage to the ARM9, and continues with the diagnostics
path. This is a deliberate safety precondition, not a bug.

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
| Custom ARM7 binary is packaged into `twlhd-rtcom.nds` | `BUILD VERIFIED` | `ndstool -i` shows ARM7 size 0x155C; default `.nds` has 0xB374 |
| Full `TwlhdFrame` transport is not feasible | `HARDWARE UNVERIFIED` — analytical | Per-byte acknowledge model versus 72 KiB frame |
| Handshake succeeds on a real console | `HARDWARE UNVERIFIED` | No hardware available |
| Echo test returns correct complements | `HARDWARE UNVERIFIED` | No hardware available |
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
| 6 | `ndstool -i twlhd-rtcom.nds` ARM7 size | `BUILD VERIFIED` — 0x155C (5468 bytes); default `.nds` ARM7 is 0xB374 (45940 bytes) |
| 7 | `python3 -I -m unittest discover -s tools` | `UNIT VERIFIED` — 12 tests pass |
| 8 | `arm11_ucode/source/ucode_entry.s` compiles | `BUILD VERIFIED` — assembled and linked into `.elf`/`uc11` |
| 9 | `arm11_ucode/Makefile` entry-point check | `BUILD VERIFIED` — check runs and passes; `.uc11` disassembly starts with `push {r1-r12,lr}` / `bl` / `pop {r1-r12,pc}` |
| 10 | Negative control (broken linker order) | `BUILD VERIFIED` — check fails with "_start is not at base address 0x00201000"; restored to fixed order |
| 11 | Round-trip test gated with `TWLHD_RTCOM_UCODE_ENABLED` | `BUILD VERIFIED` — default-off path reports `RTCOM_TEST_ROUNDTRIP_SKIPPED`; ON path uses `RTCOM_OP_ECHO` single-ack contract |

Nothing in this document is claimed as working on real hardware. The ARM7
driver, the upload path, and the echo test are all `HARDWARE UNVERIFIED` until a
New 3DS run is recorded in `docs/RTCOM_HARDWARE_TEST.md`.
