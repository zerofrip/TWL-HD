# New 3DS Hardware Validation — Deployment Audit and Test Plan

**Status: AUDIT AND PLAN ONLY. Nothing has been deployed to, or executed on, the
device. Approval is required before step T2 (see section 8).**

Target (as supplied by the project owner): New 3DS at `192.168.1.32`.
Scope of the work: the ARM11 `.3dsx` renderer (`arm11/`), Phases 4B/4C.
**Everything about that renderer's behavior on a console is HARDWARE UNVERIFIED.**

Evidence labels: **LOCAL** = read from this repository or the build output here;
**UPSTREAM** = read from the devkitPro `3dslink` source fetched in this session;
**PROBED** = observed from the network exchange listed in section 2;
**UNVERIFIED** = general knowledge or reasoning, not checked here.

## 1. Constraints honoured by this audit

| Constraint | How it was kept |
|---|---|
| No scanning of ports or the network | Exactly three exchanges with one address (section 2). Port 5543 and every other port were **not** probed. No broadcast was sent |
| No firmware install/modification | Nothing was installed. None is proposed |
| No RTCom ucode injection | `TWLHD_RTCOM_UCODE_ENABLED` is 0 and this plan never enables it |
| No TWL_FIRM patch execution | The renderer is a normal 3DS homebrew app; no DS/TWL software is involved |
| No hardware-sensitive code run | Nothing was run on the device |
| Do not deploy yet | `3dslink` was **deliberately not added** to the command allowlist and never executed, even with `--help` |

## 2. Connectivity evidence (PROBED, read-only)

| # | Action | Result |
|---|---|---|
| 1 | `ip route get 192.168.1.32` (local, sends nothing) | on-link via `enp1s0`, source `192.168.1.27` (same /24) |
| 2 | `ping -c 3 192.168.1.32` (ICMP, one host) | 3 sent / 3 received, 0 % loss, rtt min/avg/max 5.7 / 15.3 / 33.9 ms |
| 3 | `nc -z -v -w 3 192.168.1.32 17491` (TCP connect only, no data) | `Connection refused` |

Interpretation: the device is up and answering. Nothing is listening on TCP 17491 right
now, which is the port the Homebrew Launcher network loader uses (section 3). That is the
expected state while the loader is not running; it is not an error.

## 3. Deployment methods

### 3.1 `3dslink` network loader — UPSTREAM

From `devkitPro/3dslink` `host/src/main.c` (fetched this session):

- `#define NETLOADER_COMM_PORT 17491` (line 39).
- **Discovery is a LAN-wide broadcast.** `find3DS()` (lines 180–236) sends the UDP datagram
  `"3dsboot"` to `INADDR_BROADCAST:17491` repeatedly and waits for `"boot3ds"`.
- It is only called when no address was given: `if (address == NULL) { dsaddr = find3DS(retries); … }`
  (line 625). `--address, -a` takes a hostname or IPv4 address (line 492).
- With an address the tool connects over TCP to `<address>:17491` (lines 357–360) and sends the file.

**Rule for this project: if `3dslink` is ever used it must always be given
`-a 192.168.1.32`.** Running it without `-a` would broadcast to the whole network, which
violates the no-scanning constraint.

On the device the loader is started by the user from the Homebrew Launcher (the `Y`
button opens "Network loader" — **UNVERIFIED**, recollection). Until then port 17491 is
closed, exactly as probe 3 found.

The tool is installed locally at `/opt/devkitpro/tools/bin/3dslink`. It is blocked by the
command allowlist on purpose; it can only be run after the approval in section 8.

### 3.2 SD-card copy — recommended

Copy `twlhd-arm11.3dsx` to `/3ds/` on the SD card with a card reader (or any file transfer
the owner already uses) and launch it from the Homebrew Launcher. No network code, no
protocol, no tool I control touches the device. **This is the lowest-risk method.**

### 3.3 Not suitable

| Method | Why not |
|---|---|
| FTP homebrew | Not probed; presence unknown; would add a network service to reason about |
| CIA / FBI install | Installs a title into the system: far more invasive than a `.3dsx`, and unnecessary |
| Artic Base / Arctic Setup Tool (port 5543) | As far as I know (**UNVERIFIED**) this streams a real console's services to the Azahar emulator; it is not a way to run homebrew, and the port was not touched |

## 4. What the renderer does on the device — LOCAL

From `arm11/source/main.c` and `renderer.c`:

- **System services used:** `gfxInitDefault`, citro3d (`C3D_Init`, render target, shader),
  `fsInit`, HID input and APT (`aptMainLoop`). A console on the bottom screen.
- **Services not used:** AM, CFG, NS, PTM, SOC/HTTPC (no networking at all), NDSP, CSND, MCU.
- **File operations:** exactly one, `fopen("sdmc:/twlhd_frame.bin", "rb")` — **read-only**.
  There is no write, delete, rename or directory change anywhere in the app.
- **RTCom:** `rtcom_transport_init()` is a stub that returns `NOT_IMPLEMENTED`; it touches no
  hardware.
- **Persistent effects:** none by design. Nothing is written to the SD card or system
  storage.

## 5. Risks (honest list)

1. **GPU/GSP behavior is unproven on hardware.** The renderer's buffer lifetime relies on
   upstream citro3d semantics (`C3D_FrameBegin` drains the previous frame's queue) that were
   read from source but never run. A mistake here can hang the app or leave the screen
   frozen; recovery is holding the power button, not a repair.
2. **Vertex layout.** A 48-byte stride with 28 bytes of attributes loaded is expected to be
   valid but is unconfirmed on hardware.
3. **Homebrew runs with the Homebrew Launcher's privileges.** The app is small and
   read-only, but any bug in homebrew code is a bug with that environment's access. It is
   built from reviewed code with host unit tests and a sanitizer run, which lowers but does
   not remove this risk.
4. **3dslink without `-a`** would broadcast on the LAN (section 3.1). Prevented by rule.
5. **The renderer reads a v1 capture**, not v2. The test input is therefore a synthetic v1
   file produced on the host (section 6), **not** output from the DS-side demo.

Not risks of this plan, because they are excluded: firmware changes, TWL_FIRM, RTCom,
ucode upload, DS software.

## 6. Artifacts (LOCAL, not deployed)

Built from `main` at merge commit `c540fe2`:

| File | Size | SHA-256 |
|---|---|---|
| `arm11/twlhd-arm11.3dsx` | 165,640 B | `3198426913e9bc38c2e011ff299f596e2b517ba739b843e3ce304506f10dcb33` |
| synthetic v1 frame `twlhd_frame.bin` (unit cube, 6 quads, 38 commands) | 73,740 B | `776f0c3b13b5dfa48c6cba218c9e787a8dba76ae9f044719a50575dafdc080c5` |

The synthetic frame is generated by a deterministic script and was accepted by **both** the
Phase 2 Python tool (`tools/twlhd_dump.py stats`: 38 commands, 24 `VTX_16`, 6 `NORMAL`,
6 `COLOR`) and the C parser built with ASan/UBSan (`result=OK`). The generator is not yet in
the repository; adding it is part of the preparation work, not of this audit.

## 7. Test plan (staged; each stage ends at a gate)

| Stage | Action | Needs approval | Pass criteria | Stop if |
|---|---|---|---|---|
| T0 | Offline: rebuild, re-run `make test`, recompute the hashes above | no | hashes match section 6 | any test fails |
| T1 | Owner puts the Homebrew Launcher on the device (and, only if `3dslink` is chosen, opens the network loader). I repeat **one** connect-only probe of 17491 | no | refused (loader off) or accepted (loader on), as expected | any other port needs probing |
| T2 | **Deploy** the `.3dsx` (method chosen in section 8) | **yes** | file present / transfer completes | transfer error: do not retry blindly |
| T3 | **Run** with no `twlhd_frame.bin` on the SD card. Expect: top screen shows the built-in red/green/blue triangle; bottom screen shows mode, "DEBUG CAMERA", and `HARDWARE UNVERIFIED`; `START` exits to the launcher | **yes** | triangle visible, status text correct, clean exit | black/frozen screen after ~10 s, garbled output |
| T4 | Copy the synthetic `twlhd_frame.bin` to the SD root, run, press `A` | **yes** | status shows parse OK, 38 commands, 24 vertices, 12 triangles; a cube is visible; `Y` returns to the triangle | parse error, hang, or missing geometry |
| T5 | Error paths, one at a time: file absent; 100-byte file; a v2 file renamed to the expected name | **yes** | clear on-screen message, previous scene kept, no crash | crash or hang |

Abort rule: on any freeze, hold the power button, record what was on screen, and stop. Do
not re-launch repeatedly. A hang is itself an important result about the GPU-sync
assumption in risk 1.

Evidence to keep: what the screen showed (owner photographs), the status lines, and the
exact commands used. A result is labelled **HARDWARE VERIFIED** only for what was observed
there, and only on this one console and firmware.

## 8. What I need before T2

1. **Method:** SD-card copy (recommended) or `3dslink -a 192.168.1.32 arm11/twlhd-arm11.3dsx`.
2. **If `3dslink`:** explicit approval to run that exact command (I would then allow the
   tool for that one invocation), and confirmation that you will start the network loader on
   the device first.
3. **Acknowledgement of risk 1**: a hang requiring a hard power-off is possible.
4. **Whether to add the synthetic-frame generator** to the repository (via the normal
   pipeline) so the test input is reproducible by anyone.

Until then nothing is sent to, or run on, the device.
