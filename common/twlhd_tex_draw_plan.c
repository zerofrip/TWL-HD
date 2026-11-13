// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2e-2 - host-testable draw-plan logic for the ARM11 textured
 * renderer path.
 *
 * Pure C11, allocation-free; see common/twlhd_tex_draw_plan.h.
 */
#include "twlhd_tex_draw_plan.h"

#include <stddef.h>
#include <string.h>

#define TEX_DRAW_ALIGN_MASK 127u

/*
 * Wrap identity: TwlhdPicaWrap values were defined to match the PICA200
 * register values (common/twlhd_pica_tex.h:40-45), which are in turn identical
 * to libctru GPU_TEXTURE_WRAP_PARAM (3ds/gpu/enums.h:35-41).
 */
static uint32_t tex_draw_wrap_code(TwlhdPicaWrap wrap) {
    return (uint32_t)wrap;
}

TwlhdTexDrawVerdict twlhd_tex_draw_plan_build(
    const TwlhdPicaBindPlan *plan, uint32_t entry_count,
    const int32_t *entry_texture,
    const TwlhdPicaFrameEntryStatus *entry_status,
    const TwlhdPicaFrameTexture *textures, uint32_t texture_count,
    const uint8_t *arena, uint32_t arena_used,
    uint32_t max_textures, uint32_t max_arena_bytes,
    uint32_t max_texture_bytes, uint32_t max_texcoords,
    uint32_t vertex_count,
    TwlhdTexDrawEntry *out_entries, uint32_t out_capacity,
    TwlhdTexDrawPlanResult *out_result) {

    uint32_t i;
    int all_untextured = 1;

    if (out_result) {
        out_result->verdict = TWLHD_TEX_DRAW_FALLBACK_BUDGET;
        out_result->entry_count = 0;
        out_result->texture_count = 0;
        out_result->all_untextured = 1;
    }

    if (!plan || !entry_texture || !entry_status || !textures ||
        !out_entries || !out_result) {
        return TWLHD_TEX_DRAW_FALLBACK_CAPACITY;
    }

    if (entry_count == 0u) {
        out_result->verdict = TWLHD_TEX_DRAW_NO_TEXTURE;
        return TWLHD_TEX_DRAW_NO_TEXTURE;
    }

    if (entry_count > out_capacity || texture_count > max_textures ||
        arena_used > max_arena_bytes || vertex_count > max_texcoords) {
        return TWLHD_TEX_DRAW_FALLBACK_BUDGET;
    }

    if (arena_used > 0u &&
        (arena == NULL ||
         ((uintptr_t)arena & TEX_DRAW_ALIGN_MASK) != 0u)) {
        return TWLHD_TEX_DRAW_FALLBACK_ALIGN;
    }

    for (i = 0; i < texture_count; i++) {
        uint32_t w = textures[i].width;
        uint32_t h = textures[i].height;
        if (w < 8u || h < 8u || w > 1024u || h > 1024u ||
            (w & (w - 1u)) != 0u || (h & (h - 1u)) != 0u ||
            (w * h * 4u) > max_texture_bytes) {
            return TWLHD_TEX_DRAW_FALLBACK_BUDGET;
        }
    }

    for (i = 0; i < entry_count; i++) {
        TwlhdTexDrawEntry *out = &out_entries[i];
        int32_t slot = entry_texture[i];
        int is_untextured = (slot < 0 || slot >= (int32_t)texture_count ||
                             plan[i].untextured ||
                             entry_status[i] != TWLHD_PICA_FRAME_ENTRY_OK);

        out->first_vertex = plan[i].first_vertex;
        out->vertex_count = plan[i].vertex_count;
        out->texture_slot = is_untextured ? -1 : slot;
        out->filter = TWLHD_TEX_DRAW_FILTER_NEAREST;
        out->untextured = is_untextured;

        if (is_untextured) {
            out->wrap_s = TWLHD_TEX_DRAW_WRAP_CLAMP_TO_EDGE;
            out->wrap_t = TWLHD_TEX_DRAW_WRAP_CLAMP_TO_EDGE;
        } else {
            const TwlhdPicaFrameTexture *tex = &textures[slot];
            out->wrap_s = tex_draw_wrap_code(tex->wrap_s);
            out->wrap_t = tex_draw_wrap_code(tex->wrap_t);
            all_untextured = 0;
        }
    }

    out_result->verdict =
        all_untextured ? TWLHD_TEX_DRAW_NO_TEXTURE : TWLHD_TEX_DRAW_OK;
    out_result->entry_count = entry_count;
    out_result->texture_count = texture_count;
    out_result->all_untextured = all_untextured;

    return out_result->verdict;
}

const char *twlhd_tex_draw_verdict_string(TwlhdTexDrawVerdict verdict) {
    switch (verdict) {
    case TWLHD_TEX_DRAW_OK:               return "OK";
    case TWLHD_TEX_DRAW_NO_TEXTURE:       return "NO_TEXTURE";
    case TWLHD_TEX_DRAW_FALLBACK_BUDGET:  return "FALLBACK_BUDGET";
    case TWLHD_TEX_DRAW_FALLBACK_CAPACITY:return "FALLBACK_CAPACITY";
    case TWLHD_TEX_DRAW_FALLBACK_ALIGN:   return "FALLBACK_ALIGN";
    default:                              return "UNKNOWN";
    }
}

void twlhd_tex_lifecycle_init(TwlhdTexLifecycleState *s) {
    memset(s, 0, sizeof(*s));
}

TwlhdTexLifecycleAction twlhd_tex_lifecycle_stage(TwlhdTexLifecycleState *s) {
    TwlhdTexLifecycleAction a = s->pending_ready
                                    ? TWLHD_TEX_LIFECYCLE_FREE_PENDING_NOW
                                    : TWLHD_TEX_LIFECYCLE_NONE;
    s->pending_ready = 1;
    s->textured_mode = 1;
    return a;
}

TwlhdTexLifecycleAction twlhd_tex_lifecycle_clear(TwlhdTexLifecycleState *s) {
    TwlhdTexLifecycleAction a = s->pending_ready
                                    ? TWLHD_TEX_LIFECYCLE_FREE_PENDING_NOW
                                    : TWLHD_TEX_LIFECYCLE_NONE;
    s->pending_ready = 0;
    s->textured_mode = 0;
    if (s->active_ready) {
        s->drop_active_on_commit = 1;
    }
    return a;
}

TwlhdTexLifecycleAction twlhd_tex_lifecycle_commit(TwlhdTexLifecycleState *s) {
    TwlhdTexLifecycleAction a = TWLHD_TEX_LIFECYCLE_NONE;

    if (s->drop_active_on_commit) {
        if (s->active_ready) {
            a |= TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW;
        }
        s->active_ready = 0;
        s->drop_active_on_commit = 0;
    }

    if (s->pending_ready) {
        if (s->active_ready) {
            a |= TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW;
            s->active_ready = 0;
        }
        s->active_ready = 1;
        s->pending_ready = 0;
        s->textured_mode = 1;
    }

    return a;
}

TwlhdTexLifecycleAction twlhd_tex_lifecycle_shutdown(TwlhdTexLifecycleState *s) {
    TwlhdTexLifecycleAction a = TWLHD_TEX_LIFECYCLE_NONE;
    if (s->pending_ready) {
        a |= TWLHD_TEX_LIFECYCLE_FREE_PENDING_NOW;
    }
    if (s->active_ready) {
        a |= TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW;
    }
    s->pending_ready = 0;
    s->active_ready = 0;
    s->textured_mode = 0;
    s->drop_active_on_commit = 0;
    return a;
}
