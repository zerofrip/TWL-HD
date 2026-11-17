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

## EQ-2b geometry comparison

`eq2_compare.py` compares our GX interpreter's clip-space output with a geometry
dump from the out-of-tree GPL DeSmuME harness (`format geometry-dump 1`), per
polygon and per vertex, under the tolerances of
`docs/EQUALITY_CHECK_DESIGN.md` section 5.2. It reads only the documented text
format; no harness or DeSmuME source lives here. See `docs/EQ2_COMPARE.md` for
both interchange formats, the unit conversions and the frame alignment.

```sh
tests/host/build/gx_interp_cli --eq2 frame_v2.bin > ours.txt
python3 tools/eq2_compare.py harness_dump.txt ours.txt
```

Exit codes: 0 within tolerance, 1 mismatch (worst deviation per field with
polygon/vertex indices), 2 usage or format error.

## EQ-3b picture comparison

`eq3_compare.py` compares the default-cube picture our pipeline draws (Azahar
libretro host, 400x240 top screen) with the one DeSmuME's software rasteriser
draws for the same frame (out-of-tree GPL harness, 256x192 P6 PPM). It maps
our picture back onto the DS grid by the 0.8 clipfix scale and compares
structure only: per face-colour bounding boxes, centroids and interior colours
after a 3 px erosion, under the tolerances of
`docs/EQUALITY_CHECK_DESIGN.md` section 5.1. See `docs/EQ3_COMPARE.md` for the
mapping arithmetic, tolerance derivations and real numbers. All results are
EMULATOR; no harness or DeSmuME source lives here.

```sh
python3 tools/eq3_compare.py harness_cube_f240.ppm ours_cube_f240_top.ppm
```

Exit codes: 0 within tolerance, 1 structural mismatch (worst deviation, face
and field named), 2 usage or format error (bad PPM, wrong size, an input with
no expected face class).

## EQ-2/EQ-3 scene coverage

`eq_scenes.py` runs the EQ-2b geometry comparison and, where the colour model is
valid, the EQ-3b picture comparison for every arm9v2 demo scene: the default
cube, `SCENE=1..5` and `CUBES=2`. It imports `eq2_compare`/`eq3_compare` and
restates no tolerance. Full mode needs the harness binary, `desmume-cli`, the
ROM directory and `gx_interp_cli`; `--manifest` replays committed text dumps
without an emulator. See `docs/EQ_SCENES.md` for the per-scene table and what is
not proven. All results are EMULATOR.

```sh
TWLHD_HARNESS_BIN=/path/harness DESMUME_CLI=/path/desmume-cli \
  python3 tools/eq_scenes.py --rom-dir arm9v2
python3 tools/eq_scenes.py --manifest tools/fixtures/eq_scenes/manifest.json
```

Exit codes: 0 every row PASS or NOT-COMPARABLE, 1 at least one FAIL row, 2 a
missing prerequisite or an unusable manifest.

## Errors

Malformed input exits non-zero with a one-line message instead of a traceback:

- missing or unreadable file
- truncated file (reports expected vs actual size)
- unexpected format version (regenerate the dump with a matching build)

## Tests

```sh
python3 -B -I -m unittest discover -s tools -v
```
