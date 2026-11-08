#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Opt-in localhost self-test for tools/ntr_capture.
#
# NOT part of `make test`: it opens real sockets, all on 127.0.0.1, and needs a
# JPEG encoder. It never contacts a console: the "console" is a fake TCP server
# and a UDP sender in this script.
#
# JPEG files are produced with the first available of: PIL, ImageMagick,
# cjpeg, or a small libjpeg program (the same hard dependency `make
# ntr-capture` already needs).
#
# Usage: tools/ntr_capture/selftest.sh
# Exit codes: 0 PASS, 2 binary missing, 3 no JPEG encoder, 1 FAIL.

set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
bin=$root/tools/ntr_capture/build/ntr_capture
if [ ! -x "$bin" ]; then
    echo "error: binary not found: $bin (run 'make ntr-capture')" >&2
    exit 2
fi
for tool in python3 "${CC:-cc}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        exit 1
    fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

gen_jpeg() { # <out> <width> <height>
    local out=$1 w=$2 h=$3 im=convert

    if python3 -c 'import PIL.Image' >/dev/null 2>&1; then
        python3 - "$out" "$w" "$h" <<'PY'
import sys
from PIL import Image

w, h = int(sys.argv[2]), int(sys.argv[3])
img = Image.new("RGB", (w, h))
img.putdata([(x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128)
             for y in range(h) for x in range(w)])
img.save(sys.argv[1], "JPEG", quality=95)
PY
        return 0
    fi
    if command -v magick >/dev/null 2>&1; then
        im=magick
    fi
    if command -v "$im" >/dev/null 2>&1; then
        "$im" -size "${w}x${h}" gradient: -quality 95 "$out"
        return 0
    fi
    if command -v cjpeg >/dev/null 2>&1; then
        python3 - "$tmp/in.ppm" "$w" "$h" <<'PY'
import sys

path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(path, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (w, h))
    f.write(bytes((x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 128)
                  for y in range(h) for x in range(w)))
PY
        cjpeg -quality 95 -outfile "$out" "$tmp/in.ppm"
        return 0
    fi
    cat > "$tmp/genjpeg.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>

int main(int argc, char **argv) {
    struct jpeg_compress_struct c;
    struct jpeg_error_mgr e;
    unsigned char *row;
    FILE *f;
    int w, h, x, y;

    if (argc != 4) {
        return 2;
    }
    w = atoi(argv[2]);
    h = atoi(argv[3]);
    f = fopen(argv[1], "wb");
    if (f == NULL || w <= 0 || h <= 0) {
        return 1;
    }
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    jpeg_stdio_dest(&c, f);
    c.image_width = (JDIMENSION)w;
    c.image_height = (JDIMENSION)h;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, 95, TRUE);
    jpeg_start_compress(&c, TRUE);
    row = malloc((size_t)w * 3u);
    if (row == NULL) {
        return 1;
    }
    while (c.next_scanline < c.image_height) {
        y = (int)c.next_scanline;
        for (x = 0; x < w; x++) {
            row[x * 3 + 0] = (unsigned char)(x * 255 / (w > 1 ? w - 1 : 1));
            row[x * 3 + 1] = (unsigned char)(y * 255 / (h > 1 ? h - 1 : 1));
            row[x * 3 + 2] = 128;
        }
        {
            JSAMPROW r = row;
            jpeg_write_scanlines(&c, &r, 1);
        }
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    free(row);
    fclose(f);
    return 0;
}
C
    if ! "${CC:-cc}" -O2 -o "$tmp/genjpeg" "$tmp/genjpeg.c" -ljpeg; then
        echo "selftest: no JPEG encoder (PIL, ImageMagick, cjpeg, libjpeg)" >&2
        exit 3
    fi
    "$tmp/genjpeg" "$out" "$w" "$h"
}

gen_jpeg "$tmp/top.jpg" 240 400
gen_jpeg "$tmp/bottom.jpg" 240 320

if python3 - "$bin" "$tmp/top.jpg" "$tmp/bottom.jpg" "$tmp" <<'PY'
import os
import socket
import struct
import subprocess
import sys
import time

binary, top_path, bottom_path, work = sys.argv[1:5]
top = open(top_path, "rb").read()
bottom = open(bottom_path, "rb").read()


def packets(jpeg, frame_id, is_top):
    out = []
    for off in range(0, len(jpeg), 1444):
        last = off + 1444 >= len(jpeg)
        flags = (0x00 if is_top else 0x01) | (0x10 if last else 0x00)
        out.append(bytes([frame_id, flags, 0x02, (off // 1444) & 0xFF]) +
                   jpeg[off:off + 1444])
    return out


def ppm_size(path):
    data = open(path, "rb").read()
    magic, size, maxval, pixels = data.split(b"\n", 3)
    w, h = (int(v) for v in size.split())
    assert magic == b"P6" and maxval == b"255", "%s: not a P6/255 PPM" % path
    assert len(pixels) == w * h * 3, "%s: %d pixel bytes, want %d" % (
        path, len(pixels), w * h * 3)
    return w, h


probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.bind(("127.0.0.1", 0))
udp_port = probe.getsockname()[1]
probe.close()

listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
listener.bind(("127.0.0.1", 0))
listener.listen(1)
tcp_port = listener.getsockname()[1]

log_path = os.path.join(work, "run.log")
log = open(log_path, "w")
prefix = os.path.join(work, "cap")
proc = subprocess.Popen(
    [binary, "--console-ip", "127.0.0.1", "--pc-ip", "127.0.0.1",
     "--console-port", str(tcp_port), "--udp-port", str(udp_port),
     "--frames", "1", "--timeout", "10", "--out", prefix],
    stdout=log, stderr=subprocess.STDOUT)
try:
    listener.settimeout(10)
    conn, _ = listener.accept()
    conn.settimeout(10)

    def read_exact(n):
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                raise EOFError("TCP closed after %d bytes" % len(buf))
            buf += chunk
        return buf

    heartbeat = read_exact(84)
    remote_play = read_exact(84)
    assert struct.unpack_from("<I", heartbeat, 0)[0] == 0x12345678
    assert struct.unpack_from("<I", heartbeat, 12)[0] == 0
    assert struct.unpack_from("<I", remote_play, 12)[0] == 901
    assert struct.unpack_from("<I", remote_play, 32)[0] == udp_port
    print("TCP: heartbeat cmd 0 and remote-play cmd 901, 84 bytes each")

    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sender.bind(("127.0.0.1", 0))
    decoyed = False
    decoy = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        decoy.bind(("127.0.0.2", 0))
    except OSError as exc:
        print("UDP: 127.0.0.2 unavailable (%s); filter log not exercised" % exc)
    else:
        decoy.sendto(packets(top, 0x99, True)[0], ("127.0.0.1", udp_port))
        decoyed = True
        print("UDP: sent one decoy datagram from 127.0.0.2")
    finally:
        decoy.close()

    for frame in (packets(top, 0x21, True), packets(bottom, 0x22, False)):
        for pkt in frame:
            sender.sendto(pkt, ("127.0.0.1", udp_port))
            time.sleep(0.001)
    sender.close()
    conn.close()
    listener.close()

    rc = proc.wait(timeout=10)
    assert rc == 0, "binary exit code %d" % rc
finally:
    if proc.poll() is None:
        proc.kill()
        proc.wait()
    log.close()

run_log = open(log_path, "r", errors="replace").read()
assert "UDP bound on 127.0.0.1:%d" % udp_port in run_log
assert "wrote" in run_log, "no frame was written"
if decoyed:
    assert "non-console address 127.0.0.2" in run_log, \
        "source filter did not log the decoy"
    assert "ignored: 1" in run_log

top_size = ppm_size(prefix + "_top_0.ppm")
bottom_size = ppm_size(prefix + "_bottom_0.ppm")
assert top_size == (400, 240), "top PPM is %dx%d" % top_size
assert bottom_size == (320, 240), "bottom PPM is %dx%d" % bottom_size
print("PPM: top 400x240, bottom 320x240")
print("--- binary log ---")
print(run_log.rstrip())
print("--- end binary log ---")
PY
then
    echo "selftest: PASS"
else
    echo "selftest: FAIL" >&2
    exit 1
fi
