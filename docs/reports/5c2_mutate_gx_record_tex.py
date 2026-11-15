#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Mutation runner for 5C-2 new code:
  - common/twlhd_gx_record.c between the 5C-2 serialize_tex markers
  - common/twlhd_tex_source.c (whole file)

Standalone, from the repository root:

    python3 docs/reports/5c2_mutate_gx_record_tex.py > docs/reports/5c2-mutation-run.txt 2>&1

It is not part of `make test`. The test binary is
`tests/host/build/test_gx_record_tex`; there is no CLI/golden step for this
module.

Each mutant is compiled directly with `cc` into its own directory under
`tests/host/build/mut/`; the real files in `common/` are never modified.
Every compile and every test run has a 20-second timeout.
"""

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GX_RECORD = ROOT / "common" / "twlhd_gx_record.c"
TEX_SOURCE = ROOT / "common" / "twlhd_tex_source.c"
TEST_SRC = ROOT / "tests" / "host" / "test_gx_record_tex.c"
MUT_DIR = ROOT / "tests" / "host" / "build" / "mut"

HOST_CC = os.environ.get("HOST_CC", "cc")
HOST_CFLAGS = [
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
    "-I" + str(ROOT / "common"),
    "-I" + str(ROOT / "tests" / "host"),
    "-I" + str(ROOT / "tools" / "azahar_host"),
    "-fsanitize=address,undefined",
    "-fno-omit-frame-pointer",
]
HOST_LDFLAGS = ["-fsanitize=address,undefined"]

# Sources other than the two mutated files.
OTHER_SOURCES = [
    TEST_SRC,
    ROOT / "common" / "twlhd_gx_record.c",
    ROOT / "common" / "twlhd_tex_source.c",
    ROOT / "common" / "twlhd_pica_frame.c",
    ROOT / "common" / "twlhd_pica_tex.c",
    ROOT / "common" / "twlhd_v2_parse.c",
    ROOT / "common" / "twlhd_v2_write.c",
    ROOT / "common" / "twlhd_gx_interp.c",
    ROOT / "common" / "twlhd_tex_decode.c",
    ROOT / "common" / "twlhd_tex_capture.c",
]

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

TARGETED_GX = [
    ("TWLHD_GX_REC_TEX_TXDT_MAX", "TWLHD_GX_REC_TEX_PLDT_MAX",
     "serialize apply TXDT capacity -> PLDT capacity"),
    ("TWLHD_GX_REC_TEX_PLDT_MAX", "TWLHD_GX_REC_TEX_TXDT_MAX",
     "serialize apply PLDT capacity -> TXDT capacity"),
    ("keys_dropped_budget +\n"
     "            scratch->plan.stats.keys_dropped_read_error +\n"
     "            scratch->plan.stats.keys_dropped_unsupported >\n"
     "        0u",
     "keys_dropped_budget +\n"
     "            scratch->plan.stats.keys_dropped_read_error +\n"
     "            scratch->plan.stats.keys_dropped_unsupported <\n"
     "        0u",
     "drop predicate >0 -> <0 (unsigned never true)"),
    ("file_flags |= TWLHD_V2_FLAG_OVERFLOW",
     "file_flags &= ~TWLHD_V2_FLAG_OVERFLOW",
     "file flag OR -> AND-NOT"),
    ("keys_dropped_budget +\n            scratch->plan.stats.keys_dropped_read_error +\n            scratch->plan.stats.keys_dropped_unsupported >\n        0u",
     "keys_dropped_budget +\n            scratch->plan.stats.keys_dropped_read_error +\n            scratch->plan.stats.keys_dropped_unsupported ==\n        0u",
     "drop predicate >0 -> ==0"),
    ("cap_result != TWLHD_TEX_CAP_OK", "cap_result == TWLHD_TEX_CAP_OK",
     "plan ok check invert"),
    ("    if (!rec || !scratch || !out || !limits || !read) {\n"
     "        return TWLHD_V2_ERR_NULL;\n    }",
     "    if (!rec || !scratch || !out || !limits || !read) {\n"
     "        return TWLHD_V2_ERR_SIZE;\n    }",
     "serialize_tex null error -> size error"),
    ("        if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY) "
     "return TWLHD_V2_ERR_SIZE;",
     "        if (cap_result == TWLHD_TEX_CAP_ERR_CAPACITY) "
     "return TWLHD_V2_ERR_NULL;",
     "plan capacity error -> null error"),
    ("return TWLHD_V2_ERR_TXDS;", "return TWLHD_V2_ERR_NULL;",
     "txds error -> null error"),
    ("return TWLHD_V2_ERR_TXDT;", "return TWLHD_V2_ERR_NULL;",
     "txdt error -> null error"),
    ("params.tex = scratch->entries;", "params.tex = NULL;",
     "tex entries -> NULL"),
    ("params.tex_count = scratch->plan.record_count;",
     "params.tex_count = scratch->plan.record_count + 1u;",
     "tex count +1"),
    ("file_flags = rec->flags;", "file_flags = 0;",
     "file flags cleared"),
    ("TWLHD_V2_TXDS_MAX_RECORDS);",
     "TWLHD_V2_TXDS_MAX_RECORDS - 1u);",
     "entries capacity -1"),
    ("    if (!rec || !scratch || !out || !limits || !read) {\n"
     "        return TWLHD_V2_ERR_NULL;\n    }",
     "    if (!rec || !scratch || !out || !limits || !read) {\n"
     "        return TWLHD_V2_OK;\n    }",
     "serialize_tex null error -> ok"),
    ("if (!rec || !scratch || !out || !limits || !read)",
     "if (!scratch || !out || !limits || !read)",
     "drop rec null check"),
]

TARGETED_TS = [
    ("ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX",
     "ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX - 1u",
     "source region capacity -1"),
    ("length == 0u", "length != 0u",
     "reject zero-length -> reject nonzero-length"),
    ("ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX",
     "ctx->count > TWLHD_TEX_SOURCE_REGION_MAX",
     "region-table full >= -> >"),
    ("r->kind != kind", "r->kind == kind",
     "kind match != -> =="),
    ("offset < r->base", "offset <= r->base",
     "region lower bound < -> <="),
    ("rel + (uint64_t)bytes > (uint64_t)r->length",
     "rel + (uint64_t)bytes >= (uint64_t)r->length",
     "containment > -> >="),
    ("if (dst)", "if (!dst)",
     "copy guard invert"),
    ("memcpy(dst, r->data + rel, bytes)", "memset(dst, 0, bytes)",
     "copy -> zero fill"),
    ("if (!ctx || !ram) return 1;", "if (!ctx || !ram) return 0;",
     "null add returns ok"),
    ("if (length == 0u) return 1;", "if (length == 0u) return 0;",
     "zero length returns ok"),
    ("if (ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX) return 1;",
     "if (ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX) return 0;",
     "full table returns ok"),
    ("for (i = 0; i < src->count; i++)",
     "for (i = 0; i <= src->count; i++)",
     "loop OOB scan"),
    ("if (!src) return 1;", "if (!src) return 0;",
     "null ctx read returns ok"),
    ("r->kind = kind;", "r->kind = kind + 1u;",
     "stored kind +1"),
    ("r->base = base;", "r->base = base + 1u;",
     "stored base +1"),
    ("r->length = length;", "r->length = length - 1u;",
     "stored length -1"),
    ("r->data = ram;", "r->data = NULL;",
     "stored data NULL"),
    ("if (rel < (uint64_t)r->length) return 0;",
     "if (rel <= (uint64_t)r->length) return 0;",
     "zero-length probe <="),
    ("if (dst) memcpy(dst, r->data + rel, bytes);\n        return 0;",
     "if (dst) memcpy(dst, r->data + rel, bytes);\n        return 1;",
     "successful read returns failure"),
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


def generic_mutants(text, first_line=0, last_line=None):
    """Yield (line_no, description, mutated_text) for one edit each."""
    lines = text.splitlines(keepends=True)
    views = code_view(lines)
    if last_line is None:
        last_line = len(lines)
    out = []
    for idx in range(first_line, min(last_line, len(lines))):
        line = lines[idx]
        view = views[idx]
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


def find_marker_lines(lines, begin, end):
    first = None
    last = None
    for i, line in enumerate(lines):
        if begin in line:
            first = i
        if end in line:
            last = i
    if first is None or last is None or first >= last:
        raise SystemExit(
            f"could not locate 5C-2 markers in {GX_RECORD}; "
            "review docs/reports/5c2_mutate_gx_record_tex.py")
    return first, last


def gx_record_mutants(text):
    lines = text.splitlines(keepends=True)
    first, last = find_marker_lines(lines, "BEGIN 5C-2 serialize_tex",
                                     "END 5C-2 serialize_tex")
    return generic_mutants(text, first, last + 1)


def tex_source_mutants(text):
    return generic_mutants(text)


def targeted_mutants(text, table, first_line=0, last_line=None):
    out = []
    for needle, replacement, desc in table:
        if needle not in text:
            raise SystemExit(
                f"targeted mutant substring missing: {needle!r} ({desc}); "
                "review docs/reports/5c2_mutate_gx_record_tex.py")
        new_text = text.replace(needle, replacement, 1)
        if new_text == text:
            raise SystemExit(f"targeted mutant made no change: {needle!r}")
        line_no = text[: text.index(needle)].count("\n") + 1
        if first_line and line_no < first_line:
            raise SystemExit(
                f"targeted mutant for {desc!r} landed at line {line_no}, "
                f"before the scoped range ({first_line}-{last_line}); "
                "review docs/reports/5c2_mutate_gx_record_tex.py")
        if last_line is not None and line_no > last_line:
            raise SystemExit(
                f"targeted mutant for {desc!r} landed at line {line_no}, "
                f"after the scoped range ({first_line}-{last_line}); "
                "review docs/reports/5c2_mutate_gx_record_tex.py")
        out.append((line_no, f"targeted {desc}", new_text))
    return out


def all_mutants(gx_text, ts_text):
    out = []
    seen = set()
    gx_lines = gx_text.splitlines(keepends=True)
    gx_first, gx_last = find_marker_lines(gx_lines, "BEGIN 5C-2 serialize_tex",
                                          "END 5C-2 serialize_tex")
    for line_no, desc, new_text in gx_record_mutants(gx_text):
        if new_text not in seen:
            seen.add(new_text)
            out.append(("gx_record", line_no, desc, new_text, ts_text))
    for line_no, desc, new_text in tex_source_mutants(ts_text):
        if new_text not in seen:
            seen.add(new_text)
            out.append(("tex_source", line_no, desc, gx_text, new_text))
    for line_no, desc, new_text in targeted_mutants(gx_text, TARGETED_GX,
                                                    gx_first + 1, gx_last + 1):
        if new_text not in seen:
            seen.add(new_text)
            out.append(("gx_record", line_no, desc, new_text, ts_text))
    for line_no, desc, new_text in targeted_mutants(ts_text, TARGETED_TS):
        if new_text not in seen:
            seen.add(new_text)
            out.append(("tex_source", line_no, desc, gx_text, new_text))
    out.sort(key=lambda item: (item[0], item[1], item[2]))
    return out


def compile_and_run(output_bin, gx_path, ts_path):
    """Compile the test binary and run it. Return one of: build, test, timeout, None."""
    cmd = [HOST_CC] + HOST_CFLAGS + ["-o", str(output_bin)]
    for src in OTHER_SOURCES:
        if src.name == "twlhd_gx_record.c":
            cmd.append(str(gx_path))
        elif src.name == "twlhd_tex_source.c":
            cmd.append(str(ts_path))
        else:
            cmd.append(str(src))
    cmd += HOST_LDFLAGS
    try:
        comp = subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=20)
    except subprocess.TimeoutExpired:
        return "timeout"
    if comp.returncode != 0:
        return "build"
    try:
        run = subprocess.run([str(output_bin)], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=20)
    except subprocess.TimeoutExpired:
        return "timeout"
    if run.returncode != 0:
        return "test"
    return None


def baseline():
    base_dir = MUT_DIR / "baseline"
    base_dir.mkdir(parents=True, exist_ok=True)
    output_bin = base_dir / "test_gx_record_tex"
    cmd = [HOST_CC] + HOST_CFLAGS + ["-o", str(output_bin)]
    for src in OTHER_SOURCES:
        cmd.append(str(src))
    cmd += HOST_LDFLAGS
    comp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=60)
    if comp.returncode != 0:
        print(comp.stdout.decode("utf-8", "replace"), flush=True)
        return "baseline build FAILED"
    run = subprocess.run([str(output_bin)], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, timeout=60)
    if run.returncode != 0:
        print(run.stdout.decode("utf-8", "replace"), flush=True)
        return "baseline test FAILED"
    return None


def worker(args):
    worker_index, chunk, original_gx, original_ts = args
    counts = {"test": 0, "build": 0, "timeout": 0, "survived": 0}
    survivors = []
    killed = []
    for tag_index, (tag, line_no, desc, gx_text, ts_text) in enumerate(chunk):
        mut_dir = MUT_DIR / f"w{worker_index}" / f"m{tag_index}"
        mut_dir.mkdir(parents=True, exist_ok=True)
        if tag == "gx_record":
            gx_path = mut_dir / "twlhd_gx_record.c"
            ts_path = TEX_SOURCE
            gx_path.write_text(gx_text, encoding="utf-8")
        else:
            gx_path = GX_RECORD
            ts_path = mut_dir / "twlhd_tex_source.c"
            ts_path.write_text(ts_text, encoding="utf-8")
        step = compile_and_run(mut_dir / "test_gx_record_tex", gx_path, ts_path)
        if step is None:
            counts["survived"] += 1
            survivors.append((line_no, desc))
        else:
            counts[step] += 1
            killed.append((line_no, desc, step))
    return {"counts": counts, "survivors": survivors, "killed": killed}


def main():
    parser = argparse.ArgumentParser(
        description="Mutation runner for 5C-2 serialize_tex + tex_source")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                        help="parallel workers")
    args = parser.parse_args()

    MUT_DIR.mkdir(parents=True, exist_ok=True)
    original_gx = GX_RECORD.read_text(encoding="utf-8")
    original_ts = TEX_SOURCE.read_text(encoding="utf-8")

    error = baseline()
    if error:
        print(error, flush=True)
        return 2
    print("baseline: build OK, C test OK", flush=True)

    try:
        mutant_list = all_mutants(original_gx, original_ts)
    except SystemExit as exc:
        print(exc, file=sys.stderr, flush=True)
        return 2

    print(f"mutants: {len(mutant_list)} unique", flush=True)
    if len(mutant_list) < 60:
        print("ERROR: fewer than 60 mutants", flush=True)
        return 2

    jobs = max(1, args.jobs)
    if jobs == 1:
        counts = {"test": 0, "build": 0, "timeout": 0, "survived": 0}
        survivors = []
        killed = []
        for index, (tag, line_no, desc, gx_text, ts_text) in enumerate(mutant_list):
            mut_dir = MUT_DIR / f"m{index}"
            mut_dir.mkdir(parents=True, exist_ok=True)
            if tag == "gx_record":
                gx_path = mut_dir / "twlhd_gx_record.c"
                ts_path = TEX_SOURCE
                gx_path.write_text(gx_text, encoding="utf-8")
            else:
                gx_path = GX_RECORD
                ts_path = mut_dir / "twlhd_tex_source.c"
                ts_path.write_text(ts_text, encoding="utf-8")
            step = compile_and_run(mut_dir / "test_gx_record_tex",
                                   gx_path, ts_path)
            if step is None:
                counts["survived"] += 1
                survivors.append((line_no, desc))
            else:
                counts[step] += 1
                killed.append((line_no, desc, step))
            print(f"[{index + 1}/{len(mutant_list)}] line {line_no} {desc} -> "
                  f"{'SURVIVED' if step is None else 'KILLED(' + step + ')'}",
                  flush=True)
        total = len(mutant_list)
    else:
        chunks = [[] for _ in range(jobs)]
        for index, mutant in enumerate(mutant_list):
            chunks[index % jobs].append(mutant)
        chunks = [chunk for chunk in chunks if chunk]
        worker_args = [(index, chunk, original_gx, original_ts)
                       for index, chunk in enumerate(chunks)]
        counts = {"test": 0, "build": 0, "timeout": 0, "survived": 0}
        survivors = []
        killed = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(chunks)) as pool:
            for result in pool.map(worker, worker_args):
                for key, value in result["counts"].items():
                    counts[key] += value
                survivors.extend(result["survivors"])
                killed.extend(result["killed"])
        total = len(mutant_list)
        for line_no, desc, step in sorted(killed):
            print(f"line {line_no} {desc} -> KILLED({step})", flush=True)

    print(flush=True)
    for key in ("test", "build", "timeout", "survived"):
        print(f"{key + ':':<10}{counts[key]}", flush=True)
    print(f"{'total:':<10}{total}", flush=True)
    for line_no, desc in sorted(survivors):
        print(f"SURVIVOR line {line_no} {desc}", flush=True)

    current_gx = GX_RECORD.read_text(encoding="utf-8")
    current_ts = TEX_SOURCE.read_text(encoding="utf-8")
    restored = current_gx == original_gx and current_ts == original_ts
    print(f"common/ restored: {'YES' if restored else 'NO'}", flush=True)
    return 0 if restored else 1


if __name__ == "__main__":
    sys.exit(main())
