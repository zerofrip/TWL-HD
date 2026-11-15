#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for common/twlhd_tex_capture.c (5C-1).

Standalone, from the repository root:

    python3 docs/reports/5c1_mutate_tex_capture.py --jobs 4

It is not part of `make test`.

Mutant set (one edit per mutant, code only):

  * every comparison/logical operator occurrence is swapped to its neighbour:
    `==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`. Shift operators
    (`<<`, `>>`) and `->` are skipped.
  * every numeric constant gets +1 and, when positive, -1.
  * a pinned targeted set swaps the constants that matter most (the 131,072 /
    32,768 defaults, the 65,540 full window, the format-5 4*max+8 construction,
    the 0x3FFF mask, the palette/texel/index shifts, the index base and the
    align4 pad). A missing pinned substring is a hard error: the source changed
    and the mutant set must be reviewed, not silently shrunk.

Comment text, string literals and `#include` lines are skipped; `#define`
bodies are code.

Mechanism: the mutant text is written over common/twlhd_tex_capture.c, the
host test is rebuilt and run, and a survivor is additionally checked against
the Python golden suite (building tests/host/build/tex_capture_cli first). The
original file is restored in `finally:`. Parallel workers each get a private
copy of the repository.
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
TARGET = ROOT / "common" / "twlhd_tex_capture.c"
TEST_BIN = ROOT / "tests" / "host" / "build" / "test_tex_capture"
CLI_BIN = ROOT / "tests" / "host" / "build" / "tex_capture_cli"

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

# (needle, replacement, description). Exactly one occurrence is changed.
TARGETED = [
    ("131072u", "131073u", "default TXDT 131072 -> 131073"),
    ("131072u", "131071u", "default TXDT 131072 -> 131071"),
    ("32768u", "32769u", "default PLDT 32768 -> 32769"),
    ("32768u", "32767u", "default PLDT 32768 -> 32767"),
    ("TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES", "TWLHD_V2_TEX_PALETTE_4X4_MIN_BYTES",
     "full 4x4 window -> minimum"),
    ("max * 4u + 8u", "max * 5u + 8u", "4x4 window multiplier 4 -> 5"),
    ("max * 4u + 8u", "max * 4u + 7u", "4x4 window tail 8 -> 7"),
    ("max * 4u + 8u", "max * 4u + 9u", "4x4 window tail 8 -> 9"),
    ("entry &= 0x3FFFu", "entry &= 0x1FFFu", "4x4 entry mask 14 bits -> 13"),
    ("entry &= 0x3FFFu", "entry &= 0x7FFFu", "4x4 entry mask 14 bits -> 15"),
    ("(param & 0xFFFFu) << 3", "(param & 0xFFFFu) << 2",
     "texel address shift 3 -> 2"),
    ("(param & 0xFFFFu) << 3", "(param & 0xFFFFu) << 4",
     "texel address shift 3 -> 4"),
    ("(param & 0x3FFFu) << 2", "(param & 0x3FFFu) << 3",
     "index address shift 2 -> 3"),
    ("pltt << 3", "pltt << 4", "I2 palette shift 3 -> 4"),
    ("pltt << 4", "pltt << 3", "non-I2 palette shift 4 -> 3"),
    ("0x30000u : 0x20000u", "0x20000u : 0x20000u",
     "index base 0x30000 -> 0x20000"),
    ("0x30000u : 0x20000u", "0x30000u : 0x30000u",
     "index base 0x20000 -> 0x30000"),
    ("(param & 0xC000u) == 0x8000u", "(param & 0xC000u) == 0x4000u",
     "index base selector 0x8000 -> 0x4000"),
    ("(value + 3u) & ~3u", "(value + 4u) & ~3u", "align4 pad 3 -> 4"),
    ("(value + 3u) & ~3u", "(value + 3u) & ~4u", "align4 mask ~3 -> ~4"),
    ("TWLHD_V2_TXDT_MAX_BYTES) {", "TWLHD_V2_TXDT_MAX_BYTES) + 1u) {",
     "static TXDT impossibility + 1"),
    ("TWLHD_V2_PLDT_MAX_BYTES) {", "TWLHD_V2_PLDT_MAX_BYTES) + 1u) {",
     "static PLDT impossibility + 1"),
    ("TWLHD_V2_CMD_VTX_16", "TWLHD_V2_CMD_VTX_10", "VTX range lower 0x23 -> 0x24"),
    ("TWLHD_V2_CMD_VTX_DIFF", "TWLHD_V2_CMD_VTX_YZ", "VTX range upper 0x28 -> 0x27"),
    ("TWLHD_V2_RAW_FIFO_BIT", "TWLHD_V2_CMD_ID_MASK",
     "raw FIFO skip -> command id mask"),
    ("CAP_KEY_MAX) {", "CAP_KEY_MAX) + 1u) {", "key table full check + 1"),
    ("kept != i", "kept != i + 1u", "compaction copy condition"),
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
        if line.lstrip().startswith("#include"):
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
                "review docs/reports/5c1_mutate_tex_capture.py")
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
    env = os.environ.copy()
    env["TEX_CAPTURE_CLI"] = str(root / CLI_BIN.relative_to(ROOT))
    return subprocess.run(
        [sys.executable, "-B", "-I", "-m", "unittest", "discover", "-s",
         "tools", "-p", "test_tex_capture_golden.py"],
        cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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
    write_target(root, (root / TARGET.relative_to(ROOT)).read_text(
        encoding="utf-8"))
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
        prefix=f"mutate_tex_capture_w{worker_index}_"))
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
        description="Mutation runner for common/twlhd_tex_capture.c")
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
        if len(all_mutants) < 80:
            print("ERROR: fewer than 80 mutants", flush=True)
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
