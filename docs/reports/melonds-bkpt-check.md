# melonDS cross-check: does the ARM9 bad-PC failure seen in DeSmuME also occur in melonDS?

**Review status (2026-11-22):** this report went through four Opus review rounds. The last verdict (round 3 of the repair series) was FAIL, only on the consistent use of the two PC-sample conventions (watch sample = executing address + 4 per the quoted melonDS source; frame-boundary PC9 read after RunFrame()), so decodings of the 0x01FF853C / 0x01FF8538 samples and of the frame-boundary PC9 must be treated as HYPOTHESIS. The measured outputs themselves are unchanged. The owner approved merging this state.

*(Round 4 revision: the relation between the sampled `R[15]` and the
executing address is now derived from the melonDS interpreter loop and
`ARMv5::JumpTo` (`src/ARM.cpp`).  Under that source-derived convention the
sampled value is the executing instruction address plus 4; the old
"`R[15]=PC+8`" parenthetical and the "pipeline PC of `armWaitForIrq`'s
wait-for-IRQ `mcr` at `0x01FF8534`" decoding are relabelled HYPOTHESIS where
they are not directly supported by the quoted source.)*

**Scope.** This is an EMULATOR/DOC investigation only.  No product source was changed.
The only file added in this repo is the present report.  All scratch work
(drivers, instrumented melonDS build) lives under `$HOME/3ds/hwtest-staging/`.
The melonDS checkout at `$HOME/3ds/melonDS` was left read-only and verified
empty (`git status --porcelain` before and after, quoted in §REQ-6).  The
DeSmuME and harness repos were not touched.

**Labels.**  `FACT` = full command and raw complete output quoted;
`HYPOTHESIS` = logical inference, not direct observation;
`UNVERIFIED` = could not be established this session.

---

## 1. ROM builds and byte identity (REQ-1)

All four ROMs were rebuilt with the documented recipes.  `make` reported each
`.nds` up-to-date (the bytes were already present from the previous run), and
the SHA-256 / CRC below were re-verified after the recipe invocation.

```sh
$ make -C arm9v2 SCENE=2 AUTODUMP_FRAME=240
make: Entering directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'
make[1]: '/tmp/codex-20261119-141250-2752169/worktree/arm9v2/twlhd-arm9v2-autodump-s2.nds' is up to date.
make: Leaving directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'

$ make -C arm9v2 SCENE=3 AUTODUMP_FRAME=240
make: Entering directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'
make[1]: '/tmp/codex-20261119-141250-2752169/worktree/arm9v2/twlhd-arm9v2-autodump-s3.nds' is up to date.
make: Leaving directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'

$ make -C arm9v2 CUBES=2 AUTODUMP_FRAME=240
make: Entering directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'
make[1]: '/tmp/codex-20261119-141250-2752169/worktree/arm9v2/twlhd-arm9v2-autodump-c2.nds' is up to date.
make: Leaving directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'

$ make -C arm9v2 AUTODUMP_FRAME=120 CUBES=64
make: Entering directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'
make[1]: '/tmp/codex-20261119-141250-2752169/worktree/arm9v2/twlhd-arm9v2-autodump-c64.nds' is up to date.
make: Leaving directory '/tmp/codex-20261119-141250-2752169/worktree/arm9v2'
```

```sh
$ cd arm9v2 && for f in twlhd-arm9v2-autodump-s2.nds twlhd-arm9v2-autodump-s3.nds \
  twlhd-arm9v2-autodump-c2.nds twlhd-arm9v2-autodump-c64.nds; do
  echo "=== $f ==="; sha256sum "$f"
  python3 -c "import zlib; data=open('$f','rb').read(); print('ROM crc: %08X' % (zlib.crc32(data) & 0xffffffff))"
done
=== twlhd-arm9v2-autodump-s2.nds ===
d84f210b6f92c6a5c7a35c76dcfcaa0a142524ae578f974812cb6ae04a4097ed  twlhd-arm9v2-autodump-s2.nds
ROM crc: F80EC60B
=== twlhd-arm9v2-autodump-s3.nds ===
15a5fc915a88d2c1c0622f536f5db74ad604778c99975432a8fda2ac92b63fa4  twlhd-arm9v2-autodump-s3.nds
ROM crc: 45ABC2FF
=== twlhd-arm9v2-autodump-c2.nds ===
703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15  twlhd-arm9v2-autodump-c2.nds
ROM crc: 33A1AD3F
=== twlhd-arm9v2-autodump-c64.nds ===
3ff78ee8575f81063810c759df86be871e5a6e29578727d7fe75d16fd8073275  twlhd-arm9v2-autodump-c64.nds
ROM crc: 1B79DB9D
```

**FACT comparison with earlier reports:**

| ROM | SHA-256 this run | Fixture / earlier SHA-256 (`tools/fixtures/eq_scenes/README.md:36-40`, `bkpt-diagnosis.md`) | Match |
|---|---|---|---|
| scene2 | `d84f210b6f92c6a5c7a35c76dcfcaa0a142524ae578f974812cb6ae04a4097ed` | `d84f210b6f92c6a5c7a35c76dcfcaa0a142524ae578f974812cb6ae04a4097ed` | **yes** |
| scene3 | `15a5fc915a88d2c1c0622f536f5db74ad604778c99975432a8fda2ac92b63fa4` | no fixture recorded | n/a |
| cubes2 | `703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15` | `703dcad675871fb7d67d417ff5f2634577c7445f76416761c58ee03f8c582d15` | **yes** |
| cubes64 | `3ff78ee8575f81063810c759df86be871e5a6e29578727d7fe75d16fd8073275` | no fixture recorded | n/a |

| ROM | CRC this run | Earlier CRC (`bkpt-diagnosis.md`, `cubes64-irq-return-address.md`) | Match |
|---|---|---|---|
| scene2 | `F80EC60B` | `F80EC60B` | **yes** |
| scene3 | `45ABC2FF` | `45ABC2FF` | **yes** |
| cubes2 | `33A1AD3F` | `33A1AD3F` | **yes** |
| cubes64 | `1B79DB9D` | `1B79DB9D` | **yes** |

The bytes are byte-identical to the DeSmuME reference ROMs where a reference
exists.

---

## 2. melonDS driver and bad-PC watch (REQ-2)

### 2.1 Reused / extended scratch build

**FACT.** The existing out-of-tree melonDS build at
`$HOME/3ds/hwtest-staging/melonds-build/` was reused and extended:

* the scratch source copy `$HOME/3ds/hwtest-staging/melonds-src-check/` (a
  copy of the melonDS checkout) already contained a per-instruction ARM9 PC
  watch in `src/ARM.cpp`;
* this session changed the watch to use architectural `R[15]` directly (so it
  matches the DeSmuME `pc=` convention and the documented ranges), and added an
  optional `TWLHD_RTC_EPOCH` environment-variable pin to the headless driver
  (`bkpt_driver.cpp`) so CUBES=64 could be run at the DeSmuME faulting epochs;
* the instrumented `bkpt_driver` was rebuilt, and a no-watch variant
  (`TWLHD_WATCH=0`) was produced from the same scratch source.

The driver constraints from `docs/reports/melonds-scene5-check.md` were kept:
`NDSArgs::JIT = std::nullopt` (interpreter only), heap-allocated `NDS`,
FreeBIOS direct boot via `NDS::SetupDirectBoot`, software renderer, no
limiter/throttling (`RunFrame()` loop).

**HYPOTHESIS.** The headless driver used here does **not** configure DLDI /
cflash / MPCF, because the scratch driver calls `NDS::SetupDirectBoot` and
never loads a DLDI image or creates a `CartSD`/`CartHomebrew` instance.  The
pinned melonDS checkout contains DLDI code in both the Qt frontend and the
NDS-cart core, so "FAT paths live only in the Qt frontend" is wrong:

```sh
$ cd $HOME/3ds/melonDS/src && grep -rlni 'dldi\|cflash\|mpcf' . | sort
./frontend/qt_sdl/Config.cpp
./frontend/qt_sdl/EmuInstance.cpp
./frontend/qt_sdl/EmuSettingsDialog.cpp
./frontend/qt_sdl/EmuSettingsDialog.h
./frontend/qt_sdl/EmuSettingsDialog.ui
./NDSCart/CartHomebrew.cpp
./NDSCart/CartHomebrew.h
./NDSCart/CartSD.cpp
./NDSCart/CartSD.h
./NDSCart/melonDLDI.h
$ grep -rni 'dldi\|cflash\|mpcf' . | wc -l
118
$ grep -ni 'dldi\|cflash\|mpcf' ./NDSCart/CartSD.cpp
24:// provides support for DLDI patching
52:void CartSD::ApplyDLDIPatchAt(u8* binary, u32 dldioffset, const u8* patch, u32 patchlen, bool readonly) const
54:    if (patch[0x0D] > binary[dldioffset+0x0F])
56:        Log(LogLevel::Error, "DLDI driver ain't gonna fit, sorry\n");
60:    Log(LogLevel::Info, "existing driver is: %s\n", &binary[dldioffset+0x10]);
63:    u32 memaddr = *(u32*)&binary[dldioffset+0x40];
65:        memaddr = *(u32*)&binary[dldioffset+0x68] - 0x80;
73:    memcpy(&binary[dldioffset], patch, patchlen);
75:    *(u32*)&binary[dldioffset+0x40] += delta;
76:    *(u32*)&binary[dldioffset+0x44] += delta;
77:    *(u32*)&binary[dldioffset+0x48] += delta;
78:    *(u32*)&binary[dldioffset+0x4C] += delta;
79:    *(u32*)&binary[dldioffset+0x50] += delta;
80:    *(u32*)&binary[dldioffset+0x54] += delta;
81:    *(u32*)&binary[dldioffset+0x58] += delta;
82:    *(u32*)&binary[dldioffset+0x5C] += delta;
84:    *(u32*)&binary[dldioffset+0x68] += delta;
85:    *(u32*)&binary[dldioffset+0x6C] += delta;
86:    *(u32*)&binary[dldioffset+0x70] += delta;
87:    *(u32*)&binary[dldioffset+0x74] += delta;
88:    *(u32*)&binary[dldioffset+0x78] += delta;
89:    *(u32*)&binary[dldioffset+0x7C] += delta;
100:            u32 val = *(u32*)&binary[dldioffset+addr];
102:                *(u32*)&binary[dldioffset+addr] += delta;
112:            u32 val = *(u32*)&binary[dldioffset+addr];
114:                *(u32*)&binary[dldioffset+addr] += delta;
124:            u32 val = *(u32*)&binary[dldioffset+addr];
126:                *(u32*)&binary[dldioffset+addr] += delta;
134:        memset(&binary[dldioffset+fixstart], 0, fixend-fixstart);
140:        binary[dldioffset+0x64] &= ~0x02;
143:        u32 writesec_addr = *(u32*)&binary[dldioffset+0x74];
145:        writesec_addr += dldioffset;
150:    Log(LogLevel::Debug, "applied DLDI patch at %08X\n", dldioffset);
153:void CartSD::ApplyDLDIPatch(const u8* patch, u32 patchlen, bool readonly)
159:        Log(LogLevel::Error, "bad DLDI patch\n");
174:            Log(LogLevel::Debug, "DLDI structure found at %08X (%08X)\n", i, offset+i);
175:            ApplyDLDIPatchAt(binary, i, patch, patchlen, readonly);
$ grep -ni 'dldi\|cflash\|mpcf' ./NDSCart/CartHomebrew.cpp
21:#include "melonDLDI.h"
24:// implements simple SD card access commands, which are used by the melonDS DLDI driver
50:        ApplyDLDIPatch(melonDLDI, sizeof(melonDLDI), SD->IsReadOnly());
$ grep -ni 'dldi\|cflash\|mpcf' ./NDSCart/melonDLDI.h
19:#ifndef MELONDLDI_H
20:#define MELONDLDI_H
26:const u8 melonDLDI[] =
66:#endif // MELONDLDI_H
```

Because the headless driver does not exercise DLDI, the autodump
`twlhd_frame_v2.bin` output **cannot** prove progress in these runs; progress
is judged solely from `GPU3D::NumPolygons` / `NumVertices` and the frame
counter printed by the driver.  Whether the scene2/scene3 faults are genuinely
FAT-independent is HYPOTHESIS (the ROM does not reach a DLDI write in the
observed stuck state).  The CUBES=64 DeSmuME fault was RTC- and
cflash-coupled, so the missing FAT path is an explicit caveat for that ROM.

### 2.2 Bad-PC watch definition

The instrumented scratch build prints ARM9 `R[15]` (logged as `PC9`), `LR`,
`SP`, `CPSR` and GPU3D state at every frame boundary, and a per-instruction
watch in `src/ARM.cpp` that fires on the first `R[15]` matching:

* `PC == 0x00000000`,
* `PC` in `0x01FF8000..0x01FF8040`, or
* `PC == 0x01FF853C`.

It also keeps a 64-entry chronological ring of `{pc, sp, cpsr, lr}` and prints
it when the watch fires.  A no-watch variant of the same `bkpt_driver` binary
was produced by setting `TWLHD_WATCH=0` (the per-instruction call in
`src/ARM.cpp` is skipped, but the driver's frame-boundary `TWLHD_FRAME` print
remains); this was run once per ROM as an observer-effect control.

### 2.3 Reset-slot identity from each ROM ELF

**FACT.** For every ROM, `0x01FF853C` maps to `__arm_excpt_rst`, the reset
slot at `0x01FF8020` loads `0x01FF853C`, and the exception-vector table is at
`0x01FF8038`.  Each ROM is verified separately below; the outputs are
byte-identical for this range.

**scene2 ELF:**

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-s2.elf 0x01ff853c
__arm_excpt_rst
excpt_handler.32.s:27
$ arm-none-eabi-nm arm9v2/twlhd-arm9v2-autodump-s2.elf | grep -E "1ff80(00|20|38)|1ff853c"
01ff853c t __arm_excpt_rst
01ff8038 T __excpt_vectors
01ff8000 t __itcm_start
$ arm-none-eabi-objdump -d --start-address=0x01ff8020 --stop-address=0x01ff8040 arm9v2/twlhd-arm9v2-autodump-s2.elf

arm9v2/twlhd-arm9v2-autodump-s2.elf:     file format elf32-littlearm


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
```

**scene3 ELF:**

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-s3.elf 0x01ff853c
__arm_excpt_rst
excpt_handler.32.s:27
$ arm-none-eabi-nm arm9v2/twlhd-arm9v2-autodump-s3.elf | grep -E "1ff80(00|20|38)|1ff853c"
01ff853c t __arm_excpt_rst
01ff8038 T __excpt_vectors
01ff8000 t __itcm_start
$ arm-none-eabi-objdump -d --start-address=0x01ff8020 --stop-address=0x01ff8040 arm9v2/twlhd-arm9v2-autodump-s3.elf

arm9v2/twlhd-arm9v2-autodump-s3.elf:     file format elf32-littlearm


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
```

**cubes2 ELF:**

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-c2.elf 0x01ff853c
__arm_excpt_rst
excpt_handler.32.s:27
$ arm-none-eabi-nm arm9v2/twlhd-arm9v2-autodump-c2.elf | grep -E "1ff80(00|20|38)|1ff853c"
01ff853c t __arm_excpt_rst
01ff8038 T __excpt_vectors
01ff8000 t __itcm_start
$ arm-none-eabi-objdump -d --start-address=0x01ff8020 --stop-address=0x01ff8040 arm9v2/twlhd-arm9v2-autodump-c2.elf

arm9v2/twlhd-arm9v2-autodump-c2.elf:     file format elf32-littlearm


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
```

**cubes64 ELF:**

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x01ff853c
__arm_excpt_rst
excpt_handler.32.s:27
$ arm-none-eabi-nm arm9v2/twlhd-arm9v2-autodump-c64.elf | grep -E "1ff80(00|20|38)|1ff853c"
01ff853c t __arm_excpt_rst
01ff8038 T __excpt_vectors
01ff8000 t __itcm_start
$ arm-none-eabi-objdump -d --start-address=0x01ff8020 --stop-address=0x01ff8040 arm9v2/twlhd-arm9v2-autodump-c64.elf

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
```

The four ELF files produce identical symbol/disassembly output for this
range.

### 2.4 Per-ROM results

#### scene2 (`twlhd-arm9v2-autodump-s2.nds`)

**Instrumented run (260 frames):**

```sh
$ $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-s2.nds 260 \
    > /tmp/opencode/s2_instrumented.log 2>&1
```

Raw output (the full log is 268 lines; the relevant lines are extracted
mechanically):

```sh
$ wc -l /tmp/opencode/s2_instrumented.log
268 /tmp/opencode/s2_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/s2_instrumented.log
6:TWLHD_MELON_BKPT cpu=ARM9 frame=2 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=542397
7:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
8:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
9:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
266:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
267:GPU3D: NumPolygons=10 NumVertices=12
268:frames=260 directboot=1 romlen=235520
```

**Verdict for scene2:** `R[15]=0x01FF853C` first appears at frame 2 and stays
for all 260 frames.  **HYPOTHESIS:** this is the pipeline PC of `armWaitForIrq`'s
wait-for-IRQ `mcr` at `0x01FF8534`; under the source-derived convention in
§3.1.1 it decodes to the `bx lr` at `0x01FF8538`.  The reset-slot `bkpt 0x8000`
is never observed executing, and whether an IRQ would eventually release the
wait is not shown.  The driver's frame counter advanced from 2 to 260; GPU3D
state shows 10 polygons / 12 vertices and did not change after the hit.  Whether
the ROM's own frame counter / GX capture advanced is UNVERIFIED (no DLDI path
was exercised, so `twlhd_frame_v2.bin` is not available).  The scene2 halt is a
separate melonDS-side issue (HYPOTHESIS: missing IRQ source in the headless
driver or FreeBIOS direct boot).

**No-watch control run (260 frames):**

```sh
$ TWLHD_WATCH=0 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-s2.nds 260 \
    > /tmp/opencode/s2_control_nowatch.log 2>&1
$ wc -l /tmp/opencode/s2_control_nowatch.log
266 /tmp/opencode/s2_control_nowatch.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |GPU3D:|frames=' /tmp/opencode/s2_control_nowatch.log
6:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
7:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
264:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=10 verts=12
265:GPU3D: NumPolygons=10 NumVertices=12
266:frames=260 directboot=1 romlen=235520
```

**FACT.** With the per-instruction watch disabled, the no-watch driver still
reaches frame 260 with the same stuck `PC9=0x01FF853C` and the same GPU3D
state, so the failure is not caused by the per-instruction watch.

#### scene3 (`twlhd-arm9v2-autodump-s3.nds`)

**Instrumented run (260 frames):**

```sh
$ $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-s3.nds 260 \
    > /tmp/opencode/s3_instrumented.log 2>&1
```

Raw output (the full log is 268 lines; the relevant lines are extracted
mechanically):

```sh
$ wc -l /tmp/opencode/s3_instrumented.log
268 /tmp/opencode/s3_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/s3_instrumented.log
6:TWLHD_MELON_BKPT cpu=ARM9 frame=2 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=542392
7:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
8:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
9:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
266:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
267:GPU3D: NumPolygons=5 NumVertices=12
268:frames=260 directboot=1 romlen=235520
```

**Verdict for scene3:** `R[15]=0x01FF853C` first appears at frame 2 and stays
for all 260 frames.  **HYPOTHESIS:** this is the pipeline PC of `armWaitForIrq`'s
wait-for-IRQ `mcr` at `0x01FF8534`; under the source-derived convention in
§3.1.1 it decodes to the `bx lr` at `0x01FF8538`.  The reset-slot `bkpt 0x8000`
is never observed executing, and whether an IRQ would eventually release the
wait is not shown.  The driver's frame counter advanced from 2 to 260; GPU3D
shows 5 polygons / 12 vertices and did not change after the hit.  Whether the
ROM's own frame counter / GX capture advanced is UNVERIFIED.  The scene3 halt
is a separate melonDS-side issue (HYPOTHESIS: missing IRQ source in the
headless driver or FreeBIOS direct boot).

**No-watch control run (260 frames):**

```sh
$ TWLHD_WATCH=0 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-s3.nds 260 \
    > /tmp/opencode/s3_control_nowatch.log 2>&1
$ wc -l /tmp/opencode/s3_control_nowatch.log
266 /tmp/opencode/s3_control_nowatch.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |GPU3D:|frames=' /tmp/opencode/s3_control_nowatch.log
6:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
7:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
264:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=5 verts=12
265:GPU3D: NumPolygons=5 NumVertices=12
266:frames=260 directboot=1 romlen=235520
```

**FACT.** With the per-instruction watch disabled, the no-watch driver still
reaches frame 260 with the same stuck `PC9=0x01FF853C` and the same GPU3D
state, so the failure is not caused by the per-instruction watch.

#### cubes2 (`twlhd-arm9v2-autodump-c2.nds`)

**Instrumented run (260 frames):**

```sh
$ $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c2.nds 260 \
    > /tmp/opencode/c2_instrumented.log 2>&1
```

Raw output (the full log is 268 lines; the relevant lines are extracted
mechanically):

```sh
$ wc -l /tmp/opencode/c2_instrumented.log
268 /tmp/opencode/c2_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/c2_instrumented.log
6:TWLHD_MELON_BKPT cpu=ARM9 frame=2 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=552763
7:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
8:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
9:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
266:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
267:GPU3D: NumPolygons=12 NumVertices=48
268:frames=260 directboot=1 romlen=235520
```

**Verdict for cubes2:** `R[15]=0x01FF853C` first appears at frame 2 and stays
for all 260 frames.  **HYPOTHESIS:** this is the pipeline PC of `armWaitForIrq`'s
wait-for-IRQ `mcr` at `0x01FF8534`; under the source-derived convention in
§3.1.1 it decodes to the `bx lr` at `0x01FF8538`.  The reset-slot `bkpt 0x8000`
is never observed executing, and whether an IRQ would eventually release the
wait is not shown.  The driver's frame counter advanced from 2 to 260; GPU3D
shows 12 polygons / 48 vertices and did not change after the hit.  Whether the
ROM's own frame counter / GX capture advanced is UNVERIFIED.  The cubes2 halt
is a separate melonDS-side issue (HYPOTHESIS: missing IRQ source in the
headless driver or FreeBIOS direct boot).

**No-watch control run (260 frames):**

```sh
$ TWLHD_WATCH=0 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c2.nds 260 \
    > /tmp/opencode/c2_control_nowatch.log 2>&1
$ wc -l /tmp/opencode/c2_control_nowatch.log
266 /tmp/opencode/c2_control_nowatch.log
$ grep -n -E 'TWLHD_FRAME f(2|3|260) |GPU3D:|frames=' /tmp/opencode/c2_control_nowatch.log
6:TWLHD_FRAME f2 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
7:TWLHD_FRAME f3 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
264:TWLHD_FRAME f260 PC9=01FF853C LR=01FF8534 SP=00000000 CPSR=0000001F mode=1F running=1 polys=12 verts=48
265:GPU3D: NumPolygons=12 NumVertices=48
266:frames=260 directboot=1 romlen=235520
```

**FACT.** With the per-instruction watch disabled, the no-watch driver still
reaches frame 260 with the same stuck `PC9=0x01FF853C` and the same GPU3D
state, so the failure is not caused by the per-instruction watch.

#### cubes64 (`twlhd-arm9v2-autodump-c64.nds`)

Because the DeSmuME failure for CUBES=64 was RTC-coupled, the ROM was run at
host time plus the three documented faulting epochs
(`1794921881`, `1794925481`, `1794925511` from
`docs/reports/cubes64-entry-trace.md:332-339`).

**Host-time instrumented run (140 frames):**

```sh
$ $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_host_instrumented.log 2>&1
```

Raw output (the full log is 148 lines; the relevant lines are extracted
mechanically):

```sh
$ wc -l /tmp/opencode/c64_host_instrumented.log
148 /tmp/opencode/c64_host_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(2|3|4|140) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/c64_host_instrumented.log
6:TWLHD_FRAME f2 PC9=02002E2C LR=020020DF SP=02FF3E08 CPSR=2000003F mode=1F running=1 polys=144 verts=576
7:TWLHD_MELON_BKPT cpu=ARM9 frame=3 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=1030237
8:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
9:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
10:TWLHD_FRAME f4 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
146:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
147:GPU3D: NumPolygons=126 NumVertices=504
148:frames=140 directboot=1 romlen=235520
```

**Epoch-pinned instrumented runs (140 frames each):**

```sh
$ TWLHD_RTC_EPOCH=1794921881 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_e1_instrumented.log 2>&1

$ TWLHD_RTC_EPOCH=1794925481 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_e2_instrumented.log 2>&1

$ TWLHD_RTC_EPOCH=1794925511 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_e3_instrumented.log 2>&1
```

Raw excerpts from all three epoch-pinned runs are quoted below.

**Epoch 1 (`1794921881`):**

```sh
$ wc -l /tmp/opencode/c64_e1_instrumented.log
149 /tmp/opencode/c64_e1_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(3|4|5|140) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/c64_e1_instrumented.log
8:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
9:TWLHD_MELON_BKPT cpu=ARM9 frame=4 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=1528034
10:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
11:TWLHD_FRAME f4 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
12:TWLHD_FRAME f5 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
147:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
148:GPU3D: NumPolygons=126 NumVertices=504
149:frames=140 directboot=1 romlen=235520
```

**Epoch 2 (`1794925481`):**

```sh
$ wc -l /tmp/opencode/c64_e2_instrumented.log
149 /tmp/opencode/c64_e2_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(3|4|5|140) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/c64_e2_instrumented.log
8:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
9:TWLHD_MELON_BKPT cpu=ARM9 frame=4 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=1528034
10:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
11:TWLHD_FRAME f4 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
12:TWLHD_FRAME f5 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
147:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
148:GPU3D: NumPolygons=126 NumVertices=504
149:frames=140 directboot=1 romlen=235520
```

**Epoch 3 (`1794925511`):**

```sh
$ wc -l /tmp/opencode/c64_e3_instrumented.log
149 /tmp/opencode/c64_e3_instrumented.log
$ grep -n -E 'TWLHD_FRAME f(3|4|5|140) |TWLHD_MELON_BKPT|GPU3D:|frames=' /tmp/opencode/c64_e3_instrumented.log
8:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
9:TWLHD_MELON_BKPT cpu=ARM9 frame=4 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=1528034
10:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
11:TWLHD_FRAME f4 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
12:TWLHD_FRAME f5 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
147:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
148:GPU3D: NumPolygons=126 NumVertices=504
149:frames=140 directboot=1 romlen=235520
```

**Verdict for cubes64:** `R[15]=0x01FF853C` first appears **transiently**
(frame 3 with host time, frame 4 with the three pinned epochs).  **HYPOTHESIS:**
this is the pipeline PC of `armWaitForIrq`'s wait-for-IRQ `mcr` at
`0x01FF8534`; under the source-derived convention in §3.1.1 it decodes to the
`bx lr` at `0x01FF8538`, after which an IRQ arrives and the ROM **recovered**
and progressed to frame 140 with `PC9=0x02002170` and
`NumPolygons=126 NumVertices=504`.  The reset-slot `bkpt 0x8000` is never
observed executing.  The permanent DeSmuME-style failure at frame 129 was
**not** reproduced in any of the four RTC conditions.

**No-watch control run (140 frames, host time):**

```sh
$ TWLHD_WATCH=0 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_control_nowatch.log 2>&1
$ wc -l /tmp/opencode/c64_control_nowatch.log
146 /tmp/opencode/c64_control_nowatch.log
$ grep -n -E 'TWLHD_FRAME f(2|3|140) |GPU3D:|frames=' /tmp/opencode/c64_control_nowatch.log
6:TWLHD_FRAME f2 PC9=02002E2C LR=020020DF SP=02FF3E08 CPSR=2000003F mode=1F running=1 polys=144 verts=576
7:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
144:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
145:GPU3D: NumPolygons=126 NumVertices=504
146:frames=140 directboot=1 romlen=235520
```

**FACT.** With the per-instruction watch disabled, the no-watch driver reaches
frame 140 with the same recovered `PC9` and GPU3D state, so the recovery is not
caused by the per-instruction watch.

### 2.5 Control-run summary

| ROM | Driver | Frames reached | Final ARM9 PC | Final GPU3D state |
|---|---|---|---|---|
| scene2 | no-watch `bkpt_driver` (`TWLHD_WATCH=0`) | 260 | `0x01FF853C` | NumPolygons=10 NumVertices=12 |
| scene3 | no-watch `bkpt_driver` (`TWLHD_WATCH=0`) | 260 | `0x01FF853C` | NumPolygons=5 NumVertices=12 |
| cubes2 | no-watch `bkpt_driver` (`TWLHD_WATCH=0`) | 260 | `0x01FF853C` | NumPolygons=12 NumVertices=48 |
| cubes64 | no-watch `bkpt_driver` (`TWLHD_WATCH=0`) | 140 | `0x02002170` | NumPolygons=126 NumVertices=504 |

**FACT.** The no-watch driver reaches the same frame counts, ARM9 PC, and GPU3D
state as the instrumented runs, confirming the failures (and the c64 recovery)
are not caused by the per-instruction watch.

---

## 3. Conditional analysis (REQ-3)

### 3.1 ROMs where melonDS reports `R[15]=0x01FF853C` (scene2, scene3, cubes2)

**FACT.** All three ROMs report `R[15] = 0x01FF853C` at frame 2 and stayed
there.  The reset-slot `bkpt 0x8000` at `0x01FF853C` is never observed
executing.  The first captured state is:

```
TWLHD_MELON_BKPT cpu=ARM9 frame=2 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F
```

i.e. PC=`0x01FF853C`, LR=`0x01FF8534`, SP=`0x00000000`, CPSR=`0x0000001F`
(System mode, ARM state, interrupts enabled).

**HYPOTHESIS.** `R[15] = 0x01FF853C` is the pipeline PC of the wait-for-IRQ
`mcr` at `0x01FF8534` in `armWaitForIrq`; the source-derived convention in
§3.1.1 decodes the same sampled value to the `bx lr` at `0x01FF8538`.

The 64-entry chronological PC ring at the moment the watch fires on
`R[15]=0x01FF853C` is identical for scene2/scene3/cubes2:

```
01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C
01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174
01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C
01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060
01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0
01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510
01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518
01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
```

**FACT.** Symbolization of the 55 unique addresses in the scene2 ring:

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-s2.elf \
    0x01FF8140 0x01FF8144 0x01FF8148 0x01FF814C 0x01FF8150 0x01FF8154 0x01FF8158 0x01FF815C \
    0x01FF8160 0x01FF8164 0x01FF8168 0x01FF817C 0x01FF8180 0x01FF8184 0x01FF8170 0x01FF8174 \
    0x01FF8178 0x01FF8188 0x01FF818C 0x01FF8054 0x01FF8058 0x01FF805C 0x01FF8060 \
    0x01FF809C 0x01FF80A0 0x01FF80A4 0x01FF80A8 0x01FF80AC 0x01FF80B0 0x01FF84EC 0x01FF84F0 \
    0x01FF84F4 0x01FF84F8 0x01FF84FC 0x01FF8500 0x01FF8504 0x01FF8508 0x01FF850C 0x01FF8510 \
    0x01FF8514 0x01FF80B4 0x01FF80B8 0x01FF80BC 0x01FF80C0 0x01FF80C4 0x01FF80C8 0x01FF8518 \
    0x01FF851C 0x01FF8520 0x01FF8524 0x01FF8528 0x01FF852C 0x01FF8530 0x01FF8534 0x01FF853C
threadLinkEnqueue thread-priv.h:90
threadLinkEnqueue thread-priv.h:94
threadLinkEnqueue thread-priv.h:96
threadLinkEnqueue thread-priv.h:96
threadLinkEnqueue thread-priv.h:97
threadBlock thread_hot.32.c:32
threadLinkEnqueue thread-priv.h:99
threadFindRunnable thread-priv.h:73 (discriminator 1)
threadLinkEnqueue thread-priv.h:100
threadFindRunnable thread-priv.h:73 (discriminator 1)
threadFindRunnable thread-priv.h:73 (discriminator 1)
threadFindRunnable thread-priv.h:73 (discriminator 3)
threadFindRunnable thread-priv.h:73 (discriminator 3)
threadBlock thread_hot.32.c:33
threadFindRunnable thread-priv.h:73 (discriminator 1)
threadFindRunnable thread-priv.h:73 (discriminator 1)
threadFindRunnable thread-priv.h:73 (discriminator 3)
threadBlock thread_hot.32.c:33
threadBlock thread_hot.32.c:35 (discriminator 1)
threadSwitchTo thread_hot.32.c:10 (discriminator 1)
threadSwitchTo thread_hot.32.c:10 (discriminator 2)
threadSwitchTo thread_hot.32.c:10 (discriminator 2)
threadSwitchTo thread_hot.32.c:11
threadSwitchTo thread_hot.32.c:9
threadSwitchTo thread_hot.32.c:17
threadSwitchTo thread_hot.32.c:17
threadSwitchTo thread_hot.32.c:17
threadSwitchTo thread_hot.32.c:17
threadSwitchTo thread_hot.32.c:17 (discriminator 1)
armContextSave arm-context.32.s:13
armContextSave arm-context.32.s:14
armContextSave arm-context.32.s:15
armContextSave arm-context.32.s:16
armContextSave arm-context.32.s:17
armContextSave arm-context.32.s:18
armContextSave arm-context.32.s:19
armContextSave arm-context.32.s:20
armContextSave arm-context.32.s:21
armContextSave arm-context.32.s:22
armContextLoad arm-context.32.s:28
threadSwitchTo thread_hot.32.c:17 (discriminator 1)
threadSwitchTo thread_hot.32.c:17 (discriminator 1)
threadSwitchTo thread_hot.32.c:19
threadSwitchTo thread_hot.32.c:18
threadSwitchTo thread_hot.32.c:19
threadSwitchTo thread_hot.32.c:19
armContextLoad arm-context.32.s:30
armContextLoad arm-context.32.s:31
armContextLoad arm-context.32.s:32
armContextLoad arm-context.32.s:33
armContextLoad arm-context.32.s:34
armContextLoad arm-context.32.s:35
armContextLoad arm-context.32.s:36
armWaitForIrq common.h:116
__arm_excpt_rst excpt_handler.32.s:27
```

**FACT.** Disassembly of the ring tail (0x01FF8500..0x01FF8540) from the
scene2 ELF:

```sh
$ arm-none-eabi-objdump -d --start-address=0x01FF8500 --stop-address=0x01FF8540 arm9v2/twlhd-arm9v2-autodump-s2.elf
arm9v2/twlhd-arm9v2-autodump-s2.elf:     file format elf32-littlearm

Disassembly of section .itcm:
01ff8500 <armContextSave+0x18>:
 1ff8500:	e9407ff0 	stmdb	r0, {r4, r5, r6, r7, r8, r9, sl, fp, ip, sp, lr}^
 1ff8504:	e880200c 	stm	r0, {r2, r3, sp}
 1ff8508:	e121f00c 	msr	CPSR_c, ip
 1ff850c:	e3a00000 	mov	r0, #0
 1ff8510:	e12fff1e 	bx	lr

01ff8514 <armContextLoad>:
 1ff8514:	e321f0d3 	msr	CPSR_c, #211	@ 0xd3

01ff8518 <armContextLoadFromSvc>:
 1ff8518:	e590e03c 	ldr	lr, [r0, #60]	@ 0x3c
 1ff851c:	e5903040 	ldr	r3, [r0, #64]	@ 0x40
 1ff8520:	e590d044 	ldr	sp, [r0, #68]	@ 0x44
 1ff8524:	e169f003 	msr	SPSR_fc, r3
 1ff8528:	e8d07fff 	ldm	r0, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, sp, lr}^
 1ff852c:	e1a00000 	nop			@ (mov r0, r0)
 1ff8530:	e1b0f00e 	movs	pc, lr

01ff8534 <armWaitForIrq>:
 1ff8534:	ee070f90 	mcr	15, 0, r0, cr7, cr0, {4}
 1ff8538:	e12fff1e 	bx	lr

01ff853c <__arm_excpt_rst>:
 1ff853c:	e1280070 	bkpt	0x8000
```

**FACT.** The 64-entry ring contains no `PC == 0x00000000`, no
`0x01FF8000..0x01FF8040` vector-slot entry, and no main-RAM address such as
`0x0200F55C`.  The tail is `0x01FF8530 → 0x01FF8534 → 0x01FF853C`.
Under the source-derived convention in §3.1.1 (sampled `R[15]` is the
executing instruction address plus 4) the tail decodes to executing the `nop`
at `0x01FF852C`, then the exception-return `movs pc, lr` at `0x01FF8530`,
then the `bx lr` at `0x01FF8538` inside `armWaitForIrq`.  The `bkpt 0x8000`
at `0x01FF853C` is never observed executing.

**HYPOTHESIS.** The instruction that produced `R[15] = 0x01FF853C` is the
`mcr` wait-for-interrupt instruction at `0x01FF8534` in `armWaitForIrq`.  Under
the source-derived convention in §3.1.1 the same sampled value decodes to the
`bx lr` at `0x01FF8538`.  The c64 post-hit trace (§3.1.1) shows the next
observed PC is `0x01FF8538`, and the `bkpt 0x8000` at `0x01FF853C` is never
observed executing.  The actual control flow is therefore an exception-return
into `armWaitForIrq`, not a branch to `0x00000000` followed by the reset-slot
address.  This is a different path from the DeSmuME reference chain
(`<site> → 0x00000000 → 0x01FF8020 → 0x01FF853C`).

**HYPOTHESIS.** Because scene2, scene3 and cubes2 report `R[15]=0x01FF853C`
at frame 2 in melonDS via this different path and stay stuck, the
DeSmuME-model-defect hypothesis (a single LDM exception-return mask bug) is at
most **consistent with / not contradicted by** these runs.  The stuck state may
reflect an early-boot thread-context or IRQ-stack problem (the hit has
`SP=0x00000000` in System mode) or a missing IRQ source in the headless driver
/ FreeBIOS direct boot, rather than the LDM mask.

### 3.1.1 CUBES=64 also reports `R[15]=0x01FF853C`, then recovers via IRQ

**FACT.** CUBES=64 reports the same `R[15] = 0x01FF853C` state at frame 3
(host time) / frame 4 (pinned epochs).  The PC/LR/SP/CPSR and the 64-entry
ring are identical to scene2/scene3/cubes2.  **HYPOTHESIS:** the first observed
path is the same `armWaitForIrq` pipeline-PC value analyzed above (`mcr` at
`0x01FF8534`); under the source-derived convention in §3.1.1 the sampled value
decodes to the `bx lr` at `0x01FF8538`.  The ring symbolized against the c64
ELF produces the same output as for scene2.

**FACT.** To see how execution leaves the `R[15]=0x01FF853C` state, the
scratch watch was extended to print the next 32 architectural PCs after the
first hit (`TWLHD_POST_PC_N=32`).  Host-time run, 140 frames:

```sh
$ TWLHD_POST_PC_N=32 $HOME/3ds/hwtest-staging/melonds-src-check-build/bkpt_driver \
    arm9v2/twlhd-arm9v2-autodump-c64.nds 140 \
    > /tmp/opencode/c64_host_postpc.log 2>&1
$ wc -l /tmp/opencode/c64_host_postpc.log
180 /tmp/opencode/c64_host_postpc.log
$ grep -n 'TWLHD_POST_PC\|TWLHD_MELON_BKPT\|TWLHD_FRAME f3\|TWLHD_FRAME f140\|GPU3D:\|frames=' /tmp/opencode/c64_host_postpc.log
7:TWLHD_MELON_BKPT cpu=ARM9 frame=3 pc=01FF853C cpsr=0000001F sp=00000000 lr=01FF8534 mode=1F seq=1030237
8:TWLHD_MELON_BKPT_RING 01FF8140 01FF8144 01FF8148 01FF814C 01FF8150 01FF8154 01FF8158 01FF815C 01FF8160 01FF8164 01FF8168 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8170 01FF8174 01FF8178 01FF817C 01FF8180 01FF8184 01FF8188 01FF818C 01FF8054 01FF8058 01FF805C 01FF8060 01FF809C 01FF80A0 01FF80A4 01FF80A8 01FF80AC 01FF80B0 01FF84EC 01FF84F0 01FF84F4 01FF84F8 01FF84FC 01FF8500 01FF8504 01FF8508 01FF850C 01FF8510 01FF8514 01FF80B4 01FF80B8 01FF80BC 01FF80C0 01FF80C4 01FF80C8 01FF8518 01FF851C 01FF8520 01FF8524 01FF8528 01FF852C 01FF8530 01FF8534 01FF853C
9:TWLHD_POST_PC pc=01FF8538 lr=01FF8534 sp=00000000 cpsr=0000001F mode=1F
10:TWLHD_POST_PC pc=0000001C lr=01FF853C sp=02FF3F80 cpsr=000000D2 mode=12
11:TWLHD_POST_PC pc=01FF8038 lr=01FF853C sp=02FF3F80 cpsr=000000D2 mode=12
12:TWLHD_POST_PC pc=01FF8608 lr=01FF853C sp=02FF3F80 cpsr=000000D2 mode=12
13:TWLHD_POST_PC pc=01FF860C lr=01FF8538 sp=02FF3F80 cpsr=000000D2 mode=12
14:TWLHD_POST_PC pc=01FF8610 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
15:TWLHD_POST_PC pc=01FF8614 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
16:TWLHD_POST_PC pc=01FF8618 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
17:TWLHD_POST_PC pc=01FF861C lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
18:TWLHD_POST_PC pc=01FF8620 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
19:TWLHD_POST_PC pc=01FF8624 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
20:TWLHD_POST_PC pc=01FF8628 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
21:TWLHD_POST_PC pc=01FF862C lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
22:TWLHD_POST_PC pc=01FF8630 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
23:TWLHD_POST_PC pc=01FF8634 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
24:TWLHD_POST_PC pc=01FF8638 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
25:TWLHD_POST_PC pc=01FF863C lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
26:TWLHD_POST_PC pc=01FF8640 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
27:TWLHD_POST_PC pc=01FF8644 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
28:TWLHD_POST_PC pc=01FF8648 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
29:TWLHD_POST_PC pc=01FF864C lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
30:TWLHD_POST_PC pc=01FF8650 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
31:TWLHD_POST_PC pc=01FF8654 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
32:TWLHD_POST_PC pc=01FF8658 lr=01FF8538 sp=02FF3F68 cpsr=000000D2 mode=12
33:TWLHD_POST_PC pc=01FF865C lr=01FF8538 sp=02FF3F68 cpsr=600000D2 mode=12
34:TWLHD_POST_PC pc=01FF8660 lr=01FF8538 sp=02FF3F68 cpsr=600000D2 mode=12
35:TWLHD_POST_PC pc=01FF8664 lr=01FF8538 sp=02FF3F68 cpsr=600000D2 mode=12
36:TWLHD_POST_PC pc=01FF8668 lr=01FF8538 sp=02FF3F68 cpsr=600000D2 mode=12
37:TWLHD_POST_PC pc=01FF866C lr=01FF8538 sp=02FF3F68 cpsr=600000D2 mode=12
38:TWLHD_POST_PC pc=01FF8670 lr=01FF8538 sp=02FF3F68 cpsr=200000D2 mode=12
39:TWLHD_POST_PC pc=01FF8674 lr=01FF8538 sp=02FF3F68 cpsr=200000D2 mode=12
40:TWLHD_POST_PC pc=01FF8678 lr=01FF8538 sp=02FF3F68 cpsr=200000D2 mode=12
41:TWLHD_FRAME f3 PC9=0200217A LR=020020DF SP=02FF3E08 CPSR=0000003F mode=1F running=1 polys=126 verts=504
178:TWLHD_FRAME f140 PC9=02002170 LR=020020DF SP=02FF3E08 CPSR=8000003F mode=1F running=1 polys=126 verts=504
179:GPU3D: NumPolygons=126 NumVertices=504
180:frames=140 directboot=1 romlen=235520
```

```sh
$ arm-none-eabi-addr2line -f -s -e arm9v2/twlhd-arm9v2-autodump-c64.elf 0x01FF8608 0x01FF8038 0x01FF8544
__arm_excpt_irq
irq_handler.32.s:13
__excpt_vectors
bootstub_arm9.s:32
__arm_excpt_und
excpt_handler.32.s:48
```

**FACT.** The relation between the sampled `R[15]` and the instruction being
executed is fixed by the melonDS interpreter loop and `ARMv5::JumpTo`.  First,
the scratch watch call site sits immediately before the `if (CPSR & 0x20)`
ARM/Thumb dispatch and therefore before the prefetch that advances `R[15]`:

```sh
$ grep -n 'TwlhdArm9PcWatch' $HOME/3ds/hwtest-staging/melonds-src-check/src/ARM.cpp
770:                TwlhdArm9PcWatch(this);
```

```sh
$ sed -n '766,800p' $HOME/3ds/hwtest-staging/melonds-src-check/src/ARM.cpp | cat -n
     1	        else
     2	#endif
     3	        {
     4	            if (TwlhdWatchEnabled)
     5	                TwlhdArm9PcWatch(this);
     6	
     7	            if (CPSR & 0x20) // THUMB
     8	            {
     9	                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
    10	                    GdbCheckC();
    11	
    12	                // prefetch
    13	                R[15] += 2;
    14	                CurInstr = NextInstr[0];
    15	                NextInstr[0] = NextInstr[1];
    16	                if (R[15] & 0x2) { NextInstr[1] >>= 16; CodeCycles = 0; }
    17	                else             NextInstr[1] = CodeRead32(R[15], false);
    18	
    19	                // actually execute
    20	                u32 icode = (CurInstr >> 6) & 0x3FF;
    21	                ARMInterpreter::THUMBInstrTable[icode](this);
    22	            }
    23	            else
    24	            {
    25	                if constexpr (mode == CPUExecuteMode::InterpreterGDB)
    26	                    GdbCheckC();
    27	
    28	                // prefetch
    29	                R[15] += 4;
    30	                CurInstr = NextInstr[0];
    31	                NextInstr[0] = NextInstr[1];
    32	                NextInstr[1] = CodeRead32(R[15], false);
    33	
    34	                // actually execute
    35	                if (CheckCondition(CurInstr >> 28))
    36	                {
    37	                    u32 icode = ((CurInstr >> 4) & 0xF) | ((CurInstr >> 16) & 0xFF0);
    38	                    ARMInterpreter::ARMInstrTable[icode](this);
    39	                }
    40	                else if ((CurInstr & 0xFE000000) == 0xFA000000)
    41	                {
    42	                    ARMInterpreter::A_BLX_IMM(this);
    43	                }
    44	                else
    45	                    AddCycles_C();
    46	            }
```

The upstream loop has the same ordering; the relevant ARM-state prefetch block
is:

```sh
$ grep -n 'void ARMv5::Execute\|void ARMv5::JumpTo' $HOME/3ds/melonDS/src/ARM.cpp
294:void ARMv5::JumpTo(u32 addr, bool restorecpsr)
593:void ARMv5::Execute()
```

```sh
$ sed -n '681,691p' $HOME/3ds/melonDS/src/ARM.cpp | cat -n
     1	                // prefetch
     2	                R[15] += 4;
     3	                CurInstr = NextInstr[0];
     4	                NextInstr[0] = NextInstr[1];
     5	                NextInstr[1] = CodeRead32(R[15], false);
     6	
     7	                // actually execute
     8	                if (CheckCondition(CurInstr >> 28))
     9	                {
    10	                    u32 icode = ((CurInstr >> 4) & 0xF) | ((CurInstr >> 16) & 0xFF0);
    11	                    ARMInterpreter::ARMInstrTable[icode](this);
    12	                }
```

`JumpTo` loads the pipeline and sets `R[15]` directly; for an ARM target it
sets `R[15] = addr+4` and fetches the first two instructions at `addr` and
`addr+4`:

```sh
$ sed -n '294,360p' $HOME/3ds/melonDS/src/ARM.cpp | cat -n
     1	void ARMv5::JumpTo(u32 addr, bool restorecpsr)
     2	{
     3	    if (restorecpsr)
     4	    {
     5	        RestoreCPSR();
     6	
     7	        if (CPSR & 0x20)    addr |= 0x1;
     8	        else                addr &= ~0x1;
     9	    }
    10	
    11	    // aging cart debug crap
    12	    //if (addr == 0x0201764C) printf("capture test %d: R1=%08X\n", R[6], R[1]);
    13	    //if (addr == 0x020175D8) printf("capture test %d: res=%08X\n", R[6], R[0]);
    14	
    15	    u32 oldregion = R[15] >> 24;
    16	    u32 newregion = addr >> 24;
    17	
    18	    RegionCodeCycles = MemTimings[addr >> 12][0];
    19	
    20	    if (addr & 0x1)
    21	    {
    22	        addr &= ~0x1;
    23	        R[15] = addr+2;
    24	
    25	        if (newregion != oldregion) SetupCodeMem(addr);
    26	
    27	        // two-opcodes-at-once fetch
    28	        // doesn't matter if we put garbage in the MSbs there
    29	        if (addr & 0x2)
    30	        {
    31	            NextInstr[0] = CodeRead32(addr-2, true) >> 16;
    32	            Cycles += CodeCycles;
    33	            NextInstr[1] = CodeRead32(addr+2, false);
    34	            Cycles += CodeCycles;
    35	        }
    36	        else
    37	        {
    38	            NextInstr[0] = CodeRead32(addr, true);
    39	            NextInstr[1] = NextInstr[0] >> 16;
    40	            Cycles += CodeCycles;
    41	        }
    42	
    43	        CPSR |= 0x20;
    44	    }
    45	    else
    46	    {
    47	        addr &= ~0x3;
    48	        R[15] = addr+4;
    49	
    50	        if (newregion != oldregion) SetupCodeMem(addr);
    51	
    52	        NextInstr[0] = CodeRead32(addr, true);
    53	        Cycles += CodeCycles;
    54	        NextInstr[1] = CodeRead32(addr+4, false);
    55	        Cycles += CodeCycles;
    56	
    57	        CPSR &= ~0x20;
    58	    }
    59	
    60	    if (!(PU_Map[addr>>12] & 0x04))
    61	    {
    62	        PrefetchAbort();
    63	        return;
    64	    }
    65	
    66	    NDS.MonitorARM9Jump(addr);
    67	}
```

**FACT (source-derived convention).** Because the watch samples `R[15]` before
the prefetch, and because `JumpTo` sets `R[15] = target+4` for an ARM branch or
exception entry, the sampled value is consistently the address of the
instruction about to execute plus 4.  Equivalently, the instruction whose
address is `sampled_R15 - 4` is the one whose opcode is dispatched on that
iteration.  The bare "`R[15] = PC + 8`" parenthetical used in round 3 is not the
sampling relation; it describes the architectural value after the prefetch has
advanced `R[15]`.

Re-derived post-hit addresses (subtract 4 from each sampled `R[15]`):
`0x01FF853C` → `bx lr` at `0x01FF8538`; `0x01FF8538` → `mcr` wait-for-IRQ at
`0x01FF8534`; `0x0000001C` → IRQ vector at `0x00000018`; `0x01FF8038` → the
`ldr pc` vector-load instruction at `0x01FF8034`; and `0x01FF8608` → the
`__arm_excpt_irq` handler at `0x01FF8604`.  The `bkpt 0x8000` at `0x01FF853C`
is therefore never the instruction being executed when `R[15]=0x01FF853C` is
sampled.  The exact literal word fetched by the `0x01FF8034` vector load and the
precise entry offset inside `__arm_excpt_irq` are not quoted here; the meanings
of `0x01FF8038` and `0x01FF8608` beyond the convention-derived decoding are
**UNVERIFIED**.

**HYPOTHESIS.** The ROM returns to System mode and resumes the main loop at
`0x0200217A`; this is supported by the next frame-boundary line
`TWLHD_FRAME f3 PC9=0200217A ... mode=1F` (System mode), not by the 32
post-hit PCs, which end in IRQ mode at `0x01FF8678`.  This recovery is
consistent with melonDS's architecturally standard exception-return path.

### 3.2 Hypothesis statement

**FACT (scoped to the runs quoted in §2.4).**

* scene2, scene3 and cubes2 in melonDS report `R[15]=0x01FF853C` at frame 2
  and stay stuck there.
* CUBES=64 in melonDS reports `R[15]=0x01FF853C` transiently at frame 3 (host
  time) / frame 4 (pinned epochs), then recovers and progresses to frame 140
  with normal GPU3D output.

**HYPOTHESIS.** The melonDS observations do **not** follow the DeSmuME
branch-to-0 path (`<site> → 0x00000000 → 0x01FF8020 → 0x01FF853C`).  The
first observed path in melonDS is an exception-return into `armWaitForIrq`
(`0x01FF8534`).  **HYPOTHESIS:** the pipeline of that exception-return makes
architectural `R[15]` read the reset-slot address (`0x01FF853C`); the
source-derived convention in §3.1.1 decodes the same sampled value to the
`bx lr` at `0x01FF8538`.  In either decoding, the `bkpt 0x8000` at
`0x01FF853C` is never observed executing.  No ROM reproduces the DeSmuME
branch-to-0 path in melonDS, so the DeSmuME LDM-mask hypothesis is at most
**consistent with / not contradicted by** these runs (scoped to the §2.4
configurations).  The scene2/scene3/cubes2 halt is a separate melonDS-side
issue (HYPOTHESIS: missing IRQ source in the headless driver or FreeBIOS direct
boot).  For CUBES=64, the scoped non-reproduction is consistent with melonDS's
architecturally standard LDM/exception-return behaviour and is not evidence
that the DeSmuME mask is correct.

**What this does NOT prove for hardware:**

* FreeBIOS direct boot is not a console boot; secure-area decryption fails and
the boot path differs.
* The headless driver uses the interpreter and no JIT; timing differs from a
normal frontend.
* DLDI/cflash/FAT are not exercised by the headless driver; the CUBES=64
DeSmuME fault was RTC/cflash-coupled.
* The RTC was pinned to three specific epochs plus host time; other epochs or
sub-second phase differences are UNVERIFIED.

Therefore, "melonDS does not reproduce the CUBES=64 stuck fault" is only a
fact for the quoted emulator configuration; it is not a proof that hardware is
immune.

### 3.3 Next experiment

**For scene2/scene3/cubes2:** extend the scratch watch to record the last
non-ITCM (`0x02xxxxxx`) PC before the hit and the exact `LR`/`SPSR` consumed by
the `movs pc, lr` at `0x01FF8530`, to identify the ROM-side site corresponding
to DeSmuME's `0x0200F55C` and confirm whether the stuck state is an
IRQ-stack/thread-context issue or a different root cause.

**For the LDM-mask hypothesis specifically:** instrument a scratch DeSmuME
build to log the popped PC and `next_instruction` at the same `OP_LDMIA2_W`
site, then compare the values for CUBES=64 at the faulting epochs.
Alternatively, a hardware flashcard run of the CUBES=64 ROM (§5) would bound
whether the fault is emulator-specific.

---

## 4. melonDS LDM exception-return mask (REQ-4)

**FACT.** The ARM LDM interpreter in the pinned checkout is at
`src/ARMInterpreter_LoadStore.cpp:393`.  The PC is loaded in the `R[15]` block
(starting at `:435`), bit 0 is masked only for ARM7 at `:439`, and the actual
exception-return branch is completed at `:464` by
`cpu->JumpTo(pc, cpu->CurInstr & (1<<22))`:

```sh
$ sed -n '393,450p' $HOME/3ds/melonDS/src/ARMInterpreter_LoadStore.cpp
void A_LDM(ARM* cpu)
{
    u32 baseid = (cpu->CurInstr >> 16) & 0xF;
    u32 base = cpu->R[baseid];
    u32 wbbase;
    u32 preinc = (cpu->CurInstr & (1<<24));
    bool first = true;

    if (!(cpu->CurInstr & (1<<23)))
    {
        base -= 4 * __builtin_popcount(cpu->CurInstr & 0xFFFF);

        if (cpu->CurInstr & (1<<21))
        {
            // pre writeback
            wbbase = base;
        }

        preinc = !preinc;
    }

    if ((cpu->CurInstr & (1<<22)) && !(cpu->CurInstr & (1<<15)))
        cpu->UpdateMode(cpu->CPSR, (cpu->CPSR&~0x1F)|0x10, true);

    for (int i = 0; i < 15; i++)
    {
        if (cpu->CurInstr & (1<<i))
        {
            if (preinc) base += 4;
            if (first) cpu->DataRead32 (base, &cpu->R[i]);
            else       cpu->DataRead32S(base, &cpu->R[i]);
            first = false;
            if (!preinc) base += 4;
        }
    }

    u32 pc = 0;
    if (cpu->CurInstr & (1<<15))
    {
        if (preinc) base += 4;
        if (first) cpu->DataRead32 (base, &pc);
        else       cpu->DataRead32S(base, &pc);
        if (!preinc) base += 4;

        if (cpu->Num == 1)
            pc &= ~0x1;
    }

    if (cpu->CurInstr & (1<<21))
    {
        // post writeback
        if (cpu->CurInstr & (1<<23))
            wbbase = base;

        if (cpu->CurInstr & (1 << baseid))
        {
            if (cpu->Num == 0)
            {
```

The full 15-line block around the `JumpTo` call requested by the command is:

```sh
$ sed -n '456,470p' $HOME/3ds/melonDS/src/ARMInterpreter_LoadStore.cpp | cat -n
     1	        else
     2	            cpu->R[baseid] = wbbase;
     3	    }
     4	
     5	    if ((cpu->CurInstr & (1<<22)) && !(cpu->CurInstr & (1<<15)))
     6	        cpu->UpdateMode((cpu->CPSR&~0x1F)|0x10, cpu->CPSR, true);
     7	
     8	    if (cpu->CurInstr & (1<<15))
     9	        cpu->JumpTo(pc, cpu->CurInstr & (1<<22));
    10	
    11	    cpu->AddCycles_CDI();
    12	}
    13	
    14	void A_STM(ARM* cpu)
    15	{
```

**HYPOTHESIS.** melonDS does **not** use DeSmuME's mask
`tmp & (0xFFFFFFFC | (BIT0(tmp)<<1))`.  Instead it loads the PC value, masks
only bit 0 for ARM7 (`pc &= ~0x1`), and calls `JumpTo(pc, restorecpsr=S-bit)`.
`JumpTo` (in `src/ARM.cpp:294`) restores CPSR from SPSR first and then uses
**SPSR.T** to decide ARM vs. Thumb mode, clearing or setting bit 0 of `addr`
accordingly.  This is the standard ARMv5 exception-return behaviour and is
architecturally different from DeSmuME's `BIT0(tmp)` mask.

---

## 5. Hardware relevance (REQ-5)

**HYPOTHESIS only.** The smallest hardware experiment that would bound these
emulator observations is the same one already scoped in
`docs/reports/cubes64-entry-trace.md:704-710`: flash the CUBES=64 ROM to a
DS/DSi flashcard, run it with the RTC set near the faulting epochs, and check
whether `twlhd_frame_v2.bin` is written at frame 120 and whether the frame
counter passes 129.

**Brick-risk class:** low/moderate — this is a user-mode homebrew ROM with no
firmware or bootloader writes; risk is limited to a possible freeze requiring a
hard reset.  It was **not run** in this session.

---

## 6. Scope, tests, privacy, and touched files (REQ-6)

**FACT.** `make test` was run and passed:

```sh
$ make test 2>&1 | tail -25
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq3_compare.py'
..............
----------------------------------------------------------------------
Ran 14 tests in 1.551s

OK
PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools -p 'test_eq_scenes.py'
................
----------------------------------------------------------------------
Ran 16 tests in 1.213s

OK
EQ2_DISCOVER_SKIP=1 EQ3_DISCOVER_SKIP=1 EQ_SCENES_DISCOVER_SKIP=1 PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
...............................................................................................................usage: compare_texture_capture.py <captured.bin> [angle]
usage: compare_texture_capture.py <captured.bin> [angle]
error: angle must be an integer
........ssssssssssssssssssssssssssssssssssssssssss...........................................................................................................................................................................................................
-----------------------------------------------------------------------
Ran 364 tests in 88.750s

OK (skipped=42)
/tmp/tmpptrhvm1a/twlhd_frame_v2_tex_over.bin
vertex_count=36
max_clip_deviation=0.007324
colour_exactness=PASS
```

**FACT.** The diff for this round is below the 90 000-byte size limit:

```sh
$ git diff HEAD | wc -c
24901
```

**FACT.** The melonDS checkout was not modified:

```sh
$ git -C $HOME/3ds/melonDS status --porcelain
(no output)
```

**FACT.** This repo's `git status --porcelain` shows only the report edit;
the ROM build artifacts in `arm9v2/` are ignored by the existing `.gitignore`:

```sh
$ git status --porcelain
 M docs/reports/melonds-bkpt-check.md
```

No `.elf`, `.nds`, or `build-autodump-*` files appear because they are ignored
by the existing `.gitignore`.  No literal home-directory paths were written into the repo; the
report uses `$HOME` as required by the privacy git hook.  No worktrees were
created or removed.

---

## 7. Requirement traceability

| Requirement | Status | Evidence in this report |
|---|---|---|
| REQ-1: build four ROMs, quote commands, SHA-256, CRC, compare with earlier reports | DONE | §1, full build commands and SHA/CRC output |
| REQ-2: run each ROM in melonDS headless driver ≥250/≥130 frames, objective bad-PC watch, ELF verification, per-ROM verdict, progress evidence, uninstrumented control | DONE | §2 (all four ROMs, all epochs, watch definition, ELF verification, no-watch `TWLHD_WATCH=0` control runs with quoted `TWLHD_FRAME` PC9) |
| REQ-3: first observed path + ring for failing ROMs; scoped non-reproduction FACT + hypothesis + caveats for c64; consistent-with / not-contradicted hypothesis statement | DONE | §3.1 (addr2line + objdump path analysis), §3.1.1 (source-derived PC convention, c64 pipeline-PC event and IRQ recovery trace) |
| REQ-4: quote melonDS LDM equivalent file:line and compare with DeSmuME | DONE | §4 |
| REQ-5: hardware hypothesis + smallest experiment + brick risk, not run | DONE | §5 |
| REQ-6: scope/tests/privacy/untouched checkout/git status | DONE | §6 |

---

## 8. Files changed

Only one repo file was changed:

* `docs/reports/melonds-bkpt-check.md`

ROM build artifacts under `arm9v2/` are scratch outputs of the documented
recipes and are expected to be untracked/ignored.  All instrumentation and
driver changes are outside the repo under `$HOME/3ds/hwtest-staging/`.
