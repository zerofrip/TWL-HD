<!-- SPDX-License-Identifier: Apache-2.0 -->
# EQ-4 — NTR-HR capture of the DS-mode top screen — hardware trial result (2026-11-19)

**Scope:** DOCUMENTATION ONLY. The owner-approved run of 2026-11-19 was a read-only
NTR-HR capture (`tools/ntr_capture`, which performs no console writes,
`tools/ntr_capture/README.md:235-238`); no code, test, tool, ROM, fixture or
Makefile changed. The only file this task adds to the repository is this report;
`docs/EQUALITY_CHECK_DESIGN.md` is amended only where it stated option (c)/EQ-4
as UNKNOWN or pending.

**Labels.** FACT = a file:line read in this session, or a command output quoted
verbatim below. HARDWARE = measured on the console in this run. UNVERIFIED = not
measured. Nothing outside the quoted output is FACT; no cause is inferred.

**Run:** New 3DS `192.168.1.32`; NTR-HR Remote Play (JPEG Compat UDP) started by
the owner in normal 3DS mode; PC `192.168.1.27`. Capture command (as run by the
owner's script `eq4_capture.sh`):

    ntr_capture --console-ip 192.168.1.32 --pc-ip 192.168.1.27 --screen both \
        --frames 1 --timeout 12 --out <prefix>

(`ntr_capture` from `tools/ntr_capture`, read-only, no console writes —
`tools/ntr_capture/README.md:235-238`.)

## 1. Control — 3DS mode (2026-11-19 13:16:59–13:17:03 JST)

**FACT (HARDWARE, measured):** the control captured both screens. Raw output,
verbatim:

```
== base round 1 13:16:59
[2026-11-19 13:17:03] heartbeats sent: 4 total
[2026-11-19 13:17:03] datagrams from non-console addresses ignored: 0
[2026-11-19 13:17:03] closing sockets (the viewer has no stop command; close only, ntr_hb.c:26-37)
```

Files written:

| File | Size |
|---|---|
| `base_1_top_0.ppm` | 288015 B |
| `base_1_bottom_0.ppm` | 230415 B |
| `base_1_bottom_1.ppm` | 230415 B |

The sizes match the tool's documented PPM sizes ("about 288 KB for the top
screen and 230 KB for the bottom", `tools/ntr_capture/README.md:238-239`).

## 2. DS mode — 12 attempts, all failed

**FACT (HARDWARE, measured):** the owner then started
`/roms/nds/twlhd-s5cull-plain.nds` (sha256 prefix `1bde37007ab36c05`, the same
ROM as Run 23/24) from TWiLight Menu++ with remote play left running. 12 captures
were taken 8 s apart (13:18:34 .. 13:20:38). **All 12 failed and no frame was
written.** The complete output of the run, verbatim, from the command
`sh eq4_capture.sh ds 12 8`:

```
== ds round 1 13:18:34
[2026-11-19 13:18:34] UDP bound on 192.168.1.27:8001
[2026-11-19 13:18:34] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:18:40] TCP connect to 192.168.1.32:8000 failed: Connection timed out
== ds round 2 13:18:48
[2026-11-19 13:18:48] UDP bound on 192.168.1.27:8001
[2026-11-19 13:18:48] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:18:51] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 3 13:18:59
[2026-11-19 13:18:59] UDP bound on 192.168.1.27:8001
[2026-11-19 13:18:59] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:02] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 4 13:19:10
[2026-11-19 13:19:10] UDP bound on 192.168.1.27:8001
[2026-11-19 13:19:10] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:13] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 5 13:19:21
[2026-11-19 13:19:21] UDP bound on 192.168.1.27:8001
[2026-11-19 13:19:21] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:24] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 6 13:19:32
[2026-11-19 13:19:32] UDP bound on 192.168.1.27:8001
[2026-11-19 13:19:32] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:35] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 7 13:19:43
[2026-11-19 13:19:43] UDP bound on 192.168.1.27:8001
[2026-11-19 13:19:43] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:46] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 8 13:19:54
[2026-11-19 13:19:54] UDP bound on 192.168.1.27:8001
[2026-11-19 13:19:54] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:19:57] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 9 13:20:05
[2026-11-19 13:20:05] UDP bound on 192.168.1.27:8001
[2026-11-19 13:20:05] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:20:08] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 10 13:20:16
[2026-11-19 13:20:16] UDP bound on 192.168.1.27:8001
[2026-11-19 13:20:16] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:20:19] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 11 13:20:27
[2026-11-19 13:20:27] UDP bound on 192.168.1.27:8001
[2026-11-19 13:20:27] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:20:30] TCP connect to 192.168.1.32:8000 failed: No route to host
== ds round 12 13:20:38
[2026-11-19 13:20:38] UDP bound on 192.168.1.27:8001
[2026-11-19 13:20:38] connecting to 192.168.1.32:8000 (TCP)
[2026-11-19 13:20:41] TCP connect to 192.168.1.32:8000 failed: No route to host
```

Every round printed the `UDP bound` line, the `connecting` line and a failure;
round 1's failure was `Connection timed out`, rounds 2-12's was `No route to host`.

## 3. Not measured (UNVERIFIED)

- **UNVERIFIED:** why the console disappears from the network. The owner did not
  observe the remote-play screen or the console state while the DS ROM ran.
- **UNVERIFIED:** whether remote play could be re-enabled while a DS ROM runs.
- **UNVERIFIED / not an indicator:** ping gets no ICMP reply even in the
  3DS-mode control, so ping cannot distinguish the two modes.

## 4. Verdict

**EQ-4: NTR-HR remote play (TCP 8000 / UDP 8001) is NOT reachable while a DS ROM
runs in TWL/DS mode on this console (one run, 12 attempts, 2026-11-19); option
(c) is not available as a numeric DS-mode capture.**

Consequences, stated as fact of plan (no hypothesis):

- The remaining hardware options are EQ-5 (owner photos) and EQ-6 (DS-side
  framebuffer dump from our own ROM).
- EQ-3 comparisons against the DeSmuME framebuffer are unaffected: they are
  host-only and need no console.

## 5. Requirement traceability (docs only, 2026-11-19)

Docs-only task: this report is new and `docs/EQUALITY_CHECK_DESIGN.md` is amended
only at the statements that called option (c)/EQ-4 UNKNOWN or pending. No code,
test, tool, ROM, fixture or Makefile change; no test weakened. `TESTS: make test`
— the docs are not parsed by the tests, so the check is that nothing regressed.

- **REQ-1 (record the setup).** The console `192.168.1.32`, the owner-started
  JPEG Compat remote play, and the full capture command are quoted above
  (section "Run"); read-only guarantee cited at `tools/ntr_capture/README.md:235-238`.
- **REQ-2 (control quoted).** The 3DS-mode raw block and the three file sizes are
  quoted verbatim in section 1.
- **REQ-3 (DS-mode attempts quoted).** The complete 12-round raw output (all
  attempts, verbatim) and the 12-attempt/8 s-apart timeline are in section 2.
- **REQ-4 (UNVERIFIED list).** Section 3 lists exactly the three unmeasured items,
  including the ping caveat.
- **REQ-5 (verdict in both files).** Section 4 carries the verdict verbatim;
  `docs/EQUALITY_CHECK_DESIGN.md` carries the same verdict in the §4 EQ-4 and §6.3
  D2 "Result (2026-11-19)" blocks and the shorter result statement in §3.3(c), each
  with a pointer here.
- **REQ-6 (design-doc D2 result block).** `docs/EQUALITY_CHECK_DESIGN.md` §6.3 D2
  keeps its original recommendation text and appends a "Result (2026-11-19)"
  block; §4 EQ-4 does the same, the option table row (c) records the result, and
  the trial-design text elsewhere is retained.
- **REQ-7 (no code/test change).** `git status --short` lists only this report and
  `docs/EQUALITY_CHECK_DESIGN.md`; `make test` unchanged.
