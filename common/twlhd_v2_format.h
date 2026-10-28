// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - v2 capture format constants and tables.
 *
 * The normative description is docs/TWLHD_V2_FORMAT.md. This header is the
 * single source of truth for the command id + parameter count table shared by
 * the parser, the writer and the GX recorder.
 *
 * All on-disk fields are little-endian. Nothing here is hardware-specific:
 * the module is pure C11 and is host-tested only (HARDWARE UNVERIFIED).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWLHD_V2_VERSION                 2u
#define TWLHD_V2_HEADER_SIZE             32u
#define TWLHD_V2_CHUNK_HEADER_SIZE       16u
#define TWLHD_V2_FILE_MAX                1048576u /* 1 MiB */
#define TWLHD_V2_GXCM_MAX_ENTRIES        65536u
#define TWLHD_V2_RSTA_MAX_ENTRIES        4096u
#define TWLHD_V2_CHUNK_COUNT_MAX         16u
#define TWLHD_V2_META_PAYLOAD_SIZE       16u
#define TWLHD_V2_GXCM_ENTRY_SIZE         8u
#define TWLHD_V2_RSTA_ENTRY_SIZE         16u

/* FourCC packing: 'a' occupies the lowest byte, matching little-endian order. */
#define TWLHD_FOURCC(a, b, c, d)                                             \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) |                 \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define TWLHD_V2_CHUNK_GXCM  TWLHD_FOURCC('G', 'X', 'C', 'M')
#define TWLHD_V2_CHUNK_META  TWLHD_FOURCC('M', 'E', 'T', 'A')
#define TWLHD_V2_CHUNK_RSTA  TWLHD_FOURCC('R', 'S', 'T', 'A')
/* Reserved, NOT implemented. Parsers treat these as unknown chunks. */
#define TWLHD_V2_CHUNK_CLPM  TWLHD_FOURCC('C', 'L', 'P', 'M')
#define TWLHD_V2_CHUNK_TXDS  TWLHD_FOURCC('T', 'X', 'D', 'S')
#define TWLHD_V2_CHUNK_TXDT  TWLHD_FOURCC('T', 'X', 'D', 'T')
#define TWLHD_V2_CHUNK_PLDT  TWLHD_FOURCC('P', 'L', 'D', 'T')

/* File header flags (offset 12). */
#define TWLHD_V2_FLAG_FRAME_COMPLETE     0x00000001u
#define TWLHD_V2_FLAG_OVERFLOW           0x00000002u
#define TWLHD_V2_FLAG_RESERVED_MASK      0xFFFFFFFCu

/* Chunk header flags (offset 8 inside a chunk header). */
#define TWLHD_V2_CHUNK_FLAG_REQUIRED     0x00000001u
#define TWLHD_V2_CHUNK_FLAG_RESERVED     0xFFFFFFFEu

/* META coverage_flags bits. */
#define TWLHD_V2_COVERAGE_LIBRARY_INTERNAL_WRITES 0x00000001u
#define TWLHD_V2_COVERAGE_DMA_DISPLAY_LISTS       0x00000002u
#define TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN   0x00000004u
#define TWLHD_V2_COVERAGE_RESERVED_MASK           0xFFFFFFF8u

/* META backend_id values. Other values are tolerated by the parser. */
#define TWLHD_V2_BACKEND_UNKNOWN          0u
#define TWLHD_V2_BACKEND_REGISTER_PROXY   1u
#define TWLHD_V2_BACKEND_SYNTHETIC        2u

/*
 * GXCM cmd_word bit 16 marks an opaque write to the raw FIFO register
 * (0x04000400). Bits 0..7 must be zero for such an entry; for a normal entry
 * they hold the command id and every other bit is zero.
 */
#define TWLHD_V2_RAW_FIFO_BIT            0x00010000u
#define TWLHD_V2_CMD_ID_MASK             0x000000FFu
#define TWLHD_V2_CMD_NONRAW_MASK         0xFFFFFF00u

/* GX command ids valid in a non-RAW GXCM entry. */
#define TWLHD_V2_CMD_MTX_MODE        0x10u
#define TWLHD_V2_CMD_MTX_PUSH        0x11u
#define TWLHD_V2_CMD_MTX_POP         0x12u
#define TWLHD_V2_CMD_MTX_STORE       0x13u
#define TWLHD_V2_CMD_MTX_RESTORE     0x14u
#define TWLHD_V2_CMD_MTX_IDENTITY    0x15u
#define TWLHD_V2_CMD_MTX_LOAD_4x4    0x16u
#define TWLHD_V2_CMD_MTX_LOAD_4x3    0x17u
#define TWLHD_V2_CMD_MTX_MULT_4x4    0x18u
#define TWLHD_V2_CMD_MTX_MULT_4x3    0x19u
#define TWLHD_V2_CMD_MTX_MULT_3x3    0x1Au
#define TWLHD_V2_CMD_MTX_SCALE       0x1Bu
#define TWLHD_V2_CMD_MTX_TRANS       0x1Cu
#define TWLHD_V2_CMD_COLOR           0x20u
#define TWLHD_V2_CMD_NORMAL          0x21u
#define TWLHD_V2_CMD_TEX_COORD       0x22u
#define TWLHD_V2_CMD_VTX_16          0x23u
#define TWLHD_V2_CMD_VTX_10          0x24u
#define TWLHD_V2_CMD_VTX_XY          0x25u
#define TWLHD_V2_CMD_VTX_XZ          0x26u
#define TWLHD_V2_CMD_VTX_YZ          0x27u
#define TWLHD_V2_CMD_VTX_DIFF        0x28u
#define TWLHD_V2_CMD_POLYGON_ATTR    0x29u
#define TWLHD_V2_CMD_TEXIMAGE_PARAM  0x2Au
#define TWLHD_V2_CMD_PLTT_BASE       0x2Bu
#define TWLHD_V2_CMD_DIF_AMB         0x30u
#define TWLHD_V2_CMD_SPE_EMI         0x31u
#define TWLHD_V2_CMD_LIGHT_VECTOR    0x32u
#define TWLHD_V2_CMD_LIGHT_COLOR     0x33u
#define TWLHD_V2_CMD_SHININESS       0x34u
#define TWLHD_V2_CMD_BEGIN_VTXS      0x40u
#define TWLHD_V2_CMD_END_VTXS        0x41u
#define TWLHD_V2_CMD_SWAP_BUFFERS    0x50u
#define TWLHD_V2_CMD_VIEWPORT        0x60u
#define TWLHD_V2_CMD_BOX_TEST        0x70u
#define TWLHD_V2_CMD_POS_TEST        0x71u
#define TWLHD_V2_CMD_VEC_TEST        0x72u

/*
 * Number of GXCM *entries* consumed by one command, i.e. one entry per port
 * write. A zero count still consumes exactly one entry (the dummy write that
 * carries the command id), which callers express as "expected entries".
 *
 * Returns 1 and writes *out_count when id is valid, 0 otherwise.
 */
int twlhd_v2_cmd_param_count(uint32_t id, uint8_t *out_count);

/* Expected number of entries for a valid command id (never 0). */
uint8_t twlhd_v2_cmd_entry_count(uint32_t id);

/*
 * True when an RSTA entry (addr, width) is legal: width is 1, 2 or 4, addr is
 * aligned to width, and [addr, addr+width) lies entirely inside ONE whitelisted
 * rendering-register range.
 */
int twlhd_v2_rsta_whitelisted(uint32_t addr, uint32_t width);

/* True when addr lies inside any whitelisted range (width ignored). */
int twlhd_v2_rsta_addr_in_range(uint32_t addr);

/* Input entry layouts for the writer and recorder. Host structs only. */
typedef struct {
    uint32_t cmd_word;
    uint32_t value;
} TwlhdV2CmdEntry;

typedef struct {
    uint32_t addr;
    uint32_t value;
    uint32_t gx_index;
    uint32_t width;
} TwlhdV2RstaEntry;

#ifdef __cplusplus
}
#endif
