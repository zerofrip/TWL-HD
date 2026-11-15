// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5C-2 - source-buffer read backend.
 * See common/twlhd_tex_source.h. BUILD/UNIT only.
 */

#include "twlhd_tex_source.h"

#include <string.h>

void twlhd_tex_source_reset(TwlhdTexSourceCtx *ctx) {
    if (!ctx) return;
    ctx->count = 0;
}

int twlhd_tex_source_add(TwlhdTexSourceCtx *ctx, uint32_t kind,
                         uint32_t base, uint32_t length, const uint8_t *ram) {
    TwlhdTexSourceRegion *r;

    if (!ctx || !ram) return 1;
    if (length == 0u) return 1;
    if (ctx->count >= TWLHD_TEX_SOURCE_REGION_MAX) return 1;

    r = &ctx->regions[ctx->count++];
    r->kind = kind;
    r->base = base;
    r->length = length;
    r->data = ram;
    return 0;
}

int twlhd_tex_source_read(void *ctx, uint32_t kind, uint32_t offset,
                          uint32_t bytes, uint8_t *dst) {
    TwlhdTexSourceCtx *src = (TwlhdTexSourceCtx *)ctx;
    uint32_t i;

    if (!src) return 1;

    for (i = 0; i < src->count; i++) {
        TwlhdTexSourceRegion *r = &src->regions[i];
        uint64_t rel;

        if (r->kind != kind) continue;
        if (offset < r->base) continue;

        rel = (uint64_t)offset - (uint64_t)r->base;
        if (bytes == 0u) {
            if (rel < (uint64_t)r->length) return 0;
            continue;
        }
        if (rel + (uint64_t)bytes > (uint64_t)r->length) continue;

        if (dst) memcpy(dst, r->data + rel, bytes);
        return 0;
    }

    return 1;
}
