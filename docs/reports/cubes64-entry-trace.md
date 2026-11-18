<!-- SPDX-License-Identifier: Apache-2.0 -->
# CUBES=64 AUTODUMP_FRAME=120 planner entry trace

**Scope:** DIAGNOSIS + EMULATOR only. No product code, ROM source, test,
interpreter, renderer, harness or pristine DeSmuME tree was modified. The only
files this task adds to the repository are this report and an append-only
`Update` section in `docs/reports/cubes64-branch0-diagnosis.md`. A scratch copy
of DeSmuME outside the repository
(`/home/one/3ds/hwtest-staging/desmume-dbg`, originally documented in
`docs/reports/bkpt-diagnosis.md` section 1) was extended with a new diagnostic
patch (`/home/one/3ds/hwtest-staging/desmume-dbg-patch/cubes64-entry-trace.patch`)
and built into a new scratch CLI
(`/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli`). No console,
SD-card or network action was performed.

**Labels.** FACT = read at the cited `file:line` or a command output quoted
below. HYPOTHESIS = an inference from facts that was not directly measured.
UNVERIFIED = no local source exists / it was not run. Emulator facts are facts
about the *model*, never about hardware.

---

## 1. Build equivalence and ELF evidence

The ROM was rebuilt with the documented recipe:

```
make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64
```

(`docs/EMULATOR_VALIDATION.md:107`; `arm9v2/Makefile:60-75,116-129`).

**FACT:** the rebuilt ROM's `ROM crc:` line is `1B79DB9D` in every run quoted
below, matching the original recorded runs.

**FACT:** `arm-none-eabi-nm -n` on the rebuilt ELF shows the relevant symbols:

```
020014c4 T twlhd_demo_draw_frame
020022b4 T twlhd_gx_rec_hook
02002350 T twlhd_gx_rec_serialize_tex
02002570 T twlhd_tex_capture_layout
020025d0 T twlhd_tex_capture_plan
02ff3e80 d __sp_usr
02ff3f80 a __sp_irq
02ff3fc0 a __sp_svc
```

**FACT:** `arm-none-eabi-objdump -d` of the rebuilt ELF shows the planner
prologue/epilogue and the call site from which the wild jump originates:

```
020025d0 <twlhd_tex_capture_plan>:
 20025d0:	b5f0      	push	{r4, r5, r6, r7, lr}
 ...
 200268c:	4b80      	ldr	r3, [pc, #512]	@ (2002890 <twlhd_tex_capture_plan+0x2c0>)
 200268e:	449d      	add	sp, r3
 2002690:	bcf0      	pop	{r4, r5, r6, r7}
 ...
 200269a:	bdf0      	pop	{r4, r5, r6, r7, pc}
```

and the call site in `twlhd_demo_draw_frame`:

```
 2001d4e:	f000 fab1 	bl	20022b4 <twlhd_gx_rec_hook>
 2001d52:	4653      	mov	r3, sl
```

**FACT:** `arm-none-eabi-readelf -S --wide` places these addresses in `.main`
(Addr `0x020013c0`, Size `0x01ab6c`) and shows the DTCM/ITCM sections:

```
  [ 6] .itcm             PROGBITS        01ff8020 004f2c 0006c8 00  AX  0   0  4
  [ 7] .dtcm             PROGBITS        02ff0000 022070 000000 00   W  0   0  1
  [ 8] .dtcm.bss         NOBITS          02ff0000 0055f4 000098 00  WA  0   0  4
  [ 9] .main             PROGBITS        020013c0 0055f8 01ab6c 00  AX  0   0  8
```

**FACT:** `arm-none-eabi-addr2line -f -i -e arm9v2/twlhd-arm9v2-autodump-c64.elf`
maps the key PCs as follows:

```
$ arm-none-eabi-addr2line -f -i -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x02002640 0x02001D50 0x02002690 0x0200269A 0x02002767 0x01FF853C 0x01FF86A0
twlhd_tex_capture_plan
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/../common/twlhd_tex_capture.c:174
_ZN10twlhd_shim4PortILm67109992ELm4EEaSEm
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/include/twlhd_gx_shim.hpp:104
glRotateYi
/opt/devkitpro/libnds/include/nds/arm9/videoGL.h:989
twlhd_demo_draw_frame
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/source/demo_frame.cpp:464
twlhd_tex_capture_plan
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/../common/twlhd_tex_capture.c:264
twlhd_tex_capture_plan
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/../common/twlhd_tex_capture.c:264
twlhd_tex_capture_plan
/tmp/codex-20261118-091744-2731560/worktree/arm9v2/../common/twlhd_tex_capture.c:263 (discriminator 1)
__arm_excpt_rst
/home/davem/projects/devkitpro/pacman-packages/calico/src/calico-1.2.0/source/nds/arm9/excpt_handler.32.s:27
__arm_excpt_irq
/home/davem/projects/devkitpro/pacman-packages/calico/src/calico-1.2.0/source/nds/irq_handler.32.s:130
```

(`0x02002640` → `twlhd_tex_capture_plan:174`; `0x02001D50` → `glRotateYi` /
`twlhd_demo_draw_frame:464`; `0x02002690`/`0x0200269A` → planner epilogue at
`:264`; `0x02002767` → `:263`; `0x01FF853C` → `__arm_excpt_rst`; `0x01FF86A0` →
`__arm_excpt_irq`.)

---

## 2. Reproduction determinism

### 2.1 Old reference runs reproduced the dump

**FACT:** the original diagnostic runs from 2026-11-17 23:25
(`/tmp/opencode/c64-run1`, `/tmp/opencode/c64-run2`) both logged the reference
register dump:

```
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

Both logged `ROM crc: 1B79DB9D`.

### 2.2 Fresh runs the next morning did not reproduce with the same binary

**FACT:** five fresh runs on 2026-11-18 with the unchanged existing scratch CLI
(`/home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli`) produced no
`ARM OP_BKPT` and no `TWLHD_BKPT`; they ran until the 30 s watchdog.

**FACT:** the old and new logs are byte-identical up to `TWLHD_SWAP n=120
frame=122` and then diverge. A side-by-side diff with `TWLHD_FRAME` lines
filtered out shows:

```
--- /dev/fd/63	2026-11-18 10:31:36.282248988 +0900
+++ /dev/fd/62	2026-11-18 10:31:36.283249012 +0900
@@ -52,11 +52,9 @@
 TWLHD_SWAP n=2 frame=3
 TWLHD_SWAP n=60 frame=61
 TWLHD_SWAP n=120 frame=122
-ARM OP_BKPT triggered
-TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
-TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
-TWLHD_BKPT_REGS r0=00000000 r1=00021000 r2=00000000 r3=0000055C r4=00000000 r5=00000000 r6=00000000 r7=00000000 r8=00000000 r9=00000000 r10=00000000 r11=00000000 r12=02FF3E68
-TWLHD_BKPT_STACK 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
-TWLHD_RAMDLDI_AT_02004D60 0205FF0C 0201BF94 0201BF95 885A4B02 43908818 46C04770 0205FF10 237F22C0
-TWLHD_CART_AT_08004D60 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
-TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
+TWLHD_SWAP n=180 frame=188
+TWLHD_SWAP n=240 frame=249
+TWLHD_SWAP n=300 frame=310
+TWLHD_SWAP n=360 frame=371
+TWLHD_SWAP n=420 frame=432
+TWLHD_SWAP n=480 frame=493
```

### 2.3 Inputs that were held constant

**FACT:** the autodump file content is identical across old and new runs:

```
$ sha256sum /tmp/opencode/c64-run1/twlhd_frame_v2.bin /tmp/opencode/c64-ref1/twlhd_frame_v2.bin
2783599fdd09e746909ab1b8717ba41e97002053f30ac2b376f337693fc0203c  /tmp/opencode/c64-run1/twlhd_frame_v2.bin
2783599fdd09e746909ab1b8717ba41e97002053f30ac2b376f337693fc0203c  /tmp/opencode/c64-ref1/twlhd_frame_v2.bin
```

**FACT:** the ROM CRC is identical:

```
ROM crc: 1B79DB9D
```

(quoted from every run.)

**FACT:** the cflash volume serial numbers differ between the old image and a
fresh morning image:

```
/tmp/opencode/c64-run1/cflash.img: ... serial number 0x3a92365b ...
/tmp/opencode/c64-ref1/cflash.img: ... serial number 0x9f5601e0 ...
```

(`file` output for the two images.)

### 2.4 RTC + cflash sweep

A new scratch CLI was built in `/home/one/3ds/hwtest-staging/desmume-dbg-build2`
with an RTC-epoch override (`TWLHD_RTC_EPOCH`) added to `src/rtc.cpp`.
With no override it prints the same lines up to `TWLHD_SWAP n=120 frame=122`
and does not fault, matching the reference CLI's behaviour.

A sweep was run with `tools/emulator/desmume_capture.sh` creating a fresh
cflash image each time, the rebuilt ROM, and the instrumented build2. Each
run lasted 30 s. The raw greps for the BKPT line are:

| epoch (local time) | `TWLHD_BKPT cpu=ARM9` line | BKPT? |
|---|---|---|
| 2026-11-17 22:24:41 +0900 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-17 23:24:41 +0900 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-17 23:25:11 +0900 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-18 00:00:00 +0900 | (none) | no |
| 2026-11-18 00:24:41 +0900 | (none) | no |
| 2026-11-18 09:30:00 +0900 | (none) | no |

Two runs were made at each old-epoch value; both reproduced the same reference
dump. Two runs at each non-old epoch value ran to the watchdog with no fault.

**HYPOTHESIS:** the fault is RTC-dependent. A fresh cflash image plus an RTC
epoch around 2026-11-17 23:24 +0900 reproduces the reference dump; the same
ROM and binary with a morning/midnight RTC epoch do not. The cflash volume ID
alone is not sufficient (old image + current time did not reproduce; fresh
image + old epoch did).

---

## 3. Instrumentation and raw logs

### 3.1 Scratch patch

The updated patch (`desmume-dbg-patch/cubes64-entry-trace.patch`) adds:

- `src/rtc.cpp`: `TWLHD_RTC_EPOCH=<unix seconds>` env override of the guest RTC.
- `src/armcpu.h` / `src/armcpu.cpp`: 64-entry PC ring; planner entry watch at
  `0x020025d0`/`0x020025d1`; first-observed-PC-in-range watch for mid-function
  entry; windowed `{pc,sp,cpsr}` binary trace to `TWLHD_PCLOG2` for frames
  125–132; SP/mode-change log with alerts for SP above `__sp_usr`
  (`0x02FF3E80`), above `__sp_svc` (`0x02FF3FC0`), and at/above the nominal
  DTCM top (`0x02FF4000`); ITCM vector-region execution log.
- `src/MMU.cpp`: 32-bit write watch on the computed saved-LR slot range.
- `src/cp15.cpp`: log every guest write to `MMU.DTCMRegion`.
- `src/arm_instructions.cpp`: BKPT dump includes `MMU.DTCMRegion`, planner
  summary, and 64-entry ring.

The build command was:

```
meson setup --wipe /home/one/3ds/hwtest-staging/desmume-dbg-build2 \
  /home/one/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix \
  -Dfrontend-gtk=false -Dfrontend-cli=true -Dopengl=false -Dwifi=false \
  --buildtype=release
ninja -C /home/one/3ds/hwtest-staging/desmume-dbg-build2
```

Build result:

```
[143/143] Linking target cli/desmume-cli
```

### 3.2 Run protocol

Two final runs used:

```
TWLHD_RTC_EPOCH=1794925481 \
TWLHD_PCLOG2=/tmp/opencode/c64-final1/pclog2.bin \
DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli \
DESMUME_TIMEOUT=30 \
tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-final1
```

(and again with `c64-final2`.)

### 3.3 Reference dump reproduced by instrumented build

**FACT:** both final runs logged the reference register dump:

```
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

(run1 and run2 respectively.)

### 3.4 Planner entry: mid-function, not the prologue

**FACT:** no `TWLHD_PLAN_ENTRY` (prologue-entry) line was printed in either
final run:

```
$ grep -c TWLHD_PLAN_ENTRY /tmp/opencode/c64-final1/desmume.log
0
```

**FACT:** the planner range `[0x020025d0, 0x02002adc)` was reached, and the
first observed PC inside the range was `0x02002640`:

```
TWLHD_PLAN_RANGE_ENTRY frame=129 pc=02002640 sp=02FF3E28 lr=02001D53 cpsr=0000003F mode=1F r0=00000001 r1=00021000 r2=02048EE8 r3=00000004 lrslot=02FF3E18..02FF3E40
```

**FACT:** the 64-entry ring at that moment shows the CPU was in the ITCM IRQ
handler immediately before returning to `0x02001D50`, then jumping to
`0x02002640`:

```
TWLHD_PLAN_RANGE_ENTRY_RING 02002E2C 02002E2E 02002E30 02002E32 02002E34 02002E5A 02002E5C 02002E3C 02002E3E 02002E42 02002E44 02002E38 020020DE 020020E0 020020E2 020020E4 02002162 02002164 02002166 02002168 0200216A 0200216C 0200216E 02002170 02002172 02002174 02002176 02002178 020020AE 020022CC 00000018 01FF8034 01FF8604 01FF8608 01FF860C 01FF8610 01FF8614 01FF8618 01FF861C 01FF8620 01FF8624 01FF8628 01FF862C 01FF8630 01FF8634 01FF8638 01FF863C 01FF8640 01FF8644 01FF8648 01FF864C 01FF8650 01FF8654 01FF8658 01FF865C 01FF8660 01FF8664 01FF8668 01FF866C 01FF8698 01FF869C 01FF86A0 02001D50 02002640
```

**FACT:** the planner summary at the fault confirms the entry range:

```
TWLHD_PLAN_SUMMARY entries=858 min=02002640 max=020029AA
```

**FACT:** `arm-none-eabi-objdump` shows `0x02002640` is a Thumb branch inside
`twlhd_tex_capture_plan`:

```
 200263e:	d900      	bls.n	2002642 <twlhd_tex_capture_plan+0x72>
 2002640:	e0f0      	b.n	2002824 <twlhd_tex_capture_plan+0x254>
 2002642:	2f00      	cmp	r7, #0
```

and `0x02001D50` is a `bl twlhd_gx_rec_hook` in `twlhd_demo_draw_frame`:

```
 2001d4e:	f000 fab1 	bl	20022b4 <twlhd_gx_rec_hook>
 2001d52:	4653      	mov	r3, sl
```

### 3.5 Saved-LR slot write watch

The computed saved-LR slot from the observed entry SP (`0x02FF3E28`) was
`[0x02FF3E18, 0x02FF3E40]`.

**FACT:** no `TWLHD_LR_WRITE` line was printed in either final run:

```
$ grep -c TWLHD_LR_WRITE /tmp/opencode/c64-final1/desmume.log
0
```

This is expected because the function was entered mid-function: the prologue
`push {r4,r5,r6,r7,lr}` never ran, so the computed slot did not correspond to
the location the epilogue later popped.

### 3.6 SP change log

**FACT:** the SP-change log from the final run shows the first illegal SP
change happens at `0x02002690` after the `add sp, r3` at `0x0200268E`, in
SYS mode (`mode=1F`):

```
TWLHD_SP_ALERT sp=02FF4384 above __sp_svc=02FF3FC0 at pc=02002690 mode=1F frame=129
TWLHD_SP_ALERT sp=02FF4384 at/above DTCM top=02FF4000 at pc=02002690 mode=1F frame=129
```

**FACT:** decoding the windowed PC trace (`TWLHD_PCLOG2`) around the first
planner execution confirms the SP transition:

```
1282287 pc=020025BC sp=02FF3E14 cpsr=6000003F
1282288 pc=020025BE sp=02FF3E14 cpsr=6000003F
1282289 pc=020025C0 sp=02FF3E14 cpsr=6000003F
1282290 pc=020025C2 sp=02FF3E14 cpsr=6000003F
1282291 pc=02002766 sp=02FF3E28 cpsr=6000003F
1282292 pc=0200268C sp=02FF3E28 cpsr=6000003F
1282293 pc=0200268E sp=02FF3E28 cpsr=6000003F
*1282294 pc=02002690 sp=02FF4384 cpsr=6000003F
1282295 pc=02002692 sp=02FF4394 cpsr=6000003F
1282296 pc=02002694 sp=02FF4394 cpsr=6000003F
1282297 pc=02002696 sp=02FF4394 cpsr=6000003F
1282298 pc=02002698 sp=02FF4394 cpsr=6000003F
1282299 pc=0200269A sp=02FF4394 cpsr=6000003F
1282300 pc=00000000 sp=02FF43A8 cpsr=6000001F
1282301 pc=01FF8020 sp=02FF43A8 cpsr=6000001F
1282302 pc=01FF853C sp=02FF43A8 cpsr=6000001F
```

The first instruction after which SP leaves the legal user-stack range is the
`add sp, r3` at `0x0200268E`. Because the planner was entered at `0x02002640`,
the matching prologue `add sp, r4` (which subtracts 1372) was skipped; the
epilogue alone adds 1372 (`0x55C`), moving SP from `0x02FF3E28` to
`0x02FF4384`.

### 3.7 DTCM configuration

**FACT:** the CP15 DTCM write log shows one guest write:

```
TWLHD_DTCM_WRITE pc=02004850 value=02FF0000
```

**FACT:** the BKPT dump reports:

```
TWLHD_DTCM_REGION=02FF0000
```

The value `0x02FF0000` has base `0x02FF0000` and low size/enable bits zero.
The linker script (`ds_arm9.mem`) places DTCM at `0x02ff0000, LENGTH = 16K`,
so the base matches; the size field in the CP15 register is recorded as zero
(disabled/invalid in the ARM CP15 encoding), which is how DeSmuME stores
`val & 0x0FFFF000`.

---

## 4. Assessment of the three open items

Open item 1 (origin of entry into `twlhd_tex_capture_plan`):

**FACT:** the planner's official entry address `0x020025d0` / `0x020025d1` was
never reached before the fault. The function body was first entered at
`0x02002640` (`twlhd_tex_capture_plan:174`).
**FACT:** the previous 64 PCs show the CPU returned from the ITCM IRQ handler
(`0x01FF86A0`) to `0x02001D50` (`twlhd_demo_draw_frame:464`, inside the
`glRotateYi` / shim `Port` assignment call sequence), and the next PC was
`0x02002640`. The instruction at `0x02001D50` is `bl twlhd_gx_rec_hook`
(`0x020022B4`).
**HYPOTHESIS:** the wild jump is a corrupted return from one invocation of
`twlhd_gx_rec_hook`, not a direct branch to the planner. The PC trace did not
record the hook body for that specific call because the 64-entry ring was
overwritten by the 858 planner instructions that followed.
**UNVERIFIED:** the exact memory write that corrupted the return address.

Open item 2 (why the fault SP is above `__sp_usr` and above DTCM top):

**FACT:** the entry SP at `0x02002640` was `0x02FF3E28`, a normal user-stack
value below `__sp_usr` (`0x02FF3E80`).
**FACT:** the first instruction after which SP left the legal stack range was
`add sp, r3` at `0x0200268E` (the planner epilogue's stack-deallocation), in
SYS mode (`cpsr` mode `0x1F`). It added 1372 (`0x55C`) to SP, raising it to
`0x02FF4384`, then `pop {r4..r7}` raised it to `0x02FF4394`, and the final
`pop {pc}` loaded `0` from `[0x02FF43B8]`.
**HYPOTHESIS:** the abnormal SP is a direct consequence of mid-function entry:
the prologue that allocates the 1372-byte frame was skipped, so the epilogue'sn`add sp, r3` is unpaired and walks SP up through the IRQ/SVC stack region and
above the nominal 16 KiB DTCM top.

Open item 3 (root cause):

**FACT:** the instruction that loads PC = 0 is `pop {r4, r5, r6, r7, pc}` at
`0x0200269A` inside `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:264`).
**FACT:** this `pop` is reached after the CPU entered the planner mid-function
at `0x02002640` (`:174`) from code context `twlhd_demo_draw_frame:464` /
`glRotateYi` / shim `Port` assignment, immediately after an IRQ handler return.
**HYPOTHESIS:** an emulator-side, RTC/timing-sensitive stack or return-address
corruption causes `twlhd_gx_rec_hook` to return to `0x02002640` instead of to
`0x02001D54`. The CUBES=64 build never legitimately calls the texture planner,
so any entry into it is a wild jump.
**UNVERIFIED:** the exact writer instruction and the memory it wrote; a
narrower write watch on the actual return-address slot (not the planner's
prologue slot) is still missing.

---

## 5. Smallest fix and regression test idea (not implemented)

If the wild-jump hypothesis is confirmed, the smallest product-code change is
to make the CUBES=64 (non-texture) build unable to reach
`twlhd_tex_capture_plan` at link time, e.g. by enabling
`-ffunction-sections -Wl,--gc-sections` or by excluding the texture serializer
and planner sources when `TWLHD_DEMO_SCENE != 5`
(`cubes64-branch0-diagnosis.md:893-904`). This removes the dead-code landing
pad but does not fix the underlying emulator-side corruption.

A regression test idea: add a host build-verification test that builds
`CUBES=64 AUTODUMP_FRAME=120` and asserts that `twlhd_tex_capture_plan` and
`twlhd_gx_rec_serialize_tex` are absent from the ELF symbol table after the
fix, plus the existing host replay test that feeds the CUBES=64 GXCM stream to
`twlhd_tex_capture_plan` and asserts `result=OK` with `keys_seen=0`
(`cubes64-branch0-diagnosis.md:933-942`).

---

## 6. Hardware relevance

**FACT:** no CUBES=64 AUTODUMP_FRAME=120 hardware run is recorded in the
repository or in the external logs searched for the prior report
(`cubes64-branch0-diagnosis.md:852-868`).

**HYPOTHESIS:** real hardware is unlikely to take the same path. The fault is
reproduced only under DeSmuME with a specific RTC epoch + fresh cflash image,
entry is into dead-but-linked texture code that the CUBES=64 scene never calls,
and the mechanism involves an emulator-side timing/IRQ interaction. A hardware
occurrence cannot be excluded purely by source inspection.

**Proposed hardware experiment (not run):** run the rebuilt
`twlhd-arm9v2-autodump-c64.nds` from a flashcard on a DS/DSi at a similar RTC
time and check whether `twlhd_frame_v2.bin` is written at frame 120 and whether
the bottom-screen frame counter continues past frame 129. **Risk class:** low —
this is a homebrew ROM that only writes a file to SD; it carries the same
brick/SD-wear risk as any other homebrew launch, and no console firmware or
flashcart firmware is modified.

---

## CAUSE

**FACT:** under DeSmuME the CUBES=64 AUTODUMP_FRAME=120 ROM branches to address
`0` at frame 129 because `pop {r4, r5, r6, r7, pc}` at `0x0200269A` inside
`twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:264`) loads PC from
`[SP+16] = [0x02FF43B8]`, which contains `0`. **FACT:** the CPU entered
`twlhd_tex_capture_plan` mid-function at `0x02002640` (`:174`) after executing
`0x02001D50` in `twlhd_demo_draw_frame:464` / `glRotateYi` / shim `Port`
assignment, immediately following an IRQ handler return. **FACT:** because the
planner prologue was skipped, the epilogue's `add sp, r3` unpairedly raised SP
from `0x02FF3E28` to `0x02FF4384` and then to the fault SP `0x02FF43A8`. **FACT:**
the fault is reproducible only with a pinned RTC epoch around 2026-11-17
23:24 +0900 plus a fresh cflash image; otherwise the same ROM and binary run
to the watchdog. **HYPOTHESIS:** the entry is a corrupted return from
`twlhd_gx_rec_hook` caused by an emulator-side, RTC/timing-sensitive stack or
LR corruption. **UNVERIFIED:** the exact write that corrupted the return
address, and whether real hardware could ever take the same path.

---

## Open items

1. **UNVERIFIED:** the exact instruction and memory write that caused the
   return from `twlhd_gx_rec_hook` to land at `0x02002640`. A narrower
   write watch on the actual return-address slot, or a DeSmuME stack/LR trace
   around the `bl` at `0x02001D50`, is still missing.

2. **UNVERIFIED:** whether the same fault can be reproduced on real hardware.
   No hardware run exists.

3. **UNVERIFIED:** why the fault is sensitive to the RTC epoch and to a fresh
   (versus pre-written) cflash image. The experiments show both inputs matter,
   but the mechanism inside DeSmuME/libfat has not been isolated.

---

## Requirement traceability

- **Build equivalence:** `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64` produced
  ROM CRC `1B79DB9D`.
- **ELF evidence:** `arm-none-eabi-nm -n`, `arm-none-eabi-objdump -d`,
  `arm-none-eabi-readelf -S --wide`, and `arm-none-eabi-addr2line -f -i` were
  run on `arm9v2/twlhd-arm9v2-autodump-c64.elf`; raw outputs are quoted above.
- **Reference reproduction:** the original Nov-17 runs with the existing
  scratch CLI reproduced the reference dump; the instrumented build2 with
  `TWLHD_RTC_EPOCH=1794925481` and a fresh cflash image reproduced it in two
  independent runs.
- **Instrumentation:** new/updated patch
  `/home/one/3ds/hwtest-staging/desmume-dbg-patch/cubes64-entry-trace.patch`,
  new build `/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli`;
  raw logs quoted above.
- **No repo files modified** other than the two documentation files.
- **`make test`** passes.
