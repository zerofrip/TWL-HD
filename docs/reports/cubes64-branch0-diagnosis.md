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

## 1. What is at `common/twlhd_tex_capture.c:208` / `:215` in the failing build

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

### Disassembly at the branch site and LR

`arm-none-eabi-nm -n` shows the function layout:

```
02002350 T twlhd_gx_rec_serialize_tex
020025d0 T twlhd_tex_capture_plan
02002adc T twlhd_tex_capture_apply
```

**FACT:** `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf`
on the three PCs from the ring gives:

| PC | Symbol / line |
|---|---|
| `0x020025BC` | `twlhd_tex_capture_layout`, `common/twlhd_tex_capture.c:285` |
| `0x02002766` | inside `twlhd_tex_capture_plan`, `common/twlhd_tex_capture.c:264` (discriminator) |
| `0x0200269A` | inside `twlhd_tex_capture_plan`, `common/twlhd_tex_capture.c:264` |

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

**FACT:** the function allocates a 1364-byte stack frame
(`ldr r4, [pc, #672] ; .word 0xfffffaa4` then `add sp, r4` at
`0x020025dc-0x020025e0`). The local arrays are `last_write_seq[256]`
(`common/twlhd_tex_capture.c:149`, 1024 bytes) and `drop[256]`
(`common/twlhd_tex_capture.c:149`, 256 bytes), totalling 1280 bytes, plus the
remaining locals.

**HYPOTHESIS:** the saved LR was overwritten by an out-of-bounds write to the
local arrays. The C source guards the index at `:216`
(`if (key_count >= CAP_KEY_MAX) continue;`), but that guard only helps when the
function is entered normally. If control wild-jumps into `twlhd_tex_capture_plan`
with a corrupted `r7` (`gxcm_count`) or mid-loop, `key`/`key_count` can exceed
`CAP_KEY_MAX` and a write to `last_write_seq[key]` or `drop[key]` can reach the
saved LR slot 1280+ bytes above the arrays.

**UNVERIFIED:** whether the zero was written later by such an overflow or was
already `0` in the LR register when the wild jump entered the function.

**UNVERIFIED:** the exact instruction that wrote the zero into the saved-LR
slot was not captured (the 4 M-record PC trace stopped before frame 129; see
section 2.3).

---

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

**HYPOTHESIS:** the stack pointer has been corrupted or the stack has overflowed
into the reserved DTCM area above the user stack. The observed SP is outside the
nominal 16 KiB DTCM region, so normal stack growth inside DTCM cannot explain
it; either the CPU entered this function with a bad SP or the SP was modified
after a wild jump.

**FACT:** stack overflow into `.bss`/heap is **not plausible**, because the
stack lives in DTCM (`0x02ff0000` region) while `.bss`/heap and the large static
buffers (`g_serialize`, `g_recorder`) live in EWRAM (`0x02000000` region). The
EWRAM region is 4 MiB − 512 KiB = 3,670,016 bytes; only 391,224 bytes are used,
leaving ~3.28 MiB free.

### 2.3 Called vs wild entry

**FACT:** `dump_frame` in the CUBES=64 ROM calls only the non-texture
serializer:

```
02001fb4:	f000 f99a \tbl\t20022ec <twlhd_gx_rec_serialize>
```

The only `bl` to `twlhd_tex_capture_plan` is the one inside
`twlhd_gx_rec_serialize_tex` at `0x020023a0`; there is **no** `bl` to
`twlhd_gx_rec_serialize_tex` from any other function in `.main`.

**FACT:** `arm9v2/source/main.cpp:109-129` gates the texture path with
`#if TWLHD_DEMO_SCENE == 5`; for the CUBES=64 build `SCENE` is unset/0, so
`twlhd_gx_rec_serialize_tex` is not called from C source.

**FACT:** the 4 M-record PC trace (`TWLHD_PCLOG`) captured the first
4,000,000 ARM9 instructions from reset. It contains **no** execution in
`twlhd_tex_capture_plan` (`0x020025d0-0x02002adc`) and **no** execution in
`twlhd_gx_rec_serialize_tex` proper (`0x02002350-0x020024a4`); the trace limit
was reached long before frame 129, so it cannot directly show the entry into the
plan.

**HYPOTHESIS:** the CPU wild-jumped into the dead-but-linked texture code. The
static call graph shows no legitimate caller, and the return-style branch to `0`
is consistent with a corrupted saved LR after entering dead code with invalid
parameters.

**UNVERIFIED:** the exact origin PC of the wild jump (which instruction branched
into `twlhd_tex_capture_plan`) was not captured.

### 2.4 What differs at frame 129 vs the working scenes

**FACT:** at frame 120 the ROM writes a valid 46056-byte v2 file (5737 GXCM + 4
RSTA entries) via `twlhd_gx_rec_serialize`, `fatInitDefault`, `fopen` and
`fwrite` (`arm9v2/source/main.cpp:127-155`).

**FACT:** at frame 129 the fault occurs in `twlhd_tex_capture_plan`, a function
that the CUBES=64 ROM never calls legitimately.

**FACT:** the CUBES=64 stream contains no texture commands (`TEXIMAGE_PARAM`
count = 0, `PLTT_BASE` count = 0 in the captured file). Even if the texture path
had been called legitimately, `cap_format(param) == 0` at
`common/twlhd_tex_capture.c:199` would cause the planner to see `keys_seen = 0`
and return `TWLHD_TEX_CAP_OK` after only the GXCM scan.

**HYPOTHESIS:** the fault at frame 129 is not triggered by the *content* of the
capture (the stream is byte-valid and untextured) but by the emulator reaching
dead code through a wild jump. The difference between frame 120 and frame 129 is
simply that enough time/execution has passed for the wild jump to occur, not
that the second dump attempt or libfat state changed in a meaningful way.

---

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
`TWLHD_TEX_CAP_ERR_NULL` at `common/twlhd_tex_capture.c:163`; it does not branch
to `0`.

**FACT:** feeding a textured synthetic stream with a valid callback through the
planner + apply under ASAN/UBSAN produces no memory errors and no undefined
behaviour in the host test suite.

**CONCLUSION (HYPOTHESIS):** the CUBES=64 ROM inputs do **not** reproduce the
branch-to-0 on the host. The host planner behaves correctly on the real captured
stream. This makes a NULL/corrupted `read` callback or a planner logic bug an
unlikely explanation for the emulator fault.

---

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
  **dead code** in the CUBES=64 build. There is no call to it from `dump_frame`
  or from any other ROM code. The captured stream is byte-valid and untextured,
  and the same stream passes cleanly on the host. These facts make H-real
  unlikely.

- **H-dead** (a wild jump into dead-but-linked texture code): strongly
  supported. The function is present only because common/ is fully linked into
  every arm9v2 build with no `--gc-sections`
  (`arm9v2/Makefile:26-28,46-47`). The fault is a return from this dead
  function with a zeroed saved LR, consistent with stack corruption after an
  uncontrolled entry.

### 5.2 Smallest proposed fix

**Proposed fix:** in the next build/variant stage, compile the non-texture
arm9v2 variants so that the texture-capture path is not present in the ROM at
all. The least invasive way is to enable `-ffunction-sections` and
`-Wl,--gc-sections` for the arm9v2 build, or to exclude `twlhd_tex_capture.c`,
`twlhd_tex_source.c` and the texture-aware serialize path from `SOURCES` when
`TWLHD_DEMO_SCENE != 5`. This removes the dead-code target that the wild jump
hit, without changing `common/` semantics.

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

Under DeSmuME the CUBES=64 AUTODUMP_FRAME=120 ROM executes a wild jump into the
dead-but-linked `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:144`) and
returns through a zeroed saved LR, branching to address `0` and landing in the
ITCM reset slot `0x01FF853C` at frame 129. **FACT:** the branch site is the
function return `pop {r4, r5, r6, r7, pc}` at `0x0200269A`; the rebuilt ROM
reproduces the recorded register dump exactly (`ROM crc 1B79DB9D`,
`pc=01FF853C`, `sp=02FF43A8`, `lr=02002767`). **HYPOTHESIS:** the zeroed return
address is caused by stack corruption after entering dead code, not by a
legitimate call with a NULL callback. **UNVERIFIED:** the exact instruction that
wrote the zero and the precise origin of the wild jump were not captured.

---

## Open items

1. **UNVERIFIED:** the exact origin PC of the wild jump into
   `twlhd_tex_capture_plan`. The 4 M-record PC trace stopped before frame 129;
   a larger trace or a watchpoint on the plan entry address would be needed.

2. **UNVERIFIED:** the exact store that zeroed the saved LR slot on the stack.
   The local arrays in `twlhd_tex_capture_plan` are the obvious candidate, but
   the specific instruction was not observed.

3. **UNVERIFIED:** whether the SP value `0x02ff43a8` reflects a corrupted SP
   from the wild jump or a genuine stack overflow into the reserved DTCM region.
   The linker script gives a 16 KiB DTCM, but runtime CP15 configuration could
   change the effective size.

4. **UNVERIFIED:** whether real hardware would ever take the same wild jump. No
   CUBES=64 hardware run exists, and the function is dead code in the source,
   so a hardware occurrence cannot be excluded purely by source inspection.

5. **UNVERIFIED:** the underlying emulator instability that produces the wild
   jump. The CUBES=64 AF=120 case is one of several DeSmuME-specific failures
   documented in `docs/EMULATOR_VALIDATION.md:269-292` and
   `docs/reports/bkpt-diagnosis.md`.
