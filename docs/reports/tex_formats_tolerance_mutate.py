#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for tools/check_tex_formats.py (tolerance review).

Standalone, from the repository root:

    python3 docs/reports/tex_formats_tolerance_mutate.py [--jobs N]

It is not part of `make test`. The committed report is
docs/reports/tex-formats-tolerance-mutation-report.md.

Mechanism follows docs/reports/5c3_mutate_tex_compare.py: each mutant is one
edit of the checker in a temporary sandbox copy of the repository, then only
`CheckerTest` runs (`unittest discover -s tools -p test_tex_formats.py -k
CheckerTest`). A nonzero run kills the mutant. The original file is restored
in `finally:` and the run fails if the sandbox file did not match afterwards.

Mutant set (code only; comments and strings are skipped):

  * every numeric constant gets +1 and, when positive, -1;
  * every comparison/logical operator is swapped to its neighbour
    (`==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`);
  * a pinned targeted set mutates the cell/bbox/tolerance logic that the
    tolerance decision depends on. A missing pinned substring is a hard error.
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
TARGET = ROOT / "tools" / "check_tex_formats.py"
TEST_PATTERN = "test_tex_formats.py"
TEST_FILTER = "CheckerTest"

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
    ("TOLERANCE = 28", "TOLERANCE = 29", "default tolerance 28 -> 29"),
    ("TOLERANCE = 28", "TOLERANCE = 27", "default tolerance 28 -> 27"),
    ("TOLERANCE = 28", "TOLERANCE = 24", "default tolerance back to 24"),
    ("return all(abs(pixel[i] - CLEAR[i]) <= tolerance",
     "return all(abs(pixel[i] - CLEAR[i]) < tolerance",
     "clear test boundary <= -> <"),
    ("if deviation <= tolerance:", "if deviation < tolerance:",
     "cell match boundary <= -> <"),
    ("if deviation > worst:", "if deviation >= worst:",
     "worst update > -> >="),
    ("score = (-matched, worst, abs(dx) + abs(dy))",
     "score = (-matched, -worst, abs(dx) + abs(dy))",
     "shift score prefers larger worst"),
    ("score = (-matched, worst, abs(dx) + abs(dy))",
     "score = (matched, worst, abs(dx) + abs(dy))",
     "shift score prefers fewer matches"),
    ("got = _mean3(data, width, top, x, y)",
     "got = _pixel_at(data, width, x, y)",
     "sample one pixel instead of the 3x3 mean"),
    ("y = _cell_centre(upper, box_h, tex_h, ty) + dy",
     "y = _cell_centre(upper, box_h, tex_h, ty) - dy", "shift dy sign"),
    ("x = _cell_centre(left, box_w, tex_w, tx) + dx",
     "x = _cell_centre(left, box_w, tex_w, tx) - dx", "shift dx sign"),
    ("want = expected[ty * tex_w + tx]", "want = expected[tx * tex_h + ty]",
     "expected index transposed"),
    ("y = _cell_centre(upper, box_h, tex_h, ty)",
     "y = _cell_centre(upper, box_h, tex_h, tex_h - 1 - ty)",
     "expected row flipped"),
    ("return int(left + (cell + 0.5) * size / texels)",
     "return int(left + cell * size / texels)",
     "sample the cell left edge, not the centre"),
    ("return int(left + (cell + 0.5) * size / texels)",
     "return int(left + (cell + 0.5) * texels / size)",
     "cell centre scales by texels/size"),
    ("deviation = max(abs(got[i] - want[i]) for i in range(3))",
     "deviation = abs(got[0] - want[0])", "deviation uses only red"),
    ("deviation = max(abs(got[i] - want[i]) for i in range(3))",
     "deviation = min(abs(got[i] - want[i]) for i in range(3))",
     "deviation uses the channel minimum"),
    ("if best is None or score < best[0]:",
     "if best is None or score > best[0]:", "shift selection inverted"),
    ("x1 = min(x1, width)", "x1 = max(x1, width)",
     "cell x1 clamped with max"),
    ("return 0 if all_ok else 1", "return 0", "always exit 0"),
    ("return 0 if all_ok else 1", "return 1", "always exit 1"),
]


def code_view(lines):
    """One same-length string per line with comments/strings blanked."""
    views = []
    triple = None
    for line in lines:
        chars = list(line)
        index = 0
        while index < len(chars):
            if triple is not None:
                end = line.find(triple, index)
                if end == -1:
                    for tail in range(index, len(chars)):
                        chars[tail] = " "
                    break
                for tail in range(index, end + 3):
                    chars[tail] = " "
                index = end + 3
                triple = None
                continue
            if chars[index] == "#":
                for tail in range(index, len(chars)):
                    chars[tail] = " "
                break
            if line.startswith('"""', index) or line.startswith("'''", index):
                triple = line[index:index + 3]
                index += 3
                continue
            if chars[index] in "\"'":
                quote = chars[index]
                chars[index] = " "
                index += 1
                while index < len(chars):
                    if chars[index] == "\\":
                        chars[index] = " "
                        if index + 1 < len(chars):
                            chars[index + 1] = " "
                        index += 2
                        continue
                    if chars[index] == quote:
                        chars[index] = " "
                        index += 1
                        break
                    chars[index] = " "
                    index += 1
                continue
            index += 1
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
    lines = text.splitlines(keepends=True)
    views = code_view(lines)
    out = []
    for index, (line, view) in enumerate(zip(lines, views)):
        for match in NUM_RE.finditer(view):
            value, suffix = parse_num(match.group(0))
            for delta in (1, -1):
                if delta == -1 and value <= 0:
                    continue
                new = value + delta
                out.append((index + 1, match.start(), "const",
                            f"const {display(value)} -> {display(new)}",
                            line[:match.start()] + num_text(new, suffix) +
                            line[match.end():]))
        for match in OP_RE.finditer(view):
            swap = OP_SWAP.get(match.group(0))
            if swap is None:
                continue
            out.append((index + 1, match.start(), "op",
                        f"op {match.group(0)} -> {swap}",
                        line[:match.start()] + swap + line[match.end():]))
    out.sort(key=lambda item: (item[0], 0 if item[2] == "const" else 1,
                               item[1]))

    seen = set()
    kept = []
    for line_no, _column, _kind, desc, new_line in out:
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
                "review docs/reports/tex_formats_tolerance_mutate.py")
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


def write_target(root, text):
    (root / TARGET.relative_to(ROOT)).write_text(text, encoding="utf-8")


def run_tests(root):
    return subprocess.run(
        [sys.executable, "-B", "-I", "-m", "unittest", "discover", "-s",
         "tools", "-p", TEST_PATTERN, "-k", TEST_FILTER],
        cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def baseline(root):
    if not run_tests(root):
        return "baseline test FAILED"
    return None


def worker(args):
    worker_index, chunk, original, source_root = args
    temp_root = Path(tempfile.mkdtemp(
        prefix=f"mutate_tex_formats_w{worker_index}_"))
    try:
        shutil.copytree(
            source_root, temp_root, dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(
                ".git", "build*", "*.o", "*.a", "*.nds", "*.elf", "*.map",
                "__pycache__"))
        write_target(temp_root, original)
        error = baseline(temp_root)
        if error:
            return {"error": f"worker {worker_index}: {error}"}

        survivors = []
        killed = []
        for line_no, desc, new_text in chunk:
            write_target(temp_root, new_text)
            alive = run_tests(temp_root)
            write_target(temp_root, original)
            if alive:
                survivors.append((line_no, desc))
            else:
                killed.append((line_no, desc))
        return {"survivors": survivors, "killed": killed}
    finally:
        shutil.rmtree(temp_root, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(
        description="Mutation runner for tools/check_tex_formats.py")
    parser.add_argument("--jobs", type=int, default=1,
                        help="parallel workers (each gets a temp repo copy)")
    args = parser.parse_args()

    original = TARGET.read_text(encoding="utf-8")
    try:
        error = baseline(ROOT)
        if error:
            print(error, flush=True)
            return 2
        print("baseline: test OK", flush=True)

        all_mutants = mutants(original)
        print(f"mutants: {len(all_mutants)} unique", flush=True)
        if len(all_mutants) < 15:
            print("ERROR: fewer than 15 mutants", flush=True)
            return 2

        if args.jobs <= 1:
            survivors = []
            killed = []
            for index, (line_no, desc, new_text) in enumerate(all_mutants):
                write_target(ROOT, new_text)
                alive = run_tests(ROOT)
                write_target(ROOT, original)
                if alive:
                    survivors.append((line_no, desc))
                else:
                    killed.append((line_no, desc))
                print(f"[{index + 1}/{len(all_mutants)}] line {line_no} "
                      f"{desc} -> "
                      f"{'SURVIVED' if alive else 'KILLED'}", flush=True)
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

            survivors = []
            killed = []
            errors = []
            for result in results:
                if "error" in result:
                    errors.append(result["error"])
                    continue
                survivors.extend(result["survivors"])
                killed.extend(result["killed"])
            if errors:
                for problem in errors:
                    print(problem, flush=True)
                return 2
            for line_no, desc in sorted(killed):
                print(f"line {line_no} {desc} -> KILLED", flush=True)

        current = TARGET.read_text(encoding="utf-8")
        same = current == original
        print(flush=True)
        print(f"{'mutants:':<12}{len(all_mutants)}", flush=True)
        print(f"{'killed:':<12}{len(killed)}", flush=True)
        print(f"{'survived:':<12}{len(survivors)}", flush=True)
        for line_no, desc in sorted(survivors):
            print(f"SURVIVOR line {line_no} {desc}", flush=True)
        print(f"checker restored: {'YES' if same else 'NO'}", flush=True)
        return 0 if same else 1
    finally:
        write_target(ROOT, original)


if __name__ == "__main__":
    sys.exit(main())
