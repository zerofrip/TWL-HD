#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# usage: CC3DSFS_BIN=<dir with cc3dsfs> OUT_DIR=<dir> sh cc_shots.sh <label> <n> <gap_s>
# Xvfb + cc3dsfs (capture board), one xwd->png per gap.
L=${1:-t}; N=${2:-24}; G=${3:-5}
O=${OUT_DIR:-$PWD}
B=${CC3DSFS_BIN:-.}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$B" || exit 1
export OUT_DIR="$O" L N G CONVERTER="$HERE/xwd2png.py"
xvfb-run -a -s "-screen 0 1280x720x24" sh -c '
 ./cc3dsfs --auto_connect --no_audio --no_auto_save > "$OUT_DIR/${L}.log" 2>&1 &
 P=$!
 sleep 6
 i=1
 while [ "$i" -le "$N" ]; do
   xwd -root -silent -out "$OUT_DIR/${L}_${i}.xwd" && python3 "$CONVERTER" "$OUT_DIR/${L}_${i}.xwd" "$OUT_DIR/${L}_${i}.png" >/dev/null && rm -f "$OUT_DIR/${L}_${i}.xwd"
   i=$((i+1)); sleep "$G"
 done
 kill $P 2>/dev/null
'
ls "$O" | grep "^$L" | head -40
