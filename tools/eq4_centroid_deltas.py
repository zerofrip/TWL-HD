#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Print per-class centroid deltas from eq4_hw_compare.py reports.

    python3 tools/eq4_centroid_deltas.py report1.txt [report2.txt ...]

Each report file is the raw `key=value` stdout of tools/eq4_hw_compare.py.
For every colour class the script prints `dx` and `dy` as capture minus
reference (`class_<name>_capture_centroid_*` minus
`class_<name>_ref_centroid_*`), so a negative value means the capture sits
left/above the reference.  Exit 2 on a missing or non-numeric key.
"""

import sys

CLASSES = ("white", "red", "green", "blue", "yellow")


def parse_report(path):
    values = {}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                key, sep, value = line.strip().partition("=")
                if sep:
                    values[key] = value
    except OSError as exc:
        sys.stderr.write("eq4_centroid_deltas: cannot read %s: %s\n"
                         % (path, exc))
        raise SystemExit(2)
    return values


def deltas(values, path):
    out = []
    for name in CLASSES:
        try:
            dx = (float(values["class_%s_capture_centroid_x" % name])
                  - float(values["class_%s_ref_centroid_x" % name]))
            dy = (float(values["class_%s_capture_centroid_y" % name])
                  - float(values["class_%s_ref_centroid_y" % name]))
        except (KeyError, ValueError):
            sys.stderr.write("eq4_centroid_deltas: %s has no numeric centroid "
                             "for class %s\n" % (path, name))
            raise SystemExit(2)
        out.append((name, dx, dy))
    return out


def main(argv):
    if len(argv) < 2:
        sys.stderr.write("usage: eq4_centroid_deltas.py <report> [<report> ...]\n")
        return 2
    for path in argv[1:]:
        print(path)
        for name, dx, dy in deltas(parse_report(path), path):
            print("%s dx=%.4f dy=%.4f" % (name, dx, dy))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
