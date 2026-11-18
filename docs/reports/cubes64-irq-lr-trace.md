<!-- SPDX-License-Identifier: Apache-2.0 -->
# CUBES=64 AUTODUMP_FRAME=120 IRQ/LR trace follow-up

Path note: literal home-directory paths in quoted commands and outputs were replaced by $HOME for publication; this is the only edit to the quoted evidence; no other text was changed.

**Scope:** DIAGNOSIS + EMULATOR only. No product code, ROM source, test,
interpreter, renderer, harness or pristine DeSmuME tree was modified. The only
repo files changed are this report, an append-only `Update` section in
`docs/reports/cubes64-entry-trace.md`, and a consistency update in
`docs/reports/cubes64-branch0-diagnosis.md`. A scratch copy of DeSmuME outside
the repository (`$HOME/3ds/hwtest-staging/desmume-dbg`, documented in
`docs/reports/bkpt-diagnosis.md` section 1) was extended with a new diagnostic
patch (`$HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace-r2.patch`)
and built into a new scratch CLI
(`$HOME/3ds/hwtest-staging/desmume-dbg-build4/cli/desmume-cli`). The
round-1 patch (`cubes64-irq-lr-trace.patch`) is unchanged and is cited by the
round-1 log evidence that remains valid. No console, SD-card or network action
was performed.

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

**FACT:** the scratch patch file exists and is 183 lines:

```
$ wc -l $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace.patch
183 $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace.patch
```

**FACT:** the patch applies cleanly to the already-patched scratch tree and
produces the expected source changes:

```
$ mkdir -p /tmp/patch-test3/src && cp /tmp/patch-a/src/* /tmp/patch-test3/src/ && patch -p1 -d /tmp/patch-test3 < $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace.patch && diff -q /tmp/patch-test3/src/armcpu.h /tmp/patch-b/src/armcpu.h && diff -q /tmp/patch-test3/src/armcpu.cpp /tmp/patch-b/src/armcpu.cpp
patching file src/armcpu.h
patching file src/armcpu.cpp
```

(`/tmp/patch-a` and `/tmp/patch-b` are local snapshots of the scratch tree
before and after applying this patch.)

**FACT:** the round-2 scratch patch file exists and is 146 lines:

```
$ wc -l $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace-r2.patch
146 $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace-r2.patch
```

**FACT:** `desmume-dbg-build4` was configured from the scratch source tree:

```
$ meson introspect --buildsystem-files $HOME/3ds/hwtest-staging/desmume-dbg-build4
["$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/meson.build", "$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/meson_options.txt", "$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix/cli/meson.build"]
```

**FACT:** the round-2 patch is the one applied to that source tree. A reverse dry-run against the tree succeeds, and the tree contains the r2-only `TWLHD_IRQTRACE` instrumentation:

```
$ patch --dry-run -R -p1 -d $HOME/3ds/hwtest-staging/desmume-dbg < $HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace-r2.patch
checking file desmume/src/armcpu.h
checking file desmume/src/armcpu.cpp
$ grep -c 'TWLHD_IRQTRACE' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
6
```

The `TWLHD_IRQTRACE_DUMP` output in §3.2 is the behavioural confirmation that
the running `desmume-dbg-build4` binary contains this instrumentation.

**FACT:** the scratch CLI was built in a new directory (`desmume-dbg-build4`)
and linked successfully. The full `meson setup` output is elided here; the build
result is quoted as the complete output of the narrow tail command:

```
$ ninja -C $HOME/3ds/hwtest-staging/desmume-dbg-build4 2>&1 | tail -2
[142/143] Linking static target libdesmume.a
[143/143] Linking target cli/desmume-cli
```

**FACT:** the reproduction protocol follows `tools/emulator/desmume_capture.sh:74`
for cflash creation and `tools/emulator/desmume_capture.sh:86-92` for the
emulator launch. The §3 log evidence comes from the original
`/tmp/opencode/c64-irqtrace-r2v2-e1794921881-1` run. No saved script output
contains the `set -x` `+ mkfs.fat` creation line for that run, so the
*creation provenance* of its `cflash.img` is **UNVERIFIED**. The post-run image
currently on disk is:

```
$ sha256sum /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/cflash.img
c7aaa6a68b13765821b305150a82eae252b070ce54ed730216388ade6db4b6a7  /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/cflash.img
```

The §3 log quotes below remain from that same `-1` run.

**FACT:** the original fault-reproduction runs used the same capture script with
`DESMUME_TIMEOUT=30` and a scratch CLI. The base protocol is quoted in
`docs/reports/cubes64-branch0-diagnosis.md:183-187`:

```
DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli
DESMUME_TIMEOUT=30
tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds <outdir>
```

The instrumented entry-trace runs added `TWLHD_RTC_EPOCH=1794925481` and a
windowed PC trace (`TWLHD_PCLOG2`) as shown in
`docs/reports/cubes64-entry-trace.md:396-400`:

```
TWLHD_RTC_EPOCH=1794925481 \
TWLHD_PCLOG2=/tmp/opencode/c64-final1/pclog2.bin \
DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli \
DESMUME_TIMEOUT=30 \
tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-final1
```

(`TWLHD_PCLOG`, the opt-in 4 M-record PC trace mentioned in
`docs/reports/bkpt-diagnosis.md:93-94` and used for the pc-trace run in
`docs/reports/cubes64-branch0-diagnosis.md:193`, is likewise an emulator-side
logging option.)

**FACT:** the quoted build4 run used only `TWLHD_RTC_EPOCH=1794921881` (no
`TWLHD_PCLOG2` override) and the original `cflash.img` from the `-1` run
(creation provenance **UNVERIFIED**).

**FACT:** the scratch source tree reads `TWLHD_PCLOG`/`TWLHD_PCLOG2` only in
`desmume/src/armcpu.cpp` to open optional binary log files and write PC/sp/cpsr
records to them; the variables are not used elsewhere in the searched emulator
core source tree:

```
$ grep -rn 'TWLHD_PCLOG' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:712:			const char *lp = getenv("TWLHD_PCLOG");
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:764:			const char *lp = getenv("TWLHD_PCLOG2");
```

The `armcpu.cpp` occurrences are:

```
$ grep -n -A9 -B1 'TWLHD_PCLOG' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
711-		{
712:			const char *lp = getenv("TWLHD_PCLOG");
713-			twlhd_pclog_init = true;
714-			if (lp != NULL && PROCNUM == 0) twlhd_pclog = fopen(lp, "wb");
715-		}
716-		if (twlhd_pclog != NULL && twlhd_pclog_n < 4000000)
717-		{
718-			u32 rec[2] = { (u32)PROCNUM, ARMPROC.instruct_adr };
719-			fwrite(rec, sizeof(rec), 1, twlhd_pclog);
720-			if (++twlhd_pclog_n == 4000000) { fclose(twlhd_pclog); twlhd_pclog = NULL; }
721-		}
--
763-			twlhd_pclog2_init = 1;
764:			const char *lp = getenv("TWLHD_PCLOG2");
765-			if (lp != NULL) twlhd_pclog2 = fopen(lp, "wb");
766-		}
767-		if (twlhd_pclog2 != NULL && PROCNUM == 0)
768-		{
769-			if (currFrameCounter >= 125 && currFrameCounter <= 132) twlhd_pclog2_active = 1;
770-			if (twlhd_pclog2_active)
771-			{
772-				u32 rec[3] = { ARMPROC.instruct_adr, ARMPROC.R[13], ARMPROC.CPSR.val };
773-				fwrite(rec, sizeof(rec), 1, twlhd_pclog2);
```

**HYPOTHESIS:** dropping `TWLHD_PCLOG`/`TWLHD_PCLOG2` does not change the
emulated run: both are opt-in outputs produced by the *scratch emulator*, not
inputs to the emulated ROM (`TWLHD_PCLOG` is a 4 M-record PC trace,
`bkpt-diagnosis.md:93-94`; `TWLHD_PCLOG2` is a windowed binary PC trace,
`cubes64-entry-trace.md:366`).
`TWLHD_RTC_EPOCH=1794921881` is the epoch that reproduced the fault in the
entry-trace §2.4 sweep. The full `desmume_capture.sh` trace output is omitted;
the result line is quoted as the complete output of the narrow tail command:

```
$ TWLHD_RTC_EPOCH=1794921881 \
  DESMUME_CLI=$HOME/3ds/hwtest-staging/desmume-dbg-build4/cli/desmume-cli \
  DESMUME_TIMEOUT=30 \
  tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1 2>&1 | tail -1
capture: /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/twlhd_frame_v2.bin
```

**FACT:** the build4 run reproduced the reference fault:

```
$ grep -m1 '^TWLHD_BKPT cpu=ARM9' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

---

## 2. Register banking in the scratch emulator

**FACT:** DeSmuME banks the user/system LR in `armcpu_t::R14_usr`
(`$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.h:268`):

```
$ sed -n '268,273p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.h
	u32 R13_usr, R14_usr;
	u32 R13_svc, R14_svc;
	u32 R13_abt, R14_abt;
	u32 R13_und, R14_und;
	u32 R13_irq, R14_irq;
	u32 R8_fiq, R9_fiq, R10_fiq, R11_fiq, R12_fiq, R13_fiq, R14_fiq;
```

**FACT:** `armcpu_switchMode` saves the current `R[14]` into `R14_usr` when
leaving USR/SYS mode and loads `R[14]` from `R14_usr` when entering USR/SYS mode
(`$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp:377-380,
425-428`):

```
$ sed -n '375,382p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
	switch(oldmode)
	{
		case USR :
		case SYS :
			armcpu->R13_usr = armcpu->R[13];
			armcpu->R14_usr = armcpu->R[14];
			break;
			
$ sed -n '423,430p' $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src/armcpu.cpp
		switch(mode)
		{
			case USR :
			case SYS :
				armcpu->R[13] = armcpu->R13_usr;
				armcpu->R[14] = armcpu->R14_usr;
				//SPSR = CPSR;
				break;
```

**Important consequence:** in SYS mode the *current* LR is `R[14]`; the banked
field `R14_usr` is only updated on a mode switch. The instrumentation therefore
reports `lr_sys` as `R[14]` when the current mode is USR/SYS and as `R14_usr`
otherwise. This is stated as FACT about the model.

---

## 3. Raw log evidence around the fault

The round-2 diagnostic patch (`cubes64-irq-lr-trace-r2.patch`) keeps the
round-1 instrumentation and adds:
- a dedicated 32-entry IRQ entry/exit ring (`TWLHD_IRQTRACE`) with seq, frame,
  pc, sp, cpsr, mode, `lr` and `lr_sys`;
- a windowed per-instruction `TWLHD_R14_TRACE` for every ARM9 instruction from
  the last `BL_SUFFIX` in frame 129 through the matching `IRQ_EXIT`;
- a richer `TWLHD_IRQ_EXCEPTION` line logging `instruct_adr`,
  `next_instruction`, `R[15]`, the pre-exception `lr`/`lr_sys`, the CPSR
  T-bit, and the `R14_irq` value written by `armcpu_irqException`.

### 3.1 Planner trigger and PC ring

**FACT:** the build4 run entered the planner mid-function at `0x02002640` with
`LR=0x02001D53`:

```
$ grep '^TWLHD_PLAN_RANGE_ENTRY ' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_PLAN_RANGE_ENTRY frame=129 pc=02002640 sp=02FF3E28 lr=02001D53 cpsr=0000003F mode=1F r0=00000001 r1=00021000 r2=02048EE8 r3=00000004 lrslot=02FF3E18..02FF3E40
```

**FACT:** the 64-entry PC ring at the trigger shows the last non-handler PC
before the IRQ vector was `0x020022CC` (inside `twlhd_gx_rec_hook`), and the
handler returned to `0x02001D50` (the BL suffix), then jumped to `0x02002640`:

```
$ grep 'TWLHD_PLAN_RANGE_ENTRY_RING' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_PLAN_RANGE_ENTRY_RING 02002E2C 02002E2E 02002E30 02002E32 02002E34 02002E5A 02002E5C 02002E3C 02002E3E 02002E42 02002E44 02002E38 020020DE 020020E0 020020E2 020020E4 02002162 02002164 02002166 02002168 0200216A 0200216C 0200216E 02002170 02002172 02002174 02002176 02002178 020020AE 020022CC 00000018 01FF8034 01FF8604 01FF8608 01FF860C 01FF8610 01FF8614 01FF8618 01FF861C 01FF8620 01FF8624 01FF8628 01FF862C 01FF8630 01FF8634 01FF8638 01FF863C 01FF8640 01FF8644 01FF8648 01FF864C 01FF8650 01FF8654 01FF8658 01FF865C 01FF8660 01FF8664 01FF8668 01FF866C 01FF8698 01FF869C 01FF86A0 02001D50 02002640
```

### 3.2 IRQ exception call and trace rings

**FACT:** the ARM9 IRQ exception at frame 129 was entered with
`instruct_adr=0x02001D52`, `next_instruction=0x02001D54`, `R15=0x02001D56`,
pre-exception `lr=lr_sys=0x020020DF`, Thumb mode (`t_before=1`), and wrote
`R14_irq=0x02001D56`:

```
$ grep 'TWLHD_IRQ_EXCEPTION cpu=9 frame=129' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_IRQ_EXCEPTION cpu=9 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 lr_before=020020DF lr_sys_before=020020DF cpsr_before=0000003F t_before=1 r14_irq=02001D56 cpsr_after=00000092 t_after=0
```

**FACT:** the dedicated IRQ-entry/exit ring dumped at the planner trigger shows
the faulting pair plus fifteen earlier pairs (16 pairs total) before the trigger:

```
$ grep -A 32 '^TWLHD_IRQTRACE_DUMP cpu=9' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_IRQTRACE_DUMP cpu=9 ring newest->oldest
TWLHD_IRQTRACE IRQ_EXIT seq=62551664 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=0000003F
TWLHD_IRQTRACE IRQ_ENTRY seq=62551633 frame=129 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=02001D56 lr_sys=020020DF spsr=0000003F
TWLHD_IRQTRACE IRQ_EXIT seq=62090653 frame=128 pc=01FF86A4 sp=02FF3F68 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62090645 frame=128 pc=01FF865C sp=02FF3F68 cpsr=00000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62090639 frame=128 pc=0200C704 sp=02FF3F40 cpsr=60000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62090582 frame=128 pc=01FF81A0 sp=02FF3F40 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62090549 frame=128 pc=0200C64C sp=02FF3F68 cpsr=20000092 mode=12 lr=01FF865C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62090526 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62090523 frame=128 pc=01FF8538 sp=00000000 cpsr=0000001F mode=1F lr=01FF8534 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62090492 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62089848 frame=128 pc=01FF86A4 sp=02FF3F68 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62089840 frame=128 pc=01FF865C sp=02FF3F68 cpsr=00000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62089834 frame=128 pc=0200C704 sp=02FF3F40 cpsr=60000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62089777 frame=128 pc=01FF81A0 sp=02FF3F40 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62089744 frame=128 pc=0200C64C sp=02FF3F68 cpsr=20000092 mode=12 lr=01FF865C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62089721 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62089718 frame=128 pc=01FF8538 sp=00000000 cpsr=0000001F mode=1F lr=01FF8534 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62089687 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62089684 frame=128 pc=01FF8538 sp=00000000 cpsr=0000001F mode=1F lr=01FF8534 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62089653 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62088753 frame=128 pc=01FF86A4 sp=02FF3F68 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62088745 frame=128 pc=01FF865C sp=02FF3F68 cpsr=00000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62088739 frame=128 pc=0200C704 sp=02FF3F40 cpsr=60000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62088682 frame=128 pc=01FF81A0 sp=02FF3F40 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62088649 frame=128 pc=0200C64C sp=02FF3F68 cpsr=20000092 mode=12 lr=01FF865C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62088626 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62088623 frame=128 pc=01FF8538 sp=00000000 cpsr=0000001F mode=1F lr=01FF8534 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62088592 frame=128 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=01FF853C lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62087692 frame=128 pc=01FF86A4 sp=02FF3F68 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62087684 frame=128 pc=01FF865C sp=02FF3F68 cpsr=00000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_EXIT seq=62087678 frame=128 pc=0200C704 sp=02FF3F40 cpsr=60000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
TWLHD_IRQTRACE IRQ_ENTRY seq=62087621 frame=128 pc=01FF81A0 sp=02FF3F40 cpsr=20000092 mode=12 lr=0200C704 lr_sys=01FF8534 spsr=0000001F
```

**FACT:** the dumped LR trace ring (newest first) around the fault is:

```
$ grep -A 32 'TWLHD_LRTRACE_DUMP cpu=9' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_LRTRACE_DUMP cpu=9 ring newest->oldest
TWLHD_LRTRACE IRQ_EXIT seq=62551664 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=0000003F
TWLHD_LRTRACE BL_SUFFIX seq=62551664 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=0000003F
TWLHD_LRTRACE IRQ_ENTRY seq=62551633 frame=129 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=02001D56 lr_sys=020020DF spsr=0000003F
TWLHD_LRTRACE BL_SUFFIX seq=62551562 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62551561 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62543825 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62543824 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62536088 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62536087 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62528351 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62528350 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62520614 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62520613 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62512877 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62512876 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62505140 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62505139 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62497403 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62497402 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62489666 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62489665 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62481929 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62481928 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62474192 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62474191 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62466455 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62466454 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62458718 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62458717 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62450981 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
TWLHD_LRTRACE BL_PREFIX seq=62450980 frame=129 pc=02001D4E sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=200000DF
TWLHD_LRTRACE BL_SUFFIX seq=62443244 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=02001D52 lr_sys=02001D52 spsr=200000DF
```

Reading the ring in chronological order (reverse the dump), the last complete
BL execution before the fault is:

- seq 62551561: `BL_PREFIX pc=0x02001D4E lr=0x020020DF lr_sys=0x020020DF`
- seq 62551562: `BL_SUFFIX pc=0x02001D50 lr=0x02001D52 lr_sys=0x02001D52`
- seq 62551563–62551631: guest instructions (69 instructions; no IRQ entry in this window)
- seq 62551632: exception-vector step (`pc=0x00000018 mode=0x12`)
- seq 62551633: `IRQ_ENTRY pc=0x01FF8034 lr_irq=0x02001D56 lr_sys=0x020020DF`
- seq 62551664: `IRQ_EXIT pc=0x02001D50 lr=0x020020DF lr_sys=0x020020DF`
- seq 62551664: `BL_SUFFIX pc=0x02001D50 lr=0x020020DF lr_sys=0x020020DF`

The next recorded PC after the second `BL_SUFFIX` is `0x02002640` (the planner
entry). The gap from the final `BL_SUFFIX` (seq 62551562) to `IRQ_ENTRY` (seq
62551633) contains 69 guest instructions plus the exception-vector step (seq
62551632, `pc=0x00000018 mode=0x12`), as counted from the per-instruction trace
below.

**FACT:** the per-instruction `R14`/`R14_usr` trace from the final `BL_SUFFIX`
through `IRQ_ENTRY` is (filtered from `TWLHD_R14_TRACE`; `TWLHD_SP_CHANGE`
lines remain in the raw output):

```
$ awk '/^TWLHD_R14_TRACE seq=62551562/{p=1} p{print} /^TWLHD_R14_TRACE seq=62551633/{exit}' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
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
TWLHD_IRQ_EXCEPTION cpu=9 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 lr_before=020020DF lr_sys_before=020020DF cpsr_before=0000003F t_before=1 r14_irq=02001D56 cpsr_after=00000092 t_after=0
TWLHD_IRQ_EXCEPTION cpu=7 frame=129 ia=037FC64A ni=037FC64C r15=037FC64E lr_before=037FC649 lr_sys_before=037FC649 cpsr_before=0000003F t_before=1 r14_irq=037FC64E cpsr_after=00000092 t_after=0
TWLHD_SP_CHANGE frame=129 pc=00000018 old_sp=02FF3E20 new_sp=02FF3F80 old_mode=1F new_mode=12
TWLHD_R14_TRACE seq=62551632 frame=129 pc=00000018 r14=02001D56 r14_usr=020020DF mode=12
TWLHD_R14_TRACE seq=62551633 frame=129 pc=01FF8034 r14=02001D56 r14_usr=020020DF mode=12
```

### 3.3 Answers to the three questions

**(a) Did an IRQ occur between `0x02001D4E` and `0x02001D50`?**

**FACT:** the LR trace ring does not show an IRQ entry between a `BL_PREFIX`
event and the following `BL_SUFFIX` event. The chronological order is
`BL_PREFIX` (seq 62551561), `BL_SUFFIX` (seq 62551562), then 69 guest
instructions, then the exception-vector step (seq 62551632,
`pc=0x00000018 mode=0x12`), then `IRQ_ENTRY` (seq 62551633). Therefore the log
does **not** establish that an IRQ occurred between the two halfwords of the
Thumb BL pair.

**HYPOTHESIS:** DeSmuME delivered the IRQ after the BL prefix/suffix pair
completed; the `TWLHD_IRQ_EXCEPTION` line records `ia=0x02001D52`, i.e. the
instruction after the BL, which is consistent with the IRQ being taken after
the prefix/suffix pair.

**(b) What was the LR value before the suffix that misbranches?**

**FACT:** at the post-IRQ `BL_SUFFIX` event (seq 62551664) the current LR and
`lr_sys` are both `0x020020DF`. This is the value used by the suffix to compute
its branch target.

**(c) Does `LR_sys` differ before IRQ entry vs after exit?**

**FACT:** at `IRQ_ENTRY` (seq 62551633) `lr_sys=0x020020DF`. At `IRQ_EXIT`
(seq 62551664) `lr_sys=0x020020DF`. The handler did not change `LR_sys`; it
was already `0x020020DF` when the IRQ was taken.

**FACT:** the per-instruction trace shows `LR_sys` changed from `0x02001D52`
(seq 62551562, the `BL_SUFFIX` event, value captured before the suffix ran) to
`0x020020DF` (seq 62551600, inside guest hook-body execution) before the IRQ
entry at seq 62551633. After the suffix the model holds `LR=0x02001D53` (seq
62551563), so the `BL_SUFFIX` event's `lr` field is the value *before* the
suffix ran. The change from `0x02001D53` to `0x020020DF` occurred during guest
(hook body) execution, not inside the handler.

---

## 4. ITCM IRQ handler disassembly

**FACT:** `arm-none-eabi-readelf -S --wide` places `.itcm` at address
`0x01FF8020` with size `0x0006C8` and `.main` at `0x020013C0`:

```
$ arm-none-eabi-readelf -S --wide arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '\.itcm|\.main '
  [ 6] .itcm             PROGBITS        01ff8020 004f2c 0006c8 00  AX  0   0  4
  [ 9] .main             PROGBITS        020013c0 0055f8 01ab6c 00  AX  0   0  8
```

**FACT:** the ITCM exception vector at `0x01FF8034` loads the IRQ handler
address `0x01FF8604` from `0x01FF804C`:

```
$ arm-none-eabi-objdump -d -j .itcm arm9v2/twlhd-arm9v2-autodump-c64.elf | head -21

arm9v2/twlhd-arm9v2-autodump-c64.elf:     file format elf32-littlearm


Disassembly of section .itcm:

01ff8020 <__itcm_start+0x20>:
 1ff8020:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8038 <__excpt_vectors>
 1ff8024:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff803c <__excpt_vectors+0x4>
 1ff8028:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8040 <__excpt_vectors+0x8>
 1ff802c:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8044 <__excpt_vectors+0xc>
 1ff8030:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff8048 <__excpt_vectors+0x10>
 1ff8034:	e59ff010 	ldr	pc, [pc, #16]	@ 1ff804c <__excpt_vectors+0x14>

01ff8038 <__excpt_vectors>:
 1ff8038:	01ff853c 	.word	0x01ff853c
 1ff803c:	01ff8544 	.word	0x01ff8544
 1ff8040:	ffff0008 	.word	0xffff0008
 1ff8044:	01ff8540 	.word	0x01ff8540
 1ff8048:	01ff8550 	.word	0x01ff8550
 1ff804c:	01ff8604 	.word	0x01ff8604
```

**FACT:** the handler body `__arm_excpt_irq` at `0x01FF8604` runs in ARM mode
(its first instruction is a 32-bit ARM `sub lr, lr, #4` and the runtime log
shows `cpsr=0x00000092`, mode `0x12` = IRQ). It saves `LR_irq` on the IRQ
stack and restores it on the no-reschedule return path, but does not save or
restore `LR_sys`:

```
$ arm-none-eabi-objdump -d -j .itcm arm9v2/twlhd-arm9v2-autodump-c64.elf | sed -n '/^01ff8604 <__arm_excpt_irq>:/,/^01ff86e8/p'
01ff8604 <__arm_excpt_irq>:
 1ff8604:       e24ee004        sub     lr, lr, #4
 1ff8608:       e92d500f        push    {r0, r1, r2, r3, ip, lr}
 1ff860c:       e59fc0c4        ldr     ip, [pc, #196]  @ 1ff86d8 <__arm_excpt_irq+0xd4>
 1ff8610:       e89c0003        ldm     ip, {r0, r1}
 1ff8614:       e0100001        ands    r0, r0, r1
 1ff8618:       08fd900f        ldmeq   sp!, {r0, r1, r2, r3, ip, pc}^
 1ff861c:       e2601000        rsb     r1, r0, #0
 1ff8620:       e0011000        and     r1, r1, r0
 1ff8624:       e16f2f11        clz     r2, r1
 1ff8628:       e3a03001        mov     r3, #1
 1ff862c:       e262101f        rsb     r1, r2, #31
 1ff8630:       e1a02113        lsl     r2, r3, r1
 1ff8634:       e59f30a0        ldr     r3, [pc, #160]  @ 1ff86dc <__arm_excpt_irq+0xd8>
 1ff8638:       e58c2004        str     r2, [ip, #4]
 1ff863c:       e5930000        ldr     r0, [r3]
 1ff8640:       e1800002        orr     r0, r0, r2
 1ff8644:       e5830000        str     r0, [r3]
 1ff8648:       e59f3090        ldr     r3, [pc, #144]  @ 1ff86e0 <__arm_excpt_irq+0xdc>
 1ff864c:       e7933101        ldr     r3, [r3, r1, lsl #2]
 1ff8650:       ee0d2f30        mcr     15, 0, r2, cr13, cr0, {1}
 1ff8654:       e3530000        cmp     r3, #0
 1ff8658:       112fff33        blxne   r3
 1ff865c:       ee1d3f30        mrc     15, 0, r3, cr13, cr0, {1}
 1ff8660:       e59f207c        ldr     r2, [pc, #124]  @ 1ff86e4 <__arm_excpt_irq+0xe0>
 1ff8664:       e592100c        ldr     r1, [r2, #12]
 1ff8668:       e0133001        ands    r3, r3, r1
 1ff866c:       0a000009        beq     1ff8698 <__arm_excpt_irq+0x94>
 1ff8670:       e1c11003        bic     r1, r1, r3
 1ff8674:       e582100c        str     r1, [r2, #12]
 1ff8678:       e59fc05c        ldr     ip, [pc, #92]   @ 1ff86dc <__arm_excpt_irq+0xd8>
 1ff867c:       e59c1000        ldr     r1, [ip]
 1ff8680:       e1c11003        bic     r1, r1, r3
 1ff8684:       e58c1000        str     r1, [ip]
 1ff8688:       e2820010        add     r0, r2, #16
 1ff868c:       e1a01003        mov     r1, r3
 1ff8690:       ebffff45        bl      1ff83ac <threadUnblockAllByMask>
 1ff8694:       e59f2048        ldr     r2, [pc, #72]   @ 1ff86e4 <__arm_excpt_irq+0xe0>
 1ff8698:       e5920004        ldr     r0, [r2, #4]
 1ff869c:       e3500000        cmp     r0, #0
 1ff86a0:       08fd900f        ldmeq   sp!, {r0, r1, r2, r3, ip, pc}^
 1ff86a4:       e3a03000        mov     r3, #0
 1ff86a8:       e5921000        ldr     r1, [r2]
 1ff86ac:       e8820009        stm     r2, {r0, r3}
 1ff86b0:       e14f2000        mrs     r2, SPSR
 1ff86b4:       e5812040        str     r2, [r1, #64]   @ 0x40
 1ff86b8:       e8bd000c        pop     {r2, r3}
 1ff86bc:       e8a1000c        stmia   r1!, {r2, r3}
 1ff86c0:       e8bd500c        pop     {r2, r3, ip, lr}
 1ff86c4:       e8c17ffc        stmia   r1, {r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, sp, lr}^
 1ff86c8:       e581e034        str     lr, [r1, #52]   @ 0x34
 1ff86cc:       e321f0d3        msr     CPSR_c, #211    @ 0xd3
 1ff86d0:       e581d03c        str     sp, [r1, #60]   @ 0x3c
 1ff86d4:       eaffff8f        b       1ff8518 <armContextLoadFromSvc>
 1ff86d8:       04000210        .word   0x04000210
 1ff86dc:       02ff3ff8        .word   0x02ff3ff8
 1ff86e0:       02ff0000        .word   0x02ff0000
 1ff86e4:       02ff0080        .word   0x02ff0080
```

**FACT:** the handler saves `LR_irq` with `push {r0, r1, r2, r3, ip, lr}` at
`0x01FF8608` and restores it with `ldmeq sp!, {r0, r1, r2, r3, ip, pc}^` at
`0x01FF8618` (spurious-no-interrupt path) and `0x01FF86A0` (normal
no-reschedule path). The `^` suffix restores CPSR from SPSR, returning to the
original (SYS) mode.

**FACT:** the only references to the user/system LR in the handler are the
`stmia r1, {r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, sp, lr}^` at
`0x01FF86C4` and `str lr, [r1, #52]` at `0x01FF86C8`, both on the *reschedule*
path. They store the current user-mode LR into a thread-context block; they do
**not** restore it on the no-reschedule return path. Therefore, on the path
taken in the failing run, the handler neither saves nor restores `LR_sys`.

---

## 5. Assessment

**FACT:** the planner entry is reached after the ITCM IRQ handler returns to
`0x02001D50`, the BL suffix halfword, with `LR=LR_sys=0x020020DF`. The suffix
computes a branch target from that LR and the next observed PC is
`0x02002640`.

**FACT:** the BL encoding at `0x02001D4E` is `f000 fab1`, i.e. offset
`+0x562`:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '^ 2001d4e:|^ 2001d52:'
 2001d4e:       f000 fab1       bl      20022b4 <twlhd_gx_rec_hook>
 2001d52:       4653            mov     r3, sl
```

Reaching `0x02002640` requires `LR_before_suffix = 0x02002640 - 0x562 =
0x020020DE` (Thumb `0x020020DF`). The observed `LR=0x020020DF` at the
post-IRQ suffix matches this arithmetic.

**HYPOTHESIS:** the observed fault is a DeSmuME model artefact. After the BL
prefix/suffix pair completed, guest hook-body execution changed `LR_sys` to
`0x020020DF`. An IRQ was then taken while the CPU was inside the hook body. The
handler returned to the BL suffix address `0x02001D50` instead of to the
instruction after the BL (`0x02001D52`). Because the handler does not
save/restore `LR_sys` on the no-reschedule path, the suffix re-executed with
the stale `LR_sys` value and branched into the planner.

**UNVERIFIED:** the exact mechanism that makes the handler return to
`0x02001D50` rather than `0x02001D52`. The three relevant observations are
quoted side by side below; together they are inconsistent and no quoted
evidence explains the 2-byte difference.

- `TWLHD_IRQ_EXCEPTION` records `ia=0x02001D52`, `ni=0x02001D54` and writes
  `R14_irq=0x02001D56`:
  ```
  TWLHD_IRQ_EXCEPTION cpu=9 frame=129 ia=02001D52 ni=02001D54 r15=02001D56 lr_before=020020DF lr_sys_before=020020DF cpsr_before=0000003F t_before=1 r14_irq=02001D56 cpsr_after=00000092 t_after=0
  ```
  In the standard ARM model a `sub lr, lr, #4` inside the handler would produce
  a return to `0x02001D52`, i.e. the instruction after the BL.

- The ITCM handler disassembly shows `sub lr, lr, #4` at `0x01FF8604`; the
  `TWLHD_LRTRACE IRQ_ENTRY` line records `lr=0x02001D56` before that subtract:
  ```
  TWLHD_LRTRACE IRQ_ENTRY seq=62551633 frame=129 pc=01FF8034 sp=02FF3F80 cpsr=00000092 mode=12 lr=02001D56 lr_sys=020020DF spsr=0000003F
  ```
  After `sub lr, lr, #4` this also implies a return to `0x02001D52`.

- The PC ring shows the last guest PC before the vector was `0x020022CC`, and
  `TWLHD_LRTRACE IRQ_EXIT` shows the handler actually returned to `0x02001D50`:
  ```
  TWLHD_LRTRACE IRQ_EXIT seq=62551664 frame=129 pc=02001D50 sp=02FF3E28 cpsr=0000003F mode=1F lr=020020DF lr_sys=020020DF spsr=0000003F
  ```

**Open item 4 (ring shows hook-body PC `0x020022CC` before IRQ): UNRESOLVED.**
`cubes64-entry-trace.md:768-774` noted the conflict with the "IRQ between BL
halves" prediction. The relevant lines show three things: the last guest PC in
the 64-entry ring before the IRQ vector is `0x020022CC`; `TWLHD_IRQ_EXCEPTION`
records `ia=0x02001D52`; and the handler actually returned to `0x02001D50`. The
*location* where the CPU was when the IRQ was taken (inside `twlhd_gx_rec_hook`
versus at `0x02001D52`) is therefore **UNVERIFIED**; it is one of the conflicting
observations itself, not a confirmed fact. The disagreement about where the
handler should return also remains **UNVERIFIED**.

---

## 6. Hypotheses and next minimal experiment

Because the cause is not confirmed by a FACT chain, this section lists only
labelled **HYPOTHESES** and one concrete next experiment. Any fix proposal
remains a conditional follow-up until the experiment selects among the
hypotheses.

1. **HYPOTHESIS (DeSmuME return-address computation):** `armcpu_irqException`
   or the Thumb instruction decoder computes `next_instruction`/`instruct_adr`
   in a way that, when an IRQ is taken after a Thumb BL suffix, records a
   return address two bytes earlier than the architecturally correct
   `0x02001D52`. The `TWLHD_IRQ_EXCEPTION` line shows `ia=0x02001D52` but the
   handler returns to `0x02001D50`; the discrepancy originates in how DeSmuME
   prefetches or pipelines the Thumb halfwords around the BL.

2. **HYPOTHESIS (mismatched IRQ):** the `TWLHD_IRQ_EXCEPTION` line with
   `ia=0x02001D52` is from a different IRQ than the `IRQ_ENTRY` at seq
   62551633. The observed return to `0x02001D50` could then belong to an
   unlogged exception whose `instruct_adr` was actually `0x02001D4E` or
   `0x02001D50`.

3. **HYPOTHESIS (guest hook return path):** a guest-side path inside
   `twlhd_gx_rec_hook` (e.g. a nested BL/BLX or an explicit LR write) set
   `LR_sys=0x020020DF` and the subsequent exception-return path is actually
   correct, but the combination with the DeSmuME model produces the suffix
   re-execution.

**Call site:** the `bl twlhd_gx_rec_hook` at `0x02001D4E` is inside
`twlhd_demo_draw_frame`, which `addr2line` maps to
`arm9v2/source/demo_frame.cpp:464`:

```
$ arm-none-eabi-addr2line -f -i -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x02001D50
_ZN10twlhd_shim4PortILm67109992ELm4EEaSEm
/tmp/codex-20261118-143840-4170525/worktree/arm9v2/include/twlhd_gx_shim.hpp:104
glRotateYi
/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:989
twlhd_demo_draw_frame
/tmp/codex-20261118-143840-4170525/worktree/arm9v2/source/demo_frame.cpp:464
```

```
$ sed -n '460,468p' arm9v2/source/demo_frame.cpp
        y = mulf32(inttof32(2 * row - (rows - 1)), half_spacing);
        glPushMatrix();
        glTranslatef32(x, y, 0);
        glRotateXi((int)angle);
        glRotateYi((int)angle);
        glScalef32(floattof32(0.12), floattof32(0.12), floattof32(0.12));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE); /* mirrors the single-cube path */
        draw_cube();
        glPopMatrix(1);
```

**Next minimal experiment:** at the `armcpu_irqException` call that produces
`TWLHD_IRQ_EXCEPTION cpu=9 frame=129`, log `instruct_adr`,
`next_instruction`, `R[15]`, the `R14_irq` value written, and the CPSR T-bit
before the mode switch (already done by the round-2 patch). Then, in the same
binary, log the *emulator-internal* `armcpu->next_instruction` value at the
exact moment the IRQ is flagged (just before `armcpu_irqException` is invoked)
and dump every `PC`/`R14`/`R14_usr` from the final `BL_SUFFIX` (seq 62551562)
through `IRQ_EXIT+1` (seq 62551665). Compare the logged `R14_irq` with the
address actually loaded into `LR_irq` by `armcpu_switchMode` and with the
return PC observed at `IRQ_EXIT`. If the values match `ia=0x02001D52` through
the exception entry but the return still lands at `0x02001D50`, hypothesis 1
is favoured; if a second, unlogged exception is found, hypothesis 2 is
favoured.

**Conditional follow-ups (only after the experiment):**
- If hypothesis 1 is favoured: patch DeSmuME's Thumb `next_instruction` /
  `armcpu_irqException` logic so the saved LR points to `0x02001D52` when an
  IRQ is taken after a Thumb BL suffix.
- If hypothesis 2 is favoured: extend tracing to timestamp every
  `armcpu_irqException` invocation and correlate it with the seq counter.
- If hypothesis 3 is favoured: inspect the hook body disassembly for BL/BLX or
  explicit `LR`-writing instructions between `0x020022B4` and `0x020022CC`,
  and/or guest-side mitigation by disabling IRQs around the `glRotateYi` call
  site.

---

## 7. Hardware relevance

**FACT:** no CUBES=64 AUTODUMP_FRAME=120 hardware run is recorded in this
repository.

**HYPOTHESIS:** on ARMv5TE (ARM946E-S), a 32-bit Thumb `BL` is a pair of
separate 16-bit instructions (`BL` prefix and `BL` suffix), and an IRQ can be
taken between them. If the IRQ were taken between the halfwords, the CPU would
return to the suffix with the prefix-written LR. The prefix sets
`LR = prefix address + 4 + (offset_hi<<12)`, which is `0x02001D4E + 4 =
0x02001D52` here (`offset_hi = 0`), i.e. the address after the BL pair, not the
address after the pair plus four. That is *not* what the trace shows: the trace
shows the BL pair completing (`LR=0x02001D53` at seq 62551563), the hook body
running, and `LR_sys` later becoming `0x020020DF` before the IRQ. If hypotheses
1/2 in §6 hold, this would favour a DeSmuME model artefact (the return lands
inside a completed BL suffix with a stale `LR_sys`); a guest-side cause
(hypothesis 3) remains open, so the behaviour is **not** asserted to be an
expected hardware behaviour.

**Proposed hardware experiment (not run):** run the rebuilt
`twlhd-arm9v2-autodump-c64.nds` from a flashcard on a DS/DSi at a similar RTC
time and check whether `twlhd_frame_v2.bin` is written at frame 120 and whether
the bottom-screen frame counter continues past frame 129. **Risk class:** low —
this is a homebrew ROM that only writes a file to SD; it carries the same
brick/SD-wear risk as any other homebrew launch, and no console firmware or
flashcart firmware is modified.

---

## Requirement traceability

- **REQ-1 (new report exists).** `docs/reports/cubes64-irq-lr-trace.md` exists
  with FACT/HYPOTHESIS/UNVERIFIED labels.
- **REQ-2 (raw log evidence).** §3 quotes the ordered `TWLHD_LRTRACE` events,
  the `TWLHD_IRQ_EXCEPTION` line, the new `TWLHD_IRQTRACE` ring, the
  `TWLHD_R14_TRACE` per-instruction window, and the
  `TWLHD_PLAN_RANGE_ENTRY_RING`.
- **REQ-3 (Open item 4).** §5 restates the `0x020022CC` conflict as
  **UNRESOLVED** and quotes the three conflicting observations side by side.
- **REQ-4 (handler disassembly).** §4 quotes the `readelf` section map and the
  `.itcm` objdump around `__arm_excpt_irq`; states the handler runs in IRQ mode
  and does not save/restore `LR_sys` on the no-reschedule path.
- **REQ-5 (hypotheses / next experiment).** §6 gives three labelled hypotheses,
  the `demo_frame.cpp:464` call site quoted via `addr2line` and `sed`, and one
  concrete next minimal experiment; §7 gives hardware relevance as HYPOTHESIS
  with risk class.
- **REQ-6 (`make test` green).** Verified below.
- **REQ-7 (round 1 repair, 2026-11-18).** Repaired ten evidence-discipline
  defects: (1) relabelled the `LR_sys` change timing as a FACT backed by the
  per-instruction `TWLHD_R14_TRACE` and noted the `BL_SUFFIX` `lr` field is
  captured before the suffix runs; (2) restated Open item 4 as **UNRESOLVED**
  with the three conflicting observations quoted side by side; (3) rewrote §6
  as a numbered list of **HYPOTHESES** plus one concrete next minimal
  experiment, with fix ideas as conditional follow-ups only; (4) removed the
  unsupported "approximately 71 instructions" claim and replaced it with the
  exact 69-instruction guest window plus the exception-vector step from the
  per-instruction trace; (5) corrected the §7 hardware premise (Thumb BL
  prefix/suffix are two 16-bit instructions on ARMv5TE and an IRQ can be taken
  between them; the prefix-written LR is the address after the pair) and redid
  the hardware-relevance assessment; (6) added a dedicated IRQ entry/exit ring
  showing 16 pairs before the planner trigger; (7) replaced all elided FACT
  blocks with either complete raw output or narrower commands whose full output
  is exactly quoted; (8) quoted the cflash image provenance and reproduction
  env vars (round-4 update: the `-1` image creation provenance is now
  **UNVERIFIED** and the r3-1 run was removed; only a scoped post-run
  `sha256sum` remains); (9) quoted the `demo_frame.cpp:464` call site via `addr2line` and
  `sed`; (10) quoted the real `make test` tail. Extended the scratch DeSmuME
  copy with `cubes64-irq-lr-trace-r2.patch` and build4; did not modify product
  code, tests, harness, or pristine trees. Aligned
  `cubes64-branch0-diagnosis.md` Update item 1 with the restated Open item 4.
  Evidence: §1 (build/cflash), §3 (R14/IRQ rings), §5 (Open item 4), §6
  (hypotheses/call site), §7 (hardware premise), Verification (`make test`).

- **REQ-8 (round 2 repair of irq-lr-trace report and entry-trace Update,
  2026-11-18).** Re-ran every quoted command and replaced stale or lossy quotes
  with exact raw output: the §2 `sed` range was narrowed to the six banked-LR
  lines actually shown; the §3.2 IRQ ring command was changed to `grep -A 32` and
  the quote now contains all 32 entries (16 pairs), correcting the prose from
  "10 pairs" to "16 pairs"; the §3.2 chronological list and §3.3(a) now state
  the 69-instruction guest window (seq 62551563–62551631) and identify seq
  62551632 as the exception-vector step (`pc=0x00000018 mode=0x12`); the §4
  `objdump` command was changed to `head -21` to match the 21-line quoted block.
  Removed the "confirmed *location* observation" wording from Open item 4 in
  both `cubes64-irq-lr-trace.md` §5 and the `cubes64-entry-trace.md` Update;
  the location is now labelled **UNVERIFIED** and only per-line facts (`last
  guest PC 0x020022CC`; `ia=0x02001D52`; return to `0x02001D50`) are stated.
  Reworded the entry-trace Update CAUSE-consequence sentence so the
  return-to-suffix chain is self-labelled **HYPOTHESIS/UNVERIFIED**. Corrected
  the §7 hardware premise: the Thumb BL prefix sets `LR = prefix address + 4 +
  (offset_hi<<12) = 0x02001D52` (address after the pair), and the conclusion is
  now conditional on hypotheses 1/2 holding, leaving hypothesis 3 (guest-side
  cause) open. Added setup-parity text in §1 and the entry-trace Update: the
  original `DESMUME_TIMEOUT=30` capture-script protocol is quoted next to the
  new build4 command, the dropping of `TWLHD_PCLOG`/`TWLHD_PCLOG2` is relabelled
  **HYPOTHESIS** (they are described as emulator-side logging outputs, not ROM
  inputs), and the cflash provenance now quotes the script's `set -x` creation
  line and a post-run (flushed) `sha256sum` from a fresh round-3 run.
  Propagated the corrected counts into the historical REQ-7/REQ-16 summaries.
  Round 4 correction: the r3-1 run was removed because its full launch command
  and `set -x` printing command were not recorded; the `-1` image creation
  provenance is now **UNVERIFIED** (scoped post-run `sha256sum` only). The
  `TWLHD_PCLOG` quote was corrected to the exact `-A9` boundary and the scope
  claim was backed by a tree-wide `grep -rn`. The build4 source-tree link was
  backed by `meson introspect --buildsystem-files`. Did not modify product code,
  tests, harness, or pristine trees. Evidence: §1 (setup parity, build4
  source-dir, cflash UNVERIFIED, PCLOG scope), §2 (sed), §3.2 (IRQ ring +
  chronological list), §3.3(a) (guest window), §4 (objdump), §5 (Open item 4),
  §6 (CAUSE consequence in entry-trace Update), §7 (hardware premise),
  Verification (`make test`).

- **REQ-18 (round 3 repair of irq-lr-trace report and entry-trace Update,
  2026-11-18).** Corrected three evidence-discipline defects: (1) replaced the
  contradictory cflash provenance in §1 and the entry-trace Update with a fresh
  run into `/tmp/opencode/c64-irqtrace-r2v2-e1794921881-r3-1`, quoting the
  script's `set -x` `+ mkfs.fat -C ...` creation line and the post-run
  (flushed) `sha256sum`; removed "taken before the emulator run" and the manual
  `mkfs.fat` quote; (2) relabelled "Dropping `TWLHD_PCLOG`/`TWLHD_PCLOG2` does
  not change the emulated run" as **HYPOTHESIS** in §1, the entry-trace Update,
  REQ-8 and REQ-17; (3) added a FACT block for the r2 patch (`wc -l`, reverse
  `patch --dry-run`, and `grep -c TWLHD_IRQTRACE` in the source tree)
  so the build4 attribution is evidence-backed. Updated REQ-8/REQ-17 summaries
  and added matching REQ-18 summaries in both files. Did not modify product
  code, tests, harness, or pristine trees. Round 4 correction: the r3-1 run was
  removed and the `-1` cflash creation provenance relabelled **UNVERIFIED**;
  the build4 source-tree link was backed by
  `meson introspect --buildsystem-files`; the `TWLHD_PCLOG` scope claim was
  backed by a tree-wide `grep -rn`; the REQ-18 wording was corrected to say the
  `grep -c TWLHD_IRQTRACE` command covers the source tree, not build4. Evidence:
  §1 (cflash UNVERIFIED, r2 patch, build4 source-dir, PCLOG scope), §3.2
  (`TWLHD_IRQTRACE_DUMP` behavioural confirmation), entry-trace Update,
  Verification (`make test`).

- **REQ-19 (round 4 docs-only repair of irq-lr-trace report and entry-trace
  Update, 2026-11-18).** Repaired three evidence-discipline defects: (1) removed
  the `/tmp/opencode/c64-irqtrace-r2v2-e1794921881-r3-1` run from §1 and the
  entry-trace Update because its full launch command and the command printing
  its `set -x` line were not recorded, relabelled the creation provenance of
  the `-1` `cflash.img` as **UNVERIFIED**, and quoted only a scoped post-run
  `sha256sum` of the current `-1` image; (2) corrected the `TWLHD_PCLOG`
  `grep -n -A9` quote to end at source line 773 and backed the scope claim with
  the full output of `grep -rn 'TWLHD_PCLOG'
  $HOME/3ds/hwtest-staging/desmume-dbg/desmume/src`; (3) added a
  `meson introspect --buildsystem-files` FACT linking `desmume-dbg-build4` to
  the scratch source tree and corrected the REQ-18 wording to say the
  `grep -c TWLHD_IRQTRACE` command covers the source tree, not build4. Updated
  REQ-7/REQ-8/REQ-16/REQ-17/REQ-18 summaries with round-4 amendment clauses.
  Did not modify product code, tests, harness, or pristine trees. Evidence:
  §1 (build4 source-dir, cflash UNVERIFIED, PCLOG scope), Verification
  (`make test`).

---

## Verification

```
$ make test 2>&1 | tail -20
OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq_scenes.py'
................
----------------------------------------------------------------------
Ran 16 tests in 1.217s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
...............................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
----------------------------------------------------------------------
Ran 364 tests in 84.688s

OK (skipped=42)
/tmp/tmpdunv90vs/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```
