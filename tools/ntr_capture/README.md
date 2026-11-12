# ntr_capture / ntr_input — headless NTR-HR remote play and input

This directory holds two independent, opt-in host tools:

- **`ntr_capture`** — headless client for NTR-HR's **JPEG Compat (UDP)** remote
  play. It connects to the console's TCP port 8000, sends the same start
  command NTRViewer-HR sends, reassembles the UDP stream on port 8001,
  JPEG-decodes each screen, rotates the portrait framebuffer to landscape, and
  writes one P6 PPM per screen.
- **`ntr_input`** — button/touch input redirection (20-byte UDP frames to port
  4950), for automating hardware tests. See
  [ntr_input](#ntr_input--input-redirection-buttons-and-touch) below.

Status: **BUILD/UNIT only**. Both tools were built and tested on the host
(`ntr_capture` with synthetic packets, `ntr_input` with a pure frame builder);
neither was **connected to a console** during implementation. Nothing from
NTRViewer-HR or libjpeg is vendored in this repository.

## Build and run

```sh
make ntr-capture
./tools/ntr_capture/build/ntr_capture --console-ip 192.168.1.32 \
    --pc-ip 192.168.1.5 --out /tmp/shot
```

`make ntr-capture` is opt-in: it is not part of `make all` or `make test`.
`make clean` removes `tools/ntr_capture/build/`.

Requirements: a C11 compiler, POSIX sockets, and libjpeg headers/library
(`/usr/include/jpeglib.h`, `-ljpeg`). The target checks for them and fails with
one clear line if missing.

The viewer's bundled `jpeg_turbo` is referenced through
`NTRVIEWER_DIR ?= /home/one/3ds/ntrviewer-hr`, but it cannot be built standalone
on this host: it hard-codes `WITH_SIMD 1` (`jpeg_turbo/jconfig.h:22`) and needs
`nasm` for `jpeg_turbo/simd/x86_64/*.asm`; `nasm` is absent. The target
therefore falls back to the system libjpeg-turbo headers, as allowed by the
task contract. Viewer sources are never copied here.

Options:

| Option | Meaning |
|---|---|
| `--console-ip <ip>` | console IPv4 (required unless `--dry-run`; `--host` is an alias) |
| `--console-port <n>` | console TCP port, default `8000` (`--port` is an alias) |
| `--pc-ip <ip>` | **local** IPv4 address the UDP socket binds to (required unless `--dry-run`) |
| `--udp-port <n>` | local UDP port, default `8001` |
| `--screen top\|bottom\|both` | screen selection through the priority field (see below) |
| `--quality <n>` | JPEG quality `10..100`, default `95` |
| `--priority-factor <n>` | priority factor for `both`, `0..255`, default `1` |
| `--bandwidth <n>` | bandwidth cap in Mbps `4..20`, default `16` |
| `--frames <n>` | frames per selected screen to write, default `1` |
| `--timeout <sec>` | overall timeout, default `15` |
| `--out <prefix>` | PPM prefix, default `ntr_capture` |
| `--dry-run` | print the exact TCP bytes and ports; open no sockets |

`--pc-ip` is the address this host binds its UDP socket to, so it must be one of
the machine's own IPv4 addresses. The console is told **only the UDP port**, never
a PC address: `args[4]` carries `(uint16_t)rp_port_last` plus the KCP/lossless
flag bits (`ntr_hb.c:381-386`). There is no PC-IP argument to send.

Output names: `<prefix>_top_<n>.ppm` and `<prefix>_bottom_<n>.ppm`, where `<n>`
counts from 0 per screen. Exit codes follow `tools/azahar_host`: `2` usage
errors, `1` runtime errors, `0` success. Every console-facing action and every
runtime error is one timestamped stderr line.

## Protocol summary (viewer citations)

All facts below were read from `/home/one/3ds/ntrviewer-hr` (MIT). The viewer's
menu thread connects TCP to the console on port **8000**
(`main.c:823-829`), and the UDP stream listens on port **8001**
(`ntr_common.c:22`).

### TCP packet (84 bytes, native little-endian)

`struct tcp_packet_hdr` (`ntr_hb.c:8-19`) is:

| Offset | Field |
|---|---|
| 0 | `magic = 0x12345678` (`ntr_hb.c:9`) |
| 4 | `seq` |
| 8 | `type` (0) |
| 12 | `cmd` |
| 16 | `args[16]` (64 bytes) |
| 80 | `data_len` |

`tcp_send_packet_header()` memcpy()s that struct straight to the socket
(`ntr_hb.c:160-179`). The reference viewer is x86-64, so the bytes are
little-endian.

A heartbeat is command `0` with all-zero arguments (`ntr_hb.c:327`). The
remote-play start/update command is **901** (`ntr_hb.c:393-395`), with
(`ntr_hb.c:377-386`):

| Arg | Value |
|---|---|
| `args[0]` | `(top_screen_priority << 8) \| priority_factor` |
| `args[1]` | JPEG quality |
| `args[2]` | bandwidth limit × 128 × 1024 |
| `args[3]` | guarding magic `1404036572` |
| `args[4]` | UDP port, plus KCP/lossless flag bits (zero for JPEG Compat) |
| `args[5]` | audio (`2` = in-band KCP audio, else 0) |
| `args[6]` | full width (`2` = both, `1` = right, else 0) |
| `args[7..15]` | zero |

`--screen top` sets priority top with factor 0; `--screen bottom` sets priority
bottom with factor 0; `--screen both` sets priority top and the requested
factor. The viewer documents factor 0 as "only the prioritized screen is
streamed" (`README.md` "Priority Screen Factor"), which is how screen selection
is expressed in this fixed 16-argument command.

### Viewer start order

On connect the viewer sends a heartbeat with `seq = 0`, increments the sequence,
and then sends command 901 with `seq = 1`; later heartbeats continue from
`seq = 2` (`ntr_hb.c:226-245,327,393-409`). This client does the same. The
viewer has **no stop command**: it shuts the socket down and closes it
(`ntr_hb.c:26-37,205-213`); this client does the same and says so in its final
log line.

### UDP frame

Each datagram is a 4-byte header plus up to 1444 bytes of JPEG data
(`RP_PACKET_SIZE 1448`, `RP_DATA_HDR_SIZE 4`, `RP_PACKET_DATA_SIZE 1444`;
`const.h:229-231`):

| Byte | Meaning |
|---|---|
| 0 | frame id |
| 1 | screen: the whole byte after clearing bit 4 — `0x00` = bottom, any nonzero value = top; bit 1 = full width (top only); bit 4 = last packet of the frame (`ntr_rp.c:1795-1796,1833`) |
| 2 | must satisfy `(b & ~0x0d) == 2`; bit 0 lossless; bits 2-3 downsample (`ntr_rp.c:1827-1838`) |
| 3 | packet number |

The frame identity is bytes 0-2 **after clearing the end bit**
(`ntr_rp.c:1836,1843`). Byte 1 is a whole-byte screen selector, not a single
bit: after the end bit is cleared, `0x00` is the bottom screen and any nonzero
value is the top screen (`ntr_rp.c:1795-1796`). Byte 1 bit 1 requests a
full-width top frame (~800 px wide) and is only meaningful for top frames. A
new identity starts a new frame. A non-end packet must
be exactly 1444 bytes. Packet numbers wrap by 256 (`ntr_rp.c:1900-1902`). On the
end packet the viewer checks that every packet `0..end_packet` arrived; a
missing packet discards the whole frame (`ntr_rp.c:1930-1948`). The end packet's
own position and length bound the assembled JPEG, so a frame that fits in one
datagram (packet 0 carrying the end bit) is emitted with its real size, not
zero. This client mirrors those rules in `ntr_cap_feed()`; duplicates are
counted and ignored. Datagrams whose source address is not `--console-ip` are
dropped before reassembly: the first one is logged with its address and the
total is reported at shutdown.

Displayed sizes are 400×240 (top) and 320×240 (bottom); the JPEG itself is the
portrait 240×400 / 240×320 framebuffer (`const.h:189-192`,
`ntr_rp.c:813-853`). The SDL renderer rotates the upright texture by −90°
(`ui_renderer_sdl.c:450`); this client applies the equivalent 90°
counter-clockwise rotation (`ntr_cap_rotate_ccw`), so the PPM is landscape.

## Dry-run hex versus the viewer's bytes

`--dry-run` opens no sockets. For `--console-ip 192.168.1.32 --pc-ip
192.168.1.5 --console-port 8000 --screen both --quality 95 --bandwidth 16
--udp-port 8001` it prints:

```
heartbeat cmd 0 seq 0 (84 bytes):
  0000  78 56 34 12 00 00 00 00 00 00 00 00 00 00 00 00
  0016  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0032  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0048  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0064  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0080  00 00 00 00
remote-play cmd 901 seq 1 (84 bytes):
  0000  78 56 34 12 01 00 00 00 00 00 00 00 85 03 00 00
  0016  01 01 00 00 5f 00 00 00 00 00 20 00 dc e5 af 53
  0032  41 1f 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0048  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0064  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0080  00 00 00 00
```

The same two buffers come out of the viewer's expressions, byte for byte:

```
tcp_send_packet_header(sockfd, 0, 0, 0, 0, 0, 0)                    # ntr_hb.c:327
  78 56 34 12 00 00 00 00 00 00 00 00 00 00 00 00 ... 00

args[] = { 0x0101, 95, 0x00200000, 1404036572, 0x1f41, 0, 0 }      # ntr_hb.c:377-386
tcp_send_packet_header(sockfd, 1, 0, 901, args, 7, 0)              # ntr_hb.c:393-395
  78 56 34 12 01 00 00 00 00 00 00 00 85 03 00 00
  01 01 00 00 5f 00 00 00 00 00 20 00 dc e5 af 53
  41 1f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ... 00
```

The viewer hex is derived by applying those expressions to the same inputs; it
is not a console packet trace.

## UNVERIFIED items

- No console connection was made. Whether NTR-HR accepts the 901 command
  immediately after connect (without first receiving the console's heartbeat
  header) is inferred from the viewer's send order, not observed.
- UDP frame ids, downsample bits, packet ordering and JPEG dimensions from a
  real game stream were not observed. Reassembly is tested with synthetic
  packets only.
- A live capture on 2026-11-08 20:03 (console 192.168.1.32) established the
  correct byte-1 screen semantics: bottom packets carried 240×400 JPEGs and
  top packets carried 240×320, matching the whole-byte mapping
  (`ntr_rp.c:1795-1796`). The earlier single-bit reading came from the KCP
  path (`ntr_rp.c:242`) and was inverted.
- Output orientation is mirrored from the viewer's SDL renderer. The OpenGL
  renderer swaps texture coordinates equivalently by inspection, but no PPM
  was visually compared with a running viewer.
- The localhost self-test (`tools/ntr_capture/selftest.sh`) proves only the
  end-to-end host path against a synthetic console; it says nothing about
  NTR-HR's own behaviour.
- Heartbeat cadence (250 ms) is copied from `HEART_BEAT_EVERY_MS`
  (`ntr_hb.c:7`); the minimum rate NTR-HR tolerates is not documented.
- IPv4 only.

## Limits

- **JPEG is lossy.** PPM values are libjpeg-turbo's RGB output and can differ
  from the console framebuffer by a few levels.
- **Frame rate.** One PPM per screen per `--frames` is written; this is a
  capture client, not a video player. Throughput is bounded by the console and
  PPM writes.
- **UDP loss.** A lost packet discards the whole frame (viewer semantics); there
  is no retransmission. Duplicates are ignored. Reordering is tolerated until
  a new frame identity arrives.
- **Full-resolution JPEG Compat only.** Downsample values 2 and 3 are skipped
  with a log line. The UDP full-width flag is parsed and stored; a full-width
  top frame (~800 px wide) is skipped explicitly with a log line because the
  portrait JPEG cap does not cover it. KCP / reliable stream, delta, lossless,
  audio and full-width *requests* are not implemented: the remote-play
  `args[6]` full-width field remains 0.
- **No input redirection from `ntr_capture`** and no console writes of any
  kind. Input redirection lives in the separate `ntr_input` binary below; the
  capture binary and its target are unchanged.
- PPM is uncompressed: about 288 KB for the top screen and 230 KB for the
  bottom.

## Console access and policy

Remote play must be enabled on the console: NTR-HR menu (X+Y) → Remote Play,
using a mode reported as **JPEG Compat (UDP)**. The PC must be able to receive
UDP on port 8001 (bound to the `--pc-ip` address) and connect to TCP 8000.

This tool performs only the actions the owner already allowed on 2026-11-07:
NTR-HR streaming from TCP 8000 / UDP 8001. Brick-risk operations (NAND, system
titles, boot chain, TWL/DS-mode path, formatting) still require owner approval.
Every console action must be logged and reported. See
`/home/one/3ds/hwtest-staging/CONSOLE_ACCESS_POLICY.md`.

## License

NTRViewer-HR is MIT-licensed; only protocol facts and file:line citations are
used here. No viewer code and no libjpeg source is vendored. The binary links
the host's libjpeg-turbo, whose licence is the usual libjpeg BSD-style licence.

## Mutation evidence

Baseline: `make test` green (all host tests, including 16 groups in
`test_ntr_capture_util`; 86 Python tests OK), exit 0.

Each mutant was applied to a copy of `ntr_capture_util.h`, the unchanged test
source was rebuilt with the same `-Wall -Wextra -Werror` + ASan/UBSan flags, and
`make test` was run. Every mutant turned `make test` red (`rc=2`, the host-test
loop stops at the first failing test). The header was then restored; the final
`git diff` of the header is empty and the baseline is green again.

| Helper | Mutant | Before | After (verbatim first failing check) |
|---|---|---|---|
| `ntr_cap_build_remote_play` | (a) `args[0]` priority factor shifted left by 1 | PASS | exit 1: `test_ntr_capture_util.c:47: expected 0x0101 == 257, got 258` |
| `ntr_cap_build_remote_play` | (b) bandwidth uses ×1000 instead of ×1024 | PASS | exit 1: `test_ntr_capture_util.c:49: expected 16 * 128 * 1024 == 2097152, got 2048000` |
| `ntr_cap_build_heartbeat` | (c) heartbeat command becomes 1 | PASS | exit 1: `test_ntr_capture_util.c:82: expected 0 == 0, got 1` |
| `ntr_cap_build_heartbeat` | (d) heartbeat sequence becomes `seq ^ 1` | PASS | exit 1: `test_ntr_capture_util.c:80: expected 42 == 42, got 43` |
| `ntr_cap_udp_parse` | (e) whole-byte screen mapping inverted | PASS | exit 1: `test_ntr_capture_util.c:108: expected 0 == 0, got 1` |
| `ntr_cap_udp_parse` | (f) full-width bit ignored (`full_width` always 0) | PASS | exit 1: `test_ntr_capture_util.c:136: expected 1 == 1, got 0` |
| `ntr_cap_udp_parse` | (g) bit-0-only screen mapping instead of whole byte | PASS | exit 1: `test_ntr_capture_util.c:135: expected 1 == 1, got 0` |
| `ntr_cap_udp_parse` | (h) end-flag mask `0x10` → `0x20` | PASS | exit 1: `test_ntr_capture_util.c:125: expected 1 == 1, got 0` |
| `ntr_cap_feed` | (i) missing-packet test inverted | PASS | exit 1: `test_ntr_capture_util.c:197: expected NTR_CAP_FEED_READY == 2, got 1` |
| `ntr_cap_feed` | (j) non-end size check removed | PASS | exit 1: `test_ntr_capture_util.c:269: expected NTR_CAP_FEED_IGNORED == 0, got 1` |
| `ntr_cap_rotate_ccw` | (k) source row not mirrored | PASS | exit 1: `test_ntr_capture_util.c:355: CHECK failed: memcmp(dst, expected, sizeof(expected)) == 0` |
| `ntr_cap_rotate_ccw` | (l) last destination row skipped | PASS | exit 1: `test_ntr_capture_util.c:355: CHECK failed: memcmp(dst, expected, sizeof(expected)) == 0` |
| `ntr_cap_ppm_header` | (m) magic `P3` instead of `P6` | PASS | exit 1: `test_ntr_capture_util.c:370: CHECK failed: strcmp(buf, "P6\n400 240\n255\n") == 0` |
| `ntr_cap_ppm_header` | (n) zero width accepted | PASS | exit 1: `test_ntr_capture_util.c:378: expected -1 == -1, got 13` |
| `ntr_cap_addr_is` | (o) comparison inverted (`==` → `!=`) | PASS | exit 1: `test_ntr_capture_util.c:329: expected 1 == 1, got 0` |
| `ntr_cap_addr_is` | (p) always matches (source filter removed) | PASS | exit 1: `test_ntr_capture_util.c:331: expected 0 == 0, got 1` |
| `ntr_cap_feed` | (q) end-packet size assignment removed | PASS | exit 1: `test_ntr_capture_util.c:306: expected 100 == 100, got 0` |

Two reviewed fixes are **not** mutation-testable under this protocol. The
`volatile` qualifier on `decode_jpeg_rgb`'s `rgb` pointer is a C11 7.13.2.1
compiler rule: dropping it still builds and passes every test. The
pre-decompress `image_width`/`image_height` cap lives in `ntr_capture.c`, while
this protocol mutates only `ntr_capture_util.h`.

## Localhost self-test

`tools/ntr_capture/selftest.sh` (opt-in, never part of `make test`) runs the
real binary against a fake console on `127.0.0.1`: a TCP listener reads the two
84-byte commands, a UDP sender streams synthetic 240×400 and 240×320 JPEG
frames, and one decoy datagram from `127.0.0.2` proves the console-address
filter drops it. It needs `python3`, the built binary, and any of PIL,
ImageMagick, `cjpeg`, or a C compiler with libjpeg (the last is the same hard
dependency `make ntr-capture` already has). Exit codes: `0` PASS, `2` binary
missing, `3` no JPEG encoder, `1` FAIL.

Verbatim output on this host:

```
TCP: heartbeat cmd 0 and remote-play cmd 901, 84 bytes each
UDP: sent one decoy datagram from 127.0.0.2
PPM: top 400x240, bottom 320x240
--- binary log ---
[2026-11-08 19:14:38] binding UDP 127.0.0.1:45236
[2026-11-08 19:14:38] UDP bound on 127.0.0.1:45236
[2026-11-08 19:14:38] connecting to 127.0.0.1:40451 (TCP)
[2026-11-08 19:14:38] TCP connected to 127.0.0.1:40451
[2026-11-08 19:14:38] sent heartbeat (cmd 0, seq 0, 84 bytes)
[2026-11-08 19:14:38] sent remote-play (cmd 901, seq 1, 84 bytes, udp port 45236)
[2026-11-08 19:14:38] first datagram from non-console address 127.0.0.2 ignored
[2026-11-08 19:14:38] screen top: frame 0 240x400 -> 400x240, wrote /tmp/tmp.DqbuwXkGDj/cap_top_0.ppm
[2026-11-08 19:14:38] screen bottom: frame 0 240x320 -> 320x240, wrote /tmp/tmp.DqbuwXkGDj/cap_bottom_0.ppm
[2026-11-08 19:14:38] heartbeats sent: 1 total
[2026-11-08 19:14:38] datagrams from non-console addresses ignored: 1
[2026-11-08 19:14:38] closing sockets (the viewer has no stop command; close only, ntr_hb.c:26-37)
--- end binary log ---
selftest: PASS
```

Sample heartbeat-rate logging from a 6 s run against a silent fake console
(`--timeout 6`), showing the 4 s cadence line and the shutdown total:

```
[2026-11-08 19:14:47] heartbeats sent: 15 total, seq 15 latest
[2026-11-08 19:14:49] heartbeats sent: 19 total
```

## ntr_input — input redirection (buttons and touch)

Headless presser for Rosalina's (Luma3DS) **input redirection**, so hardware
tests can be automated (the motivating case: `--press Y` in the Homebrew
Launcher starts netloader). It sends 20-byte UDP datagrams to the console on
port **4950**; there is no TCP connection and the console never replies.

The frame layout and idle values come from NTRViewer-HR's own input
redirection client, and the receiver is Luma3DS Rosalina — NTR-HR's README
calls its NFC patch the "alternative to Input Redirection/Debugger in
Rosalina".

### Build and run

```sh
make ntr-input
./tools/ntr_capture/build/ntr_input --console-ip 192.168.1.32 --press Y
```

`make ntr-input` is opt-in: it is not part of `make all` or `make test`. The
binary lands in the same `tools/ntr_capture/build/` directory, so the existing
`tools/ntr_capture/build/` ignore rule and `make clean` cover it. It needs only
a C11 compiler and POSIX sockets (no libjpeg). The pure frame builder is tested
by `make test` (`test_ntr_input_util`), like `test_ntr_capture_util`.

Options:

| Option | Meaning |
|---|---|
| `--console-ip <ip>` | console IPv4 (required unless `--dry-run`; no hostnames) |
| `--press A,B,X,...` | buttons to hold while the frame is held; comma-separated, OR-ed |
| `--hold-ms <n>` | hold duration, any integer in `-1000000..1000000` (outside that is a usage error), then **clamped** to `50..5000`; default `150` |
| `--repeat <n>` | iterations, `1..20`, default `1` (out of range is a usage error) |
| `--gap-ms <n>` | idle wait between iterations, `0..60000`, default `250` |
| `--touch <x>,<y>` | touch the bottom screen at pixel `x 0..319`, `y 0..239` while holding |
| `--release-frames <n>` | idle frames after each hold, `1..600`, default `6` |
| `--dry-run` | print the exact 20-byte frames; open no sockets |
| `--help`, `-h` | show the usage text; exit `0` |

There is **no port option**: 4950 is fixed (`NTR_INPUT_UDP_PORT`). There is no
`--pc-ip`: UDP sends need no local bind. Exit codes: `2` usage/validation,
`1` runtime errors, `0` success. Every console-facing action is one
timestamped stderr line stating what was sent and how many frames.

Per iteration the tool sends ~60 Hz frames carrying the pressed buttons (and
the touch, if any) for `--hold-ms`, then `--release-frames` idle frames, then
waits `--gap-ms` before the next iteration. The frame period is
`NTR_INPUT_FRAME_NS = 1e9/60` ns ≈ 16.667 ms (the viewer's 60 Hz input timer,
`main.c:883`); `--hold-ms` maps to frames via `ntr_input_hold_frames()` =
`ceil(ms * 60 / 1000)` (150 ms → 9 frames), and the `--release-frames` idle
frames follow at the same 60 Hz. The frame deadline is **re-based from
`CLOCK_MONOTONIC` at the start of every iteration**, so the 60 Hz spacing holds
across repeats and `--hold-ms` is honoured whenever `--repeat > 1` (before this
fix, every frame of iteration ≥ 2 was sent in one burst). The release burst is
also sent before exiting on `SIGINT`/`SIGTERM`, because the console's HID patch
keeps applying the last frame it received — a missing release would leave a
button held.

### Protocol

`input_redirection_frame_t` is five `uint32_t` fields, `sizeof` 20, sent with a
plain `sendto()` to `ntr_ip:4950` (`ntr_rp.h:6-12`, `ntr_rp.c:2738-2763`). The
viewer `memcpy()`s the native struct and the reference build is little-endian,
so the wire bytes are little-endian.

| Offset | Field |
|---|---|
| 0 | `hidPad` |
| 4 | `touchScreenState` |
| 8 | `circlePadState` |
| 12 | `cppState` |
| 16 | `interfaceButtons` |

Idle values, exactly as the viewer's default initializer
(`ui_input_redirection.c:467-473`): `hidPad=0x00000fff`,
`touchScreenState=0x02000000`, `circlePadState=0x007ff7ff`,
`cppState=0x80800081`, `interfaceButtons=0`.

`hidPad` is the HID module's **active-low** pad register: a cleared bit is a
pressed button. The console side proves the polarity (`ntr-hr/include/func.h:34`
inverts the register: `getKeys() ((REG(IoBasePad) & 0xFFF) ^ 0xFFF)`), and the
viewer clears bits with `&= ~(1 << n)` (`ui_input_redirection.c:651-703`). Idle
`0xfff` therefore means "nothing pressed".

| Bit | Button | `--press` name | `hidPad` when only this is pressed |
|---|---|---|---|
| 0 | A | `A` | `0xffe` |
| 1 | B | `B` | `0xffd` |
| 2 | Select | `SELECT` | `0xffb` |
| 3 | Start | `START` | `0xff7` |
| 4 | D-pad right | `RIGHT` | `0xfef` |
| 5 | D-pad left | `LEFT` | `0xfdf` |
| 6 | D-pad up | `UP` | `0xfbf` |
| 7 | D-pad down | `DOWN` | `0xf7f` |
| 8 | R | `R` | `0xeff` |
| 9 | L | `L` | `0xdff` |
| 10 | X | `X` | `0xbff` |
| 11 | Y | `Y` | `0x7ff` |

**No A/B or X/Y swap is applied.** The viewer's "swap face buttons" setting
(`ui_controller_swap_face_buttons`, `ui_input_redirection.c:647,662-671`) swaps
the *SDL gamepad* mapping, not the wire bits. `ntr_input` names the physical
console bits directly, so `--press Y` is the 3DS Y button.

Touch is `(1 << 24) | (y << 12) | x` (`ui_input_redirection.c:481`), with the
last non-touch value `0x02000000`. The encoded `x`/`y` are 12-bit raw
coordinates (0..0xfff), not pixels: the viewer scales the bottom-screen mouse
position by `TOUCH_SCREEN_COORD_RANGE` (0xfff) over the screen dimensions before
encoding (`ui_input_redirection.c:383,417-429,478-481`). The HID module converts
raw to pixels — the shared-memory touch entries consumed by applications are
pixel coordinates (3dbrew "HID Shared Memory", 0xA8+0x20), while +0x18 holds the
raw entry. `ntr_input` therefore scales its pixel argument the same way
(round-to-nearest, then clamped to 0xfff): `raw = (0xfff * pixel + 160) / 320`
for x, `(0xfff * pixel + 120) / 240` for y (so pixel `319,239` → raw
`4082,4078`). Pixel arguments are range-checked: 320, 240 or a
negative value is a usage error (exit 2), never a silent clamp.

Cadence: the viewer's input timer fires every `1000/60` ms from `main.c:883`.
The viewer only calls `sendto()` when the frame differs from the previous one
(`ui_input_redirection.c:792-795`); `ntr_input` instead repeats the frame at
60 Hz for the whole hold. Repetition is harmless (the receiver simply
overwrites its HID data, Luma3DS
`sysmodules/rosalina/source/input_redirection.c:95,142-145`) and keeps the timing
independent of that send-on-change optimisation. The loop is deadline-based on
`CLOCK_MONOTONIC`: each frame advances one absolute deadline by
`NTR_INPUT_FRAME_NS` (with the nanosecond carry), and the deadline is re-based
from the clock at the start of each iteration, so sleeps do not accumulate
drift and the 60 Hz spacing holds across `--repeat` iterations.

### Safety rules

- **`interfaceButtons` is always 0.** The builder has no parameter for it and
  no other code writes the field, so HOME, POWER and the long-POWER press are
  structurally unreachable.
- **Button whitelist only.** The 12 names in the table; anything else is a
  usage error (exit 2). No analog circle pad or C-stick/trigger input:
  `circlePadState` and `cppState` always carry their centre/idle values.
- **Ranges.** Touch pixels are range-checked; `--hold-ms` accepts
  `-1000000..1000000` and is clamped to `50..5000`; `--repeat` 1..20;
  `--gap-ms` 0..60000; `--release-frames` 1..600.
  Release frames are always sent, including on interrupt.
- **One target, one port.** Only `--console-ip` (validated dotted-quad IPv4,
  no DNS) and the fixed port 4950. No option can redirect traffic elsewhere.
- `--dry-run` opens no socket and prints the exact frame bytes.

### Netloader example

In the Homebrew Launcher (bottom screen is the HBL UI):

```sh
# 1. Y in the Homebrew Launcher starts netloader.
./tools/ntr_capture/build/ntr_input --console-ip 192.168.1.32 --press Y

# 2. Send a homebrew binary with 3dslink, then confirm/launch it.
3dslink -a 192.168.1.32 app.3dsx
./tools/ntr_capture/build/ntr_input --console-ip 192.168.1.32 --press B
./tools/ntr_capture/build/ntr_input --console-ip 192.168.1.32 --press A
```

`--dry-run --press Y` prints the exact press and release frames:

```
dry-run: no sockets opened
udp 0.0.0.0:4950, 9 frame(s) per hold, 6 release frame(s), 1 iteration(s)
pressed frame (20 bytes):
  0000  ff 07 00 00 00 00 00 02 ff f7 7f 00 81 00 80 80
  0016  00 00 00 00
release frame (20 bytes):
  0000  ff 0f 00 00 00 00 00 02 ff f7 7f 00 81 00 80 80
  0016  00 00 00 00
```

`0xfff & ~(1 << 11) = 0x7ff` is the pressed `hidPad`; the release frame is the
idle frame.

### UNVERIFIED items (ntr_input)

- No console connection was made in this task. Whether Rosalina's input
  redirection is enabled, and whether a repeated press/release sequence lands
  in the Homebrew Launcher, is unobserved.
- The viewer's send-on-change behaviour means a real viewer sends each state
  once, not at 60 Hz; `ntr_input`'s repetition is a deliberate deviation and
  its necessity is unverified.
- The pixel → 12-bit raw scaling is transcribed from the viewer at a 1:1 window
  (`ui_input_redirection.c:417-429,478-481`): round-to-nearest, then clamped to
  0xfff. That the console converts the raw fields to pixels is inferred from the
  HID shared-memory layout (3dbrew), not observed on this console.
- UDP has no acknowledgement, so exit 0 means "datagrams sent", not "buttons
  were applied".
- The Homebrew Launcher/netloader key sequence above was not executed.
- IPv4 only; the console must be reachable on the local network.

### Mutation evidence (ntr_input)

Baseline: `make test` green — all host tests plus 12 groups in
`test_ntr_input_util`, and 86 Python tests OK, exit 0.

Each mutant was applied to a copy of `ntr_input_util.h`, the unchanged test
source was rebuilt by `make test` with the same `-Wall -Wextra -Werror` +
ASan/UBSan flags, and `make test` was run. Every mutant turned `make test` red
(`rc=2`, the host-test loop stops at the first failing test). The header was
restored after each run; the final header is byte-identical to the tested one
(`diff` clean) and the baseline is green again.

| Helper | Mutant | Before | After (verbatim first failing check) |
|---|---|---|---|
| `ntr_input_button_bit` | (a) A and B bits swapped | PASS | exit 1: `test_ntr_input_util.c:38: expected cases[i].bit == 0, got 1` |
| `ntr_input_button_bit` | (b) LEFT bit 5 → 4 | PASS | exit 1: `test_ntr_input_util.c:38: expected cases[i].bit == 5, got 4` |
| `ntr_input_parse_buttons` | (c) OR → assignment (last button wins) | PASS | exit 1: `test_ntr_input_util.c:150: expected 0x003 == 3, got 2` |
| `ntr_input_parse_buttons` | (d) unknown name silently becomes A | PASS | exit 1: `test_ntr_input_util.c:158: expected -1 == -1, got 0` |
| `ntr_input_build_frame` | (e) `interfaceButtons` written as 1 | PASS | exit 1: `test_ntr_input_util.c:62: CHECK failed: memcmp(frame, expected, sizeof(expected)) == 0` |
| `ntr_input_build_frame` | (f) `touch_valid` ignored | PASS | exit 1: `test_ntr_input_util.c:62: CHECK failed: memcmp(frame, expected, sizeof(expected)) == 0` |
| `ntr_input_build_frame` | (g) press-mask range check removed | PASS | exit 1: `test_ntr_input_util.c:140: expected -1 == -1, got 0` |
| `ntr_input_touch_raw` | (h) pixel passed through unscaled | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 33484786, got 17756479` |
| `ntr_input_touch_raw` | (i) divided by `dimension - 1` | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 33484786, got 33554431` |
| `ntr_input_touch_raw` | (ag) round term `+ dimension/2` dropped | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 33484786, got 33480690` |
| `ntr_input_touch_raw` | (ah) `/2u` → `/3u` | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 25167872, got 25163775` |
| `ntr_input_build_frame` | (ai) x/y dimensions swapped | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 33484786, got 29306879` |
| `ntr_input_touch_raw` | (aj) clamp removed | PASS | exit 1: `test_ntr_input_util.c:148: expected 0xfff == 4095, got 6398` |
| `ntr_input_touch_raw` | (ak) `NTR_INPUT_TOUCH_RANGE` 0xfff → 0xffe | PASS | exit 1: `test_ntr_input_util.c:124: expected cases[i].touch == 33484786, got 33480689` |
| `ntr_input_validate_ipv4` | (j) every string accepted | PASS | exit 1: `test_ntr_input_util.c:175: expected 0 == 0, got 1` |
| `ntr_input_validate_ipv4` | (k) `inet_pton` failure treated as valid | PASS | exit 1: `test_ntr_input_util.c:175: expected 0 == 0, got 1` |
| `ntr_input_clamp` | (l) lower bound not clamped | PASS | exit 1: `test_ntr_input_util.c:186: expected 50 == 50, got 49` |
| `ntr_input_clamp` | (m) upper bound not clamped | PASS | exit 1: `test_ntr_input_util.c:189: expected 5000 == 5000, got 5001` |
| `ntr_input_hold_frames` | (n) round-up constant 999 → 1 | PASS | exit 1: `test_ntr_input_util.c:209: expected 1 == 1, got 0` |
| `ntr_input_hold_frames` | (o) divides by 1024 instead of 1000 | PASS | exit 1: `test_ntr_input_util.c:208: expected 300 == 300, got 293` |
| `ntr_input_repeat_ok` | (p) `NTR_INPUT_REPEAT_MAX` 20 → 21 | PASS | exit 1: `test_ntr_input_util.c:227: expected 0 == 0, got 1` |
| `ntr_input_repeat_ok` | (q) `NTR_INPUT_REPEAT_MAX` 20 → 19 | PASS | exit 1: `test_ntr_input_util.c:226: expected 1 == 1, got 0` |
| `ntr_input_repeat_ok` | (r) lower bound 1 → 0 | PASS | exit 1: `test_ntr_input_util.c:222: expected 0 == 0, got 1` |
| `ntr_input_repeat_ok` | (s) lower bound 1 → 2 | PASS | exit 1: `test_ntr_input_util.c:223: expected 1 == 1, got 0` |
| `ntr_input_repeat_ok` | (t) `>=` → `>` (lower bound) | PASS | exit 1: `test_ntr_input_util.c:223: expected 1 == 1, got 0` |
| `ntr_input_repeat_ok` | (u) `<=` → `<` (upper bound) | PASS | exit 1: `test_ntr_input_util.c:226: expected 1 == 1, got 0` |
| `ntr_input_repeat_ok` | (v) bounds check removed (always 1) | PASS | exit 1: `test_ntr_input_util.c:222: expected 0 == 0, got 1` |
| header constant | (w) `NTR_INPUT_HOLD_MS_DEFAULT` 150 → 151 | PASS | exit 1: `test_ntr_input_util.c:255: expected 150 == 150, got 151` |
| header constant | (x) `NTR_INPUT_REPEAT_DEFAULT` 1 → 2 | PASS | exit 1: `test_ntr_input_util.c:258: expected 1 == 1, got 2` |
| header constant | (y) `NTR_INPUT_GAP_MS_DEFAULT` 250 → 251 | PASS | exit 1: `test_ntr_input_util.c:260: expected 250 == 250, got 251` |
| header constant | (z) `NTR_INPUT_GAP_MS_MAX` 60000 → 60001 | PASS | exit 1: `test_ntr_input_util.c:261: expected 60000 == 60000, got 60001` |
| header constant | (aa) `NTR_INPUT_RELEASE_FRAMES_DEFAULT` 6 → 7 | PASS | exit 1: `test_ntr_input_util.c:262: expected 6 == 6, got 7` |
| header constant | (ab) `NTR_INPUT_RELEASE_FRAMES_MAX` 600 → 601 | PASS | exit 1: `test_ntr_input_util.c:263: expected 600 == 600, got 601` |
| `ntr_input_iter_rebase` | (ac) re-base removed (no-op) | PASS | exit 1: `test_ntr_input_util.c:303: expected now.tv_nsec == 333333335, got 83333335` |
| `NTR_INPUT_FRAME_NS` | (ad) period 16666667 → 1000000 | PASS | exit 1: `test_ntr_input_util.c:309: expected start_ns[n] + (long)k * period_ns == 16666667, got 1000000` |
| `ntr_input_deadline_advance` | (ae) nanosecond carry (`>= 1000000000`) removed | PASS | exit 1: `test_ntr_input_util.c:339: expected 6 == 6, got 5` |
| `ntr_input_iter_advance` | (af) gap addition removed | PASS | exit 1: `test_ntr_input_util.c:327: expected end_ns[0] + gap_ns == 333333335, got 83333335` |

The CLI-only checks (`--press`/`--touch`/`--repeat` usage errors, `--dry-run`
socket-free path, the fixed port) are not reachable by mutants of the header;
they were verified by running the built binary (exit 2 for an unknown button
name, a hostname IPv4, `320,0` touch and `--repeat 21`; `--dry-run` prints and
exits 0).
