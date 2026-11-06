#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Independent first-principles oracle for the Phase 5A-1 golden test.

Recomputes the 36 clip-space cube vertices produced by the arm9v2 demo frame
using double-precision math and the demo's intent (gluPerspective, gluLookAt,
libnds rotations). It does NOT read the capture's matrix words.

Matrices are stored COLUMN-MAJOR to match the DS (index = col*4 + row).
"""

import math
import sys


def alpha_to_float(alpha):
    """POLYGON_ATTR raw 5-bit alpha -> output vertex alpha.

    Independent model of the rule encoded in the C interpreter (it is NOT
    imported from the C sources): alpha 0 is the DS wireframe value and maps to
    0.0; alpha 1..31 uses the DeSmuME blend weight (alpha + 1) / 32.
    TO BE VERIFIED: hardware/GBATEK were not checked; alpha / 31 is the
    alternative for non-zero values.
    """
    if alpha == 0:
        return 0.0
    return (alpha + 1) / 32.0


def mat_identity():
    return [1.0 if (i % 5) == 0 else 0.0 for i in range(16)]


def mat_mult(a, b):
    """a * b in column-major OpenGL column-vector convention."""
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            s = 0.0
            for k in range(4):
                # a[row][k] is at index k*4+row; b[k][col] is at index col*4+k
                s += a[k * 4 + row] * b[col * 4 + k]
            out[col * 4 + row] = s
    return out


def mat_vec(m, v):
    out = [0.0] * 4
    for row in range(4):
        s = 0.0
        for k in range(4):
            s += m[k * 4 + row] * v[k]
        out[row] = s
    return out


def gl_perspective(fovy_deg, aspect, near, far):
    """Column-major 4x4 matching libnds glFrustumf32 / DS write order."""
    top = near * math.tan(math.radians(fovy_deg) / 2.0)
    right = top * aspect
    m = mat_identity()
    # col 0
    m[0] = 2.0 * near / (2.0 * right)
    # col 1
    m[4 + 1] = 2.0 * near / (2.0 * top)
    # col 2
    m[8 + 2] = -(far + near) / (far - near)
    m[8 + 3] = -1.0
    # col 3
    m[12 + 2] = -2.0 * far * near / (far - near)
    m[12 + 3] = 0.0
    return m


def gl_look_at(eye, center, up):
    f = [center[i] - eye[i] for i in range(3)]
    fn = math.sqrt(sum(x * x for x in f))
    f = [x / fn for x in f]
    upn = math.sqrt(sum(x * x for x in up))
    u = [x / upn for x in up]
    s = [
        f[1] * u[2] - f[2] * u[1],
        f[2] * u[0] - f[0] * u[2],
        f[0] * u[1] - f[1] * u[0],
    ]
    sn = math.sqrt(sum(x * x for x in s))
    s = [x / sn for x in s]
    u = [
        s[1] * f[2] - s[2] * f[1],
        s[2] * f[0] - s[0] * f[2],
        s[0] * f[1] - s[1] * f[0],
    ]
    m = mat_identity()
    # column 0
    m[0] = s[0]
    m[1] = u[0]
    m[2] = -f[0]
    # column 1
    m[4 + 0] = s[1]
    m[4 + 1] = u[1]
    m[4 + 2] = -f[1]
    # column 2
    m[8 + 0] = s[2]
    m[8 + 1] = u[2]
    m[8 + 2] = -f[2]
    # column 3
    m[12 + 0] = -sum(s[i] * eye[i] for i in range(3))
    m[12 + 1] = -sum(u[i] * eye[i] for i in range(3))
    m[12 + 2] = sum(f[i] * eye[i] for i in range(3))
    return m


def gl_rotate_x(angle_units):
    """Standard Rx(θ) with θ = 2π·(angle & 0x7FFF)/32768.

    libnds glRotateXi writes the words [1,0,0, 0,c,s, 0,-s,c].
    DS matrix words are stored COLUMN-MAJOR (index = col*4 + row), so:
        word 0 = col0 row0 = 1
        word 3 = col1 row0 = 0      word 4 = col1 row1 = c    word 5 = col1 row2 = s
        word 6 = col2 row0 = 0      word 7 = col2 row1 = -s   word 8 = col2 row2 = c
    Columns: col0=(1,0,0), col1=(0,c,s), col2=(0,-s,c).
    As rows this is the standard Rx(θ) = [[1,0,0],[0,c,-s],[0,s,c]].
    """
    rad = 2.0 * math.pi * ((angle_units & 0x7FFF) / 32768.0)
    c = math.cos(rad)
    s = math.sin(rad)
    m = mat_identity()
    m[4 + 1] = c
    m[4 + 2] = s
    m[8 + 1] = -s
    m[8 + 2] = c
    return m


def gl_rotate_y(angle_units):
    """Standard Ry(θ) with θ = 2π·(angle & 0x7FFF)/32768.

    libnds glRotateYi writes the words [c,0,-s, 0,1,0, s,0,c].
    DS matrix words are stored COLUMN-MAJOR (index = col*4 + row), so:
        word 0 = col0 row0 = c      word 1 = col0 row1 = 0    word 2 = col0 row2 = -s
        word 3 = col1 row0 = 0      word 4 = col1 row1 = 1    word 5 = col1 row2 = 0
        word 6 = col2 row0 = s      word 7 = col2 row1 = 0    word 8 = col2 row2 = c
    Columns: col0=(c,0,-s), col1=(0,1,0), col2=(s,0,c).
    As rows this is the standard Ry(θ) = [[c,0,s],[0,1,0],[-s,0,c]].
    """
    rad = 2.0 * math.pi * ((angle_units & 0x7FFF) / 32768.0)
    c = math.cos(rad)
    s = math.sin(rad)
    m = mat_identity()
    m[0] = c
    m[2] = -s
    m[8 + 0] = s
    m[8 + 2] = c
    return m


# Demo vertex order from arm9v2/source/demo_frame.cpp.
CUBE_VERTICES = [
    (-1.0, -1.0, 1.0),   # 0 front bottom left
    (1.0, -1.0, 1.0),    # 1 front bottom right
    (1.0, 1.0, 1.0),     # 2 front top right
    (-1.0, 1.0, 1.0),    # 3 front top left
    (-1.0, -1.0, -1.0),  # 4 back bottom left
    (1.0, -1.0, -1.0),   # 5 back bottom right
    (1.0, 1.0, -1.0),    # 6 back top right
    (-1.0, 1.0, -1.0),   # 7 back top left
]

CUBE_FACES = [
    (0, 1, 2, 3),  # front
    (5, 4, 7, 6),  # back
    (4, 0, 3, 7),  # left
    (1, 5, 6, 2),  # right
    (3, 2, 6, 7),  # top
    (4, 5, 1, 0),  # bottom
]

CUBE_COLOURS = [
    (1.0, 0.0, 0.0, 1.0),   # red
    (0.0, 1.0, 0.0, 1.0),   # green
    (0.0, 0.0, 1.0, 1.0),   # blue
    (1.0, 1.0, 0.0, 1.0),   # yellow
    (1.0, 0.0, 1.0, 1.0),   # magenta
    (0.0, 1.0, 1.0, 1.0),   # cyan
]


def demo_clip_matrix(angle_units=61440):
    """
    Reproduce the captured transform chain in the same order the DS applies it:
    clip = Projection * LookAt * RotateX * RotateY * vertex.
    """
    proj = gl_perspective(70.0, 256.0 / 192.0, 0.1, 100.0)
    look = gl_look_at((0.0, 0.0, 4.0), (0.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    rx = gl_rotate_x(angle_units)
    ry = gl_rotate_y(angle_units)
    # glRotateXi then glRotateYi: current = current * rx, then current * ry.
    modelview = mat_mult(look, mat_mult(rx, ry))
    return mat_mult(proj, modelview)


def expected_vertices(angle_units=61440):
    clip = demo_clip_matrix(angle_units)
    out = []
    for face_index, face in enumerate(CUBE_FACES):
        colour = CUBE_COLOURS[face_index]
        # Expand the quad (v0,v1,v2,v3) into two triangles as the DS does.
        tri_indices = [(0, 1, 2), (0, 2, 3)]
        for a, b, c in tri_indices:
            for vi in (face[a], face[b], face[c]):
                v = CUBE_VERTICES[vi] + (1.0,)
                cv = mat_vec(clip, v)
                out.append({
                    "clip": cv,
                    "rgba": list(colour),
                })
    return out


def main():
    angle = int(sys.argv[1]) if len(sys.argv) > 1 else 61440
    verts = expected_vertices(angle)
    for i, v in enumerate(verts):
        print(
            f"{i} "
            f"{v['clip'][0]:.6f} {v['clip'][1]:.6f} "
            f"{v['clip'][2]:.6f} {v['clip'][3]:.6f} "
            f"{v['rgba'][0]:.6f} {v['rgba'][1]:.6f} "
            f"{v['rgba'][2]:.6f} {v['rgba'][3]:.6f}"
        )


if __name__ == "__main__":
    main()
