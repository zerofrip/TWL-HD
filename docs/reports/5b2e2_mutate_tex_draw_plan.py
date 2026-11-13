#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for common/twlhd_tex_draw_plan.c (5B-2e-2).

Standalone: `python3 docs/reports/5b2e2_mutate_tex_draw_plan.py --jobs 4`
from the repository root. It is not part of `make test`.

Mutant set (one edit per mutant, code only):

  * every comparison/logical operator occurrence is swapped to its neighbour:
    `==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`. Shift operators
    (`<<`, `>>`) and `->` are skipped.
  * every numeric constant gets +1 and, when positive, -1 (`-1` keeps its sign
    and its digits are shifted, so it becomes `-2` / `-0`).

Comment text, string literals and `#include` lines are skipped. `#define`
bodies are treated as code because the wrap/filter constants are real
behaviour.

Mechanism: the mutant text is written over `common/twlhd_tex_draw_plan.c`, the
host test `tests/host/build/test_tex_draw_plan` is rebuilt with the repository
host flags, the C test is run, and the original file is restored in a
`finally:`. The run ends by comparing the file's current text with the saved
`original` text. Identical mutant texts are deduplicated.

Parallel execution: pass `--jobs N` to run up to N workers. Each worker gets
its own temporary copy of the repository, so mutants can be built and tested
concurrently without interfering with each other or with the original tree.
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

# This file lives in docs/reports/, so the repository root is two levels up.
ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "common" / "twlhd_tex_draw_plan.c"
TEST_BIN = ROOT / "tests" / "host" / "build" / "test_tex_draw_plan"

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
    """Format a mutated numeric constant, keeping the literal style."""
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


def mutants(text):
    """Yield (line_no, column, description, mutated_text) for one edit."""
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


def build(root):
    return subprocess.run(
        ["make", "-j2", str(TEST_BIN.relative_to(ROOT))],
        cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def run_c_test(root):
    return subprocess.run(
        [str(root / TEST_BIN.relative_to(ROOT))], cwd=root,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def write_target(root, text):
    (root / TARGET.relative_to(ROOT)).write_text(text, encoding="utf-8")


def run_one_mutant(root, original, line_no, desc, new_text):
    """Return (step, line_no, desc) where step is None for a survivor."""
    write_target(root, new_text)
    if not build(root):
        return ("build", line_no, desc)
    if not run_c_test(root):
        return ("test", line_no, desc)
    return (None, line_no, desc)


def worker(args):
    """Process one chunk of mutants in a private repository copy."""
    worker_index, chunk, original, source_root = args
    temp_root = Path(tempfile.mkdtemp(prefix=f"mutate_tex_draw_plan_w{worker_index}_"))
    try:
        shutil.copytree(source_root, temp_root, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns(
                            ".git", "*.o", "*.a", "__pycache__"))

        write_target(temp_root, original)
        if not build(temp_root):
            return {"error": f"worker {worker_index}: baseline build failed"}
        if not run_c_test(temp_root):
            return {"error": f"worker {worker_index}: baseline C test failed"}

        counts = {"test": 0, "build": 0, "survived": 0}
        survivors = []
        killed = []
        for line_no, desc, new_text in chunk:
            step, m_line, m_desc = run_one_mutant(
                temp_root, original, line_no, desc, new_text)
            write_target(temp_root, original)
            if step is None:
                counts["survived"] += 1
                survivors.append((m_line, m_desc))
            else:
                counts[step] += 1
                killed.append((m_line, m_desc, step))
        return {"counts": counts, "survivors": survivors, "killed": killed}
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


def run_sequential(original):
    """Original single-process path, kept for --jobs 1."""
    scratch = Path(tempfile.mkdtemp(prefix="mutate_tex_draw_plan_"))
    counts = {"test": 0, "build": 0, "survived": 0}
    survivors = []
    try:
        write_target(ROOT, original)
        if not build(ROOT):
            print("baseline build FAILED", flush=True)
            return None
        if not run_c_test(ROOT):
            print("baseline C test FAILED", flush=True)
            return None
        print("baseline: build OK, test OK", flush=True)

        all_mutants = mutants(original)
        print(f"mutants: {len(all_mutants)} unique", flush=True)

        for index, (line_no, desc, new_text) in enumerate(all_mutants):
            (scratch / "mutant.c").write_text(new_text, encoding="utf-8")
            step = None
            try:
                write_target(ROOT, new_text)
                if not build(ROOT):
                    step = "build"
                elif not run_c_test(ROOT):
                    step = "test"
            finally:
                write_target(ROOT, original)

            if step is None:
                counts["survived"] += 1
                survivors.append((line_no, desc))
                print(f"line {line_no} {desc} -> SURVIVED", flush=True)
            else:
                counts[step] += 1
                print(f"line {line_no} {desc} -> KILLED({step})", flush=True)
        return counts, survivors, len(all_mutants)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(
        description="Mutation runner for common/twlhd_tex_draw_plan.c")
    parser.add_argument(
        "--jobs", type=int, default=1,
        help="number of parallel workers (each gets a temp repo copy)")
    args = parser.parse_args()

    original = TARGET.read_text(encoding="utf-8")
    try:
        write_target(ROOT, original)
        if not build(ROOT):
            print("baseline build FAILED", flush=True)
            return 2
        if not run_c_test(ROOT):
            print("baseline C test FAILED", flush=True)
            return 2
        print("baseline: build OK, test OK", flush=True)

        all_mutants = mutants(original)
        print(f"mutants: {len(all_mutants)} unique", flush=True)

        if args.jobs <= 1:
            result = run_sequential(original)
            if result is None:
                return 2
            counts, survivors, total = result
        else:
            chunks = [[] for _ in range(args.jobs)]
            for i, m in enumerate(all_mutants):
                chunks[i % args.jobs].append(m)
            chunks = [c for c in chunks if c]
            worker_args = [
                (i, chunk, original, ROOT)
                for i, chunk in enumerate(chunks)
            ]
            pool = multiprocessing.Pool(processes=len(chunks))
            try:
                results = pool.map(worker, worker_args)
            finally:
                pool.close()
                pool.join()

            counts = {"test": 0, "build": 0, "survived": 0}
            survivors = []
            errors = []
            for r in results:
                if "error" in r:
                    errors.append(r["error"])
                    continue
                for k, v in r["counts"].items():
                    counts[k] += v
                survivors.extend(r["survivors"])
            if errors:
                for e in errors:
                    print(e, flush=True)
                return 2
            total = len(all_mutants)

            killed = []
            for r in results:
                killed.extend(r.get("killed", []))
            for line_no, desc, step in sorted(killed, key=lambda x: (x[0], x[1])):
                print(f"line {line_no} {desc} -> KILLED({step})", flush=True)
            for line_no, desc in sorted(survivors, key=lambda x: (x[0], x[1])):
                print(f"line {line_no} {desc} -> SURVIVED", flush=True)

        current = TARGET.read_text(encoding="utf-8")
        same = current == original
        print(flush=True)
        print(f"killed by test:   {counts['test']}", flush=True)
        print(f"killed by build:  {counts['build']}", flush=True)
        print(f"survived:         {counts['survived']}", flush=True)
        print(f"total:            {total}", flush=True)
        if survivors:
            print("survivors:", flush=True)
            for line_no, desc in sorted(survivors, key=lambda x: (x[0], x[1])):
                print(f"  line {line_no} {desc}", flush=True)
        print(f"common/ restored: {'YES' if same else 'NO'}", flush=True)
        return 0 if same else 1
    finally:
        write_target(ROOT, original)


if __name__ == "__main__":
    sys.exit(main())
