#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for common/twlhd_pica_frame.c (5B-2e-1).

Standalone: `python3 tools/mutate_pica_frame.py` from the repository root. It
is not part of `make test`.

Mutant set (one edit per mutant, code only):

  * every comparison/logical operator occurrence is swapped to its neighbour:
    `==` <-> `!=`, `<` <-> `<=`, `>` <-> `>=`, `&&` <-> `||`. Shift operators
    (`<<`, `>>`) and `->` are skipped.
  * every numeric constant gets +1 and, when positive, -1 (`-1` keeps its sign
    and its digits are shifted, so it becomes `-2` / `-0`).

Comment text, string literals and `#include` lines are skipped. `#define`
bodies are treated as code, because `FRAME_FORMAT`'s constants are real
behaviour.

Mechanism: the mutant text is written over `common/twlhd_pica_frame.c`, the
two artifacts `make test` builds for this feature are rebuilt with the
repository host flags (`tests/host/build/test_pica_frame` and
`tests/host/build/pica_frame_cli`), the C test is run, and for survivors the
golden unittest is run with `PICA_FRAME_CLI` pointing at the freshly built
CLI. The original file is restored in a `finally:` and the run ends with a
`git diff --exit-code common/` check. Identical mutant texts are deduplicated.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "common" / "twlhd_pica_frame.c"
TEST_BIN = ROOT / "tests" / "host" / "build" / "test_pica_frame"
CLI_BIN = ROOT / "tests" / "host" / "build" / "pica_frame_cli"

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


def build():
    return subprocess.run(
        ["make", "-j2", str(TEST_BIN.relative_to(ROOT)),
         str(CLI_BIN.relative_to(ROOT))],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def run_c_test():
    return subprocess.run(
        [str(TEST_BIN)], cwd=ROOT, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    ).returncode == 0


def run_golden():
    env = dict(os.environ)
    env["PICA_FRAME_CLI"] = str(CLI_BIN)
    return subprocess.run(
        [sys.executable, "-B", "-I", "-m", "unittest", "discover", "-s",
         "tools", "-p", "test_pica_frame_golden.py"],
        cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    ).returncode == 0


def write_target(text):
    TARGET.write_text(text, encoding="utf-8")


def main():
    original = TARGET.read_text(encoding="utf-8")
    scratch = Path(tempfile.mkdtemp(prefix="mutate_pica_frame_"))
    counts = {"test": 0, "golden": 0, "build": 0, "survived": 0}
    survivors = []
    try:
        write_target(original)
        if not build():
            print("baseline build FAILED", flush=True)
            return 2
        if not run_c_test():
            print("baseline C test FAILED", flush=True)
            return 2
        if not run_golden():
            print("baseline golden test FAILED", flush=True)
            return 2
        print("baseline: build OK, test OK, golden OK", flush=True)

        all_mutants = mutants(original)
        print(f"mutants: {len(all_mutants)} unique", flush=True)

        for index, (line_no, desc, new_text) in enumerate(all_mutants):
            (scratch / "mutant.c").write_text(new_text, encoding="utf-8")
            step = None
            try:
                write_target(new_text)
                if not build():
                    step = "build"
                elif not run_c_test():
                    step = "test"
                elif not run_golden():
                    step = "golden"
            finally:
                write_target(original)

            if step is None:
                counts["survived"] += 1
                survivors.append((line_no, desc))
                print(f"line {line_no} {desc} -> SURVIVED", flush=True)
            else:
                counts[step] += 1
                print(f"line {line_no} {desc} -> KILLED({step})", flush=True)

        same = subprocess.run(
            ["git", "diff", "--exit-code", "--", "common/"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        ).returncode == 0
        print(flush=True)
        print(f"killed by test:   {counts['test']}", flush=True)
        print(f"killed by golden: {counts['golden']}", flush=True)
        print(f"killed by build:  {counts['build']}", flush=True)
        print(f"survived:         {counts['survived']}", flush=True)
        print(f"total:            {len(all_mutants)}", flush=True)
        if survivors:
            print("survivors:", flush=True)
            for line_no, desc in survivors:
                print(f"  line {line_no} {desc}", flush=True)
        print(f"common/ restored: {'YES' if same else 'NO'}", flush=True)
        return 0 if same else 1
    finally:
        write_target(original)
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
