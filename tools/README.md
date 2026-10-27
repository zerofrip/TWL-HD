# Host analysis tooling

`twlhd_dump.py` parses the raw capture dumps written by the ARM9 demo. It needs
Python 3 and nothing outside the standard library.

## Getting a dump

1. Copy `arm9/twlhd-arm9.nds` to the SD card and boot it in a DS/DSi flashcard
   or an emulator with SD access (melonDS/DeSmuME map the emulated SD card).
2. Run the demo and press **A**. The frame captured during the current loop
   iteration is written to `/twlhd_frame.bin` (73740 bytes) and the bottom
   screen confirms the write.
3. Copy `/twlhd_frame.bin` from the SD card to the host.

The dump is the raw `TwlhdFrame` from `common/twlhd_frame_format.h`, so its
size is fixed at 73740 bytes (12-byte header + 1024 command slots of 72 bytes).

## Usage

```sh
python3 tools/twlhd_dump.py stats /twlhd_frame.bin
python3 tools/twlhd_dump.py dump  /twlhd_frame.bin
python3 tools/twlhd_dump.py diff  /twlhd_frame_a.bin /twlhd_frame_b.bin
```

`stats` prints the frame index and per-command-type counts:

```
file:       /twlhd_frame.bin (73740 bytes)
version:    1
frame:      59
commands:   40 / 1024
MTX_MODE      1
MTX_IDENTITY  1
COLOR         6
NORMAL        6
VTX_16        24
BEGIN_VTXS    1
END_VTXS      1
```

`dump` prints every valid command with its raw parameter words, in issue order.
`diff` compares two dumps: command count, the first command that differs, and
per-type count deltas. Identical prefixes with different lengths are reported
separately.

Only the first `cmd_count` slots are read; the remaining ring-buffer slots are
zero-fill.

## Errors

Malformed input exits non-zero with a one-line message instead of a traceback:

- missing or unreadable file
- truncated file (reports expected vs actual size)
- unexpected format version (regenerate the dump with a matching build)

## Tests

```sh
python3 -B -I -m unittest discover -s tools -v
```
