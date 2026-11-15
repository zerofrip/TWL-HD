#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fake-emulator end-to-end test for the `emu-validate-tex` script path.

No real DeSmuME is invoked: the fake DESMUME_CLI parses `--cflash-image`,
injects a synthetic twlhd_frame_v2.bin (positive or corrupted) into the FAT
image that tools/emulator/desmume_capture.sh created with mkfs.fat, prints the
two device lines and exits 0 on SIGINT so the script's flush path runs.

The injector is pure stdlib and BPB-driven (no mtools); the script's own
`7z x` extraction cross-checks it (the copied capture must equal what we
injected).

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
EMULATOR_DIR = os.path.join(TOOLS_DIR, "emulator")
sys.path.insert(0, TOOLS_DIR)
sys.path.insert(0, EMULATOR_DIR)

import compare_texture_capture as tex  # noqa: E402
import test_emulator_texture_compare as texture_tests  # noqa: E402

CAPTURE_SCRIPT = os.path.join(EMULATOR_DIR, "desmume_capture.sh")
VALIDATE_SCRIPT = os.path.join(EMULATOR_DIR, "validate_with_parsers.sh")
COMPARE = os.path.join(EMULATOR_DIR, "compare_texture_capture.py")

SLOT2_LINE = "Slot 2: MPCF Flash Card Device"
DLDI_LINE = "AUTO-PATCHING DLDI"

FAKE_BODY = '''#!/usr/bin/env python3
import os
import signal
import sys
import time

image = None
args = sys.argv[1:]
for index, arg in enumerate(args):
    if arg == "--cflash-image" and index + 1 < len(args):
        image = args[index + 1]

sys.stdout.write("%(slot2)s\\n")
sys.stdout.write("%(dldi)s\\n")
sys.stdout.flush()
with open(os.environ["TWLHD_FAKE_PID_FILE"], "w", encoding="utf-8") as handle:
    handle.write(str(os.getpid()))

sys.path.insert(0, %(tools)r)
from test_emulator_texture_run import inject_file

with open(os.environ["TWLHD_FAKE_CAPTURE"], "rb") as handle:
    capture = handle.read()
inject_file(image, "twlhd_frame_v2.bin", capture)


def stop(signum, frame):
    sys.exit(0)


signal.signal(signal.SIGINT, stop)
while True:
    time.sleep(0.05)
''' % {"slot2": SLOT2_LINE, "dldi": DLDI_LINE, "tools": TOOLS_DIR}


def _lfn_entries(long_name, short_name_bytes):
    """Long-file-name directory entries for one short entry (FAT spec 6.4)."""
    checksum = 0
    for byte in short_name_bytes:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 0xFF
    raw = long_name.encode("utf-16-le")
    units = [raw[index:index + 2] for index in range(0, len(raw), 2)]
    units.append(b"\x00\x00")
    while len(units) % 13:
        units.append(b"\xff\xff")
    count = len(units) // 13
    entries = []
    for index in range(count - 1, -1, -1):
        part = units[index * 13:(index + 1) * 13]
        sequence = index + 1
        if index == count - 1:
            sequence |= 0x40
        entry = bytearray(32)
        entry[0] = sequence
        entry[1:11] = b"".join(part[0:5])
        entry[11] = 0x0F
        entry[13] = checksum
        entry[14:26] = b"".join(part[5:11])
        entry[28:32] = b"".join(part[11:13])
        entries.append(bytes(entry))
    return b"".join(entries)


def inject_file(image_path, filename, data):
    """Write *data* as *filename* into a FAT12/16 image, driven by its BPB."""
    with open(image_path, "r+b") as handle:
        image = bytearray(handle.read())
        bytes_per_sector = struct.unpack_from("<H", image, 11)[0]
        sectors_per_cluster = image[13]
        reserved = struct.unpack_from("<H", image, 14)[0]
        fat_count = image[16]
        root_entries = struct.unpack_from("<H", image, 17)[0]
        fat_sectors = struct.unpack_from("<H", image, 22)[0]
        if fat_sectors == 0:
            fat_sectors = struct.unpack_from("<I", image, 36)[0]

        cluster_size = bytes_per_sector * sectors_per_cluster
        fat_start = reserved * bytes_per_sector
        root_start = (reserved + fat_count * fat_sectors) * bytes_per_sector
        data_start = root_start + root_entries * 32
        total_clusters = (len(image) - data_start) // cluster_size

        needed = max(1, (len(data) + cluster_size - 1) // cluster_size)
        fat = list(struct.unpack_from(
            "<%dH" % (fat_sectors * bytes_per_sector // 2), image, fat_start))
        free = [index + 2 for index in range(total_clusters)
                if fat[index + 2] == 0]
        if len(free) < needed:
            raise RuntimeError("FAT image has no free cluster chain")
        chosen = free[:needed]
        for index, cluster in enumerate(chosen):
            fat[cluster] = (chosen[index + 1] if index + 1 < len(chosen)
                            else 0xFFFF)
        fat_bytes = struct.pack("<%dH" % len(fat), *fat)
        for fat_index in range(fat_count):
            offset = fat_start + fat_index * fat_sectors * bytes_per_sector
            image[offset:offset + len(fat_bytes)] = fat_bytes

        for index, cluster in enumerate(chosen):
            block = data[index * cluster_size:(index + 1) * cluster_size]
            offset = data_start + (cluster - 2) * cluster_size
            image[offset:offset + cluster_size] = (
                block + b"\x00" * (cluster_size - len(block)))

        short = b"TWLHDFR1BIN"
        long_entries = _lfn_entries(filename, short)
        long_slots = len(long_entries) // 32
        start = None
        for slot in range(root_entries - long_slots):
            if all(image[root_start + (slot + step) * 32] in (0x00, 0xE5)
                   for step in range(long_slots + 1)):
                start = slot
                break
        if start is None:
            raise RuntimeError("FAT root directory is full")
        image[root_start + start * 32:
              root_start + start * 32 + len(long_entries)] = long_entries

        entry = bytearray(32)
        entry[0:11] = short
        entry[11] = 0x20
        struct.pack_into("<H", entry, 26, chosen[0])
        struct.pack_into("<I", entry, 28, len(data))
        offset = root_start + (start + long_slots) * 32
        image[offset:offset + 32] = bytes(entry)

        handle.seek(0)
        handle.write(image)


class TextureRunTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="twlhd-tex-run-")
        self.addCleanup(shutil.rmtree, self.tmpdir)

        missing = [tool for tool in ("bash", "mkfs.fat", "7z", "stdbuf")
                   if shutil.which(tool) is None]
        if missing:
            self.fail("required tool(s) not found: %s" % ", ".join(missing))
        if shutil.which(os.environ.get("CC", "cc")) is None:
            self.fail("host C compiler not found; validate_with_parsers.sh "
                      "cannot run")

        self.rom = os.path.join(self.tmpdir, "fake.nds")
        open(self.rom, "w", encoding="utf-8").close()
        self.pidfile = os.path.join(self.tmpdir, "fake.pid")
        self.fake_cli = os.path.join(self.tmpdir, "desmume-fake")
        with open(self.fake_cli, "w", encoding="utf-8") as handle:
            handle.write(FAKE_BODY)
        os.chmod(self.fake_cli,
                 stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

    def run_capture(self, capture, outdir):
        env = os.environ.copy()
        env["DESMUME_TIMEOUT"] = "2"
        env["DESMUME_INT_GRACE"] = "2"
        env["DESMUME_CLI"] = self.fake_cli
        env["TWLHD_FAKE_PID_FILE"] = self.pidfile
        capture_path = os.path.join(self.tmpdir, "capture-source.bin")
        with open(capture_path, "wb") as handle:
            handle.write(capture)
        env["TWLHD_FAKE_CAPTURE"] = capture_path
        return subprocess.run(
            ["bash", CAPTURE_SCRIPT, self.rom, outdir], env=env,
            capture_output=True, text=True, timeout=180)

    def run_pipeline(self, capture):
        outdir = os.path.join(self.tmpdir, "out")
        completed = self.run_capture(capture, outdir)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        captured = os.path.join(outdir, "twlhd_frame_v2.bin")
        self.assertTrue(os.path.isfile(captured))
        with open(captured, "rb") as handle:
            self.assertEqual(handle.read(), capture,
                             "the 7z extraction disagrees with the injector")
        validated = subprocess.run(
            ["bash", VALIDATE_SCRIPT, captured], cwd=ROOT,
            capture_output=True, text=True, timeout=180)
        compared = subprocess.run(
            [sys.executable, "-B", "-I", COMPARE, captured], cwd=ROOT,
            capture_output=True, text=True, timeout=120)
        return validated, compared

    def test_positive_pipeline_passes(self):
        validated, compared = self.run_pipeline(texture_tests.texture_file())
        self.assertEqual(validated.returncode, 0, validated.stderr)
        self.assertIn("parsers: OK", validated.stdout)
        self.assertEqual(compared.returncode, 0, compared.stdout)
        self.assertIn("PASS", compared.stdout)

    def test_corrupted_capture_fails_at_the_comparator(self):
        corrupted = texture_tests.texture_file(
            txdt=bytes([tex.TEXEL[0] ^ 0xFF]) + tex.TEXEL[1:])
        validated, compared = self.run_pipeline(corrupted)
        self.assertEqual(validated.returncode, 0, validated.stderr)
        self.assertIn("parsers: OK", validated.stdout)
        self.assertEqual(compared.returncode, 1, compared.stdout)
        self.assertIn("FAIL", compared.stdout)
        self.assertIn("- txdt-bytes:", compared.stdout)
        self.assertIn("byte 0", compared.stdout)


if __name__ == "__main__":
    unittest.main()
