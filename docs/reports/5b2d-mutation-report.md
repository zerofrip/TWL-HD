# 5B-2d mutation report: host-side PICA texture preparation

Status: **IMPLEMENTED** (repair round 1), host-side only. `make test`
green. Nothing EMULATOR or HARDWARE verified.

## Method

Every mutant is a scratch copy of the ONE file `common/twlhd_pica_tex.c`
under `/tmp/opencode/mutants5b2d/work/mNNN/` (outside the worktree).
Each mutant was compiled against the **repaired** `tests/host/test_pica_tex.c`
and the **unmodified** `tests/host/pica_tex_cli.c` with the repository
`HOST_CFLAGS` (`-std=c11 -Wall -Wextra -Werror -O1 -g -Icommon
-Itests/host -Itools/azahar_host -fsanitize=address,undefined
-fno-omit-frame-pointer`), producing exactly the two artifacts `make test`
builds and runs for this feature:

1. `tests/host/build/test_pica_tex` (the `host-test` step) — run directly;
   `FAIL pica-tex/...` labels and ASan/UBSan aborts are the killers below.
2. `tests/host/build/pica_tex_cli` + `tools/test_pica_tex_golden.py` (the
   `python3 -m unittest discover -s tools` step) — the golden test was run
   with `PICA_TEX_CLI` pointing at the mutant CLI build.

A mutant is **KILLED** when at least one of those two `make test` steps exits
non-zero. A mutant that no longer compiles under `-Werror` is also killed by
`make test` and is listed as `build`. The unmutated baseline passes both
(13/13 C tests, 3/3 golden tests).

The mutant set is the union of:

* the 58 curated mutants from the round-0 report (run-key, byte order, wrap,
  size and capacity checks),
* the 215 systematic mutants (every numeric constant +/-1 and every operator
  swap on the code lines), and
* 11 targeted round-1 mutants: the texcoords `i <= count` loop bound, the
  first/last-texel +/-4 input indexes in swizzle and unswizzle, and the
  bind-plan `i += 4u` / `tex[i - 2u]` edits in both loops.

That is 284 candidate mutants. 24 are byte-identical edits and are counted
once, leaving **260 unique mutant sources**. 15 are cross-set duplicates
between the curated and systematic sets; 9 of the 11 targeted round-1 edits
are byte-identical to a systematic mutant (the systematic set already
generated that edit) and are listed below by their alias. Two targeted edits
are new unique sources.

Rerun: `python3 /tmp/opencode/mutants5b2d/run_mutants.py` (machine-readable
`/tmp/opencode/mutants5b2d/results.json`; console log
`/tmp/opencode/mutants5b2d/run_r1.log`).

## Results

**260 unique mutants, 251 KILLED, 9 equivalent survivors.**

| Set | Unique sources | Killed by test | Killed by build | Equivalent survivors |
|---|---:|---:|---:|---:|
| curated | 58 | 58 | 0 | 0 |
| systematic | 200 | 180 | 11 | 9 |
| targeted (new sources) | 2 | 2 | 0 | 0 |

The 9 survivors are all provably equivalent (see Notes); no round-1 survivor
is an untested behaviour change.

Round-1 targeted mutants (an `(alias of ...)` result means the same edit was
already produced by the systematic set and deduplicated):

| Mutant | Unique result | Killing step | New test that kills it |
|---|---|---|---|
| `req-texcoords-loop-inclusive` (alias of `sys-137-op-<-<=-18`) | KILLED | `test_pica_tex` | `pica-tex/oob-texcoord`: exact output guard + 0x00/0xFF input tail |
| `req-swizzle-src-r-plus1` (alias of `sys-57-const-3-4`) | KILLED | `test_pica_tex` | `pica-tex/oob-swizzle`: 0x00/0xFF pass compare (last texel) |
| `req-swizzle-src-a-minus4` | KILLED | `test_pica_tex` | `pica-tex/oob-swizzle`: 0x00/0xFF pass compare (first texel) |
| `req-unswizzle-src-r-plus1` (alias of `sys-89-const-3-4`) | KILLED | `test_pica_tex` | `pica-tex/oob-swizzle`: 0x00/0xFF pass compare (last texel) |
| `req-unswizzle-src-a-minus4` | KILLED | `test_pica_tex` | `pica-tex/oob-swizzle`: 0x00/0xFF pass compare (first texel) |
| `req-bind-count-i-plus4` (alias of `sys-159-const-3-4`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: A A A B B with capacity 1 |
| `req-bind-write-i-plus4` (alias of `sys-167-const-3-4`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: A A A B B `first_vertex`/`vertex_count` |
| `req-bind-count-param-i-minus2` (alias of `sys-160-const-3-2`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: vertices 1/2 differ |
| `req-bind-count-pltt-i-minus2` (alias of `sys-161-const-3-2`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: vertices 1/2 differ |
| `req-bind-write-param-i-minus2` (alias of `sys-168-const-3-2`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: vertices 1/2 differ |
| `req-bind-write-pltt-i-minus2` (alias of `sys-169-const-3-2`) | KILLED | `test_pica_tex` | `pica-tex/bind-plan`: vertices 1/2 differ |

## Per-mutant results

| # | Mutant | Result | Killing `make test` step(s) |
|---:|---|---|---|
| 1 | `morton-x-lut-1` | KILLED | `test_pica_tex` |
| 2 | `morton-x-lut-2` | KILLED | `test_pica_tex` |
| 3 | `morton-y-lut-1` | KILLED | `test_pica_tex` |
| 4 | `morton-y-lut-2` | KILLED | `test_pica_tex` |
| 5 | `swizzle-byte-r` | KILLED | `test_pica_tex` |
| 6 | `swizzle-byte-g` (alias: `sys-58-const-2-1`) | KILLED | `test_pica_tex` |
| 7 | `swizzle-byte-b` (alias: `sys-59-const-1-2`) | KILLED | `test_pica_tex` |
| 8 | `swizzle-byte-a` | KILLED | `test_pica_tex` |
| 9 | `swizzle-no-vflip` | KILLED | `test_pica_tex` |
| 10 | `swizzle-tile-row-width` | KILLED | `test_pica_tex` |
| 11 | `swizzle-tile-col-32` | KILLED | `test_pica_tex` |
| 12 | `swizzle-morton-x-mask3` | KILLED | `test_pica_tex` |
| 13 | `swizzle-morton-y-mask3` | KILLED | `test_pica_tex` |
| 14 | `swizzle-drop-tile-col` | KILLED | `test_pica_tex` |
| 15 | `swizzle-vflip-off-by-one` | KILLED | `test_pica_tex` |
| 16 | `swizzle-dst-size-disabled` | KILLED | `test_pica_tex` |
| 17 | `swizzle-width-check-dropped` | KILLED | `test_pica_tex` |
| 18 | `size-min-8-to-gt` (alias: `sys-26-op->=->-13`) | KILLED | `test_pica_tex` |
| 19 | `size-max-1024-to-512` | KILLED | `test_pica_tex` |
| 20 | `size-pow2-disabled` | KILLED | `test_pica_tex` |
| 21 | `byte-size-times-3` (alias: `sys-30-const-4-3`) | KILLED | `test_pica_tex` |
| 22 | `unswizzle-byte-order` | KILLED | `test_pica_tex` |
| 23 | `unswizzle-no-vflip` | KILLED | `test_pica_tex` |
| 24 | `unswizzle-tile-row-width` | KILLED | `test_pica_tex` |
| 25 | `unswizzle-dst-size-disabled` | KILLED | `test_pica_tex` |
| 26 | `unswizzle-drop-tile-col` | KILLED | `test_pica_tex` |
| 27 | `wrap-repeat-s-shift` (alias: `sys-101-const-16-17`) | KILLED | `test_pica_tex` |
| 28 | `wrap-repeat-t-shift` (alias: `sys-102-const-17-16`) | KILLED | `test_pica_tex` |
| 29 | `wrap-flip-s-shift` (alias: `sys-103-const-18-19`) | KILLED | `test_pica_tex` |
| 30 | `wrap-flip-t-shift` (alias: `sys-104-const-19-18`) | KILLED | `test_pica_tex` |
| 31 | `wrap-s-mirror-or` (alias: `sys-108-op-&&-||-17`) | KILLED | `test_pica_tex` |
| 32 | `wrap-s-mirror-as-repeat` | KILLED | `test_pica_tex` |
| 33 | `wrap-s-repeat-as-clamp` | KILLED | `test_pica_tex` |
| 34 | `wrap-t-mirror-as-flip-only` | KILLED | `test_pica_tex` |
| 35 | `wrap-t-clamp-as-border` | KILLED | `test_pica_tex` |
| 36 | `wrap-null-t-dropped` | KILLED | `test_pica_tex` |
| 37 | `uv-u-from-t` | KILLED | `test_pica_tex` |
| 38 | `uv-no-vflip` | KILLED | `test_pica_tex` |
| 39 | `uv-u-divisor` | KILLED | `test_pica_tex` |
| 40 | `uv-v-divisor` | KILLED | `test_pica_tex` |
| 41 | `uv-capacity-off-by-one` (alias: `sys-135-op-<-<=-21`) | KILLED | `test_pica_tex` |
| 42 | `uv-v-sign` | KILLED | `test_pica_tex` |
| 43 | `uv-null-out-dropped` | KILLED | `test_pica_tex` |
| 44 | `bind-count-mod2` (alias: `sys-155-const-3-2`) | KILLED | `test_pica_tex` |
| 45 | `bind-count-no-pltt` | KILLED | `test_pica_tex` |
| 46 | `bind-write-no-pltt` | KILLED | `test_pica_tex` |
| 47 | `bind-capacity-ge` (alias: `sys-164-op->->=-13`) | KILLED | `test_pica_tex` |
| 48 | `bind-entry-count-4` (alias: `sys-171-const-3-4`) | KILLED | `test_pica_tex` |
| 49 | `bind-merge-6` | KILLED | `test_pica_tex` |
| 50 | `bind-untextured-eq-1` (alias: `sys-175-const-0-1`) | KILLED | `test_pica_tex` |
| 51 | `bind-untextured-shift25` (alias: `sys-175-const-26-25`) | KILLED | `test_pica_tex` |
| 52 | `bind-outcount-dropped` | KILLED | `test_pica_tex` |
| 53 | `bind-null-outcount-dropped` | KILLED | `test_pica_tex` |
| 54 | `bind-capacity-check-disabled` | KILLED | `test_pica_tex` |
| 55 | `bind-first-vertex-plus1` | KILLED | `test_pica_tex` |
| 56 | `bind-count-no-param` | KILLED | `test_pica_tex` |
| 57 | `bind-write-no-param` | KILLED | `test_pica_tex` |
| 58 | `result-string-size-ok` | KILLED | `test_pica_tex` |
| 59 | `sys-20-const-8-9` | SURVIVED | — (equivalent) |
| 60 | `sys-20-const-8-7` | KILLED | build (cannot compile) |
| 61 | `sys-22-const-8-9` | SURVIVED | — (equivalent) |
| 62 | `sys-22-const-8-7` | KILLED | build (cannot compile) |
| 63 | `sys-26-const-8-9` | KILLED | `test_pica_tex` |
| 64 | `sys-26-const-8-7` | SURVIVED | — (equivalent) |
| 65 | `sys-26-const-1024-1025` | SURVIVED | — (equivalent) |
| 66 | `sys-26-const-1024-1023` | KILLED | `test_pica_tex` |
| 67 | `sys-26-const-1-0` | KILLED | `test_pica_tex` |
| 68 | `sys-26-const-1-2` | SURVIVED | — (equivalent) |
| 69 | `sys-26-const-0-1` | KILLED | `test_pica_tex` |
| 70 | `sys-26-op-&-|-39` | KILLED | `test_pica_tex` |
| 71 | `sys-26-op-==-!=-51` | KILLED | `test_pica_tex` |
| 72 | `sys-26-op-<=-<-24` | KILLED | `test_pica_tex` |
| 73 | `sys-26-op-&&-||-19` | KILLED | build (cannot compile) |
| 74 | `sys-26-op-&&-||-33` | KILLED | build (cannot compile) |
| 75 | `sys-30-const-4-5` | KILLED | `test_pica_tex` |
| 76 | `sys-38-op-||-&&-13` | KILLED | `test_pica_tex` |
| 77 | `sys-39-op-||-&&-33` | KILLED | `test_pica_tex` |
| 78 | `sys-41-op-<-<=-17` | KILLED | `test_pica_tex` |
| 79 | `sys-44-const-0-1` | KILLED | `test_pica_tex` |
| 80 | `sys-44-op-<-<=-18` | KILLED | `test_pica_tex` |
| 81 | `sys-47-const-1-0` | KILLED | `test_pica_tex` |
| 82 | `sys-47-const-1-2` | KILLED | `test_pica_tex` |
| 83 | `sys-48-const-3-2` | KILLED | `test_pica_tex` |
| 84 | `sys-48-const-3-4` | KILLED | `test_pica_tex` |
| 85 | `sys-48-const-8-9` | KILLED | `test_pica_tex` |
| 86 | `sys-48-const-8-7` | KILLED | `test_pica_tex` |
| 87 | `sys-48-op->>-<<-32` | KILLED | `test_pica_tex` |
| 88 | `sys-50-const-0-1` | KILLED | `test_pica_tex` |
| 89 | `sys-50-op-<-<=-22` | KILLED | `test_pica_tex` |
| 90 | `sys-52-const-7-8` | KILLED | `test_pica_tex` |
| 91 | `sys-52-const-7-6` | KILLED | `test_pica_tex` |
| 92 | `sys-52-const-7-8` | KILLED | `test_pica_tex` |
| 93 | `sys-52-const-7-6` | KILLED | `test_pica_tex` |
| 94 | `sys-52-op-&-|-27` | KILLED | `test_pica_tex` |
| 95 | `sys-52-op-&-|-47` | KILLED | `test_pica_tex` |
| 96 | `sys-52-op-+---33` | KILLED | `test_pica_tex` |
| 97 | `sys-53-const-3-2` | KILLED | `test_pica_tex` |
| 98 | `sys-53-const-3-4` | KILLED | `test_pica_tex` |
| 99 | `sys-53-const-64-65` | KILLED | `test_pica_tex` |
| 100 | `sys-53-const-64-63` | KILLED | `test_pica_tex` |
| 101 | `sys-53-op->>-<<-47` | KILLED | `test_pica_tex` |
| 102 | `sys-53-op-+---42` | KILLED | `test_pica_tex` |
| 103 | `sys-53-op-+---59` | KILLED | `test_pica_tex` |
| 104 | `sys-54-const-4-3` | KILLED | `test_pica_tex` |
| 105 | `sys-54-const-4-5` | KILLED | `test_pica_tex` |
| 106 | `sys-54-op-+---37` | KILLED | `test_pica_tex` |
| 107 | `sys-55-const-4-3` | KILLED | `test_pica_tex` |
| 108 | `sys-55-const-4-5` | KILLED | `test_pica_tex` |
| 109 | `sys-57-const-0-1` | KILLED | `test_pica_tex` |
| 110 | `sys-57-const-3-2` | KILLED | `test_pica_tex` |
| 111 | `sys-57-const-3-4` (alias: `req-swizzle-src-r-plus1`) | KILLED | `test_pica_tex` |
| 112 | `sys-57-op-+---20` | SURVIVED | — (equivalent) |
| 113 | `sys-57-op-+---35` | KILLED | `test_pica_tex` |
| 114 | `sys-58-const-1-0` | KILLED | `test_pica_tex` |
| 115 | `sys-58-const-1-2` | KILLED | `test_pica_tex` |
| 116 | `sys-58-const-2-3` | KILLED | `test_pica_tex` |
| 117 | `sys-58-op-+---20` | KILLED | `test_pica_tex` |
| 118 | `sys-58-op-+---35` | KILLED | `test_pica_tex` |
| 119 | `sys-59-const-2-1` | KILLED | `test_pica_tex` |
| 120 | `sys-59-const-2-3` | KILLED | `test_pica_tex` |
| 121 | `sys-59-const-1-0` | KILLED | `test_pica_tex` |
| 122 | `sys-59-op-+---20` | KILLED | `test_pica_tex` |
| 123 | `sys-59-op-+---35` | KILLED | `test_pica_tex` |
| 124 | `sys-60-const-3-2` | KILLED | `test_pica_tex` |
| 125 | `sys-60-const-3-4` | KILLED | `test_pica_tex` |
| 126 | `sys-60-const-0-1` | KILLED | `test_pica_tex` |
| 127 | `sys-60-op-+---20` | KILLED | `test_pica_tex` |
| 128 | `sys-60-op-+---35` | SURVIVED | — (equivalent) |
| 129 | `sys-72-op-||-&&-13` | KILLED | `test_pica_tex` |
| 130 | `sys-73-op-||-&&-33` | KILLED | `test_pica_tex` |
| 131 | `sys-75-op-<-<=-17` | KILLED | `test_pica_tex` |
| 132 | `sys-78-const-0-1` | KILLED | `test_pica_tex` |
| 133 | `sys-78-op-<-<=-18` | KILLED | `test_pica_tex` |
| 134 | `sys-79-const-1-0` | KILLED | `test_pica_tex` |
| 135 | `sys-79-const-1-2` | KILLED | `test_pica_tex` |
| 136 | `sys-80-const-3-2` | KILLED | `test_pica_tex` |
| 137 | `sys-80-const-3-4` | KILLED | `test_pica_tex` |
| 138 | `sys-80-const-8-9` | KILLED | `test_pica_tex` |
| 139 | `sys-80-const-8-7` | KILLED | `test_pica_tex` |
| 140 | `sys-80-op->>-<<-32` | KILLED | `test_pica_tex` |
| 141 | `sys-82-const-0-1` | KILLED | `test_pica_tex` |
| 142 | `sys-82-op-<-<=-22` | KILLED | `test_pica_tex` |
| 143 | `sys-84-const-7-8` | KILLED | `test_pica_tex` |
| 144 | `sys-84-const-7-6` | KILLED | `test_pica_tex` |
| 145 | `sys-84-const-7-8` | KILLED | `test_pica_tex` |
| 146 | `sys-84-const-7-6` | KILLED | `test_pica_tex` |
| 147 | `sys-84-op-&-|-27` | KILLED | `test_pica_tex` |
| 148 | `sys-84-op-&-|-47` | KILLED | `test_pica_tex` |
| 149 | `sys-84-op-+---33` | KILLED | `test_pica_tex` |
| 150 | `sys-85-const-3-2` | KILLED | `test_pica_tex` |
| 151 | `sys-85-const-3-4` | KILLED | `test_pica_tex` |
| 152 | `sys-85-const-64-65` | KILLED | `test_pica_tex` |
| 153 | `sys-85-const-64-63` | KILLED | `test_pica_tex` |
| 154 | `sys-85-op->>-<<-46` | KILLED | `test_pica_tex` |
| 155 | `sys-85-op-+---41` | KILLED | `test_pica_tex` |
| 156 | `sys-85-op-+---58` | KILLED | `test_pica_tex` |
| 157 | `sys-86-const-4-3` | KILLED | `test_pica_tex` |
| 158 | `sys-86-const-4-5` | KILLED | `test_pica_tex` |
| 159 | `sys-87-const-4-3` | KILLED | `test_pica_tex` |
| 160 | `sys-87-const-4-5` | KILLED | `test_pica_tex` |
| 161 | `sys-87-op-+---38` | KILLED | `test_pica_tex` |
| 162 | `sys-89-const-0-1` | KILLED | `test_pica_tex` |
| 163 | `sys-89-const-3-2` | KILLED | `test_pica_tex` |
| 164 | `sys-89-const-3-4` (alias: `req-unswizzle-src-r-plus1`) | KILLED | `test_pica_tex` |
| 165 | `sys-89-op-+---20` | SURVIVED | — (equivalent) |
| 166 | `sys-89-op-+---35` | KILLED | `test_pica_tex` |
| 167 | `sys-90-const-1-0` | KILLED | `test_pica_tex` |
| 168 | `sys-90-const-1-2` | KILLED | `test_pica_tex` |
| 169 | `sys-90-const-2-1` | KILLED | `test_pica_tex` |
| 170 | `sys-90-const-2-3` | KILLED | `test_pica_tex` |
| 171 | `sys-90-op-+---20` | KILLED | `test_pica_tex` |
| 172 | `sys-90-op-+---35` | KILLED | `test_pica_tex` |
| 173 | `sys-91-const-2-1` | KILLED | `test_pica_tex` |
| 174 | `sys-91-const-2-3` | KILLED | `test_pica_tex` |
| 175 | `sys-91-const-1-0` | KILLED | `test_pica_tex` |
| 176 | `sys-91-const-1-2` | KILLED | `test_pica_tex` |
| 177 | `sys-91-op-+---20` | KILLED | `test_pica_tex` |
| 178 | `sys-91-op-+---35` | KILLED | `test_pica_tex` |
| 179 | `sys-92-const-3-2` | KILLED | `test_pica_tex` |
| 180 | `sys-92-const-3-4` | KILLED | `test_pica_tex` |
| 181 | `sys-92-const-0-1` | KILLED | `test_pica_tex` |
| 182 | `sys-92-op-+---20` | KILLED | `test_pica_tex` |
| 183 | `sys-92-op-+---35` | SURVIVED | — (equivalent) |
| 184 | `sys-101-const-16-15` | KILLED | `test_pica_tex` |
| 185 | `sys-101-const-1-0` | KILLED | `test_pica_tex` |
| 186 | `sys-101-const-1-2` | KILLED | `test_pica_tex` |
| 187 | `sys-101-op->>-<<-40` | KILLED | `test_pica_tex` |
| 188 | `sys-101-op-&-|-47` | KILLED | `test_pica_tex` |
| 189 | `sys-102-const-17-18` | KILLED | `test_pica_tex` |
| 190 | `sys-102-const-1-0` | KILLED | `test_pica_tex` |
| 191 | `sys-102-const-1-2` | KILLED | `test_pica_tex` |
| 192 | `sys-102-op->>-<<-40` | KILLED | `test_pica_tex` |
| 193 | `sys-102-op-&-|-47` | KILLED | `test_pica_tex` |
| 194 | `sys-103-const-18-17` | KILLED | `test_pica_tex` |
| 195 | `sys-103-const-1-0` | KILLED | `test_pica_tex` |
| 196 | `sys-103-const-1-2` | KILLED | `test_pica_tex` |
| 197 | `sys-103-op->>-<<-38` | KILLED | `test_pica_tex` |
| 198 | `sys-103-op-&-|-45` | KILLED | `test_pica_tex` |
| 199 | `sys-104-const-19-20` | KILLED | `test_pica_tex` |
| 200 | `sys-104-const-1-0` | KILLED | `test_pica_tex` |
| 201 | `sys-104-const-1-2` | KILLED | `test_pica_tex` |
| 202 | `sys-104-op->>-<<-38` | KILLED | `test_pica_tex` |
| 203 | `sys-104-op-&-|-45` | KILLED | `test_pica_tex` |
| 204 | `sys-106-op-||-&&-16` | KILLED | `test_pica_tex` |
| 205 | `sys-115-op-&&-||-17` | KILLED | `test_pica_tex` |
| 206 | `sys-132-op-||-&&-13` | KILLED | `test_pica_tex` |
| 207 | `sys-133-op-||-&&-33` | KILLED | `test_pica_tex` |
| 208 | `sys-137-const-0-1` | KILLED | `test_pica_tex` |
| 209 | `sys-137-op-<-<=-18` (alias: `req-texcoords-loop-inclusive`) | KILLED | `test_pica_tex` |
| 210 | `sys-151-const-0-1` | KILLED | `test_pica_tex` |
| 211 | `sys-154-op-||-&&-13` | KILLED | build (cannot compile) |
| 212 | `sys-154-op-||-&&-21` | KILLED | build (cannot compile) |
| 213 | `sys-155-const-3-4` | KILLED | `test_pica_tex` |
| 214 | `sys-155-const-0-1` | KILLED | `test_pica_tex` |
| 215 | `sys-155-op-!=-==-26` | KILLED | `test_pica_tex` |
| 216 | `sys-159-const-0-1` | KILLED | `test_pica_tex` |
| 217 | `sys-159-const-3-2` | KILLED | `test_pica_tex` |
| 218 | `sys-159-const-3-4` (alias: `req-bind-count-i-plus4`) | KILLED | `test_pica_tex` |
| 219 | `sys-159-op-<-<=-18` | KILLED | `test_pica_tex` |
| 220 | `sys-160-const-0-1` | KILLED | `test_pica_tex` |
| 221 | `sys-160-const-3-2` (alias: `req-bind-count-param-i-minus2`) | KILLED | `test_pica_tex` |
| 222 | `sys-160-const-3-4` | KILLED | `test_pica_tex` |
| 223 | `sys-160-op-==-!=-14` | KILLED | `test_pica_tex` |
| 224 | `sys-160-op-!=-==-45` | KILLED | `test_pica_tex` |
| 225 | `sys-160-op-||-&&-20` | KILLED | build (cannot compile) |
| 226 | `sys-160-op-||-&&-75` | KILLED | build (cannot compile) |
| 227 | `sys-161-const-3-2` (alias: `req-bind-count-pltt-i-minus2`) | KILLED | `test_pica_tex` |
| 228 | `sys-161-const-3-4` | KILLED | `test_pica_tex` |
| 229 | `sys-161-op-!=-==-29` | KILLED | `test_pica_tex` |
| 230 | `sys-166-const-0-1` | KILLED | `test_pica_tex` |
| 231 | `sys-167-const-0-1` | KILLED | `test_pica_tex` |
| 232 | `sys-167-const-3-2` | KILLED | `test_pica_tex` |
| 233 | `sys-167-const-3-4` (alias: `req-bind-write-i-plus4`) | KILLED | `test_pica_tex` |
| 234 | `sys-167-op-<-<=-18` | KILLED | `test_pica_tex` |
| 235 | `sys-168-const-0-1` | KILLED | `test_pica_tex` |
| 236 | `sys-168-const-3-2` (alias: `req-bind-write-param-i-minus2`) | KILLED | `test_pica_tex` |
| 237 | `sys-168-const-3-4` | KILLED | `test_pica_tex` |
| 238 | `sys-168-op-==-!=-14` | KILLED | `test_pica_tex` |
| 239 | `sys-168-op-!=-==-45` | KILLED | `test_pica_tex` |
| 240 | `sys-168-op-||-&&-20` | KILLED | build (cannot compile) |
| 241 | `sys-168-op-||-&&-75` | KILLED | build (cannot compile) |
| 242 | `sys-169-const-3-2` (alias: `req-bind-write-pltt-i-minus2`) | KILLED | `test_pica_tex` |
| 243 | `sys-169-const-3-4` | KILLED | `test_pica_tex` |
| 244 | `sys-169-op-!=-==-29` | KILLED | `test_pica_tex` |
| 245 | `sys-171-const-3-2` | KILLED | `test_pica_tex` |
| 246 | `sys-175-const-26-27` | KILLED | `test_pica_tex` |
| 247 | `sys-175-const-7-8` | KILLED | `test_pica_tex` |
| 248 | `sys-175-const-7-6` | KILLED | `test_pica_tex` |
| 249 | `sys-175-const-1-0` | KILLED | `test_pica_tex` |
| 250 | `sys-175-const-1-2` | KILLED | `test_pica_tex` |
| 251 | `sys-175-const-0-1` | KILLED | `test_pica_tex` |
| 252 | `sys-175-op->>-<<-40` | KILLED | `test_pica_tex` |
| 253 | `sys-175-op-&-|-47` | KILLED | build (cannot compile) |
| 254 | `sys-175-op-==-!=-53` | KILLED | `test_pica_tex` |
| 255 | `sys-178-const-1-0` | KILLED | `test_pica_tex` |
| 256 | `sys-178-const-1-2` | KILLED | `test_pica_tex` |
| 257 | `sys-178-const-3-2` | KILLED | `test_pica_tex` |
| 258 | `sys-178-const-3-4` | KILLED | `test_pica_tex` |
| 259 | `req-swizzle-src-a-minus4` | KILLED | `test_pica_tex` |
| 260 | `req-unswizzle-src-a-minus4` | KILLED | `test_pica_tex` |

## Notes

### Round-1 harness corrections (all in `tests/host/test_pica_tex.c`)

* `g_oob_coord` is now a `uint8_t` region of `OOB_GUARD + 32*sizeof(TwlhdPicaTexCoord) + OOB_GUARD` bytes, `_Alignas(8)`, with the output pointer at `region + OOB_GUARD`; *all* guard bytes are checked before and after the exact output region via `guard_intact()`. The old array was 64 *elements* (512 bytes) oversized and `OOB_COORD` advanced 64 elements, so the guard check probed unused bytes.
* `test_swizzle_out_of_bounds()` and `test_texcoord_out_of_bounds()` now run the same payload in both passes with 0x00 vs 0xFF guard fills and `memcmp` the outputs; `tex[-1]`/`tex[count]` and first/last-texel source over-reads therefore change the output.
* The texcoord input is a `uint8_t` region of `OOB_GUARD + 32*sizeof(TwlhdGxTexVertex) + OOB_GUARD` bytes, aligned, with the input pointer at `region + OOB_GUARD`; the input-unmodified snapshot compares the exact region.
* Bind-plan tests now cover: only vertex 0 as the run key (vertices 1/2 carry different state, both merge and split directions), runs of three or more triangles (A A A B B, `first_vertex == 9`), only-`pltt_base` and only-`teximage_param` changes, and a capacity-1 buffer that must stay byte-identical to 0x5B.
* New boundary checks: swizzle width 0 and height 12/2048, unswizzle width 4/12/0 and height 0/12/2048 (line 73 size validation had no direct test), and texcoords `(8, 12)` / `(8, 0)`. The valid width + invalid height pairs kill the `||` -> `&&` mutants at lines 39, 73 and 133.

### Equivalent survivors

* `sys-20-const-8-9`: `kMortonX[8]` -> `[9]`: element 8 is never read (`x & 7u` <= 7).
* `sys-22-const-8-9`: `kMortonY[8]` -> `[9]`: element 8 is never read (`vy & 7u` <= 7).
* `sys-26-const-8-7`: `v >= 8u` -> `v >= 7u`: 7 is not a power of two, so the pow2 term already rejects it; accepted set unchanged.
* `sys-26-const-1024-1025`: `v <= 1024u` -> `v <= 1025u`: 1025 is not a power of two and no power of two lies in (1024,1025], so accepted set unchanged.
* `sys-26-const-1-2`: `(v & (v - 1u))` -> `(v & (v - 2u))`: exhaustive over v in [8,1024] shows exactly the powers of two satisfy both forms.
* `sys-57-op-+---20`: `out + 0u` -> `out - 0u`: unsigned zero, identical arithmetic.
* `sys-60-op-+---35`: `in + 0u` -> `in - 0u`: unsigned zero, identical arithmetic.
* `sys-89-op-+---20`: `out + 0u` -> `out - 0u`: unsigned zero, identical arithmetic.
* `sys-92-op-+---35`: `in + 0u` -> `in - 0u`: unsigned zero, identical arithmetic.

`kMortonX`/`kMortonY` index 8 is never read because both indexes are masked with `& 7u`.
The `size_ok` constants are equivalent only on the pow2 domain: 7 and 1025 are
not powers of two, and exhaustive checking of `v & (v - 2u) == 0` over
[8, 1024] yields exactly the powers of two. `+ 0u` and `- 0u` are the same
unsigned arithmetic. None of these can change accepted sizes, indexes, or output.

### Orientation fix addendum (5B-2d repair round)

Rerun command: `python3 docs/reports/5b2d2_mutate_pica_tex.py --jobs 4`.

The row flip in `common/twlhd_pica_tex.c` was removed (memory tile rows are now
image rows top-down). The runner targets only `common/twlhd_pica_tex.c` and
uses the same artifacts as `make test`:

* `tests/host/build/test_pica_tex` — run directly;
* `tests/host/build/pica_tex_cli` + `tools/test_pica_tex_golden.py`.

**Results: 202 unique mutants, 197 killed, 5 equivalent survivors.**

Verbatim runner totals:

```text
killed by test:   187
killed by golden: 0
killed by build:  10
survived:         5
total:            202
targeted swizzle-restore-vflip: KILLED(test)
targeted unswizzle-restore-vflip: KILLED(test)
survivors:
  line 27 const 8 -> 9
  line 29 const 8 -> 9
  line 33 const 1 -> 2
  line 33 const 1024 -> 1025
  line 33 const 8 -> 7
common/ restored: YES
```

Per-set split (systematic + targeted = total):

| Set | Unique sources | Killed by test | Killed by golden | Killed by build | Survivors |
|---|---|---:|---:|---:|---:|
| systematic | 200 | 185 | 0 | 10 | 5 |
| targeted | 2 | 2 | 0 | 0 | 0 |
| total | 202 | 187 | 0 | 10 | 5 |

Targeted mutants (the systematic ±1/op-swap generator cannot produce the
`height-1-y` row flip):

| Mutant | Result | Killing step | New test that kills it |
|---|---|---|---|
| `swizzle-restore-vflip` | KILLED | `test_pica_tex` | `pica-tex/orientation-rows`: row 0 red must land on Morton indexes 0..21 |
| `unswizzle-restore-vflip` | KILLED | `test_pica_tex` | `pica-tex/roundtrip-*`: unswizzling with a restored flip does not round-trip |

Equivalent survivors:

* `line 27 const 8 -> 9`: `kMortonX[8]` -> `[9]`: element 8 is never read (`x & 7u` <= 7).
* `line 29 const 8 -> 9`: `kMortonY[8]` -> `[9]`: element 8 is never read (`y & 7u` <= 7).
* `line 33 const 8 -> 7`: `v >= 8u` -> `v >= 7u`: 7 is not a power of two, so the pow2 term already rejects it; accepted set unchanged.
* `line 33 const 1024 -> 1025`: `v <= 1024u` -> `v <= 1025u`: 1025 is not a power of two and no power of two lies in (1024,1025], so accepted set unchanged.
* `line 33 const 1 -> 2`: `(v & (v - 1u))` -> `(v & (v - 2u))`: exhaustive over v in [8,1024] shows exactly the powers of two satisfy both forms.

### Other notes

* The counting-loop `i += 4u` mutant is killed by the A A A B B capacity-1 case: it undercounts to one run, passes the gate, and the write loop then overruns the 1-entry caller buffer under ASan. The write-loop `i += 4u` mutant is killed by the same layout at capacity 4 (`first_vertex`/`vertex_count`).
* `tex[i - 3u] -> tex[i - 2u]` is killed only by the non-uniform-vertex triangle: with the old per-triangle-uniform tests vertex 1 was value-identical to vertex 0, so the mutant was equivalent under the suite.
* `bind-untextured-shift25` is not equivalent: shifting by 25 instead of 26 reads `SizeShiftT` bit 2 as PackedFormat bit 0, so a format-0 texture with `SizeShiftT = 4` would be treated as textured; the test pins `TEXIMAGE_PARAM = 0x02000000`.
* Mutating comment text was not used and does not count.
* 11 mutants are killed by the `-Werror` build rather than a test: two shrunken Morton-array declarations with excess initializers, eight `&&`/`||` precedence warnings, and one tautological compare. They are counted as killed because `make test` fails on them.

