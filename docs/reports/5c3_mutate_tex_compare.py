#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for tools/emulator/compare_texture_capture.py (5C-3).

Standalone, from the repository root:

    python3 docs/reports/5c3_mutate_tex_compare.py [--jobs N]

It is not part of `make test`. The committed report is
docs/reports/5c3-mutation-report.md.

Mutant set (one edit per mutant, code only):

  * every comparison/logical operator occurrence is swapped to its neighbour:
    `==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`. Shift operators
    (`<<`, `>>`) and `->` are skipped.
  * every numeric constant gets +1 and, when positive, -1.
  * a pinned targeted set mutates the constants that matter most: the
    TEXIMAGE_PARAM/PLTT_BASE literals, the three size literals, the command
    ids, GL_QUADS, the vertex/group counts, the backend/coverage constants and
    the order/SWAP/overlap comparisons. A missing pinned substring is a hard
    error: the source changed and the mutant set must be reviewed, not
    silently shrunk.

Comment text and string literals are skipped.

Mechanism: each mutant is written over the comparator in a temporary sandbox
copy of the repository, then only the comparator test class is run
(`unittest discover -s tools -p test_emulator_texture_compare.py -k
TextureCompareTest`). A nonzero run kills the mutant. The original file is
restored in `finally:` and the run aborts if the sandbox file did not match the
original afterwards.
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
TARGET = ROOT / "tools" / "emulator" / "compare_texture_capture.py"
TEST_PATTERN = "test_emulator_texture_compare.py"
TEST_FILTER = "TextureCompareTest"

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
    ("TEXIMAGE_PARAM = 3 << 26", "TEXIMAGE_PARAM = 3 << 25",
     "param format shift 26 -> 25"),
    ("TEXIMAGE_PARAM = 3 << 26", "TEXIMAGE_PARAM = 2 << 26",
     "param I4 format 3 -> 2 (A3I5)"),
    ("PLTT_BASE = 0", "PLTT_BASE = 1", "pltt base 0 -> 1"),
    ("TEXEL_SIZE = 32", "TEXEL_SIZE = 31", "texel size 32 -> 31"),
    ("TEXEL_SIZE = 32", "TEXEL_SIZE = 33", "texel size 32 -> 33"),
    ("PALETTE_SIZE = 32", "PALETTE_SIZE = 31", "palette size 32 -> 31"),
    ("INDEX_SIZE = 0", "INDEX_SIZE = 1", "index size 0 -> 1"),
    ("CMD_TEXIMAGE_PARAM = 0x2A", "CMD_TEXIMAGE_PARAM = 0x2B",
     "TEXIMAGE_PARAM cmd 0x2A -> 0x2B"),
    ("CMD_PLTT_BASE = 0x2B", "CMD_PLTT_BASE = 0x2C",
     "PLTT_BASE cmd 0x2B -> 0x2C"),
    ("CMD_TEX_COORD = 0x22", "CMD_TEX_COORD = 0x23",
     "TEX_COORD cmd 0x22 -> 0x23"),
    ("CMD_BEGIN_VTXS = 0x40", "CMD_BEGIN_VTXS = 0x41",
     "BEGIN cmd 0x40 -> 0x41"),
    ("CMD_END_VTXS = 0x41", "CMD_END_VTXS = 0x42",
     "END cmd 0x41 -> 0x42"),
    ("CMD_SWAP_BUFFERS = 0x50", "CMD_SWAP_BUFFERS = 0x51",
     "SWAP cmd 0x50 -> 0x51"),
    ("GL_QUADS = 1", "GL_QUADS = 2", "GL_QUADS 1 -> 2"),
    ("VERTEX_COUNT = 4", "VERTEX_COUNT = 5", "vertex count 4 -> 5"),
    ("BACKEND_REGISTER_PROXY = 1", "BACKEND_REGISTER_PROXY = 2",
     "backend 1 -> 2"),
    ("COVERAGE_ALL = 0x7", "COVERAGE_ALL = 0x8", "coverage 0x7 -> 0x8"),
    ("DEFAULT_ANGLE = 61440", "DEFAULT_ANGLE = 61441",
     "default angle 61440 -> 61441"),
    ("len(records) != 1", "len(records) != 2", "record count check 1 -> 2"),
    ("record.teximage_param != TEXIMAGE_PARAM",
     "record.teximage_param == TEXIMAGE_PARAM",
     "txds param key comparison != -> =="),
    ("record.pltt_base != PLTT_BASE", "record.pltt_base == PLTT_BASE",
     "txds pltt key comparison != -> =="),
    ("if not teximage[0] < pltt[0] < begin:",
     "if not teximage[0] <= pltt[0] <= begin:",
     "key order strict < -> non-strict <="),
    ("if len(ends) != 1 or not after:", "if len(ends) != 1 or after:",
     "END-after-BEGIN presence check"),
    ("for vertex in range(VERTEX_COUNT):",
     "for vertex in range(VERTEX_COUNT + 1):", "group walk off-by-one"),
    ("if group[base][0] != CMD_TEX_COORD:",
     "if group[base][0] == CMD_TEX_COORD:", "TEX_COORD presence inverted"),
    ("if view.txdt != TEXEL:", "if view.txdt != PALETTE:",
     "TXDT compared with the palette array"),
    ("if view.pldt != PALETTE:", "if view.pldt != TEXEL:",
     "PLDT compared with the texel array"),
    ("if not entries or entries[-1][0] != CMD_SWAP_BUFFERS:",
     "if not entries or entries[-1][0] == CMD_SWAP_BUFFERS:",
     "SWAP-last check inverted"),
    ("if view.flags != twlhd_v2_dump.FLAG_FRAME_COMPLETE:",
     "if view.flags == twlhd_v2_dump.FLAG_FRAME_COMPLETE:",
     "FRAME_COMPLETE check inverted"),
    ("if view.flags & twlhd_v2_dump.FLAG_OVERFLOW:",
     "if not view.flags & twlhd_v2_dump.FLAG_OVERFLOW:",
     "OVERFLOW detection inverted"),
    ("0x10, 0x32, 0x54", "0x11, 0x32, 0x54",
     "expected TEXEL[0] 0x10 -> 0x11"),
    ("0x1F, 0x00, 0x00, 0x80", "0x1E, 0x00, 0x00, 0x80",
     "expected PALETTE[8] 0x1F -> 0x1E"),
]


def data_block_lines(lines):
    """Line indexes of the TEXEL/PALETTE byte tables (data, not logic)."""
    skip = set()
    name = None
    for index, line in enumerate(lines):
        if name is None:
            if re.match(r"(?:TEXEL|PALETTE) = bytes\(\($", line.rstrip()):
                name = "table"
                skip.add(index)
        else:
            skip.add(index)
            if line.strip() == "))":
                name = None
    return skip


def code_view(lines):
    """Return one same-length string per line with comments/strings blanked.

    Python-aware: `#` comments, single/double quoted strings and triple-quoted
    docstrings are skipped; operator/number scanning only sees code.
    """
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
    skip = data_block_lines(lines)
    out = []
    for index, (line, view) in enumerate(zip(lines, views)):
        if index in skip:
            continue
        if line.lstrip().startswith("#include"):
            continue
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
                "review docs/reports/5c3_mutate_tex_compare.py")
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
         "tools", "-p", TEST_PATTERN, "-k", "TextureCompareTest",
         "-k", "DemoSourceCrossCheckTest"],
        cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def baseline(root):
    if not run_tests(root):
        return "baseline test FAILED"
    return None


def worker(args):
    worker_index, chunk, original, source_root = args
    temp_root = Path(tempfile.mkdtemp(
        prefix=f"mutate_tex_compare_w{worker_index}_"))
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
        description="Mutation runner for tools/emulator/compare_texture_capture.py")
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
        if len(all_mutants) < 20:
            print("ERROR: fewer than 20 mutants", flush=True)
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
        print(f"comparator restored: {'YES' if same else 'NO'}", flush=True)
        return 0 if same else 1
    finally:
        write_target(ROOT, original)


if __name__ == "__main__":
    sys.exit(main())
