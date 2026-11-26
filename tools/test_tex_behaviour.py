#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for the texture-behaviour frame tooling (5B-2e-2 follow-up).

Covers tools/gen_tex_behaviour_frames.py and tools/check_tex_behaviour.py:

  - the generator is deterministic and the frame parses with the repo parser
  - twlhd_pica_frame_prepare() accepts the 8 records and reports the wrap
    codes the DS TEXIMAGE_PARAM bits 16-19 imply
  - the checker accepts a perfect / noisy / 1-px-shifted synthetic image
  - the checker rejects: translucent drawn opaque, colour-0 transparency not
    applied, repeat drawn clamped, mirror drawn as repeat, flip-only drawn as
    mirrored, a vertically flipped wrap quad and a missing quad
  - hand-computed literal blend/wrap colours for one cell of each behaviour

Runs under `make test` via `python3 -m unittest discover -s tools`.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import gen_tex_behaviour_frames as gen  # noqa: E402
import check_tex_behaviour as chk  # noqa: E402
import tex_decode_oracle as oracle  # noqa: E402

SCRIPT = os.path.join(TOOLS_DIR, "check_tex_behaviour.py")
VALIDATE = os.path.join(TOOLS_DIR, "twlhd_v2_dump.py")

WIDTH = 400
HEIGHT = 480
# OLD-build clear colour for this synthetic validator; kept self-consistent.
CLEAR = (16, 16, 24)

SPECS = gen.QUAD_SPECS
PIXELS = [gen.expected_pixels(spec) for spec in SPECS]
EXPECTED = [chk.expected_cells(spec) for spec in SPECS]


def _ensure_cli(target):
    """Return the CLI path, building it with make when a compiler exists."""
    path = os.path.join(ROOT, target)
    if os.access(path, os.X_OK):
        return path
    if shutil.which(os.environ.get("CC", "cc")) is None:
        return None
    if shutil.which("make") is None:
        return None
    proc = subprocess.run(["make", target], cwd=ROOT, capture_output=True,
                          text=True)
    if proc.returncode != 0 or not os.access(path, os.X_OK):
        return None
    return path


def _default_colour(index, cell):
    return EXPECTED[index][cell]


def image_bytes(colour_at, dx=0, dy=0, skip=(), height=HEIGHT):
    """Render the quads into a PPM body using colour_at(index, cell)."""
    data = bytearray()
    for y in range(height):
        for x in range(WIDTH):
            colour = CLEAR
            if y < 240:
                for index, spec in enumerate(SPECS):
                    if index in skip:
                        continue
                    x0, y0, x1, y1 = spec["rect"]
                    if x0 <= x - dx < x1 and y0 <= y - dy < y1:
                        grid = spec["grid"]
                        ci = min(grid - 1,
                                 (x - dx - x0) * grid // (x1 - x0))
                        cj = min(grid - 1,
                                 (y - dy - y0) * grid // (y1 - y0))
                        colour = colour_at(index, cj * grid + ci)
                        break
            data += bytes(colour)
    return b"P6\n400 %d\n255\n" % height + bytes(data)


def write_image(directory, name, *args, **kwargs):
    path = os.path.join(directory, name)
    with open(path, "wb") as handle:
        handle.write(image_bytes(*args, **kwargs))
    return path


def run_checker(path, extra=()):
    proc = subprocess.run(
        [sys.executable, "-B", "-I", SCRIPT, path] + list(extra),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def wrap_colour(spec, cell, mode_s, mode_t, flip_rows=False):
    """Screen colour of a wrap cell when drawn with the given wrap modes."""
    i = cell % 16
    j = cell // 16
    s0, t0, s1, t1 = spec["span"]
    s_texel = (s0 + (i + 0.5) * (s1 - s0) / 16.0) * spec["width"]
    t_texel = (t0 + (j + 0.5) * (t1 - t0) / 16.0) * spec["height"]
    col = chk.wrap_coord(s_texel, spec["width"], mode_s)
    row = chk.wrap_coord(t_texel, spec["height"], mode_t)
    if flip_rows:
        row = spec["height"] - 1 - row
    pixels = PIXELS[SPECS.index(spec)]
    return pixels[row * spec["width"] + col][:3]


class GeneratorTest(unittest.TestCase):
    def test_deterministic_and_fixed_size(self):
        first = gen.frame_bytes()
        second = gen.frame_bytes()
        self.assertEqual(first, second)
        self.assertEqual(len(first), 2776)

    def test_validate_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = gen.write_frame(tmp)
            proc = subprocess.run(
                [sys.executable, "-B", VALIDATE, "validate", path],
                capture_output=True, text=True, check=True)
            self.assertIn("result=OK", proc.stdout)
            self.assertIn("entries=185", proc.stdout)

    def test_texture_records(self):
        frame = gen.frame_bytes()
        self.assertEqual(struct.unpack_from("<I", frame, 8)[0], len(frame))
        chunk_count = struct.unpack_from("<I", frame, 20)[0]
        chunks = {}
        offset = 32
        for _ in range(chunk_count):
            ctype, size = struct.unpack_from("<II", frame, offset)
            chunks[ctype] = frame[offset + 16:offset + 16 + size]
            offset += 16 + size

        txds = chunks[gen.TXDS]
        self.assertEqual(len(txds) // 40, 8)
        keys = set()
        formats = []
        texels = []
        for i in range(8):
            record = struct.unpack_from("<10I", txds, i * 40)
            param = record[0]
            keys.add((param, record[1]))
            formats.append((param >> 26) & 7)
            texels.append(chunks[gen.TXDT][record[2]:record[2] + record[3]])
        self.assertEqual(len(keys), 8)
        self.assertEqual(formats, [1, 6, 3, 3, 7, 7, 7, 7])
        # Q2/Q3 share the I4 texture and palette; Q4-Q7 share the ramp.
        self.assertEqual(texels[2], texels[3])
        self.assertEqual(texels[4], texels[5])
        self.assertEqual(texels[5], texels[6])
        self.assertEqual(texels[6], texels[7])
        self.assertEqual(SPECS[2]["palette"], SPECS[3]["palette"])
        self.assertEqual(SPECS[2]["param"] & gen.KEY_COLOR0, gen.KEY_COLOR0)
        self.assertEqual(SPECS[3]["param"] & gen.KEY_COLOR0, 0)

    def test_describe_prints_roles(self):
        proc = subprocess.run(
            [sys.executable, "-B", os.path.join(
                TOOLS_DIR, "gen_tex_behaviour_frames.py"), "--describe"],
            capture_output=True, text=True, check=True)
        for index in range(8):
            self.assertIn("quad %d:" % index, proc.stdout)
        for role in ("A3I5 alpha", "A5I3 alpha", "I4 key0 on", "I4 key0 off",
                     "repeat ST", "clamp ST", "mirror ST",
                     "flip-only clamp"):
            self.assertIn("role=%s" % role, proc.stdout)


class PrepareTest(unittest.TestCase):
    def test_prepare_accepts_frame_records(self):
        cli = _ensure_cli("tests/host/build/pica_frame_cli")
        if cli is None:
            self.skipTest("pica_frame_cli not buildable (no C compiler/make)")

        blob = bytearray()
        blob += struct.pack("<I", 3 * len(SPECS))
        for spec in SPECS:
            width, height = spec["width"], spec["height"]
            for s, t in spec["texcoords"][:3]:
                blob += struct.pack("<ffII", s * width, t * height,
                                    spec["param"], 0)
        blob += struct.pack("<I", len(SPECS))
        for spec in SPECS:
            blob += struct.pack("<IIIII", spec["param"], 0,
                                len(spec["texel"]), len(spec["palette"]),
                                len(spec["index"]))
            blob += spec["texel"] + spec["palette"] + spec["index"]

        proc = subprocess.run([cli], input=bytes(blob), capture_output=True,
                              text=False)
        self.assertEqual(proc.returncode, 0,
                         proc.stderr.decode("utf-8", "replace"))
        out = proc.stdout.decode("utf-8", "replace")
        self.assertIn("result=OK", out)
        self.assertIn("entry_count=8", out)
        self.assertIn("texture_count=8", out)

        wraps = {}
        for line in out.splitlines():
            if not line.startswith("texture "):
                continue
            parts = line.split()
            index = int(parts[1])
            fields = dict(item.split("=", 1) for item in parts[2:])
            wraps[index] = (fields["wrap_s"], fields["wrap_t"])
        # DS bits 16-19: repeat=2, clamp=0, mirrored repeat=3.
        self.assertEqual(wraps[4], ("2", "2"))   # REPEAT
        self.assertEqual(wraps[5], ("0", "0"))   # CLAMP
        self.assertEqual(wraps[6], ("3", "3"))   # MIRRORED_REPEAT
        self.assertEqual(wraps[7], ("0", "0"))   # flip-only == clamp


class HandComputedColoursTest(unittest.TestCase):
    """Literals derived by hand from the DS rules in docs/DS_GX_RESEARCH.md.

    Blend: (texel*alpha + clear*(255-alpha)) // 255, clear=(16,16,24).
    Wrap: repeat = mod, mirrored = triangle about h, clamp = min/max.
    """

    def test_blend_literals(self):
        # A3I5 cell (2,0): yellow texel, alpha 5 -> THREE_TO_EIGHT[5] = 182.
        # r=(255*182+16*73)//255=186, g same, b=(0+24*73)//255=6.
        self.assertEqual(PIXELS[0][2][3], 182)
        self.assertEqual(EXPECTED[0][2], (186, 186, 6))
        # A5I3 cell (2,0): magenta texel, alpha 20 -> FIVE_TO_EIGHT[20]=165.
        # r=(255*165+16*90)//255=170, g=(0+16*90)//255=5,
        # b=(255*165+24*90)//255=173.
        self.assertEqual(PIXELS[1][2][3], 165)
        self.assertEqual(EXPECTED[1][2], (170, 5, 173))

    def test_wrap_literals(self):
        # Q4 cell (10,0): s_texel=10.5, repeat mod 8 -> col 2, row 0.
        # Ramp (2,0): R=2*4 -> FIVE_TO_EIGHT[8]=66, G=0.
        self.assertEqual(EXPECTED[4][10], (66, 0, 0))
        # Q6 cell (10,0): 10.5 mirrored about 8 -> 16-10.5=5.5 -> col 5.
        # Ramp (5,0): R=5*4 -> FIVE_TO_EIGHT[20]=165.
        self.assertEqual(EXPECTED[6][10], (165, 0, 0))
        # Q5 cell (13,0): s_texel=9.5 clamped to 7 -> Ramp (7,0) R=231.
        self.assertEqual(EXPECTED[5][13], (231, 0, 0))
        # Q7 (flip-only == clamp) cell (10,0): 10.5 clamped to 7 -> 231.
        self.assertEqual(EXPECTED[7][10], (231, 0, 0))
        # Q4 cell (0,10): t_texel=10.5 repeat -> row 2, G=2*4 -> 66.
        self.assertEqual(EXPECTED[4][10 * 16], (0, 66, 0))
        # Q5 cell (0,0): clamp to (0,0), the unique marker B=31 -> 255.
        self.assertEqual(EXPECTED[5][0], (0, 0, 255))


class CheckerTest(unittest.TestCase):
    def assert_passes(self, path, extra=()):
        rc, out, err = run_checker(path, extra)
        self.assertEqual(rc, 0, "stdout:\n%s\nstderr:\n%s" % (out, err))
        self.assertNotIn("FAIL", out)
        for index in range(8):
            self.assertIn("quad %d role=" % index, out)

    def assert_fails(self, path):
        rc, out, err = run_checker(path)
        self.assertNotEqual(rc, 0, "stdout:\n%s\nstderr:\n%s" % (out, err))
        self.assertIn("FAIL", out)
        return out

    def test_perfect_image_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "perfect.ppm", _default_colour)
            self.assert_passes(path)
            _, out, _ = run_checker(path)
            self.assertIn("quad 0 role=A3I5 alpha cells=64/64", out)
            self.assertIn("quad 4 role=repeat ST cells=256/256", out)

    def test_top_screen_only_image_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "top.ppm", _default_colour, height=240)
            self.assert_passes(path)

    def test_noise_and_shift_pass(self):
        def noisy(index, cell):
            base = _default_colour(index, cell)
            return tuple(
                max(0, min(255, base[channel] +
                           ((index * 7 + cell * 37 + channel * 53) % 31) - 15))
                for channel in range(3))

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_passes(write_image(tmp, "noise.ppm", noisy))
        with tempfile.TemporaryDirectory() as tmp:
            self.assert_passes(write_image(tmp, "shift.ppm", _default_colour,
                                           dx=1, dy=1))

    def test_translucent_drawn_opaque_fails(self):
        def opaque(index, cell):
            if index == 0:
                return PIXELS[0][cell][:3]
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "opaque.ppm", opaque))

    def test_colour_zero_not_applied_fails(self):
        spec = SPECS[2]
        visible = oracle.decode(spec["param"] & ~gen.KEY_COLOR0,
                                spec["texel"], spec["palette"], spec["index"])

        def shown(index, cell):
            if index == 2:
                return visible[cell][:3]
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "key0.ppm", shown))

    def test_repeat_drawn_clamped_fails(self):
        def clamped(index, cell):
            if index == 4:
                return wrap_colour(SPECS[4], cell, "clamp", "clamp")
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "clampq4.ppm", clamped))

    def test_mirror_drawn_as_repeat_fails(self):
        def repeated(index, cell):
            if index == 6:
                return wrap_colour(SPECS[6], cell, "repeat", "repeat")
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "repeatq6.ppm", repeated))

    def test_flip_only_drawn_as_mirror_fails(self):
        def mirrored(index, cell):
            if index == 7:
                return wrap_colour(SPECS[7], cell, "mirror", "mirror")
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "mirrorq7.ppm", mirrored))

    def test_vertically_flipped_wrap_quad_fails(self):
        def flipped(index, cell):
            if index == 4:
                return wrap_colour(SPECS[4], cell, "repeat", "repeat",
                                   flip_rows=True)
            return _default_colour(index, cell)

        with tempfile.TemporaryDirectory() as tmp:
            self.assert_fails(write_image(tmp, "flipq4.ppm", flipped))

    def test_missing_quad_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_image(tmp, "missing.ppm", _default_colour, skip=(6,))
            out = self.assert_fails(path)
            self.assertIn("quad 6 role=mirror ST MISSING", out)


if __name__ == "__main__":
    unittest.main()
