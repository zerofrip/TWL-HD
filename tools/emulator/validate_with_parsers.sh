#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Cross-check a v2 capture with both independent parsers.
#
# Usage: validate_with_parsers.sh <captured.bin>
#
# Builds tests/host/v2_dump_cli.c (the C parser) with ASan+UBSan, then runs it
# and tools/twlhd_v2_dump.py (the Python parser) on the capture. Both must
# accept the file and report the same GXCM/RSTA counts.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <captured.bin>" >&2
    exit 2
fi

capture=$1
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cc=${CC:-gcc}

if [ ! -f "$capture" ]; then
    echo "error: capture not found: $capture" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

set -x

"$cc" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$root/common" \
    "$root/tests/host/v2_dump_cli.c" "$root/common/twlhd_v2_parse.c" \
    -o "$tmp/v2_dump_cli"

"$tmp/v2_dump_cli" "$capture" >"$tmp/c.txt"
python3 "$root/tools/twlhd_v2_dump.py" validate "$capture" >"$tmp/py_validate.txt"
python3 "$root/tools/twlhd_v2_dump.py" stats "$capture" >"$tmp/py_stats.txt"

set +x

if ! grep -q '^result=OK$' "$tmp/c.txt"; then
    echo "error: the C parser rejected the capture:" >&2
    cat "$tmp/c.txt" >&2
    exit 1
fi

if ! grep -q '^result=OK$' "$tmp/py_validate.txt"; then
    echo "error: the Python parser rejected the capture:" >&2
    cat "$tmp/py_validate.txt" >&2
    exit 1
fi

c_entries=$(sed -n 's/^entries=//p' "$tmp/c.txt")
py_entries=$(sed -n 's/^entries=//p' "$tmp/py_validate.txt")
c_rsta=$(sed -n 's/^rsta=//p' "$tmp/c.txt")
py_rsta=$(sed -n 's/^rsta=//p' "$tmp/py_validate.txt")

if [ -z "$c_entries" ] || [ -z "$c_rsta" ]; then
    echo "error: the C parser did not report entry counts" >&2
    cat "$tmp/c.txt" >&2
    exit 1
fi

if [ "$c_entries" != "$py_entries" ] || [ "$c_rsta" != "$py_rsta" ]; then
    echo "error: parser disagreement: C entries=$c_entries rsta=$c_rsta," \
         "Python entries=$py_entries rsta=$py_rsta" >&2
    exit 1
fi

echo "parsers: OK (entries=$c_entries rsta=$c_rsta)"
