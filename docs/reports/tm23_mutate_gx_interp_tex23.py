#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for the Phase 5B-2b follow-up texture modes 2/3
(common/twlhd_gx_interp.c).

Standalone, from the repository root:

    python3 docs/reports/tm23_mutate_gx_interp_tex23.py --jobs 4

It is not part of `make test`.

Scope: only the NEW modes-2/3 code is mutated. Mutant text is generated from
the lines that contain one of `NEW_MARKERS` (the normal/vertex latches, the
`compute_tex_vertex` mode-2/3 branches, the raw-vertex plumbing in the VTX
handlers and `emit_vertex`). The generic pass swaps comparison/logical
operators and adds +/-1 to every numeric constant on those lines; that also
mutates the array indices (`[0]`/`[1]`...) and the shift amounts. A pinned
targeted list additionally fixes the exact `16777216` (s<<24) constant, the
`>> 24` shift and the mode mask, because the generic pass never rewrites `<<`
and `>>` themselves.

Mechanism: the mutant text is written over common/twlhd_gx_interp.c, the host
test is rebuilt and run. A survivor is additionally checked against the
Python golden suite (rebuilt CLI). The original file is restored in
`finally:`. Parallel workers each get a private repository copy under
/tmp/opencode.
"""

import argparse
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "common" / "twlhd_gx_interp.c"
TEST_BIN = ROOT / "tests" / "host" / "build" / "test_gx_interp"
CLI_BIN = ROOT / "tests" / "host" / "build" / "gx_interp_cli"

NUM_RE = re.compile(
    r"(?<![A-Za-z0-9_.])"
    r"(0[xX][0-9a-fA-F]+|\d+\.\d*(?:[eE][+-]?\d+)?|\d+(?:[eE][+-]?\d+)?)"
    r"([uUlLfF]*)(?![A-Za-z0-9_])"
)
OP_RE = re.compile(r"==|!=|<=|>=|&&|\|\||<<|>>|->|[<>]")
OP_SWAP = {
    "==": "!=",
    "!=": "==",
    "<": "<=",
    "<=": "<",
    ">": ">=",
    ">=": ">",
    "&&": "||",
    "||": "&&",
}

# Lines that belong to the NEW modes-2/3 code paths.
NEW_MARKERS = (
    "uint32_t tmode = (g->teximage_param >> 30) & 0x3u;",
    "if (tmode == 2u) {",
    "if (tmode == 3u) {",
    "g->tex.m[0] * g->normal[0]",
    "g->tex.m[4] * g->normal[1]",
    "g->tex.m[8] * g->normal[2]",
    "g->tex.m[1] * g->normal[0]",
    "g->tex.m[5] * g->normal[1]",
    "g->tex.m[9] * g->normal[2]",
    "(int64_t)g->tex_s * 16777216",
    "(int64_t)g->tex_t * 16777216",
    "s_acc >> 24",
    "t_acc >> 24",
    "g->tex.m[0] * g->raw_vtx[0]",
    "g->tex.m[4] * g->raw_vtx[1]",
    "g->tex.m[8] * g->raw_vtx[2]",
    "g->tex.m[1] * g->raw_vtx[0]",
    "g->tex.m[5] * g->raw_vtx[1]",
    "g->tex.m[9] * g->raw_vtx[2]",
    "g->normal[0] = n[0];",
    "g->normal[1] = n[1];",
    "g->normal[2] = n[2];",
    "g->raw_vtx[0] = rx;",
    "g->raw_vtx[1] = ry;",
    "g->raw_vtx[2] = rz;",
    "sign_extend10(params[0]) * 64",
    "sign_extend10(params[0] >> 10) * 64",
    "sign_extend10(params[0] >> 20) * 64",
    "(int32_t)sign_extend16(params[0]),",
    "(int32_t)sign_extend16(params[0] >> 16),",
    "(int32_t)sign_extend16(params[1]));",
)

# (needle, replacement, description). Exactly one occurrence is changed.
TARGETED = [
    ("* 16777216", "* 8388608", "mode 2/3 base shift 24 -> 23"),
    ("* 16777216", "* 33554432", "mode 2/3 base shift 24 -> 25"),
    ("(s_acc >> 24) / 16.0f", "(s_acc >> 20) / 16.0f",
     "mode 2 sum shift 24 -> 20"),
    ("(s_acc >> 24) / 16.0f", "(s_acc >> 28) / 16.0f",
     "mode 2 sum shift 24 -> 28"),
    ("(t_acc >> 24) / 16.0f", "(t_acc >> 20) / 16.0f",
     "mode 2/3 t sum shift 24 -> 20"),
    ("(t_acc >> 24) / 16.0f", "(t_acc >> 28) / 16.0f",
     "mode 2/3 t sum shift 24 -> 28"),
    ("(s_acc >> 24) / 16.0f", "(s_acc >> 24) / 8.0f",
     "mode 2/3 output scale 16 -> 8"),
    ("(t_acc >> 24) / 16.0f", "(t_acc >> 24) / 32.0f",
     "mode 2/3 output scale 16 -> 32"),
    ("(g->teximage_param >> 30) & 0x3u", "(g->teximage_param >> 30) & 0x1u",
     "mode mask 2 bits -> 1"),
    ("(g->teximage_param >> 30) & 0x3u", "(g->teximage_param >> 29) & 0x3u",
     "mode shift 30 -> 29"),
    ("if (tmode == 2u) {", "if (tmode == 1u) {", "mode 2 branch -> mode 1"),
    ("if (tmode == 3u) {", "if (tmode == 2u) {", "mode 3 branch -> mode 2"),
]


def code_view(lines):
    """Return one same-length string per line with comments/strings blanked."""
    views = []
    in_block = False
    for line in lines:
        chars = list(line)
        i = 0
        while i < len(chars):
            if in_block:
                if line.startswith("*/", i):
                    chars[i] = chars[i + 1] = " "
                    in_block = False
                    i += 2
                else:
                    chars[i] = " "
                    i += 1
                continue
            if line.startswith("/*", i):
                chars[i] = chars[i + 1] = " "
                in_block = True
                i += 2
                continue
            if line.startswith("//", i):
                for j in range(i, len(chars)):
                    chars[j] = " "
                break
            if chars[i] == '"':
                chars[i] = " "
                i += 1
                while i < len(chars) and chars[i] != '"':
                    chars[i] = " "
                    i += 1
                if i < len(chars):
                    chars[i] = " "
                    i += 1
                continue
            i += 1
        views.append("".join(chars))
    return views


def num_text(value, suffix):
    if isinstance(value, float):
        return f"{value:.1f}{suffix}"
    return f"{value}{suffix}"


def parse_num(token):
    body, suffix = NUM_RE.fullmatch(token).groups()
    if body.lower().startswith("0x"):
        return int(body, 16), suffix
    if "." in body or "e" in body.lower():
        return float(body), suffix
    return int(body), suffix


def display(value):
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else repr(value)
    return str(value)


def generic_mutants(text):
    """Yield (line_no, description, mutated_text) for one edit each."""
    lines = text.splitlines(keepends=True)
    views = code_view(lines)
    out = []
    for idx, (line, view) in enumerate(zip(lines, views)):
        if not any(marker in line for marker in NEW_MARKERS):
            continue
        for m in NUM_RE.finditer(view):
            value, suffix = parse_num(m.group(0))
            for delta in (1, -1):
                if delta == -1 and value <= 0:
                    continue
                new = value + delta
                out.append((idx + 1, m.start(), "const",
                            f"const {display(value)} -> {display(new)}",
                            line[: m.start()] + num_text(new, suffix) +
                            line[m.end():]))
        for m in OP_RE.finditer(view):
            swap = OP_SWAP.get(m.group(0))
            if swap is None:
                continue
            out.append((idx + 1, m.start(), "op",
                        f"op {m.group(0)} -> {swap}",
                        line[: m.start()] + swap + line[m.end():]))
    out.sort(key=lambda item: (item[0], 0 if item[2] == "const" else 1, item[1]))

    seen = set()
    kept = []
    for line_no, _col, _kind, desc, new_line in out:
        new_text = "".join(lines[: line_no - 1]) + new_line + \
            "".join(lines[line_no:])
        if new_text in seen or new_text == text:
            continue
        seen.add(new_text)
        kept.append((line_no, desc, new_text))
    return kept


def targeted_mutants(text):
    out = []
    for needle, replacement, desc in TARGETED:
        if needle not in text:
            raise SystemExit(
                f"targeted mutant substring missing: {needle!r} ({desc}); "
                "review docs/reports/tm23_mutate_gx_interp_tex23.py")
        new_text = text.replace(needle, replacement, 1)
        if new_text == text:
            raise SystemExit(f"targeted mutant made no change: {needle!r}")
        line_no = text[: text.index(needle)].count("\n") + 1
        out.append((line_no, f"targeted {desc}", new_text))
    return out


def mutants(text):
    out = []
    seen = {text}
    for line_no, desc, new_text in generic_mutants(text) + targeted_mutants(text):
        if new_text in seen:
            continue
        seen.add(new_text)
        out.append((line_no, desc, new_text))
    out.sort(key=lambda item: (item[0], item[1]))
    return out


def run_make(root, targets):
    return subprocess.run(
        ["make", "-j2"] + targets, cwd=root, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    ).returncode == 0


def build_test(root):
    return run_make(root, [str(TEST_BIN.relative_to(ROOT))])


def build_cli(root):
    return run_make(root, [str(CLI_BIN.relative_to(ROOT))])


def run_c_test(root):
    return subprocess.run(
        [str(root / TEST_BIN.relative_to(ROOT))], cwd=root,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def run_golden(root):
    return subprocess.run(
        [sys.executable, "-B", "-I", "-m", "unittest", "discover", "-s",
         "tools", "-p", "test_gx_interp_golden.py"],
        cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def write_target(root, text):
    (root / TARGET.relative_to(ROOT)).write_text(text, encoding="utf-8")


def run_one_mutant(root, new_text):
    """Return one of 'build', 'test', 'golden', or None for a survivor."""
    write_target(root, new_text)
    if not build_test(root):
        return "build"
    if not run_c_test(root):
        return "test"
    if not build_cli(root):
        return "build"
    if not run_golden(root):
        return "golden"
    return None


def baseline(root):
    if not build_test(root):
        return "baseline test build FAILED"
    if not build_cli(root):
        return "baseline CLI build FAILED"
    if not run_c_test(root):
        return "baseline C test FAILED"
    if not run_golden(root):
        return "baseline golden FAILED"
    return None


def worker(args):
    worker_index, chunk, original, source_root = args
    temp_root = Path(tempfile.mkdtemp(
        dir="/tmp/opencode", prefix=f"mutate_tex23_w{worker_index}_"))
    try:
        shutil.copytree(
            source_root, temp_root, dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(
                ".git", "*.o", "*.a", "__pycache__"))
        write_target(temp_root, original)
        error = baseline(temp_root)
        if error:
            return {"error": f"worker {worker_index}: {error}"}

        counts = {"test": 0, "golden": 0, "build": 0, "survived": 0}
        survivors = []
        killed = []
        for line_no, desc, new_text in chunk:
            step = run_one_mutant(temp_root, new_text)
            write_target(temp_root, original)
            if step is None:
                counts["survived"] += 1
                survivors.append((line_no, desc))
            else:
                counts[step] += 1
                killed.append((line_no, desc, step))
        return {"counts": counts, "survivors": survivors, "killed": killed}
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


def run_sequential(original):
    counts = {"test": 0, "golden": 0, "build": 0, "survived": 0}
    survivors = []
    error = baseline(ROOT)
    if error:
        print(error, flush=True)
        return None
    all_mutants = mutants(original)
    print(f"mutants: {len(all_mutants)} unique", flush=True)
    for index, (line_no, desc, new_text) in enumerate(all_mutants):
        step = None
        try:
            step = run_one_mutant(ROOT, new_text)
        finally:
            write_target(ROOT, original)
        if step is None:
            counts["survived"] += 1
            survivors.append((line_no, desc))
        else:
            counts[step] += 1
        print(f"[{index + 1}/{len(all_mutants)}] line {line_no} {desc} -> "
              f"{'SURVIVED' if step is None else 'KILLED(' + step + ')'}",
              flush=True)
    return counts, survivors, len(all_mutants)


def main():
    parser = argparse.ArgumentParser(
        description="Mutation runner for texture modes 2/3 in "
                    "common/twlhd_gx_interp.c")
    parser.add_argument("--jobs", type=int, default=1,
                        help="parallel workers (each gets a temp repo copy)")
    args = parser.parse_args()

    original = TARGET.read_text(encoding="utf-8")
    try:
        error = baseline(ROOT)
        if error:
            print(error, flush=True)
            return 2
        print("baseline: build OK, C test OK, golden OK", flush=True)

        all_mutants = mutants(original)
        print(f"mutants: {len(all_mutants)} unique", flush=True)
        if len(all_mutants) < 60:
            print("ERROR: fewer than 60 mutants", flush=True)
            return 2

        if args.jobs <= 1:
            result = run_sequential(original)
            if result is None:
                return 2
            counts, survivors, total = result
        else:
            chunks = [[] for _ in range(args.jobs)]
            for index, mutant in enumerate(all_mutants):
                chunks[index % args.jobs].append(mutant)
            chunks = [chunk for chunk in chunks if chunk]
            worker_args = [(index, chunk, original, ROOT)
                           for index, chunk in enumerate(chunks)]
            pool = multiprocessing.Pool(processes=len(chunks))
            try:
                results = pool.map(worker, worker_args)
            finally:
                pool.close()
                pool.join()

            counts = {"test": 0, "golden": 0, "build": 0, "survived": 0}
            survivors = []
            errors = []
            killed = []
            for result in results:
                if "error" in result:
                    errors.append(result["error"])
                    continue
                for key, value in result["counts"].items():
                    counts[key] += value
                survivors.extend(result["survivors"])
                killed.extend(result["killed"])
            if errors:
                for error in errors:
                    print(error, flush=True)
                return 2
            for line_no, desc, step in sorted(killed):
                print(f"line {line_no} {desc} -> KILLED({step})", flush=True)
            total = len(all_mutants)

        current = TARGET.read_text(encoding="utf-8")
        same = current == original
        print(flush=True)
        for key in ("test", "golden", "build", "survived"):
            print(f"{key + ':':<10}{counts[key]}", flush=True)
        print(f"{'total:':<10}{total}", flush=True)
        for line_no, desc in sorted(survivors):
            print(f"SURVIVOR line {line_no} {desc}", flush=True)
        print(f"common/ restored: {'YES' if same else 'NO'}", flush=True)
        return 0 if same else 1
    finally:
        write_target(ROOT, original)


if __name__ == "__main__":
    sys.exit(main())
