# cc3dsfs_capture — DS-mode top-screen capture via a cc3dsfs capture board

Two small helpers used for the EQ-4b hardware result
(`docs/reports/eq4b-cc3dsfs-ds-capture.md`):

- **`cc_shots.sh`** — runs `cc3dsfs` under `xvfb-run` (capture board attached
  to the console), takes one `xwd -root` screenshot per gap, converts each to
  PNG and deletes the intermediate XWD.
- **`xwd2png.py`** — converts a ZPixmap XWD screenshot to PNG, honouring the
  header's `bits_per_pixel`: 24 bpp is 3-byte BGR, 32 bpp is 4-byte BGRX; rows
  use the header's `bytes_per_line` stride. Any other bpp exits non-zero.

Status: **used once on the host** on 2026-11-22 for EQ-4b. Passive capture only:
nothing is sent to the console. The 400×240 capture layout, the pixel-exactness
of the board output, frame sync and the capture window size are **UNVERIFIED**
(see the report).

## Usage

```sh
CC3DSFS_BIN=/path/to/cc3dsfs-build/bin OUT_DIR=/path/to/out \
    sh tools/cc3dsfs_capture/cc_shots.sh ds1 24 5
```

Arguments: `<label> <frames> <gap_s>` (defaults `t 24 5`). Output per frame is
`$OUT_DIR/<label>_<i>.png` (1280×720 Xvfb root screenshot) plus
`$OUT_DIR/<label>.log`; the script ends with a short listing of the label's
files. `xwd2png.py` is located relative to the script, so no install step is
needed.

Requirements: `xvfb-run`, `xwd`, `python3` with PIL, and a `cc3dsfs` binary
built elsewhere (the repository does not contain it).

## XWD header fields used

The converter reads the 25 big-endian `uint32` header fields (`struct '>25I'`):

| index | field | use |
|-------|-------|-----|
| 0 | header size (`hsize`) | pixel data starts at `hsize + ncolors*12` |
| 3 | pixmap depth | informational (24 in the real capture) |
| 4, 5 | `width`, `height` | image size |
| 11 | `bits_per_pixel` | 24 -> 3-byte BGR, 32 -> 4-byte BGRX, else exit 2 |
| 12 | `bytes_per_line` | row stride passed to Pillow's `frombuffer` |
| 19 | `ncolors` | extra colour-map bytes skipped (`ncolors*12`) |

A real `xwd -root` capture from the Xvfb screen has the first 14 fields
`(107, 7, 2, 24, 1280, 720, 0, 0, 32, 0, 8, 24, 5120, 5)`: header size 107,
depth 24, 1280×720, **bits_per_pixel 24 (index 11), bytes_per_line 5120
(index 12), ncolors 5**. The data are 3-byte BGR pixels; decoding them as
4-byte BGRX squeezes the picture horizontally by 3/4. The 32-bpp path is kept
for other `xwd` outputs.

## Test

```sh
make test-cc3dsfs          # or: make test
```

`test_xwd2png.py` builds synthetic XWDs (ZPixmap, both 32 bpp BGRX and 24 bpp
BGR with padded rows) in a temp directory, checks each converted PNG
pixel-exactly, pins that the old 4-byte decoding of the 24-bpp data is wrong,
and checks that an unsupported bpp exits non-zero. It skips with a labelled
reason if PIL is missing.
