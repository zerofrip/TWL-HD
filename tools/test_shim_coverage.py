# SPDX-License-Identifier: Apache-2.0
"""Shim port-coverage test (no compiler required).

Parses the installed libnds video.h, extracts the GX/MATRIX port macros, and
checks that arm9v2/include/twlhd_gx_shim.hpp proxies every write-capable port
and deliberately leaves the documented read-only ports untouched.

DEVKITPRO must be set: this test is a hard failure otherwise (not a skip),
because the shim's coverage only means something against the real header.
"""

import os
import re
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
SHIM = os.path.join(ROOT, "arm9v2", "include", "twlhd_gx_shim.hpp")

SCALAR_RE = re.compile(
    r"^\s*#define\s+(\w+)\s+\(\*\((vu|vs)(8|16|32)\*\)\s*(0x[0-9A-Fa-f]+)\)",
    re.MULTILINE)
ARRAY_RE = re.compile(
    r"^\s*#define\s+(\w+)\s+\(\((vu|vs)(8|16|32)\*\)\s*\(?\s*"
    r"(0x[0-9A-Fa-f]+)\s*\)?\)",
    re.MULTILINE)
SHIM_RE = re.compile(
    r"^\s*#define\s+(\w+)\s+\(::twlhd_shim::(port|array_port)<\s*"
    r"(0x[0-9A-Fa-f]+)u?\s*,\s*(\d+)u?\s*>\(\)\)",
    re.MULTILINE)

# Documented read-only ports (see the shim header). They must NOT be proxied.
READ_ONLY = {
    "GFX_STATUS",
    "GFX_RDLINES_COUNT",
    "GFX_VERTEX_RAM_USAGE",
    "GFX_POLYGON_RAM_USAGE",
    "GFX_POS_RESULT",
    "GFX_VEC_RESULT",
    "MATRIX_READ_CLIP",
    "MATRIX_READ_VECTOR",
    "POINT_RESULT",
    "VECTOR_RESULT",
}


def gx_ports(header_text):
    """Return {name: (width, address, is_array)} for GFX_/MATRIX_ ports."""
    ports = {}
    for name, _sign, bits, addr in SCALAR_RE.findall(header_text):
        if name.startswith(("GFX_", "MATRIX_")) or name in (
                "POINT_RESULT", "VECTOR_RESULT"):
            ports[name] = (int(bits) // 8, int(addr, 16), False)
    for name, _sign, bits, addr in ARRAY_RE.findall(header_text):
        if name.startswith(("GFX_", "MATRIX_")) or name in (
                "POINT_RESULT", "VECTOR_RESULT"):
            ports[name] = (int(bits) // 8, int(addr, 16), True)
    return ports


def shim_ports(shim_text):
    out = {}
    for name, kind, addr, width in SHIM_RE.findall(shim_text):
        out[name] = (int(width), int(addr, 16), kind == "array_port")
    return out


class ShimCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        devkitpro = os.environ.get("DEVKITPRO")
        if not devkitpro:
            raise RuntimeError(
                "DEVKITPRO is not set: the shim port-coverage test needs the "
                "installed libnds headers (export DEVKITPRO=/opt/devkitpro)")

        header = os.path.join(devkitpro, "libnds", "include", "nds", "arm9",
                              "video.h")
        if not os.path.isfile(header):
            raise RuntimeError("libnds video.h not found at %s" % header)

        with open(header, "r", encoding="utf-8") as handle:
            cls.header_text = handle.read()
        with open(SHIM, "r", encoding="utf-8") as handle:
            cls.shim_text = handle.read()

        cls.all_ports = gx_ports(cls.header_text)
        cls.proxied = shim_ports(cls.shim_text)

    def test_header_was_parsed(self):
        self.assertGreaterEqual(len(self.all_ports), 40)

    def test_every_write_capable_port_is_proxied(self):
        missing = []
        mismatched = []
        for name, (width, addr, is_array) in sorted(self.all_ports.items()):
            if name in READ_ONLY:
                continue
            if name not in self.proxied:
                missing.append(name)
                continue
            pwidth, paddr, parray = self.proxied[name]
            if (pwidth, paddr, parray) != (width, addr, is_array):
                mismatched.append(
                    "%s: header=(%d,0x%08X,%s) shim=(%d,0x%08X,%s)"
                    % (name, width, addr, is_array,
                       pwidth, paddr, parray))

        self.assertEqual(missing, [],
                         "write-capable ports not proxied: %s"
                         % ", ".join(missing))
        self.assertEqual(mismatched, [],
                         "proxy address/width mismatch: %s"
                         % "; ".join(mismatched))

    def test_read_only_ports_are_not_proxied(self):
        proxied_read_only = sorted(READ_ONLY & set(self.proxied))
        self.assertEqual(proxied_read_only, [],
                         "read-only ports must not be proxied: %s"
                         % ", ".join(proxied_read_only))

    def test_no_extra_proxies(self):
        expected = {name for name in self.all_ports if name not in READ_ONLY}
        extra = sorted(set(self.proxied) - expected)
        self.assertEqual(extra, [], "shim proxies unknown macros: %s"
                         % ", ".join(extra))

    def test_read_only_names_still_exist_in_header(self):
        missing = sorted(READ_ONLY - set(self.all_ports))
        self.assertEqual(missing, [],
                         "documented read-only ports not found in video.h: %s"
                         % ", ".join(missing))


if __name__ == "__main__":
    unittest.main()
