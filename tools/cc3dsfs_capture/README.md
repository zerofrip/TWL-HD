# cc3dsfs_capture — DS-mode top-screen capture via a cc3dsfs capture board

Two small helpers used for the EQ-4b hardware result
(`docs/reports/eq4b-cc3dsfs-ds-capture.md`):

- **`cc_shots.sh`** — runs `cc3dsfs` under `xvfb-run` (capture board attached
  to the console), takes one `xwd -root` screenshot per gap, converts each to
  PNG and deletes the intermediate XWD.
- **`xwd2png.py`** — converts a 32bpp ZPixmap XWD screenshot to PNG (BGRX byte
  order, header bytes-per-line respected).

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

## Test

```sh
make test-cc3dsfs          # or: make test
```

`test_xwd2png.py` builds a synthetic XWD (ZPixmap, 32bpp, BGRX, padded rows) in
a temp directory and checks the converted PNG pixel-exactly. It skips with a
labelled reason if PIL is missing.
