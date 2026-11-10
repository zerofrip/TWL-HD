# TWL-HD v2 capture format

**Status: BUILD VERIFIED / UNIT VERIFIED / HARDWARE UNVERIFIED.**
The common/ modules build for the host and for devkitARM, and the host suite
(`make test`) exercises the parser, writer, recorder, the libnds shim and a
Python cross-implementation agreement corpus. The arm9v2 demo has never run on
a DS or in an emulator: every runtime capture claim is **HARDWARE UNVERIFIED**.

This document is normative. It defines the version 2 (v2) container, the chunk
payloads, the validation order and the compatibility rules. The v1 format in
`common/twlhd_frame_format.h` is unchanged and remains valid.

## 1. Why v2

v1 stores decoded `TwlhdCmd` records: at most 16 parameter words per command,
1024 commands per frame, and only calls that go through the Phase 1 wrappers.
The v2 primary representation is the raw GX command stream (chunk `GXCM`), one
entry per port write, which every capture backend naturally produces. v1 files,
parsers, renderers and RTCom code are untouched.

### Approved decisions (verbatim)

1. Direction: raw GX command stream (chunk GXCM) is the primary representation
   inside a versioned, chunked v2 container. v1 is preserved.
2. Scope: homebrew only. Capture backend = "register-write proxy shim"
   (backend B). Backends C (clip-matrix readback) and D (display-list decoder)
   are NOT implemented (their chunk FourCCs are only RESERVED in the spec).
   E (emulator trace) is not started and not decided. F (commercial-game
   interception) is out of scope.
3. Real-time transport is out of scope; the offline pipeline is the vehicle
   for this phase. Whether live play is ever pursued stays an OPEN question.
4. Licensing posture for an external emulator tracer: OPEN, no work.
5. No hardware and no DS emulator are available: everything runtime is
   HARDWARE UNVERIFIED.

## 2. Conventions

- Every integer is little-endian.
- `u32` is 32 bits, `u16` 16 bits, `u8` 8 bits.
- FourCC values are packed as `a | b<<8 | c<<16 | d<<24` (see
  `TWLHD_FOURCC` in `common/twlhd_v2_format.h`).
- The file is a sequence of fixed 32-byte header, then `chunk_count` chunks.
  There are no offsets: chunks tile the region after the header exactly.
- Hard limit: a file is at most **1 MiB** (1,048,576 bytes).
- All offset/size arithmetic uses 64-bit intermediates. A parser never reads
  out of bounds, never casts a buffer to a struct, and never allocates.

## 3. File header (32 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | `version` | `2` |
| 4 | 4 | `header_size` | `32` |
| 8 | 4 | `total_size` | exact file size in bytes |
| 12 | 4 | `flags` | see below |
| 16 | 4 | `frame_index` | producer-defined frame number |
| 20 | 4 | `chunk_count` | `1..16` |
| 24 | 4 | `reserved0` | `0` |
| 28 | 4 | `reserved1` | `0` |

File flags:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `FRAME_COMPLETE` | the producer closed the frame with `SWAP_BUFFERS` |
| 1 | `OVERFLOW` | at least one entry or RSTA write was dropped |
| 2..31 | reserved | must be zero |

`FRAME_COMPLETE` means "this stream is a complete frame, ending in the swap".
`OVERFLOW` means "this stream is lossy". Both are producer statements; the
parser validates the one structural consequence (section 8).

## 4. Chunk header (16 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | `type` | FourCC |
| 4 | 4 | `payload_size` | bytes, always a multiple of 4 |
| 8 | 4 | `chunk_flags` | bit 0 = `REQUIRED`, bits 1..31 zero |
| 12 | 4 | `reserved` | `0` |

Chunks must tile the region after the header exactly: no gaps and no trailing
bytes. Duplicate known chunks are rejected.

| FourCC | Status | Payload |
|---|---|---|
| `GXCM` | mandatory, `REQUIRED` set | raw GX entries |
| `META` | mandatory, `REQUIRED` clear | backend and coverage |
| `RSTA` | optional, at most once, `REQUIRED` clear | rendering-register writes |
| `TXDS` | optional, at most once, `REQUIRED` clear | texture descriptors |
| `TXDT` | mandatory when `TXDS` is present, `REQUIRED` clear; illegal without `TXDS` | texel/index data |
| `PLDT` | optional, at most once, `REQUIRED` clear; illegal without `TXDS` | palette data |
| `CLPM` | **reserved, not implemented** | — |

`CLPM` (clip matrix) is still a placeholder for backend C. No code produces or
consumes it: a parser treats it like any unknown chunk (skipped and counted when
optional, `ERR_UNSUPPORTED_REQUIRED` when the `REQUIRED` bit is set).

`TXDS`/`TXDT`/`PLDT` are implemented for **host tooling only** (parser, writer,
read-only view accessors) at Phase 5B-2a. A `TXDT` or `PLDT` chunk without a
`TXDS` chunk is an orphan and is rejected with `ERR_MISSING_CHUNK`. The payload
layout in section 7.1 is
an **owner-reviewable proposal**; it is BUILD/UNIT tested and nothing in it is
EMULATOR or HARDWARE verified. The DS recorder does not capture textures yet, so
every file the recorder produces is still GXCM+META(+RSTA). The Python mirror
(`tools/twlhd_v2_dump.py`) still classifies the three FourCCs as unknown; that
divergence is deliberate and recorded in section 9.

## 5. `GXCM` — raw GX command stream

Payload: `N * 8` bytes, `N` in `1..65536`. Entry layout:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `cmd_word` |
| 4 | 4 | `value` |

One entry per 32-bit GX port write.

`cmd_word` encoding:

- Normal entry: bits 0..7 are the command id, bits 8..31 are **zero**.
- RAW FIFO entry: `cmd_word == 0x00010000` exactly (bit 16 set, bits 0..7 and
  all other bits zero). `value` is the word written to `0x04000400`; the packed
  command bytes inside it are opaque to this format.

Valid ids and the number of entries one command consumes:

| Id | Name | Entries | Id | Name | Entries |
|---|---|---|---:|---|---:|
| `10h` | MTX_MODE | 1 | `2Ah` | TEXIMAGE_PARAM | 1 |
| `11h` | MTX_PUSH | 0 | `2Bh` | PLTT_BASE | 1 |
| `12h` | MTX_POP | 1 | `30h` | DIF_AMB | 1 |
| `13h` | MTX_STORE | 1 | `31h` | SPE_EMI | 1 |
| `14h` | MTX_RESTORE | 1 | `32h` | LIGHT_VECTOR | 1 |
| `15h` | MTX_IDENTITY | 0 | `33h` | LIGHT_COLOR | 1 |
| `16h` | MTX_LOAD_4x4 | 16 | `34h` | SHININESS | 32 |
| `17h` | MTX_LOAD_4x3 | 12 | `40h` | BEGIN_VTXS | 1 |
| `18h` | MTX_MULT_4x4 | 16 | `41h` | END_VTXS | 0 |
| `19h` | MTX_MULT_4x3 | 12 | `50h` | SWAP_BUFFERS | 1 |
| `1Ah` | MTX_MULT_3x3 | 9 | `60h` | VIEWPORT | 1 |
| `1Bh` | MTX_SCALE | 3 | `70h` | BOX_TEST | 3 |
| `1Ch` | MTX_TRANS | 3 | `71h` | POS_TEST | 2 |
| `20h` | COLOR | 1 | `72h` | VEC_TEST | 1 |
| `21h` | NORMAL | 1 | | | |
| `22h` | TEX_COORD | 1 | | | |
| `23h` | VTX_16 | 2 | | | |
| `24h` | VTX_10 | 1 | | | |
| `25h` | VTX_XY | 1 | | | |
| `26h` | VTX_XZ | 1 | | | |
| `27h` | VTX_YZ | 1 | | | |
| `28h` | VTX_DIFF | 1 | | | |
| `29h` | POLYGON_ATTR | 1 | | | |

A zero count still consumes exactly one entry (the dummy write that carries
the id), e.g. `11h` MTX_PUSH and `41h` END_VTXS. NOP (`00h`) is **not** a valid
GXCM id.

### 5.1 Consumer grouping rules

- Consecutive non-RAW entries with the same id must form complete groups of
  `max(count, 1)` entries. A group may not be interrupted by a different id or
  by a RAW entry.
- RAW entries are opaque and are not grouped. They may appear between complete
  groups.
- A trailing incomplete group is accepted only when `OVERFLOW` is set (the
  recorder ran out of capacity mid-command). An incomplete group anywhere else
  is `ERR_GXCM_GROUP`.
- `SWAP_BUFFERS` (`50h`) may appear at most once and only as the last entry.
  `FRAME_COMPLETE` must be set **iff** the last entry is `SWAP_BUFFERS`.
- A consumer that wants decoded commands must apply the same grouping: the
  first entry of a group carries the id, the following `count-1` entries are
  its parameters in issue order.

### 5.2 Recorder resolution of the SWAP/OVERFLOW contradiction

An earlier draft allowed a swap after overflow, which conflicts with "swap iff
FRAME_COMPLETE". Resolution, implemented identically in C and Python:

- A clean frame records the `SWAP_BUFFERS` entry, sets `FRAME_COMPLETE` and
  disarms.
- If any entry or RSTA write has been dropped, the recorder **drops the swap
  too**, sets `OVERFLOW`, leaves `FRAME_COMPLETE` clear and disarms. The
  dropped swap is counted in `dropped_entries`.

Every file the recorder produces therefore satisfies the parser's iff rule.
The parser itself does not forbid `FRAME_COMPLETE|OVERFLOW` together; the
recorder simply never emits that combination.

## 6. `META` — backend and coverage

Payload exactly 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `backend_id` |
| 4 | 4 | `coverage_flags` |
| 8 | 4 | `reserved` = 0 |
| 12 | 4 | `reserved` = 0 |

`backend_id` values: `0` unknown, `1` register-write proxy shim, `2`
synthetic/test. Other values are tolerated by the parser (forward
compatibility); consumers must treat an unknown id as "unknown".

`coverage_flags`:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `LIBRARY_INTERNAL_WRITES_UNCAPTURED` | some writes were issued by compiled library code (e.g. `glInit`) and are not in `GXCM` |
| 1 | `DMA_DISPLAY_LISTS_UNCAPTURED` | `glCallList` / DMA streams bypass the proxy |
| 2 | `INITIAL_STATE_UNKNOWN` | state set before arming is not reproduced |
| 3..31 | reserved | must be zero |

Backend notes:

- The register-proxy recorder is **not interrupt-safe**. A GX write from an
  interrupt handler while an entry is being appended can corrupt `gxcm_count`
  or `rsta_count`. Arm, capture and disarm only from thread/task context.
- `glCallList()` uploads a display list via DMA and does **not** route through
  the shim. Captured files therefore set bit 1 (`DMA_DISPLAY_LISTS_UNCAPTURED`).

## 7. `RSTA` — rendering-register writes

Optional, at most once. Payload: `M * 16` bytes, `M` in `0..4096`. Entry:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `addr` |
| 4 | 4 | `value` written |
| 8 | 4 | `gx_index` = `GXCM` entry count at write time |
| 12 | 4 | `width` in bytes: 1, 2 or 4 |

Rules for every entry:

- `width` is 1, 2 or 4.
- `addr % width == 0`.
- `[addr, addr + width)` lies entirely inside **one** whitelist range below.
- `value` has no bits set above the register `width` (e.g. a 1-byte write must
  have `value & ~0xFF == 0`). The recorder and the shim both mask values to
  the port width; a producer that emits an out-of-width value is rejected.
- `gx_index <= GXCM entry count`.
- `gx_index` is non-decreasing across the chunk.

Whitelist (GBATEK 3D rendering-engine registers):

| Range | Bytes | Content |
|---|---:|---|
| `0x04000060..0x04000061` | 2 | DISP3DCNT (`GFX_CONTROL`) |
| `0x04000330..0x0400033F` | 16 | edge colours (8 x u16) |
| `0x04000340..0x04000341` | 2 | alpha test (`GFX_ALPHA_TEST`) |
| `0x04000350..0x04000357` | 8 | clear colour, clear depth, clear image offset |
| `0x04000358..0x0400035F` | 8 | fog colour, fog offset |
| `0x04000360..0x0400037F` | 32 | fog table (32 x u8) |
| `0x04000380..0x040003BF` | 64 | toon table (32 x u16) |
| `0x04000610..0x04000611` | 2 | 1-dot depth (`GFX_CUTOFF_DEPTH`) |

The clear-poly-id field is the top byte of the clear-colour word at
`0x04000350` (`glClearPolyID` writes `GFX_CLEAR_COLOR` bits 24..29 in
`videoGL.h`), so it is already inside the clear range. The task contract's
"clear-poly-id byte" parenthetical next to the edge-colour range is therefore
resolved this way; no extra address is whitelisted inside `0x330..0x33F`.

## 7.1 `TXDS` / `TXDT` / `PLDT` — texture data (owner-reviewable proposal)

Status: host tooling only — BUILD/UNIT implemented and tested, no EMULATOR or
HARDWARE verification. The recorder does not emit these chunks yet. The layout
below is a proposal pending owner review; it extends, and does not replace, any
earlier sketch.

Three chunks carry one frame's textures:

- `TXDS`: an array of fixed 40-byte descriptors, one per distinct
  (`TEXIMAGE_PARAM`, `PLTT_BASE`) pair.
- `TXDT`: raw concatenated texel blocks (and, for 4x4, index blocks).
- `PLDT`: raw concatenated RGB555 little-endian palette blocks.

### `TXDS` records

Payload `N * 40` bytes, `N` in `1..256`. Record fields, all `u32`:

| Offset | Field | Meaning |
|---:|---|---|
| 0 | `teximage_param` | the `2Ah` command word; bits 20..28 define size/format |
| 4 | `pltt_base` | the `2Bh` value this descriptor was captured with |
| 8 | `texel_offset` | byte offset of the texel block inside `TXDT` |
| 12 | `texel_size` | texel block size, must equal the format formula |
| 16 | `palette_offset` | byte offset of the palette block inside `PLDT` |
| 20 | `palette_size` | palette block size; fixed formula for formats 1/2/3/4/6, 0 for format 7, an even window in `[2, 65540]` for format 5 |
| 24 | `index_offset` | byte offset of the 4x4 index block inside `TXDT` |
| 28 | `index_size` | index block size, must equal the format formula |
| 32 | `reserved0` | `0` |
| 36 | `reserved1` | `0` |

Rules:

- (`teximage_param`, `pltt_base`) is unique across the chunk.
- Every size equals the shared `twlhd_v2_tex_sizes()` result for
  `teximage_param`, except `palette_size` for format 5, which is a
  caller-chosen window validated by the shared
  `twlhd_v2_tex_palette_size_ok()` helper. The parser and the writer call the
  same two helpers.
- An offset is `0` when its size is `0` (a record without a palette has
  `palette_offset == 0`).
- `TXDT` is mandatory when `TXDS` is present; `PLDT` is mandatory iff some
  record has `palette_size > 0`. Either one without a `TXDS` chunk is an
  orphan and rejected with `ERR_MISSING_CHUNK` (section 8, step 10).

### Size formula

`TEXIMAGE_PARAM` bits 20..22 are the width shift and bits 23..25 the height
shift: `width = 8 << SizeShiftS`, `height = 8 << SizeShiftT`. Bits 26..28 are
the packed format. Format 0 (no texture) has no sizes and no record.

| Format | Name | Bytes/texel | `palette_size` | `index_size` |
|---:|---|---:|---:|---:|
| 1 | A3I5 | 1 | 64 | 0 |
| 2 | I2 | 1/4 | 8 | 0 |
| 3 | I4 | 1/2 | 32 | 0 |
| 4 | I8 | 1 | 512 | 0 |
| 5 | 4x4 | 1/4 | even window in `[2, 65540]` | `width*height >> 3` |
| 6 | A5I3 | 1 | 16 | 0 |
| 7 | 16bpp | 2 | 0 | 0 |

`texel_size = width * height * bytes_per_texel`. This is the DeSmuME
`texSizes[] = {0,4,1,2,4,1,4,8}` table divided by 4
(`docs/DS_GX_RESEARCH.md` "Texture memory addressing"), i.e. txSize/4 bytes per
texel. Palette sizes are 2 bytes per entry (32/4/16/256/8 entries) for formats
1/2/3/4/6.

**4x4 palette window (explicit choice):** `docs/DS_GX_RESEARCH.md`'s format
table lists "4 per block" palette entries for 4x4, and DeSmuME's PAL4X4 macro
(`texcache.cpp` ~1752) shows why: the unpacker reads palette RAM per block with
`palAddress = PLTT_BASE<<4 + pal1offset` and `pal1offset = (blockword & 0x3FFF)<<1`
(`texcache.cpp` ~1789-1843), producing the 4 colours of each compressed block.
The `paletteSizeList[5] = 0` at `texcache.cpp:418/441` only sizes the cache
copy, not the palette read; it is not evidence that format 5 has no palette.
A format-5 record therefore carries a `PLDT` window chosen by the capture
tool: any even byte count in `[2, 65540]`. The window starts at byte
`PLTT_BASE << 4`; the record has no start field, so the first PLDT byte is the
palette-RAM byte at that address. The upper bound comes from DeSmuME
`texcache.cpp:1752/1791`: `pal1offset = (pal & 0x3FFF) << 1` counts 16-bit
entries and `PAL4X4(o)` reads byte `palAddress + o*2` for `o` in `0..3`, so the
largest byte index read is `0x3FFF*4 + 7 = 65,539`, i.e. a window needs up to
65,540 bytes. DeSmuME also wraps palette reads across the six 16 KiB palette
slots (`(addr >> 14) & 7`), so a decoder must treat the captured PLDT bytes as
one contiguous window and report an error beyond it, never wrap. Colours beyond
the captured window are a later decoder error, not a parser error. The
65,540-byte cap keeps one maximum window inside the 98,304-byte `PLDT` limit
(section 12).

### `TXDT` and `PLDT` layout

For every record, in `TXDS` order, `TXDT` holds `texel_size` texel bytes
followed by `index_size` index bytes, hence
`index_offset = texel_offset + texel_size` for a 4x4 record. `PLDT` holds the
palette blocks in the same record order. Texel and index blocks are multiples
of 4 bytes for every format (`width,height >= 8`), so `TXDT` stays 4-byte
aligned. Format-5 palette windows are only even, so they may be `2 mod 4`
(2, 6, 10, ... bytes): the writer appends 1..3 zero pad bytes after the last
palette block so the `PLDT` payload length is a multiple of 4. The padding
belongs to the chunk, not to any record (`palette_offset` and `palette_size`
never cover it), and the parser ignores any trailing `PLDT` bytes beyond the
referenced ranges.

**Parser lax, writer canonical.** The layout above is what the writer emits:
records in order, each index block right after its texels, palettes
concatenated in record order. Trailing `PLDT` padding is tolerated by the
parser and emitted only by the writer. The parser only requires that every
referenced range is in bounds; it accepts any in-bounds offsets, including
overlapping texel ranges, overlapping records, shared palette windows and odd
`palette_offset` values. Only the writer is canonical. A future Python mirror
must follow the parser (in-bounds ranges), not the writer's layout.



## 8. Validation order (normative)

C and Python implement this exact order. A file is accepted only if every step
passes. Python does not implement steps 10 and 15 for `TXDS`/`TXDT`/`PLDT`
(section 9).

1. NULL arguments → `ERR_NULL`.
2. If `size >= 4`, read `version`; `version != 2` → `ERR_VERSION` (so a v1
   file reports the version, not a size error).
3. `size < 32` → `ERR_TRUNCATED`.
4. `header_size != 32` or `reserved0/1 != 0` → `ERR_HEADER`.
5. Reserved file flag bits set → `ERR_FLAGS`.
6. `chunk_count == 0 || chunk_count > 16` → `ERR_CHUNK_COUNT`.
7. `total_size != size` or `total_size > 1 MiB` → `ERR_SIZE`.
8. For each chunk, in order:
   - chunk header past `total_size` → `ERR_CHUNK_BOUNDS`;
   - declared payload past `total_size` (64-bit math) → `ERR_CHUNK_BOUNDS`;
   - `payload_size % 4 != 0` → `ERR_CHUNK_ALIGN`;
   - reserved `chunk_flags` bits or nonzero `reserved` → `ERR_CHUNK_FLAGS`;
   - known type already seen → `ERR_DUP_CHUNK`;
   - unknown type with `REQUIRED` → `ERR_UNSUPPORTED_REQUIRED`; otherwise
     skip and count it.
9. Chunk walk does not end exactly at `total_size` → `ERR_CHUNK_BOUNDS`
   (gaps/trailing bytes).
10. Missing `GXCM` or `META` → `ERR_MISSING_CHUNK`. Then, independently of any
    payload-size cap, `TXDT` or `PLDT` present without `TXDS` (orphan chunks)
    → `ERR_MISSING_CHUNK`; this fires before the step-14 `ERR_TXDT`/`ERR_PLDT`
    caps and before every `ERR_TXDS*` class, so an oversized orphan `TXDT`
    still reports `ERR_MISSING_CHUNK`. Finally, with `TXDS` present, missing
    `TXDT`, or missing `PLDT` while a record needs one → `ERR_MISSING_CHUNK`.
11. `GXCM` without `REQUIRED`; `META`/`RSTA`/`TXDS`/`TXDT`/`PLDT` with
    `REQUIRED` → `ERR_CHUNK_FLAGS`.
12. `GXCM` payload/count errors → `ERR_GXCM`; then cmd-word/id errors →
    `ERR_GXCM_CMD`; then grouping → `ERR_GXCM_GROUP`; then the swap rule →
    `ERR_GXCM_FRAME`.
13. `RSTA` errors → `ERR_RSTA` (whitelist, alignment, width/value consistency,
    `gx_index` bounds and monotonicity).
14. `TXDS`/`TXDT`/`PLDT` errors, in this order (C only for now; orphan
    `TXDT`/`PLDT` was already rejected at step 10):
    - structural record checks: payload `% 40`, record count `0` or `> 256`,
      nonzero `reserved0/1`, format-0 record → `ERR_TXDS`;
    - duplicate (`teximage_param`, `pltt_base`) key, in record order →
      `ERR_TXDS_KEY`;
    - size formula mismatch (`twlhd_v2_tex_sizes()`; format-5 `palette_size`
      must also pass `twlhd_v2_tex_palette_size_ok()` as an even window in
      `[2, 65540]`), or nonzero offset with zero size → `ERR_TXDS_SIZE`;
    - missing `TXDT` / required-but-missing `PLDT` → `ERR_MISSING_CHUNK`;
    - `offset + size` past the chunk payload, computed in 64-bit so an
      overflowing offset is caught (`TXDT` first, then `PLDT`) →
      `ERR_TXDS_BOUNDS`;
    - `TXDT` payload over 524,288 bytes → `ERR_TXDT`; padded `PLDT` payload
      over 98,304 bytes → `ERR_PLDT`.
15. `META` errors → `ERR_META`.

Within step 12 the three passes are separate: all cmd-word checks run before
any grouping check, and all grouping checks run before the swap check. This is
what keeps C and Python identical on malformed input.

## 9. Compatibility and fail-closed behavior

- **v1 stays valid.** `common/twlhd_frame_format.h`, its parser and
  `tools/twlhd_dump.py` are unchanged.
- `twlhd_detect_format()` returns `V1` only for `version == 1 && size ==
  73740`, `V2` only for `version == 2 && size >= 32`, and `UNKNOWN` otherwise.
  In particular, version 1 with the wrong size and version 2 with fewer than
  32 bytes are `UNKNOWN`.
- The v1 parser checks size before version, so a v2 file reports `ERR_SIZE`
  below the v1 size and `ERR_VERSION` at exactly 73740 bytes.
- The v2 parser checks version before size, so a valid v1 file reports
  `ERR_VERSION`.
- `tools/twlhd_dump.py` exits non-zero on a v2 file. At the v1 size its
  message is "has format version 2".
- Unknown optional chunks are skipped and counted. Unknown `REQUIRED` chunks
  are rejected; no parser guesses their semantics.
- `CLPM` is still reserved and unimplemented: the C parser treats it as an
  unknown chunk.
- The C parser and writer implement `TXDS`/`TXDT`/`PLDT` (section 7.1). The
  Python mirror `tools/twlhd_v2_dump.py` was **not** extended in this stage, so
  it still treats those three FourCCs as unknown optional chunks: on a file
  that contains them, Python skips and counts them while C validates them. No
  current producer emits them, so the two implementations agree on every file
  in the test corpus. Extending the mirror is a separate task; this divergence
  is recorded rather than hidden.

## 10. Worked byte example (golden vector)

A 176-byte file: `FRAME_COMPLETE`, frame 1, three chunks, eight `GXCM`
entries, one `RSTA` entry.

```
0x00  02 00 00 00  20 00 00 00  B0 00 00 00  01 00 00 00
0x10  01 00 00 00  03 00 00 00  00 00 00 00  00 00 00 00
0x20  47 58 43 4D  40 00 00 00  01 00 00 00  00 00 00 00
0x30  10 00 00 00  02 00 00 00  15 00 00 00  00 00 00 00
0x40  40 00 00 00  01 00 00 00  20 00 00 00  1F 00 00 00
0x50  23 00 00 00  00 10 00 00  23 00 00 00  00 20 00 00
0x60  41 00 00 00  00 00 00 00  50 00 00 00  00 00 00 00
0x70  4D 45 54 41  10 00 00 00  00 00 00 00  00 00 00 00
0x80  01 00 00 00  07 00 00 00  00 00 00 00  00 00 00 00
0x90  52 53 54 41  10 00 00 00  00 00 00 00  00 00 00 00
0xA0  50 03 00 04  FF 3F 1F 00  03 00 00 00  04 00 00 00
```

Decoded:

- Header: version 2, header 32, total `0xB0` (176), flags `1`, frame 1,
  chunk count 3, reserved words 0.
- `GXCM` at `0x20`, payload 64, `REQUIRED`, reserved 0.
  Entries: `10h/2`, `15h/0`, `40h/1`, `20h/1F`, `23h/1000`, `23h/2000`,
  `41h/0`, `50h/0`. Groups: 1, 1, 1, 1, 2, 1, 1. `SWAP` is last and
  `FRAME_COMPLETE` is set.
- `META` at `0x70`, payload 16, backend 1, coverage 7, reserved 0/0.
- `RSTA` at `0x90`, payload 16: addr `0x04000350`, value `0x001F3FFF`,
  `gx_index` 3, width 4.

The C writer reproduces these bytes exactly (`tests/host/test_v2_write.c`),
and the parser test builds them by hand (`tests/host/test_v2_parse.c`).

## 10.1 Worked byte example with textures

A 352-byte file with the same `GXCM`/`META`/`RSTA` prefix as section 10 plus two
8x8 I2 records (`TXDS` 80 B, `TXDT` 32 B, `PLDT` 16 B). Mirrored by the literal
fixture in `tests/host/test_v2_write.c`.

```
0x00  02 00 00 00  20 00 00 00  60 01 00 00  01 00 00 00
0x10  01 00 00 00  06 00 00 00  00 00 00 00  00 00 00 00
0x20  47 58 43 4D  40 00 00 00  01 00 00 00  00 00 00 00
0x30  10 00 00 00  02 00 00 00  15 00 00 00  00 00 00 00
0x40  40 00 00 00  01 00 00 00  20 00 00 00  1F 00 00 00
0x50  23 00 00 00  00 10 00 00  23 00 00 00  00 20 00 00
0x60  41 00 00 00  00 00 00 00  50 00 00 00  00 00 00 00
0x70  4D 45 54 41  10 00 00 00  00 00 00 00  00 00 00 00
0x80  01 00 00 00  07 00 00 00  00 00 00 00  00 00 00 00
0x90  52 53 54 41  10 00 00 00  00 00 00 00  00 00 00 00
0xA0  50 03 00 04  FF 3F 1F 00  03 00 00 00  04 00 00 00
0xB0  54 58 44 53  50 00 00 00  00 00 00 00  00 00 00 00
0xC0  00 00 00 08  00 00 00 00  00 00 00 00  10 00 00 00
0xD0  00 00 00 00  08 00 00 00  00 00 00 00  00 00 00 00
0xE0  00 00 00 00  00 00 00 00  00 00 00 08  01 00 00 00
0xF0  10 00 00 00  10 00 00 00  08 00 00 00  08 00 00 00
0x100  00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
0x110  54 58 44 54  20 00 00 00  00 00 00 00  00 00 00 00
0x120  00 01 02 03  04 05 06 07  08 09 0A 0B  0C 0D 0E 0F
0x130  10 21 32 43  54 65 76 87  98 A9 BA CB  DC ED FE 0F
0x140  50 4C 44 54  10 00 00 00  00 00 00 00  00 00 00 00
0x150  00 1F 10 2E  20 3D 30 4C  40 5B 50 6A  60 79 70 88
```

Decoded (beyond the section 10 prefix):

- Header: total `0x160` (352), chunk count 6.
- `TXDS` at `0xB0`, payload 80, `REQUIRED` clear. Two 40-byte records:
  - `0xC0`: `TEXIMAGE_PARAM 0x08000000` (I2, 8x8), `PLTT_BASE 0`, texel 0/16,
    palette 0/8, index 0/0.
  - `0xE8`: `TEXIMAGE_PARAM 0x08000000`, `PLTT_BASE 1`, texel 16/16,
    palette 8/8, index 0/0.
- `TXDT` at `0x110`, payload 32: both 16-byte texel blocks.
- `PLDT` at `0x140`, payload 16: both 8-byte palette blocks.



## 11. GBATEK cross-check

Sources checked while producing this document:

- The in-repo GBATEK-derived table `docs/GEOMETRY_FIFO.md:35-58` matches this
  spec on every id it lists: `10h..1Ah`, `20h..27h`, `40h`, `41h`, `60h`.
- The proposal `docs/PHASE4D_CAPTURE_EXPANSION_PROPOSAL.md` §2 lists the
  remaining ids and names but, as noted there, no parameter counts. The counts
  for `1Bh`, `1Ch`, `28h..2Bh`, `30h..34h`, `50h`, `70h..72h` come from the
  Phase 4D implementation brief's GBATEK-derived table and are the values
  implemented here.
- The RSTA ranges come from the GBATEK 3D rendering-engine register map; the
  labels in section 7 are the ones exposed by `nds/arm9/video.h`.

No GBATEK text is reproduced. Data was not re-fetched in the execution session;
this is the same evidence basis the proposal records (GBATEK / LOCAL).

## 12. Limits

| Limit | Value |
|---|---:|
| File size | 1,048,576 bytes |
| `chunk_count` | 1..16 |
| `GXCM` entries | 1..65,536 |
| `RSTA` entries | 0..4,096 |
| `TXDS` records | 1..256 (40 bytes each) |
| `TXDT` payload | 0..524,288 bytes |
| `PLDT` payload | 0..98,304 bytes |
| Format-5 palette window | 2..65,540 bytes, even |
| Recorder `GXCM` capacity (demo) | 16,384 |
| Recorder `RSTA` capacity (demo) | 256 |
| Recorder serialize buffer | 135,264 bytes |

The 524,288-byte `TXDT` limit is 4 x 128 KiB (the DS texture slots); the
98,304-byte `PLDT` limit is 6 x 16 KiB (the DS palette slots). The demo recorder
does not emit texture chunks, so the serialize-buffer figure is unchanged.

The 65,540-byte format-5 window cap is deliberate: one maximum window plus the
fixed palettes of every other format (64 + 8 + 32 + 512 + 16 = 632 bytes) is
66,172 bytes, which fits the 98,304-byte `PLDT` limit. The capture tool chooses
the window, so a file never needs to carry more palette than the colours it
actually captures; reading colours beyond the captured window is a
decoder-stage error, not a parser error.

The 98,304-byte `PLDT` cap is applied to the padded payload length by both the
parser and the writer. The canonical writer zero-pads (at most 3 bytes) before
the chunk header is written, so writer and parser gate on the same length; the
exact-cap boundary sum (65,540 + 32,762 + 2 pad = 98,304) is exercised by the
host tests.

**Known limit for owner review:** two full format-5 windows (2 x 65,540 =
131,080 bytes) exceed the 98,304-byte `PLDT` cap. The writer concatenates
palettes and cannot share one window between two records, so two such records
cannot be written; the parser can read them if they share a window (section
7.1's lax layout). Raising the cap to hold two windows would also need a review
of the 1 MiB file and `TXDT` limits, so it is left unchanged here.

The serialize-buffer figure is the true maximum for the demo capacities:
32 (header) + 3 x 16 (chunk headers) + 16 (META payload) + 16,384 x 8 + 256 x 16
= 135,264. An earlier draft quoted 135,248, which omitted the mandatory 16-byte
META payload; the code uses the computed 135,264 and documents the discrepancy
here.
