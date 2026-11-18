<!-- SPDX-License-Identifier: Apache-2.0 -->
# CUBES=64 AUTODUMP_FRAME=120 IRQ return-address experiment

Path note: literal home-directory paths in quoted commands and outputs were
replaced by `$HOME` for publication; literal `\t` sequences in the §4 `grep`
output replace real tab characters. These are the only edits to the quoted
evidence; no other text was changed.

**Scope:** DIAGNOSIS + EMULATOR only. No product code, ROM source, test,
interpreter, renderer, harness or pristine DeSmuME tree was modified. The repo
files changed are this report and an appended `Update` section in
`docs/reports/cubes64-irq-lr-trace.md`. `docs/reports/cubes64-entry-trace.md`
and `docs/reports/cubes64-branch0-diagnosis.md` were reverted byte-for-byte to
their `main` versions. A scratch copy of DeSmuME outside
the repository (`$HOME/3ds/hwtest-staging/desmume-dbg`, documented in
`docs/reports/bkpt-diagnosis.md` section 1) was extended with a new diagnostic
patch (`$HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-return-address.patch`)
on top of the existing round-1 and round-2 patches, and built into a new
scratch CLI (`$HOME/3ds/hwtest-staging/desmume-dbg-build5/cli/desmume-cli`).
The round-1 and round-2 patches are unchanged. No console, SD-card or network
action was performed.

**Labels.** FACT = read at the cited `file:line` or a command output quoted
below. HYPOTHESIS = an inference from facts that was not directly measured.
UNVERIFIED = no local source exists / it was not run. Emulator facts are facts
about the *model*, never about hardware.

---

## 1. Build, patch and reproduction setup

The ROM was rebuilt with the documented recipe:

```
make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64
```

**FACT:** the rebuilt ROM CRC is `1B79DB9D`:

```
$ python3 -c "import zlib; data=open('arm9v2/twlhd-arm9v2-autodump-c64.nds','rb').read(); print('ROM crc: %08X' % (zlib.crc32(data) & 0xffffffff))"
ROM crc: 1B79DB9D
```

**FACT:** the round-3 patch file exists and is 120 lines:

```
$ wc -l $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-return-address.patch
120 $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-return-address.patch
```

**FACT:** the patch applies cleanly on top of the round-1 + round-2 patched
scratch tree; a reverse dry-run succeeds:

```
$ patch --dry-run -R -p1 -d $HOME/3ds/hwtest-staging/desmume-dbg < $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-return-address.patch
checking file desmume/src/armcpu.cpp
checking file desmume/src/NDSSystem.cpp
checking file desmume/src/arm_instructions.cpp
```

**FACT:** the scratch tree contains the new round-3 instrumentation markers:

```
$ grep -c 'TWLHD_IRQ_FLAG' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/NDSSystem.cpp
1
$ grep -c 'TWLHD_IRQ_MODE_SWITCH' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
1
$ grep -c 'TWLHD_IRQ_LR_WRITTEN' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
1
$ grep -c 'TWLHD_IRQ_RETURN' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/arm_instructions.cpp
2
```

**FACT:** `desmume-dbg-build5` was configured from the scratch source tree:

```
$ meson introspect --buildsystem-files $HOME/3ds/hwtest-staging/desmume-dbg-build5
["$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/meson.build", "$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/meson_options.txt", "$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/cli/meson.build"]
```

**FACT:** the scratch CLI build directory (`desmume-dbg-build5`) was already
up to date. The full `meson setup` output is elided; the build result is
quoted as the complete output of the narrow tail command:

```
$ ninja -C $HOME/3ds/hwtest-staging/desmume-dbg-build5 2>&1 | tail -2
ninja: Entering directory `$HOME/3ds/hwtest-staging/desmume-dbg-build5'
ninja: no work to do.
```

**FACT:** the reproduction protocol follows `tools/emulator/desmume_capture.sh:64`
for cflash creation and `:86-92` for the emulator launch. The full launch
command and the `set -x` cflash creation line are quoted below. The run used a
fresh cflash image created by the script:

```
$ TWLHD_RTC_EPOCH=1794921881 \
  DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-dbg-build5/cli/desmume-cli \
  DESMUME_TIMEOUT=30 \
  tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-irqreturn-r3-e1794921881-3 2>&1 | tee /tmp/opencode/c64-irqreturn-r3-e1794921881-3/capture-script.log | tail -3
+ cp /tmp/opencode/c64-irqreturn-r3-e1794921881-3/fat/twlhd_frame_v2.bin /tmp/opencode/c64-irqreturn-r3-e1794921881-3/twlhd_frame_v2.bin
+ echo 'capture: /tmp/opencode/c64-irqreturn-r3-e1794921881-3/twlhd_frame_v2.bin'
capture: /tmp/opencode/c64-irqreturn-r3-e1794921881-3/twlhd_frame_v2.bin
```

```
$ grep 'mkfs.fat' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/capture-script.log
+ mkfs.fat -C /tmp/opencode/c64-irqreturn-r3-e1794921881-3/cflash.img 32768
mkfs.fat 4.2 (2021-01-31)
```

```
$ sha256sum /tmp/opencode/c64-irqreturn-r3-e1794921881-3/cflash.img
6db43fdf2614d62a4d3cdb64de3ad1e008a65875182f25c4586783d72fa42ef4  /tmp/opencode/c64-irqreturn-r3-e1794921881-3/cflash.img
```

**FACT:** the build5 run reproduced the reference fault:

```
$ grep -m1 '^TWLHD_BKPT cpu=ARM9' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

**FACT:** the same run reproduced the same `IRQ_ENTRY` / `IRQ_EXIT` pair as the
round-2 trace (seq numbers are scoped to this run; the entry/exit values are
identical):

```
$ grep -E 'TWLHD_IRQTRACE (IRQ_ENTRY|IRQ_EXIT) seq=' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log | head -2
TWLHD_IRQTRACE IRQ_EXIT seq=62551664 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=0000003F
TWLHD_IRQTRACE IRQ_ENTRY seq=62551633 frame=129 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=02001D56 lr_sys=020020DF spsr=0000003F
```

---

## 2. Experiment data

### 2.1 IRQ flag and exception entry

**FACT:** the ARM9 IRQ for frame 129 was flagged at seq 62551631, just before
`armcpu_irqException`, with `instruct_adr=0x02001D52`, `next_instruction=0x02001D54`,
`R15=0x02001D56`, CPSR=0x0000003F and T-bit=1:

```
$ grep 'TWLHD_IRQ_FLAG cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_IRQ_FLAG cpu=9 seq=62551631 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 cpsr=0000003F t=1
```

**FACT:** the same exception was logged inside `armcpu_irqException` at seq
62551631 with the same `instruct_adr`, `next_instruction`, `R15`, pre-exception
LR/`LR_sys`, CPSR, T-bit, and the computed `R14_irq` value:

```
$ grep 'TWLHD_IRQ_EXCEPTION cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_IRQ_EXCEPTION cpu=9 seq=62551631 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 lr_before=020020DF lr_sys_before=020020DF cpsr_before=0000003F t_before=1 r14_irq=02001D56 cpsr_after=00000092 t_after=0
```

**FACT:** after `armcpu_switchMode(..., IRQ)` loaded the stale banked `R14_irq`,
before the explicit LR write, `R[14]` and `R14_irq` were both `0x01FF8538`:

```
$ grep 'TWLHD_IRQ_MODE_SWITCH cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_IRQ_MODE_SWITCH cpu=9 seq=62551631 frame=129 r14_after_switch=01FF8538 r14_irq_after_switch=01FF8538
```

**FACT:** after the explicit `R[14] = instruct_adr + 4`, the value actually
written into `LR_irq` (`R[14]`) was `0x02001D56`; the banked `R14_irq` still
held the stale `0x01FF8538`:

```
$ grep 'TWLHD_IRQ_LR_WRITTEN cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_IRQ_LR_WRITTEN cpu=9 seq=62551631 frame=129 lr_irq_written=02001D56 r14_irq=01FF8538
```

### 2.2 Per-instruction PC / R14 / R14_usr list

**FACT:** the complete 117-line `awk` output (103 `TWLHD_R14_TRACE` records plus 6 `TWLHD_SP_CHANGE` and 8 `TWLHD_IRQ_*` lines) is:

```
$ awk '/^TWLHD_R14_TRACE seq=62551562/{p=1} p{print} /^TWLHD_R14_TRACE seq=62551664/{exit}' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_R14_TRACE seq=62551562 frame=129 pc=02001D50 r14=02001D52 r14_usr=02001D52 mode=1F
TWLHD_R14_TRACE seq=62551563 frame=129 pc=020022B4 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551564 frame=129 pc=020022B6 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551565 frame=129 pc=020022B8 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551566 frame=129 pc=020022BA r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_SP_CHANGE frame=129 pc=020022BC old_sp=02FF3E28 new_sp=02FF3E20 old_mode=1F new_mode=1F
TWLHD_R14_TRACE seq=62551567 frame=129 pc=020022BC r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551568 frame=129 pc=020022BE r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551569 frame=129 pc=020022C0 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551570 frame=129 pc=020022C2 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551571 frame=129 pc=020022C4 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551572 frame=129 pc=020022C6 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551573 frame=129 pc=020022C8 r14=02001D53 r14_usr=02001D53 mode=1F
TWLHD_R14_TRACE seq=62551574 frame=129 pc=020022CA r14=020012CC r14_usr=020012CC mode=1F
TWLHD_R14_TRACE seq=62551575 frame=129 pc=0200206C r14=020022CD r14_usr=020022CD mode=1F
TWLHD_SP_CHANGE frame=129 pc=0200206E old_sp=02FF3E20 new_sp=02FF3E08 old_mode=1F new_mode=1F
TWLHD_R14_TRACE seq=62551576 frame=129 pc=0200206E r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551577 frame=129 pc=02002070 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551578 frame=129 pc=02002072 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551579 frame=129 pc=02002074 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551580 frame=129 pc=02002076 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551581 frame=129 pc=02002078 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551582 frame=129 pc=0200207A r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551583 frame=129 pc=0200207C r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551584 frame=129 pc=0200207E r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551585 frame=129 pc=02002080 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551586 frame=129 pc=02002082 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551587 frame=129 pc=02002084 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551588 frame=129 pc=02002086 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551589 frame=129 pc=02002088 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551590 frame=129 pc=0200208A r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551591 frame=129 pc=0200208C r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551592 frame=129 pc=0200208E r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551593 frame=129 pc=020020D0 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551594 frame=129 pc=020020D2 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551595 frame=129 pc=020020D4 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551596 frame=129 pc=020020D6 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551597 frame=129 pc=020020D8 r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551598 frame=129 pc=020020DA r14=020022CD r14_usr=020022CD mode=1F
TWLHD_R14_TRACE seq=62551599 frame=129 pc=020020DC r14=020020DE r14_usr=020020DE mode=1F
TWLHD_R14_TRACE seq=62551600 frame=129 pc=02002E28 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551601 frame=129 pc=02002E2A r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551602 frame=129 pc=02002E2C r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551603 frame=129 pc=02002E2E r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551604 frame=129 pc=02002E30 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551605 frame=129 pc=02002E32 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551606 frame=129 pc=02002E34 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551607 frame=129 pc=02002E5A r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551608 frame=129 pc=02002E5C r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551609 frame=129 pc=02002E3C r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551610 frame=129 pc=02002E3E r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551611 frame=129 pc=02002E42 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551612 frame=129 pc=02002E44 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551613 frame=129 pc=02002E38 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551614 frame=129 pc=020020DE r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551615 frame=129 pc=020020E0 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551616 frame=129 pc=020020E2 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551617 frame=129 pc=020020E4 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551618 frame=129 pc=02002162 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551619 frame=129 pc=02002164 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551620 frame=129 pc=02002166 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551621 frame=129 pc=02002168 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551622 frame=129 pc=0200216A r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551623 frame=129 pc=0200216C r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551624 frame=129 pc=0200216E r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551625 frame=129 pc=02002170 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551626 frame=129 pc=02002172 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551627 frame=129 pc=02002174 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551628 frame=129 pc=02002176 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551629 frame=129 pc=02002178 r14=020020DF r14_usr=020020DF mode=1F
TWLHD_R14_TRACE seq=62551630 frame=129 pc=020020AE r14=020020DF r14_usr=020020DF mode=1F
TWLHD_SP_CHANGE frame=129 pc=020022CC old_sp=02FF3E08 new_sp=02FF3E20 old_mode=1F new_mode=1F
TWLHD_R14_TRACE seq=62551631 frame=129 pc=020022CC r14=020020DF r14_usr=020020DF mode=1F
TWLHD_IRQ_FLAG cpu=9 seq=62551631 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 cpsr=0000003F t=1
TWLHD_IRQ_EXCEPTION cpu=9 seq=62551631 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 lr_before=020020DF lr_sys_before=020020DF cpsr_before=0000003F t_before=1 r14_irq=02001D56 cpsr_after=00000092 t_after=0
TWLHD_IRQ_MODE_SWITCH cpu=9 seq=62551631 frame=129 r14_after_switch=01FF8538 r14_irq_after_switch=01FF8538
TWLHD_IRQ_LR_WRITTEN cpu=9 seq=62551631 frame=129 lr_irq_written=02001D56 r14_irq=01FF8538
TWLHD_IRQ_EXCEPTION cpu=7 seq=0 frame=129 ia=037FC64A ni=037FC64C r15=037FC64E lr_before=037FC649 lr_sys_before=037FC649 cpsr_before=0000003F t_before=1 r14_irq=037FC64E cpsr_after=00000092 t_after=0
TWLHD_IRQ_MODE_SWITCH cpu=7 seq=0 frame=129 r14_after_switch=037FC64E r14_irq_after_switch=037FC64E
TWLHD_IRQ_LR_WRITTEN cpu=7 seq=0 frame=129 lr_irq_written=037FC64E r14_irq=037FC64E
TWLHD_SP_CHANGE frame=129 pc=00000018 old_sp=02FF3E20 new_sp=02FF3F80 old_mode=1F new_mode=12
TWLHD_R14_TRACE seq=62551632 frame=129 pc=00000018 r14=02001D56 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551633 frame=129 pc=01FF8034 r14=02001D56 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551634 frame=129 pc=01FF8604 r14=02001D56 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551635 frame=129 pc=01FF8608 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_SP_CHANGE frame=129 pc=01FF860C old_sp=02FF3F80 new_sp=02FF3F68 old_mode=12 new_mode=12
TWLHD_R14_TRACE seq=62551636 frame=129 pc=01FF860C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551637 frame=129 pc=01FF8610 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551638 frame=129 pc=01FF8614 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551639 frame=129 pc=01FF8618 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551640 frame=129 pc=01FF861C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551641 frame=129 pc=01FF8620 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551642 frame=129 pc=01FF8624 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551643 frame=129 pc=01FF8628 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551644 frame=129 pc=01FF862C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551645 frame=129 pc=01FF8630 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551646 frame=129 pc=01FF8634 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551647 frame=129 pc=01FF8638 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551648 frame=129 pc=01FF863C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551649 frame=129 pc=01FF8640 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551650 frame=129 pc=01FF8644 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551651 frame=129 pc=01FF8648 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551652 frame=129 pc=01FF864C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551653 frame=129 pc=01FF8650 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551654 frame=129 pc=01FF8654 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551655 frame=129 pc=01FF8658 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551656 frame=129 pc=01FF865C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551657 frame=129 pc=01FF8660 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551658 frame=129 pc=01FF8664 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551659 frame=129 pc=01FF8668 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551660 frame=129 pc=01FF866C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551661 frame=129 pc=01FF8698 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551662 frame=129 pc=01FF869C r14=02001D52 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551663 frame=129 pc=01FF86A0 r14=02001D52 r14_usr=020020DF mode=12
TWLHD_IRQ_RETURN cpu=9 seq=62551663 frame=129 ldm_pc=02001D52 r15=02001D50 next_instruction=02001D50 cpsr=0000003F
TWLHD_SP_CHANGE frame=129 pc=02001D50 old_sp=02FF3F68 new_sp=02FF3E28 old_mode=12 new_mode=1F
TWLHD_R14_TRACE seq=62551664 frame=129 pc=02001D50 r14=020020DF r14_usr=020020DF mode=1F
```

**FACT:** the handler's `sub lr, lr, #4` at `0x01FF8604` (seq 62551634) sees
`LR=0x02001D56`; the next instruction at `0x01FF8608` (seq 62551635) sees
`LR=0x02001D52`, which is the value pushed onto the IRQ stack. The popped value
at the `0x01FF86A0` LDM is also `0x02001D52`:

```
$ grep 'TWLHD_IRQ_RETURN cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
TWLHD_IRQ_RETURN cpu=9 seq=62551663 frame=129 ldm_pc=02001D52 r15=02001D50 next_instruction=02001D50 cpsr=0000003F
```

---

## 3. Analysis

### 3.1 LR written on IRQ entry vs the architecturally correct return address

**FACT:** the scratch `armcpu_irqException` writes `R[14] = instruct_adr + 4`
(`$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:661`):

```
$ sed -n '657,662p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
	//TODO - remove GDB specific code
//#ifdef GDB_STUB
//	armcpu->R[14] = armcpu->next_instruction + 4;
//#else
	armcpu->R[14] = armcpu->instruct_adr + 4;
//#endif
```

**FACT:** for the frame-129 exception the value written was `0x02001D56`
(`TWLHD_IRQ_LR_WRITTEN`, §2.1).

**HYPOTHESIS:** the architecturally correct return address for a Thumb IRQ
taken at `instruct_adr=0x02001D52` is `0x02001D52` (resume the instruction at
`0x02001D52`, which has not yet executed). With the standard handler prologue
`sub lr, lr, #4` at `0x01FF8604`, an entry LR of `0x02001D56` produces exactly
that return address: `0x02001D56 - 4 = 0x02001D52`. The log confirms the
handler performs this subtraction and the pushed/popped LR remains
`0x02001D52` (§2.2). Therefore the LR written by `armcpu_irqException` is
consistent with the architecturally correct return address for the point at
which the IRQ was taken.

### 3.2 Decision among §6 hypotheses

**FACT:** there is exactly one `TWLHD_IRQ_FLAG` / `TWLHD_IRQ_EXCEPTION` pair for
ARM9 in frame 129, and it correlates with the single `IRQ_ENTRY` / `IRQ_EXIT`
pair at seq 62551633 / 62551664:

```
$ grep -c 'TWLHD_IRQ_FLAG cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
1
$ grep -c 'TWLHD_IRQ_EXCEPTION cpu=9.*frame=129' /tmp/opencode/c64-irqreturn-r3-e1794921881-3/desmume.log
1
```

**HYPOTHESIS 2 (mismatched IRQ) is therefore ruled out:** no second, unlogged
exception is present in the window.

**FACT:** the popped PC from the exception-return LDM at `0x01FF86A0` is
`0x02001D52`, but the model loads `R[15]` / `next_instruction` as
`0x02001D50` (§2.2). The 2-byte loss happens in the model's LDM exception-return
path, not in the entry LR, not in the handler's `sub lr,#4`, and not in the
value stored on the IRQ stack.

**HYPOTHESIS 1 (DeSmuME return-address computation) is favoured.** The
`TWLHD_IRQ_RETURN` marker that fired is inside `OP_LDMIA2_W`
(`$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/arm_instructions.cpp:5409`);
the 2-byte PC mask is at `:5454`:

```
$ sed -n '5409,5475p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/arm_instructions.cpp
TEMPLATE static u32 DESMUME_FASTCALL  OP_LDMIA2_W(const u32 i)
{
	u32 c = 0;
		
	u32 oldmode = 0;
	u32 start = cpu->R[REG_POS(i,16)];
	u32 * registres;
	u32 tmp;
	Status_Reg SPSR;
//	emu_halt();	
	if(BIT15(i)==0)
	{  
		if((cpu->CPSR.bits.mode==USR)||(cpu->CPSR.bits.mode==SYS)) { printf("ERROR1\n"); return 1; }
		oldmode = armcpu_switchMode(cpu, SYS);
	}

	registres = cpu->R;
	
	OP_L_IA(0, start);
	OP_L_IA(1, start);
	OP_L_IA(2, start);
	OP_L_IA(3, start);
	OP_L_IA(4, start);
	OP_L_IA(5, start);
	OP_L_IA(6, start);
	OP_L_IA(7, start);
	OP_L_IA(8, start);
	OP_L_IA(9, start);
	OP_L_IA(10, start);
	OP_L_IA(11, start);
	OP_L_IA(12, start);
	OP_L_IA(13, start);
	OP_L_IA(14, start);
	
	if(BIT15(i)==0)
	{
		if (!BIT_N(i, REG_POS(i,16)))
			registres[REG_POS(i,16)] = start;
		armcpu_switchMode(cpu, oldmode);
		return MMU_aluMemCycles<PROCNUM>(2, c);
	}

	if (!BIT_N(i, REG_POS(i,16)))
		registres[REG_POS(i,16)] = start + 4;
	tmp = READ32(cpu->mem_if->data, start);
	registres[15] = tmp & (0XFFFFFFFC | (BIT0(tmp)<<1));
	SPSR = cpu->SPSR;
	armcpu_switchMode(cpu, SPSR.bits.mode);
	cpu->CPSR=SPSR;
	armcpu_changeCPSR();
	cpu->next_instruction = registres[15];
	c += MMU_memAccessCycles<PROCNUM,32,MMU_AD_READ>(start);

	// TWLHD diagnostic (scratch build only): log the popped PC at the
	// exception-return LDM site used by the no-reschedule IRQ path.
	if (PROCNUM == 0 && cpu->instruct_adr == 0x01FF86A0)
	{
		extern int currFrameCounter;
		extern u32 TwlhdSeq[2];
		char lbuf[256];
		int ln = snprintf(lbuf, sizeof lbuf,
			"TWLHD_IRQ_RETURN cpu=9 seq=%u frame=%d ldm_pc=%08X r15=%08X next_instruction=%08X cpsr=%08X\n",
			TwlhdSeq[PROCNUM], currFrameCounter, tmp, registres[15], cpu->next_instruction, cpu->CPSR.val);
		ssize_t lw = write(2, lbuf, (size_t)ln); (void)lw;
	}

	return MMU_aluMemCycles<PROCNUM>(2, c);
```

**FACT:** the same file also contains a second `TWLHD_IRQ_RETURN` marker
inside `OP_LDMIA2` (`:5145`):

```
$ sed -n '5140,5210p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/arm_instructions.cpp
		cpu->R[REG_POS(i,16)] = start;

	return MMU_aluMemCycles<PROCNUM>(2, c);
}

TEMPLATE static u32 DESMUME_FASTCALL  OP_LDMIA2(const u32 i)
{
	u32 oldmode = 0;

	u32 c = 0;
	
	u32 start = cpu->R[REG_POS(i,16)];
	u32 * registres;

	if(BIT15(i)==0)
	{  
		if((cpu->CPSR.bits.mode==USR)||(cpu->CPSR.bits.mode==SYS)) { printf("ERROR1\n"); return 1; }
		oldmode = armcpu_switchMode(cpu, SYS);
	}

	registres = cpu->R;
			
	OP_L_IA(0, start);
	OP_L_IA(1, start);
	OP_L_IA(2, start);
	OP_L_IA(3, start);
	OP_L_IA(4, start);
	OP_L_IA(5, start);
	OP_L_IA(6, start);
	OP_L_IA(7, start);
	OP_L_IA(8, start);
	OP_L_IA(9, start);
	OP_L_IA(10, start);
	OP_L_IA(11, start);
	OP_L_IA(12, start);
	OP_L_IA(13, start);
	OP_L_IA(14, start);
	
	if(BIT15(i) == 0)
	{
		armcpu_switchMode(cpu, oldmode);
	}
	else
	{
    
		u32 tmp = READ32(cpu->mem_if->data, start);
		Status_Reg SPSR;
		cpu->R[15] = tmp & (0XFFFFFFFC | (BIT0(tmp)<<1));
		SPSR = cpu->SPSR;
		armcpu_switchMode(cpu, SPSR.bits.mode);
		cpu->CPSR=SPSR;
		armcpu_changeCPSR();
		//start += 4;
		cpu->next_instruction = cpu->R[15];
		c += MMU_memAccessCycles<PROCNUM,32,MMU_AD_READ>(start);
		// TWLHD diagnostic (scratch build only): log the popped PC at the
		// exception-return LDM site used by the no-reschedule IRQ path.
		if (PROCNUM == 0 && cpu->instruct_adr == 0x01FF86A0)
		{
			extern int currFrameCounter;
			extern u32 TwlhdSeq[2];
			char lbuf[256];
			int ln = snprintf(lbuf, sizeof lbuf,
				"TWLHD_IRQ_RETURN cpu=9 seq=%u frame=%d ldm_pc=%08X r15=%08X next_instruction=%08X cpsr=%08X\n",
				TwlhdSeq[PROCNUM], currFrameCounter, tmp, cpu->R[15], cpu->next_instruction, cpu->CPSR.val);
			ssize_t lw = write(2, lbuf, (size_t)ln); (void)lw;
		}

		// debugging
		if (cpu->runToRet) {
			execute = false;
```

**FACT:** the handler instruction at `0x01FF86A0` is an LDM with writeback and
the S-bit set (`ldmeq sp!, {r0, r1, r2, r3, ip, pc}^`).

**HYPOTHESIS:** the fired marker is therefore the one in `OP_LDMIA2_W` (`:5409`).
Both `OP_LDMIA2_W` and `OP_LDMIA2` apply the same mask
`tmp & (0XFFFFFFFC | (BIT0(tmp)<<1))`.

```
$ arm-none-eabi-objdump -d -j .itcm arm9v2/twlhd-arm9v2-autodump-c64.elf | grep '1ff86a0:'
 1ff86a0:	08fd900f	ldmeq	sp!, {r0, r1, r2, r3, ip, pc}^
```

**FACT:** for the failing run, `tmp = 0x02001D52`, so `BIT0(tmp) = 0` and the mask is
`0xFFFFFFFC`, forcing `R[15]` to a word boundary `0x02001D50`.

**HYPOTHESIS:** because the IRQ entry code writes `R14_irq = instruct_adr + 4`
without the Thumb bit, the LDM path misclassifies the return as ARM-state and
strips bit 1.

**HYPOTHESIS 3 (guest hook return path) is ruled out as the cause of the 2-byte
loss.** The per-instruction trace shows `LR_sys` already set to `0x020020DF`
before the IRQ (seq 62551600 onward, §2.2) and the handler does not restore
`LR_sys` on the no-reschedule path (established in
`docs/reports/cubes64-irq-lr-trace.md` §4). The re-execution of the BL suffix
with the stale `LR_sys` is a *consequence* of the incorrect return address, not
the cause.

### 3.3 Open item 4

**HYPOTHESIS:** Open item 4 from `docs/reports/cubes64-irq-lr-trace.md` §5 is
consistent with correct execution, not a model artefact. The 64-entry PC ring
shows the hook-body PC `0x020022CC` as the last guest PC before the IRQ vector
because `0x020022CC` is the hook epilogue that returns to the BL call site. The
per-instruction trace (§2.2) places `pc=0x020022CC` at seq 62551631 and the
next record is `TWLHD_IRQ_FLAG ... ia=0x02001D52`: this is consistent with the
IRQ being taken at the hook-return boundary.

**FACT:** the instruction at `0x020022CC` is the hook epilogue
`pop {r4, pc}`:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -A2 -B2 '20022cc:'
 20022c6:	4661    	mov	r1, ip
 20022c8:	f7ff fed0	bl	200206c <twlhd_gx_rec_write.part.0>
 20022cc:	bd10    	pop	{r4, pc}
 20022ce:	4a04    	ldr	r2, [pc, #16]	@ (20022e0 <twlhd_gx_rec_hook+0x2c>)
 20022d0:	5883    	ldr	r3, [r0, r2]
```

**HYPOTHESIS:** this `pop {r4, pc}` loads the saved return address from the
stack. Because the immediately following `TWLHD_IRQ_FLAG` line records
`ia=0x02001D52`, the popped value is the saved LR for the `bl
twlhd_gx_rec_hook` call (interworking address `0x02001D53`, whose Thumb
execution address is `0x02001D52`). The ring observation is therefore
consistent with the CPU executing the hook epilogue and the IRQ being taken
exactly at the return to `0x02001D52`; this is a **HYPOTHESIS**, not a proven
fact.

### 3.4 Real-hardware impact and proposed guest-side check

**HYPOTHESIS:** real ARM946E-S hardware is not affected by this specific
behaviour. The architectural exception-return LDM uses the SPSR state (Thumb
bit) to determine the return-PC alignment, so a return to a halfword-aligned
Thumb address such as `0x02001D52` would be preserved. This remains **HYPOTHESIS**
because no hardware run was performed.

**Proposed minimal guest-side check (not implemented):** build a minimal ROM
whose ITCM IRQ handler records `R14_irq` at entry and the actual return address
after a `sub lr,#4` / exception-return LDM, triggered at a controlled Thumb
instruction boundary (e.g. a timer IRQ between two known Thumb instructions at
a halfword-aligned address ending in `...2`). The expected architectural result
is that the popped return PC equals the address of the next Thumb instruction.
Running it first on the same scratch emulator would confirm whether the 2-byte
loss reproduces on a simplified case; running it on hardware would validate the
HYPOTHESIS above.

---

## 4. Call-site survey

**FACT:** `armcpu_irqException` has one live dispatch call site in the searched
scratch source tree (`$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/NDSSystem.cpp:2300`);
the occurrence at `$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:1054`
is inside a `#if 0` block:

```
$ sed -n '1047,1062p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
#if 0 //#ifdef GDB_STUB
	if (ARMPROC.stalled) {
		return STALLED_CYCLE_COUNT;
	}

	/* check for interrupts */
	if (ARMPROC.irq_flag) {
		armcpu_irqException(&ARMPROC);
	}

	cFetch = armcpu_prefetch(&ARMPROC);

	if (ARMPROC.stalled) {
		return MMU_fetchExecuteCycles<PROCNUM>(cExecute, cFetch);
	}
#endif
```

**FACT:** a tree-wide `grep` finds the same two call sites plus the definition
and a comment (literal `\t` sequences replace real tabs):

```
$ grep -rn 'armcpu_irqException' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/NDSSystem.cpp:2286:\t\t// exact moment the IRQ is flagged, just before the call to armcpu_irqException.
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/NDSSystem.cpp:2300:\t\tarmcpu_irqException(&ARMPROC);
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:585:\t//TODO - merge with armcpu_irqException?
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:620:BOOL armcpu_irqException(armcpu_t *armcpu)
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:1054:\t\tarmcpu_irqException(&ARMPROC);
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.h:344:BOOL armcpu_irqException(armcpu_t *armcpu);
```

**HYPOTHESIS:** the `armcpu.cpp:1054` path is unused in the scratch build
because it is guarded by `#if 0`; this is consistent with the single live call
site at `NDSSystem.cpp:2300`.

---

## Open items

1. **UNVERIFIED:** whether real hardware reproduces the same CUBES=64
   AUTODUMP_FRAME=120 fault. No hardware run exists.
2. **UNVERIFIED:** whether the proposed minimal guest-side check actually
   reproduces the 2-byte loss on the scratch emulator; the check is described
   but not implemented.

---

## Requirement traceability

- **REQ-1 (new patch exists and stacks cleanly).** §1 quotes `wc -l` (120
  lines) and the reverse `patch --dry-run -R` success against the r1+r2 patched
  tree.
- **REQ-2 (new build dir linked to scratch tree).** §1 quotes
  `meson introspect --buildsystem-files` and the `ninja` tail showing
  `ninja: no work to do.` (the build directory was already up to date).
- **REQ-3 (fresh reproduction with cflash provenance).** §1 quotes the full
  launch command, the `set -x` `+ mkfs.fat -C ...` line, the ROM CRC, the
  post-run `sha256sum`, and the identical `TWLHD_BKPT` and
  `IRQ_ENTRY`/`IRQ_EXIT` lines.
- **REQ-4 (experiment data quoted).** §2.1 quotes `TWLHD_IRQ_FLAG`,
  `TWLHD_IRQ_EXCEPTION`, `TWLHD_IRQ_MODE_SWITCH`, `TWLHD_IRQ_LR_WRITTEN` and
  `TWLHD_IRQ_RETURN`; §2.2 quotes the complete 117-line `awk` output (103 `TWLHD_R14_TRACE` records plus 6 `TWLHD_SP_CHANGE` and 8 `TWLHD_IRQ_*` lines) for seq 62551562–62551664 via one `awk` command.
- **REQ-5 (four numbered answers).** §3.1 states the LR written vs the
  architecturally correct return address with the scratch `armcpu.cpp:661`
  source lines; §3.2 decides among hypotheses 1/2/3; §3.3 gives Open item 4 as a
  **HYPOTHESIS** of correct hook-return execution (not a model artefact) with
  the `0x020022CC` disassembly; §3.4 gives hardware impact as HYPOTHESIS and
  describes the unimplemented guest-side check.
- **REQ-6 (only allowed repo files changed).** Scope paragraph states the repo
  files changed are this report and the appended Update in
  `cubes64-irq-lr-trace.md`; `cubes64-entry-trace.md` and
  `cubes64-branch0-diagnosis.md` were reverted byte-for-byte to their `main`
  versions. No product code, tests or harness were edited; the only change to an earlier report is the appended Update section in `cubes64-irq-lr-trace.md`. Its earlier text is byte-identical to `main`.
- **REQ-7 (REQ traceability).** This section provides one REQ line per
  acceptance criterion with evidence pointers.
- **REQ-8 (`make test` green).** Verified below.
- **REQ-9 (privacy / path scrubbing).** Path note at the top of this report
  discloses the `$HOME` and `\t` substitutions in quoted outputs.

---

## Verification

```
$ make test 2>&1 | tail -20
OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq_scenes.py'
................
----------------------------------------------------------------------
Ran 16 tests in 1.220s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
...............................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 364 tests in 88.712s

OK (skipped=42)
/tmp/tmpque1hr7a/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```
