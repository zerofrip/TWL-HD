#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Opt-in smoke test for the headless Azahar libretro host.
#
# NOT part of `make test` and never run by `make all`. Needs an external Azahar
# libretro core (AZAHAR_LIBRETRO) and the staging inputs under /home/one/3ds.
#
# Usage: tools/azahar_host/smoke.sh [outdir]
# Exit codes: 2 when AZAHAR_LIBRETRO is unset, 1 on any other failure.

set -euo pipefail

if [ -z "${AZAHAR_LIBRETRO:-}" ]; then
    echo "AZAHAR_LIBRETRO not set" >&2
    exit 2
fi
if [ ! -x "$AZAHAR_LIBRETRO" ]; then
    echo "error: AZAHAR_LIBRETRO=$AZAHAR_LIBRETRO is not executable" >&2
    exit 1
fi

for tool in python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 1
    fi
done

host=${AZAHAR_HOST:-tools/azahar_host/build/azahar_host}
if [ ! -x "$host" ]; then
    echo "error: host binary not found: $host (run 'make azahar-host')" >&2
    exit 1
fi

game=/home/one/3ds/hwtest-staging/twlhd-arm11-blend.3dsx
frame=/home/one/3ds/hwtest-staging/frames/twlhd_frame_v2_scene4_alpha.bin
if [ ! -f "$game" ]; then
    echo "error: game not found: $game" >&2
    exit 1
fi
if [ ! -f "$frame" ]; then
    echo "error: frame not found: $frame" >&2
    exit 1
fi

outdir=${1:-tools/azahar_host/build/smoke}
mkdir -p "$outdir"
sdroot=$outdir/sd
rm -rf "$sdroot"
mkdir -p "$sdroot"
cp "$frame" "$sdroot/twlhd_frame.bin"

prefix=$outdir/shot
log=$outdir/run.log

if ! "$host" --core "$AZAHAR_LIBRETRO" --game "$game" --frames 200 \
        --press A:60-63 --shot 199 --out "$prefix" --sd-root "$sdroot" \
        > /dev/null 2> "$log"; then
    echo "error: azahar_host failed; last log lines:" >&2
    tail -n 20 "$log" >&2 || true
    exit 1
fi

ppm=${prefix}_199.ppm
if [ ! -f "$ppm" ]; then
    echo "error: no PPM at $ppm" >&2
    exit 1
fi

python3 - "$ppm" <<'PY'
import sys

path = sys.argv[1]
data = open(path, "rb").read()
pos = 0


def token():
    global pos
    while pos < len(data) and data[pos:pos + 1].isspace():
        pos += 1
    start = pos
    while pos < len(data) and not data[pos:pos + 1].isspace():
        pos += 1
    return data[start:pos]


magic = token()
width = int(token())
height = int(token())
maxval = int(token())
pos += 1
if magic != b"P6" or maxval != 255:
    sys.exit("error: not a P6/255 PPM: magic=%r maxval=%d" % (magic, maxval))
pixels = data[pos:]
expected = width * height * 3
if len(pixels) < expected:
    sys.exit("error: %s: %d pixel bytes, header claims %d"
             % (path, len(pixels), expected))
colours = {pixels[i:i + 3] for i in range(0, expected, 3)}
print("PPM %s: %dx%d, %d pixel bytes, %d distinct colours"
      % (path, width, height, len(pixels), len(colours)))
if len(colours) < 2:
    sys.exit("error: %s is a single colour" % path)
PY

echo "smoke: OK ($ppm)"
