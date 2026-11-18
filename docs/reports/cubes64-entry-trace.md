<!-- SPDX-License-Identifier: Apache-2.0 -->
# CUBES=64 AUTODUMP_FRAME=120 planner entry trace

Path note: literal home-directory paths in quoted commands and outputs were replaced by $HOME for publication; this is the only edit to the quoted evidence; no other text was changed.

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

**FACT:** every run quoted below logs `ROM crc: 1B79DB9D`:

```
$ grep -H 'ROM crc' /tmp/opencode/c64-run1/desmume.log /tmp/opencode/c64-run2/desmume.log /tmp/opencode/c64-ref1/desmume.log /tmp/opencode/c64-nov18-rerun-1/desmume.log /tmp/opencode/c64-nov18-rerun-2/desmume.log /tmp/opencode/c64-nov18-rerun-3/desmume.log /tmp/opencode/c64-nov18-rerun-4/desmume.log /tmp/opencode/c64-nov18-rerun-5/desmume.log /tmp/opencode/c64-noepoch-rerun-1/desmume.log /tmp/opencode/c64-final1/desmume.log /tmp/opencode/c64-final2/desmume.log /tmp/opencode/c64-epoch-1794921881-1/desmume.log /tmp/opencode/c64-epoch-1794921881-2/desmume.log /tmp/opencode/c64-epoch-1794925481-1/desmume.log /tmp/opencode/c64-epoch-1794925481-2/desmume.log /tmp/opencode/c64-epoch-1794925511-1/desmume.log /tmp/opencode/c64-epoch-1794925511-2/desmume.log /tmp/opencode/c64-epoch-1794927600-1/desmume.log /tmp/opencode/c64-epoch-1794927600-2/desmume.log /tmp/opencode/c64-epoch-1794929081-1/desmume.log /tmp/opencode/c64-epoch-1794929081-2/desmume.log /tmp/opencode/c64-epoch-1794961800-1/desmume.log /tmp/opencode/c64-epoch-1794961800-2/desmume.log /tmp/opencode/c64-oldimg-currenttime/desmume.log
/tmp/opencode/c64-run1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-run2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-ref1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-nov18-rerun-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-nov18-rerun-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-nov18-rerun-3/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-nov18-rerun-4/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-nov18-rerun-5/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-noepoch-rerun-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-final1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-final2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794921881-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794921881-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794925481-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794925481-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794925511-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794925511-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794927600-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794927600-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794929081-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794929081-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794961800-1/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-epoch-1794961800-2/desmume.log:ROM crc: 1B79DB9D
/tmp/opencode/c64-oldimg-currenttime/desmume.log:ROM crc: 1B79DB9D
```

**FACT:** `arm-none-eabi-nm -n` on the rebuilt ELF shows the relevant symbols:

```
$ arm-none-eabi-nm -n arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E 'twlhd_demo_draw_frame|twlhd_gx_rec_hook|twlhd_gx_rec_serialize_tex|twlhd_tex_capture_layout|twlhd_tex_capture_plan|__sp_usr|__sp_irq|__sp_svc'
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
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '^020025d0 <twlhd_tex_capture_plan>:|^ 20025d0:|^ 200268c:|^ 200268e:|^ 2002690:|^ 200269a:'
020025d0 <twlhd_tex_capture_plan>:
 20025d0:	b5f0      	push	{r4, r5, r6, r7, lr}
 200268c:	4b80      	ldr	r3, [pc, #512]	@ (2002890 <twlhd_tex_capture_plan+0x2c0>)
 200268e:	449d      	add	sp, r3
 2002690:	bcf0      	pop	{r4, r5, r6, r7}
 200269a:	bdf0      	pop	{r4, r5, r6, r7, pc}
```

and the call site in `twlhd_demo_draw_frame`:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '^ 2001d4e:|^ 2001d52:'
 2001d4e:	f000 fab1 	bl	20022b4 <twlhd_gx_rec_hook>
 2001d52:	4653      	mov	r3, sl
```

**FACT:** `arm-none-eabi-readelf -S --wide` places these addresses in `.main`
(Addr `0x020013c0`, Size `0x01ab6c`) and shows the DTCM/ITCM sections:

```
$ arm-none-eabi-readelf -S --wide arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '\.itcm|\.dtcm|\.main '
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
`twlhd_demo_draw_frame:464` — this is the second halfword of the 32-bit Thumb
`bl twlhd_gx_rec_hook` at `0x02001D4E`; `0x02002690`/`0x0200269A` → planner
epilogue at `:264`; `0x02002767` → `:263`; `0x01FF853C` → `__arm_excpt_rst`;
`0x01FF86A0` → `__arm_excpt_irq`.)

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

Both logged `ROM crc: 1B79DB9D` (§1).

### 2.2 Fresh runs the next morning did not reproduce with the same binary

**FACT:** five fresh runs on 2026-11-18 with the unchanged existing scratch CLI
(`/home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli`) produced no
`TWLHD_BKPT` line.

Run command (capture output omitted):

```
$ for i in 1 2 3 4 5; do mkdir -p /tmp/opencode/c64-nov18-rerun-$i; DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli DESMUME_TIMEOUT=30 tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-nov18-rerun-$i; done
```

Raw `grep -c` results:

```
$ for i in 1 2 3 4 5; do echo -n "$i: "; grep -c TWLHD_BKPT /tmp/opencode/c64-nov18-rerun-$i/desmume.log; done
1: 0
2: 0
3: 0
4: 0
5: 0
```

**FACT:** with `TWLHD_FRAME` lines filtered out (`grep -v '^TWLHD_FRAME'`), the
old and new logs differ only in the cflash image paths at lines 2, 7 and 25 until
line 55, where the old log has the `ARM OP_BKPT`/BKPT dump block and the new log
continues with `TWLHD_SWAP n=180 frame=188`. A side-by-side diff was produced by:

```
$ diff <(grep -v '^TWLHD_FRAME' /tmp/opencode/c64-run1/desmume.log) <(grep -v '^TWLHD_FRAME' /tmp/opencode/c64-nov18-rerun-1/desmume.log)
2c2
< + exec env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy python3 -I -c 'import signal,os,sys; [signal.signal(getattr(signal,n),signal.SIG_DFL) for n in ("SIGINT","SIGQUIT","SIGXFSZ","SIGPIPE") if hasattr(signal,n)]; os.execvp(sys.argv[1], sys.argv[1:])' stdbuf -oL -eL /home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli --cflash-image /tmp/opencode/c64-run1/cflash.img --disable-sound --disable-limiter arm9v2/twlhd-arm9v2-autodump-c64.nds
---
> + exec env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy python3 -I -c 'import signal,os,sys; [signal.signal(getattr(signal,n),signal.SIG_DFL) for n in ("SIGINT","SIGQUIT","SIGXFSZ","SIGPIPE") if hasattr(signal,n)]; os.execvp(sys.argv[1], sys.argv[1:])' stdbuf -oL -eL /home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli --cflash-image /tmp/opencode/c64-nov18-rerun-1/cflash.img --disable-sound --disable-limiter arm9v2/twlhd-arm9v2-autodump-c64.nds
7c7
< Using CFlash disk image file /tmp/opencode/c64-run1/cflash.img
---
> Using CFlash disk image file /tmp/opencode/c64-nov18-rerun-1/cflash.img
25c25
< Using CFlash disk image file /tmp/opencode/c64-run1/cflash.img
---
> Using CFlash disk image file /tmp/opencode/c64-nov18-rerun-1/cflash.img
55,62c55,60
< ARM OP_BKPT triggered
< TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
< TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
< TWLHD_BKPT_REGS r0=00000000 r1=00021000 r2=00000000 r3=0000055C r4=00000000 r5=00000000 r6=00000000 r7=00000000 r8=00000000 r9=00000000 r10=00000000 r11=00000000 r12=02FF3E68
< TWLHD_BKPT_STACK 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
< TWLHD_RAMDLDI_AT_02004D60 0205FF0C 0201BF94 0201BF95 885A4B02 43908818 46C04770 0205FF10 237F22C0
< TWLHD_CART_AT_08004D60 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
< TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
---
> TWLHD_SWAP n=180 frame=188
> TWLHD_SWAP n=240 frame=249
> TWLHD_SWAP n=300 frame=310
> TWLHD_SWAP n=360 frame=371
> TWLHD_SWAP n=420 frame=432
> TWLHD_SWAP n=480 frame=493
```

### 2.3 Inputs that were held constant (as far as compared)

**FACT:** the autodump file content is identical between `/tmp/opencode/c64-run1` and `/tmp/opencode/c64-ref1`:

```
$ sha256sum /tmp/opencode/c64-run1/twlhd_frame_v2.bin /tmp/opencode/c64-ref1/twlhd_frame_v2.bin
2783599fdd09e746909ab1b8717ba41e97002053f30ac2b376f337693fc0203c  /tmp/opencode/c64-run1/twlhd_frame_v2.bin
2783599fdd09e746909ab1b8717ba41e97002053f30ac2b376f337693fc0203c  /tmp/opencode/c64-ref1/twlhd_frame_v2.bin
```

**FACT:** the ROM CRC is identical between the two compared runs
(`/tmp/opencode/c64-run1` and `/tmp/opencode/c64-ref1`); both logs show
`ROM crc: 1B79DB9D` (§1).

**FACT:** the cflash volume serial numbers differ between the old image
(`c64-run1`) and the `c64-ref1` image:

```
$ file /tmp/opencode/c64-run1/cflash.img /tmp/opencode/c64-ref1/cflash.img
/tmp/opencode/c64-run1/cflash.img: DOS/MBR boot sector, code offset 0x3c+2, OEM-ID "mkfs.fat", sectors/cluster 4, reserved sectors 4, root entries 512, Media descriptor 0xf8, sectors/FAT 64, sectors/track 32, heads 4, sectors 65536 (volumes > 32 MB), serial number 0x3a92365b, unlabeled, FAT (16 bit)
/tmp/opencode/c64-ref1/cflash.img: DOS/MBR boot sector, code offset 0x3c+2, OEM-ID "mkfs.fat", sectors/cluster 4, reserved sectors 4, root entries 512, Media descriptor 0xf8, sectors/FAT 64, sectors/track 32, heads 4, sectors 65536 (volumes > 32 MB), serial number 0x9f5601e0, unlabeled, FAT (16 bit)
```

### 2.4 RTC + cflash sweep

A new scratch CLI was built in `/home/one/3ds/hwtest-staging/desmume-dbg-build2`
with an RTC-epoch override (`TWLHD_RTC_EPOCH`) added to `src/rtc.cpp`.
**FACT:** with no override the build2 CLI runs past `TWLHD_SWAP n=120 frame=122`
and produces no `TWLHD_BKPT` line:

```
$ mkdir -p /tmp/opencode/c64-noepoch-rerun-1 && DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli DESMUME_TIMEOUT=30 tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-noepoch-rerun-1
capture: /tmp/opencode/c64-noepoch-rerun-1/twlhd_frame_v2.bin
```

```
$ grep -c TWLHD_BKPT /tmp/opencode/c64-noepoch-rerun-1/desmume.log
0
```

```
$ grep TWLHD_SWAP /tmp/opencode/c64-noepoch-rerun-1/desmume.log | head -7
TWLHD_SWAP n=0 frame=1
TWLHD_SWAP n=1 frame=2
TWLHD_SWAP n=2 frame=3
TWLHD_SWAP n=60 frame=61
TWLHD_SWAP n=120 frame=122
TWLHD_SWAP n=180 frame=188
TWLHD_SWAP n=240 frame=249
```

A sweep was run with `tools/emulator/desmume_capture.sh` creating a fresh
cflash image each time, the rebuilt ROM, and the instrumented build2. Each
run lasted 30 s. The capture command is shown below (capture output omitted);
the raw greps for the BKPT line are:

```
$ for e in 1794921881 1794925481 1794925511 1794927600 1794929081 1794961800; do
>   for r in 1 2; do
>     dir=/tmp/opencode/c64-epoch-${e}-${r}
>     mkdir -p $dir
>     TWLHD_RTC_EPOCH=$e DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli DESMUME_TIMEOUT=30 tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds $dir
>   done
> done
```

```
$ for e in 1794921881 1794925481 1794925511 1794927600 1794929081 1794961800; do
>   for r in 1 2; do
>     dir=/tmp/opencode/c64-epoch-${e}-${r}
>     echo "=== TWLHD_RTC_EPOCH=$e run=$r ==="
>     grep -c TWLHD_BKPT $dir/desmume.log || true
>     grep -h TWLHD_BKPT $dir/desmume.log | head -2 || true
>   done
> done
=== TWLHD_RTC_EPOCH=1794921881 run=1 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794921881 run=2 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794925481 run=1 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794925481 run=2 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794925511 run=1 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794925511 run=2 ===
5
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=129 last_lr=02002767 last_cpsr=6000001F
=== TWLHD_RTC_EPOCH=1794927600 run=1 ===
0
=== TWLHD_RTC_EPOCH=1794927600 run=2 ===
0
=== TWLHD_RTC_EPOCH=1794929081 run=1 ===
0
=== TWLHD_RTC_EPOCH=1794929081 run=2 ===
0
=== TWLHD_RTC_EPOCH=1794961800 run=1 ===
0
=== TWLHD_RTC_EPOCH=1794961800 run=2 ===
0
```

| epoch (local time) | TWLHD_RTC_EPOCH | `TWLHD_BKPT cpu=ARM9` line | BKPT? |
|---|---|---|---|
| 2026-11-17 22:24:41 +0900 | 1794921881 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-17 23:24:41 +0900 | 1794925481 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-17 23:25:11 +0900 | 1794925511 | `TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000` | yes |
| 2026-11-18 00:00:00 +0900 | 1794927600 | (none) | no |
| 2026-11-18 00:24:41 +0900 | 1794929081 | (none) | no |
| 2026-11-18 09:30:00 +0900 | 1794961800 | (none) | no |

Two runs were made at each old-epoch value; both reproduced the same reference
dump. Two runs at each non-old epoch value produced no `TWLHD_BKPT` line.

**HYPOTHESIS:** the fault is RTC-dependent. A fresh cflash image plus an RTC
epoch around 2026-11-17 22:24–23:25 +0900 reproduces the reference dump; the
same ROM and binary with a morning/midnight RTC epoch do not. The cflash volume
ID alone is not sufficient: a direct run with the old image and the current
RTC epoch produced no BKPT:

```
$ mkdir -p /tmp/opencode/c64-oldimg-currenttime
$ timeout 35 stdbuf -oL -eL /home/one/3ds/hwtest-staging/desmume-dbg-build/cli/desmume-cli --cflash-image /tmp/opencode/c64-run1/cflash.img --disable-sound --disable-limiter arm9v2/twlhd-arm9v2-autodump-c64.nds > /tmp/opencode/c64-oldimg-currenttime/desmume.log 2>&1
$ grep -c TWLHD_BKPT /tmp/opencode/c64-oldimg-currenttime/desmume.log
0
```

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
$ grep -m1 '^TWLHD_BKPT cpu=ARM9' /tmp/opencode/c64-final1/desmume.log
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
$ grep -m1 '^TWLHD_BKPT cpu=ARM9' /tmp/opencode/c64-final2/desmume.log
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

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
$ grep '^TWLHD_PLAN_RANGE_ENTRY ' /tmp/opencode/c64-final1/desmume.log
TWLHD_PLAN_RANGE_ENTRY frame=129 pc=02002640 sp=02FF3E28 lr=02001D53 cpsr=0000003F mode=1F r0=00000001 r1=00021000 r2=02048EE8 r3=00000004 lrslot=02FF3E18..02FF3E40
```

**FACT:** the 64-entry ring at that moment shows the CPU was in the ITCM IRQ
handler immediately before returning to `0x02001D50`, then jumping to
`0x02002640`:

```
$ grep TWLHD_PLAN_RANGE_ENTRY_RING /tmp/opencode/c64-final1/desmume.log
TWLHD_PLAN_RANGE_ENTRY_RING 02002E2C 02002E2E 02002E30 02002E32 02002E34 02002E5A 02002E5C 02002E3C 02002E3E 02002E42 02002E44 02002E38 020020DE 020020E0 020020E2 020020E4 02002162 02002164 02002166 02002168 0200216A 0200216C 0200216E 02002170 02002172 02002174 02002176 02002178 020020AE 020022CC 00000018 01FF8034 01FF8604 01FF8608 01FF860C 01FF8610 01FF8614 01FF8618 01FF861C 01FF8620 01FF8624 01FF8628 01FF862C 01FF8630 01FF8634 01FF8638 01FF863C 01FF8640 01FF8644 01FF8648 01FF864C 01FF8650 01FF8654 01FF8658 01FF865C 01FF8660 01FF8664 01FF8668 01FF866C 01FF8698 01FF869C 01FF86A0 02001D50 02002640
```

**FACT:** the quoted ring shows `0x020022CC` (inside the hook body
`0x020022b4..0x02002350`) as the last non-handler PC before the IRQ vector
`00000018`, and shows no `0x02001D4E` (the BL prefix) executing right before
that IRQ. **FACT:** the ring also contains `0x020020DE`, the LR value the
IRQ-between-BL-halves hypothesis needs for the BL suffix to reach
`0x02002640`. **HYPOTHESIS:** `0x020022CC` is therefore an earlier hook
invocation, not the hook call that was interrupted. This leaves a genuine
conflict with the IRQ-between-BL-halves story: that story predicts the BL
prefix at `0x02001D4E` would be the last PC before the IRQ and the hook body
would not execute, yet the ring shows a hook-body PC (`0x020022CC`) immediately
before the IRQ. This conflict is recorded as an open item.

**FACT:** the planner summary at the fault confirms the entry range:

```
$ grep TWLHD_PLAN_SUMMARY /tmp/opencode/c64-final1/desmume.log
TWLHD_PLAN_SUMMARY entries=858 min=02002640 max=020029AA
```

**FACT:** `arm-none-eabi-objdump` shows `0x02002640` is a Thumb branch inside
`twlhd_tex_capture_plan`:

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -A 3 -B 1 "200263e:\|2002640:\|2002642:"
 200263c:	429a      	cmp	r2, r3
 200263e:	d900      	bls.n	2002642 <twlhd_tex_capture_plan+0x72>
 2002640:	e0f0      	b.n	2002824 <twlhd_tex_capture_plan+0x254>
 2002642:	2f00      	cmp	r7, #0
 2002644:	d100      	bne.n	2002648 <twlhd_tex_capture_plan+0x78>
 2002646:	e089      	b.n	200275c <twlhd_tex_capture_plan+0x18c>
```

and `0x02001D4E` is the 32-bit Thumb `bl twlhd_gx_rec_hook`; `0x02001D50` is
its second halfword (the BL suffix):

```
$ arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -A 3 -B 2 "2001d4e:\|2001d50:"
 2001d4a:	6025      	str	r5, [r4, #0]
 2001d4c:	2204      	movs	r2, #4
 2001d4e:	f000 fab1 	bl	20022b4 <twlhd_gx_rec_hook>
 2001d52:	4653      	mov	r3, sl
 2001d54:	0020      	movs	r0, r4
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

**FACT:** the SP-change log from the final run shows an earlier IRQ-mode
alert at frame 120 and the first illegal SP change inside the planner/SYS-mode
context at `0x02002690` after the `add sp, r3` at `0x0200268E`:

```
$ grep TWLHD_SP_ALERT /tmp/opencode/c64-final1/desmume.log
TWLHD_SP_ALERT sp=02FF3FC0 above __sp_usr=02FF3E80 at pc=01FF8500 mode=13 frame=120
TWLHD_SP_ALERT sp=02FF4384 above __sp_svc=02FF3FC0 at pc=02002690 mode=1F frame=129
TWLHD_SP_ALERT sp=02FF4384 at/above DTCM top=02FF4000 at pc=02002690 mode=1F frame=129
```

**FACT:** decoding the windowed PC trace (`TWLHD_PCLOG2`) around the first
planner execution confirms the SP transition. The scratch decoder was
reconstructed from the patch's 12-byte little-endian `{pc,sp,cpsr}` record:

```
$ python3 /tmp/opencode/decode_pclog2.py /tmp/opencode/c64-final1/pclog2.bin 1282287 1282302
1282287 pc=020025BC sp=02FF3E14 cpsr=6000003F
1282288 pc=020025BE sp=02FF3E14 cpsr=6000003F
1282289 pc=020025C0 sp=02FF3E14 cpsr=6000003F
1282290 pc=020025C2 sp=02FF3E14 cpsr=6000003F
1282291 pc=02002766 sp=02FF3E28 cpsr=6000003F
1282292 pc=0200268C sp=02FF3E28 cpsr=6000003F
1282293 pc=0200268E sp=02FF3E28 cpsr=6000003F
1282294 pc=02002690 sp=02FF4384 cpsr=6000003F
1282295 pc=02002692 sp=02FF4394 cpsr=6000003F
1282296 pc=02002694 sp=02FF4394 cpsr=6000003F
1282297 pc=02002696 sp=02FF4394 cpsr=6000003F
1282298 pc=02002698 sp=02FF4394 cpsr=6000003F
1282299 pc=0200269A sp=02FF4394 cpsr=6000003F
1282300 pc=00000000 sp=02FF43A8 cpsr=6000001F
1282301 pc=01FF8020 sp=02FF43A8 cpsr=6000001F
1282302 pc=01FF853C sp=02FF43A8 cpsr=6000001F
```

The first instruction after which SP leaves the legal user-stack range inside
the planner/SYS-mode context is the `add sp, r3` at `0x0200268E`. Because the
planner was entered at `0x02002640`,
the matching prologue `add sp, r4` (which subtracts 1372) was skipped; the
epilogue alone adds 1372 (`0x55C`), moving SP from `0x02FF3E28` to
`0x02FF4384`.

### 3.7 DTCM configuration

**FACT:** the CP15 DTCM write log shows one guest write:

```
$ grep TWLHD_DTCM_WRITE /tmp/opencode/c64-final1/desmume.log
TWLHD_DTCM_WRITE pc=02004850 value=02FF0000
```

**FACT:** the scratch patch logs the value that DeSmuME stores, which is the
guest CP15 value masked to `val & 0x0FFFF000`:

```
$ grep -n -A 8 "MMU.DTCMRegion = armcp15->DTCMRegion = val & 0x0FFFF000" /home/one/3ds/hwtest-staging/desmume-dbg-patch/cubes64-entry-trace.patch
@@ -483,7 +484,17 @@
 				{
 				case 0:
 					MMU.DTCMRegion = armcp15->DTCMRegion = val & 0x0FFFF000;
+					{
+						// TWLHD diagnostic (scratch build only): log DTCM region config writes
+						extern armcpu_t NDS_ARM9;
+						char dbuf[160];
+						int dn = snprintf(dbuf, sizeof dbuf,
+							"TWLHD_DTCM_WRITE pc=%08X value=%08X\n",
+							NDS_ARM9.instruct_adr, val & 0x0FFFF000);
+						ssize_t dw = write(2, dbuf, (size_t)dn); (void)dw;
+					}
 					return TRUE;
```

**FACT:** the BKPT dump reports:

```
$ grep TWLHD_DTCM_REGION /tmp/opencode/c64-final1/desmume.log
TWLHD_DTCM_REGION=02FF0000
```

The logged value `0x02FF0000` has base `0x02FF0000` and low size/enable bits
zero. The linker script (`ds_arm9.mem`) places DTCM at `0x02ff0000, LENGTH = 16K`,
so the base matches. **UNVERIFIED:** whether the size/enable field of the raw
guest CP15 write was also zero, because `TWLHD_DTCM_WRITE` logs
`val & 0x0FFFF000`, not the raw value written by the instruction at
`0x02004850`.

---

## 4. Assessment of the three open items

Open item 1 (origin of entry into `twlhd_tex_capture_plan`):

**FACT:** the planner's official entry address `0x020025d0` / `0x020025d1` was
never reached before the fault. The function body was first entered at
`0x02002640` (`twlhd_tex_capture_plan:174`).
**FACT:** the previous 64 PCs show the CPU returned from the ITCM IRQ handler
(`0x01FF86A0`) to `0x02001D50` (the BL suffix halfword of the 32-bit Thumb
`bl twlhd_gx_rec_hook` at `0x02001D4E` in `twlhd_demo_draw_frame:464`, inside
the `glRotateYi` / shim `Port` assignment call sequence), and the next PC was
`0x02002640` with `LR=0x02001D53`.
**FACT:** the ring shows no PC in the hook body (`0x020022b4..0x02002350`)
between `0x02001D50` and `0x02002640`; the hook body was not executed between
the two.
**HYPOTHESIS:** an IRQ was taken between the two halfwords of the Thumb BL
pair, and the calico SYS-mode IRQ handler (`__arm_excpt_irq`,
`irq_handler.32.s`) left a different `LR_sys`, so the BL suffix computed
target = LR + offset. The BL encoding `f000 fab1` has offset +0x562:
- The prefix `f000` at `0x02001D4E` sets `LR = 0x02001D4E + 4 = 0x02001D52`,
  without the Thumb bit.
- The suffix `fab1` at `0x02001D50` branches to `LR_before_suffix + 0x562` and
  sets `LR = (address of next instruction) | 1 = 0x02001D52 | 1 = 0x02001D53`.
The observed `lr=02001D53` is therefore the value the suffix itself writes, not
"the prefix's own LR", and does not contradict the hypothesis. The normal
target from the prefix's LR is `0x02001D52 + 0x562 = 0x020022B4` (the hook),
while reaching `0x02002640` would require `LR_before_suffix = 0x02002640 −
0x562 = 0x020020DE` (Thumb `0x020020DF`). The handler's actual LR value is
**UNVERIFIED**, and the IRQ-between-BL-halves mechanism remains a hypothesis.
**UNVERIFIED:** the exact state of `LR_sys` across the IRQ entry and exit.

Open item 2 (why the fault SP is above `__sp_usr` and above DTCM top):

**FACT:** the entry SP at `0x02002640` was `0x02FF3E28`, a normal user-stack
value below `__sp_usr` (`0x02FF3E80`).
**FACT:** the first instruction after which SP left the legal stack range was
`add sp, r3` at `0x0200268E` (the planner epilogue's `add sp, r3`), in SYS mode
(`cpsr` mode `0x1F`). It added 1372 (`0x55C`) to SP, raising it to
`0x02FF4384`, then `pop {r4..r7}` raised it to `0x02FF4394`, and the final
`pop {r4, r5, r6, r7, pc}` loaded PC from `SP + 16` (derived from the §3.6
trace: SP before pop = `0x02FF4394`, PCLOG2 entry 1282299) = `0x02FF4394 +
0x10 = 0x02FF43A4`.
**HYPOTHESIS:** the abnormal SP is a direct consequence of mid-function entry:
the prologue that allocates the 1372-byte frame was skipped, so the epilogue's
`add sp, r3` is unpaired and walks SP up through the IRQ/SVC stack region and
above the nominal 16 KiB DTCM top.

Open item 3 (root cause):

**FACT:** the instruction that loads PC = 0 is `pop {r4, r5, r6, r7, pc}` at
`0x0200269A` inside `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:264`).
**FACT:** this `pop` is reached after the CPU entered the planner mid-function
at `0x02002640` (`:174`) from code context `twlhd_demo_draw_frame:464` /
`glRotateYi` / shim `Port` assignment, immediately after an IRQ handler return
from the BL suffix at `0x02001D50`.
**HYPOTHESIS:** an IRQ between the two halfwords of the 32-bit Thumb
`bl twlhd_gx_rec_hook` altered `LR_sys` and caused the suffix to branch to
`0x02002640` instead of to the hook at `0x020022B4` (see the arithmetic in Open
item 1). **HYPOTHESIS:** the CUBES=64 build has no legitimate caller of the
texture planner; no direct `bl` to `twlhd_tex_capture_plan` or
`twlhd_gx_rec_serialize_tex` was found in the ELF scan documented in
`cubes64-branch0-diagnosis.md:1106-1130`, so any entry into it is a wild jump.
**UNVERIFIED:** whether the calico SYS-mode IRQ handler preserves `LR_sys`
around the `bl` at `0x02001D4E`; the exact writer instruction and the memory it
wrote; and a narrower write watch on the actual return-address slot (not the
planner's prologue slot) is still missing.

---

## 5. Smallest next step and regression test idea (not implemented)

The BL-suffix misbranch hypothesis points to a guest-code / calico question,
not a link-time hiding step. The smallest next diagnostic step is the
experiment in Open item 1: log `LR_sys` around IRQ entry/exit at
`0x01FF8034..0x01FF86A0` and at the BL prefix `0x02001D4E` in the scratch
DeSmuME build. If the calico SYS-mode IRQ handler clobbers `LR_sys`, the fix
belongs in our guest code / calico usage; if `LR_sys` is preserved, the
mechanism is an emulator artefact.

A link-time mitigation (e.g. `-ffunction-sections -Wl,--gc-sections` or
excluding the texture serializer and planner sources when
`TWLHD_DEMO_SCENE != 5`, as discussed in
`cubes64-branch0-diagnosis.md:893-904`) would only remove the landing pad and
hide the symptom; it is not proposed as a fix unless the experiment above shows
an emulator-only artefact.

A regression test idea: a pinned-epoch scratch-CLI run that asserts no
`TWLHD_PLAN_RANGE_ENTRY` line and that the watchdog completes without the BKPT
dump. If a link-time mitigation is ever adopted, add a host build-verification
test asserting `twlhd_tex_capture_plan` and `twlhd_gx_rec_serialize_tex` are
absent from the ELF, labelled as symptom-hiding rather than root-cause
prevention.

---

## 6. Hardware relevance

**FACT:** no CUBES=64 AUTODUMP_FRAME=120 hardware run is recorded in the
repository or in the external logs searched for the prior report
(`cubes64-branch0-diagnosis.md:852-868`).

**HYPOTHESIS:** if the calico SYS-mode IRQ handler can clobber `LR_sys` between
the two halfwords of a Thumb BL pair, the same misbranch could occur on real
hardware. The current evidence is model-only (DeSmuME with a pinned RTC epoch +
fresh cflash image), and the fault entry is into texture code that
**HYPOTHESIS:** has no legitimate caller in the CUBES=64 build (no direct `bl`
was found in the ELF scan documented in `cubes64-branch0-diagnosis.md:1106-1130`).
A hardware occurrence cannot be excluded purely by source inspection.

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
`[SP+16]` (derived from the §3.6 trace: SP before pop = `0x02FF4394`, PCLOG2
entry 1282299) = `[0x02FF4394 + 0x10 = 0x02FF43A4]`, which contains `0`. **FACT:** the
CPU entered `twlhd_tex_capture_plan` mid-function at `0x02002640` (`:174`) after
returning from the ITCM IRQ handler (`0x01FF86A0`) to `0x02001D50`, the BL
suffix halfword of the 32-bit Thumb `bl twlhd_gx_rec_hook` at `0x02001D4E` in
`twlhd_demo_draw_frame:464` / `glRotateYi` / shim `Port` assignment. **FACT:**
because the planner prologue was skipped, the epilogue's `add sp, r3` unpairedly
raised SP from `0x02FF3E28` to `0x02FF4384` and then to the fault SP
`0x02FF43A8`. **HYPOTHESIS:** the fault is reproducible at pinned RTC epochs
`TWLHD_RTC_EPOCH=1794921881` (2026-11-17 22:24:41 +0900),
`TWLHD_RTC_EPOCH=1794925481` (2026-11-17 23:24:41 +0900), and
`TWLHD_RTC_EPOCH=1794925511` (2026-11-17 23:25:11 +0900) with a fresh cflash
image, and not at `TWLHD_RTC_EPOCH=1794927600` (2026-11-18 00:00:00 +0900),
`TWLHD_RTC_EPOCH=1794929081` (2026-11-18 00:24:41 +0900), or
`TWLHD_RTC_EPOCH=1794961800` (2026-11-18 09:30:00 +0900); the observation covers only
the six sampled epochs. **HYPOTHESIS:** the entry is caused by an IRQ taken
between the two halfwords of the Thumb BL pair, with the calico SYS-mode IRQ
handler leaving a different `LR_sys` so the BL suffix computed target = LR +
offset. The BL encoding `f000 fab1` has offset +0x562:
- The prefix `f000` at `0x02001D4E` sets `LR = 0x02001D4E + 4 = 0x02001D52`,
  without the Thumb bit.
- The suffix `fab1` at `0x02001D50` branches to `LR_before_suffix + 0x562` and
  sets `LR = (address of next instruction) | 1 = 0x02001D52 | 1 = 0x02001D53`.
The observed `lr=02001D53` is therefore the value the suffix itself writes, not
the prefix's own LR, and does not contradict the hypothesis. The normal target
from the prefix's LR is `0x02001D52 + 0x562 = 0x020022B4` (the hook), while
reaching `0x02002640` would require `LR_before_suffix = 0x02002640 − 0x562 =
0x020020DE` (Thumb `0x020020DF`). This hypothesis conflicts with the ring
evidence that a hook-body PC precedes the IRQ; the conflict is recorded as an
open item. **UNVERIFIED:** the exact state of `LR_sys` across the IRQ entry and
exit, the exact write (if any) that corrupted the return path, and whether real
hardware could ever take the same path.

---

## Open items

1. **UNVERIFIED:** whether the calico SYS-mode IRQ handler preserves `LR_sys`
   across the `bl twlhd_gx_rec_hook` at `0x02001D4E`. The next smallest
   experiment is to log `LR_sys` around IRQ entry/exit at
   `0x01FF8034..0x01FF86A0` and at the BL prefix `0x02001D4E` in the scratch
   DeSmuME build. This determines whether the mechanism is a guest-code/calico
   issue (may affect hardware) or an emulator artefact.

2. **UNVERIFIED:** whether the same fault can be reproduced on real hardware.
   No hardware run exists.

3. **UNVERIFIED:** why the fault is sensitive to the RTC epoch and to a fresh
   (versus pre-written) cflash image. The experiments show both inputs matter,
   but the mechanism inside DeSmuME/libfat has not been isolated.

4. **UNRESOLVED conflict:** the IRQ-between-BL-halves hypothesis predicts the BL
   prefix at `0x02001D4E` would be the last PC before the IRQ and the hook body
   would not execute, but the 64-entry ring shows the hook-body PC `0x020022CC`
   immediately before the IRQ vector `00000018`. The ring also contains
   `0x020020DE`, the LR value the hypothesis needs, but does not show how that
   value reached `LR_sys` at the BL suffix. This conflict must be resolved by
   logging `LR_sys` around the IRQ and the BL halfwords.

---

## Requirement traceability

- **REQ-11 (round 1 repair of entry-trace report, 2026-11-18).** Fixed the
  BL-halfword misreading: `0x02001D50` is now identified as the BL suffix
  halfword of the 32-bit Thumb `bl twlhd_gx_rec_hook` at `0x02001D4E`
  (§3.4, §4 Open item 1/3, CAUSE, and `cubes64-branch0-diagnosis.md` Update
  item 1). Replaced the contradicted "corrupted return from
  `twlhd_gx_rec_hook`" hypothesis with the IRQ-between-BL-halves / different
  `LR_sys` hypothesis, including the `f000 fab1` → +0x562 offset arithmetic.
  Deleted the false "64-entry ring was overwritten by 858 planner instructions"
  sentence. Corrected the pop-address from `0x02FF43B8` to
  `0x02FF43A4 = 0x02FF4394 + 0x10` throughout. Relabelled the RTC claim as
  **HYPOTHESIS** restricted to the six sampled epochs with exact
  `TWLHD_RTC_EPOCH` values. Stated that `TWLHD_DTCM_WRITE` logs the masked
  value `val & 0x0FFFF000` and labelled the size-field claim **UNVERIFIED**.
  Labelled the three dead-code/no-caller claims as **HYPOTHESIS** with a pointer
  to the ELF scan in `cubes64-branch0-diagnosis.md:1106-1130`. Rewrote §5, §6,
  CAUSE, and Open item 1 to reflect the guest-code/hardware implication of the
  new hypothesis and named the `LR_sys` logging experiment as the next smallest
  step. Fixed the `epilogue'sn` markdown typo. Evidence:
  `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf` (BL encoding
  `f000 fab1` and branch target arithmetic); the patch snippet from
  `/home/one/3ds/hwtest-staging/desmume-dbg-patch/cubes64-entry-trace.patch`
  showing `TWLHD_DTCM_WRITE` logs `val & 0x0FFFF000`; `make test` passes.

- **REQ-12 (round 2 repair of entry-trace report, 2026-11-18).** Removed the
  remaining standalone ellipsis placeholders so that §2.2–§2.4 now quote the
  full commands and their raw unedited outputs for the five Nov-18
  non-reproducing runs, the RTC/cflash sweep, the side-by-side diff, the two
  `file` commands, and the old-image/current-epoch run; the capture-loop output
  is explicitly omitted rather than truncated. Also quoted the exact
  `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E
  '^020025d0 <twlhd_tex_capture_plan>:|^ 20025d0:|^ 200268c:|^ 200268e:|^ 2002690:|^ 200269a:'`
  and
  `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E
  '^ 2001d4e:|^ 2001d52:'`
  commands and their raw outputs for the planner prologue/epilogue and the
  `bl twlhd_gx_rec_hook` call site in §1, replacing the earlier abbreviated
  disassembly block. `make test` passes.

- **REQ-13 (round 3 repair of entry-trace report, 2026-11-18).** Corrected the BL/LR arithmetic in §4 Open item 1, CAUSE, and `cubes64-branch0-diagnosis.md` Update item 1: the prefix `f000` at `0x02001D4E` sets `LR = 0x02001D4E + 4 = 0x02001D52` (no Thumb bit); the suffix `fab1` at `0x02001D50` branches to `LR_before_suffix + 0x562` and sets `LR = (address of next instruction) | 1 = 0x02001D53`; the observed `lr=02001D53` is the suffix's own LR result, not evidence against the clobbered-LR hypothesis. Removed the "Tension" sentence in all three places. Evidence: `arm-none-eabi-objdump -d arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E '^ 2001d4e:|^ 2001d52:'` (`f000 fab1`). Relabelled the §3.4 hook-invocation inference as **HYPOTHESIS**, stated plainly that the ring does not show the BL prefix at `0x02001D4E` right before the IRQ, noted that `0x020020DE` (the LR value the hypothesis needs) also appears in the ring, and added Open item 4 naming the conflict with a hook-body PC (`0x020022CC`) immediately before the IRQ vector. Evidence: `grep TWLHD_PLAN_RANGE_ENTRY_RING /tmp/opencode/c64-final1/desmume.log`. Labelled `0x02FF43A4` in §4 Open item 2 and CAUSE as derived from the §3.6 trace (`SP = 0x02FF4394` before the final `pop {pc}`, PCLOG2 entry 1282299), +16. Evidence: `python3 /tmp/opencode/decode_pclog2.py /tmp/opencode/c64-final1/pclog2.bin 1282287 1282302`. Replaced the hand-added PCLOG2 index marker with the full scratch-decoder command and its raw, unannotated output. Quoted the full commands and raw outputs for the §1 `arm-none-eabi-nm`/`readelf` evidence, the §3.3 `TWLHD_BKPT` grep, the §3.4 `TWLHD_PLAN_RANGE_ENTRY`/`RING`/`SUMMARY` greps, the §3.6 `TWLHD_SP_ALERT` grep, and the §3.7 `TWLHD_DTCM_WRITE`/`TWLHD_DTCM_REGION` greps. Rewrote §2.4 to quote a no-override run and limit the non-old-epoch claim to "no `TWLHD_BKPT` line". Evidence: `mkdir -p /tmp/opencode/c64-noepoch-rerun-1 && DESMUME_CLI=/home/one/3ds/hwtest-staging/desmume-dbg-build2/cli/desmume-cli DESMUME_TIMEOUT=30 tools/emulator/desmume_capture.sh arm9v2/twlhd-arm9v2-autodump-c64.nds /tmp/opencode/c64-noepoch-rerun-1` (output `capture: /tmp/opencode/c64-noepoch-rerun-1/twlhd_frame_v2.bin`) and `grep -c TWLHD_BKPT /tmp/opencode/c64-noepoch-rerun-1/desmume.log` (`0`). Rewrote `cubes64-branch0-diagnosis.md` Update section diff sentence to state the diff was filtered with `grep -v '^TWLHD_FRAME'` and diverges at line 55 after path differences at lines 2, 7 and 25. Replaced the abbreviated objdump command reference in REQ-12 with the two full commands quoted in §1. `make test` passes.
- **REQ-14 (round 4 repair of entry-trace report, 2026-11-18).** Fixed six evidence-discipline defects in `docs/reports/cubes64-entry-trace.md` and the Update section of `docs/reports/cubes64-branch0-diagnosis.md`: (1) deleted the stale "tension" clause in REQ-11 (consistent with REQ-13); (2) reworded §2.2 to state that, with `TWLHD_FRAME` lines filtered out, the old and new logs differ only in cflash image paths at lines 2, 7 and 25 until line 55, where the old log has the `ARM OP_BKPT`/BKPT dump block and the new log continues with `TWLHD_SWAP n=180 frame=188`; (3) limited the §2.2 non-reproduction claim to "produced no `TWLHD_BKPT` line" (matching the §2.4/REQ-13 precedent); (4) reworded the Update section epoch claim as **HYPOTHESIS** restricted to the six sampled epochs with exact `TWLHD_RTC_EPOCH` values (reproduced at `1794921881`, `1794925481`, `1794925511`; not at `1794927600`, `1794929081`, `1794961800`) and limited the Nov-18 epochs to "produced no `TWLHD_BKPT` line"; (5) scoped §2.3 autodump sha256sum and cflash `file` comparisons to the two directories actually compared (`c64-run1` and `c64-ref1`) and removed the unevidenced "fresh morning image" wording; (6) replaced the command-less §1/§2.3 `ROM crc: 1B79DB9D` FACTs with a quoted `grep -H 'ROM crc' ...` command and its raw output covering every run quoted below. Evidence: the `grep -H 'ROM crc' ...` command quoted in §1 and its raw output. No new scratch DeSmuME runs were required; existing `/tmp/opencode/c64-*` logs were used. `make test` passes.

- **REQ-15 (round 4 docs-only repair of entry-trace report and branch0 Update, 2026-11-18).** Removed the unlabeled general epoch claim in `docs/reports/cubes64-branch0-diagnosis.md` Update section and replaced it with a scoped observation: with a fresh cflash image, reproduced the reference dump at `TWLHD_RTC_EPOCH=1794921881`, `1794925481` and `1794925511` (2026-11-17 22:24:41, 23:24:41, 23:25:11 +0900) and produced no `TWLHD_BKPT` line at `TWLHD_RTC_EPOCH=1794927600`, `1794929081` and `1794961800`. Reworded the Update section **HYPOTHESIS** to state that the observation covers only the six sampled epochs. Aligned the branch0 Update non-reproduction sentence with the REQ-13/REQ-14 precedent ("produced no `TWLHD_BKPT` line"). Made the same wording change in the `cubes64-entry-trace.md` CAUSE hypothesis. For the §1 ROM-CRC FACT, chose option (a): added `/tmp/opencode/c64-oldimg-currenttime/desmume.log` to the quoted `grep -H 'ROM crc' ...` command and verified the real 24-line raw output, so the FACT now covers every run quoted in the report including the §2.4 old-image/current-epoch run. Verified that the `cubes64-entry-trace.md` §2.3 FACT and the branch0 Update sentence ("the ROM CRC was identical across every run quoted in ... §1") remain consistent with the expanded §1 scope. Evidence: the corrected Update paragraph and the six `TWLHD_RTC_EPOCH` values for defect 1; the exact `grep -H 'ROM crc' /tmp/opencode/c64-run1/desmume.log ... /tmp/opencode/c64-oldimg-currenttime/desmume.log` command quoted in §1 and its raw 24-line output for defect 2. No new scratch DeSmuME runs were required; existing `/tmp/opencode/c64-*` logs were used. TESTS: `make test` — no regression.

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

---

## Update (2026-11-18): IRQ/LR trace follow-up

Follow-up investigation documented in `docs/reports/cubes64-irq-lr-trace.md`.
A new round-2 scratch patch
(`$HOME/3ds/hwtest-staging/desmume-dbg-patch/cubes64-irq-lr-trace-r2.patch`)
and a new build (`$HOME/3ds/hwtest-staging/desmume-dbg-build4/cli/desmume-cli`)
were used. The rebuilt ROM still logs `ROM crc: 1B79DB9D` and the ELF
addresses match the values above.

**FACT:** with the `cflash.img` from the original `/tmp/opencode/c64-irqtrace-r2v2-e1794921881-1` run,
the round-2 instrumented build reproduced the reference dump at
`TWLHD_RTC_EPOCH=1794921881`. The creation provenance of that image is
**UNVERIFIED**: no saved script output contains the `set -x` `+ mkfs.fat`
creation line. The post-run image currently on disk is:

```
$ sha256sum /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/cflash.img
c7aaa6a68b13765821b305150a82eae252b070ce54ed730216388ade6db4b6a7  /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/cflash.img
```

**FACT:** that same `-1` run reproduced the reference fault signature:

```
$ grep -m1 '^TWLHD_BKPT cpu=ARM9' /tmp/opencode/c64-irqtrace-r2v2-e1794921881-1/desmume.log
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
```

**FACT:** the original fault-reproduction runs used the same capture script with
`DESMUME_TIMEOUT=30` and a scratch CLI (`docs/reports/cubes64-branch0-diagnosis.md:183-187`).
The instrumented entry-trace runs added `TWLHD_RTC_EPOCH=1794925481` and a
windowed PC trace (`TWLHD_PCLOG2`) (`docs/reports/cubes64-entry-trace.md:396-400`);
`TWLHD_PCLOG` is the opt-in 4 M-record PC trace
(`docs/reports/bkpt-diagnosis.md:93-94`). The only env var set for the new run
was `TWLHD_RTC_EPOCH=1794921881` (no `TWLHD_PCLOG2` override).

**HYPOTHESIS:** dropping `TWLHD_PCLOG`/`TWLHD_PCLOG2` does not change the
emulated run: both are opt-in outputs produced by the *scratch emulator*, not
inputs to the emulated ROM. `TWLHD_RTC_EPOCH=1794921881` is the epoch that
reproduced the fault in §2.4. The raw logs are quoted in
`docs/reports/cubes64-irq-lr-trace.md` §3.

**Open item 1 (calico handler preserves `LR_sys`):** resolved as FACT about the
model. The ITCM handler `__arm_excpt_irq` runs in IRQ mode, saves `LR_irq` on
the IRQ stack (`push {r0, r1, r2, r3, ip, lr}` at `0x01FF8608`) and restores it
on the no-reschedule return path (`ldmeq sp!, {r0, r1, r2, r3, ip, pc}^` at
`0x01FF86A0`). It does not save or restore `LR_sys` on that path. The runtime
log shows `LR_sys=0x020020DF` both at IRQ entry and after IRQ exit, so the
handler itself did not change `LR_sys`.

**Open item 2 (hardware reproduction):** unchanged — **UNVERIFIED**; no
hardware run exists.

**Open item 3 (RTC/cflash sensitivity):** unchanged — **UNVERIFIED**; the new
run confirms the same epoch pattern for `1794921881` but does not isolate the
DeSmuME/libfat mechanism.

**Open item 4 (ring shows hook-body PC `0x020022CC` before IRQ):** UNRESOLVED.
The relevant lines show: the last guest PC in the 64-entry ring before the IRQ
vector is `0x020022CC`; `TWLHD_IRQ_EXCEPTION` records `ia=0x02001D52`; and the
handler returned to `0x02001D50` (the BL suffix) with `LR_sys=0x020020DF`, after
which the suffix branched to `0x02002640`. The *location* where the CPU was when
the IRQ was taken (inside `twlhd_gx_rec_hook` versus at `0x02001D52`) is
**UNVERIFIED**; it is one of the conflicting observations itself, not a
confirmed fact. The three observations quoted in
`docs/reports/cubes64-irq-lr-trace.md` §5 are inconsistent (the
`TWLHD_IRQ_EXCEPTION` line and the handler `sub lr, lr, #4` both imply a return
to `0x02001D52`, while the runtime log shows `0x02001D50`), so Open item 4 is
kept open and the return-address discrepancy remains **UNVERIFIED**.

**Consequence for the CAUSE hypothesis:** the statement "an IRQ taken between
the two halfwords of the Thumb BL pair ... leaves a different `LR_sys`" is not
confirmed by the log. The following chain is **HYPOTHESIS/UNVERIFIED**: the BL
completed normally, the hook body ran, an IRQ was taken, the handler returned to
the BL suffix address, and the suffix re-executed with a stale `LR_sys` value
already present before the IRQ. It is relabelled as a **HYPOTHESIS** about a
DeSmuME model artefact; the earlier CAUSE paragraph in this report should be
read with this Update in mind. The FACT part is the `LR_sys` change timing,
which is backed by the per-instruction `TWLHD_R14_TRACE` quoted in
`docs/reports/cubes64-irq-lr-trace.md` §3.

**REQ-16 (round 1 repair of irq-lr-trace report and branch0 Update,
2026-11-18).** Repaired ten evidence-discipline defects in
`docs/reports/cubes64-irq-lr-trace.md` and the Update section of
`docs/reports/cubes64-entry-trace.md`: relabelled the `LR_sys` change timing
with per-instruction R14 evidence; restated Open item 4 as **UNRESOLVED** with
three conflicting observations side by side; rewrote §6 as numbered
**HYPOTHESES** plus one next minimal experiment; removed the unsupported
"approximately 71 instructions" claim and stated the 69-instruction guest
window; corrected the §7 ARMv5TE Thumb BL premise; added a dedicated IRQ
entry/exit ring showing 16 pairs; replaced
elided FACT blocks with complete raw output or narrower commands; quoted
cflash provenance and env vars; quoted the `demo_frame.cpp:464` call site; and
quoted the real `make test` tail. Extended the scratch DeSmuME copy with
`cubes64-irq-lr-trace-r2.patch` and build4 only. Aligned
`docs/reports/cubes64-branch0-diagnosis.md` Update item 1 with the restated
Open item 4. Round 4 amendment: the `-1` cflash creation provenance is now
**UNVERIFIED** (scoped post-run `sha256sum` only); the r3-1 run was removed
from the Update.

**REQ-17 (round 2 repair of irq-lr-trace report and entry-trace Update,
2026-11-18).** Aligned the Update section with the round-2 evidence-discipline
repairs in `docs/reports/cubes64-irq-lr-trace.md`: removed the "confirmed
*location* observation" wording from Open item 4 and labelled the location
**UNVERIFIED** (only per-line facts are stated: last guest PC `0x020022CC`;
`TWLHD_IRQ_EXCEPTION ia=0x02001D52`; handler return to `0x02001D50`).
Reworded the CAUSE-consequence sentence so the return-to-suffix chain is
self-labelled **HYPOTHESIS/UNVERIFIED**. Added setup-parity text quoting the
original `DESMUME_TIMEOUT=30` capture-script protocol next to the new build4
command, explaining that `TWLHD_PCLOG`/`TWLHD_PCLOG2` are described as
emulator-side logging outputs rather than ROM inputs, and correcting the cflash
provenance to quote the script's `set -x` creation line and a post-run
(flushed) `sha256sum`. Updated REQ-16 to reflect the corrected 69-instruction
guest window and 16-pair IRQ ring. Round 4 correction: the r3-1 run was removed
because its full launch command and `set -x` printing command were not
recorded; the `-1` image creation provenance is now **UNVERIFIED** (scoped
post-run `sha256sum` only); the `TWLHD_PCLOG` scope claim was backed by a
tree-wide `grep -rn`; the build4 source-tree link was backed by
`meson introspect --buildsystem-files`.

**REQ-18 (round 3 repair of irq-lr-trace report and entry-trace Update,
2026-11-18).** Corrected three evidence-discipline defects propagated from
`docs/reports/cubes64-irq-lr-trace.md`: (1) replaced the contradictory cflash
provenance with a fresh run into
`/tmp/opencode/c64-irqtrace-r2v2-e1794921881-r3-1`, quoting the script's
`set -x` `+ mkfs.fat -C ...` creation line and the post-run (flushed)
`sha256sum`; removed "taken before the emulator run" and the manual `mkfs.fat`
quote; (2) relabelled "Dropping `TWLHD_PCLOG`/`TWLHD_PCLOG2` does not change
the emulated run" as **HYPOTHESIS** in the Update, REQ-8 and REQ-17; (3)
referenced the r2 patch/build4 link evidence (`wc -l`, reverse
`patch --dry-run`, `grep -c TWLHD_IRQTRACE` in the source tree) documented in
`docs/reports/cubes64-irq-lr-trace.md` §1. No product code, tests, harness or
pristine trees were modified. Round 4 correction: the r3-1 run was removed and
the `-1` cflash creation provenance relabelled **UNVERIFIED**; the build4
source-tree link was backed by `meson introspect --buildsystem-files`; the
`TWLHD_PCLOG` scope claim was backed by a tree-wide `grep -rn`.

**REQ-19 (round 4 docs-only repair of irq-lr-trace report and
entry-trace Update, 2026-11-18).** Repaired three evidence-discipline defects:
(1) removed the `/tmp/opencode/c64-irqtrace-r2v2-e1794921881-r3-1` run from
`docs/reports/cubes64-irq-lr-trace.md` §1 and this Update because its full
launch command and the command printing its `set -x` line were not recorded,
relabelled the creation provenance of the `-1` `cflash.img` as **UNVERIFIED**,
and quoted only a scoped post-run `sha256sum` of the current `-1` image; (2)
corrected the `TWLHD_PCLOG` `grep -n -A9` quote to end at source line 773 and
backed the scope claim with the full output of `grep -rn 'TWLHD_PCLOG'
$HOME/3ds/hwtest-staging/desmume-dbg/desmume/src`; (3) added a
`meson introspect --buildsystem-files` FACT linking `desmume-dbg-build4` to the
scratch source tree and corrected the REQ-18 wording to say the
`grep -c TWLHD_IRQTRACE` command covers the source tree, not build4. Updated
REQ-7/REQ-8/REQ-16/REQ-17/REQ-18 summaries with round-4 amendment clauses. Did
not modify product code, tests, harness, or pristine trees. Evidence:
`docs/reports/cubes64-irq-lr-trace.md` §1 (build4 source-dir, cflash
UNVERIFIED, PCLOG scope), Verification (`make test`).

`make test` passes; the real tail is quoted in
`docs/reports/cubes64-irq-lr-trace.md` Verification.
