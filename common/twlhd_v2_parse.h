// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - v2 capture format parser.
 *
 * Zero-copy, allocation-free and overflow-safe. The caller's buffer is never
 * cast to a struct; every multi-byte field is read with an explicit
 * little-endian accessor. See docs/TWLHD_V2_FORMAT.md for the normative
 * check order and the chunk semantics.
 *
 * HARDWARE UNVERIFIED: host-tested only.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "twlhd_v2_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TWLHD_V2_OK = 0,
    TWLHD_V2_ERR_NULL,
    TWLHD_V2_ERR_TRUNCATED,
    TWLHD_V2_ERR_VERSION,
    TWLHD_V2_ERR_HEADER,
    TWLHD_V2_ERR_SIZE,
    TWLHD_V2_ERR_FLAGS,
    TWLHD_V2_ERR_CHUNK_COUNT,
    TWLHD_V2_ERR_CHUNK_BOUNDS,
    TWLHD_V2_ERR_CHUNK_ALIGN,
    TWLHD_V2_ERR_CHUNK_FLAGS,
    TWLHD_V2_ERR_DUP_CHUNK,
    TWLHD_V2_ERR_MISSING_CHUNK,
    TWLHD_V2_ERR_UNSUPPORTED_REQUIRED,
    TWLHD_V2_ERR_META,
    TWLHD_V2_ERR_GXCM,
    TWLHD_V2_ERR_GXCM_CMD,
    TWLHD_V2_ERR_GXCM_GROUP,
    TWLHD_V2_ERR_GXCM_FRAME,
    TWLHD_V2_ERR_RSTA,
    TWLHD_V2_ERR_TXDS,
    TWLHD_V2_ERR_TXDS_KEY,
    TWLHD_V2_ERR_TXDS_SIZE,
    TWLHD_V2_ERR_TXDS_BOUNDS,
    TWLHD_V2_ERR_TXDT,
    TWLHD_V2_ERR_PLDT
} TwlhdV2Result;

/* Decoded TXDS record (host struct; the on-disk record is little-endian). */
typedef struct {
    uint32_t teximage_param;
    uint32_t pltt_base;
    uint32_t texel_offset;
    uint32_t texel_size;
    uint32_t palette_offset;
    uint32_t palette_size;
    uint32_t index_offset;
    uint32_t index_size;
} TwlhdV2TexRecord;

typedef struct {
    uint32_t total_size;
    uint32_t flags;        /* TWLHD_V2_FLAG_* */
    uint32_t frame_index;
    uint32_t chunk_count;  /* number of chunk headers walked */
    uint32_t unknown_chunks_skipped;
    uint32_t gxcm_count;   /* GXCM entries */
    const uint8_t *gxcm;   /* pointer into the caller's buffer */
    uint32_t rsta_count;
    const uint8_t *rsta;   /* pointer into the caller's buffer, NULL if absent */
    uint32_t meta_backend_id;
    uint32_t meta_coverage_flags;
    uint32_t txds_count;
    const uint8_t *txds;   /* raw TXDS records, NULL if absent */
    const uint8_t *txdt;   /* texel/index data, NULL if absent */
    uint32_t txdt_size;
    const uint8_t *pldt;   /* palette data, NULL if absent */
    uint32_t pldt_size;
} TwlhdV2View;

/* Parse and validate a complete v2 file. On error *view must be ignored. */
TwlhdV2Result twlhd_v2_parse(const uint8_t *data, size_t size,
                             TwlhdV2View *view);

const char *twlhd_v2_result_string(TwlhdV2Result result);

/* Explicit little-endian GXCM entry accessors (index < gxcm_count). */
uint32_t twlhd_v2_gxcm_cmd_word(const TwlhdV2View *view, uint32_t index);
uint32_t twlhd_v2_gxcm_value(const TwlhdV2View *view, uint32_t index);

/* Explicit little-endian RSTA entry accessors (index < rsta_count). */
uint32_t twlhd_v2_rsta_addr(const TwlhdV2View *view, uint32_t index);
uint32_t twlhd_v2_rsta_value(const TwlhdV2View *view, uint32_t index);
uint32_t twlhd_v2_rsta_gx_index(const TwlhdV2View *view, uint32_t index);
uint32_t twlhd_v2_rsta_width(const TwlhdV2View *view, uint32_t index);

/* Number of TXDS records, 0 when the chunk is absent. */
uint32_t twlhd_v2_txds_count(const TwlhdV2View *view);

/*
 * Decode TXDS record `index` into *out. Returns 1 on success, 0 for a NULL
 * view/out or an out-of-range index.
 */
int twlhd_v2_txds_record(const TwlhdV2View *view, uint32_t index,
                         TwlhdV2TexRecord *out);

/*
 * Read-only pointers into the referenced block, or NULL when the record's size
 * is 0 or the view/index/bounds are invalid. Never dereference past the chunk.
 */
const uint8_t *twlhd_v2_tex_texel_ptr(const TwlhdV2View *view, uint32_t index);
const uint8_t *twlhd_v2_tex_palette_ptr(const TwlhdV2View *view,
                                        uint32_t index);
const uint8_t *twlhd_v2_tex_index_ptr(const TwlhdV2View *view, uint32_t index);

typedef enum {
    TWLHD_FILE_UNKNOWN = 0,
    TWLHD_FILE_V1,
    TWLHD_FILE_V2
} TwlhdFileFormat;

/*
 * Classify a buffer by its version word without fully validating it.
 * version == 1 requires size == 73740; version == 2 requires size >= 32;
 * anything else is TWLHD_FILE_UNKNOWN.
 */
TwlhdFileFormat twlhd_detect_format(const uint8_t *data, size_t size);

/* Size of a v1 file (kept here so the compat test does not depend on v1). */
#define TWLHD_V2_V1_WIRE_SIZE 73740u

#ifdef __cplusplus
}
#endif
