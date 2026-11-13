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
#include <string.h>

#include "twlhd_tex_decode.h"

#define FRAME_FORMAT(param) (((param) >> 26) & 7u)

/* One classified run: the input key, the record, and the sizing result. */
typedef struct {
    uint32_t param;
    uint32_t pltt;
    TwlhdV2TexRecord record;
    const uint8_t *texel;
    const uint8_t *palette;
    const uint8_t *index;
    int untextured;
    TwlhdPicaFrameEntryStatus status;
    uint32_t width;
    uint32_t height;
    uint32_t bytes;
} FrameRun;

static uint16_t frame_read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

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
 * Fixed palette table size in 16-bit entries, copied from
 * fixed_palette_entries() (common/twlhd_tex_decode.c:54-63). Formats 5
 * (window) and 7 (none) have no fixed table.
 */
static uint32_t frame_fixed_palette_entries(uint32_t format) {
    switch (format) {
    case 1u: return 32u;  /* A3I5 */
    case 2u: return 4u;   /* I2 */
    case 3u: return 16u;  /* I4 */
    case 4u: return 256u; /* I8 */
    case 6u: return 8u;   /* A5I3 */
    default: return 0u;
    }
}

/* 4x4 palette bytes needed by one block of `mode`, copied from
 * mode_palette_bytes() (common/twlhd_tex_decode.c:88-95). */
static uint32_t frame_mode_palette_bytes(uint32_t mode) {
    switch (mode) {
    case 0u: return 6u;
    case 2u: return 8u;
    default: return 4u;
    }
}

/*
 * Classify one run (the triangle whose first vertex is `first`).
 *
 * This is the one no-decode predicate shared by twlhd_pica_frame_prepare()'s
 * validation pass, its write pass and twlhd_pica_frame_arena_needed(): it reads
 * the input view and vertex array and writes only *run. Every refusal
 * twlhd_tex_decode() can make is reproduced here read-only (the file:line
 * citations below are common/twlhd_tex_decode.c), so a run reported OK decodes
 * in the write pass; the only remaining decode failure is ERR_OUTPUT_SIZE,
 * which twlhd_pica_frame_prepare() rules out up front with `bytes >
 * scratch_size` (:186-187).
 *
 * The record and pointer fields stay zero/NULL for an untextured or failed
 * run; the write pass only uses them for status OK.
 */
static void frame_classify_run(const TwlhdGxTexVertex *tex,
                               const TwlhdV2View *view, uint32_t first,
                               FrameRun *run) {
    uint32_t format;
    uint32_t texel_bytes;
    uint32_t index_bytes;
    uint32_t entries;
    uint32_t blocks;
    uint32_t block;
    uint32_t record_index;

    memset(run, 0, sizeof(*run));
    run->param = tex[first].teximage_param;
    run->pltt = tex[first].pltt_base;
    run->status = TWLHD_PICA_FRAME_ENTRY_OK;

    format = FRAME_FORMAT(run->param);
    if (format == 0u) {
        run->untextured = 1; /* :163: format 0 is the untextured path */
        return;
    }

    if (!frame_find_record(view, run->param, run->pltt, &record_index,
                           &run->record)) {
        run->status = TWLHD_PICA_FRAME_ENTRY_NO_DATA;
        return;
    }

    run->texel = twlhd_v2_tex_texel_ptr(view, record_index);
    run->palette = twlhd_v2_tex_palette_ptr(view, record_index);
    run->index = twlhd_v2_tex_index_ptr(view, record_index);
    /* :160-165: the decoder's own pointer rules. */
    if (!run->texel || (format != 7u && !run->palette) ||
        (format == 5u && !run->index)) {
        run->status = TWLHD_PICA_FRAME_ENTRY_NO_DATA;
        return;
    }

    /* :65-72 and :183-185: the PICA size check the decoder applies. */
    if (!twlhd_tex_dimensions(run->param, &run->width, &run->height) ||
        !frame_size_ok(run->width) || !frame_size_ok(run->height)) {
        run->status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return;
    }

    /* :167-173: sizes must match twlhd_v2_tex_sizes(). That call cannot fail
     * here because format != 0 (common/twlhd_v2_parse.c:144). */
    (void)twlhd_v2_tex_sizes(run->param, &texel_bytes, NULL, &index_bytes);
    if (run->record.texel_size != texel_bytes ||
        run->record.index_size != index_bytes) {
        run->status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return;
    }

    /* :174-176: format 5 window / exact fixed table. */
    if (!twlhd_v2_tex_palette_size_ok(run->param, run->record.palette_size)) {
        run->status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return;
    }

    /* :178-181: fixed palette table must be fully present. */
    entries = frame_fixed_palette_entries(format);
    if (entries != 0u &&
        (uint64_t)run->record.palette_size < (uint64_t)entries * 2u) {
        run->status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
        return;
    }

    /* :239-247: every 4x4 block's palette window must fit palette_size. The
     * index bytes are already range-checked by the accessor above, so this
     * only reads the input view. */
    if (format == 5u) {
        blocks = (run->width >> 2) * (run->height >> 2);
        for (block = 0; block < blocks; block++) {
            uint16_t word =
                frame_read_le16(run->index + (size_t)block * 2u);
            uint32_t offset = (uint32_t)(word & 0x3FFFu) * 4u;
            uint64_t end = (uint64_t)offset + (uint64_t)
                frame_mode_palette_bytes((uint32_t)word >> 14);

            if (end > (uint64_t)run->record.palette_size) {
                run->status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
                return;
            }
        }
    }

    run->bytes = run->width * run->height * 4u;
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

    /*
     * An empty vertex array has no runs. Returning here keeps the write pass
     * from treating i == j == vertex_count == 0 as a run boundary, which
     * would read tex[0] and write entry_status[0]/entry_texture[0] (and
     * textures[0] with a matching TXDS record) past zero capacities.
     */
    if (vertex_count == 0u) {
        *out_entry_count = 0u;
        *out_texture_count = 0u;
        *out_arena_used = 0u;
        return TWLHD_PICA_FRAME_OK;
    }

    runs = frame_count_runs(tex, vertex_count);
    if (runs > plan_capacity || runs > entry_capacity ||
        vertex_count > texcoord_capacity) {
        return TWLHD_PICA_FRAME_ERR_CAPACITY;
    }

    /*
     * Validation/sizing pass: reads only. Every run goes through the same
     * no-decode predicate twlhd_pica_frame_arena_needed() uses, so the two
     * cannot disagree. `bytes` is the decoded arena size; rejecting
     * `bytes > scratch_size` here is the frame-level guard that makes
     * TWLHD_TEX_ERR_OUTPUT_SIZE (common/twlhd_tex_decode.c:186-187)
     * impossible in the write pass.
     */
    for (i = 0; i < vertex_count; i += 3u) {
        FrameRun run;

        if (i != 0u && frame_same_key(tex, i, i - 3u)) continue;

        frame_classify_run(tex, view, i, &run);
        if (run.status != TWLHD_PICA_FRAME_ENTRY_OK || run.untextured) continue;
        if (run.bytes > scratch_size) return TWLHD_PICA_FRAME_ERR_CAPACITY;

        for (j = 0; j < tex_count; j++) {
            if (key_param[j] == run.param && key_pltt[j] == run.pltt) break;
        }
        if (j != tex_count) continue;

        if (tex_count >= TWLHD_PICA_FRAME_MAX_TEXTURES ||
            tex_count >= texture_capacity) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        key_param[tex_count] = run.param;
        key_pltt[tex_count] = run.pltt;
        tex_count++;
        arena_used += run.bytes;
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
        FrameRun run;
        uint32_t vc;
        int32_t tex_index = -1;

        if (!boundary) continue;

        vc = i - j;
        frame_classify_run(tex, view, j, &run);

        if (run.status == TWLHD_PICA_FRAME_ENTRY_OK && !run.untextured) {
            uint32_t t;

            for (t = 0; t < tex_count; t++) {
                if (key_param[t] == run.param && key_pltt[t] == run.pltt) break;
            }
            if (t == tex_count) {
                /*
                 * The validation pass proved run.bytes <= scratch_size and the
                 * predicate reproduced every decoder refusal, so neither call
                 * can fail; these branches are kept as defence and are
                 * classified as unreachable in the mutation report.
                 */
                if (twlhd_tex_decode(&run.record, run.texel, run.palette,
                                     run.index, scratch,
                                     scratch_size) != TWLHD_TEX_OK) {
                    run.status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
                } else if (twlhd_pica_tex_swizzle_rgba8(
                               scratch, run.width, run.height,
                               arena + arena_used,
                               arena_capacity - arena_used) !=
                           TWLHD_PICA_TEX_OK) {
                    run.status = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;
                } else {
                    textures[tex_count].offset = arena_used;
                    textures[tex_count].width = run.width;
                    textures[tex_count].height = run.height;
                    (void)twlhd_pica_wrap_from_teximage(
                        run.param, &textures[tex_count].wrap_s,
                        &textures[tex_count].wrap_t);
                    tex_index = (int32_t)tex_count;
                    arena_used += run.bytes;
                    tex_count++;
                }
            } else {
                tex_index = (int32_t)t;
            }
        }

        entry_status[run_index] = run.status;
        entry_texture[run_index] = tex_index;
        run_index++;

        if (run.status == TWLHD_PICA_FRAME_ENTRY_OK && !run.untextured &&
            tex_index >= 0) {
            if (twlhd_pica_texcoords_from_gx(tex + j, vc, run.width, run.height,
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
    const TwlhdV2View *view, uint32_t *out_arena_bytes) {
    uint32_t key_param[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t key_pltt[TWLHD_PICA_FRAME_MAX_TEXTURES] = {0};
    uint32_t tex_count = 0;
    uint32_t arena_used = 0;
    uint32_t i;
    uint32_t j;

    if (!tex || !view || !out_arena_bytes) {
        return TWLHD_PICA_FRAME_ERR_NULL;
    }
    if (vertex_count % 3u != 0u) return TWLHD_PICA_FRAME_ERR_COUNT;

    for (i = 0; i < vertex_count; i += 3u) {
        FrameRun run;

        if (i != 0u && frame_same_key(tex, i, i - 3u)) continue;

        frame_classify_run(tex, view, i, &run);
        if (run.status != TWLHD_PICA_FRAME_ENTRY_OK || run.untextured) continue;

        for (j = 0; j < tex_count; j++) {
            if (key_param[j] == run.param && key_pltt[j] == run.pltt) break;
        }
        if (j != tex_count) continue;

        if (tex_count >= TWLHD_PICA_FRAME_MAX_TEXTURES) {
            return TWLHD_PICA_FRAME_ERR_CAPACITY;
        }
        key_param[tex_count] = run.param;
        key_pltt[tex_count] = run.pltt;
        tex_count++;
        arena_used += run.bytes;
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
