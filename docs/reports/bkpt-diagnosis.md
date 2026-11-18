<!-- SPDX-License-Identifier: Apache-2.0 -->
# `ARM OP_BKPT` on the SCENE=2 / SCENE=3 / CUBES=2 autodump captures — diagnosis

**Scope:** DIAGNOSIS + EMULATOR only. No product code, ROM source, test,
interpreter, renderer, harness or DeSmuME tree was modified; the only file this
task adds to the repository is this report. A scratch copy of DeSmuME was built
**outside** the repository and the GPL patch lives outside it too (section 1).
No console, SD-card or network action was performed.

**Labels.** FACT = read at the cited `file:line`, or a command output quoted
below. HYPOTHESIS = an inference from facts that was not directly measured.
UNVERIFIED = no local source exists / it was not run. Emulator facts are facts
about the *model*, never about hardware.

**Evidence paths.**
- DeSmuME source citations are the **pristine** checkout
  `/home/one/3ds/desmume` (`release_0_9_13-429-gfc504ee0`).
- Scratch derivative: `/home/one/3ds/hwtest-staging/desmume-dbg` (rsync of the
  pristine tree), patch `hwtest-staging/desmume-dbg-patch/bkpt-register-dump.patch`,
  build directory `hwtest-staging/desmume-dbg-build` (new; the existing
  `desmume-build` was not touched).
- ROMs: `arm9v2/twlhd-arm9v2-autodump-{s1,s2,s3,c2,c64}.nds`,
  `arm9v2/twlhd-arm9v2-s2.nds` (non-autodump). Their SHA-256 match the recorded
  run for s1/s2/c2 (`tools/fixtures/eq_scenes/README.md:36-50`); scene3 has no
  fixture row.

---

## 0. Result in one table

Diagnostic CLI, documented capture script, `--cflash-image` (therefore MPCF +
DLDI autopatch), `--disable-limiter`, 25-30 s:

| ROM (build) | frame at fault | faulting PC | LR at fault | 3D frames presented before fault |
|---|---|---|---|---|
| `SCENE=2 AUTODUMP_FRAME=240` | **0** | `0x01FF853C` | `0x0200C3C0` | **0** |
| `SCENE=3 AUTODUMP_FRAME=240` | **0** | `0x01FF853C` | `0x020166B1` | **0** |
| `CUBES=2 AUTODUMP_FRAME=240` | **6** | `0x01FF853C` | `0x02380074` | **0** |
| `CUBES=64 AUTODUMP_FRAME=120` (analogue) | **129** | `0x01FF853C` | `0x02002767` | 120+ (last instrumented submission n=120, frame 122) |
| `SCENE=1 AUTODUMP_FRAME=240` (works) | — | no BKPT | — | yes (continuous) |

`0x01FF853C` is `__arm_excpt_rst`, the **reset** slot of the ITCM exception
vector table, whose instruction is `bkpt 0x8000`. It is not where the fault
happens; it is where the CPU arrives *after* it has branched to address
`0x00000000`. The measured cause of the BKPT message in every row is:

**the ARM9 executes a branch to address 0, the vector table sends it to the
reset slot, and DeSmuME's `OP_BKPT` handler then leaves the CPU in abort mode.**

Section 2 shows the branch sites, section 3 the discriminators, section 4 what
is fact and what is not.

---

## 1. Method A — how the PC was measured (EMULATOR)

**Scratch tree (not this repository).**

```
rsync -a /home/one/3ds/desmume/ /home/one/3ds/hwtest-staging/desmume-dbg/
```

**Build**, same recipe as `hwtest-staging/build_desmume.sh`, new build
directory:

```
meson setup /home/one/3ds/hwtest-staging/desmume-dbg-build \
  /home/one/3ds/hwtest-staging/desmume-dbg/desmume/src/frontend/posix \
  -Dfrontend-gtk=false -Dfrontend-cli=true -Dopengl=false -Dwifi=false \
  --buildtype=release
ninja -C /home/one/3ds/hwtest-staging/desmume-dbg-build
```

Result (FACT, real output):

```
[3/3] Linking target cli/desmume-cli
```

The libpcap private-header workaround noted in
`docs/EMULATOR_TRACER_FEASIBILITY.md:151` was **not** needed here
(`Run-time dependency pcap found: YES 1.10.6` in
`hwtest-staging/desmume-dbg-build.setup.log`); the scratch tree also exposes no
gdb-stub meson option, so the register-print patch was used as the contract
preferred.

**Diagnostic patch** (`hwtest-staging/desmume-dbg-patch/bkpt-register-dump.patch`,
GPL, outside the repository; 237 lines over 5 files). It does one thing per
file, all guarded to the scratch build:

- `src/armcpu.h`, `src/armcpu.cpp` — a 16-entry per-CPU PC ring written in
  `armcpu_exec()`; a jump-to-0 counter (LR, CPSR, frame of the first `PC==0`); a
  frame-counter high-water mark; an opt-in 4 M-record binary PC trace
  (`TWLHD_PCLOG`); 60-frame milestone prints; and a print for the first 20
  executions inside the DLDI region `0x037F0000-0x03810000`.
- `src/arm_instructions.cpp` (in `OP_BKPT`, before DeSmuME switches to abort
  mode) — one `write(2, …)` of CPU (ARM9/ARM7), `currFrameCounter`, PC, CPSR,
  SP, LR, R0-R12, 16 stack words **below** SP, the last 16 PCs, and the DLDI/RAM
  words at the DLDI stub address. Unbuffered, single syscall, to keep the
  observer effect minimal.
- `src/gfx3d.cpp` — count of `SWAP_BUFFERS` submissions (i.e. 3D frames actually
  presented).
- `src/NDSSystem.cpp` — `TWLHD_NODLDI` (skip the autopatch),
  `TWLHD_SCRATCHPATCH` (do the patch on a throwaway copy, emulated ROM untouched),
  `TWLHD_DUMPROM` (write out the ROM image as patched).

Short quote of the core hunk (the mechanism under test):

```c
	printf("ARM OP_BKPT triggered\n");
	{
		char buf[1024];
		int n = snprintf(buf, sizeof buf,
			"TWLHD_BKPT cpu=ARM%c frame=%d max_frame=%d resets=%d pc=%08X cpsr=%08X sp=%08X lr=%08X intvec=%08X\n", ...);
```

The emulator was always driven through the documented script
(`DESMUME_CLI=<scratch cli> tools/emulator/desmume_capture.sh <rom> <dir>`), so
the invocation, watchdog, FAT image and log checks are the shipped ones.

Baseline (FACT): the **stock** installed CLI reproduces the failure through the
same script, so the fault is not introduced by the patch:

```
== stock-s2-1  ARM OP_BKPT count=1
== stock-s2-2  ARM OP_BKPT count=1
== stock-s3-1  ARM OP_BKPT count=1
== stock-s3-2  ARM OP_BKPT count=1
== stock-c2-1  ARM OP_BKPT count=1
== stock-c2-2  ARM OP_BKPT count=1
== stock-s1-1  ARM OP_BKPT count=0
```

---

## 2. Method B — the faulting state, and what it maps to

### 2.1 Raw measurement (real output, one block per ROM)

`SCENE=2` (identical in all six runs: `run-s2img`, `run-s2img2`, `run-rep-img1..3`, `f-s2b`):

```
TWLHD_BKPT cpu=ARM9 frame=0 max_frame=0 resets=1 pc=01FF853C cpsr=6000001F sp=02FF3E18 lr=0200C3C0 intvec=00000000
TWLHD_BKPT_PCZERO count=1 last_frame=0 last_lr=0200C3C0 last_cpsr=6000001F
TWLHD_BKPT_REGS r0=00000000 r1=00000000 r2=0000001C r3=6000001F r4=0200F4CF r5=02025D20 r6=02060400 r7=00000000 r8=00000002 r9=00000001 r10=00010000 r11=FFFFFFFF r12=0205FF68
TWLHD_BKPT_RING 0200C3E8 0200C3EC 020166B4 020166B6 020166B8 0200F5FE 0200F550 0200F552 0200F554 0200F556 0200F558 0200F55A 0200F55C 00000000 01FF8020 01FF853C
```

`SCENE=3`:

```
TWLHD_BKPT cpu=ARM9 frame=0 max_frame=0 resets=1 pc=01FF853C cpsr=6000001F sp=02FF3DC8 lr=020166B1 intvec=00000000
TWLHD_BKPT_RING 0201C354 0201C358 0201C35C 0201C360 0201C364 0201C368 0201C36C 0201C370 0201C374 0201C378 0201C37C 0201C380 0201C384 00000000 01FF8020 01FF853C
```

`CUBES=2`:

```
TWLHD_BKPT cpu=ARM9 frame=6 max_frame=6 resets=1 pc=01FF853C cpsr=6000001F sp=0380FE80 lr=02380074 intvec=00000000
TWLHD_BKPT_RING 01FF8648 01FF864C 01FF8650 01FF8654 01FF8658 01FF865C 01FF8660 01FF8664 01FF8668 01FF866C 01FF8698 01FF869C 01FF86A0 00000000 01FF8020 01FF853C
```

`CUBES=64 AUTODUMP_FRAME=120` (the analogue from `docs/EMULATOR_VALIDATION.md:286`):

```
TWLHD_BKPT cpu=ARM9 frame=129 max_frame=129 resets=1 pc=01FF853C cpsr=6000001F sp=02FF43A8 lr=02002767 intvec=00000000
TWLHD_BKPT_RING 020025BC 020025BE 020025C0 020025C2 02002766 0200268C 0200268E 02002690 02002692 02002694 02002696 02002698 0200269A 00000000 01FF8020 01FF853C
```

Four facts fall out of the ring, which is chronological (oldest first):

- Every row executes `… <site> 00000000 01FF8020 01FF853C`: the last
  "real" instruction is a branch to **address 0**, then the CPU executes
  `01FF8020` (the ITCM vector-table reset slot) and lands on the `bkpt`.
- `cpsr=6000001F` in every row: the branch happened in **SYS mode, ARM state**,
  i.e. ordinary user code, not inside an exception entry.
- `frame=0` for scene2/scene3 means the fault happens **before the first
  VBlank of the emulated boot**, and `SWAP_BUFFERS` never fired at all for
  scene2/scene3/cube2 — so those ROMs never present a 3D frame. The harness
  record ("scene3 presents no 3D frame even there") is confirmed for the
  emulator side (FACT).
- ARM7 is never involved: `cpu=ARM9` in every row.

### 2.2 Mapping (`arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-s2.elf`)

```
$ arm-none-eabi-addr2line -f -e arm9v2/twlhd-arm9v2-autodump-s2.elf 0x01ff853c
__arm_excpt_rst
/home/davem/projects/devkitpro/pacman-packages/calico/src/calico-1.2.0/source/nds/arm9/excpt_handler.32.s:27
$ arm-none-eabi-nm arm9v2/twlhd-arm9v2-autodump-s2.elf | grep -E "1ff80(00|20|38)|1ff853c"
01ff8000 t __itcm_start
01ff8038 T __excpt_vectors
01ff853c t __arm_excpt_rst
$ arm-none-eabi-objdump -d --start-address=0x01ff8020 --stop-address=0x01ff8040 …elf
01ff8020:  e59ff010  ldr  pc, [pc, #16]   @ 1ff8038 <__excpt_vectors>   <- reset slot
01ff8038:  .word 0x01ff853c                                        <- -> __arm_excpt_rst
```

So the PC is the **consequence**. The cause is the branch to 0. For `SCENE=2`
the branch site is at `0x0200F55C`:

```
$ arm-none-eabi-objdump -d --start-address=0x0200f540 --stop-address=0x0200f560 …elf
200f550: bc80  pop {r7}
200f552: 46b8  mov r8, r7
200f554: bcf0  pop {r4, r5, r6, r7}
200f556: bc02  pop {r1}
200f558: 4708  bx  r1            <- r1 = 0  -> PC = 0
```

`0x0200F550-0x0200F55C` is the epilogue of newlib `setvbuf`
(`addr2line` → `setvbuf ??:?`; no line info in the prebuilt newlib), and
`r1=00000000` in the register dump is the value it popped. The full call chain
comes from a 4 M-record PC trace (`TWLHD_PCLOG`), which reaches the fault at
record 77 111:

```
   76031  02004B78   <- consoleDemoInit
   76054  02004A08   <- consoleInit
   76073  02004B1A
   76079  020161B4   <- __getreent
   76092  02004B26   <- consoleInit, libnds source/arm9/console.c:504
   76098  0200F490   <- setvbuf
   ...
   77088  0200C3C0   <- mutexUnlock (calico source/system/mutex.c:143)
   77098  0200C3E8   <- mutexUnlock (mutex.c:154)
   77100  020166B4   <- __libc_lock_release_recursive
   77103  0200F5FE   <- setvbuf
   77110  0200F55C   <- pop {r1}
   77111  00000000   <- PC = 0
   77112  01FF8020
   77113  01FF853C   <- bkpt 0x8000  (OP_BKPT)
   77114  0000000C   <- then the prefetch-abort vector …
   77116  01FF8540   <- __arm_excpt_pabt
```

Read with `addr2line`/`nm` on the same ELF the chain is:

`main()` → `consoleDemoInit()` (`arm9v2/source/main.cpp:183`) → libnds
`consoleInit()` (`source/arm9/console.c:504`) → newlib `setvbuf()` →
`__libc_lock_release_recursive()` → calico `rmutexUnlock()` →
`mutexUnlock()` (`source/system/mutex.c:143`) → `threadRemoveWaiter()`
(`mutex.c:57`, `:82`) → back through `setvbuf` → `bx r1` with a **zero return
address**.

For the other rows the branch sites are, on the matching ELF:

| ROM | last real PC before 0 | symbol (`addr2line` on its own ELF) | LR at fault |
|---|---|---|---|
| scene2 | `0x0200F55C` | newlib `setvbuf` epilogue | `0x0200C3C0` `mutexUnlock` |
| scene3 | `0x0201C384` | not mapped in `.text` of the s3 ELF (`?? ??:0`), LR is `__libc_lock_release_recursive` | `0x020166B1` `__libc_lock_release_recursive` |
| cube2 | `0x01FF86A0` | inside `__arm_excpt_irq` (`0x01FF8604-0x01FF86E8`); SP `0x0380FE80` is **inside the DLDI region** `0x0380B000+0x4E80` | `0x02380074` (RAM, not a ROM address) |
| cube64/AF120 | `0x0200269A` | `twlhd_tex_capture_plan` epilogue (`common/twlhd_tex_capture.c:264`) | `0x02002767` `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:263`) |

**The stack was not recoverable by unwinding** (FACT, and a correction of a
first attempt): R13 at the fault points into a region that reads as all zeros,
both above and below SP (`TWLHD_BKPT_STACK` is 16 × `00000000` for all four
ROMs). The call chain above therefore comes from the PC trace, not from stack
words. That the return-address slot itself reads 0 is what `pop {r1}; bx r1`
with `r1=0` shows.

**Two distinct branch sites, not one:** scene2's zero return address is inside
newlib/calico startup code; cube64/AF120's is inside our own
`twlhd_tex_capture_plan` (at frame 129, after 120+ presented frames). They agree
only in *how* the fault becomes visible (branch to 0 → reset slot → `bkpt`),
not in *where* it starts. The contract's warning that this may be two faults is
borne out.

---

## 3. Method C — discriminators (real output)

### 3.1 SCENE=1 (works) vs SCENE=2 (faults): same startup code, different outcome

`SCENE=2` and `SCENE=1` are the same ROM except `demo_frame.cpp`
(`arm9v2/source/demo_frame.cpp:237-291`). Running both through the same
diagnostic CLI, the *code path up to and including the first two `setvbuf`
calls is identical* — a PC trace comparison shows the same addresses
(`0200F490`, `0200F4xx`, `020161B4`) at the same record indices:

```
second setvbuf entry s1: 76768  s2: 76757
 +  0  s1=0200F490  s2=0200F490
 +  7  s1=0200F49E  s2=0200F49E
 +  8  s1=020161B0  s2=0200F4A0     <- first divergence
```

- scene1: both `setvbuf` calls return normally
  (`… 0200F55A 0200F55C 02004B32 02004B34` at record 76760) and
  **no `PC==0` occurs anywhere in the first 120 000 records**; the run presents
  3D frames continuously (`TWLHD_SWAP n=6720 frame=6720` was the last
  instrumented submission) and writes `twlhd_frame_v2.bin`.
- scene2: the second call ends `0200F55A 0200F55C 00000000 01FF8020 01FF853C`
  at record 77 111.

So the fault is *not* a difference in which startup code runs — the same
`consoleInit`/`setvbuf` sequence runs in both. What differs is the value the
return-address slot yields. HYPOTHESIS: the difference is the stack content at
that point (the slot reads 0 for scene2, the real LR for scene1), which the
scene-specific binary layout can change; UNVERIFIED: which writer leaves the
zero there.

### 3.2 With vs without the MPCF/DLDI autopatch

Every combination was run through the same script and CLI. Fault counts are
runs that printed `ARM OP_BKPT`:

| ROM (from each run's `ROM crc` log line) | configuration | emulated ROM patched? | faults | note |
|---|---|---|---|---|
| scene2 `twlhd-arm9v2-autodump-s2.nds` (`F80EC60B`), scene3 (`45ABC2FF`), cube2 (`33A1AD3F`) | `--cflash-image` (documented path), no extra env | yes | **6/6** scene2 (`run-s2img`, `run-s2img2`, `run-rep-img1..3`, `f-s2b`), 1/1 scene3, 1/1 cube2; the **stock** CLI adds 2/2 (scene2), 2/2 (scene3), 2/2 (cube2) | identical PC/registers every run |
| scene2 autodump (`F80EC60B`) | `TWLHD_NODLDI=1 --cflash-image` | no | **0/4** | 98-101 swaps printed, no BKPT |
| scene2 autodump (`F80EC60B`) | `--cflash-path <dir>` (PassME 0x08, no patch) | no | **0/1** | 139 swaps, no BKPT, writes nothing |
| scene2 autodump (`F80EC60B`) | `TWLHD_SCRATCHPATCH=1 --cflash-image` (patch a throwaway copy) | no | **0/1** | 120 swaps, no BKPT |
| patched scene2 image (`8370E1B5`) | patched image fed **as the input file**, no Slot-2 device | n/a | **0/1** | 240 swaps, no BKPT |
| patched scene2 image (`8370E1B5`) | patched image + `--cflash-image` + `TWLHD_NODLDI=1` | bytes present, patch act not repeated | **0/1** | 240 swaps, no BKPT |
| scene2 autodump (`F80EC60B`) | unpatched ROM + `--cflash-image` + `TWLHD_NODLDI=1` | no | **0/1** | 239 swaps (control) |

The ROM column is a FACT read from the `ROM crc:` line DeSmuME prints in each
run's `desmume.log`: every unpatched/scene2 row logs `F80EC60B` (the scene2
autodump ROM, the same identification open item 6 makes), and the two
patched-image rows log `8370E1B5`, i.e. they fed the `TWLHD_DUMPROM` patched
copy as the input file, as the configuration column says. None of those runs
logged `AUTO-PATCHING DLDI`.

One scratch run left by the earlier attempt is **excluded** from these counts
because its configuration was not recorded: `run-pcB` (`ROM crc F80EC60B`, i.e.
the scene2 autodump ROM, `autopatch=1`) ran to 78 swap prints without a BKPT.
It is an unexplained counter-example unless its unrecorded variable was an
observer-effect knob (open item 6).

Two further FACTs from this table:

1. For scene2, the fault needs the **emulator's in-place patch of the ROM
   image**, not the patched bytes: the same bytes fed as the input file (or
   present with the patch disabled) do not fault.
2. The patch itself does not touch code. Diffing the emulator's patched ROM dump
   (`TWLHD_DUMPROM`) against the file on disk:

```
sizes 235520 235520
differing bytes: 1630
first: 0x4d65 last: 0x549f
dldi section file range: 0x4d58..0x8d58
```

All 1630 bytes are inside the ROM's `.dldi` section
(VMA `0x0380B000`, LMA `0x02004D58`, 16 KB — `arm-none-eabi-objdump -h`), i.e.
the region the crt0 copies to `0x0380B000`; no `.text`, `.crt0`, `.vectors` or
`.itcm` byte changes. DeSmuME performs this write at
`desmume/src/utils/dlditool.cpp:843` (`memcpy`) and `:891` (driver BSS
`memset`), called from `desmume/src/NDSSystem.cpp:823`.

**Why nothing is written under `--cflash-path`** (the open question in
`docs/EQ_SCENES.md`): with that flag DeSmuME selects `PassME (0x08)` and does
**not** run the autopatch — the logs contain no `AUTO-PATCHING DLDI` line, while
the `--cflash-image` logs do. The ROM's `.dldi` section is then still the empty
stub (`Old driver: Default (No interface)`, quoted from the patch log), so
`fatInitDefault()` (`arm9v2/source/main.cpp:137`) has no driver to mount.
HYPOTHESIS (code-level inference, not observed): it returns false, `dump_frame`
returns at that gate (`main.cpp:136-142`) and no file is ever opened — which is
also why that configuration cannot fault: the ROM never faults before the mount
point at all. UNVERIFIED: a direct print of `fatInitDefault()`'s result.

**Without the patch — scene3, CUBES=2 and CUBES=64 (FACT, real output, this
repair's runs, 2026-11-17).** The three ROMs were rebuilt with the documented
variables (`SCENE=3 AUTODUMP_FRAME=240`, `CUBES=2 AUTODUMP_FRAME=240`,
`CUBES=64 AUTODUMP_FRAME=120`; the CUBES=2 build's SHA-256
`703dcad6…` matches `tools/fixtures/eq_scenes/README.md:40`) and run with the
same method as the table above: `TWLHD_NODLDI=1`, the documented capture
script, the same scratch CLI, `DESMUME_TIMEOUT=30`. The script itself exits 1
because it requires the `AUTO-PATCHING DLDI` log line that `TWLHD_NODLDI=1`
suppresses; the measurement is the emulator log, and no run logged
`AUTO-PATCHING DLDI` (the same shape as the `0/4` row above).

| ROM (rebuilt, `ROM crc` in log) | `ARM OP_BKPT` | swap prints | outcome |
|---|---|---|---|
| scene3 autodump (`45ABC2FF`) | 0 | 0 | no fault in 30 s, but no 3D frame presented either (scene3 presents none in the harness too — the scene3 row in section 3 of `docs/EQ_SCENES.md`) |
| CUBES=2 autodump (`33A1AD3F`) | 0 | to `n=30120` (frame 30120) | no fault in 30 s; frames present continuously |
| CUBES=64 autodump (`1B79DB9D`) | 0 | to `n=480` (frame 490) | **still fails, differently**: at ~frame 490 the log shows `ARM9: Undefined instruction: 0x3C000000 PC=0x0500049C`; no swap print after it, while the `TWLHD_FRAME` counter runs on to 10620. Reproduced in a second run at the same PC. Cause UNVERIFIED. |

So the "autopatch required" relation is measured for scene2 (the discriminator
rows) and is consistent with CUBES=2 (faults 1/1 with the patch, no fault
without it in 30 s); scene3 does not fault without the patch in 30 s but also
presents no frame; CUBES=64 without the patch does **not** reproduce the
`0x01FF853C`/`twlhd_tex_capture_plan:263-264` fault, it reaches a different
wild jump later.

### 3.3 The non-autodump variant, in the harness

`make -C arm9v2 SCENE=2` (no `AUTODUMP_FRAME`, so no `fatInitDefault` and no
file I/O at all) → `arm9v2/twlhd-arm9v2-s2.nds`, SHA-256
`479792983437417d8e95fb97e5c872ddef0c97cf3dd2839917c977cceb0564ba`:

```
$ /home/one/3ds/twlhd-desmume-harness/harness arm9v2/twlhd-arm9v2-s2.nds 242 --out … --ppm …
rc=0
format geometry-dump 1
emulator desmume fc504ee0
rom-sha256 47979298…
requested-frame 242
captured-frame 242
polygon-count 10
vertex-count 30
viewport-mode per-polygon
```

FACT: scene2's scene code, geometry and submission work; the ROM presents frame
242 and its geometry dump parses. The same ROM through the capture script
(`--cflash-image`, no autodump code, but the emulator still autopatches) also
runs to 108 swap prints without a BKPT. So neither `demo_frame`'s scene-2 path
nor the geometry/write-less path is implicated for this ROM; scene2's fault is
reached only in the autodump configuration **and** only when the emulator patches
the ROM.

### 3.4 The CUBES=64 AF=120 analogue

Rebuilt as `make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64` and run through the
diagnostic CLI. It faults too — but the comparison changes the picture
(details in 2.1/2.2): frame **129** (the autodump frame 120 *was* reached and
120+ frames were presented), LR `0x02002767` =
`twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:263`), branch site
`0x0200269A` = the `pop {pc}` epilogue of `twlhd_tex_capture_plan`
(`common/twlhd_tex_capture.c:264`). So the record
"aborted before the autodump frame" (`docs/EMULATOR_VALIDATION.md:286`) does not
describe this run: the fault is *after* the dump frame, in the texture-capture
plan, and is a different branch site from scene2's.

---

## 4. Method D — attribution

**FACT (EMULATOR, measured).**

1. The message `ARM OP_BKPT triggered` is DeSmuME's `OP_BKPT`
   (`desmume/src/arm_instructions.cpp:6335`), reached at PC `0x01FF853C`
   (`__arm_excpt_rst`, `calico-1.2.0/source/nds/arm9/excpt_handler.32.s:27`,
   instruction `bkpt 0x8000`) in all four failing ROMs, with the frame counters
   0 / 0 / 6 / 129 and `SWAP_BUFFERS` never reached for scene2, scene3, cube2.
2. The CPU gets there by executing a **branch to address 0** — measured as the
   last real PC in the 16-entry ring and in a 4 M-record PC trace
   (`0200F55C → 00000000 → 01FF8020 → 01FF853C`). No ARM7 involvement.
3. For scene2 the branch is the `pop {r1}; bx r1` epilogue of newlib `setvbuf`
   with `r1 = 0`, called from libnds `consoleInit` (`source/arm9/console.c:504`)
   via our `consoleDemoInit()` at `arm9v2/source/main.cpp:183`; the chain runs
   through calico's recursive lock release (`mutex.c:143`, `:57`, `:82`).
4. For the scene2 autodump ROM, the failure correlates with the **emulator's
   DLDI autopatch act** (it faults in all six plain runs with the patch, none of
   the nine runs without it — one scratch run, `run-pcB`, is an unexplained
   counter-example, section 3.2), not with the patched bytes (0/2 with the same
   bytes present but the patch not performed) and not with the scene code alone
   (the non-autodump scene-2 ROM presents frame 242 in the harness). scene3 and
   CUBES=2/64 were not part of that discriminator; section 3.2 gives this
   repair's patch-free runs for them (CUBES=2: no fault in 30 s; scene3: no
   fault, no frame; CUBES=64: no `0x01FF853C` fault, a different wild jump at
   `PC=0x0500049C`).
5. `setvbuf`'s return-address slot reads 0, and the 16 words around SP read 0,
   so the return address is not recoverable from the stack.

**Not our ROM code's write path for the scene2 row (FACT for the tested
configurations):** no recorder capacity is near a limit (unchanged from the
existing record, `common/twlhd_gx_record.h:30,41-45`), the non-autodump scene2
ROM presents frame 242, and scene2's fault happens before any 3D frame, before
`dump_frame`'s frame 240 trigger (`arm9v2/source/main.cpp:253-259`) and before
any DLDI-region code executes (0 executions in
`0x03800000-0x03810000` in the first 120 000 records of the scene2 trace). The
scene3 and cube2 branch sites are **UNVERIFIED** (open items 3-4): scene3's site
`0x0201C384` is unmapped in the s3 ELF (`?? ??:0`, section 2.2); cube2's branch
is inside calico `__arm_excpt_irq` with LR `0x02380074` an unmapped RAM address
and SP `0x0380FE80` inside the DLDI region (section 2.2), and an IRQ dispatcher
branching to 0 could be a NULL handler that our code registered. For those two
rows our code is neither implicated nor excluded.

**CUBES=64/AF120 is different: its branch site IS in our code (FACT, cause
UNRESOLVED).** For that row the ring's last real PC `0x0200269A` maps by
`addr2line` on the CUBES=64 ELF to `common/twlhd_tex_capture.c:264` (the
`pop {pc}` epilogue of `twlhd_tex_capture_plan`), with LR `0x02002767` in
`twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:263`) (section 2.2). The cause of that branch is **UNRESOLVED**; nothing below scopes
it out, and it is the one failing row that may be a real ROM defect that could
also occur on hardware.

**HYPOTHESIS — the CUBES=64/AF120 mechanism (not measured, could be a ROM
defect).** `twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:144`) receives
the `read` callback (`TwlhdTexCapReadFn`, `common/twlhd_tex_capture.h:65`) and
passes it to `cap_classify_key` (`common/twlhd_tex_capture.c:223-225`), which
invokes it indirectly at `:119`, `:127` and `:136`; a NULL or corrupted function
pointer there would branch to address 0 exactly as observed. The function guards
its own pointers on entry at `:163` (`if (!limits || !gxcm || !read || !out)
return TWLHD_TEX_CAP_ERR_NULL;`), so this hypothesis needs a pointer or return
address corrupted *after* that check. The `:208`/`:215` attributions in the
original version of this report were an earlier wrong attribution: a fresh
rebuild maps `0x0200269A` to `common/twlhd_tex_capture.c:264` and
`0x02002766`/`0x02002767` to `:263`, and disassembles the instruction at
`0x0200269A` as `pop {pc}` — a function return from a zeroed saved LR, not an
indirect call — see `docs/reports/cubes64-branch0-diagnosis.md` section 1. The
mechanism is therefore HYPOTHESIS/UNVERIFIED, not a measured cause.

**HYPOTHESIS (not proven).**

- That the zero return address is a *stack-content* artefact (a slot that is
  zero in scene2's boot and holds the real LR in scene1's), rather than a
  corrupted SP: SP is a plausible 0x02FF3E18 (DTCM) in scene2 and the trace
  shows well-formed call/return pairs around it.
- That, for scene2, the causal link from "DeSmuME wrote the DLDI section in
  place" to "a guest stack slot reads 0" is an emulator load-time/ordering effect
  rather than a ROM bug: the bytes that change are only the 16 KB `.dldi` copy,
  and the same bytes in the same place do not reproduce it. The mechanism is
  **UNVERIFIED**.
- That `--cflash-path` writes nothing because `fatInitDefault()` fails without a
  driver (section 3.2).

**Emulator behaviour that differs from hardware (FACT about the model):**
DeSmuME emulates `bkpt` by entering abort mode and continuing
(`desmume/src/arm_instructions.cpp:6335-6350`); the next fetch at `0x0C`
then prefetch-aborts, so the model ends up spinning in the exception path
instead of the ROM being visibly stopped. The emulator keeps running (the log
reaches frame 1620 after the BKPT). On real hardware `bkpt` is likewise
"branch to debugger" (nothing is running under a debugger), so the ROM would
not have a working capture either — but the *reason* it reaches `bkpt` there is
unknown.

**Real hardware (HYPOTHESIS, not run):** for the scene2/scene3/cube2 rows the
same BKPT is **not** expected on a real DS, because the faulting step is a
branch to address 0 in code that the hardware executes from a different memory
state, and those faults need DeSmuME's autopatch act. That expectation does not
cover CUBES=64/AF120: its branch is inside our own
`twlhd_tex_capture_plan`, so a hardware occurrence of that row cannot be
excluded (HYPOTHESIS; the mechanism is in section 4). The experiment that would
settle it: run the scene2 autodump ROM from the flashcard on hardware and watch
the bottom-screen frame counter and the SD/flash write — a written
`twlhd_frame_v2.bin` would prove the fault is emulator-only. That experiment was
**not** run (out of scope: no console/SD action).

---

## 5. Method E — recommendation (not implemented)

Ranked by size. All are for a later stage; none is applied here.

1. **Capture-side, smallest (harness/flow stage):** for scene2 and cube2, take
   the reference dump from the harness (already committed, frame 242) and stop
   the DeSmuME-v2 capture from being the blocking path until the cause is
   settled — i.e. keep reporting NOT-COMPARABLE with this report as the reason.
   Zero code, no risk, but leaves no our-side stream.
2. **Diagnosis-stage (recommended next):** finish the causal link with one more
   scratch experiment — set a host-side watchpoint on the guest address that
   supplies `setvbuf`'s return slot (`SP` at the `bx`, `0x02FF3E14` in scene2's
   trace) and on `gameInfo.romdataForReader` around `NDSSystem.cpp:823`, and
   repeat scene1/scene2 with `TWLHD_PCLOG`. This distinguishes "the patch act
   perturbs host state" from "something in the guest writes the slot", and it is
   a scratch-tree change only.
3. **Emulator-side, only if 2 lands on the emulator (harness/DeSmuME stage):**
   make the CLI's autopatch not run for capture builds (a `--no-dldi-patch`
   flag or an upstream patch), which is behaviour-equivalent to the already
   working `--cflash-path`/`TWLHD_NODLDI` runs — but then no `fatInitDefault`
   either, so the autodump ROM cannot write; the real capture fix would be to
   have the ROM write through a path that neither needs the emulator's patched
   DLDI driver nor a Slot-2 card.
4. **Do not** add a BKPT/assert workaround in our ROM code. For the scene2 row
   the measurements implicate nothing in `arm9v2/`, `common/` or `tools/`
   (section 4); the scene3 and cube2 sites are not attributed (UNVERIFIED,
   section 4, open items 3-4). For CUBES=64/AF120 the implicated file
   **is** `common/twlhd_tex_capture.c` (branch site `:208`, LR in
   `twlhd_tex_capture_plan` at `:215`) and its cause is UNRESOLVED, so settle it
   with the watchpoint path in item 2 before changing anything; the
   NULL/corrupted `read` callback (`common/twlhd_tex_capture.h:65`, called at
   `common/twlhd_tex_capture.c:119,127,136`) is a HYPOTHESIS only.

---

## 6. Open items

1. **UNVERIFIED — why the return-address slot holds 0 for scene2 but the real LR
   for scene1.** The slot address is known from the trace (`SP` at the
   `pop {r1}`); a watchpoint run (recommendation 2) would settle it.
2. **UNVERIFIED — the causal step from DeSmuME's in-place `.dldi` write to that
   slot.** The patch act is required for scene2 in the tested configurations
   (6/6 vs 0/9; open item 6 for `run-pcB`, the one exception) and its byte
   footprint is confined to the DLDI section (1630 bytes, measured), so the link
   is not through ROM content.
3. **UNVERIFIED — scene3.** Its branch site `0x0201C384` is not mapped in the
   s3 ELF's `.text` and its LR is `__libc_lock_release_recursive`; it is the
   same "branch to 0" shape as scene2 but a different site. Treat scene3 as a
   separate row until measured the same way.
4. **UNVERIFIED — cube2.** Its LR (`0x02380074`) is a RAM address and its SP is
   inside the DLDI region (`0x0380FE80`), so this row already has DLDI code on
   the stack — it is not the same shape as scene2 and needs its own
   measurement.
5. **FACT that contradicts the existing record:**
   `docs/EMULATOR_VALIDATION.md:286` describes the CUBES=64 AF=120 failure as
   "control reaching `0x01ff853c`"; the measured run reaches the autodump frame
   and faults at frame 129 from `twlhd_tex_capture_plan`
   (`common/twlhd_tex_capture.c:264`). That document
   was not edited by this task (out of scope).
6. **Observer effect / one unexplained run.** In these runs the fault is
   reproducible to the instruction (identical registers and ring in six runs,
   including the 4 M-record `TWLHD_PCLOG` run), unlike the gdb record where
   extra breakpoints suppressed the fault. The single exception is `run-pcB`
   (scene2 ROM, autopatch on, 78 swap prints, no BKPT) imported from the
   earlier attempt, whose unrecorded extra variable is unknown; it is not
   counted above and it is not explained here.

---

## CAUSE

**FACT:** under DeSmuME every failing ROM executes a branch to address `0` and
lands in the ITCM reset slot `0x01FF853C` (`__arm_excpt_rst`, `bkpt 0x8000`),
which is why the capture log ends with `ARM OP_BKPT triggered` — scene2 at frame
0 from newlib `setvbuf`'s `bx r1` with `r1=0` (`0x0200F55C`, called by libnds
`consoleInit` in the boot path), scene3 at frame 0 from a different site, cube2
at frame 6 with SP inside the DLDI region, cube64/AF120 at frame 129 from
`twlhd_tex_capture_plan` (`common/twlhd_tex_capture.c:264`); scene2, scene3 and cube2 never present a 3D
frame at all, and, **for the scene2 autodump ROM in the tested configurations**,
the fault requires DeSmuME's in-place DLDI autopatch of the ROM image (6/6 with
it, 0/9 without); that is the only ROM the with/without discriminator ran on, and
the scratch run `run-pcB` (scene2, autopatch on, 78 swap prints, no BKPT,
unrecorded extra variable; open item 6) is an unexplained counter-example to the
blanket claim. scene3 and CUBES=2 were **not part of the original with/without
discriminator** (with the patch: 1/1 each, plus stock 2/2 each; CUBES=64 with the
patch in one run, the AF120 analogue); this repair's patch-free runs (section
3.2) show CUBES=2 faulting only with the
patch, scene3 not faulting but also presenting no frame,
and CUBES=64 not reaching the `0x01FF853C` fault at all — it wild-jumps to
`PC=0x0500049C` at ~frame 490 instead. The patched bytes alone do not reproduce
the scene2 fault.
**HYPOTHESIS:** for scene2/scene3 the zero return address is a stack-content
artefact of the patched-emulator boot, not a ROM-code defect; for
CUBES=64/AF120 the branch site is in our own `twlhd_tex_capture_plan`
(`common/twlhd_tex_capture.c:264`, LR `:263`), where a NULL/corrupted `read`
callback (`common/twlhd_tex_capture.h:65`, called at `:119,127,136`) is the
HYPOTHESIS, so that row could be a real ROM defect. **UNVERIFIED:** the mechanism
that turns the emulator's `.dldi` write into that zero (scene2), the CUBES=64
mechanism, and therefore whether real hardware would fault (expected: **no** for
scene2/scene3/cube2, HYPOTHESIS; not excluded for CUBES=64).

---

## Requirement traceability (docs repair, 2026-11-17)

Docs-only repair of this report and `docs/EQ_SCENES.md`; no code, test, ROM
source, fixture or emulator file changed. Line numbers are in the repaired
files. `TESTS: make test` for every item; the docs are not parsed by the tests,
so the check is that nothing regressed.

- **REQ-1 (hypothesis-as-fact, `docs/EQ_SCENES.md`).** Before, the scene2
  paragraph ended "…and that the ROM's own bytes are not sufficient, so the
  remaining gap is emulator-side."; after, `docs/EQ_SCENES.md:222` reads
  "**HYPOTHESIS: emulator-side** (see the report); the cause is not
  established." The measured facts stay in the same block (`0x01FF853C`, the
  reset slot `__arm_excpt_rst` / `bkpt 0x8000`, the branch to address 0 from
  `setvbuf`, frame 0 — `docs/EQ_SCENES.md:186-193`, `:212-215`), and the
  "reproduces only with it" clause at `:190-193` is scoped to this scene2 ROM in
  the tested configurations, with `run-pcB` named.
- **REQ-2 (autopatch claim applied to every row, `docs/EQ_SCENES.md`).** Before,
  the section 5 bullet said the emulator's in-place DLDI auto-patch "is required
  for it" for the scene2/scene3/cubes rows. After, `docs/EQ_SCENES.md:308-326`
  limits the measured requirement to scene2 in the tested configurations
  (`:319-320`), names `run-pcB` as the unexplained counter-example (`:320-321`),
  states that the CUBES=64 AF=120 branch site is in our code (`:316-318`), and
  reports this repair's patch-free results (`:321-326`; the runs themselves in
  section 3.2 of this report, `:364-386`).
- **REQ-3 (the "not our ROM code" claim contradicts the measured CUBES=64
  site).** Before, section 4 said "**Not our ROM code's write path (FACT for the
  tested configurations)**" and section 5 item 4 said "nothing in `arm9v2/`,
  `common/` or `tools/` is implicated". After, the section 4 heading is scoped to
  the scene2 row (`:458-470`), a separate paragraph states that the
  CUBES=64/AF120 branch site `0x0200269A` is `common/twlhd_tex_capture.c:208`
  with LR in `twlhd_tex_capture_plan` (`:215`) and that its cause is
  **UNRESOLVED** (`:472-478`), the NULL/corrupted `read` callback mechanism is
  HYPOTHESIS only with the `:119,127,136` / `:163` code lines and the
  addr2line-vs-current-source caveat (`:480-492`), and §5 item 4 carries the
  same scope (`:555-563`).
- **REQ-4 (CAUSE overclaim and unnamed ROMs in the 3.2 table).** Before, the
  CAUSE FACT said "the fault requires DeSmuME's in-place DLDI autopatch of the
  ROM image (6/6 with it, 0/9 without)" for every failing ROM, and the 3.2 table
  named no ROM for the 0/4 and 0/1 rows. After, the table has a ROM column with
  the `ROM crc` of every row (`:308-316`) plus the evidence paragraph
  (`:318-323`), FACT 1 is scoped to scene2 (`:333-335`), §4 FACT 4 is scoped with
  `run-pcB` (`:445-453`), and the CAUSE FACT restricts the requirement to the
  scene2 autodump ROM, names `run-pcB` and the other rows' status
  (`:609-621`).
- **REQ-5 (typo).** Before, `:182` wrote the CPU name with a `C` where the `M`
  belongs (`ARC` + `7`); after it reads "ARM7 is never involved: `cpu=ARM9` in
  every row."
- **REQ-6 (optional live patch-free runs — performed, real output).** The
  scene3, CUBES=2 and CUBES=64 autodump ROMs were rebuilt with the documented
  variables (CUBES=2 SHA-256 `703dcad6…` matches
  `tools/fixtures/eq_scenes/README.md:40`) and run with `TWLHD_NODLDI=1`, the
  documented capture script and the same scratch CLI, `DESMUME_TIMEOUT=30`; the
  per-ROM results and the script's exit-1-on-missing-`AUTO-PATCHING` caveat are
  in section 3.2 (`:364-386`). No result was invented; nothing was run on
  hardware.
- **REQ-7 (third repair: "not our code" overclaims for scene3 and cube2).** Four
  defects fixed, docs only, every measured fact kept (PCs, LRs, run counts,
  SHA-256s). (a) §4 heading and paragraph: before, "Not our ROM code's write path
  for the scene2, scene3 and cube2 rows (FACT…)" and "…an unmapped s3 site, and
  `__arm_excpt_irq` … (section 2.2), none of them our code either"; after, the
  FACT covers the scene2 row only (`:458-470`), the 0-DLDI-executions fact is
  scoped to the scene2 trace, and scene3/cube2 are stated **UNVERIFIED** with
  the unmapped s3 site `0x0201C384`, the calico `__arm_excpt_irq` context, the
  unmapped RAM LR `0x02380074`, and the possible NULL-handler registration, so
  our code is "neither implicated nor excluded". (b) §5 item 4: before, the
  measurements "implicate nothing" for "the scene2, scene3 and cube2 rows";
  after, only scene2 is attributed and scene3/cube2 are "not attributed
  (UNVERIFIED)" (`:555-563`). (c) `docs/EQ_SCENES.md:314-316`: before, the
  branch sites for scene2/scene3/CUBES=2 were "newlib/calico startup, an unmapped
  site and the calico exception handler — not our code"; after, only scene2's
  site (newlib `setvbuf`) is stated as outside our code and scene3/CUBES=2 read
  "not attributed (UNVERIFIED, see the report's open items 3-4)". (d) CAUSE
  paragraph (`:614-618`): before, "scene3 and CUBES=2 were measured with the
  patch only (1/1 each, plus stock 2/2 each)", which contradicted the patch-free
  runs cited next; after, they "were **not part of the original with/without
  discriminator**" with the 1/1 and 2/2 counts and the CUBES=64 AF120 analogue
  kept as measured facts, deferring to section 3.2 (`:364-386`) for the
  patch-free results. Labels follow the policy at `:10-13`; the consistency
  anchor is open items 3-4 (`:577-584`), which already said UNVERIFIED for
  scene3/cube2. Section 2.2 (`:246-253`) and section 3.2 (`:364-386`) were not
  edited. Line refs in REQ-2/3/4 were re-derived after the edit. `TESTS: make
  test` — the docs are not parsed by the tests, so the check is no regression.

- **REQ-8 (CUBES=64/AF120 line-number correction, 2026-11-18).** This is a
  follow-up docs repair of `docs/reports/cubes64-branch0-diagnosis.md`,
  `docs/EQ_SCENES.md` and this report. The Opus review found that the `:208`/
  `:215` attributions for the CUBES=64 AF=120 branch site/LR were inconsistent
  with a fresh rebuild: `arm-none-eabi-addr2line -f -i -e
  arm9v2/twlhd-arm9v2-autodump-c64.elf 0x0200269A 0x02002766 0x02002767` maps
  `0x0200269A` to `common/twlhd_tex_capture.c:264` and `0x02002766`/`0x02002767`
  to `:263`. The corrected references are: `docs/reports/bkpt-diagnosis.md:253`
  (table row), `:385`, `:421-422`, `:474-476`, `:589-591` (open item 5),
  `:611-612`, `:628-629` (CAUSE); `docs/EQ_SCENES.md:279-280` and `:318-319`;
  `docs/reports/cubes64-branch0-diagnosis.md` section 1 (source lines + mismatch
  explanation) and the new requirement traceability section at the end of that
  file. The ring evidence is also reconciled: the disassembly shows the `bl` at
  `0x02002762` calling `twlhd_tex_capture_layout`, and the ring shows the return
  to `0x02002766` (LR `0x02002767`), the `b.n` to the shared epilogue, and the
  `pop {pc}` at `0x0200269A` loading a saved LR that was already `0`. The 16-entry
  ring window does not show whether the planner ran its full body and is not
  evidence against "saved LR overwritten later". No code, test, Makefile, tool or fixture was changed. `TESTS: make
  test` — no regression.
