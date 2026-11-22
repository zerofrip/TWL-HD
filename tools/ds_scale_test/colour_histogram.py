#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print the most frequent RGB colours of the top-left 400x240 capture crop.

    python3 colour_histogram.py <capture.png> [top_n]

One `(r, g, b) count` line per colour, ordered by count descending then RGB
ascending (deterministic).  Default top_n = 16.  Requires PIL; exits 2 with a
one-line message when PIL is missing or the PNG cannot be read.
"""

import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - depends on the host
    Image = None

REGION_WIDTH = 400
REGION_HEIGHT = 240
TOP_N = 16


def histogram(image, top_n=TOP_N):
    """Return ((r, g, b), count) pairs, count desc then RGB asc."""
    region = image.convert("RGB").crop(
        (0, 0, min(REGION_WIDTH, image.width),
         min(REGION_HEIGHT, image.height)))
    raw = region.tobytes()
    counts = {}
    for i in range(0, len(raw), 3):
        rgb = (raw[i], raw[i + 1], raw[i + 2])
        counts[rgb] = counts.get(rgb, 0) + 1
    return sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:top_n]


def main(argv):
    if len(argv) not in (2, 3):
        sys.stderr.write("usage: colour_histogram.py <capture.png> [top_n]\n")
        return 2
    top_n = TOP_N
    if len(argv) == 3:
        try:
            top_n = int(argv[2])
        except ValueError:
            sys.stderr.write(
                "colour_histogram: top_n must be an integer: %s\n" % argv[2])
            return 2
    if Image is None:
        sys.stderr.write(
            "colour_histogram: PIL is not installed (pip install Pillow)\n")
        return 2
    try:
        image = Image.open(argv[1])
        image.load()
    except (OSError, ValueError) as exc:
        sys.stderr.write(
            "colour_histogram: cannot read %s: %s\n" % (argv[1], exc))
        return 2
    for (r, g, b), count in histogram(image, top_n):
        print("(%d, %d, %d) %d" % (r, g, b, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
