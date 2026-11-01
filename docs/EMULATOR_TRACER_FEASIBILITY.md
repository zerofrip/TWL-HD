# Emulator GX Tracer — Feasibility Audit and Implementation Plan

**Status: AUDIT AND PLAN ONLY. No emulator source was modified. No tracer exists.**
The DeSmuME tree at `/home/one/3ds/desmume` is untouched (`git status` empty,
revision `fc504ee0`); the melonDS tree at `/home/one/3ds/melonDS` was only read.

This answers the "backend E" question from `docs/PHASE4D_CAPTURE_EXPANSION_PROPOSAL.md`:
can an instrumented open-source DS emulator emit TWL-HD v2 traces
(`docs/TWLHD_V2_FORMAT.md`) so the format and renderer can be tested against content
that the homebrew register-proxy shim (backend B) cannot see?

Evidence labels: **SOURCE-READ** = read in the emulator source in this session
(file:line given; paths are relative to the emulator checkout root, e.g.
`/home/one/3ds/desmume`); **MECHANICAL** = compared by an ad-hoc script at the time; the scripts were **not kept**, so on re-check treat these results as SOURCE-READ until re-run;
**BUILD VERIFIED** = a build or command actually ran here and its output was observed;
**EMULATOR VERIFIED** = observed by running our ROM in DeSmuME (never hardware);
**UNVERIFIED** = reasoning or recollection, not checked here.
Nothing in this document is HARDWARE VERIFIED.

**Where the BUILD/EMULATOR evidence lives:** the DeSmuME out-of-tree build, the run of the
`arm9v2` demo and its parse/comparison results are recorded in `docs/EMULATOR_VALIDATION.md`
(added to `main` by the emulator-validation harness merge), including the SHA-256 of the captured frame and the exact commands.

**Supersedes one earlier statement:** `docs/PHASE4D_CAPTURE_EXPANSION_PROPOSAL.md` §9
(decision 5) said no DS emulator was available. Since then emulator sources were supplied and
DeSmuME was built; real **hardware is still not available** for this work.

## 1. Summary

- **Technically feasible and low-risk to hook.** Both emulators funnel every decoded GX
  command — whether it arrived through the packed GXFIFO, DMA, or a direct command
  port — through one function that already receives `(command id, 32-bit parameter)`,
  which is exactly the shape of a v2 `GXCM` entry.
- **It closes the shim's blind spots.** Because the hook sits behind the emulated hardware,
  it sees library-internal writes (`glInit`, `glMaterialf`, texture setup) and DMA display
  lists, so `coverage_flags` for such a trace could be 0 for the GX stream.
- **The command tables agree four ways.** GBATEK, DeSmuME, melonDS and TWL-HD's
  `twlhd_v2_cmd_param_count()` define the same 37 commands with identical parameter
  counts (**MECHANICAL**, details in section 3).
- **The licensing boundary is the real design constraint**, not the code (section 5).
- **Open decision (section 8):** whether v2's size limits suit real frames, which this
  audit cannot settle because no commercial ROM was used or is available.

## 2. Where the hooks go

### 2.1 DeSmuME (GPL-2.0-or-later) — SOURCE-READ

| Purpose | Location | Notes |
|---|---|---|
| Single funnel for decoded commands | `GFX_FIFOsend(cmd, param)`, called from `GXF_Hardware::receive` (`desmume/src/gfx3d.cpp:117`) and from `gfx3d_sendCommand` (`:3630`) | Sees both packed-FIFO/DMA and direct-port writes |
| Packed GXFIFO words (incl. DMA) | `gfx3d_sendCommandToFIFO(v)` → `gxf_hardware.receive(v)` (`:3623`) | Called from the MMU for writes to `0x04000400` (`desmume/src/MMU.cpp:4685`; the case covers writes to `0x04000400–0x0400043F`) |
| Direct command ports | `gfx3d_sendCommand(u32 cmd, u32 param)`: `cmd = (cmd & 0x01FF) >> 2` (`:3632`) | Called from `MMU.cpp:4717`. The `case` list is the same id set as our table |
| Frame boundary | `SWAP_BUFFERS` (0x50) passes through the same funnel; execution is `gfx3d_glFlush` (`:3197`) | Issue-time boundary is what a v2 file needs |
| Rendering registers (RSTA) | `gfx3d_glClearColor<T>`, `gfx3d_glClearDepth<T,N>`, `gfx3d_glClearImageOffset<T,N>`, `gfx3d_glFogColor`, `gfx3d_glFogOffset`, `gfx3d_glAlphaFunc`, called from width-specific MMU write handlers (`MMU.cpp:3795–3830`, `4447–4479`, `5009–5032`) | The handler path gives the access width (u8/u16/u32) that RSTA entries need. Toon, edge and fog tables go through `gfx3d_UpdateToonTable<T>` (`MMU.cpp:3397,3949,4677`), `gfx3d_UpdateEdgeMarkColorTable<T>` (`:3383,3935,4663`) and `gfx3d_UpdateFogTable<T>` (`:3389,3941,4669`); the MMU write handlers **mask each byte to 7 bits** before calling the fog setter (`& 0x7F` / `0x7F7F` / `0x7F7F7F7F` at the call sites above), so a tracer must decide whether to record the value as written (pre-mask, matching the shim) or as applied. `DISP3DCNT` (`REG_DISPA_DISP3DCNT`) is handled in `MMU.cpp` (e.g. lines 2536, 2914, 3556; found in the follow-up fact-check, not analysed further here) |

Facts that affect the writer:

- `GXF_Hardware::receive` calls `GFX_FIFOsend(currCommand, 0)` for zero-parameter commands:
  **the dummy parameter a game may have written is not preserved** (the emulator's own
  comment, `gfx3d.cpp:117+`, says games differ on whether they send one).
- The unpacker skips NOP (command 0) when other work is pending.

### 2.2 melonDS (GPL-3.0-or-later) — SOURCE-READ

| Purpose | Location | Notes |
|---|---|---|
| Single funnel | `GPU3D::CmdFIFOWrite(const CmdFIFOEntry& entry)` (`src/GPU3D.cpp:1665`) | `CmdFIFOEntry` is `{Command (u8), Param (u32)}` — same shape as a `GXCM` entry |
| Packed GXFIFO | `GPU3D::WriteToGXFIFO(u32)` (`:2511`), reached from `Write32` for `0x04000400–0x0400043F` (`:2876–2878`) | |
| Direct ports | `Write32` for `0x04000440–0x040005CB` builds an entry and calls `CmdFIFOWrite` (`:2882–2887`) | |
| Stall path | If the FIFO is full the entry goes to `CmdStallQueue` and the CPU stalls (`:1679`) | **Hook where the entry is created, not where it is dequeued**, or issue order is distorted |
| Frame boundary | `FlushRequest = 1` at command execution (`:2064`), consumed in `VBlank` (`:2425–2497`) | |

Facts that affect the writer:

- `WriteToGXFIFO` **can produce `Command == 0` (NOP) entries** (`:2527`,
  `NumCommands == 4 && CurCommand == 0`). A v2 entry may not be a NOP, so the tracer must
  drop them.

### 2.3 Why both

Hooking a second emulator is the cheapest way to validate the *tracer itself*: a bug in one
emulator's hook is unlikely to appear identically in the other.

## 3. Command-table cross-check — MECHANICAL

Ad-hoc scripts parsed each source when this audit was written; the scripts were not kept in the repository, so the counts below are reproducible only by re-reading the three tables (treat as SOURCE-READ until re-run).

| Source | Result |
|---|---|
| GBATEK 3D I/O map (fetched page) vs `twlhd_v2_cmd_param_count()` | 37 vs 37 commands, **0 mismatches** |
| DeSmuME `gfx3d_commandTypes[]` (`desmume/src/gfx3d.cpp:82`) vs ours | 37 vs 37, ids and counts **identical**; `0xCC` = undefined, `0x00` = no-arg, `0xFF` = invalid |
| melonDS `CmdNumParams[256]` (`GPU3D.cpp:105`) vs ours | all 37 of ours match; melonDS lists **no parameterised command we lack** |

No code was copied. The table in `common/twlhd_v2_parse.c` was derived from GBATEK before any
emulator source was opened; the emulator tables were used afterwards only as independent
checks, and the command ids and counts are hardware facts.

## 4. Fit with the v2 format

The tracer would emit one v2 file per frame (a frame ends at its `SWAP_BUFFERS` entry).

| Item | Tracer behaviour | Spec / format consequence |
|---|---|---|
| Coverage | Every GX write reaches the emulator | `coverage_flags` can omit `LIBRARY_INTERNAL_WRITES_UNCAPTURED` and `DMA_DISPLAY_LISTS_UNCAPTURED`. `INITIAL_STATE_UNKNOWN` is still true unless tracing starts at power-on |
| Zero-parameter value | DeSmuME passes 0; the shim records whatever the game wrote | **Spec follow-up:** state that consumers must ignore the value of a zero-parameter entry. Not changed in this audit |
| NOP | melonDS emits them | Tracer must filter; entry validity rule unchanged |
| RSTA | Hook the register setters with width | Same whitelist applies |
| Hidden library state | The shim captures the *actual values written*; libnds keeps the clear colour's polygon id in the library global `glCurClearColor` (inline `glClearColor`/`glClearPolyID` do `GFX_CLEAR_COLOR = glCurClearColor = (glCurClearColor & mask) \| …`, `videoGL.h:1303,1311`; the symbol is a BSS variable in `libnds9.a`) | **Observed (EMULATOR VERIFIED):** the first clear-colour write in the captured DS frame was `0x3F1F0000`, while a fresh single-frame host run gives `0x001F0000`. **Cause (as stated in `docs/EMULATOR_VALIDATION.md` and the comparator's comments; libnds `glInit()` was not re-read for this audit):** the library global `glCurClearColor` is seeded by `glInit()` on the DS (polygon-id bits 24..29 = `0x3F`) while the host test leaves it at zero, so the first clear-colour write differs only in those bits. Not a defect; worth a spec note |
| Frame size | Real frames may exceed 65,536 `GXCM` entries or 1 MiB | **UNVERIFIED.** Hardware vertex/polygon RAM is finite, but commands beyond it are still issued. Section 8 decision |
| Textures | Descriptors (`TEXIMAGE_PARAM`, `PLTT_BASE`) are ordinary commands; pixels live in VRAM state | Needs new chunks (`TXDS/TXDT/PLDT`, currently reserved). Out of scope here |

## 5. Licensing boundary

This section describes a design posture. It is **not legal advice**; obtain a legal review
before distributing anything.

Facts read in this session: TWL-HD is Apache-2.0. DeSmuME's `gfx3d.cpp` is
GPL-2.0-or-later (header text; meson declares `GPL2+`). melonDS's `GPU3D.cpp` is
GPL-3.0-or-later (header text).

Rules for this work:

1. **A tracer would be a modification of a GPL program.** As we understand the GPL, any binary or patch that contains
   emulator code must be distributed under the emulator's license, with source. As a policy choice we keep it in a separate
   fork/repository (or a patch file) outside TWL-HD; rule 2 explains why that alone is not sufficient.
2. **Separate repositories are not by themselves a guarantee.** What matters is what code
   moves between them:
   - Copying emulator code into TWL-HD would put that code under GPL terms inside an
     Apache-2.0 repository. **Do not.**
   - Copying TWL-HD's Apache-2.0 code (for example `common/twlhd_v2_write.c`) into the
     fork would, in the project's cautious reading, make the combined binary a GPL work that also carries Apache attribution/NOTICE
     obligations. The commonly stated position (e.g. the FSF's) is that Apache-2.0 is compatible with GPLv3 but not with GPLv2-only;
     DeSmuME's "or later" wording would permit GPLv3 here, yet this still couples the two projects. (Not legal advice.)
   - **Linking** either way is treated by this project as creating a combined work and is excluded as a policy.
3. **Therefore the shared artifact is the specification only.** The tracer's writer is
   implemented independently from `docs/TWLHD_V2_FORMAT.md` (no copy of TWL-HD source),
   and TWL-HD's validators (`twlhd_v2_parse`, `tools/twlhd_v2_dump.py`) are used as
   external black boxes on the files it produces. TWL-HD never includes emulator code,
   headers, tables, or patch text.
4. **Output files are data.** A v2 trace does not contain program code. Traces from
   *commercial* ROMs may still embed copyrighted geometry and colours: do not commit or
   share them. Repository tests use only our own homebrew ROMs.
5. **Facts versus expression.** The project's working posture is that command ids and parameter counts are hardware facts
   (public GBATEK) rather than protectable expression; this is a posture, not a legal conclusion. The cross-check in section 3
   compared values and copied no code.

## 6. Implementation plan (nothing started)

| Step | Work | Verification available here | Exit criterion |
|---|---|---|---|
| M0 | Create the out-of-tree fork (or patch series) pinned to a recorded revision; document how to build it (the DeSmuME out-of-tree meson recipe, including the libpcap header workaround, was built and run here (BUILD VERIFIED, using a private libpcap header prefix because the system has no `libpcap-dev`)) | build | clean build of the unmodified fork |
| M1 | DeSmuME: hook `GFX_FIFOsend` to append `(cmd, param)`, drop NOP, close the frame at `SWAP_BUFFERS`, write a v2 file with an independent writer | emulator run | file accepted by the TWL-HD C and Python validators |
| M2 | RSTA: hook the register setters with access width; mask to width as the spec requires | emulator run | validators accept; RSTA fields match expectations |
| M3 | **Cross-backend equivalence test:** run our `arm9v2` demo in the tracer fork; compare the tracer's frame with the shim's own capture of the same frame (`docs/EMULATOR_VALIDATION.md`). After normalisation (shim has no `glInit` writes; zero-parameter values ignored) the streams must agree | emulator run | identical command-id sequence and values for the common subset |
| M4 | melonDS hook, repeat M1–M3 | emulator run | two independent emulators agree on our homebrew frames |
| M5 | Frame-size statistics from **user-supplied** content to settle v2 limits | needs ROMs the user owns | decision recorded |
| M6 | Texture/palette capture design (new chunks) | — | separate proposal |

## 7. Risks

- Emulated timing is not hardware timing; a trace records **issue order**, which is the
  property the format needs, not cycle counts.
- melonDS stall queue and DeSmuME's deferred flush can mislead a hook placed at execution
  rather than issue time (section 2).
- Savestate load/rewind can replay or skip commands; the tracer must be disabled or reset
  across them.
- Multi-threaded renderers do not affect GX issue order but do affect when a frame "ends"
  visibly; use the issue-time `SWAP_BUFFERS`.
- Content legality: tracing commercial ROMs is the user's responsibility; none are used here.

## 8. Decisions requested

1. **Approve M0–M3 (DeSmuME first, our homebrew only)?** M4 and M5 can wait.
2. **Fork location and publication:** a private local fork, or a public one (public means
   GPL source distribution duties apply from the first push)?
3. **v2 limits:** keep 65,536 entries / 1 MiB until real data exists, or raise them now?
4. **Spec clarification** that the value of a zero-parameter `GXCM` entry is
   unspecified (consumers must ignore it) — approve as a small v2 amendment?
5. **Legal review** of section 5 before any public release of a fork.
