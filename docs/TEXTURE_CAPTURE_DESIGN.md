# Recorder-side texture capture on the DS — design study

Status: **documentation only**. No source, test, tool or Makefile change accompanies
this file. It answers the questions in the 2026-11-14 texture-capture design task
and proposes stages `5C-0..5C-5`; it implements none of them.

Scope is the one already fixed in `docs/PHASE5B_PLAN.md:10-14`: offline rendering
of captures from **our own homebrew ROMs**. Commercial-game GPU interception,
real-time DS rendering, RTCom changes and assistant-run hardware are out of
scope. Nothing here changes the owner-approval list at
`docs/PHASE5B_PLAN.md:680-690`.

Sources read for this document:

- Repository: `arm9v2/`, `common/`, `tests/host/`, `tools/`, `docs/` at the
  working tree of 2026-11-14.
- DeSmuME checkout `/home/one/3ds/desmume`, `desmume/` subdirectory, commit
  `fc504ee0` (2026-10-25), `git describe` `release_0_9_13-429-gfc504ee0`. Line
  numbers below are from that checkout.
- devkitPro under `/opt/devkitpro`: libnds **2.0.0**
  (`/opt/devkitpro/libnds/include/nds/libversion.h:5-9`) and a present
  `devkitARM` tree. GBATEK was **not** read in this session.

Labels, in the style of `docs/DS_GX_RESEARCH.md:10-12`:

- **VERIFIED** — read at the cited `file:line` in this session. Where the file is
  DeSmuME source, the fact describes the *emulator model*, not silicon; the
  citation still lets the reviewer re-check the claim.
- **UNVERIFIED** — not checked against hardware, GBATEK or a run. The text says
  what would confirm it.

A statement that carries a `file:line` citation and no explicit UNVERIFIED tag
is VERIFIED in this session; a statement tagged UNVERIFIED is not, even when it
is widely believed. This keeps the binary tag the task asks for without repeating
it on every sub-clause.

Every code claim below carries a citation. Hardware-only facts are marked
UNVERIFIED even when they are widely known; the reviewer can then see exactly
which claims still need a hardware or GBATEK source.

---

## 0. Executive summary

1. **Where to attach.** Snapshot at dump time, not inside the GX write hook:
   `twlhd_gx_rec_serialize()` already passes `params.tex = NULL`
   (`common/twlhd_gx_record.c:170-171`), and the demo dumps after the frame has
   been submitted and the VBlank has passed (`arm9v2/source/main.cpp:120-136`).
   The recorder's GXCM stream already contains every `TEXIMAGE_PARAM`/`PLTT_BASE`
   write (`common/twlhd_v2_format.h:117-118`, `common/twlhd_v2_parse.c:77-78`),
   so the key set can be rebuilt from the frame it just captured.

2. **What can be read.** A DS VRAM bank mapped to texture memory or texture
   palette is **not** in the ARM9 CPU page map (DeSmuME model:
   `MMU.cpp:555-558`, `:591-594`, `:624-630`, `:663-666` against
   `MMU.cpp:443-456`), so the
   recorder cannot read texels out of it while the 3D engine owns it. It can
   read the `VRAMCNT` registers at any time (`MMU.cpp:521-526`;
   `libnds/.../video.h:146`). A bank can be temporarily remapped to LCDC and then
   read, but that steals it from the running game; the side effects are
   UNVERIFIED and potentially severe.

3. **Recommendation.** For our own homebrew ROMs (the fixed scope) capture from
   **the game's own main-RAM source buffers** — zero risk and byte-exact — and use
   **DeSmuME-assisted capture** for validation. Keep the VRAM-remap backend as a
   later, explicitly owner-approved stage. This is developed in §2.4 and §6.

4. **Budget.** The format's own caps are `TXDT ≤ 524,288 B`, `PLDT ≤ 98,304 B`,
   `TXDS ≤ 256 × 40 B` (`common/twlhd_v2_format.h:30-34`). A typical 48-texture
   frame is ~295 KB of texels; the 1 MiB file cap, not the TXDT cap, binds when
   GXCM is large (§3.3). Recommended default capture budget for the demo:
   **TXDT 131,072 B + PLDT 32,768 B**, with the full-cap build reserved for the
   test ROM (§3.6).

5. **Owner decisions** are listed with recommendations in §6.2. The two that
   block code are the capture source (§2.4) and the texture-drop signal (§3.5).

---

## 1. How the existing recorder hooks the GX stream, and where a snapshot attaches

### 1.1 The hook

The recorder is a register-write proxy, backend B (`common/twlhd_gx_record.h:3-18`).
`arm9v2/include/twlhd_gx_shim.hpp` is included before `<nds.h>` and replaces the
write-capable `GFX_*` / `MATRIX_*` port macros with C++ proxy objects
(`arm9v2/include/twlhd_gx_shim.hpp:5-16`). Every assignment performs the real
MMIO write and calls the recorder first:

- scalar port: `twlhd_gx_rec_hook(ADDR, masked, WIDTH)` then
  `TWLHD_SHIM_HW_WRITE` (`arm9v2/include/twlhd_gx_shim.hpp:102-106`);
- array port element: the same pair in `ArrayPort::write()`
  (`arm9v2/include/twlhd_gx_shim.hpp:171-176`);
- `twlhd_gx_rec_hook()` forwards to the process-wide recorder
  (`common/twlhd_gx_record.c:148-150`, instance at `:13`).

The texture ports are covered: `GFX_TEX_FORMAT` is the proxy for `0x040004A8`
and `GFX_PAL_FORMAT` for `0x040004AC`
(`arm9v2/include/twlhd_gx_shim.hpp:222-226`).

**VERIFIED:** there is a second, older capture backend in `arm9/` that wraps
individual libnds primitives by hand (`arm9/source/fifo_hook.c:1-8`) and whose
command table has no `TEXIMAGE_PARAM`/`PLTT_BASE` entries
(`arm9/source/fifo_hook.c:22-45`). Texture capture therefore attaches only to
the v2 proxy recorder; the v1 backend would need its own wrappers and is out of
scope here.

**VERIFIED (libnds headers):** the proxy works only for code that expands these
macros. libnds' inline helpers are proxied because they are `GL_STATIC_INL` in
`<nds/arm9/videoGL.h>` (`videoGL.h:51-52`, inline section starts `:530`), but
the texture functions are ordinary out-of-line library calls: `glTexImage2D`
(`videoGL.h:433`), `glColorTableEXT` (`:442`), `glTexParameter` (`:468`),
`glBindTexture` (`:486`). They were compiled into libnds without this
translation unit's macro replacements, so their internal `GFX_TEX_FORMAT` /
`GFX_PAL_FORMAT` writes cannot be intercepted by the shim. **UNVERIFIED (exact
internal write path):** the libnds source is not installed under
`/opt/devkitpro/libnds`, so the internal MMIO statements were not read; the
conclusion follows from the declarations being out-of-line and from the shim's
own macro-expansion rationale (`arm9v2/include/twlhd_gx_shim.hpp:10-16`). A game
that binds textures only through those calls therefore contributes no
`0x2A`/`0x2B` entries. This is a new coverage gap and must be declared, like the
existing DMA display-list gap (`common/twlhd_gx_record.h:14-15`).

### 1.2 What is recorded per command

One `TwlhdV2CmdEntry` (8 bytes: `cmd_word`, `value`) is appended per **32-bit
port write** (`common/twlhd_gx_record.c:31-42`,
`common/twlhd_v2_format.h:182-186`):

- A write to the command window `0x04000440..0x040005FF` becomes an entry whose
  `cmd_word` is the command id `(addr - 0x04000400) >> 2`; a write to the raw
  FIFO register `0x04000400` becomes an opaque entry with
  `TWLHD_V2_RAW_FIFO_BIT` (`common/twlhd_gx_record.c:95-114`,
  `common/twlhd_v2_format.h:84-91`).
- A write to a whitelisted rendering-engine register becomes a 16-byte
  `TwlhdV2RstaEntry` `{addr, value, gx_index, width}`
  (`common/twlhd_gx_record.c:140-143`, `:44-57`).
- `TEXIMAGE_PARAM` (id `0x2A`) and `PLTT_BASE` (id `0x2B`) are ordinary one-entry
  commands (`common/twlhd_v2_format.h:117-118`,
  `common/twlhd_v2_parse.c:77-78`), so the stream preserves their value and
  their order relative to vertices.

The recorder stores the *writes*, not decoded state. Grouping into commands is
done by the parser/writer (`twlhd_v2_cmd_param_count`,
`common/twlhd_v2_parse.c:50-96`).

### 1.3 Size limits and frame boundary

- Capacities: **16,384 GXCM entries, 256 RSTA entries**
  (`common/twlhd_gx_record.h:28-29`). Overflow sets
  `TWLHD_V2_FLAG_OVERFLOW` and drops the entry
  (`common/twlhd_gx_record.c:26-42`, `:46-50`). A full-cap serialization is
  135,264 bytes (`common/twlhd_gx_record.h:31-43`).
- Frame boundary: command `0x50` `SWAP_BUFFERS` is the boundary. A clean frame
  records it, sets `FRAME_COMPLETE` and disarms; if any drop already happened the
  swap itself is dropped, so every producible file satisfies the parser's
  "`FRAME_COMPLETE` iff the last entry is `SWAP`" rule
  (`common/twlhd_gx_record.c:116-134`; parser rule
  `common/twlhd_v2_parse.c:255-271`).
- The recorder is explicitly **not interrupt-safe**; it must be armed and read
  from thread context (`common/twlhd_gx_record.h:10-12`). The demo arms at the
  top of the loop, draws, waits for VBlank, then optionally dumps
  (`arm9v2/source/main.cpp:120-136`).

### 1.4 Where a texture snapshot attaches

Three candidate points.

| Point | Correctness | Cost / risk | Verdict |
|---|---|---|---|
| At each `0x2A`/`0x2B` write, inside `twlhd_gx_rec_write` | Wrong in general: the game may upload texels after setting the parameter (libnds' `glTexImage2D` "loads a 2D texture into texture memory and sets the currently bound texture ID", `videoGL.h:428-433`), and a mid-frame VRAM remap would run in the game's own thread | A snapshot of up to 128 KB per write runs inside the render loop; the function is not IRQ-safe and has no VRAM abstraction (`common/twlhd_gx_record.c:84-146`) | Reject |
| At `SWAP_BUFFERS`, inside the same function | VRAM is stable and the frame is complete | Still inside the render thread at the end of the frame; any remap here runs while the 3D engine may still be finishing; the recorder has no platform dependency today | Viable later, not first |
| At dump/serialize time, scanning the captured GXCM | The stream is complete; `twlhd_gx_rec_serialize` is already the writer seam (`common/twlhd_gx_record.c:156-174`); the demo calls it after the frame (`arm9v2/source/main.cpp:74`, `:120-136`) | One scan over ≤16,384 entries plus the actual byte copies; no per-frame cost when not dumping | **Recommended** |

The concrete seam is `params.tex = NULL` at
`common/twlhd_gx_record.c:170-171`: the serializer already knows the tex entry
layout (`common/twlhd_v2_write.h:22-31`, `:38-43`) and the writer accepts
`tex_count` up to 256 (`common/twlhd_v2_write.c:65-98`). A later stage replaces
that NULL with a planner-produced array; no format change is needed.

**VERIFIED:** the demo refuses to dump a frame whose flags contain
`TWLHD_V2_FLAG_OVERFLOW` (`arm9v2/source/main.cpp:65-67`). Any policy that uses
that flag for a texture drop must also change this gate, or the file is never
written (§3.5).

**UNVERIFIED:** whether the game can still change VRAM between the `SWAP_BUFFERS`
write and the dump call. The demo waits for VBlank before dumping
(`arm9v2/source/main.cpp:123`), so the just-submitted frame's textures are the
ones in VRAM; a general game could start the next frame's uploads immediately.
A dump-time snapshot is therefore a *point-in-time* sample, not a per-draw
guarantee.

---

## 2. DS texture and palette memory facts, and what the ARM9 can capture

### 2.1 `TEXIMAGE_PARAM` and `PLTT_BASE`

Command ids and ports: `GFX3D_TEXIMAGE_PARAM = 0x2A` / `GFX3D_PLTT_BASE = 0x2B`
(`desmume/src/gfx3d.h:57-58`), I/O words at `0x040004A8` / `0x040004AC`
(`desmume/src/gfx3d.h:508-509`). Both latch immediately in their handlers
(`desmume/src/gfx3d.cpp:1375-1395`); the repo research reaches the same
conclusion at `docs/DS_GX_RESEARCH.md:267-271`. The interpreter latches them at
`common/twlhd_gx_interp.c:935-940`, and the per-vertex record is
`TwlhdGxTexVertex` (`common/twlhd_gx_interp.h:28-33`).

Bit layout, little-endian branch (`desmume/src/gfx3d.h:289-358`), matching
`docs/DS_GX_RESEARCH.md:273-287`:

| Bits | Field | Meaning |
|---:|---|---|
| 0-15 | `VRAMOffset` | texture data base in 8-byte units; address = `VRAMOffset << 3` |
| 16-19 | wrap / mirror bits | repeat and flip S/T |
| 20-22 | `SizeShiftS` | width = `8 << N` (8..1024) |
| 23-25 | `SizeShiftT` | height = `8 << N` (8..1024) |
| 26-28 | `PackedFormat` | 0 none, 1 A3I5, 2 I2, 3 I4, 4 I8, 5 4x4, 6 A5I3, 7 16bpp |
| 29 | `KeyColor0_Enable` | palette index 0 transparent (I2/I4/I8 only) |
| 30-31 | `TexCoordTransformMode` | 0 none, 1 texcoord, 2 normal, 3 vertex |

**VERIFIED (DeSmuME model):** `PLTT_BASE` is a raw `u32`
(`desmume/src/gfx3d.cpp:1392-1395`) used without masking. The palette byte
address is `PLTT_BASE << 3` for I2 and `PLTT_BASE << 4` for every other format
(`desmume/src/texcache.cpp:438`). The task's "16-byte units, 8-byte for
format 2" is therefore exact.

**VERIFIED (DeSmuME model):** sizes and bytes per texel come from
`texSizes[] = {0,4,1,2,4,1,4,8}` (fixed point /4) and
`paletteSizeList[] = {0,32,4,16,256,0,8,0}`
(`desmume/src/texcache.cpp:414-418`), with
`_packSize = width*height*texSizes[format] >> 2`
(`desmume/src/texcache.cpp:428-429`) and
`_paletteSize = paletteSizeList[format] * 2`
(`desmume/src/texcache.cpp:441`). The repository re-implements the same formula
in one shared helper used by both parser and writer:
`twlhd_v2_tex_sizes()` (`common/twlhd_v2_parse.c:134-157`,
`common/twlhd_v2_format.h:154-180`). Format 5's texel byte count is
`width*height/4`; its palette has **no fixed table** (`paletteSizeList[5] = 0`)
and is a caller-chosen window, validated by
`twlhd_v2_tex_palette_size_ok()` (`common/twlhd_v2_parse.c:160-178`).

**VERIFIED (DeSmuME model):** for format 5 the byte data and the index block are
separate reads: `indexBase = 0x20000` or `0x30000` depending on
`(VRAMOffset & 0xC000) == 0x8000`, and
`indexOffset = (VRAMOffset & 0x3FFF) << 2`
(`desmume/src/texcache.cpp:443-446`); index size is `width*height >> 3`. In slot
terms, texture slots 0/2 hold the texels and slots 1/3 hold the index block — the
split the task asks about.

### 2.2 VRAM bank and slot mapping

**VERIFIED (DeSmuME model):** DS-mode banks and sizes are A-D = 8 pages of
16 KiB = 128 KiB each, E = 4 pages = 64 KiB, F = 1 page = 16 KiB, G = 1 page =
16 KiB (`desmume/src/MMU.cpp:309-319`; bank ids `desmume/src/MMU.h:574-584`).
Banks H/I are DSi-only and irrelevant to a DS capture.

**VERIFIED (DeSmuME model):** the mapping is programmed through one byte per
bank, at `0x04000240 + bank` for banks A-G
(`desmume/src/MMU.cpp:521-526`; the libnds aliases are
`VRAM_A_CR`..`VRAM_G_CR`, `/opt/devkitpro/libnds/include/nds/arm9/video.h:146-153`).

- Banks A/B, mode field `MST = 3`: the bank becomes a **texture slot** selected
  by `OFS`: `MMU.texInfo.textureSlotAddr[OFS]`
  (`desmume/src/MMU.cpp:555-558`). Banks C/D do the same
  (`desmume/src/MMU.cpp:591-594`). So A-D → texture slots 0-3, 128 KiB each, and
  the libnds enum offers exactly `VRAM_A_TEXTURE_SLOT0..3`
  (`video.h:169-173`).
- Bank E, `MST = 3` (texture palette): it fills palette slots 0-3, four 16 KiB
  slots (`desmume/src/MMU.cpp:624-630`).
- Banks F/G, `MST = 3` (texture palette): each fills one 16 KiB palette slot;
  DeSmuME's `pageofslut[] = {0,1,4,5}` maps the bank's `OFS` to a slot index
  (`desmume/src/MMU.cpp:663-666`). The common E + F/G configuration therefore
  gives six 16 KiB palette slots, matching
  `common/twlhd_v2_format.h:32-34`. **UNVERIFIED:** the exact GBATEK encoding of
  F/G `OFS` in texture-palette mode; DeSmuME is the only source read here.
- Texture image addressing within slots: `slot = (ofs >> 17) & 3`; a block wraps
  across slots; the slot pointer plus `ofs & 0x1FFFF`
  (`desmume/src/texcache.cpp:144-165`).
- Palette addressing within slots: `slot = (ofs >> 14) & 7`; slot values 6-7
  have 5 subtracted, i.e. they wrap into slots 1-2
  (`desmume/src/texcache.cpp:169-197`). A recorder must reproduce this wrap or
  refuse the texture; the file palette window does not wrap
  (`docs/TWLHD_V2_FORMAT.md:344-349`).

### 2.3 What the ARM9 can read, and when

**VERIFIED (DeSmuME model):** the CPU page map `vram_arm9_map` is filled only
for LCDC / A-BG / A-OBJ mappings (`desmume/src/MMU.cpp:504-511`, calls at
`:537`, `:542`, `:549`, `:569`, `:574`, `:618`, `:622`, `:655`, `:660`). A
texture-mapped bank sets only
`MMU.texInfo.textureSlotAddr[...]` (or `texPalSlot[...]`) — `:555-558`,
`:591-594`, `:624-630`, `:663-666` — and never appears in `vram_arm9_map`. CPU
reads of the `0x06000000` region resolve through `vram_arm9_map`
(`desmume/src/MMU.cpp:443-456`), so an unmapped page reads as blank. The physical
backing is the same array (`desmume/src/MMU.cpp:513-516`), and an LCDC mapping is
an identity page map (`desmume/src/MMU.cpp:494-502`), so **contents survive a
remap**.

**VERIFIED:** the `VRAMCNT` registers are CPU-readable at any time
(`desmume/src/MMU.cpp:526`; `libnds/.../video.h:146-153`; `vramSetBankA()` writes
`VRAM_A_CR`, `video.h:372`). The recorder can therefore learn the bank/slot
configuration at dump time even though the shim never captures those writes.

**UNVERIFIED (hardware):** that a texture-mapped bank is unreadable on silicon in
exactly the emulator's way. GBATEK's VRAM control chapter is the source to check;
it was not consulted. The practical conclusion (a bank must be remapped to
LCDC/BG/OBJ before the CPU can read it) is the standard DS programming model and
is consistent with libnds exposing only `VRAM_A_LCD` (`= 0`,
`video.h:160`) and `VRAM_A_TEXTURE` (`= 3`, `video.h:169`) as *different*
mappings.

**When:** the 3D engine samples textures during rasterisation of the submitted
frame, which is complete at `SWAP_BUFFERS` (`common/twlhd_gx_record.c:116-134`).
A dump-time read after VBlank is therefore ordering-safe for the frame just
captured; it is not safe against a game that starts the next frame's uploads
before the dump call.

**Common homebrew/game patterns (VERIFIED for the API surface, UNVERIFIED for
behaviour):** a libnds app maps a bank with `vramSetBankA(VRAM_A_TEXTURE)` or one
of the slot-specific values (`libnds/.../video.h:169-173`, `:372`), asks the
library for texture names (`glGenTextures`, `videoGL.h:492`), uploads with
`glTexImage2D` (`videoGL.h:433`) and binds with `glBindTexture`
(`videoGL.h:486`); the upload path inside libnds is not installed here, so
whether it uses DMA or CPU stores is **UNVERIFIED**. Games that do not fit a
single slot commonly keep the packed texture in main RAM and re-upload, or
allocate their own slot with `VRAM_A_TEXTURE_SLOTn`. libnds' `glGetTexturePointer`
(`videoGL.h:480-482`) is documented as returning the address allocated to a
texture name; whether that address is CPU-dereferenceable on hardware is
**UNVERIFIED** and must not be treated as a readback path.

### 2.4 Capture options

| Option | What it captures | Correctness | Risk to the running game | Cost / complexity | Recommendation |
|---|---|---|---|---|---|
| **(a) Remap the bank to LCDC, read, restore** | Exact VRAM bytes for every mapped slot | High *if* the mapping is read from `VRAMCNT` first and the remap/restore is atomic | **Critical.** The bank leaves the 3D engine's slot; if anything samples or displays it before restore, the frame is wrong or the visible screen glitches. A remap that aliases an existing LCDC bank can break the 2D display. Must run in VBlank with IRQs off; 512 KiB cannot be read in one VBlank (§2.5) | Highest: mapping discovery, per-bank save/restore, timing, failure recovery | **Last**, behind explicit owner approval |
| **(b) Snoop the DMA/CPU writes that fill VRAM** | Only writes the recorder can intercept | Partial: misses game-built/decompressed textures, mid-frame rewrites, and direct CPU stores unless every path is hooked | None (observe only) | High: needs DMA-register hooks or libnds call wrapping; the shim covers only the GX ports and deliberately leaves read-only ports and the packed FIFO macros alone (`arm9v2/include/twlhd_gx_shim.hpp:22-29`) | Not first; only if third-party ROMs ever enter scope |
| **(c) Read the game's main-RAM source buffers** | Exactly the bytes the ROM uploaded | Byte-exact for our own ROMs | None | Low: the demo owns the texture arrays | **Primary for our own ROMs on hardware** |
| **(d) Emulator-assisted capture (DeSmuME internals / patched build)** | Exactly what the emulator's 3D engine reads | Exact for the emulator model; validates the decoder, not silicon | None | Medium: a local, non-vendored DeSmuME build or debug hook; provenance needs a declared coverage/flag extension (owner approval) | **Primary for validation** |

Why (c) is legitimate here: the scope is explicitly "our own homebrew ROMs"
(`docs/PHASE5B_PLAN.md:10-14`), and a demo texture is an array the ROM itself
provides before it uploads it. It does not exercise the VRAM read path, so it
cannot catch a slot or palette-addressing mistake; that is exactly what (d) and,
later, (a) add.

Why (a) is dangerous in detail:

1. The recorder cannot know from its own stream which bank is in which slot; it
   must read `VRAMCNT` at dump time (`desmume/src/MMU.cpp:521-526`). A mapping
   change *during* the captured frame is invisible, so the read may resolve the
   wrong bank.
2. Remapping A/B/C/D from `MST = 3` to `MST = 0` removes the slot pointer from
   the 3D engine (`desmume/src/MMU.cpp:555-558` vs `:535-539`). DeSmuME's
   `MemSpan_TexMem` reports "Tried to reference unmapped texture memory"
   (`desmume/src/texcache.cpp:152-162`); hardware behaviour is UNVERIFIED and may
   be worse (stale or blank texture, or a bus stall).
3. The new LCDC mapping competes with whatever was already mapped at the same
   `0x06000000` window; the emulator's page map has a single winner per page
   (`desmume/src/MMU.cpp:443-456`), so the 2D display can be corrupted.
4. The remap must be restored before the next frame; an exception or an
   interrupt between the two writes leaves the game with a stolen bank.
5. `VRAMCNT` writes are not proxied by the shim, so the recorder would also be
   mutating state it does not track.

None of this is acceptable by default for a recorder that is supposed to observe
a game. It is defensible only for our own ROM, with the owner's explicit
approval, and with a restore-in-`finally` style guard.

### 2.5 Cost estimate for the remap option

**UNVERIFIED (derived estimate):** one 128 KiB bank is 65,536 16-bit reads. At an
assumed ~4 ARM9 cycles per VRAM read at 67 MHz that is ~262k cycles ≈ **3.9 ms**
per bank; four banks ≈ **16 ms**, plus palette banks. A 60 Hz frame is 16.7 ms,
so a full four-bank capture cannot fit in one VBlank. The estimate depends on the
real VRAM wait states and on libnds' generated access width; measuring it on
hardware is required before committing to (a). The design consequence is stable:
**(a) needs either a multi-frame capture policy or a per-texture budget far below
the full 512 KiB**, which further weakens it against (c)/(d).

---

## 3. Capture policy, deduplication and budget

### 3.1 Key formation

The only key the format can express is the exact pair
(`teximage_param`, `pltt_base`) (`docs/TWLHD_V2_FORMAT.md:272-273`, `:296`). A
planner therefore walks the captured GXCM once and tracks the current
`TEXIMAGE_PARAM` and `PLTT_BASE`:

1. Start with `param = 0`, `pltt = 0`.
2. On `0x2A`, set `param`; on `0x2B`, set `pltt` (the interpreter's latch at
   `common/twlhd_gx_interp.c:935-940` is the same state machine).
3. The precise "use" point is a VTX command inside a `BEGIN` group: the state at
   that moment is what the polygon samples. Record the pair only when
   `(param >> 26) & 7 != 0` (format 0 is untextured) and only on the first VTX
   of each distinct state within the group.
4. Deduplicate globally in the frame: the second occurrence of a pair reuses the
   first record's bytes.

Step 3 is better than "record at every `0x2A`/`0x2B`": a game commonly sets a
texture, binds a palette, and then draws; recording at the parameter write would
snapshot stale VRAM or an incomplete pair, and it would also capture states that
are never drawn. It also handles palette-before-format ordering. The cost is one
pass over at most 16,384 entries.

**Same-key, different-bytes limitation.** If a frame reuses one
(`param`, `pltt`) pair for two different small textures (same slot and format,
VRAM rewritten in between), the key collides. The format has no way to express two
payloads for one key, and the parser rejects duplicate keys
(`common/twlhd_v2_parse.c:355-366`, `docs/TWLHD_V2_FORMAT.md:296`). Policy: the
**first snapshot wins**; the planner must document the loss and count it. A
content hash could detect the collision, but it cannot repair it without a
format extension; that is an owner decision (§6.2, D7).

**Coverage caveat.** Because the out-of-line libnds texture calls bypass the shim
(§1.1), a ROM that binds textures only through them contributes no keys. The
capture must declare that gap; a future `TWLHD_V2_COVERAGE_*` bit is a format
change and needs approval (`common/twlhd_v2_format.h:73-77` has three bits and a
reserved mask). The demo can avoid the gap by writing
`GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` through the shim explicitly.

### 3.2 Deduplication scope

Dedupe is **within one dumped frame**. The file has no cross-frame reference:
each `TXDS` record's offsets point into *that file's* `TXDT`/`PLDT` only
(`docs/TWLHD_V2_FORMAT.md:279-306`), and each file is one frame
(`docs/TWLHD_V2_FORMAT.md:263-275`). The current pipeline dumps one frame per
file (`arm9v2/source/main.cpp:59-110`), so re-emitting every distinct key per
dump is correct and needs no extension. An unchanged texture across frames is
not referenced; it is re-sent in each file. A persistent cross-frame cache is a
**format extension** and an owner decision (§6.2, D2). The recommendation is not
to extend now: the offline single-frame workflow gains nothing from it.

### 3.3 Budget arithmetic

Format limits (`common/twlhd_v2_format.h:30-34`,
`docs/TWLHD_V2_FORMAT.md:555-570`):

| Item | Cap |
|---|---:|
| File | 1,048,576 B |
| `TXDS` | 256 × 40 = 10,240 B |
| `TXDT` | 524,288 B (= 4 × 128 KiB) |
| `PLDT` | 98,304 B (= 6 × 16 KiB) |
| `GXCM` payload | 65,536 × 8 = 524,288 B |
| `RSTA` payload | 4,096 × 16 = 65,536 B |

The caps cannot all be reached in one file. Fixed overhead is 32 (header) +
6 × 16 (chunk headers) + 16 (META payload) = 144 B. With the maximum GXCM, RSTA,
TXDS and PLDT, the TXDT budget is:

```
1,048,576 - (144 + 524,288 + 65,536 + 10,240 + 98,304) = 350,064 B
```

so **the 1 MiB file cap, not the 524,288-byte TXDT cap, is the real limit for a
command-heavy frame**. With the demo recorder's smaller GXCM/RSTA caps
(16,384 / 256), the fixed part is 144 + 131,072 + 4,096 + 10,240 + 98,304 =
243,856 B, leaving 804,720 B, and the TXDT cap binds: a full-cap file is
768,144 B.

Representative texel sizes from the shared formula
(`common/twlhd_v2_parse.c:134-157`):

| Texture | Texel bytes | Fits in `TXDT` |
|---|---:|---|
| 8×8 I4 | 32 | many |
| 64×64 I4 | 2,048 | 256 (but TXDS caps at 256 records) |
| 64×64 I8 / A3I5 / A5I3 | 4,096 | 128 |
| 64×64 16bpp | 8,192 | 64 |
| 128×128 16bpp | 32,768 | 16 |
| 256×256 I4 | 32,768 | 16 |
| 256×256 16bpp | 131,072 | 4 |
| 512×512 16bpp | 524,288 | 1 (fills `TXDT`) |
| 1024×1024 I4 | 524,288 | 1 (fills `TXDT`) |
| 1024×1024 16bpp | 2,097,152 | **0 — cannot be captured** |
| 1024×1024 4x4 | 262,144 texel + 131,072 index = 393,216 | 1 |

**Typical scene** (the numbers below are a worked example, not a measurement):
48 distinct textures — 24 × 64×64 I4, 12 × 64×64 I8, 8 × 64×64 16bpp, 4 ×
128×128 16bpp. Texels: 24×2,048 + 12×4,096 + 8×8,192 + 4×32,768 = 49,152 +
49,152 + 65,536 + 131,072 = **294,912 B**. Palettes: 24×32 + 12×512 = 768 + 6,144
= **6,912 B**. `TXDS`: 48 × 40 = **1,920 B**. Total texture data ≈ **303,744 B**,
comfortably inside both caps and inside the 1 MiB file with a 16,384-entry GXCM.
The fixed GXCM/META/RSTA/TXDS/header part is 135,264 (serialize max) + 3 × 16
(TXDS/TXDT/PLDT chunk headers) + 1,920 (TXDS) = 137,232 B, so the file is
137,232 + 294,912 + 6,912 = **439,056 B** (≈429 KiB).

**Worst case that still parses:** 256 records, `TXDT` exactly 524,288 B,
`PLDT` 98,304 B, file 768,144 B with demo GXCM caps. A scene that needs more
must drop records (below).

**Palette budget.** Fixed palettes are small: 64/8/32/512/16 bytes per record
(`common/twlhd_v2_format.h:154-180`; `docs/TWLHD_V2_FORMAT.md:314-322`). Format 5
is the exception: a full window is 65,540 B
(`common/twlhd_v2_format.h:36-47`), so **one** full format-5 texture nearly fills
`PLDT` and **two full windows (131,080 B) do not fit** (known limit,
`docs/TWLHD_V2_FORMAT.md:588-593`). The recorder can do much better than the full
window: for every 16-bit index entry (bits 0-13 palette offset, bits 14-15 mode),
the palette byte offset used is `(entry & 0x3FFF) * 4 + 2*k`, k = 0..3
(`desmume/src/texcache.cpp:1791-1801`; derivation in
`docs/TWLHD_V2_FORMAT.md:330-349`). The entry is one per 4x4 block — the index
block is `width*height/8` bytes, i.e. 2 bytes per block — and the host consumer
reads it with `frame_read_le16(run->index + block*2)`
(`common/twlhd_pica_frame.c:185-189`), not from the 32-bit texel word. Taking
`m = max(entry & 0x3FFF)` over the captured index block, the minimal even window
is `4*m + 8` bytes (rounded up to even), which for typical palettes is tens of
bytes. That makes several format-5 textures per frame feasible; the policy is D4
in §6.2.

### 3.4 What "acceptable per frame" means here

Because the snapshot happens at dump time (§1.4), there is **no per-rendered-frame
cost** while capture is armed: the GXCM stream is recorded every frame, and the
texture bytes are copied only when the user dumps. The budget is therefore per
*dumped* frame. Continuous capture would be different: 128 KiB × 60 fps =
7.5 MB/s, far above any plausible libfat/SD throughput (UNVERIFIED estimate) and
outside the current offline workflow. The recommendation is to keep the offline
dump model and set the default budget at **TXDT 131,072 B + PLDT 32,768 B**:
enough for 64 × 64×64 I4 textures or 16 × 64×64 16bpp, and 131,072 + 32,768 +
10,240 + headers ≈ 174 KB of texture chunks. A full-cap build stays available for
the test ROM.

### 3.5 When a frame exceeds the limits — required fallback

The writer is all-or-nothing: it returns `ERR_TXDT`/`ERR_PLDT`/`ERR_SIZE` and
writes nothing valid (`common/twlhd_v2_write.c:107-109`, `:127`,
`:245-250`). The recorder must therefore make the decision **before** calling it.

Proposed policy (deterministic, no partial blocks):

1. Build the full key list in stream order (§3.1).
2. Walk it in that order and add a record only if its texel+index bytes fit the
   remaining `TXDT` budget, its palette bytes fit the remaining `PLDT` budget
   (including the worst-case 3 pad bytes), and the resulting file fits 1 MiB.
3. A record that does not fit is dropped **whole**; its texel block is never
   truncated. Continue with later, smaller records (a deterministic first-fit
   order, so the same input always yields the same file).
4. Set `TWLHD_V2_FLAG_OVERFLOW` and count the drops, mirroring the GXCM overflow
   rule (`common/twlhd_gx_record.c:26-29`, `:116-128`).
5. If no record fits, omit `TXDS`/`TXDT`/`PLDT` entirely (`tex_count = 0`,
   `common/twlhd_v2_write.c:177-243` only emits them for `tex_count != 0`). The
   file remains a valid untextured capture; the drop count says why.

**Conflict to resolve before code:** the demo refuses any dump whose flags carry
`OVERFLOW` (`arm9v2/source/main.cpp:65-67`). Reusing the existing flag is the only
way to signal a texture drop without a format change, so the gate must be relaxed
for texture-only drops (allow a complete GXCM with dropped textures, and report
the counts). That is a behaviour change and an owner decision (D3). The
alternative — a new flag bit — is a format extension and needs approval under
`docs/PHASE5B_PLAN.md:684`.

**UNVERIFIED:** whether the parser will accept a file with `OVERFLOW` set and a
clean `SWAP` last entry. The parser's frame rule only couples `FRAME_COMPLETE`
to the last entry (`common/twlhd_v2_parse.c:255-271`); `OVERFLOW` is a separate
bit (`common/twlhd_v2_format.h:64-67`), so it should. This is worth one host test
in stage 5C-1.

### 3.6 Serialize-buffer consequence

`TWLHD_GX_REC_SERIALIZE_MAX` is 135,264 B today
(`common/twlhd_gx_record.h:31-43`), and the demo's static buffer is sized from it
(`arm9v2/source/main.cpp:30-31`). Adding the maximum texture chunks makes the
worst case:

```
135,264 + 16 + 10,240 (TXDS) + 16 + 524,288 (TXDT) + 16 + 98,304 (PLDT) = 768,144 B
```

That is a 5.7× increase in static RAM. It is feasible on the DS (the demo's
comment assumes ~270 KB total today) but wasteful for a normal build. The
planner's budget (§3.4) bounds it: with 131,072 + 32,768 the buffer is
135,264 + 48 + 10,240 + 131,072 + 32,768 = **309,392 B**. The recommendation is a
compile-time budget define, with the full-cap value used only by the test ROM
(D5). This is exactly the "serialize-budget redesign" risk flagged in the task
contract; it is quantified here rather than hand-waved.

---

## 4. What the writer and the host consumer need from the recorder

### 4.1 Writer input

The recorder must produce one `TwlhdV2TexEntry` per distinct key
(`common/twlhd_v2_write.h:22-31`):

| Field | Value the planner supplies |
|---|---|
| `teximage_param` | the captured `0x2A` word (bits 20-28 size/format) |
| `pltt_base` | the captured `0x2B` word |
| `texel`, `texel_bytes` | contiguous texel block, exactly `twlhd_v2_tex_sizes()` bytes |
| `palette`, `palette_bytes` | fixed table for 1/2/3/4/6, window for 5, 0 for 7 |
| `index`, `index_bytes` | 4x4 only: `width*height/8` bytes from the separate index base |

The writer re-validates every field: sizes via the shared helpers
(`common/twlhd_v2_write.c:75-89`), null pointers for nonzero sizes (`:85-89`),
duplicate keys (`:90-95`), `TXDS` count ≤ 256 (`:65-67`), `PLDT` pad and cap
(`:100-109`), total file size (`:111-127`), and finally re-parses its own output
(`:245-250`). The recorder does not need to duplicate those checks, but it must
not rely on partial writes: a rejected writer call produces no usable file.

### 4.2 Record layout, alignment, key and window rules

- `TXDS` is `N × 40` bytes, `N` = 1..256, all fields `u32`
  (`docs/TWLHD_V2_FORMAT.md:277-292`; `common/twlhd_v2_format.h:30-31`).
- `texel_offset` and `palette_offset` are **file-relative to `TXDT`/`PLDT`**,
  not VRAM addresses (`docs/TWLHD_V2_FORMAT.md:285-289`). `index_offset =
  texel_offset + texel_size` for a 4x4 record (`:351-356`).
- `TXDT` holds each record's texel block followed by its index block, in `TXDS`
  order (`docs/TWLHD_V2_FORMAT.md:351-356`).
- Every chunk payload must be a multiple of 4 (`docs/TWLHD_V2_FORMAT.md:393`,
  validation step 8). `TXDS` (40 N) and every texel/index block are multiples of
  4; a format-5 palette window is only even, so the writer zero-pads the
  `PLDT` payload by 1..3 bytes and applies the 98,304 cap to the padded length
  (`common/twlhd_v2_write.c:100-109`, `:226-242`;
  `docs/TWLHD_V2_FORMAT.md:357-363`). The padding is never part of a record.
- Key uniqueness is exact-pair uniqueness across records, enforced by both
  writer and parser (`common/twlhd_v2_write.c:90-95`,
  `common/twlhd_v2_parse.c:355-366`).
- Format 5: the palette block is an even window in `[2, 65,540]` bytes, starting
  at byte `PLTT_BASE << 4`, with **no start field** in the record
  (`docs/TWLHD_V2_FORMAT.md:288`, `:330-349`;
  `common/twlhd_v2_format.h:36-47`). The first captured byte must therefore be
  the palette byte at that address; the recorder must not trim from the front.
- The host consumer resolves a run's `(param, pltt)` against the `TXDS` list
  (`common/twlhd_pica_frame.c:54-71`), requires a palette for every format except
  7 and an index block for format 5 (`common/twlhd_pica_frame.c:145-150`), then
  checks that the palette window is legal or the fixed table fully present
  (`common/twlhd_pica_frame.c:168-180`), and caps distinct textures at the
  format's 256 (`common/twlhd_pica_frame.h:94`).

### 4.3 Test coverage that already exists

| Area | Test | Notes |
|---|---|---|
| Writer, two-record I2 golden | `tests/host/test_v2_write.c:51-54`, `:116-118`, `:318-361` | byte-exact file, `TXDS`/`TXDT`/`PLDT` |
| Writer, all seven formats | `tests/host/test_v2_write.c:363-452` | size formula per format |
| Writer, format-5 golden | `tests/host/test_v2_write.c:560-597` | texel + index + palette window |
| Writer error paths | `tests/host/test_v2_write.c:599-662` | includes `ERR_TXDS_SIZE` and `ERR_TXDS_KEY` at `:651` |
| Writer `PLDT` cap | `tests/host/test_v2_write.c:693-732` | two full windows rejected |
| Parser, texture chunks | `tests/host/test_v2_parse.c:843-1362` | two I2 records, accessors, bounds, limits, lax layout, format-5 golden |
| Host consumer | `tests/host/test_pica_frame.c:33-37`, `:119-128` | formats 4/6/5, texture reuse |
| Writer self-check | `common/twlhd_v2_write.c:245-250` | every writer test also runs the parser |
| **Recorder → texture chunks** | **none** | `tests/host/test_gx_record.c` has no texture constructs; `params.tex = NULL` at `common/twlhd_gx_record.c:170-171` |
| Planner, dedupe, budget, readback | **none** | to be added in 5C-1 |

`make test` runs the C host tests plus the Python suite
(`Makefile:67-104`, `:513-517`). The Python mirror still treats the three texture
FourCCs as unknown optional chunks
(`tools/twlhd_v2_dump.py:85-87`; recorded in `docs/TWLHD_V2_FORMAT.md:99-103`,
`:452-458`). Extending it is a separate task and is **not** part of this
document's scope.

---

## 5. Test strategy without hardware

### 5.1 Pure-C capture planner in `common/` (BUILD + UNIT)

Proposed new module, no hardware dependency:

- `common/twlhd_tex_capture.[ch]`
  - fixed key table of `TWLHD_V2_TXDS_MAX_RECORDS` (256) entries; no malloc;
  - fields: key, byte counts, budget counters, drop count, overflow flag;
  - a read callback seam,
    `int (*read)(void *ctx, uint32_t vram_offset, uint8_t *dst, uint32_t len)`,
    so the planner never touches MMIO and host tests supply a mock VRAM array;
  - `twlhd_tex_cap_plan(cap, gxcm, count)` builds keys from the stream (§3.1);
  - `twlhd_tex_cap_emit(cap, entries, max, &n)` returns `TwlhdV2TexEntry`s with
    caller-owned byte buffers, applying the budget and the whole-record drop
    policy (§3.5).
- A DS backend (`arm9v2`) implements the read callback; the first version reads
  from the game's own source arrays (option c), a later one from VRAM (option a).
- The planner must be deterministic: same GXCM + same VRAM bytes → byte-identical
  entries and identical drop set.

Host tests (`tests/host/test_tex_capture.c`), wired like the existing tests
(`Makefile:67-104`, build-rule pattern `:123-140`):

1. key formation: parameter before/after palette, mid-`BEGIN` changes, format 0
   skipped, duplicate pair deduped;
2. `TEXIMAGE_PARAM` value copied exactly (bits 16-19 preserved);
3. 4x4 texel + index pointers from the two VRAM regions;
4. slot wrap for a texture that crosses a 128 KiB boundary
   (`desmume/src/texcache.cpp:144-165`);
5. palette wrap / out-of-window behaviour
   (`desmume/src/texcache.cpp:169-197`);
6. budget boundary: exact fit, one byte over, many small vs one large record;
7. PLDT pad: a `2 mod 4` window, cap applied to the padded length;
8. overflow: `OVERFLOW` set, drop count correct, file still parses;
9. no partial block and deterministic first-fit order;
10. format-5 minimal-window arithmetic (`4*max + 8`, even).

Plus the repository's established verification pattern: an independent Python
oracle and a mutation script with a report, as for 5B-2c..2e2
(`docs/reports/5b2c-mutation-report.md`,
`docs/reports/5b2e1_mutate_pica_frame.py`,
`docs/reports/5b2e2_mutate_tex_draw_plan.py`). The mutation set should hit every
constant that matters: 40, 524,288, 98,304, 65,540, the 4x4 index shift, the
format-5 window multiplier.

### 5.2 DeSmuME-assisted validation (EMULATOR)

The existing toolchain:

- `tools/emulator/desmume_capture.sh <rom.nds> <outdir>` boots an autodump ROM,
  hands it a FAT image as Slot-2 CFlash, stops the emulator with SIGINT so libfat
  flushes, and extracts `twlhd_frame_v2.bin` (`tools/emulator/desmume_capture.sh:1-16`,
  `:44-50`). Requirements: `DESMUME_CLI`, `mkfs.fat`, `7z`, `stdbuf`, `python3`.
- `tools/emulator/compare_capture.py` compares GXCM and RSTA against a
  host-generated expected stream (`tools/emulator/compare_capture.py:1-20`).
- `make emu-validate` builds a separate autodump ROM and runs the chain
  (`docs/EMULATOR_VALIDATION.md:89-95`); it is never part of `all`/`test`.

Proposed extension (a later stage, not this docs task):

1. **Textured test ROM.** Add one scene to the existing demo (new `SCENE=5` or a
   `TEXTURE=1` define) in `arm9v2/source/demo_frame.cpp`. It uploads a known
   array — e.g. one 8×8 I4 texture (32 texel bytes) and a 16-entry palette
   (32 bytes), or one 8×8 16bpp texture (128 bytes) — to a bank mapped
   `VRAM_A_TEXTURE` (`libnds/.../video.h:169`), then draws one textured quad.
   `glTexImage2D` may do the upload; the draw must set
   `GFX_TEX_FORMAT`/`GFX_PAL_FORMAT` through the shim so the keys are recorded
   (§1.1). The existing demo has no texture calls at all, so this is new scene
   code and needs the approval at `docs/PHASE5B_PLAN.md:687`.
2. **Capture.** Run `desmume_capture.sh` on the autodump build; parse the file
   with the C parser and compare `TXDS`/`TXDT`/`PLDT` bytes with the known array.
3. **Comparator.** `compare_capture.py` currently ignores texture state; extend
   it to require: exactly one `TXDS` record; key equals the written
   `TEXIMAGE_PARAM`/`PLTT_BASE`; `TXDT` equals the source texels; `PLDT` equals
   the source palette. This needs `tools/twlhd_v2_dump.py` to learn the three
   chunks first, which is the separate Python-mirror task
   (`docs/TWLHD_V2_FORMAT.md:452-458`).
4. **Second check.** Compare the recorder's captured bytes with the emulator's
   own VRAM contents at dump time (a DeSmuME debug dump or the known source
   array), so a slot-addressing bug is separated from a writer bug.

Toolchain, checked this session:

- `/opt/devkitpro/devkitARM` exists and `/opt/devkitpro/libnds` is libnds 2.0.0
  (`libversion.h:5-9`). `arm9v2/Makefile:6-18` requires `DEVKITARM` and
  `DEVKITPRO` and the libnds header; all are present.
- No prebuilt `.nds` is in the tree (`arm9v2/*.nds` matches nothing), so the ROM
  must be built locally — a normal `make -C arm9v2` once the environment is set.
- The demo ROM source is reused: `arm9v2/source/demo_frame.cpp` already owns the
  scene and vertex arrays (`:21-51`, `:123-154`); a textured scene adds the
  texture and palette arrays plus the explicit texture port write.

What this validates and what it does not:

- validates the planner, the record writer, the slot-addressing arithmetic, and
  the C/Python parsers on real ARM9 code;
- does **not** validate VRAM readability, the remap side effects, DMA timing, or
  libfat on real hardware;
- DeSmuME is a model: a PASS is EMULATOR evidence only
  (`docs/EMULATOR_VALIDATION.md:9-14`).

### 5.3 Hardware-only items

These cannot be settled by host tests or an emulator:

1. whether a texture-mapped bank is really unreadable by the ARM9, and whether a
   temporary LCDC remap preserves contents and does not disturb the 3D/2D engines;
2. the real cost of the remap/read (wait states, cache, DMA interference) and
   whether it fits a VBlank (§2.5 is an estimate);
3. DMA/CPU fill patterns and when VRAM is stable relative to `SWAP_BUFFERS`;
4. palette-slot wrap across E/F/G on silicon (DeSmuME's `pageofslut` is a model,
   §2.2);
5. `VRAMCNT` state after libnds/`glInit()` on a real console;
6. libfat/SD throughput for a ~300 KB dump (UNVERIFIED estimate).

---

## 6. Staged implementation plan

### 6.1 Stages (smallest safe first)

| Stage | Scope | Files | Tests | Labels | Risk | Approvals |
|---|---|---|---|---|---|---|
| **5C-0** | This design study and the owner decisions D1-D7 | `docs/TEXTURE_CAPTURE_DESIGN.md`, one line in `docs/PHASE5B_PLAN.md` | none (documentation) | none (docs) | none | none for the doc; decisions are the owner's |
| **5C-1** | Pure-C planner: key table, dedupe, budget, whole-record drop, format-5 minimal window, serialization-budget define | new `common/twlhd_tex_capture.[ch]`, `tests/host/test_tex_capture.c`, `Makefile` test wiring, Python oracle + mutation script | UNIT: `make test` (host + oracle + mutation report) | BUILD/UNIT | Low: no recorder wiring, no hardware path | Stage approval (`docs/PHASE5B_PLAN.md:683`) |
| **5C-2** | Recorder wiring: fill `params.tex` from the planner; **source-buffer backend (option c)** for the demo; grow/resize the serialize buffer with the compile-time budget; textured demo scene; relax the dump gate for texture drops | `common/twlhd_gx_record.[ch]`, `arm9v2/source/main.cpp`, `arm9v2/source/demo_frame.cpp`, `arm9v2/include/twlhd_gx_shim.hpp` only if needed | UNIT: `make test` including a host test that drives recorder GXCM + mock source buffers to a parsed texture file; BUILD: `make arm9v2` | BUILD/UNIT | Medium: buffer and dump behaviour change; the shim gap is documented, not fixed | Stage approval; "capture-format extension" only if D3/D7 choose one; "new test-ROM scene generation or AUTODUMP change" (`:687`) |
| **5C-3** | DeSmuME validation of the 5C-2 ROM scene: extend `compare_capture.py` + `twlhd_v2_dump.py` for texture chunks, compare TXDT/PLDT with the known array | `tools/emulator/`, `tools/twlhd_v2_dump.py`, `tools/test_emulator_compare.py` | EMULATOR: `make emu-validate` (or the equivalent texture run) must PASS; negative comparator tests | EMULATOR | Medium: two parsers must agree; the comparator extension is a tool change | Stage approval; tool changes are outside the current approval list and should be called out explicitly |
| **5C-4** | Hardware run with the option-(c) backend: our own ROM, one dump, byte comparison against the known array | no source change; procedure doc | HARDWARE: one run, one dump, C parser + byte compare | HARDWARE | Medium: read path untested on silicon | One approval per run/build hash (`:686`) |
| **5C-5** (optional) | VRAM readback backend (option a): `VRAMCNT` discovery, per-bank remap/restore, VBlank scheduling, abort recovery; the real generic path | `arm9v2/` platform backend, planner read callback | UNIT: mock `VRAMCNT`/VRAM; EMULATOR: textures captured from VRAM; HARDWARE: side-effect observation | BUILD/UNIT then EMULATOR then HARDWARE | **High**: steals banks from the running game; needs a written risk review | Separate stage approval, new hardware runs, owner sign-off on the remap design |

Deliverables per stage:

- 5C-1: planner + tests + mutation report; no serializer change yet.
- 5C-2: a demo frame with one textured quad dumps a v2 file whose `TXDT`/`PLDT`
  equal the demo's source arrays (host test can drive the planner directly; the
  DS dump is checked in 5C-3/5C-4).
- 5C-3: EMULATOR PASS on the known texture, including the slot-address maths.
- 5C-4: HARDWARE observation only for the option-(c) path.
- 5C-5: the first stage that can capture a texture the ROM did not hand to the
  recorder; it is also the first that can damage the running game.

### 6.2 Open owner decisions and recommendations

| # | Decision | Options | Recommendation |
|---|---|---|---|
| **D1** | Capture source | (c) game source buffers for our ROMs; (a) VRAM remap; (b) DMA snooping; (d) emulator only | **(c) for 5C-2..5C-4, (d) for 5C-3 validation; defer (a) to 5C-5 behind a risk review.** The fixed scope is our own homebrew (`docs/PHASE5B_PLAN.md:10-14`), so (c) is correct and zero-risk; (a) is the only generic path but can corrupt the game. |
| **D2** | Cross-frame texture cache | re-emit per dumped frame; or extend the format with a persistent cache / back-reference | **Do not extend now.** The pipeline dumps one frame per file; each file is self-contained. Revisit only if continuous or multi-frame capture enters scope. |
| **D3** | Texture-drop signal | reuse `TWLHD_V2_FLAG_OVERFLOW` and relax the demo dump gate; or add a new flag bit | **Reuse `OVERFLOW`, relax the gate in 5C-2, report the drop count.** No format change; but this changes dump behaviour, so it needs an explicit OK. |
| **D4** | Format-5 palette window | full 65,540 B per record; or the computed minimal window `4*max(entry & 0x3FFF) + 8` (even) | **Computed minimal window, with a full-window fallback** if the index block is unavailable. Keeps two or more 4x4 textures inside `PLDT`. |
| **D5** | Serialize budget | full caps (768,144 B static); or a smaller default | **Default 131,072 B `TXDT` + 32,768 B `PLDT` (≈309 KB buffer); full-cap value only for the test ROM,** as a compile-time define. |
| **D6** | `glTexImage2D`/`glBindTexture` coverage gap | accept it and require explicit port writes; or intercept the libnds calls | **Accept and document it in 5C-2.** Intercepting libnds is a separate shim project and would still miss direct MMIO and FIFO lists. |
| **D7** | Same-key, different-bytes within a frame | first snapshot wins (silent); or detect with a hash and drop/count; or format extension | **First snapshot wins, counted as a documented loss.** A hash detects but cannot repair; a format extension is premature. |

Any choice other than these recommendations needs the matching approval at
`docs/PHASE5B_PLAN.md:682-687` before code.

---

## 6.3 Status 5C-1 (2026-11-15)

**BUILD/UNIT only.** The pure-C planner exists and is tested on the host;
nothing is wired to the recorder, no arm9/arm11/arm9v2 source changed, and no
EMULATOR or HARDWARE claim is made. There is no format change and the planner
never sets a v2 flag.

What exists:

- `common/twlhd_tex_capture.[ch]`: allocation-free planner. Input is a
  `const TwlhdV2CmdEntry[]` + count (the recorder/serializer representation);
  output is `TwlhdTexCapPlan` (fixed 256-record array) and, through
  `twlhd_tex_capture_apply()`, caller-owned TXDT/PLDT bytes and
  `TwlhdV2TexEntry[]` ready for `twlhd_v2_write()` unchanged.
- Key rules follow §3.1: a key is used only by a VTX command inside a `BEGIN`
  group, format 0 is skipped, and the current `TEXIMAGE_PARAM`/`PLTT_BASE`
  latches start at 0. Dedupe is per frame by the exact pair; two different keys
  with identical bytes are kept twice because TXDS records are keyed, not
  content-addressed (§3.2).
- Sizes come from the shared, already exported `twlhd_v2_tex_sizes()`; no
  formula was copied. No header export was needed and no existing file changed.
- Budget/drop follows D3/D5: defaults 131,072 B TXDT, 32,768 B PLDT and 256
  records (`twlhd_tex_capture_full_limits()` exposes the format caps for
  tests); first-fit in first-use order, whole-record drop only, later smaller
  records still fit. `overflow` is a plan field; 5C-2 maps it to
  `TWLHD_V2_FLAG_OVERFLOW`.
- Format 5 uses the D4 minimal window `4*max(entry & 0x3FFF) + 8`; a failed
  2-byte content scan falls back to the 65,540-byte full window.
- Tests: `tests/host/test_tex_capture.c` (14 labels, hand-computed literals,
  guard-byte/OOB harness, round-trip through writer → parser → pica_frame),
  independent oracle `tools/tex_capture_oracle.py`, 36-scene golden suite
  `tools/test_tex_capture_golden.py`. `make test` is green. Mutation numbers
  are in `docs/reports/5c1-mutation-report.md`.

Corrections to this design, found while implementing:

- §5.1's "pure sizing (no reads)" cannot be literal. The D4 minimal window and
  the read-error drop both need information before the plan can commit. The
  chosen split is: `twlhd_tex_capture_plan()` performs dst==NULL availability
  probes and real 2-byte index scans for format 5; `twlhd_tex_capture_layout()`
  is the read-free sizing call (it only recomputes offsets/totals from the kept
  set); `twlhd_tex_capture_apply()` consumes the immutable plan and never
  re-decides the kept set, so sizing and apply agree by construction.
- §5.1's callback signature has no address space and has no probe form. The
  implemented seam is
  `int (*)(void *ctx, uint32_t kind, uint32_t offset, uint32_t bytes, uint8_t *dst)`
  with `kind` = texel / palette / index, because the DS has three disjoint
  spaces and 5C-2 must route them. `dst == NULL` is an availability probe.
- §5.1 names `twlhd_tex_cap_plan()`/`twlhd_tex_cap_emit()`; the delivered API
  is `twlhd_tex_capture_plan()`/`_layout()`/`_apply()` with a
  `TwlhdTexCapPlan` that owns the record array.
- §3.5 says the PLDT check includes "the worst-case 3 pad bytes" and §5.1 test
  item 7 asks for "a 2 mod 4 window". Both are wrong: every legal palette size
  is a multiple of 4 (fixed tables are multiples of 8; format 5 is
  `4*max + 8`), so the writer's 4-pad is always zero and a 2 mod 4 window
  cannot exist. The planner nevertheless checks the exact padded running total,
  which is required if a future format ever has a non-multiple-of-4 table.
- §3.5 step 4 ("set `TWLHD_V2_FLAG_OVERFLOW`") is a 5C-2 action; the planner
  reports `overflow` only.
- D7's "first snapshot wins" is implemented as "one record per key, read once
  in apply, conflicts counted when either register was rewritten between two
  uses of that key". The planner does not hash content (§3.1 leaves the hash
  unapproved). The conflict counter is therefore a detected-rebind count, not
  a proof that bytes changed.
- Format-5 fallback boundary: "index block cannot be read" is split. If the
  index block's availability probe fails, the key is a read-error drop (apply
  would have no index bytes to copy — a dangling reference). If the block is
  available but the 2-byte content scan fails, the planner falls back to the
  65,540-byte window. This satisfies both D4 and the "no dangling reference"
  rule.
- The full format-5 window (65,540 B) exceeds the default PLDT cap (32,768 B),
  so a fallback key is budget-dropped with defaults; only the full-cap test
  path exercises a kept fallback.
- One stated bound is approximate by construction: `keys_seen` counts distinct
  used keys, but keys beyond the fixed 256-entry table cannot be deduped, so a
  257th+ key that is used repeatedly adds one per use to `keys_seen` and
  `keys_dropped_budget`. The records are still dropped whole. 5C-2 can revisit
  this if a real frame ever exceeds 256 distinct keys.

Open items: D6 (out-of-line libnds coverage) stands; recorder wiring and the
source-buffer backend are 5C-2; the planner does not touch MMIO/VRAM and the
read backend is the only platform-specific part; the host consumer's
`NO_DATA` result for a dropped key is expected and allowed.

---

## 6.4 Status 5C-2 (2026-11-15)

**BUILD/UNIT only.** The texture-aware serialize path is wired into the GX
recorder, the source-buffer backend exists, the arm9v2 demo has an opt-in
textured scene, and a host test suite exercises the whole path. No DS run, no
emulator run, no hardware claim is made.

What exists:

- `common/twlhd_gx_record.[ch]`: new `twlhd_gx_rec_serialize_tex()` runs the
  planner over the recorder's GXCM entries, applies the result into
  caller-owned scratch, fills `params.tex`, and calls the existing v2 writer
  unchanged. The existing `twlhd_gx_rec_serialize()` is untouched and remains
  byte-identical for every existing test.
- Compile-time texture budgets with a `_Static_assert`ed serialize-buffer size:
  default 131,072 B TXDT + 32,768 B PLDT ⇒ `TWLHD_GX_REC_SERIALIZE_TEX_MAX` =
  **309,395 B**. The full-cap override (524,288 + 98,304) compiles on the host
  and gives **768,147 B** (the design §3.6 figure 768,144 omitted the
  worst-case 3-byte PLDT pad).
- `common/twlhd_tex_source.[ch]`: host-compilable, ARM9-clean source-buffer
  backend. It registers `(kind, base, length, main-RAM pointer)` regions and
  resolves the planner's texel/palette/index offsets against them. It rejects
  unregistered offsets, partial overlap, offset+bytes uint32 wraparound, and
  unknown `kind`; it supports the `dst == NULL` probe; a zero-length read
  succeeds iff the offset lies inside a matching region.
- `arm9v2/source/demo_frame.cpp`: new `TWLHD_DEMO_SCENE == 5` branch with an
  8×8 I4 texture + palette in main RAM. The scene uploads to VRAM once at init
  (LCDC remap, 16-bit stores, then texture/palette mapping), enables texturing,
  and writes `GFX_TEX_FORMAT` / `GFX_PAL_FORMAT` / `GFX_TEX_COORD` explicitly
  through the shim so the keys and coordinates are recorded (D6).
- `arm9v2/source/main.cpp`: in textured builds the dump path calls
  `twlhd_gx_rec_serialize_tex()` and prints kept/dropped counts; the normal
  build keeps the original path and buffer size unchanged.
- `tests/host/test_gx_record_tex.c`: 19 labels covering one textured quad,
  two keys, budget drop, read-error drop, no-texture byte-identity,
  too-small output buffer (full output untouched), NULL args, source-backend
  cases, region capacity, backend null args, full-table OOB read, poisoned
  slot, apply-time read error, 256-distinct-key capacity, large-texture budget,
  `pica_frame` status, recorder overflow semantics, the before/after OOB
  guard harness, and limits above the scratch caps returning `ERR_SIZE` with
  every output untouched.
- `docs/reports/5c2_mutate_gx_record_tex.py` and
  `docs/reports/5c2-mutation-report.md`: ≥60 mutants of the new serialize
  function and the backend, with real kill/survive numbers and every survivor
  classified.

**OVERFLOW decision (D3):** texture-drop `TWLHD_V2_FLAG_OVERFLOW` is set
**only in the serialized file flags**, never in `rec->flags`. The arm9v2 dump
gate (`rec->flags & OVERFLOW != 0 ⇒ refuse`) therefore keeps its exact
recorder-level meaning: only GXCM/RSTA overflow refuses the dump. The file
flag is set for **any dropped key** (budget + read-error + unsupported), not
just for `plan.stats.overflow`, because read-error and unsupported drops also
leave the file missing texture data a draw references. Same-key conflicts do
not set the flag (no drop).

**Static RAM numbers (ARM9, measured from `.elf` / `.map`):**

| Buffer | Default | Full-cap override |
|---|---|---|
| serialize output (`g_serialize`) | 309,395 B | 768,147 B |
| texture scratch (`TwlhdGxRecTexScratch`) | 180,268 B | ~641 KiB |
| source backend context (`TwlhdTexSourceCtx`) | 260 B | 260 B |
| normal build `g_serialize` | 135,264 B | — |
| normal build BSS total | 274,064 B | — |
| textured build BSS total | 628,808 B | — |

The textured build adds **+354,744 B** of BSS over the normal build (output
buffer growth of +174,131 B plus the scratch/backend state). The normal build
keeps `g_serialize` at 135,264 B and adds no texture state.

**D6 outcome:** libnds `glTexImage2D` / `glColorTableEXT` / `glBindTexture`
are out-of-line and bypass the shim's macro proxy. Scene 5 therefore:

- uploads the source arrays to VRAM once at init with direct 16-bit stores
  after mapping the banks to LCDC, then remaps them to texture/palette memory;
- issues explicit `GFX_TEX_FORMAT = param;` and `GFX_PAL_FORMAT = pltt;` writes
  through the proxy so the keys are recorded in GXCM;
- enables texturing with `glEnable(GL_TEXTURE_2D);`, which expands to
  `GFX_CONTROL |= GL_TEXTURE_2D;` and is proxied, but `0x04000060` is outside
  the GXCM window so the recorder counts it as an ignored write (not a GXCM
  command);
- issues raw texel-scaled coordinates through `GFX_TEX_COORD =
  TEXTURE_PACK(inttot16(...), inttot16(...));`, which is proxied and recorded.

The record path captures `GFX_TEX_FORMAT`, `GFX_PAL_FORMAT` and `GFX_TEX_COORD`;
the VRAM upload and the `GFX_CONTROL` enable are not GXCM commands.

Corrections to this design, found while implementing:

- §3.5 said the gate must be relaxed for texture-only drops. The implemented
  design keeps the gate untouched and puts the flag in the file only; the
  owner explicitly approved this relaxation on 2026-11-15.
- §3.5/§3.6 used 768,144 B for the full-cap buffer. The real worst-case
  includes the writer's 3-byte PLDT pad: 768,147 B.
- §3.5 step 4 said the planner sets `TWLHD_V2_FLAG_OVERFLOW`. The planner only
  reports drop counters; `twlhd_gx_rec_serialize_tex()` maps the three drop
  counters to the file flag.

**Repair round 1 (review defects fixed, BUILD/UNIT only):**

- Scene-5 VRAM upload now maps banks to LCDC, copies with 16-bit stores, then
  remaps to texture/palette memory, and it runs once at init instead of every
  frame. Source arrays are `__attribute__((aligned(4)))`.
- Scene 5 now enables texturing (`glEnable(GL_TEXTURE_2D)`) and issues raw
  texel-scaled `GFX_TEX_COORD` writes through the shim.
- `twlhd_gx_rec_serialize_tex()` only writes `stats_out` when the writer
  returns `TWLHD_V2_OK`, so all outputs stay untouched on error.
- Mutation survivors at `twlhd_tex_source.c:39` and
  `twlhd_gx_record.c:205/209` are now killed by new tests; the mutation report
  and run log were regenerated with real numbers.

Open items: 5C-3 is DeSmuME validation of scene 5; 5C-4 is a hardware dump
byte-compare; 5C-5 (VRAM readback backend) remains deferred. The source-buffer
backend covers only game-registered source arrays; generic capture from
arbitrary VRAM is not implemented.

---

## 7. What cannot be claimed yet

| Claim | Status |
|---|---|
| The recorder sees every texture bind | **FALSE** for out-of-line libnds calls (`videoGL.h:433`, `:442`, `:468`, `:486`) and for FIFO lists (`shim.hpp:27-29`) |
| A texture-mapped bank is CPU-readable | **UNVERIFIED**; DeSmuME's model says no (`MMU.cpp:555-558`, `:443-456`) |
| Remapping a bank to LCDC is safe | **UNVERIFIED** and expected to be risky; no hardware run supports it |
| A full 512 KiB texture set can be read in one VBlank | **UNVERIFIED**; the estimate in §2.5 says no |
| The format-5 palette window bound (65,540 bytes) | **VERIFIED** in DeSmuME (`texcache.cpp:1791-1801`) and in the repo doc (`TWLHD_V2_FORMAT.md:330-349`); hardware `PAL4X4` behaviour is the same model |
| The parser accepts `OVERFLOW` with a clean swap | **UNVERIFIED**; the code suggests yes (`parse.c:255-271`), needs a host test |
| Python tools understand texture chunks | **FALSE** today (`tools/twlhd_v2_dump.py:85-87`); separate task |
| Hardware texture capture works | **UNVERIFIED**; no run exists |

## 8. Reference index

Repository:

- `common/twlhd_gx_record.[ch]`, `arm9v2/include/twlhd_gx_shim.hpp`,
  `arm9v2/source/main.cpp` — recorder hook, limits, dump loop.
- `common/twlhd_v2_format.h`, `common/twlhd_v2_parse.[ch]`,
  `common/twlhd_v2_write.[ch]` — chunk format, validation, writer.
- `common/twlhd_gx_interp.[ch]`, `common/twlhd_pica_frame.c` — texture state and
  host consumer.
- `docs/TWLHD_V2_FORMAT.md` §7.1, §8, §12; `docs/DS_GX_RESEARCH.md` "Textures";
  `docs/PHASE5B_PLAN.md` §5B-2a, §6; `docs/EMULATOR_VALIDATION.md`.
- `tests/host/test_v2_write.c`, `test_v2_parse.c`, `test_pica_frame.c`,
  `test_gx_record.c`; `tools/emulator/desmume_capture.sh`,
  `tools/emulator/compare_capture.py`, `tools/twlhd_v2_dump.py`.

External (read once, commit `fc504ee0`):

- `desmume/src/texcache.cpp` — `MemSpan_TexMem`/`MemSpan_TexPalette`
  (`:144-197`), `TextureStore` constructor (`:412-499`), `PAL4X4`/4x4 unpack
  (`:1752-1801`).
- `desmume/src/gfx3d.h` — `TEXIMAGE_PARAM` bitfields (`:289-358`), port words
  (`:508-509`).
- `desmume/src/gfx3d.cpp` — parameter and palette latches (`:1375-1395`),
  command dispatch (`:2680-2695`).
- `desmume/src/MMU.cpp` — bank table (`:309-319`), physical pointer (`:513-516`),
  `VRAMCNT` read (`:521-526`), per-bank mapping (`:535-666`), CPU map
  (`:494-511`, `:443-456`).
- `/opt/devkitpro/libnds/include/nds/arm9/video.h` — `VRAM_*_CR` (`:146-153`),
  mapping enums (`:160-173`), `vramSetBankA` (`:372`).
- `/opt/devkitpro/libnds/include/nds/arm9/videoGL.h` — `GL_STATIC_INL`
  (`:51-52`), `glTexImage2D` (`:433`), `glColorTableEXT` (`:442`),
  `glTexParameter` (`:468`), `glBindTexture` (`:486`), inline section (`:530`).
