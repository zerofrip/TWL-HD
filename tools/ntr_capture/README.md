# ntr_capture — headless NTR-HR remote-play capture

Opt-in, headless client for NTR-HR's **JPEG Compat (UDP)** remote play. It
connects to the console's TCP port 8000, sends the same start command
NTRViewer-HR sends, reassembles the UDP stream on port 8001, JPEG-decodes each
screen, rotates the portrait framebuffer to landscape, and writes one P6 PPM
per screen.

Status: **BUILD/UNIT only**. This tool was built and tested on the host with
synthetic packets; it was **not connected to a console** during implementation.
Nothing from NTRViewer-HR or libjpeg is vendored in this repository.

## Build and run

```sh
make ntr-capture
./tools/ntr_capture/build/ntr_capture --host 192.168.1.32 --out /tmp/shot
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
| `--host <ip>` | console IPv4 (required unless `--dry-run`) |
| `--port <n>` | console TCP port, default `8000` |
| `--udp-port <n>` | local UDP port, default `8001` |
| `--screen top\|bottom\|both` | screen selection through the priority field (see below) |
| `--quality <n>` | JPEG quality `10..100`, default `95` |
| `--priority-factor <n>` | priority factor for `both`, `0..255`, default `1` |
| `--bandwidth <n>` | bandwidth cap in Mbps `4..20`, default `16` |
| `--frames <n>` | frames per selected screen to write, default `1` |
| `--timeout <sec>` | overall timeout, default `15` |
| `--out <prefix>` | PPM prefix, default `ntr_capture` |
| `--dry-run` | print the exact TCP bytes and ports; open no sockets |

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
| 1 | bit 0: bottom screen when set; bit 4: last packet of the frame |
| 2 | must satisfy `(b & ~0x0d) == 2`; bit 0 lossless; bits 2-3 downsample (`ntr_rp.c:1827-1838`) |
| 3 | packet number |

The frame identity is bytes 0-2 **after clearing the end bit**
(`ntr_rp.c:1836,1843`). A new identity starts a new frame. A non-end packet must
be exactly 1444 bytes. Packet numbers wrap by 256 (`ntr_rp.c:1900-1902`). On the
end packet the viewer checks that every packet `0..end_packet` arrived; a
missing packet discards the whole frame (`ntr_rp.c:1930-1948`). Duplicate packet
numbers overwrite the same slot and are harmless. This client mirrors those
rules in `ntr_cap_feed()`; duplicates are counted and ignored.

Displayed sizes are 400×240 (top) and 320×240 (bottom); the JPEG itself is the
portrait 240×400 / 240×320 framebuffer (`const.h:189-192`,
`ntr_rp.c:813-853`). The SDL renderer rotates the upright texture by −90°
(`ui_renderer_sdl.c:450`); this client applies the equivalent 90°
counter-clockwise rotation (`ntr_cap_rotate_ccw`), so the PPM is landscape.

## Dry-run hex versus the viewer's bytes

`--dry-run` opens no sockets. For `--host 192.168.1.32 --screen both --quality
95 --bandwidth 16 --udp-port 8001` it prints:

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
- Output orientation is mirrored from the viewer's SDL renderer. The OpenGL
  renderer swaps texture coordinates equivalently by inspection, but no PPM
  was visually compared with a running viewer.
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
  with a log line. KCP / reliable stream, delta, lossless, audio and full-width
  modes are not implemented.
- **No input redirection** and no console writes of any kind.
- PPM is uncompressed: about 288 KB for the top screen and 230 KB for the
  bottom.

## Console access and policy

Remote play must be enabled on the console: NTR-HR menu (X+Y) → Remote Play,
using a mode reported as **JPEG Compat (UDP)**. The PC must be able to receive
UDP on port 8001 and connect to TCP 8000.

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

Baseline: `make test` green (all host tests, including 11 checks in
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
| `ntr_cap_udp_parse` | (e) top/bottom flag inverted | PASS | exit 1: `test_ntr_capture_util.c:106: expected 1 == 1, got 0` |
| `ntr_cap_udp_parse` | (f) end-flag mask `0x10` → `0x20` | PASS | exit 1: `test_ntr_capture_util.c:120: expected 1 == 1, got 0` |
| `ntr_cap_feed` | (g) missing-packet test inverted | PASS | exit 1: `test_ntr_capture_util.c:156: expected NTR_CAP_FEED_READY == 2, got 1` |
| `ntr_cap_feed` | (h) non-end size check removed | PASS | exit 1: `test_ntr_capture_util.c:228: expected NTR_CAP_FEED_IGNORED == 0, got 1` |
| `ntr_cap_rotate_ccw` | (i) source row not mirrored | PASS | exit 1: `test_ntr_capture_util.c:270: CHECK failed: memcmp(dst, expected, sizeof(expected)) == 0` |
| `ntr_cap_rotate_ccw` | (j) last destination row skipped | PASS | exit 1: `test_ntr_capture_util.c:270: CHECK failed: memcmp(dst, expected, sizeof(expected)) == 0` |
| `ntr_cap_ppm_header` | (k) magic `P3` instead of `P6` | PASS | exit 1: `test_ntr_capture_util.c:285: CHECK failed: strcmp(buf, "P6\n400 240\n255\n") == 0` |
| `ntr_cap_ppm_header` | (l) zero width accepted | PASS | exit 1: `test_ntr_capture_util.c:293: expected -1 == -1, got 13` |

A synthetic localhost run (fake TCP server on 18000, fake UDP sender on 18001)
also exercised the whole binary without a console: it sent the two 84-byte
commands, reassembled 240×400 and 240×320 JPEG frames, and produced
`cap_top_0.ppm` (400×240) and `cap_bottom_0.ppm` (320×240) with sampled pixels
matching the 90° counter-clockwise mapping within JPEG tolerance.
