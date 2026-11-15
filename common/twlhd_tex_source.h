// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5C-2 - source-buffer read backend for the texture capture
 * planner.
 *
 * The ROM registers its own main-RAM source arrays by (kind, base offset,
 * length, pointer). The planner's read callback then resolves
 * (TWLHD_TEX_CAP_READ_TEXEL / PALETTE / INDEX) offsets against those regions.
 * Never touches MMIO, VRAM or malloc. See docs/TEXTURE_CAPTURE_DESIGN.md,
 * "Status 5C-2".
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWLHD_TEX_SOURCE_REGION_MAX 16u

typedef struct {
    uint32_t kind;
    uint32_t base;
    uint32_t length;
    const uint8_t *data;
} TwlhdTexSourceRegion;

typedef struct {
    uint32_t count;
    TwlhdTexSourceRegion regions[TWLHD_TEX_SOURCE_REGION_MAX];
} TwlhdTexSourceCtx;

/* Reset the region table (count = 0). NULL is ignored. */
void twlhd_tex_source_reset(TwlhdTexSourceCtx *ctx);

/*
 * Register one region. Returns 0 on success, nonzero on error (NULL ctx/ram,
 * zero length, or table full). Regions may be added in any order and may
 * overlap; the first matching region wins.
 */
int twlhd_tex_source_add(TwlhdTexSourceCtx *ctx, uint32_t kind,
                         uint32_t base, uint32_t length, const uint8_t *ram);

/*
 * Planner read callback. Returns 0 when [offset, offset+bytes) is fully
 * contained in a registered region of the requested kind. dst == NULL is a
 * probe that copies nothing. A zero-length read succeeds iff offset lies
 * inside a matching region. Partial overlap, unknown kind and unregistered
 * offsets fail (return nonzero) without copying anything.
 */
int twlhd_tex_source_read(void *ctx, uint32_t kind, uint32_t offset,
                          uint32_t bytes, uint8_t *dst);

#ifdef __cplusplus
}
#endif
