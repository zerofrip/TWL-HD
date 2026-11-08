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


# ---------------------------------------------------------------------------
# Phase 5B-1 lighting oracle.
#
# Independently implements DeSmuME's integer lighting equations (gfx3d.cpp
# 1158-1331, 2117-2150 and matrix.cpp 560-610) plus the vector-matrix state
# those commands use. It does not read the C interpreter's state. Values are
# 20.12 fixed point unless stated otherwise.
# ---------------------------------------------------------------------------

FX_ONE = 4096
LIGHT_COUNT = 4
SHININESS_TABLE_SIZE = 128

CMD_MTX_MODE = 0x10
CMD_MTX_PUSH = 0x11
CMD_MTX_POP = 0x12
CMD_MTX_STORE = 0x13
CMD_MTX_RESTORE = 0x14
CMD_MTX_IDENTITY = 0x15
CMD_MTX_LOAD_4x4 = 0x16
CMD_MTX_LOAD_4x3 = 0x17
CMD_MTX_MULT_4x4 = 0x18
CMD_MTX_MULT_4x3 = 0x19
CMD_MTX_MULT_3x3 = 0x1A
CMD_MTX_SCALE = 0x1B
CMD_MTX_TRANS = 0x1C
CMD_COLOR = 0x20
CMD_NORMAL = 0x21
CMD_POLYGON_ATTR = 0x29
CMD_DIF_AMB = 0x30
CMD_SPE_EMI = 0x31
CMD_LIGHT_VECTOR = 0x32
CMD_LIGHT_COLOR = 0x33
CMD_SHININESS = 0x34
CMD_BEGIN_VTXS = 0x40
CMD_END_VTXS = 0x41
CMD_SWAP_BUFFERS = 0x50

PARAM_COUNT = {
    CMD_MTX_MODE: 1, CMD_MTX_PUSH: 0, CMD_MTX_POP: 1,
    CMD_MTX_STORE: 1, CMD_MTX_RESTORE: 1, CMD_MTX_IDENTITY: 0,
    CMD_MTX_LOAD_4x4: 16, CMD_MTX_LOAD_4x3: 12, CMD_MTX_MULT_4x4: 16,
    CMD_MTX_MULT_4x3: 12, CMD_MTX_MULT_3x3: 9, CMD_MTX_SCALE: 3,
    CMD_MTX_TRANS: 3, CMD_COLOR: 1, CMD_NORMAL: 1, CMD_POLYGON_ATTR: 1,
    CMD_DIF_AMB: 1, CMD_SPE_EMI: 1, CMD_LIGHT_VECTOR: 1,
    CMD_LIGHT_COLOR: 1, CMD_SHININESS: 32, CMD_BEGIN_VTXS: 1,
    CMD_END_VTXS: 0, CMD_SWAP_BUFFERS: 1,
}


def s32(value):
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def sign_extend10(word):
    value = word & 0x3FF
    return value - 0x400 if value & 0x200 else value


def mat_identity_fixed():
    return [FX_ONE if (i % 5) == 0 else 0 for i in range(16)]


def mat_mult_fixed(a, b):
    out = [0] * 16
    for col in range(4):
        for row in range(4):
            acc = 0
            for k in range(4):
                acc += a[k * 4 + row] * b[col * 4 + k]
            out[col * 4 + row] = acc >> 12
    return out


def sat_shiftdown(acc):
    """matrix.cpp:560-575: clamp the summed 64-bit accumulator, then >>12."""
    if acc > 0x000007FFFFFFFFFF:
        return 0x7FFFFFFF
    if acc < -0x80000000000:
        return -0x80000000
    return acc >> 12


def mat3_transform(matrix, vec):
    out = []
    for row in range(3):
        acc = (matrix[0 * 4 + row] * vec[0] +
               matrix[1 * 4 + row] * vec[1] +
               matrix[2 * 4 + row] * vec[2])
        out.append(sat_shiftdown(acc))
    return out


def mul_fixed32(a, b):
    return (a * b) >> 12


def vec3dot_fixed32(a, b):
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) >> 12


def rgb555_channels(word):
    return [(word >> 0) & 0x1F, (word >> 5) & 0x1F, (word >> 10) & 0x1F]


def _cdiv_trunc(a, b):
    """C integer division truncates toward zero; b must be positive."""
    return a // b if a >= 0 else -((-a) // b)


class LightingOracle:
    """Vector matrix plus latched material/light state, driven by a command
    list in the same v2 entry format the golden streams use."""

    def __init__(self):
        self.mode = 0
        self.vec = mat_identity_fixed()
        self.stack = []
        self.diffuse = 0
        self.ambient = 0
        self.specular = 0
        self.emission = 0
        self.light_color = [0] * LIGHT_COUNT
        self.light_dir = [[0, 0, 0] for _ in range(LIGHT_COUNT)]
        self.light_half_neg = [[0, 0, 0] for _ in range(LIGHT_COUNT)]
        self.shininess_pending = [0] * SHININESS_TABLE_SIZE
        self.shininess_applied = [0] * SHININESS_TABLE_SIZE
        self.shininess_index = 0
        self.pending_mask = 0
        self.applied_mask = 0
        self.colour = (0.0, 0.0, 0.0)
        for index in range(LIGHT_COUNT):
            self._update_half_vector(index)

    # -- vector matrix operations (only the vector matrix moves) -----------
    def load_4x4(self, words):
        self.vec = [s32(w) for w in words]

    def load_4x3(self, words):
        vec = [0] * 16
        for col in range(3):
            for row in range(3):
                vec[col * 4 + row] = s32(words[col * 3 + row])
        vec[15] = FX_ONE
        self.vec = vec

    def mult_4x4(self, words):
        self.vec = mat_mult_fixed(self.vec, [s32(w) for w in words])

    def mult_4x3(self, words):
        self.vec = mat_mult_fixed(self.vec, self._load_4x3_matrix(words))

    def mult_3x3(self, words):
        self.vec = mat_mult_fixed(self.vec, self._load_3x3_matrix(words))

    def translate(self, words):
        op = mat_identity_fixed()
        op[12 + 0] = s32(words[0])
        op[12 + 1] = s32(words[1])
        op[12 + 2] = s32(words[2])
        self.vec = mat_mult_fixed(self.vec, op)

    @staticmethod
    def _load_4x3_matrix(words):
        vec = [0] * 16
        for col in range(3):
            for row in range(3):
                vec[col * 4 + row] = s32(words[col * 3 + row])
        vec[15] = FX_ONE
        return vec

    @staticmethod
    def _load_3x3_matrix(words):
        vec = mat_identity_fixed()
        for col in range(3):
            for row in range(3):
                vec[col * 4 + row] = s32(words[col * 3 + row])
        return vec

    def push(self):
        if len(self.stack) < 31:
            self.stack.append(list(self.vec))

    def pop(self, count):
        if count <= 0 or count > len(self.stack):
            return
        self.vec = self.stack[-count]
        del self.stack[-count:]

    def store(self, index):
        if index < 31:
            while len(self.stack) <= index:
                self.stack.append(mat_identity_fixed())
            self.stack[index] = list(self.vec)

    def restore(self, index):
        if index < len(self.stack):
            self.vec = list(self.stack[index])

    # -- lighting state ----------------------------------------------------
    def set_light_vector(self, param):
        index = (param >> 30) & 3
        direction = [
            sign_extend10(param) * 8,
            sign_extend10(param >> 10) * 8,
            sign_extend10(param >> 20) * 8,
        ]
        self.light_dir[index] = mat3_transform(self.vec, direction)
        self._update_half_vector(index)

    def _update_half_vector(self, index):
        light = self.light_dir[index]
        half = [light[0], light[1], light[2] - FX_ONE]
        dot = vec3dot_fixed32(half, half)
        half_length = (math.isqrt(dot) if dot > 0 else 0) << 6
        if half_length != 0:
            half_length = abs(half_length) >> 6
            half = [_cdiv_trunc(c * 64, half_length) for c in half]
        self.light_half_neg[index] = [-c for c in half]

    def set_shininess(self, words):
        for word in words:
            for byte_index in range(4):
                self.shininess_pending[self.shininess_index] = (
                    (word >> (byte_index * 8)) & 0xFF
                )
                self.shininess_index += 1
            if self.shininess_index >= SHININESS_TABLE_SIZE:
                self.shininess_applied = list(self.shininess_pending)
                self.shininess_index = 0

    def normal(self, param):
        normal = [
            sign_extend10(param) * 8,
            sign_extend10(param >> 10) * 8,
            sign_extend10(param >> 20) * 8,
        ]
        normal = mat3_transform(self.vec, normal)

        diffuse = rgb555_channels(self.diffuse)
        ambient = rgb555_channels(self.ambient)
        specular = rgb555_channels(self.specular)
        vertex = rgb555_channels(self.emission)

        for index in range(LIGHT_COUNT):
            if not ((self.applied_mask >> index) & 1):
                continue
            light = rgb555_channels(self.light_color[index])

            fixed_diffuse = -vec3dot_fixed32(self.light_dir[index], normal)
            fixed_diffuse = max(0, fixed_diffuse)

            dot = vec3dot_fixed32(self.light_half_neg[index], normal)
            shininess = 0
            if dot > 0:
                shininess = 2 * mul_fixed32(dot, dot) - FX_ONE
            shininess = min(shininess, 4095)
            shininess = max(shininess, 0)
            if self.specular & 0x8000:
                shininess = self.shininess_applied[shininess >> 5] << 4

            for channel in range(3):
                vertex[channel] += (
                    (specular[channel] * light[channel] * shininess) >> 17
                ) + (
                    (diffuse[channel] * light[channel] * fixed_diffuse) >> 17
                ) + (
                    (ambient[channel] * light[channel]) >> 5
                )

        return tuple(min(31, c) / 31.0 for c in vertex)

    # -- command stream driver --------------------------------------------
    def process(self, entries):
        """Run the lighting-relevant commands of a v2 entry list in order and
        leave the final latched colour in self.colour."""
        index = 0
        while index < len(entries):
            command, _ = entries[index]
            count = PARAM_COUNT.get(command, 0)
            params = [entries[index + i][1] for i in range(count)]
            index += count if count else 1

            if command == CMD_MTX_MODE:
                self.mode = params[0] & 3
            elif command == CMD_MTX_PUSH:
                if self.mode != 0:
                    self.push()
            elif command == CMD_MTX_POP:
                if self.mode not in (0, 3):
                    count_words = params[0] & 0x3F
                    if params[0] & 0x20:
                        count_words -= 0x40
                    self.pop(count_words)
            elif command == CMD_MTX_STORE:
                if self.mode not in (0, 3):
                    self.store(params[0] & 0x1F)
            elif command == CMD_MTX_RESTORE:
                if self.mode not in (0, 3):
                    self.restore(params[0] & 0x1F)
            elif self.mode == 2:
                if command == CMD_MTX_IDENTITY:
                    self.vec = mat_identity_fixed()
                elif command == CMD_MTX_LOAD_4x4:
                    self.load_4x4(params)
                elif command == CMD_MTX_LOAD_4x3:
                    self.load_4x3(params)
                elif command == CMD_MTX_MULT_4x4:
                    self.mult_4x4(params)
                elif command == CMD_MTX_MULT_4x3:
                    self.mult_4x3(params)
                elif command == CMD_MTX_MULT_3x3:
                    self.mult_3x3(params)
                elif command == CMD_MTX_TRANS:
                    self.translate(params)
                # CMD_MTX_SCALE deliberately leaves the vector matrix alone.

            if command == CMD_POLYGON_ATTR:
                self.pending_mask = params[0] & 0xF
            elif command == CMD_BEGIN_VTXS:
                self.applied_mask = self.pending_mask
            elif command == CMD_DIF_AMB:
                self.diffuse = params[0] & 0xFFFF
                self.ambient = (params[0] >> 16) & 0xFFFF
            elif command == CMD_SPE_EMI:
                self.specular = params[0] & 0xFFFF
                self.emission = (params[0] >> 16) & 0xFFFF
            elif command == CMD_LIGHT_COLOR:
                self.light_color[(params[0] >> 30) & 3] = params[0]
            elif command == CMD_LIGHT_VECTOR:
                self.set_light_vector(params[0])
            elif command == CMD_SHININESS:
                self.set_shininess(params)
            elif command == CMD_NORMAL:
                self.colour = self.normal(params[0])

        return self.colour


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
