#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EQ-2/EQ-3 scene coverage for the TWL-HD equality check.

Runs the geometry comparison (EQ-2b, tools/eq2_compare.py) and, where the
colour model is valid and an Azahar picture can be produced, the structural
picture comparison (EQ-3b, tools/eq3_compare.py) for every demo scene of the
arm9v2 autodump ROM: the default cube, SCENE=1..5 and the CUBES=2 variant.

Frame alignment is the one measured in docs/EQ2_COMPARE.md section 3 and the
harness README: ROM frame 240 -> v2 recorder frame_index 241 -> DeSmuME/harness
frame 242.  A row whose harness/ours dump has a different frame or rom-sha256,
or whose dump cannot be loaded, is NOT-COMPARABLE (exit 0): the two sides are
not the same frame/ROM, so there is nothing to compare.  A polygon or vertex
count mismatch between two dumps of the same frame is a FAIL (exit 1): the
frame lines agree, so the geometry really differs.

All results are EMULATOR: the reference is DeSmuME via the out-of-tree GPL
harness, our side is the recorder's v2 stream interpreted on the host.  No
console, SD card or network action is taken.

Tolerances are never restated here: the loaded eq2_compare/eq3_compare modules
own them.  This tool only runs the commands, forwards the reports, and applies
the verdict rule.

Modes
-----
Full run (needs harness binary, desmume-cli, ROM directory, gx_interp_cli):
    tools/eq_scenes.py --harness-bin ... --desmume-cli ... --rom-dir arm9v2

Manifest (no harness/emulator needed; used by tools/test_eq_scenes.py):
    tools/eq_scenes.py --manifest tools/fixtures/eq_scenes/manifest.json

Exit codes: 0 every row PASS or NOT-COMPARABLE, 1 at least one FAIL row,
2 missing prerequisite / usage / unreadable manifest.  Stdlib only.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(TOOLS_DIR)
sys.path.insert(0, TOOLS_DIR)

import eq2_compare  # noqa: E402
import eq3_compare  # noqa: E402

# Frame alignment: docs/EQ2_COMPARE.md section 3, measured for the demo ROM and
# re-checked for every scene ROM by the harness header / our frame line.
HARNESS_FRAME = 242
OURS_FRAME = 241

CAPTURE_SCRIPT = os.path.join(TOOLS_DIR, "emulator", "desmume_capture.sh")
CAPTURE_TOOLS = ("mkfs.fat", "7z", "stdbuf")
EMPTY = "-"

WORST_RE = re.compile(
    r"^(\S+): worst (\d+) (\S+(?: px)?) "
    r"(?:tol (\d+)|reported, not gated) at poly")


class PrerequisiteError(Exception):
    """A required path or tool is missing: exit 2."""


class ManifestError(Exception):
    """The manifest is unusable: exit 2."""


class Scene(object):
    """One demo scene and the build variables that produce its ROM."""

    def __init__(self, key, build, rom, polys, verts, eq3, eq3_reason=None):
        self.key = key
        self.build = build
        self.rom = rom
        self.polys = polys
        self.verts = verts
        self.eq3 = eq3
        self.eq3_reason = eq3_reason


# arm9v2/Makefile composition order: AUTODUMP (60-75) -> CUBES (116-129) ->
# SCENE (139-152).  Names and expected counts below are the harness
# polygon/vertex totals for harness frame 242 (ROM frame 240) taken from the
# committed dump of each scene: the real-run pair for cube/scene1/scene4/scene5
# and the harness reference dumps for scene2 (harness_s2_f240.txt:8-9) and
# cubes (harness_c2_f240.txt:8-9).  scene3 has no dump and no counts (None):
# its ROM never presents a 3D frame, and tests/host/test_demo_frame.cpp:933-936
# pins only the interpreter-side 12-in/30-out/5-split, not a harness total.
SCENES = (
    Scene("cube", "AUTODUMP_FRAME=240", "twlhd-arm9v2-autodump.nds",
          6, 24, "valid"),
    Scene("scene1", "SCENE=1 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-s1.nds", 8, 24, "not-comparable",
          "per-vertex 5-bit red ramp (i+1)<<3; eq3's six cube face classes "
          "cannot model 24 distinct red shades"),
    Scene("scene2", "SCENE=2 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-s2.nds", 10, 30, "not-comparable",
          "per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] "
          "(demo_frame.cpp:237, colour loop :266-269, drawn at :283); eq3's "
          "six cube face classes cannot model 12 distinct red shades"),
    Scene("scene3", "SCENE=3 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-s3.nds", None, None, "not-comparable",
          "per-vertex 5-bit red ramp (i+1)<<3 over scene_strip_vertices[12] "
          "(demo_frame.cpp:237, colour loop :266-269, drawn at :289); eq3's "
          "six cube face classes cannot model 12 distinct red shades"),
    Scene("scene4", "SCENE=4 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-s4.nds", 4, 16, "not-comparable",
          "blended pixels need the design section 5.1 blended tolerance 16 "
          "and the alpha convention a/31 vs (a+1)/32 is UNVERIFIED; eq3 "
          "models neither blending nor the alpha-0 wireframe (design 2.3)"),
    Scene("scene5", "SCENE=5 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-s5.nds", 1, 4, "not-comparable",
          "textured scene: eq3 has no texture model and the picture "
          "comparison cannot see texture state from the geometry list"),
    Scene("cubes", "CUBES=2 AUTODUMP_FRAME=240",
          "twlhd-arm9v2-autodump-c2.nds", 12, 48, "valid"),
)

SCENES_BY_KEY = {scene.key: scene for scene in SCENES}


class RowResult(object):
    """One table row: what was compared and the verdict."""

    def __init__(self, scene, build):
        self.scene = scene
        self.build = build
        # Compared counts: set only when EQ-2 actually ran (PASS or FAIL).
        self.polys = None
        self.verts = None
        # Harness reference counts: set whenever a harness dump was parsed.
        self.ref_polys = None
        self.ref_verts = None
        self.worst = {}
        self.eq2 = "not run"
        self.eq2_detail = []
        self.eq2_not_comparable = False
        self.eq3 = "not run"
        self.eq3_detail = []
        self.verdict = "NOT-COMPARABLE"
        self.reason = ""

    def counts(self):
        if self.polys is not None and self.verts is not None:
            return "%d/%d" % (self.polys, self.verts)
        if self.ref_polys is not None and self.ref_verts is not None:
            return "0 compared (reference %d/%d)" % (self.ref_polys,
                                                     self.ref_verts)
        return EMPTY


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------

def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _first_line(text, default):
    for line in (text or "").splitlines():
        line = line.strip()
        if line:
            return line
    return default


def _executable(path, what):
    if not path:
        raise PrerequisiteError("%s is not set" % what)
    if not os.path.isfile(path):
        raise PrerequisiteError("%s does not exist: %s" % (what, path))
    if not os.access(path, os.X_OK):
        raise PrerequisiteError("%s is not executable: %s" % (what, path))
    return path


# --------------------------------------------------------------------------
# Manifest mode
# --------------------------------------------------------------------------

def load_manifest(path):
    """Return the row dicts of a JSON manifest, resolving relative paths."""
    if not os.path.isfile(path):
        raise ManifestError("manifest does not exist: %s" % path)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError) as exc:
        raise ManifestError("cannot read manifest %s: %s" % (path, exc))
    if not isinstance(data, dict) or not isinstance(data.get("rows"), list):
        raise ManifestError("manifest %s has no rows list" % path)
    if not data["rows"]:
        raise ManifestError("manifest %s has an empty rows list" % path)

    base = os.path.dirname(os.path.abspath(path))
    rows = []
    for index, raw in enumerate(data["rows"]):
        if not isinstance(raw, dict):
            raise ManifestError("manifest row %d is not an object" % index)
        row = dict(raw)
        row.setdefault("scene", "row%d" % index)
        row.setdefault("build", "manifest")
        for key in ("harness", "ours", "eq3_harness", "eq3_ours"):
            value = row.get(key)
            if value and not os.path.isabs(value):
                row[key] = os.path.normpath(os.path.join(base, value))
        rows.append(row)
    return rows


def check_manifest_prerequisites(rows):
    for row in rows:
        for key in ("harness", "ours", "eq3_harness", "eq3_ours"):
            path = row.get(key)
            if path and not os.path.isfile(path):
                raise PrerequisiteError(
                    "%s: %s does not exist: %s"
                    % (row["scene"], key.replace("_", " "), path))


# --------------------------------------------------------------------------
# Full-run mode
# --------------------------------------------------------------------------

def check_full_prerequisites(args):
    issues = []
    try:
        _executable(args.harness_bin, "harness binary (--harness-bin / "
                                     "TWLHD_HARNESS_BIN)")
    except PrerequisiteError as exc:
        issues.append(str(exc))
    try:
        _executable(args.desmume_cli, "DeSmuME CLI (--desmume-cli / "
                                     "DESMUME_CLI)")
    except PrerequisiteError as exc:
        issues.append(str(exc))
    try:
        _executable(args.gx_interp_cli, "gx_interp_cli (--gx-interp-cli)")
    except PrerequisiteError as exc:
        issues.append(str(exc))
    if not args.rom_dir or not os.path.isdir(args.rom_dir):
        issues.append("ROM directory does not exist: %s" % args.rom_dir)
    else:
        for scene in SCENES:
            path = os.path.join(args.rom_dir, scene.rom)
            if not os.path.isfile(path):
                issues.append("scene %s ROM does not exist: %s"
                              % (scene.key, path))
    if not os.path.isfile(CAPTURE_SCRIPT):
        issues.append("capture script does not exist: %s" % CAPTURE_SCRIPT)
    for tool in CAPTURE_TOOLS:
        if shutil.which(tool) is None:
            issues.append("required tool not found: %s" % tool)

    azahar = (args.azahar_host, args.azahar_core, args.arm11_3dsx)
    if any(azahar) and not all(azahar):
        issues.append("--azahar-host, --azahar-core and --arm11-3dsx must be "
                      "given together")
    for path, what in (
            (args.azahar_host, "Azahar host"),
            (args.azahar_core, "Azahar libretro core"),
            (args.arm11_3dsx, "ARM11 3dsx")):
        if path and not os.path.isfile(path):
            issues.append("%s does not exist: %s" % (what, path))
    if args.azahar_host and not os.access(args.azahar_host, os.X_OK):
        issues.append("Azahar host is not executable: %s" % args.azahar_host)

    if issues:
        raise PrerequisiteError("; ".join(issues))


def _run(command, env=None, cwd=None):
    return subprocess.run(command, capture_output=True, text=True, env=env,
                          cwd=cwd)


def _capture_failure_text(log_path):
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
            tail = [line.strip() for line in handle if line.strip()]
    except OSError:
        tail = []
    interesting = [line for line in tail
                   if line.startswith(("error:", "harness:", "ROM "))]
    return interesting[-1] if interesting else (tail[-1] if tail else "unknown")


def _prepare_harness(args, scene, outdir):
    """Run the harness for harness frame 242. Returns (path, row problem)."""
    harness_path = os.path.join(outdir, "harness.txt")
    ppm_path = os.path.join(outdir, "harness.ppm")
    result = _run([args.harness_bin,
                   os.path.join(args.rom_dir, scene.rom),
                   str(HARNESS_FRAME),
                   "--out", harness_path,
                   "--ppm", ppm_path])
    if result.returncode != 0:
        detail = _first_line(result.stderr, "exit %d" % result.returncode)
        return None, None, "harness failed: %s" % detail
    return harness_path, ppm_path, None


def _prepare_capture(args, scene, outdir):
    """Run the documented DeSmuME capture. Returns (v2 path, problem).

    For scene2, scene3 and CUBES=2 the capture aborts with a guest
    ``ARM OP_BKPT triggered`` before the autodump frame, so no v2 stream is
    written.  It is not a recorder/demo_frame assert or size limit: arm9v2/ and
    common/ contain no runtime assert or BKPT (only compile-time
    ``_Static_assert``) and the recorder capacities
    (common/twlhd_gx_record.h:30,41-45) are far from full.  The faulting PC is
    not measurable here (the installed desmume-cli has no gdb stub), so the
    cause stays UNRESOLVED and is not attributed to DeSmuME; docs/EQ_SCENES.md
    section 3 lists what was measured.  Retrying the same documented script is
    the only workaround; a failure after every attempt stays a NOT-COMPARABLE
    row.
    """
    last_problem = "DeSmuME capture failed"
    attempts = max(1, args.capture_retries)
    for attempt in range(attempts):
        cap_dir = os.path.join(outdir, "capture-%d" % attempt)
        env = dict(os.environ)
        env["DESMUME_CLI"] = args.desmume_cli
        env["DESMUME_TIMEOUT"] = str(args.desmume_timeout)
        result = _run([CAPTURE_SCRIPT,
                       os.path.join(args.rom_dir, scene.rom),
                       cap_dir], env=env)
        v2 = os.path.join(cap_dir, "twlhd_frame_v2.bin")
        if result.returncode == 0 and os.path.isfile(v2):
            return v2, None
        last_problem = ("DeSmuME capture failed (attempt %d/%d): %s"
                        % (attempt + 1, attempts,
                           _capture_failure_text(os.path.join(cap_dir,
                                                              "desmume.log"))))
    return None, last_problem


def _prepare_ours(args, v2_path, outdir):
    ours_path = os.path.join(outdir, "ours.txt")
    result = _run([args.gx_interp_cli, "--eq2", v2_path])
    if result.returncode != 0:
        return None, ("gx_interp_cli --eq2 failed: %s"
                      % _first_line(result.stderr,
                                    "exit %d" % result.returncode))
    with open(ours_path, "w", encoding="utf-8") as handle:
        handle.write(result.stdout)
    return ours_path, None


def _prepare_azahar(args, v2_path, outdir):
    """Run the Azahar host on the captured frame. Returns (ppm path, problem)."""
    sd_dir = os.path.join(outdir, "sd")
    os.makedirs(sd_dir, exist_ok=True)
    shutil.copyfile(v2_path, os.path.join(sd_dir, "twlhd_frame.bin"))
    prefix = os.path.join(outdir, "azahar")
    ppm = prefix + "_199.ppm"
    result = _run([args.azahar_host,
                   "--core", args.azahar_core,
                   "--game", args.arm11_3dsx,
                   "--frames", "200",
                   "--press", "A:60-63",
                   "--shot", "199",
                   "--out", prefix,
                   "--sd-root", sd_dir])
    if result.returncode != 0 or not os.path.isfile(ppm):
        return None, ("Azahar host failed: %s"
                      % _first_line(result.stderr,
                                    "exit %d" % result.returncode))
    return ppm, None


def prepare_full_row(args, scene):
    """Run the per-scene commands and return the row dict for process_row."""
    outdir = os.path.join(args.work_dir, scene.key)
    os.makedirs(outdir, exist_ok=True)
    row = {
        "scene": scene.key,
        "build": scene.build,
        "rom": os.path.join(args.rom_dir, scene.rom),
        "expected_polys": scene.polys,
        "expected_verts": scene.verts,
        "eq3": scene.eq3,
        "eq3_reason": scene.eq3_reason,
        "azahar_correction": args.azahar_correction,
    }

    harness, harness_ppm, problem = _prepare_harness(args, scene, outdir)
    if harness:
        row["harness"] = harness
        row["eq3_harness"] = harness_ppm
    if problem:
        row["not_comparable"] = problem
        return row

    v2, problem = _prepare_capture(args, scene, outdir)
    if problem:
        row["not_comparable"] = problem
        return row
    row["v2"] = v2

    ours, problem = _prepare_ours(args, v2, outdir)
    if problem:
        row["not_comparable"] = problem
        return row
    row["ours"] = ours

    # Bind the harness reference to the scene ROM it was asked to run.
    try:
        harness_header = eq2_compare.load_harness(harness)
    except (eq2_compare.FormatError, eq2_compare.CountError) as exc:
        row["not_comparable"] = "harness dump is unusable: %s" % exc
        return row
    row["harness_sha256"] = harness_header.get("rom-sha256")
    row["rom_sha256"] = _sha256(row["rom"])

    if scene.eq3 == "valid" and args.azahar_host:
        ppm, problem = _prepare_azahar(args, v2, outdir)
        if problem:
            row["eq3_reason"] = problem
        else:
            row["eq3_ours"] = ppm
    elif scene.eq3 == "valid" and scene.key == "cube":
        # The committed EQ-3b pair is the same cube frame/ROM (docs/EQ3_COMPARE
        # section 6); use it as the baseline when no Azahar run is configured.
        row["eq3_harness"] = os.path.join(TOOLS_DIR, "fixtures", "eq3",
                                          "harness_cube_f240.ppm")
        row["eq3_ours"] = os.path.join(TOOLS_DIR, "fixtures", "eq3",
                                       "ours_cube_f240_top.ppm")
    elif scene.eq3 == "valid":
        row["eq3_reason"] = ("our-side picture not produced (no Azahar host "
                             "configured)")
    return row


def select_scenes(keys):
    if not keys:
        return list(SCENES)
    unknown = [key for key in keys if key not in SCENES_BY_KEY]
    if unknown:
        raise ManifestError("unknown scene(s): %s (known: %s)"
                            % (", ".join(unknown), ", ".join(SCENES_BY_KEY)))
    return [SCENES_BY_KEY[key] for key in keys]


# --------------------------------------------------------------------------
# Comparison of one row (shared by both modes)
# --------------------------------------------------------------------------

def _parse_worst(report):
    worst = {}
    for line in report:
        match = WORST_RE.match(line)
        if not match:
            continue
        field, value = match.group(1), int(match.group(2))
        worst[field] = value
    return worst


def _first_mismatch(report):
    for line in report:
        if line.startswith("MISMATCH"):
            return line
    return None


def _format_worst(worst, fields):
    values = [worst[field] for field in fields if field in worst]
    if not values:
        return EMPTY
    return str(max(values))


def compare_eq2(row, result):
    """Run EQ-2 for one row into result. Returns an error string or None.

    Sets result.eq2_not_comparable for the cases where there is nothing to
    compare (missing dump, unloadable dump, different frame, different
    rom-sha256): those are NOT-COMPARABLE, exit 0.  A polygon/vertex count
    mismatch between two dumps of the same frame is left as a real FAIL.
    """
    harness_path = row.get("harness")
    ours_path = row.get("ours")
    if not harness_path:
        result.eq2_not_comparable = True
        result.reason = row.get("not_comparable") or "no harness dump"
        return result.reason
    try:
        harness = eq2_compare.load_harness(harness_path)
    except (eq2_compare.FormatError, eq2_compare.CountError) as exc:
        result.eq2_not_comparable = True
        result.reason = "harness dump is unusable: %s" % exc
        return result.reason

    result.ref_polys = len(harness["polys"])
    result.ref_verts = sum(poly["verts"] for poly in harness["polys"])

    if not ours_path:
        result.eq2_not_comparable = True
        result.reason = row.get("not_comparable") or "no our-side dump"
        return result.reason

    # Frame binding: the same arithmetic for every scene ROM (docs/EQ2_COMPARE
    # section 3).  A different captured frame is NOT-COMPARABLE, never a pass.
    if (harness["requested_frame"] != HARNESS_FRAME
            or harness["captured_frame"] != HARNESS_FRAME):
        result.eq2_not_comparable = True
        result.reason = ("frame alignment: harness has requested %d captured "
                         "%d, expected %d (ROM frame 240 + 2)"
                         % (harness["requested_frame"],
                            harness["captured_frame"], HARNESS_FRAME))
        return result.reason
    try:
        ours = eq2_compare.load_ours(ours_path)
    except (eq2_compare.FormatError, eq2_compare.CountError) as exc:
        result.eq2_not_comparable = True
        result.reason = "our-side dump is unusable: %s" % exc
        return result.reason
    if ours["frame"] != OURS_FRAME:
        result.eq2_not_comparable = True
        result.reason = ("frame alignment: our recorder frame is %d, expected "
                         "%d (ROM frame 240 + 1)" % (ours["frame"],
                                                    OURS_FRAME))
        return result.reason

    # The harness must have dumped the ROM this row asked for.  Manifest rows
    # may omit the ROM hash; a present hash always wins.
    expected_sha = row.get("rom_sha256")
    harness_sha = harness.get("rom-sha256")
    if expected_sha and harness_sha and expected_sha.lower() != \
            harness_sha.lower():
        result.eq2_not_comparable = True
        result.reason = ("harness rom-sha256 %s does not match scene ROM %s"
                         % (harness_sha, expected_sha))
        return result.reason

    expected_polys = row.get("expected_polys")
    expected_verts = row.get("expected_verts")
    if expected_polys is not None and expected_polys != result.ref_polys:
        result.reason = ("harness dumped %d polygons, expected %d for this "
                         "scene" % (result.ref_polys, expected_polys))
        return result.reason
    if expected_verts is not None and expected_verts != result.ref_verts:
        result.reason = ("harness dumped %d vertices, expected %d for this "
                         "scene" % (result.ref_verts, expected_verts))
        return result.reason

    # Same frame and ROM, counts agree: EQ-2 compares geometry from here on.
    result.polys = result.ref_polys
    result.verts = result.ref_verts
    try:
        code, report = eq2_compare.compare(harness, ours)
    except eq2_compare.FormatError as exc:
        result.eq2_not_comparable = True
        result.reason = "EQ-2 comparison error: %s" % exc
        return result.reason
    except eq2_compare.CountError as exc:
        result.reason = "EQ-2 count mismatch: %s" % exc
        return result.reason
    result.worst = _parse_worst(report)
    result.eq2_detail = report
    result.eq2 = "PASS" if code == 0 else "FAIL"
    if code == 0:
        return None
    result.reason = _first_mismatch(report) or "EQ-2 mismatch (see detail)"
    return result.reason


def compare_eq3(row, result):
    """Run EQ-3 when both pictures exist; otherwise record the reason."""
    harness = row.get("eq3_harness")
    ours = row.get("eq3_ours")
    if not harness or not ours:
        result.eq3 = "NOT-COMPARABLE"
        result.eq3_detail = [row.get("eq3_reason") or "picture pair not "
                             "produced"]
        return
    try:
        hpic = eq3_compare.load_picture(harness, "harness")
        opic = eq3_compare.load_picture(ours, "ours",
                                        row.get("azahar_correction", "auto"))
        print(opic["correction_line"])
        code, report = eq3_compare.compare(hpic, opic)
    except (eq3_compare.FormatError, OSError) as exc:
        result.eq3 = "NOT-COMPARABLE"
        result.eq3_detail = ["picture comparison unusable: %s" % exc]
        return
    result.eq3 = "PASS" if code == 0 else "FAIL"
    result.eq3_detail = report
    if code == 0:
        for line in report:
            if line.startswith("PASS"):
                match = re.search(r"worst (\S+)", line)
                worst = match.group(1) if match else ""
                result.eq3 = ("PASS " + worst).strip()
                break
    else:
        first = _first_mismatch(report) or "EQ-3 mismatch"
        result.eq3 = "FAIL"
        result.eq3_detail = report
        result.reason = (result.reason + "; " if result.reason else "") + first


def process_row(row):
    """Compare one row and return its RowResult."""
    result = RowResult(row.get("scene", "row"), row.get("build", "manifest"))

    # A row that is known not to be comparable still reports the harness
    # reference counts when a dump exists, so the table never hides how much
    # was available.
    if row.get("not_comparable") and not row.get("ours"):
        if row.get("harness") and row.get("harness") != "":
            try:
                harness = eq2_compare.load_harness(row["harness"])
                result.ref_polys = len(harness["polys"])
                result.ref_verts = sum(poly["verts"]
                                       for poly in harness["polys"])
            except (eq2_compare.FormatError, eq2_compare.CountError):
                pass
        result.reason = row["not_comparable"]
        result.eq3 = "NOT-COMPARABLE"
        result.eq3_detail = [row.get("eq3_reason") or row["not_comparable"]]
        return result

    error = compare_eq2(row, result)
    if error is not None:
        if result.eq2_not_comparable:
            result.eq2 = "NOT-COMPARABLE"
            result.verdict = "NOT-COMPARABLE"
        else:
            result.eq2 = "FAIL"
            result.verdict = "FAIL"
        compare_eq3(row, result)
        return result

    compare_eq3(row, result)
    if result.eq2 == "FAIL" or result.eq3.startswith("FAIL"):
        result.verdict = "FAIL"
    else:
        result.verdict = "PASS"
    return result


# --------------------------------------------------------------------------
# Aggregation and output
# --------------------------------------------------------------------------

def select_rows(rows):
    """The per-scene loop.  A module function so a mutant can drop rows."""
    return list(rows)


def process_rows(rows):
    return [process_row(row) for row in select_rows(rows)]


def aggregate(results):
    """Exit code: 1 if any row FAILed, else 0 (NOT-COMPARABLE is not a fail)."""
    if any(result.verdict == "FAIL" for result in results):
        return 1
    return 0


def _px(value):
    return "%.4f px" % (value / 65536.0)


def format_table(results):
    header = ("scene    build                        polys/verts  "
              "x          y          z(0.31)  w   rgb  tex  EQ-3  verdict")
    lines = ["EQ-2/EQ-3 scene coverage (EMULATOR; frame 240 -> recorder 241 -> "
             "harness 242)", header]
    for result in results:
        worst = result.worst
        eq3 = result.eq3.split(" (", 1)[0]
        lines.append("%-8s %-28s %-12s %-10s %-10s %-8s %-3s %-3s %-4s %-5s %s"
                     % (result.scene, result.build, result.counts(),
                        _px(worst["x"]) if "x" in worst else EMPTY,
                        _px(worst["y"]) if "y" in worst else EMPTY,
                        result.worst.get("z", EMPTY),
                        result.worst.get("w", EMPTY),
                        _format_worst(worst, ("r", "g", "b")),
                        _format_worst(worst, ("tex-s", "tex-t")),
                        eq3, result.verdict))
    return "\n".join(lines)


def format_details(results):
    lines = []
    for result in results:
        if result.verdict == "PASS" and result.eq3.startswith("PASS"):
            continue
        lines.append("")
        lines.append("%s: %s" % (result.scene, result.verdict))
        if result.reason:
            lines.append("  reason: %s" % result.reason)
        if result.eq2 == "FAIL" and result.eq2_detail:
            lines.append("  EQ-2 report:")
            lines.extend("    " + line for line in result.eq2_detail)
        if result.eq3_detail:
            lines.append("  EQ-3 report:")
            lines.extend("    " + line for line in result.eq3_detail)
    return "\n".join(lines)


def print_report(results):
    print(format_table(results))
    details = format_details(results)
    if details:
        print(details)
    fails = sum(1 for result in results if result.verdict == "FAIL")
    not_comparable = sum(1 for result in results
                         if result.verdict == "NOT-COMPARABLE")
    print("")
    print("summary: %d rows, %d PASS, %d FAIL, %d NOT-COMPARABLE"
          % (len(results), len(results) - fails - not_comparable, fails,
             not_comparable))


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser():
    parser = argparse.ArgumentParser(
        description="Run EQ-2/EQ-3 for every arm9v2 demo scene (EMULATOR)."
                    " Full mode needs the harness, DeSmuME, the ROMs and "
                    "gx_interp_cli; --manifest replays pre-existing dumps.")
    parser.add_argument("--manifest",
                        help="JSON manifest of pre-existing harness/ours/picture "
                             "paths (no emulator needed)")
    parser.add_argument("--harness-bin",
                        default=os.environ.get("TWLHD_HARNESS_BIN"),
                        help="twlhd-desmume-harness binary "
                             "(env TWLHD_HARNESS_BIN)")
    parser.add_argument("--desmume-cli",
                        default=os.environ.get("DESMUME_CLI"),
                        help="desmume-cli binary (env DESMUME_CLI)")
    parser.add_argument("--rom-dir",
                        default=os.environ.get("TWLHD_ROM_DIR"),
                        help="directory with the seven scene ROMs "
                             "(env TWLHD_ROM_DIR)")
    parser.add_argument("--work-dir",
                        default=os.environ.get("TWLHD_EQ_WORK_DIR"),
                        help="scratch directory for ROMs/captures/PPMs "
                             "(default: a fresh temporary directory)")
    parser.add_argument("--gx-interp-cli",
                        default=os.environ.get(
                            "TWLHD_GX_INTERP_CLI",
                            os.path.join(REPO_DIR, "tests", "host", "build",
                                         "gx_interp_cli")),
                        help="host gx_interp_cli binary")
    parser.add_argument("--desmume-timeout", type=int,
                        default=int(os.environ.get("TWLHD_DESMUME_TIMEOUT",
                                                   "120")),
                        help="DESMUME_TIMEOUT seconds per capture attempt "
                             "(default 120, env TWLHD_DESMUME_TIMEOUT)")
    parser.add_argument("--capture-retries", type=int,
                        default=int(os.environ.get("TWLHD_CAPTURE_RETRIES",
                                                   "1")),
                        help="capture attempts per scene (default 1, env "
                             "TWLHD_CAPTURE_RETRIES)")
    parser.add_argument("--azahar-host",
                        default=os.environ.get("TWLHD_AZAHAR_HOST"),
                        help="tools/azahar_host binary for EQ-3 pictures")
    parser.add_argument("--azahar-core",
                        default=os.environ.get("AZAHAR_LIBRETRO"),
                        help="Azahar libretro core (env AZAHAR_LIBRETRO)")
    parser.add_argument("--arm11-3dsx",
                        default=os.environ.get("TWLHD_ARM11_3DSX"),
                        help="arm11/twlhd-arm11.3dsx for EQ-3 pictures")
    parser.add_argument("--scenes",
                        help="comma-separated subset, e.g. cube,scene5")
    parser.add_argument("--azahar-correction",
                        choices=("auto", "on", "off"), default="on",
                        help="EQ-4p presentation-row correction for the "
                             "Azahar 'ours' picture: on (forced, default; "
                             "skips already-corrected marker), auto (row-0 "
                             "wrap guard; uniform border rows are reported as "
                             "undetermined), off (never)")
    return parser


def run_manifest(path, correction="auto"):
    rows = load_manifest(path)
    for row in rows:
        row.setdefault("azahar_correction", correction)
    check_manifest_prerequisites(rows)
    return process_rows(rows)


def run_full(args):
    check_full_prerequisites(args)
    if not args.work_dir:
        args.work_dir = tempfile.mkdtemp(prefix="eq-scenes-")
    os.makedirs(args.work_dir, exist_ok=True)
    scenes = select_scenes([key.strip() for key in args.scenes.split(",")]
                           if args.scenes else None)
    rows = [prepare_full_row(args, scene) for scene in scenes]
    return process_rows(rows)


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.manifest:
            results = run_manifest(args.manifest, args.azahar_correction)
        else:
            results = run_full(args)
    except (PrerequisiteError, ManifestError) as exc:
        print("eq_scenes: error: %s" % exc, file=sys.stderr)
        return 2
    print_report(results)
    return aggregate(results)


if __name__ == "__main__":
    sys.exit(main())
