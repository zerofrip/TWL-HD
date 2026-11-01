#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Headless DeSmuME capture for the arm9v2 autodump build.
#
# Usage: desmume_capture.sh <rom.nds> <outdir>
#
# In <outdir> this script creates:
#   cflash.img            32 MiB FAT image handed to DeSmuME as Slot-2 CFlash
#   desmume.log           line-buffered emulator log
#   fat/                  7z extraction of the FAT image
#   twlhd_frame_v2.bin    the file the demo wrote to the FAT root
#
# Requirements: an external desmume-cli (set DESMUME_CLI, not vendored here),
# mkfs.fat, 7z and stdbuf. The emulator is stopped with SIGINT (not KILL)
# so it flushes the FAT image on shutdown.  This script implements its own
# watchdog so it does not depend on any particular coreutils watchdog utility.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <rom.nds> <outdir>" >&2
    exit 2
fi

rom=$1
outdir=$2

if [ ! -f "$rom" ]; then
    echo "error: ROM not found: $rom" >&2
    exit 1
fi

if [ -z "${DESMUME_CLI:-}" ]; then
    echo "error: DESMUME_CLI is not set; point it at a desmume-cli binary" >&2
    echo "       (see docs/EMULATOR_VALIDATION.md)" >&2
    exit 1
fi

if [ ! -x "$DESMUME_CLI" ]; then
    echo "error: DESMUME_CLI=$DESMUME_CLI is not executable" >&2
    exit 1
fi

for tool in mkfs.fat 7z stdbuf; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 1
    fi
done

DESMUME_TIMEOUT=${DESMUME_TIMEOUT:-120}
if ! [[ "$DESMUME_TIMEOUT" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: DESMUME_TIMEOUT must be a positive integer (got: $DESMUME_TIMEOUT)" >&2
    exit 2
fi

DESMUME_INT_GRACE=${DESMUME_INT_GRACE:-30}
if ! [[ "$DESMUME_INT_GRACE" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: DESMUME_INT_GRACE must be a positive integer (got: $DESMUME_INT_GRACE)" >&2
    exit 2
fi

set -x

mkdir -p "$outdir"
image=$outdir/cflash.img
log=$outdir/desmume.log
fatdir=$outdir/fat

rm -f "$image" "$log" "$outdir/twlhd_frame_v2.bin"
rm -rf "$fatdir"

mkfs.fat -C "$image" 32768

# DISPLAY must never leak into the emulator: any frontend that finds one may
# try to open a window. env -u removes it for this command only.
# Use exec inside the subshell so $! is the emulator's pid, not a shell.
# Reset inherited signal masks so the emulator can handle SIGINT itself.
(
    trap - INT QUIT
    exec env -u DISPLAY -u WAYLAND_DISPLAY \
        SDL_VIDEODRIVER=offscreen \
        SDL_AUDIODRIVER=dummy \
        stdbuf -oL -eL "$DESMUME_CLI" --cflash-image "$image" --disable-sound \
        --disable-limiter "$rom"
) > "$log" 2>&1 &
emulator_pid=$!

# Watchdog: wake us after DESMUME_TIMEOUT seconds.
sleep "$DESMUME_TIMEOUT" & sleeper_pid=$!

# Optional second sleeper used while waiting for SIGINT graceful shutdown.
grace_pid=

emulator_killed_by_script=0

cleanup() {
    local pid
    for pid in "${sleeper_pid:-}" "${grace_pid:-}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [ -n "${emulator_pid:-}" ] && kill -0 "$emulator_pid" 2>/dev/null; then
        kill -INT "$emulator_pid" 2>/dev/null || true
        local i=0
        while [ "$i" -lt 2 ] && kill -0 "$emulator_pid" 2>/dev/null; do
            sleep 1
            i=$((i + 1))
        done
        if kill -0 "$emulator_pid" 2>/dev/null; then
            kill -KILL "$emulator_pid" 2>/dev/null || true
        fi
        wait "$emulator_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

# Wait for either the emulator to exit early or the watchdog to fire.
set +e
wait -n "$emulator_pid" "$sleeper_pid"
first_status=$?
set -e

if kill -0 "$emulator_pid" 2>/dev/null; then
    # Watchdog fired; send SIGINT for graceful shutdown.
    if kill -0 "$sleeper_pid" 2>/dev/null; then
        kill -TERM "$sleeper_pid" 2>/dev/null || true
        wait "$sleeper_pid" 2>/dev/null || true
    fi
    sleeper_pid=
    emulator_killed_by_script=1
    kill -INT "$emulator_pid" 2>/dev/null || true

    sleep "$DESMUME_INT_GRACE" & grace_pid=$!
    set +e
    wait -n "$emulator_pid" "$grace_pid"
    grace_status=$?
    set -e

    if kill -0 "$emulator_pid" 2>/dev/null; then
        # Did not exit on SIGINT within the grace period.
        if kill -0 "$grace_pid" 2>/dev/null; then
            kill -TERM "$grace_pid" 2>/dev/null || true
            wait "$grace_pid" 2>/dev/null || true
        fi
        grace_pid=
        kill -KILL "$emulator_pid" 2>/dev/null || true
        wait "$emulator_pid" 2>/dev/null || true
        echo "error: DeSmuME did not exit on SIGINT within ${DESMUME_INT_GRACE}s; the FAT image may be unflushed/corrupt" >&2
        exit 1
    else
        # Exited during the grace period.  Do not trust $grace_status: it may
        # belong to the grace sleeper if the emulator was reaped just after it.
        # Kill the sleeper and wait on the emulator to obtain its real status.
        if kill -0 "$grace_pid" 2>/dev/null; then
            kill -TERM "$grace_pid" 2>/dev/null || true
            wait "$grace_pid" 2>/dev/null || true
        fi
        grace_pid=
        set +e
        wait "$emulator_pid"
        status=$?
        set -e
    fi
else
    # Emulator exited before the watchdog fired.
    if kill -0 "$sleeper_pid" 2>/dev/null; then
        kill -TERM "$sleeper_pid" 2>/dev/null || true
        wait "$sleeper_pid" 2>/dev/null || true
    fi
    sleeper_pid=
    status=$first_status
fi

trap - EXIT INT TERM

if [ "$emulator_killed_by_script" -eq 1 ]; then
    if [ "$status" -ne 0 ] && [ "$status" -ne 130 ]; then
        echo "error: DeSmuME exited with status $status (see $log)" >&2
        exit 1
    fi
else
    echo "error: DeSmuME exited with status $status (see $log)" >&2
    exit 1
fi

if ! grep -qF "Slot 2: MPCF Flash Card Device" "$log" ||
   ! grep -qF "AUTO-PATCHING DLDI" "$log"; then
    echo "error: DeSmuME did not report both the Slot-2 MPCF device and" >&2
    echo "       DLDI autopatch (looked for 'Slot 2: MPCF Flash Card Device'" >&2
    echo "       and 'AUTO-PATCHING DLDI' in $log)" >&2
    exit 1
fi

7z x -y "$image" -o"$fatdir" >/dev/null

if [ ! -f "$fatdir/twlhd_frame_v2.bin" ]; then
    echo "error: twlhd_frame_v2.bin was not written to the FAT image" >&2
    echo "       (did the demo reach the autodump frame?)" >&2
    exit 1
fi

cp "$fatdir/twlhd_frame_v2.bin" "$outdir/twlhd_frame_v2.bin"
echo "capture: $outdir/twlhd_frame_v2.bin"
