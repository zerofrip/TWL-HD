<!-- SPDX-License-Identifier: Apache-2.0 -->
# CUBES=64 AUTODUMP_FRAME=120 branch-to-0 diagnosis

**Scope:** DIAGNOSIS + EMULATOR only. No product code, ROM source, test,
interpreter, renderer, harness or DeSmuME tree was modified; the only file this
task adds to the repository is this report. A scratch copy of DeSmuME was used
outside the repository (`/home/one/3ds/hwtest-staging/desmume-dbg-build`), built
from the scratch tree documented in `docs/reports/bkpt-diagnosis.md` section 1.
No console, SD-card or network action was performed.

**Labels.** FACT = read at the cited `file:line` or a command output quoted
below. HYPOTHESIS = an inference from facts that was not directly measured.
UNVERIFIED = no local source exists / it was not run. Emulator facts are facts
about the *model*, never about hardware.

---

## 1. What is at `common/twlhd_tex_capture.c:208` / `:215` / `:264` in the failing build

### Build equivalence

The ELF and ROM were rebuilt with the documented recipe:

```
make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64
```

(`docs/EMULATOR_VALIDATION.md:107`; `arm9v2/Makefile:60-75,116-129`).

**FACT:** the rebuilt ROM’s `ROM crc:` line is `1B79DB9D`, matching the
patch-free rebuild logged in `docs/reports/bkpt-diagnosis.md:379`.

**FACT:** running the rebuilt ROM through the scratch diagnostic CLI reproduces
the register/ring dump recorded in `docs/reports/bkpt-diagnosis.md:166-167`
exactly in two independent runs (see section 2).

### Source lines at `:208`, `:215` and `:264`

**FACT:** the current source at the cited lines is:

```c
// common/twlhd_tex_capture.c:207-215
            if (key < key_count) {
                if (last_write_seq[key] != write_seq) {
                    stats->conflicts_same_key++;
                    last_write_seq[key] = write_seq;
                }
                continue;
            }

            stats->keys_seen++;
```

```c
// common/twlhd_tex_capture.c:262-264
    stats->keys_kept = kept;
    out->record_count = kept;
    return twlhd_tex_capture_layout(out);
}
```

`:208` is therefore the conflict check `if (last_write_seq[key] != write_seq)`,
`:215` is `stats->keys_seen++;`, and `:264` is the closing brace of
`twlhd_tex_capture_plan` after the `return twlhd_tex_capture_layout(out);` at
`:263`. None of these is a branch-to-0 or indirect-call site.

### The `:208` / `:215` vs `:264` addr2line mismatch

**FACT:** `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf`
on the three PCs from the ring gives:

```
twlhd_tex_capture_plan
/tmp/codex-20261117-234810-212130/worktree/arm9v2/../common/twlhd_tex_capture.c:264
twlhd_tex_capture_plan
/tmp/codex-20261117-234810-212130/worktree/arm9v2/../common/twlhd_tex_capture.c:263 (discriminator 1)
twlhd_tex_capture_plan
/tmp/codex-20261117-234810-212130/worktree/arm9v2/../common/twlhd_tex_capture.c:263 (discriminator 1)
```

The same output is produced with and without the `-i` inline flag. `0x0200269A`
(the `pop {pc}` epilogue) maps to `:264`; `0x02002766` and `0x02002767` (the
`bl twlhd_tex_capture_layout` / following branch) map to `:263`.

**FACT:** `git log --oneline --all -- common/twlhd_tex_capture.c` shows only
one commit for this file (`0d29090`), and the lines quoted above are the same
lines in that commit. There is no source revision in the repository that places
`:208` or `:215` at the addresses `0x0200269A` / `0x02002766` / `0x02002767`.

**HYPOTHESIS:** the `:208` / `:215` attributions recorded in
`docs/reports/bkpt-diagnosis.md:253` and echoed in `docs/EQ_SCENES.md` were an
earlier wrong attribution, not a different source revision or a different
`addr2line` flag. The evidence supports this hypothesis but does not prove that
no other ELF was ever used; the alternative (a different, now-unavailable build)
is therefore **UNVERIFIED**.

### Disassembly at the branch site and LR

`arm-none-eabi-nm -n` shows the function layout:

```
02002350 T twlhd_gx_rec_serialize_tex
020025d0 T twlhd_tex_capture_plan
02002adc T twlhd_tex_capture_apply
```

**FACT:** `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf`
on the three PCs from the ring gives the `:263`/`:264` lines shown above. The
same result is obtained with `-i`.

**FACT:** the instruction at `0x0200269A` is:

```
0200269a:	bdf0       \tpop\t{r4, r5, r6, r7, pc}
```

(`arm-none-eabi-objdump -d` of the rebuilt ELF; quoted from the output.)

It is therefore a **function return**, not an indirect call or a computed
`bx rN`. The register that becomes `0` is **PC**, loaded from the saved LR slot
on the stack by the `pop {pc}`. In the register dump every general-purpose
register except `r1`, `r3` and `r12` is `0`; the value that matters for the
branch is the word the epilogue pops into PC.

### Where the zero comes from

**FACT:** `twlhd_tex_capture_plan` pushes the link register on entry
(`push {r4, r5, r6, r7, lr}` at `0x020025d0`) and the matching epilogue at
`0x0200269A` pops it into PC. The saved-LR stack slot contained `0` by the time
the epilogue executed.

**FACT:** the function allocates a 1372-byte stack frame
(`ldr r4, [pc, #672] ; .word 0xfffffaa4` then `add sp, r4` at
`0x020025dc-0x020025e0`; `0xfffffaa4` = −1372). The local arrays are
`last_write_seq[256]` (`common/twlhd_tex_capture.c:149`, 1024 bytes) and
`drop[256]` (`common/twlhd_tex_capture.c:149`, 256 bytes), totalling 1280 bytes,
plus the remaining locals.

**FACT:** the PC ring shows the planner executed its full body and made an
ordinary call to `twlhd_tex_capture_layout`:

```
TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
```

Disassembling the planner tail confirms this reading:

```
02002762:	f7ff ff05 \tbl\t2002570 <twlhd_tex_capture_layout>
02002766:	e791      \tb.n\t200268c <twlhd_tex_capture_plan+0xbc>
...
0200269a:	bdf0      \tpop\t{r4, r5, r6, r7, pc}
```

The planner ran to the `return twlhd_tex_capture_layout(out);` at `:263`, called
`twlhd_tex_capture_layout`, got control back at `0x02002766`, fell through to the
shared epilogue, and the `pop {pc}` loaded a saved LR that was already `0`.

**HYPOTHESIS:** the saved LR slot was `0` from the moment the function was
entered, because control reached `twlhd_tex_capture_plan` through a branch that
did not set LR (a `bx` or `mov pc` rather than a `bl`). The function then ran
correctly to completion; no stack corruption is required to explain the zeroed
return address. This hypothesis is **consistent with** the ring evidence but is
not proven, because the entry origin was not captured.

**HYPOTHESIS (alternative, now weaker):** the saved LR was overwritten by an
out-of-bounds write to the local arrays. This is still possible in principle,
but it is **less consistent with** the ring evidence, which shows a normal
return from `twlhd_tex_capture_layout` and a clean epilogue rather than a crash
mid-loop.

**UNVERIFIED:** the exact origin PC of the entry into `twlhd_tex_capture_plan`
and whether LR was already `0` at entry or was overwritten later.

## 2. Reproduction, stack and working-scene contrast

### 2.1 Instrumented CLI runs

All runs used:

```
DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli
DESMUME_TIMEOUT=30
tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds <outdir>
```

| run | `ROM crc` | `ARM OP_BKPT` | frame | pc | sp | lr | ring matches `bkpt-diagnosis.md:166-167` |
|---|---|---|---|---|---|---|---|
| run1 | `1B79DB9D` | yes | 129 | `01FF853C` | `02FF43A8` | `02002767` | yes |
| run2 | `1B79DB9D` | yes | 129 | `01FF853C` | `02FF43A8` | `02002767` | yes |
| pc-trace run | `1B79DB9D` | yes | 129 | `01FF853C` | `02FF43A8` | `02002767` | yes |

**FACT:** the register dump is identical across runs and identical to the
recorded run:

```
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
TWLHD_BKPT_REGS r0=00000000 r1=00021000 r2=00000000 r3=0000055C r4=00000000 r5=00000000 r6=00000000 r7=00000000 r8=00000000 r9=00000000 r10=00000000 r11=00000000 r12=02FF3E68
TWLHD_BKPT_STACK 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
```

**FACT:** the autodump file is written successfully at frame 120 before the
fault. Validation shows:

```
result=OK
entries=5737
rsta=4
```

and the file size is 46056 bytes. The fault is therefore **after** the dump,
at frame 129.

### 2.2 Stack and static layout

**FACT:** from the rebuilt `.map`:

```
0x020013c0                        PROVIDE (__main_start = ...)
0x0201ddfc                        PROVIDE (__main_end = ...)
0x02060bf8                        PROVIDE (__main_bss_end = ...)
```

Static RAM use (code + initialized data + BSS) =
`0x02060bf8 - 0x020013c0 = 0x5f838 = 391,224` bytes.

**FACT:** the large static objects in this ROM are:

```
0201de20 00021060 b _ZL11g_serialize   ; 135,264 B
0203ee80 00021030 b g_recorder         ; 135,216 B
```

(`arm9v2/build-autodump-c64/twlhd-arm9v2-autodump-c64.map` and
`arm-none-eabi-nm -S`.)

**FACT:** the devkitARM linker script places DTCM at
`0x02ff0000, LENGTH = 16K` (`/opt/devkitpro/devkitARM/arm-none-eabi/lib/ds_arm9.mem`).
The ELF contains:

```
02ff3e80 d __sp_usr
02ff3f80 a __sp_irq
02ff3fc0 a __sp_svc
```

**FACT:** the fault SP is `0x02ff43a8`. Relative to the linker-script values:

- `0x02ff43a8 - 0x02ff3e80 = 0x528 = 1320` bytes above `__sp_usr`.
- `0x02ff43a8 - 0x02ff4000 = 0x3a8 = 936` bytes above the nominal DTCM top.

**HYPOTHESIS:** the observed SP reflects an abnormal entry into
`twlhd_tex_capture_plan`. The CPU is in SYS mode (`cpsr=6000001F`), not an
exception mode, so this is not a normal IRQ/SVC stack switch. Possibilities
include: (a) the function was entered with SP already above `__sp_usr` because a
previous branch or return left a bad SP; (b) the frame was placed over the
IRQ/SVC stack region because runtime DTCM/CP15 configuration differs from the
linker script; or (c) the SP underflowed upward through the user stack top. The
mechanism is **UNVERIFIED**.

**FACT:** the measured addresses are:

- stack symbols in DTCM: `__sp_usr 0x02ff3e80`, `__sp_irq 0x02ff3f80`,
  `__sp_svc 0x02ff3fc0`;
- `.bss`/heap objects in EWRAM: `g_serialize 0x0201de20`, `g_recorder 0x0203ee80`.

**HYPOTHESIS:** stack overflow into `.bss`/heap is not plausible, because the
stack lives in DTCM (`0x02ff0000` region) while `.bss`/heap and the large static
buffers (`g_serialize`, `g_recorder`) live in EWRAM (`0x02000000` region). The
EWRAM region is 4 MiB − 512 KiB = 3,670,016 bytes; only 391,224 bytes are used,
leaving ~3.28 MiB free.

**FACT:** register values at the fault are `r12=0x02ff3e68` and `r3=0x55C`.
`0x02ff3e68 = 0x02ff3e80 − 0x18`, i.e. `r12` is `__sp_usr − 24` bytes. `0x55C =
1372`, which equals the negation of the prologue frame constant `0xfffffaa4`
(`−1372`). `r3` is therefore the 1372-byte stack frame size; this is consistent
with `twlhd_tex_capture_plan` having just executed its prologue with a logical
frame size of 1372 bytes (the corrected figure; see section 1). The meaning of
`r12` is **UNVERIFIED**: it may be a stale pointer to just below the user stack
top, or it may record the entry SP before the 1372-byte frame was allocated.

### 2.3 Called vs wild entry

**FACT:** `dump_frame` in the CUBES=64 ROM calls only the non-texture
serializer:

```
02001fb4:	f000 f99a \tbl\t20022ec <twlhd_gx_rec_serialize>
```

**FACT:** the only direct `bl` to `twlhd_tex_capture_plan` is the one inside
`twlhd_gx_rec_serialize_tex` at `0x020023a0`; there is **no** `bl` to
`twlhd_gx_rec_serialize_tex` from any other function in `.main`.

**FACT:** `arm9v2/source/main.cpp:109-129` gates the texture path with
`#if TWLHD_DEMO_SCENE == 5`; for the CUBES=64 build `SCENE` is unset/0, so
`twlhd_gx_rec_serialize_tex` is not called from C source.

**FACT:** an indirect-call scan of the rebuilt ELF found no reference to
`twlhd_tex_capture_plan` or `twlhd_gx_rec_serialize_tex` outside the single
direct `bl`:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E 'bl\s+20025(d0|d1)'
 20023a0:	f000 f916 \tbl\t20025d0 <twlhd_tex_capture_plan>

$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E 'bl\s+20023(50|51)'
(no output)

$ # blx rN / bx rN that might reach the plan (returns excluded)
$ arm-none-eabi-objdump -d ... | grep -E '\b47[89abcdef]8\b|\b47[0-9a-f]{2}\b' | grep -v '4770'
 20013ec:	4718      \tbx\tr3          <- libnds consolePrintChar
 200444a:	4798      \tblx\tr3        <- libnds consolePrintChar
 200293c:	47a0      \tblx\tr4        <- cap_classify_key read callback
 2002974:	47a0      \tblx\tr4        <- cap_classify_key read callback
 2002a08:	47a0      \tblx\tr4        <- cap_classify_key read callback
 2002a66:	47b0      \tblx\tr6        <- cap_fmt5_window read callback
 2002bf8:	47a0      \tblx\tr4        <- twlhd_tex_capture_apply read callback
 ... (remaining blx are inside twlhd_tex_capture_apply)

$ # literal-pool words equal to the Thumb entry addresses + 1
$ grep -iE '020025d1|02002351|d1250002|51230002' c64_disasm.txt
(no output)
```

**FACT:** the 4 M-record PC trace (`TWLHD_PCLOG`) captured the first
4,000,000 ARM9 instructions from reset. It contains **no** execution in
`twlhd_tex_capture_plan` (`0x020025d0-0x02002adc`) and **no** execution in
`twlhd_gx_rec_serialize_tex` proper (`0x02002350-0x020024a4`); the trace limit
was reached long before frame 129, so it cannot directly show the entry into the
plan.

**HYPOTHESIS:** the CPU wild-jumped into the dead-but-linked texture code. The
static call graph and the indirect-call scan above show no legitimate caller,
and the return-style branch to `0` is consistent with entering the function with
a saved LR of `0` (a branch that did not set LR). The ring evidence (section 1)
shows the planner then ran correctly to completion, including an ordinary call
to `twlhd_tex_capture_layout` and a normal return, so the entry must have
supplied enough valid state (parameters, stack layout) for the function body to
execute.

**UNVERIFIED:** the exact origin PC of the wild jump (which instruction branched
into `twlhd_tex_capture_plan`) was not captured.

### 2.4 What differs at frame 129 vs the working scenes

**FACT:** at frame 120 the ROM writes a valid 46056-byte v2 file (5737 GXCM + 4
RSTA entries) via `twlhd_gx_rec_serialize`, `fatInitDefault`, `fopen` and
`fwrite` (`arm9v2/source/main.cpp:127-155`).

**FACT:** at frame 129 the fault occurs in `twlhd_tex_capture_plan`, a function
that the CUBES=64 ROM never calls legitimately (no direct `bl` and no indirect
reference found by the scan in section 2.3).

**FACT:** the CUBES=64 stream contains no texture commands (`TEXIMAGE_PARAM`
count = 0, `PLTT_BASE` count = 0 in the captured file). Even if the texture path
had been called legitimately, `cap_format(param) == 0` at
`common/twlhd_tex_capture.c:199` would cause the planner to see `keys_seen = 0`
and return `TWLHD_TEX_CAP_OK` after only the GXCM scan.

**HYPOTHESIS:** the fault at frame 129 is not triggered by the *content* of the
capture (the stream is byte-valid and untextured) but by the emulator reaching
dead code through a wild jump. The difference between frame 120 and frame 129 is
simply that enough time/execution has passed for the wild jump to occur, not
that the second dump attempt or libfat state changed in a meaningful way. The
ring evidence (section 1) is consistent with this: the planner ran correctly to
completion after the abnormal entry, then returned to address `0`.

## 3. Host side

### 3.1 `make test` baseline

**FACT:** `make test` passes with the auto-detected ASAN/UBSAN build
(`Makefile:40-48`). Relevant host binaries:

```
tests/host/build/test_tex_capture
tests/host/build/test_gx_record_tex
tests/host/build/tex_capture_cli
```

All pass (`14 test(s) passed`, `19 test(s) passed`).

### 3.2 Replay of the CUBES=64 capture through the planner

A scratch Python driver (`/tmp/opencode/replay_c64.py`, not committed) parsed the
46056-byte captured file and fed its 5737 GXCM entries into
`tests/host/build/tex_capture_cli`.

**FACT:** real output:

```
result=OK
keys_seen=0
keys_kept=0
keys_dropped_budget=0
keys_dropped_read_error=0
keys_dropped_unsupported=0
conflicts_same_key=0
bytes_txdt=0
bytes_pldt=0
overflow=0
record_count=0
```

This is the expected result for an untextured stream: the planner skips every
vertex because `cap_format(param) == 0` (`common/twlhd_tex_capture.c:199`).

### 3.3 Attempt to reproduce a NULL/garbage jump on the host

**FACT:** feeding `twlhd_tex_capture_plan` a NULL `read` callback returns
`TWLHD_TEX_CAP_ERR_NULL` (`common/twlhd_tex_capture.c:163`). Real output from a
scratch host driver in `/tmp/opencode/`:

```
$ cc -std=c11 -Wall -Wextra -Icommon -o /tmp/opencode/null_read_test \
    /tmp/opencode/null_read_test.c common/twlhd_tex_capture.c common/twlhd_v2_parse.c -lm
$ /tmp/opencode/null_read_test
twlhd_tex_capture_plan(read=NULL) -> 1 (ERR_NULL)
```

The same guard is asserted by the existing unit test
`tests/host/test_tex_capture.c:858-860`, and `tests/host/build/test_tex_capture`
reports `14 test(s) passed`.

**FACT:** replaying the captured CUBES=64 stream through the planner under the
`make test` ASAN/UBSAN build produces no memory errors and no undefined behaviour.
Real output:

```
$ python3 /tmp/opencode/replay_c64.py /tmp/opencode/c64_capture/twlhd_frame_v2.bin | tests/host/build/tex_capture_cli
version=2 header_size=32 chunks=3 total_size=46056 flags=1
...
gxcm entries=5737
result=OK
limits txdt=131072 pldt=32768 records=256
keys_seen=0
keys_kept=0
keys_dropped_budget=0
keys_dropped_read_error=0
keys_dropped_unsupported=0
conflicts_same_key=0
bytes_txdt=0
bytes_pldt=0
overflow=0
record_count=0
txdt 0

pldt 0
```

(`tests/host/build/tex_capture_cli` is linked with `-fsanitize=address,undefined`
by the `make test` recipe; `Makefile:40-48`.)

**CONCLUSION (HYPOTHESIS):** the CUBES=64 ROM inputs do **not** reproduce the
branch-to-0 on the host. The host planner behaves correctly on the real captured
stream. This makes a NULL/corrupted `read` callback or a planner logic bug an
unlikely explanation for the emulator fault.

## 4. Hardware relevance

**FACT:** no CUBES=64 AUTODUMP_FRAME=120 hardware run is recorded in the
repository or in the external logs.

Searches:

```
/home/one/3ds/hwtest-staging/RUN2_RESULTS.md
/home/one/3ds/hwtest-staging/CONSOLE_ACTION_LOG.txt
docs/ (repo-wide grep)
```

found no row for `twlhd-arm9v2-autodump-c64.nds` or a `CUBES=64` DS ROM run.
The only cube-related hardware observations are 3DS ARM11 renderer runs
(100/170 cubes) in `docs/HARDWARE_TEST_PROCEDURE_3DS.md`, which are unrelated to
the DS capture ROM.

**CONCLUSION:** no hardware run exists for this ROM variant.

---

## 5. Conclusion, proposed fix and regression test

### 5.1 Assessment of the two hypotheses

- **H-real** (a real ROM defect in `twlhd_tex_capture_plan`): the planner is
  **dead code** in the CUBES=64 build. There is no direct `bl` to it from
  `dump_frame` or from any other ROM code, and the indirect-call scan in section
  2.3 found no other reference. The captured stream is byte-valid and untextured,
  and the same stream passes cleanly on the host under ASAN/UBSAN. These facts
  make H-real unlikely.

- **H-dead** (a wild jump into dead-but-linked texture code): consistent with
  the measurements, but the mechanism is **UNVERIFIED**. The function is present
  only because `common/` is fully linked into every arm9v2 build with no
  `--gc-sections` (`arm9v2/Makefile:26-28,46-47`). The ring evidence (section 1)
  shows the planner ran correctly to completion, including an ordinary call to
  `twlhd_tex_capture_layout` and a normal return, before the epilogue loaded a
  saved LR that was already `0`. This fits an entry with LR=`0` better than it
  fits stack corruption mid-execution, but the entry origin was not captured.

### 5.2 Smallest proposed fix

**Proposed mitigation (not a root-cause fix):** in the next build/variant stage,
compile the non-texture arm9v2 variants so that the texture-capture path is not
present in the ROM at all. The least invasive way is to enable
`-ffunction-sections` and `-Wl,--gc-sections` for the arm9v2 build, or to exclude
`twlhd_tex_capture.c`, `twlhd_tex_source.c` and the texture-aware serialize path
from `SOURCES` when `TWLHD_DEMO_SCENE != 5`. This removes the dead-code target
that the wild jump hit, without changing `common/` semantics. It is a
**symptom-hiding mitigation only**: it does not fix the unknown cause of the bad
jump or the bad SP.

**Root-cause path (diagnosis stage, before any build change):** settle the entry
origin and the SP anomaly with a scratch DeSmuME-side experiment (same patch
style as `bkpt-register-dump.patch`, outside the repository):

1. Set a watchpoint or trace on first execution of `0x020025d0`
   (`twlhd_tex_capture_plan` entry), capturing entry SP, LR and mode.
2. Set a watchpoint on the saved-LR stack slot to see when it becomes `0`.
3. Capture a longer `TWLHD_PCLOG` covering frame 129 to identify the branch that
   entered the plan.

The owner of this diagnostic step is the **emulator/harness diagnosis stage**.
Once the origin is known, the fix stage is:

- **arm9v2/common build stage** if the cause is a linkage/entry-path defect in
  our build (e.g. a missing `noreturn`, a bad function pointer, or a section
  placement issue);
- **product code stage** if the cause is a real ROM defect that could also occur
  on hardware;
- **capture-flow / DeSmuME stage** if the cause is an emulator-side artefact
  (compare `bkpt-diagnosis.md` §5 item 2 and the scene2 DLDI autopatch
  discriminator).

**Alternative / immediate mitigation:** keep the CUBES=64 AUTODUMP_FRAME=120
DeSmuME capture marked as NOT-COMPARABLE in the capture flow (documentation and
`tools/emulator/desmume_capture.sh` reporting), and rely on the harness geometry
dump and the host planner replay for validation. This is the smallest change
that matches the evidence while the emulator-side instability is unresolved.

### 5.3 Regression test idea

Add a host build-verification test that builds `CUBES=64 AUTODUMP_FRAME=120` and
runs `arm-none-eabi-nm` on the resulting ELF. The test fails if either
`twlhd_tex_capture_plan` or `twlhd_gx_rec_serialize_tex` is defined in the
symbol table. Before the fix both symbols are present; after the fix they are
absent from non-texture ROMs. A complementary host unit test feeds the real
CUBES=64 GXCM stream to `twlhd_tex_capture_plan` and asserts `result=OK` with
`keys_seen=0` (already passes today, so it protects against a future change that
breaks the untextured path).

---

## CAUSE

**FACT:** under DeSmuME the CUBES=64 AUTODUMP_FRAME=120 ROM branches to address
`0` at frame 129 and lands in the ITCM reset slot `0x01FF853C`
(`__arm_excpt_rst`, `bkpt 0x8000`). **FACT:** the instruction that loaded PC=`0`
is the function return `pop {r4, r5, r6, r7, pc}` at `0x0200269A` inside
`twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:263-264`); the rebuilt ROM
reproduces the recorded register dump exactly (`ROM crc 1B79DB9D`,
`pc=01FF853C`, `sp=02FF43A8`, `lr=02002767`). **FACT:** the PC ring and
disassembly show the planner ran to its final `return twlhd_tex_capture_layout(out);`,
called `twlhd_tex_capture_layout`, returned,
and then the epilogue popped a saved LR that was already `0`. **HYPOTHESIS:** a
wild jump entered `twlhd_tex_capture_plan` with the saved-LR slot set to `0`
(because the entry branch did not set LR); the function then executed correctly
to completion. **HYPOTHESIS:** the planner is dead-but-linked code in the
CUBES=64 build (no direct `bl` and no indirect reference found by the scan in
section 2.3). **UNVERIFIED:** the exact instruction that entered
`twlhd_tex_capture_plan`, whether LR was already `0` at entry, the reason the
entry SP was above `__sp_usr`/DTCM top, and whether real hardware could take the
same path.

---

## Open items

1. **UNVERIFIED:** the exact origin PC of the entry into
   `twlhd_tex_capture_plan`. A watchpoint or longer `TWLHD_PCLOG` covering frame
   129 is needed.

2. **UNVERIFIED:** whether the saved LR slot was `0` from entry or was
   overwritten later. The ring evidence (section 1) makes the entry-LR=`0`
   hypothesis more plausible than stack corruption, but neither is proven.

3. **UNVERIFIED:** why the entry SP was `0x02ff43a8` (above `__sp_usr` and above
   the nominal DTCM top). Possibilities: a bad SP inherited from earlier code,
   runtime DTCM configuration different from the linker script, or SP underflow
   through the user stack top.

4. **UNVERIFIED:** whether real hardware would ever take the same wild jump. No
   CUBES=64 hardware run exists, and the function is dead code in the source,
   so a hardware occurrence cannot be excluded purely by source inspection.

5. **UNVERIFIED:** the underlying emulator instability that produces the wild
   jump. The CUBES=64 AF=120 case is one of several DeSmuME-specific failures
   documented in `docs/EMULATOR_VALIDATION.md:269-292` and
   `docs/reports/bkpt-diagnosis.md`.

---

## Requirement traceability (docs repair, 2026-11-18)

Docs-only repair of this report, `docs/EQ_SCENES.md` and
`docs/reports/bkpt-diagnosis.md`; no code, test, ROM source, fixture or emulator
file changed. Line numbers are in the repaired files. `TESTS: make test` for
every item; the docs are not parsed by the tests, so the check is that nothing
regressed.

- **REQ-1 (source lines + addr2line mismatch).** Before, the report did not
  quote the source at `:208`/`:215` and left the addr2line mismatch unexplained.
  After, section 1 quotes `common/twlhd_tex_capture.c:207-215` and `:262-264`
  (`:37-65`), gives the real `addr2line -f [-i]` output mapping all three PCs to
  `:263`/`:264` (`:69-83`), and explains the mismatch as an earlier wrong
  attribution, with the alternative (a different build) left **UNVERIFIED**
  (`:85-95`). Evidence: `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64`;
  `arm-none-eabi-addr2line -f -i -e arm9v2/twlhd-arm9v2-autodump-c64.elf
  0x0200269A 0x02002766 0x02002767`;
  `git log --oneline --all -- common/twlhd_tex_capture.c`.

- **REQ-3 (CAUSE + §5.1 de-overclaim).** Before, `## CAUSE` stated the wild jump
  as fact and §5.1 called H-dead "strongly supported". After, `## CAUSE`
  (`:557-569`) labels only the measured `pop {pc}` → PC=0 as **FACT**, the wild
  jump and dead-code entry as **HYPOTHESIS**, and the unknowns as **UNVERIFIED**.
  §5.1 (`:486-499`) now says H-dead is "consistent with the measurements, but the
  mechanism is UNVERIFIED".

- **REQ-2 (cross-reference fix).** Before, `docs/EQ_SCENES.md` and
  `docs/reports/bkpt-diagnosis.md` called `:208`/`:215` the branch site. After,
  the branch site is described as the `:263` return / `:264` closing brace in
  `twlhd_tex_capture_plan` in `docs/EQ_SCENES.md:279-280,318-319` and
  `docs/reports/bkpt-diagnosis.md:253,385,421-422,474-476,588-592,611-612,628-629`.

- **REQ-4 (§2.2 stack analysis).** Before, "not plausible" was stated as **FACT**
  and the SP-above-top analysis was incomplete. After, §2.2 (`:254-282`) labels
  the overflow-into-bss inference as **HYPOTHESIS**, narrows the **FACT** to the
  measured addresses, analyses SP above `__sp_usr`/DTCM top as an abnormal entry
  (underflow / frame-over-IRQ-SVC / foreign SP), and documents `r12=0x02ff3e68`
  (`__sp_usr − 0x18`) and `r3=0x55C` (= 1372-byte frame size). The frame size is
  corrected from 1364 to 1372 bytes (`:132-137`).

- **REQ-5 (§2.3/2.4 indirect-call scan + ring re-examination).** Before, the
  "never calls legitimately" claim rested only on direct `bl` scanning. After,
  §2.3 (`:308-314`) quotes the `bl`, `blx`/`bx`, and literal-pool scans, and §2.4
  notes the claim is scoped to "no direct `bl` and no indirect reference found
  by this scan". The ring evidence is reconciled in section 1 (`:139-157`) and
  §2.3 (`:308-314`): the planner ran correctly to completion, making an ordinary
  call to `twlhd_tex_capture_layout` and returning, which is evidence against
  "entered mid-loop" and against "saved LR overwritten later".

- **REQ-6 (§3.3 real outputs).** Before, the NULL-`read` and ASAN/UBSAN claims
  had no quoted commands. After, §3.3 (`:411-456`) quotes the scratch host driver
  command/output for `twlhd_tex_capture_plan(read=NULL) -> 1 (ERR_NULL)` and the
  CUBES=64 replay through `tests/host/build/tex_capture_cli` under the `make
  test` ASAN/UBSAN build, with full output.

- **REQ-7 (§5.2 mitigation + root-cause path).** Before, §5.2 presented
  `--gc-sections`/excluding texture files as a fix. After, §5.2 (`:504-540`)
  labels it a symptom-hiding mitigation and adds a root-cause path: a scratch
  DeSmuME watchpoint/trace experiment (diagnosis stage) → fix stage determined by
  the finding (build stage, product code stage, or capture-flow/DeSmuME stage).

- **REQ-8 (never weaken tests / only docs changed).** No test, code, Makefile,
  tool or fixture file was modified. `make test` passes.

