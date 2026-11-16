#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare a DeSmuME v2 capture against the host-generated expected stream.

Usage: compare_capture.py <captured.bin> <expected.txt> <angle>

Rules (all must hold):

* file flags are exactly FRAME_COMPLETE (no OVERFLOW), META backend is the
  register proxy (1) and META coverage is all three known gaps (0x7);
* the GXCM command-id sequence is identical, entry for entry;
* every non-math GXCM value is byte-identical (this includes POLYGON_ATTR
  cmd 0x29: there is no special tolerance, so cull bits 0 fail as gxcm-value);
* the math commands are checked analytically because the host mock cannot
  reproduce the DS divider, sqrt or trig tables:
    - 0x18 (MTX_MULT_4x4): closed-form gluPerspective(70, 256/192, 0.1, 100)
      in libnds glFrustumf32 write order, 0.004 tolerance (f32 units);
    - 0x19 (MTX_MULT_4x3): identity rotation + translation z = -4, 0.001;
    - 0x1A (MTX_MULT_3x3): element-wise match to the fixed libnds
      glRotateXi/glRotateYi matrix layout, 0.002 tolerance (f32 units);
* RSTA addr/gx_index/width are identical and values are identical, except for
  the FIRST 0x04000350 write: libnds keeps the polygon id in the library
  global glCurClearColor, which glInit() seeds on the DS and the host test
  leaves at zero, so that one write may differ only in the polygon-id bits
  24..29 (mask 0x3F000000). Any other difference on that write or any
  difference on a later 0x04000350 write is treated as an RSTA value mismatch.

Stdlib only, deterministic. Prints PASS or FAIL with one reason per line.
Exit 0 on PASS, 1 on FAIL, 2 on usage errors.
"""

import math
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS_DIR)

import twlhd_v2_dump  # noqa: E402

MATH_GROUP_SIZES = {0x18: 16, 0x19: 12, 0x1A: 9}
CLEAR_COLOR_ADDR = 0x04000350
CLEAR_COLOR_POLYGON_ID_MASK = 0x3F000000
BACKEND_REGISTER_PROXY = 1
COVERAGE_ALL = 0x7

PERSPECTIVE_TOLERANCE = 0.004
LOOKAT_TOLERANCE = 0.001
ROTATION_ELEMENT_TOLERANCE = 0.002
ROTATION_ORTHO_TOLERANCE = 0.002
ROTATION_DET_TOLERANCE = 0.003


class Failures:
    def __init__(self):
        self.items = []

    def add(self, reason, detail):
        self.items.append((reason, detail))

    def __bool__(self):
        return bool(self.items)


def s32(value):
    """Reinterpret an on-disk uint32 as signed."""
    return value - 0x100000000 if value >= 0x80000000 else value


def to_f32(value):
    return s32(value) / 4096.0


def read_expected(path):
    gxcm = []
    rsta = []
    with open(path, "r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "G" and len(fields) == 4:
                gxcm.append((int(fields[2], 16), int(fields[3], 16)))
            elif fields[0] == "R" and len(fields) == 6:
                rsta.append((int(fields[2], 16), int(fields[3], 16),
                             int(fields[4]), int(fields[5])))
            else:
                raise ValueError("%s:%d: malformed line" % (path, line_number))
    return gxcm, rsta


def perspective_values():
    """Flat 16-value matrix in libnds glFrustumf32 write order."""
    near = 0.1
    far = 100.0
    aspect = 256.0 / 192.0
    top = near * math.tan(math.radians(70.0) / 2.0)
    right = top * aspect
    return [
        2.0 * near / (2.0 * right), 0.0, 0.0, 0.0,
        0.0, 2.0 * near / (2.0 * top), 0.0, 0.0,
        0.0, 0.0, -(far + near) / (far - near), -1.0,
        0.0, 0.0, -2.0 * far * near / (far - near), 0.0,
    ]


def lookat_values():
    """Flat 12-value matrix in libnds gluLookAtf32 write order."""
    return [1.0, 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0,
            0.0, 0.0, -4.0]


def rotation_values(angle):
    """Flat 9-value matrices in libnds glRotateXi/glRotateYi write order.

    libnds glRotateXi builds [1,0,0, 0,c,s, 0,-s,c].
    libnds glRotateYi builds [c,0,-s, 0,1,0, s,0,c].
    Values are in 4.12 fixed point; the comparison is done in float.
    """
    a = 2.0 * math.pi * (angle & 0x7FFF) / 32768.0
    c = math.cos(a)
    s = math.sin(a)
    block0 = [1.0, 0.0, 0.0,
              0.0, c, s,
              0.0, -s, c]
    block1 = [c, 0.0, -s,
              0.0, 1.0, 0.0,
              s, 0.0, c]
    return block0, block1


def check_perspective(group, failures):
    expected = perspective_values()
    for index, (word, value) in enumerate(group):
        actual = to_f32(value)
        if abs(actual - expected[index]) > PERSPECTIVE_TOLERANCE:
            failures.add("perspective",
                         "element %d is %.6f, expected %.6f (+-%.3f)"
                         % (index, actual, expected[index],
                            PERSPECTIVE_TOLERANCE))
            return


def check_lookat(group, failures):
    expected = lookat_values()
    for index, (word, value) in enumerate(group):
        actual = to_f32(value)
        if abs(actual - expected[index]) > LOOKAT_TOLERANCE:
            failures.add("lookat",
                         "element %d is %.6f, expected %.6f (+-%.3f)"
                         % (index, actual, expected[index], LOOKAT_TOLERANCE))
            return


def check_rotation(group, angle, label, failures):
    """Element-wise comparison against the fixed libnds rotation layouts.

    The analytic orthonormality and determinant checks are kept as an extra
    sanity net, but the primary acceptance criterion is a per-element match
    to the known glRotateXi/glRotateYi matrix layout.
    """
    block0, block1 = rotation_values(angle)
    expected = block0 if "X" in label else block1
    block_index = "0" if "X" in label else "1"

    for index, (_, value) in enumerate(group):
        actual = to_f32(value)
        if abs(actual - expected[index]) > ROTATION_ELEMENT_TOLERANCE:
            failures.add("rotation",
                         "%s block %s element %d is %.6f, expected %.6f "
                         "(+-%.4f)"
                         % (label, block_index, index, actual,
                            expected[index], ROTATION_ELEMENT_TOLERANCE))
            return

    values = [to_f32(value) for _, value in group]
    m = [values[0:3], values[3:6], values[6:9]]

    worst = 0.0
    for row in range(3):
        for column in range(3):
            dot = sum(m[row][k] * m[column][k] for k in range(3))
            target = 1.0 if row == column else 0.0
            worst = max(worst, abs(dot - target))
    if worst > ROTATION_ORTHO_TOLERANCE:
        failures.add("rotation",
                     "%s is not a proper rotation (|M*M^T-I| = %.6f)"
                     % (label, worst))
        return

    determinant = (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    if abs(determinant - 1.0) > ROTATION_DET_TOLERANCE:
        failures.add("rotation",
                     "%s determinant is %.6f, expected 1" % (label, determinant))


def collect_groups(entries, failures):
    groups = {0x18: [], 0x19: [], 0x1A: []}
    index = 0
    while index < len(entries):
        word = entries[index][0]
        size = MATH_GROUP_SIZES.get(word)
        if size is None:
            index += 1
            continue
        group = entries[index:index + size]
        if len(group) != size or any(item[0] != word for item in group):
            failures.add("gxcm-group",
                         "0x%02X at entry %d is not followed by %d params"
                         % (word, index, size))
            return groups
        groups[word].append(group)
        index += size
    return groups


def compare(captured, expected_gxcm, expected_rsta, angle, failures):
    try:
        with open(captured, "rb") as handle:
            data = handle.read()
    except OSError as exc:
        failures.add("v2-parse", "cannot read %s: %s" % (captured, exc.strerror))
        return

    try:
        view = twlhd_v2_dump.parse(data)
    except twlhd_v2_dump.V2Error as exc:
        failures.add("v2-parse", "%s (%s)" % (exc.message, exc.result))
        return

    if view.flags != twlhd_v2_dump.FLAG_FRAME_COMPLETE:
        failures.add("flags", "0x%02X, expected FRAME_COMPLETE only"
                     % view.flags)
    if view.meta_backend_id != BACKEND_REGISTER_PROXY:
        failures.add("backend", "%d, expected %d"
                     % (view.meta_backend_id, BACKEND_REGISTER_PROXY))
    if view.meta_coverage_flags != COVERAGE_ALL:
        failures.add("coverage", "0x%02X, expected 0x%02X"
                     % (view.meta_coverage_flags, COVERAGE_ALL))

    actual_gxcm = list(view.gxcm)
    if len(actual_gxcm) != len(expected_gxcm):
        failures.add("gxcm-count", "%d entries, expected %d"
                     % (len(actual_gxcm), len(expected_gxcm)))
    else:
        for index, (actual, expected) in enumerate(zip(actual_gxcm,
                                                       expected_gxcm)):
            if actual[0] != expected[0]:
                failures.add("gxcm-cmd",
                             "entry %d is 0x%02X, expected 0x%02X"
                             % (index, actual[0], expected[0]))
                break
        for index, (actual, expected) in enumerate(zip(actual_gxcm,
                                                       expected_gxcm)):
            if actual[0] in MATH_GROUP_SIZES:
                continue
            if actual[1] != expected[1]:
                failures.add("gxcm-value",
                             "entry %d (0x%02X) is 0x%08X, expected 0x%08X"
                             % (index, actual[0], actual[1], expected[1]))
                break

    groups = collect_groups(actual_gxcm, failures)
    if len(groups[0x18]) != 1:
        failures.add("perspective", "%d MTX_MULT_4x4 groups, expected 1"
                     % len(groups[0x18]))
    else:
        check_perspective(groups[0x18][0], failures)
    if len(groups[0x19]) != 1:
        failures.add("lookat", "%d MTX_MULT_4x3 groups, expected 1"
                     % len(groups[0x19]))
    else:
        check_lookat(groups[0x19][0], failures)
    if len(groups[0x1A]) != 2:
        failures.add("rotation", "%d MTX_MULT_3x3 groups, expected 2"
                     % len(groups[0x1A]))
    else:
        check_rotation(groups[0x1A][0], angle, "rotation X", failures)
        check_rotation(groups[0x1A][1], angle, "rotation Y", failures)

    actual_rsta = list(view.rsta)
    if len(actual_rsta) != len(expected_rsta):
        failures.add("rsta-count", "%d entries, expected %d"
                     % (len(actual_rsta), len(expected_rsta)))
        return

    first_clear = None
    for index, expected in enumerate(expected_rsta):
        if expected[0] == CLEAR_COLOR_ADDR:
            first_clear = index
            break

    for index, (actual, expected) in enumerate(zip(actual_rsta, expected_rsta)):
        if actual[0] != expected[0] or actual[2] != expected[2] or \
                actual[3] != expected[3]:
            failures.add("rsta-field",
                         "entry %d is 0x%08X/%d/%d, expected 0x%08X/%d/%d"
                         % (index, actual[0], actual[2], actual[3],
                            expected[0], expected[2], expected[3]))
            continue
        if actual[1] == expected[1]:
            continue
        if index == first_clear and \
                ((actual[1] ^ expected[1]) & ~CLEAR_COLOR_POLYGON_ID_MASK) == 0:
            continue
        failures.add("rsta-value",
                     "entry %d (0x%08X) is 0x%08X, expected 0x%08X"
                     % (index, actual[0], actual[1], expected[1]))


def main(argv):
    if len(argv) != 4:
        sys.stderr.write(
            "usage: %s <captured.bin> <expected.txt> <angle>\n" % argv[0])
        return 2

    try:
        angle = int(argv[3], 0)
    except ValueError:
        sys.stderr.write("error: angle must be an integer\n")
        return 2

    failures = Failures()
    try:
        expected_gxcm, expected_rsta = read_expected(argv[2])
    except (OSError, ValueError) as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 2

    compare(argv[1], expected_gxcm, expected_rsta, angle, failures)

    if failures:
        print("FAIL")
        for reason, detail in failures.items:
            print("- %s: %s" % (reason, detail))
        return 1

    print("PASS: %d GXCM + %d RSTA entries match for angle %d"
          % (len(expected_gxcm), len(expected_rsta), angle))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
