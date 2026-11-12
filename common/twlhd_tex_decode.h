// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2c - host-side DS texture decoder (formats 1-7) to RGBA8.
 *
 * Pure C11, no allocation, no libm: the caller owns every buffer. The decoder
 * follows DeSmuME desmume/src/texcache.cpp (see docs/DS_GX_RESEARCH.md,
 * "Decoder (5B-2c)") and is host-tested only (HARDWARE UNVERIFIED).
 *
 * Output layout: width*height*4 bytes, row-major with the top row first and
 * the bytes in R, G, B, A order. A pixel that DeSmuME models as fully
 * transparent is written as 0,0,0,0 (see the docs; palette index 0 with the
 * key-colour flag and 4x4 block modes 0/1 texel 3).
 *
 * The output buffer is left untouched on every error path: all validation,
 * including the 4x4 palette-window pre-scan, happens before the first write.
 */
#pragma once

#include <stdint.h>

#include "twlhd_v2_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TWLHD_TEX_OK = 0,
    TWLHD_TEX_ERR_NULL,         /* NULL record/texel/out, or palette/index */
    TWLHD_TEX_ERR_FORMAT,       /* packed format 0 */
    TWLHD_TEX_ERR_SIZE,         /* texel/index size != twlhd_v2_tex_sizes() */
    TWLHD_TEX_ERR_PALETTE_SIZE, /* !twlhd_v2_tex_palette_size_ok() */
    TWLHD_TEX_ERR_OUTPUT_SIZE,  /* out_size < width*height*4 */
    TWLHD_TEX_ERR_PALETTE_RANGE /* a palette read beyond the supplied bytes */
} TwlhdTexResult;

/*
 * Decode one texture described by *rec into out (width*height*4 bytes).
 *
 * texel/palette/index point at the record's data start, i.e. what
 * twlhd_v2_tex_texel_ptr()/palette_ptr()/index_ptr() return. texel and out are
 * always required. palette is required for formats 1-6 (format 5 reads its
 * per-block window from it), index for format 5 only.
 *
 * Sizes are checked against twlhd_v2_tex_sizes() and
 * twlhd_v2_tex_palette_size_ok(); no formula is duplicated here.
 */
TwlhdTexResult twlhd_tex_decode(const TwlhdV2TexRecord *rec,
                                const uint8_t *texel, const uint8_t *palette,
                                const uint8_t *index, uint8_t *out,
                                uint32_t out_size);

/*
 * width = 8 << SizeShiftS, height = 8 << SizeShiftT (bits 20-22 / 23-25), so
 * both are 8..1024 and width*height <= 1 MiB. The 3-bit shifts cannot
 * overflow a uint32_t. Returns 1 for formats 1-7 and 0 for format 0, in which
 * case the width and height outputs are untouched. NULL outputs are ignored.
 */
int twlhd_tex_dimensions(uint32_t teximage_param, uint32_t *width,
                         uint32_t *height);

const char *twlhd_tex_result_string(TwlhdTexResult result);

#ifdef __cplusplus
}
#endif
