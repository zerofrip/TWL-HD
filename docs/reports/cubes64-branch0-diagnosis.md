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

**FACT:** the PC ring (16 entries) and `twlhd_tex_capture_plan` disassembly show
only the *tail* of the execution window:

```
TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
```

The addresses `0x020025BC..C2` lie inside `twlhd_tex_capture_layout`
(`0x02002570–0x020025CF`; the next symbol in `nm -n` output is `0x020025d0`).
After those, the ring holds the return to `0x02002766` (LR `0x02002767`, set by
the `bl` at `0x02002762`), the branch `b.n 0x0200268C`, and the shared epilogue:

```
02002762:	f7ff ff05 \tbl\t2002570 <twlhd_tex_capture_layout>
02002766:	e791      \tb.n\t200268c <twlhd_tex_capture_plan+0xbc>
...
0200269a:	bdf0      \tpop\t{r4, r5, r6, r7, pc}
```

The `pop {pc}` at `0x0200269A` loaded a saved LR that was already `0`.

**HYPOTHESIS:** the saved LR slot was `0` from the moment the function was
entered, because control reached `twlhd_tex_capture_plan` through a branch that
did not set LR (a `bx` or `mov pc` rather than a `bl`). The ring tail is
consistent with the function then running through the observed `bl`→return→epilogue
sequence; no stack corruption is required to explain the zeroed return address.
This hypothesis is **consistent with** the ring evidence but is not proven,
because the entry origin and any earlier part of the execution window were not
captured.

**HYPOTHESIS (alternative):** the saved LR was overwritten by an out-of-bounds
write to the local arrays. This is still possible in principle; the 16-entry
ring cannot discriminate entry-LR=0 from a later overwrite of the saved-LR slot.

**UNVERIFIED:** the exact origin PC of the entry into `twlhd_tex_capture_plan`,
whether the planner ran its full body, and whether LR was already `0` at entry
or was overwritten later.

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
(`−1372`).

**HYPOTHESIS:** `r3` is the 1372-byte stack frame size of
`twlhd_tex_capture_plan`. The prologue that loaded this constant ran at function
entry, long before the observed ring window (which shows only the layout tail and
epilogue), and no instruction in that window writes `r3`; the provenance of the
value in `r3` is therefore **UNVERIFIED**. The meaning of `r12` is also
**UNVERIFIED**: it may be a stale pointer to just below the user stack top, or
it may record the entry SP before the 1372-byte frame was allocated.

### 2.3 Called vs wild entry

**FACT:** `dump_frame` in the CUBES=64 ROM calls only the non-texture
serializer:

```
02001fb4:	f000 f99a \tbl\t20022ec <twlhd_gx_rec_serialize>
```

**FACT:** the only direct `bl` to `twlhd_tex_capture_plan` found by the greps is the one inside
`twlhd_gx_rec_serialize_tex` at `0x020023a0`; no direct `bl` to
`twlhd_gx_rec_serialize_tex` from any other function in `.main` was found by the greps.

**FACT:** `arm9v2/source/main.cpp:109-129` gates the texture path with
`#if TWLHD_DEMO_SCENE == 5`; for the CUBES=64 build `SCENE` is unset/0, so
`twlhd_gx_rec_serialize_tex` is not called from C source.

**FACT:** a full scan of the rebuilt ELF (`arm9v2/twlhd-arm9v2-autodump-c64.elf`,
ROM CRC `1B79DB9D`) found no direct `bl` to `twlhd_tex_capture_plan`
(`0x020025d0`, Thumb entry `0x020025d1`) or `twlhd_gx_rec_serialize_tex`
(`0x02002350`, Thumb entry `0x02002351`) except the single call to the plan
inside `twlhd_gx_rec_serialize_tex`. The scan also searched the loaded sections
for literal-pool or function-pointer references to those addresses and found
none. The scan covers the whole ELF (including `.main`, `.main.rw`, `.init_array`,
`.fini_array`, `.eh_frame`, `.tinfo` and `.loadlist`; the ELF has no conventional
`.data` or `.rodata` section) plus all ARM- and Thumb-mode indirect branches in
the `objdump -d` output.

Direct calls:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E 'bl\s+20025(d0|d1)'
 20023a0:	f000 f916 	bl	20025d0 <twlhd_tex_capture_plan>

$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E 'bl\s+20023(50|51)'
(no output)
```

No direct `bl` to `twlhd_gx_rec_serialize_tex` was found by this grep
(**HYPOTHESIS:** it has no caller).

The single direct `bl` to the planner is at `0x020023a0`, which
`arm-none-eabi-addr2line` places inside `twlhd_gx_rec_serialize_tex`:

```
$ arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x020023a0
twlhd_gx_rec_serialize_tex
/tmp/codex-20261118-010214-555357/worktree/arm9v2/../common/twlhd_gx_record.c:198
```

Byte search of the whole ELF for the little-endian Thumb and ARM entry addresses:

```
$ python3 -c "
import sys
data = open('arm9v2/twlhd-arm9v2-autodump-c64.elf','rb').read()
for addr in (0x020025d1, 0x02002351, 0x020025d0, 0x02002350):
    b = addr.to_bytes(4,'little')
    hits = []
    off = 0
    while True:
        i = data.find(b, off)
        if i == -1: break
        hits.append(i)
        off = i + 1
    print(f'0x{addr:08x}:', ' '.join(f\"0x{h:08x}\" for h in hits) if hits else '(no match)')
"
0x020025d1: 0x000a67f0
0x02002351: 0x000a67c0
0x020025d0: 0x0002eb23 0x00078250
0x02002350: 0x0002dc1e 0x00078138 0x000a0e20
```

`arm-none-eabi-readelf -S --wide` maps those file offsets to sections:

```
$ arm-none-eabi-readelf -S --wide arm9v2/twlhd-arm9v2-autodump-c64.elf
There are 38 section headers, starting at offset 0xa90e8:

Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .secure           PROGBITS        0e000000 0001b4 000800 00  WA  0   0  1
  [ 2] .bootstub         PROGBITS        0e000800 0009b4 000198 00  AX  0   0  4
  [ 3] .crt0             PROGBITS        02001000 000b4c 0003c0 00  AX  0   0  4
  [ 4] .dldi             PROGBITS        0380b000 000f0c 004000 00  AX  0   0  4
  [ 5] .vectors          PROGBITS        00000000 004f0c 000020 00  AX  0   0  4
  [ 6] .itcm             PROGBITS        01ff8020 004f2c 0006c8 00  AX  0   0  4
  [ 7] .dtcm             PROGBITS        02ff0000 022070 000000 00   W  0   0  1
  [ 8] .dtcm.bss         NOBITS          02ff0000 0055f4 000098 00  WA  0   0  4
  [ 9] .main             PROGBITS        020013c0 0055f8 01ab6c 00  AX  0   0  8
  [10] .main.rw          PROGBITS        0201bf30 020168 001eb4 00  WA  0   0  8
  [11] .eh_frame         PROGBITS        0201dde4 02201c 000004 00   A  0   0  4
  [12] .tinfo            PROGBITS        0201dde8 022020 00000c 00   A  0   0  1
  [13] .init_array       INIT_ARRAY      0201ddf4 02202c 000004 04  WA  0   0  4
  [14] .fini_array       FINI_ARRAY      0201ddf8 022030 000004 04  WA  0   0  4
  [15] .tdata            PROGBITS        00000000 022070 000000 00 WAT  0   0  1
  [16] .tbss             NOBITS          00000000 000000 000000 00 WAT  0   0  1
  [17] .main.tls         PROGBITS        0201de00 022070 000000 00   W  0   0  8
  [18] .main.bss         NOBITS          0201de00 022038 042df8 00  WA  0   0  8
  [19] .twl              PROGBITS        02060c00 022070 000000 00   W  0   0  1
  [20] .twl.rw           PROGBITS        02060c00 022070 000000 00   W  0   0  1
  [21] .twl.bss          PROGBITS        02060c00 022070 000000 00   W  0   0  1
  [22] .loadlist         PROGBITS        0e000998 022034 000030 00   A  0   0  1
  [23] .twl.loadlist     PROGBITS        0e0009c8 022064 00000c 00   A  0   0  1
  [24] .comment          PROGBITS        00000000 022070 000030 01  MS  0   0  1
  [25] .debug_aranges    PROGBITS        00000000 0220a0 000ee0 00      0   0  8
  [26] .debug_info       PROGBITS        00000000 022f80 030705 00      0   0  1
  [27] .debug_abbrev     PROGBITS        00000000 053685 007bdc 00      0   0  1
  [28] .debug_line       PROGBITS        00000000 05b261 01cd73 00      0   0  1
  [29] .debug_frame      PROGBITS        00000000 077fd4 002908 00      0   0  4
  [30] .debug_str        PROGBITS        00000000 07a8dc 009a91 01  MS  0   0  1
  [31] .ARM.attributes   ARM_ATTRIBUTES  00000000 08436d 000027 00      0   0  1
  [32] .debug_loclists   PROGBITS        00000000 084394 018535 00      0   0  1
  [33] .debug_rnglists   PROGBITS        00000000 09c8c9 003781 00      0   0  1
  [34] .debug_line_str   PROGBITS        00000000 0a004a 00035f 01  MS  0   0  1
  [35] .symtab           SYMTAB          00000000 0a03ac 0064f0 10     36 1250  4
  [36] .strtab           STRTAB          00000000 0a689c 0026e1 00      0   0  1
  [37] .shstrtab         STRTAB          00000000 0a8f7d 00016b 00      0   0  1
Key to Flags:
  W (write), A (alloc), X (execute), M (merge), S (strings), I (info),
  L (link order), O (extra OS processing required), G (group), T (TLS),
  C (compressed), x (unknown), o (OS specific), E (exclude),
  D (mbind), y (purecode), p (processor specific)
```

Using the `sh_offset`/`sh_size`/`sh_type` values from that table, the matches are:

- `0x000a67f0` → `.symtab`
- `0x000a67c0` → `.symtab`
- `0x000a0e20` → `.symtab`
- `0x0002eb23` → `.debug_info`
- `0x0002dc1e` → `.debug_info`
- `0x00078250` → `.debug_frame`
- `0x00078138` → `.debug_frame`

None of these offsets are in loaded sections; `.main.bss` is `NOBITS` and therefore
owns no file bytes. A per-section byte search of every loaded PROGBITS range
(including `.main`) found no occurrence of the target words:

```
$ python3 -c "
import re, subprocess
elf = 'arm9v2/twlhd-arm9v2-autodump-c64.elf'
out = subprocess.check_output(['arm-none-eabi-readelf','-S','--wide',elf], text=True)
secs = []
for line in out.splitlines():
    m = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\S+)', line)
    if m:
        name, typ, addr, off, size, es, flg = m.groups()[1:]
        secs.append((name, typ, int(addr,16), int(off,16), int(size,16), flg))
data = open(elf,'rb').read()
addrs = (0x020025d1, 0x02002351, 0x020025d0, 0x02002350)
loaded = [(n, o, s) for n, t, a, o, s, f in secs if t != 'NOBITS' and 'A' in f and s > 0]
print('Loaded sections scanned:', ', '.join(n for n, o, s in loaded))
for val in addrs:
    b = val.to_bytes(4,'little')
    print(f'0x{val:08x}:', end='')
    found = False
    for name, off, size in loaded:
        hits = []
        i = off
        while True:
            i = data.find(b, i, off+size)
            if i == -1: break
            hits.append(i); i += 1
        if hits:
            print(f'  {name}: '+' '.join(hex(h) for h in hits))
            found = True
    if not found:
        print(' (no hit in loaded sections)')
"
Loaded sections scanned: .secure, .bootstub, .crt0, .dldi, .vectors, .itcm, .main, .main.rw, .eh_frame, .tinfo, .init_array, .fini_array, .loadlist, .twl.loadlist
0x020025d1: (no hit in loaded sections)
0x02002351: (no hit in loaded sections)
0x020025d0: (no hit in loaded sections)
0x02002350: (no hit in loaded sections)
```

The `objdump -s` dump below searches the same loaded sections (now including
`.main`) for aligned 4-byte words; because it only prints aligned groups it can
miss an unaligned literal, so the per-section byte search above is the decisive
check:

```
$ arm-none-eabi-objdump -s -j .main -j .main.rw -j .init_array -j .fini_array -j .eh_frame -j .tinfo -j .loadlist arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -iE 'd1250002|51230002|d0250002|50230002|020025d1|02002351'
(no output)
```

Thumb `bx`/`blx` (excluding `bx lr` returns):

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '\b47[89abcdef]8\b|\b47[0-9a-f]{2}\b' | grep -v '4770'
 20013ec:	4718      	bx	r3
 2001492:	4700      	bx	r0
 20014b4:	4700      	bx	r0
 200293c:	47a0      	blx	r4
 2002974:	47a0      	blx	r4
 2002a08:	47a0      	blx	r4
 2002a66:	47b0      	blx	r6
 2002bf8:	47b0      	blx	r6
 2002c24:	47b0      	blx	r6
 2002c44:	47b0      	blx	r6
 2002c84:	47d0      	blx	sl
 2002ce8:	47d0      	blx	sl
 2002d20:	47d0      	blx	sl
 200444a:	4798      	blx	r3
 200d9ec:	4710      	bx	r2
 200da8c:	4710      	bx	r2
 200dc20:	4708      	bx	r1
 200dd0a:	4708      	bx	r1
 200dd6a:	4708      	bx	r1
 200ddc6:	4708      	bx	r1
 200de6c:	4708      	bx	r1
 200de86:	4708      	bx	r1
 200de9e:	4708      	bx	r1
 200df4c:	4700      	bx	r0
 200e21a:	4710      	bx	r2
 200e544:	4708      	bx	r1
 200e60a:	4708      	bx	r1
 200e6d4:	4708      	bx	r1
 200e7a0:	4708      	bx	r1
 200e7fa:	4708      	bx	r1
 200e936:	4708      	bx	r1
 200eae8:	4708      	bx	r1
 200ec42:	4708      	bx	r1
 200f392:	4708      	bx	r1
 200f488:	4700      	bx	r0
 200f694:	4708      	bx	r1
 200f784:	4700      	bx	r0
 200f7c4:	4700      	bx	r0
 200f8ce:	4700      	bx	r0
 200f962:	4708      	bx	r1
 200f9e8:	4700      	bx	r0
 200fa08:	4700      	bx	r0
 200fa1c:	4700      	bx	r0
 200fa62:	4700      	bx	r0
 200fa74:	4718      	bx	r3
 200fa8a:	4708      	bx	r1
 200fa9a:	4700      	bx	r0
 200fab4:	4708      	bx	r1
 200fad6:	4700      	bx	r0
 200fb22:	4718      	bx	r3
 200fb4a:	4708      	bx	r1
 200fb84:	4708      	bx	r1
 200fbc6:	4708      	bx	r1
 200fc10:	4710      	bx	r2
 200fc2e:	4708      	bx	r1
 200fc54:	4718      	bx	r3
 200fc64:	4700      	bx	r0
 200fc78:	4700      	bx	r0
 200fcfe:	4708      	bx	r1
 2010092:	4740      	bx	r8
 2010118:	4708      	bx	r1
 2010170:	4718      	bx	r3
 2010186:	4708      	bx	r1
 20101bc:	4710      	bx	r2
 20101ca:	4700      	bx	r0
 20101f0:	4708      	bx	r1
 2010286:	4700      	bx	r0
 2010290:	4718      	bx	r3
 20103f8:	4708      	bx	r1
 2010868:	4708      	bx	r1
 201086a:	4720      	bx	r4
 2011f3e:	4708      	bx	r1
 2011f40:	4728      	bx	r5
 2011fb4:	4708      	bx	r1
 201208c:	4700      	bx	r0
 2012464:	4708      	bx	r1
 20124ce:	4708      	bx	r1
 2012578:	4730      	bx	r6
 2012624:	4708      	bx	r1
 2012626:	4740      	bx	r8
 201263e:	4708      	bx	r1
 2012684:	4708      	bx	r1
 20126c4:	4728      	bx	r5
 20126c6:	4738      	bx	r7
 201278a:	4708      	bx	r1
 2012836:	4708      	bx	r1
 201285a:	4708      	bx	r1
 20128e0:	4708      	bx	r1
 2012b70:	4728      	bx	r5
 2012b72:	4738      	bx	r7
 2012b74:	4750      	bx	sl
 2012b84:	4700      	bx	r0
 2012b98:	4700      	bx	r0
 2012bc2:	4700      	bx	r0
 2012cbe:	4708      	bx	r1
 2012d02:	4708      	bx	r1
 2012df2:	4708      	bx	r1
 2012e88:	4728      	bx	r5
 2012e8a:	4738      	bx	r7
 2012eca:	4708      	bx	r1
 2012f1e:	4708      	bx	r1
 2012f5c:	4708      	bx	r1
 2012fda:	4708      	bx	r1
 201301c:	4708      	bx	r1
 20130a4:	4708      	bx	r1
 2014a76:	4708      	bx	r1
 2014a78:	4720      	bx	r4
 2014a7a:	4728      	bx	r5
 2014a7c:	4760      	bx	ip
 2014b56:	4708      	bx	r1
 2014d02:	4708      	bx	r1
 2014d3c:	4708      	bx	r1
 2014da0:	4708      	bx	r1
 2014e08:	4708      	bx	r1
 2014e4a:	4708      	bx	r1
 2014f2a:	4708      	bx	r1
 2014f82:	4708      	bx	r1
 2014f98:	4708      	bx	r1
 2014fda:	4708      	bx	r1
 2015200:	4708      	bx	r1
 2015452:	4708      	bx	r1
 201550e:	4708      	bx	r1
 201560e:	4708      	bx	r1
 2015638:	4750      	bx	sl
 2015672:	4708      	bx	r1
 2015688:	4700      	bx	r0
 201569c:	4700      	bx	r0
 20156b0:	4708      	bx	r1
 20157b4:	4710      	bx	r2
 2015892:	4710      	bx	r2
 20158c8:	4708      	bx	r1
 20159b0:	4708      	bx	r1
 2015ba6:	4708      	bx	r1
 2015bf4:	4720      	bx	r4
 2015bf6:	4730      	bx	r6
 2015c44:	4708      	bx	r1
 2015dfc:	4710      	bx	r2
 2015fc6:	4710      	bx	r2
 2015fde:	4708      	bx	r1
 2015ff8:	4708      	bx	r1
 2016050:	4708      	bx	r1
 2016120:	4708      	bx	r1
 2016158:	4718      	bx	r3
 201624a:	4708      	bx	r1
 20162ce:	4708      	bx	r1
 20162e8:	4718      	bx	r3
 20162fc:	4708      	bx	r1
 201631c:	4708      	bx	r1
 201633a:	4700      	bx	r0
 20163ec:	4708      	bx	r1
 2016438:	4708      	bx	r1
 20164c2:	4708      	bx	r1
 20164f4:	4718      	bx	r3
 201655a:	4708      	bx	r1
 20165cc:	4708      	bx	r1
 20165ec:	4708      	bx	r1
 2016644:	4710      	bx	r2
 201665c:	4728      	bx	r5
 20166ca:	4708      	bx	r1
 20166e8:	4728      	bx	r5
 201672c:	4708      	bx	r1
 2016744:	4728      	bx	r5
 20167a8:	4700      	bx	r0
 20167c0:	4700      	bx	r0
 20167d8:	4700      	bx	r0
 20167f0:	4700      	bx	r0
 2016838:	4708      	bx	r1
 2016850:	4728      	bx	r5
```

ARM-mode register-target indirect branches (32-bit encodings only; `blx <addr>`
immediate calls are normal direct calls and are omitted):

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | python3 -c "
import re, sys
for line in sys.stdin:
    m = re.match(r'^(\s*[0-9a-f]+:\s+)([0-9a-f]+)(\s+\S.*)$', line)
    if not m: continue
    pre, bytes_, rest = m.groups()
    if len(bytes_.replace(' ','')) != 8: continue
    asm_full = rest.strip()
    parts = asm_full.split('\t')
    mnemonic = parts[0].split()[0] if parts[0].split() else ''
    operands = '\t'.join(parts[1:]) if len(parts) > 1 else ''
    if (mnemonic.startswith(('bx','blx')) and operands.startswith('r')) or \
       (mnemonic == 'ldr' and operands.startswith('pc,')) or \
       (mnemonic == 'mov' and operands.startswith('pc,')):
        sys.stdout.write(line)
"
 e0008bc:	e12fff30 	blx	r0
 e0008e8:	e59ff0a4 	ldr	pc, [pc, #164]	@ e000994 <__ds9_bootstub+0x194>
 2001044:	e12fff14 	bx	r4
 1ff8020:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8038 <__excpt_vectors>
 1ff8024:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff803c <__excpt_vectors+0x4>
 1ff8028:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8040 <__excpt_vectors+0x8>
 1ff802c:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8044 <__excpt_vectors+0xc>
 1ff8030:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8048 <__excpt_vectors+0x10>
 1ff8034:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff804c <__excpt_vectors+0x14>
 1ff8658:	112fff33 	blxne	r3
 20053bc:	e12fff33 	blx	r3
 20054ac:	e12fff33 	blx	r3
 2005558:	e12fff33 	blx	r3
 20055c0:	e12fff13 	bx	r3
 20056b4:	e12fff36 	blx	r6
 200597c:	e12fff36 	blx	r6
 2005dc8:	e12fff33 	blx	r3
 2005e6c:	e12fff33 	blx	r3
 20072c0:	e12fff34 	blx	r4
 20072ec:	e12fff34 	blx	r4
 200731c:	e12fff33 	blx	r3
 200c6c4:	e12fff33 	blx	r3
 200c760:	e12fff33 	blx	r3
 200d034:	e12fff33 	blx	r3
 200d3a8:	e12fff32 	blx	r2
 200d588:	e12fff30 	blx	r0
 2016888:	e51ff004 	ldr	pc, [pc, #-4]	@ 201688c <____aeabi_ldiv0_from_arm+0x4>
 2016890:	e51ff004 	ldr	pc, [pc, #-4]	@ 2016894 <____aeabi_idiv0_from_arm+0x4>
 2016898:	e51ff004 	ldr	pc, [pc, #-4]	@ 201689c <____libc_lock_release_from_arm+0x4>
 20168a0:	e51ff004 	ldr	pc, [pc, #-4]	@ 20168a4 <__memcpy_from_arm+0x4>
 20168a8:	e51ff004 	ldr	pc, [pc, #-4]	@ 20168ac <__free_from_arm+0x4>
```

**HYPOTHESIS:** no indirect reference to `twlhd_tex_capture_plan` or
`twlhd_gx_rec_serialize_tex` exists in the scanned sections of the CUBES=64 ROM.
This is scoped to the literal pools and direct branches that the scan can
resolve; an ARM-mode `bx`/`blx` register or a runtime-computed pointer could
still reach either function, so the absence of a statically visible reference
does not prove absence of all possible entries.

**FACT:** the 4 M-record PC trace (`TWLHD_PCLOG`) captured the first
4,000,000 ARM9 instructions from reset. It contains **no** execution in
`twlhd_tex_capture_plan` (`0x020025d0-0x02002adc`) and **no** execution in
`twlhd_gx_rec_serialize_tex` proper (`0x02002350-0x020024a4`); the trace limit
was reached long before frame 129, so it cannot directly show the entry into the
plan.

**HYPOTHESIS:** the CPU wild-jumped into the dead-but-linked texture code. The
static call graph and the full scan above show no statically resolvable caller,
and the return-style branch to `0` is consistent with entering the function with
a saved LR of `0` (a branch that did not set LR). The ring tail (section 1) is
consistent with the observed `bl` to `twlhd_tex_capture_layout`, return to
`0x02002766`, branch to the shared epilogue, and `pop {pc}` loading a saved LR
that was already `0`; the entry must have supplied enough valid state
(parameters, stack layout) for the observed tail to execute.

**UNVERIFIED:** the exact origin PC of the wild jump (which instruction branched
into `twlhd_tex_capture_plan`) was not captured.

### 2.4 What differs at frame 129 vs the working scenes

**FACT:** at frame 120 the ROM writes a valid 46056-byte v2 file (5737 GXCM + 4
RSTA entries) via `twlhd_gx_rec_serialize`, `fatInitDefault`, `fopen` and
`fwrite` (`arm9v2/source/main.cpp:127-155`).

**FACT:** at frame 129 the fault occurs in `twlhd_tex_capture_plan`. No direct
`bl` to `twlhd_gx_rec_serialize_tex` from any other function in `.main` was found
by the greps (**HYPOTHESIS:** it has no caller). The only direct `bl` to the plan
found by the scans is the single `bl` at `0x020023a0` inside that function, and
the scan in section 2.3 found no statically resolvable indirect reference.

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
ring tail (section 1) is consistent with this: after the abnormal entry the
observed `bl` to `twlhd_tex_capture_layout`, return to `0x02002766`, branch to
the shared epilogue, and `pop {pc}` loading a saved LR that was already `0`.

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

- **H-real** (a real ROM defect in `twlhd_tex_capture_plan`): **HYPOTHESIS:** the
  planner is dead code in the CUBES=64 build. There is no direct `bl` to it from
  `dump_frame`; the only direct `bl` is inside `twlhd_gx_rec_serialize_tex`, for
  which no caller was found, and the full scan in section 2.3 found no statically
  resolvable indirect reference. The captured stream is byte-valid and untextured,
  and the same stream passes cleanly on the host under ASAN/UBSAN. These findings
  make H-real **unlikely but not excluded**.

- **H-dead** (a wild jump into dead-but-linked texture code): consistent with
  the measurements, but the mechanism is **UNVERIFIED**. The function is present
  only because `common/` is fully linked into every arm9v2 build with no
  `--gc-sections` (`arm9v2/Makefile:26-28,46-47`). The ring tail (section 1)
  shows the observed `bl` to `twlhd_tex_capture_layout`, return to `0x02002766`,
  branch to the shared epilogue, and `pop {pc}` loading a saved LR that was
  already `0`. Whether the entry LR was `0` or the saved-LR slot was overwritten
  later is **UNVERIFIED**.

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
`pc=01FF853C`, `sp=02FF43A8`, `lr=02002767`). **FACT:** the disassembly shows the
`bl` at `0x02002762` calling `twlhd_tex_capture_layout` (`0x02002570`), and the
PC ring shows the return to `0x02002766` (LR `0x02002767`), the `b.n` to the
shared epilogue, and the `pop {pc}` at `0x0200269A` loading a saved LR that was
already `0`. The ring window cannot show whether the planner ran its full body.
**HYPOTHESIS:** a wild jump entered `twlhd_tex_capture_plan` with the saved-LR
slot set to `0` (because the entry branch did not set LR). **HYPOTHESIS:** the
planner is dead-but-linked code in the CUBES=64 build (no direct `bl` from
`dump_frame`; the only direct `bl` is inside `twlhd_gx_rec_serialize_tex`, for
which no caller was found, and no statically resolvable indirect reference found
by the scan in section 2.3).
**UNVERIFIED:** the exact instruction that entered `twlhd_tex_capture_plan`,
whether the planner ran its full body, whether LR was already `0` at entry or
was overwritten later, the reason the entry SP was above `__sp_usr`/DTCM top,
and whether real hardware could take the same path.

---

## Open items

1. **UNVERIFIED:** the exact origin PC of the entry into
   `twlhd_tex_capture_plan`. A watchpoint or longer `TWLHD_PCLOG` covering frame
   129 is needed.

2. **UNVERIFIED:** whether the saved LR slot was `0` from entry or was
   overwritten later. The 16-entry ring window cannot discriminate these two
   cases; both remain UNVERIFIED.

3. **UNVERIFIED:** why the entry SP was `0x02ff43a8` (above `__sp_usr` and above
   the nominal DTCM top). Possibilities: a bad SP inherited from earlier code,
   runtime DTCM configuration different from the linker script, or SP underflow
   through the user stack top.

4. **UNVERIFIED:** whether real hardware would ever take the same wild jump. No
   CUBES=64 hardware run exists, and **HYPOTHESIS:** no caller was found by the
   scan in §2.3, so a hardware occurrence cannot be excluded purely by source
   inspection.

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
  (`:946-968`) labels only the measured `pop {pc}` → PC=0 and the observed
  `bl`→return→epilogue sequence as **FACT**, the wild jump and dead-code entry as
  **HYPOTHESIS**, and the unknowns as **UNVERIFIED**. §5.1 (`:876-882`) now says
  H-dead is "consistent with the measurements, but the mechanism is UNVERIFIED".

- **REQ-2 (cross-reference fix).** Before, `docs/EQ_SCENES.md` and
  `docs/reports/bkpt-diagnosis.md` called `:208`/`:215` the branch site. After,
  the branch site is described as the `:263` return / `:264` closing brace in
  `twlhd_tex_capture_plan` in `docs/EQ_SCENES.md:279-280,318-319` and
  `docs/reports/bkpt-diagnosis.md:253,385,421-422,474-476,588-592,611-612,628-629`.

- **REQ-4 (§2.2 stack analysis).** Before, "not plausible" was stated as **FACT**
  and the SP-above-top analysis was incomplete. After, §2.2 (`:218-288`) labels
  the overflow-into-bss inference as **HYPOTHESIS**, narrows the **FACT** to the
  measured addresses, analyses SP above `__sp_usr`/DTCM top as an abnormal entry
  (underflow / frame-over-IRQ-SVC / foreign SP), and documents `r12=0x02ff3e68`
  (`__sp_usr − 0x18`) and `r3=0x55C` (= 1372) as **FACT**. The interpretation of
  `r3` as the frame size is **HYPOTHESIS** (`:282-288`). The frame size is
  corrected from 1364 to 1372 bytes (`:132-137`).

- **REQ-5 (§2.3/2.4 indirect-call scan + ring re-examination).** Before, the
  "never calls legitimately" claim contradicted the quoted direct `bl` at
  `0x020023a0` and the scan covered only `objdump -d` / Thumb `47xx`. After,
  §2.3 (`:307-732`) quotes the direct `bl` hits (including `addr2line` placing
  the planner call inside `twlhd_gx_rec_serialize_tex`), the whole-ELF byte
  search for the Thumb/ARM entry addresses, the
  `arm-none-eabi-readelf -S --wide` section table, the corrected
  offset→section mapping, the per-section byte search of every loaded PROGBITS
  range including `.main`, and the loaded-section `objdump -s` dump now including
  `-j .main` (with its aligned-only limitation noted); it also covers Thumb
  `bx`/`blx` and ARM-mode `bx`/`blx`/`ldr pc`/`mov pc`. The "no reference" claim
  is relabeled **HYPOTHESIS** scoped to statically resolvable targets. §2.4 notes
  no direct `bl` to `twlhd_gx_rec_serialize_tex` from any other function in
  `.main` was found by the greps (`:739-742`). The ring
  evidence is reconciled in section 1 (`:125-175`) and §2.3 (`:721-728`): the
  observed window shows only the `bl`→return→epilogue tail; it is not evidence
  against "entered mid-loop" or "saved LR overwritten later".

- **REQ-6 (§3.3 real outputs).** Before, the NULL-`read` and ASAN/UBSAN claims
  had no quoted commands. After, §3.3 (`:799-850`) quotes the scratch host driver
  command/output for `twlhd_tex_capture_plan(read=NULL) -> 1 (ERR_NULL)` and the
  CUBES=64 replay through `tests/host/build/tex_capture_cli` under the `make
  test` ASAN/UBSAN build, with full output.

- **REQ-7 (§5.2 mitigation + root-cause path).** Before, §5.2 presented
  `--gc-sections`/excluding texture files as a fix. After, §5.2 (`:893-932`)
  labels it a symptom-hiding mitigation and adds a root-cause path: a scratch
  DeSmuME watchpoint/trace experiment (diagnosis stage) → fix stage determined by
  the finding (build stage, product code stage, or capture-flow/DeSmuME stage).

- **REQ-8 (never weaken tests / only docs changed).** No test, code, Makefile,
  tool or fixture file was modified. `make test` passes.

- **REQ-9 (Opus review de-overclaim repair, 2026-11-18).** The Opus review found
  that section 1 claimed the 16-entry PC ring proved `twlhd_tex_capture_plan`
  "executed its full body" and ruled out "saved LR overwritten later", that
  §2.3/§2.4 overclaimed the indirect-call scan, and that §2.2 overclaimed the
  interpretation of `r3`. After this repair: section 1 (`:125-175`) limits the
  ring **FACT** to the observed tail (`bl`→return→epilogue) and relabels
  full-body execution as **HYPOTHESIS**/**UNVERIFIED**; §2.2 (`:277-288`) keeps
  the arithmetic as **FACT** and labels the `r3`=frame-size interpretation as
  **HYPOTHESIS**; §2.3 (`:307-732`) quotes the full scan commands and raw output
  (direct `bl` plus `addr2line` for `0x020023a0`, whole-ELF byte search,
  `arm-none-eabi-readelf -S --wide` section table with corrected
  offset→section mapping, per-section byte search of all loaded PROGBITS ranges
  including `.main`, the `objdump -s` dump now including `-j .main` with the
  aligned-only limitation noted, Thumb `bx`/`blx`, ARM-mode
  `bx`/`blx`/`ldr pc`/`mov pc`) and scopes the "no reference" claim to
  statically resolvable targets; §2.4 (`:739-742`) reads "no direct `bl` to
  `twlhd_gx_rec_serialize_tex` from any other function in `.main` was found by
  the greps"; §5.1 (`:876-882`) and `## CAUSE`
  (`:946-968`) use only the observed sequence and label the dead-code claim
  **HYPOTHESIS**; open item 2 (`:978-980`) states the ring cannot discriminate
  the two LR hypotheses; REQ-8 in
  `docs/reports/bkpt-diagnosis.md:733-739` is corrected. Evidence:
  `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64` (ROM CRC `1B79DB9D`);
  `arm-none-eabi-readelf -S --wide arm9v2/twlhd-arm9v2-autodump-c64.elf`;
  `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf` with
  `grep -E 'bl\s+20025(d0|d1)'` and `grep -E 'bl\s+20023(50|51)'`;
  `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x020023a0`;
  whole-ELF byte search for `0x020025d1`/`0x02002351`/`0x020025d0`/`0x02002350`;
  per-section byte search over loaded PROGBITS ranges including `.main`;
  `arm-none-eabi-objdump -s -j .main -j .main.rw -j .init_array -j .fini_array
  -j .eh_frame -j .tinfo -j .loadlist arm9v2/twlhd-arm9v2-autodump-c64.elf`.
  `TESTS: make test` — no regression.

- **REQ-10 (fourth docs-only repair, 2026-11-18).** The §2.3 "full scan"
  paragraph overclaimed that the byte search found zero matches and falsely
  attributed matches to `.main.bss` (a `NOBITS` section that owns no file bytes);
  §5.1 H-real and Open item 4 asserted "dead code" without a label. After this
  repair: §2.3 (`:307-732`) quotes `arm-none-eabi-readelf -S --wide`, maps every
  byte-search offset to its actual section (`.symtab` or `.debug_info`/`.debug_frame`),
  adds `.main` to the `objdump -s` scan, runs a per-section byte search over all
  loaded PROGBITS ranges, and notes the aligned-only limitation of the
  `objdump -s` grep; it also states that no direct `bl` to
  `twlhd_gx_rec_serialize_tex` was found by the greps (**HYPOTHESIS:** it has no
  caller). §5.1 (`:876-882`) labels the dead-code claim **HYPOTHESIS** and says
  the only direct `bl` is inside `twlhd_gx_rec_serialize_tex`, for which no
  caller was found; Open item 4 (`:987-990`) says **HYPOTHESIS:** no caller was
  found by the scan in §2.3. A whole-report consistency pass updated the
  `## CAUSE` hypothesis (`:946-968`) to match the quoted evidence. Evidence:
  `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64` (ROM CRC `1B79DB9D`);
  `arm-none-eabi-readelf -S --wide arm9v2/twlhd-arm9v2-autodump-c64.elf`;
  `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf` with
  `grep -E 'bl\s+20025(d0|d1)'` and `grep -E 'bl\s+20023(50|51)'`;
  `arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x020023a0`;
  whole-ELF byte search for `0x020025d1`/`0x02002351`/`0x020025d0`/`0x02002350`;
  per-section byte search over loaded PROGBITS ranges including `.main`;
  `arm-none-eabi-objdump -s -j .main -j .main.rw -j .init_array -j .fini_array
  -j .eh_frame -j .tinfo -j .loadlist arm9v2/twlhd-arm9v2-autodump-c64.elf`.
  `TESTS: make test` — no regression.

---

## Update (2026-11-18): entry-trace follow-up — reproduction achieved

Follow-up investigation documented in `docs/reports/cubes64-entry-trace.md`.
The rebuilt ROM still logs `ROM crc: 1B79DB9D` and the ELF addresses match the
values above (`twlhd_tex_capture_plan` at `0x020025d0`, epilogue `pop {r4, r5,
r6, r7, pc}` at `0x0200269A`).

The original Nov-17 23:25 runs with the existing scratch CLI
(`/home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli`) reproduced
the frame-129 register dump (`pc=01FF853C sp=02FF43A8 lr=02002767`). Fresh
Nov-18 runs with the same binary and ROM did **not** reproduce, even though the
autodump file content and ROM CRC were identical. A side-by-side log diff shows
the old and new runs are identical up to `TWLHD_SWAP n=120 frame=122` and then
diverge.

A new instrumented scratch CLI
(`/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli`) with an
RTC-epoch override reproduces the reference dump when the epoch is pinned to
the original run time (2026-11-17 ~23:24 +0900) and a fresh cflash image is
used; epochs on Nov-18 (00:00, 00:24, 09:30) run to the watchdog with no fault.

Consequences for the open items:

1. **Open item 1 (entry origin):** settled as FACT about the model. The planner
   entry address `0x020025d0`/`0x020025d1` was never reached. The function body
   was first entered mid-function at `0x02002640`
   (`common/twlhd_tex_capture.c:174`). The 64-entry PC ring shows the previous
   PC was `0x02001D50` (`arm9v2/source/demo_frame.cpp:464`, inside the
   `glRotateYi` / shim `Port` assignment sequence), immediately after an ITCM
   IRQ handler return. The instruction at `0x02001D50` is `bl
   twlhd_gx_rec_hook`. The mechanism that made the return land in the planner
   is **HYPOTHESIS** (corrupted LR/return address).
2. **Open item 2 (abnormal SP):** settled as FACT about the model. The entry SP
   was normal (`0x02FF3E28`). Because the prologue was skipped, the epilogue's
   `add sp, r3` at `0x0200268E` unpairedly added 1372 bytes, moving SP to
   `0x02FF4384` and then to the fault SP `0x02FF43A8`, above `__sp_usr` and the
   nominal DTCM top. The CPU was in SYS mode (`mode=0x1F`).
3. **Open item 3 (root cause):** reduced to HYPOTHESIS. The faulting
   instruction is `pop {pc}` at `0x0200269A` (`common/twlhd_tex_capture.c:264`),
   but the exact write that corrupted the return path is **UNVERIFIED**.
4. **Open item 4 (hardware relevance):** unchanged — **HYPOTHESIS**; no
   hardware run exists.
5. **New open item:** the exact RTC/cflash/FAT interaction inside DeSmuME that
   triggers the corruption.

See `docs/reports/cubes64-entry-trace.md` for the full raw logs, build details,
the RTC sweep table, and the updated patch file
(`desmume-dbg-patch/cubes64-entry-trace.patch`).
`make test` passes.
