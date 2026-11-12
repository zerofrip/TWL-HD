// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2e-1 - host-side textured frame preparation.
 *
 * Pure C11, allocation-free: see common/twlhd_pica_frame.h for the contract,
 * the memory budget and the BUILD/UNIT-only status. The texture layout is the
 * one produced by twlhd_pica_tex_swizzle_rgba8(); this module only decides
 * which textures are needed, where they go and which texcoords use them.
 */
#include "twlhd_pica_frame.h"

#include <stddef.h>

#include "twlhd_tex_decode.h"

#define FRAME_FORMAT(param) (((param) >> 26) & 7u)

static int frame_size_ok(uint32_t v) {
    return v >= 8u && v <= 1024u && (v & (v - 1u)) == 0u;
}

static int frame_same_key(const TwlhdGxTexVertex *tex, uint32_t a, uint32_t b) {
    return tex[a].teximage_param == tex[b].teximage_param &&
           tex[a].pltt_base == tex[b].pltt_base;
}

void twlhd_pica_frame_default_limits(TwlhdPicaFrameLimits *out) {
    if (!out) return;
    out->max_textures = TWLHD_PICA_FRAME_MAX_TEXTURES;
    out->arena_bytes = TWLHD_PICA_FRAME_ARENA_MAX_BYTES;
    out->max_texcoords = TWLHD_PICA_FRAME_MAX_TEXCOORDS;
}

static int frame_find_record(const TwlhdV2View *view, uint32_t param,
                             uint32_t pltt, uint32_t *out_index,
                             TwlhdV2TexRecord *out_rec) {
    uint32_t count = twlhd_v2_txds_count(view);
    uint32_t i;

    for (i = 0; i < count; i++) {
        TwlhdV2TexRecord rec;

        if (!twlhd_v2_txds_record(view, i, &rec)) return 0;
        if (rec.teximage_param == param && rec.pltt_base == pltt) {
            *out_index = i;
            *out_rec = rec;
            return 1;
        }
    }
    return 0;
}

/*
 * Classify one run (the triangle whose first vertex is `first`).
 *
 * Returns 0 when the run was classified (the out parameters are written) and
 * -1 when a texture that would decode needs more scratch than scratch_size.
 * The texture image is decoded into scratch when the run is texturable, so a
 * later pass can swizzle it without decoding a second time.
 */
static int frame_eval_run(const TwlhdGxTexVertex *tex,
                          const TwlhdV2View *view, uint8_t *scratch,
                          uint32_t scratch_size, uint32_t first,
                          uint32_t *out_param, uint32_t *out_pltt,
                          int *out_untextured,
                          TwlhdPicaFrameEntryStatus *out_status,
                          uint32_t *out_width, uint32_t *out_height,
                          uint32_t *out_bytes) {
    uint32_t param = tex[first].teximage_param;
    uint32_t pltt = tex[first].pltt_base;
    uint32_t format = FRAME_FORMAT(param);
    uint32_t rec_index = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytes;
    TwlhdV2TexRecord rec;
    const uint8_t *texel;
    const uint8_t *palette;
    const uint8_t *index;

    *out_param = param;
    *out_pltt = pltt;
    *out_width = 0;
    *out_height = 0;
    *out_bytes = 0;

    if (format == 0u) {
        *out_untextured = 1;
        *out_status = TWLHD_PICA_FRAME_ENTRY_OK;
        return 0;
    }
    *out_untextured = 0;

    if (!frame_find_record(view, param, pltt, &rec_index, &rec)) {
        *out_status = TWLHD_PICA_FRAME_ENTRY_NO_DATA;
        return 0;
    }

    texel = twlhd_v2_tex_texel_ptr(view, rec_index);
    palette = twlhd_v2_tex_palette_ptr(view, rec_index);
    index = twlhd_v2_tex_index_ptr(view, rec_index);
    if (!texel || (format != 7u && !palette) || (format == 5u && !index)) {
        *out_status = TWLHD_PICA_FRAME_ENTRY_NO_DATA;
        return 0;
    }

    if (!twlhd_tex_dimensions(param, &width, &height) ||
        !frame_size_ok(width) || !frame_size_ok(height)) {
        *out_status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return 0;
    }

    bytes = width * height * 4u;
    if (bytes > scratch_size) return -1;

    if (twlhd_tex_decode(&rec, texel, palette, index, scratch, scratch_size) !=
        TWLHD_TEX_OK) {
        *out_status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return 0;
    }

    *out_status = TWLHD_PICA_FRAME_ENTRY_OK;
    *out_width = width;
    *out_height = height;
    *out_bytes = bytes;
    return 0;
}

static uint32_t frame_count_runs(const TwlhdGxTexVertex *tex,
                                 uint32_t vertex_count) {
    uint32_t runs = 0;
    uint32_t i;

    for (i = 0; i < vertex_count; i += 3u) {
        if (i == 0u || !frame_same_key(tex, i, i - 3u)) runs++;
    }
    return runs;
}

TwlhdPicaFrameResult twlhd_pica_frame_prepare(
    const TwlhdGxTexVertex *tex, uint32_t vertex_count,
    const TwlhdV2View *view,
    TwlhdPicaBindPlan *plan, uint32_t plan_capacity,
    TwlhdPicaFrameTexture *textures, uint32_t texture_capacity,
    uint8_t *arena, uint32_t arena_capacity,
    TwlhdPicaTexCoord *texcoords, uint32_t texcoord_capacity,
    int32_t *entry_texture, TwlhdPicaFrameEntryStatus *entry_status,
    uint32_t entry_capacity,
    uint8_t *scratch, uint32_t scratch_size,
    uint32_t *out_entry_count, uint32_t *out_texture_count,
    uint32_t *out_arena_used) {
    uint32_t key_param[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t key_pltt[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t runs;
    uint32_t tex_count = 0;
    uint32_t arena_used = 0;
    uint32_t run_index = 0;
    uint32_t i;
    uint32_t j;

    if (!tex || !view || !plan || !textures || !arena || !texcoords ||
        !entry_texture || !entry_status || !scratch || !out_entry_count ||
        !out_texture_count || !out_arena_used) {
        return TWLHD_PICA_FRAME_ERR_NULL;
    }
    if (vertex_count % 3u != 0u) return TWLHD_PICA_FRAME_ERR_COUNT;

    runs = frame_count_runs(tex, vertex_count);
    if (runs > plan_capacity || runs > entry_capacity ||
        vertex_count > texcoord_capacity) {
        return TWLHD_PICA_FRAME_ERR_CAPACITY;
    }

    /* Validation/sizing pass. Writes only scratch. */
    for (i = 0; i < vertex_count; i += 3u) {
        TwlhdPicaFrameEntryStatus status;
        uint32_t param;
        uint32_t pltt;
        uint32_t width;
        uint32_t height;
        uint32_t bytes;
        int untextured;

        if (i != 0u && frame_same_key(tex, i, i - 3u)) continue;

        if (frame_eval_run(tex, view, scratch, scratch_size, i, &param, &pltt,
                           &untextured, &status, &width, &height, &bytes) < 0) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        if (status != TWLHD_PICA_FRAME_ENTRY_OK || untextured) continue;

        for (j = 0; j < tex_count; j++) {
            if (key_param[j] == param && key_pltt[j] == pltt) break;
        }
        if (j != tex_count) continue;

        if (tex_count >= TWLHD_PICA_FRAME_MAX_TEXTURES ||
            tex_count >= texture_capacity) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        key_param[tex_count] = param;
        key_pltt[tex_count] = pltt;
        tex_count++;
        arena_used += bytes;
    }

    if (arena_used > arena_capacity) return TWLHD_PICA_FRAME_ERR_ARENA;

    /* Write pass: every capacity was checked above. */
    if (twlhd_pica_tex_bind_plan(tex, vertex_count, plan, plan_capacity,
                                 &runs) != TWLHD_PICA_TEX_OK) {
        return TWLHD_PICA_FRAME_ERR_CAPACITY; /* cannot happen */
    }

    tex_count = 0;
    arena_used = 0;
    j = 0;
    for (i = 0; i <= vertex_count; i += 3u) {
        int boundary = i == vertex_count ||
                       (i != 0u && !frame_same_key(tex, i, i - 3u));
        uint32_t param;
        uint32_t pltt;
        uint32_t width;
        uint32_t height;
        uint32_t bytes;
        uint32_t vc;
        TwlhdPicaFrameEntryStatus status;
        int32_t tex_index = -1;
        int untextured;

        if (!boundary) continue;

        vc = i - j;
        if (frame_eval_run(tex, view, scratch, scratch_size, j, &param, &pltt,
                           &untextured, &status, &width, &height, &bytes) < 0) {
            /* Pass 1 proved the scratch is large enough. */
            status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        }

        if (status == TWLHD_PICA_FRAME_ENTRY_OK && !untextured) {
            uint32_t t;

            for (t = 0; t < tex_count; t++) {
                if (key_param[t] == param && key_pltt[t] == pltt) break;
            }
            if (t == tex_count) {
                if (twlhd_pica_tex_swizzle_rgba8(scratch, width, height,
                                                 arena + arena_used,
                                                 arena_capacity - arena_used) !=
                    TWLHD_PICA_TEX_OK) {
                    status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
                } else {
                    textures[tex_count].offset = arena_used;
                    textures[tex_count].width = width;
                    textures[tex_count].height = height;
                    (void)twlhd_pica_wrap_from_teximage(
                        param, &textures[tex_count].wrap_s,
                        &textures[tex_count].wrap_t);
                    tex_index = (int32_t)tex_count;
                    arena_used += bytes;
                    tex_count++;
                }
            } else {
                tex_index = (int32_t)t;
            }
        }

        entry_status[run_index] = status;
        entry_texture[run_index] = tex_index;
        run_index++;

        if (status == TWLHD_PICA_FRAME_ENTRY_OK && !untextured &&
            tex_index >= 0) {
            if (twlhd_pica_texcoords_from_gx(tex + j, vc, width, height,
                                             texcoords + j, vc) !=
                TWLHD_PICA_TEX_OK) {
                uint32_t v;
                for (v = j; v < i; v++) {
                    texcoords[v].u = 0.0f;
                    texcoords[v].v = 0.0f;
                }
            }
        } else {
            uint32_t v;
            for (v = j; v < i; v++) {
                texcoords[v].u = 0.0f;
                texcoords[v].v = 0.0f;
            }
        }

        j = i;
    }

    *out_entry_count = runs;
    *out_texture_count = tex_count;
    *out_arena_used = arena_used;
    return TWLHD_PICA_FRAME_OK;
}

TwlhdPicaFrameResult twlhd_pica_frame_arena_needed(
    const TwlhdGxTexVertex *tex, uint32_t vertex_count,
    const TwlhdV2View *view, uint8_t *scratch, uint32_t scratch_size,
    uint32_t *out_arena_bytes) {
    uint32_t key_param[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t key_pltt[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t tex_count = 0;
    uint32_t arena_used = 0;
    uint32_t i;
    uint32_t j;

    if (!tex || !view || !scratch || !out_arena_bytes) {
        return TWLHD_PICA_FRAME_ERR_NULL;
    }
    if (vertex_count % 3u != 0u) return TWLHD_PICA_FRAME_ERR_COUNT;

    for (i = 0; i < vertex_count; i += 3u) {
        TwlhdPicaFrameEntryStatus status;
        uint32_t param;
        uint32_t pltt;
        uint32_t width;
        uint32_t height;
        uint32_t bytes;
        int untextured;

        if (i != 0u && frame_same_key(tex, i, i - 3u)) continue;

        if (frame_eval_run(tex, view, scratch, scratch_size, i, &param, &pltt,
                           &untextured, &status, &width, &height, &bytes) < 0) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        if (status != TWLHD_PICA_FRAME_ENTRY_OK || untextured) continue;

        for (j = 0; j < tex_count; j++) {
            if (key_param[j] == param && key_pltt[j] == pltt) break;
        }
        if (j != tex_count) continue;

        if (tex_count >= TWLHD_PICA_FRAME_MAX_TEXTURES) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        key_param[tex_count] = param;
        key_pltt[tex_count] = pltt;
        tex_count++;
        arena_used += bytes;
    }

    *out_arena_bytes = arena_used;
    return TWLHD_PICA_FRAME_OK;
}

const char *twlhd_pica_frame_result_string(TwlhdPicaFrameResult result) {
    switch (result) {
    case TWLHD_PICA_FRAME_OK:
        return "OK";
    case TWLHD_PICA_FRAME_ERR_NULL:
        return "ERR_NULL";
    case TWLHD_PICA_FRAME_ERR_CAPACITY:
        return "ERR_CAPACITY";
    case TWLHD_PICA_FRAME_ERR_ARENA:
        return "ERR_ARENA";
    case TWLHD_PICA_FRAME_ERR_COUNT:
        return "ERR_COUNT";
    default:
        return "ERR_UNKNOWN";
    }
}

const char *twlhd_pica_frame_entry_status_string(
    TwlhdPicaFrameEntryStatus status) {
    switch (status) {
    case TWLHD_PICA_FRAME_ENTRY_OK:
        return "OK";
    case TWLHD_PICA_FRAME_ENTRY_NO_DATA:
        return "NO_DATA";
    case TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED:
        return "UNSUPPORTED";
    default:
        return "ERR_UNKNOWN";
    }
}
