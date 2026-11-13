// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI for tools/test_pica_frame_golden.py (stage 5B-2e-1).
 *
 * Reads one compact little-endian frame description from stdin:
 *
 *   u32 vertex_count
 *   vertex_count * { u32 s_bits, u32 t_bits, u32 teximage_param, u32 pltt }
 *   u32 entry_count
 *   entry_count * { u32 param, u32 pltt, u32 texel_size, u32 palette_size,
 *                   u32 index_size, texel[texel_size],
 *                   palette[palette_size], index[index_size] }
 *
 * builds one real v2 file with twlhd_v2_write() (one BEGIN_VTXS GXCM entry),
 * parses it with twlhd_v2_parse(), runs twlhd_pica_frame_prepare() and prints
 * the parsed input records plus the result. Floats are printed as IEEE-754
 * bit patterns so the Python oracle can compare exactly.
 *
 * Output layout:
 *
 *   # input
 *   record <i> param=... pltt=... texel_off=... texel_size=... ...
 *   txdt <len>
 *   <lowercase hex, empty line for 0>
 *   pldt <len>
 *   <lowercase hex>
 *   # result
 *   result=<name>
 *   entry_count=<n>
 *   texture_count=<n>
 *   arena_used=<n>
 *   entry <i> first=... count=... param=... pltt=... untextured=... status=... tex=...
 *   texture <i> offset=... width=... height=... wrap_s=... wrap_t=...
 *   texcoord <i> u=<8 hex> v=<8 hex>
 *   arena <len>
 *   <lowercase hex>
 *
 * This tool is host-only (BUILD/UNIT). Exit code 2 means bad usage or input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twlhd_pica_frame.h"
#include "twlhd_pica_tex.h"
#include "twlhd_tex_decode.h"
#include "twlhd_v2_parse.h"
#include "twlhd_v2_write.h"

#define MAX_VERTICES 65536u

static int read_u32(FILE *in, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, in) != 4) return 0;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static uint8_t *read_bytes(FILE *in, uint32_t size) {
    uint8_t *p;
    if (size == 0) return NULL;
    p = (uint8_t *)malloc(size);
    if (!p) return NULL;
    if (fread(p, 1, size, in) != size) {
        free(p);
        return NULL;
    }
    return p;
}

static void print_hex(const uint8_t *p, uint32_t size) {
    uint32_t i;
    for (i = 0; i < size; i++) printf("%02x", (unsigned)p[i]);
    printf("\n");
}

static uint32_t float_bits(float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

int main(void) {
    uint32_t vertex_count;
    uint32_t entry_count;
    uint32_t i;
    TwlhdGxTexVertex *tex = NULL;
    TwlhdV2TexEntry *entries = NULL;
    uint8_t *file = NULL;
    uint8_t *scratch = NULL;
    uint8_t *arena = NULL;
    TwlhdPicaBindPlan *plan = NULL;
    TwlhdPicaFrameTexture *textures = NULL;
    TwlhdPicaTexCoord *texcoords = NULL;
    int32_t *entry_texture = NULL;
    TwlhdPicaFrameEntryStatus *entry_status = NULL;
    TwlhdV2View view;
    TwlhdV2WriteParams params;
    TwlhdPicaFrameResult result;
    size_t file_size = 0;
    uint32_t runs;
    uint32_t entry_count_out = 0;
    uint32_t texture_count_out = 0;
    uint32_t arena_used = 0;
    uint32_t arena_needed = 0;
    uint32_t max_texture_bytes = 1;
    uint32_t scratch_size;
    int bad = 0;

    if (!read_u32(stdin, &vertex_count)) {
        fprintf(stderr, "short read: vertex_count\n");
        return 2;
    }
    if (vertex_count > MAX_VERTICES) {
        fprintf(stderr, "vertex_count too large: %u\n", (unsigned)vertex_count);
        return 2;
    }

    tex = (TwlhdGxTexVertex *)calloc(vertex_count ? vertex_count : 1u,
                                     sizeof(*tex));
    if (!tex) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    for (i = 0; i < vertex_count; i++) {
        uint32_t s_bits, t_bits, param, pltt;
        if (!read_u32(stdin, &s_bits) || !read_u32(stdin, &t_bits) ||
            !read_u32(stdin, &param) || !read_u32(stdin, &pltt)) {
            fprintf(stderr, "short read: vertex %u\n", (unsigned)i);
            bad = 1;
            break;
        }
        memcpy(&tex[i].s, &s_bits, 4);
        memcpy(&tex[i].t, &t_bits, 4);
        tex[i].teximage_param = param;
        tex[i].pltt_base = pltt;
    }
    if (bad) goto cleanup;

    if (!read_u32(stdin, &entry_count)) {
        fprintf(stderr, "short read: entry_count\n");
        bad = 1;
        goto cleanup;
    }
    if (entry_count > TWLHD_V2_TXDS_MAX_RECORDS) {
        fprintf(stderr, "entry_count too large: %u\n", (unsigned)entry_count);
        bad = 1;
        goto cleanup;
    }

    entries = (TwlhdV2TexEntry *)calloc(entry_count ? entry_count : 1u,
                                        sizeof(*entries));
    if (!entries) {
        fprintf(stderr, "out of memory\n");
        bad = 1;
        goto cleanup;
    }
    for (i = 0; i < entry_count; i++) {
        uint32_t texel_size, palette_size, index_size;

        if (!read_u32(stdin, &entries[i].teximage_param) ||
            !read_u32(stdin, &entries[i].pltt_base) ||
            !read_u32(stdin, &texel_size) ||
            !read_u32(stdin, &palette_size) ||
            !read_u32(stdin, &index_size)) {
            fprintf(stderr, "short read: entry %u header\n", (unsigned)i);
            bad = 1;
            goto cleanup;
        }
        entries[i].texel = read_bytes(stdin, texel_size);
        entries[i].palette = read_bytes(stdin, palette_size);
        entries[i].index = read_bytes(stdin, index_size);
        if ((texel_size != 0u && !entries[i].texel) ||
            (palette_size != 0u && !entries[i].palette) ||
            (index_size != 0u && !entries[i].index)) {
            fprintf(stderr, "short read: entry %u data\n", (unsigned)i);
            bad = 1;
            goto cleanup;
        }
        entries[i].texel_bytes = texel_size;
        entries[i].palette_bytes = palette_size;
        entries[i].index_bytes = index_size;
    }

    file = (uint8_t *)malloc(TWLHD_V2_FILE_MAX);
    if (!file) {
        fprintf(stderr, "out of memory\n");
        bad = 1;
        goto cleanup;
    }

    {
        static const TwlhdV2CmdEntry gxcm[1] = {{TWLHD_V2_CMD_BEGIN_VTXS, 1u}};

        params.frame_index = 1u;
        params.flags = 0u;
        params.backend_id = TWLHD_V2_BACKEND_SYNTHETIC;
        params.coverage_flags = 0u;
        params.gxcm = gxcm;
        params.gxcm_count = 1u;
        params.rsta = NULL;
        params.rsta_count = 0u;
        params.tex = entry_count ? entries : NULL;
        params.tex_count = entry_count;
    }
    if (twlhd_v2_write(&params, file, TWLHD_V2_FILE_MAX, &file_size) !=
        TWLHD_V2_OK) {
        fprintf(stderr, "v2 writer rejected the frame\n");
        bad = 1;
        goto cleanup;
    }
    if (twlhd_v2_parse(file, file_size, &view) != TWLHD_V2_OK) {
        fprintf(stderr, "v2 parser rejected the frame\n");
        bad = 1;
        goto cleanup;
    }

    /* Scratch must fit the largest single decoded texture (prepare's write
     * pass decodes into it; the sizing pass needs no scratch). */
    for (i = 0; i < twlhd_v2_txds_count(&view); i++) {
        TwlhdV2TexRecord rec;
        uint32_t w, h;

        (void)twlhd_v2_txds_record(&view, i, &rec);
        if (twlhd_tex_dimensions(rec.teximage_param, &w, &h)) {
            uint32_t bytes = w * h * 4u;
            if (bytes > max_texture_bytes) max_texture_bytes = bytes;
        }
    }
    scratch_size = max_texture_bytes;
    scratch = (uint8_t *)malloc(scratch_size);
    if (!scratch) {
        fprintf(stderr, "out of memory\n");
        bad = 1;
        goto cleanup;
    }

    if (twlhd_pica_frame_arena_needed(tex, vertex_count, &view,
                                      &arena_needed) !=
        TWLHD_PICA_FRAME_OK) {
        fprintf(stderr, "arena_needed failed\n");
        bad = 1;
        goto cleanup;
    }

    arena = (uint8_t *)malloc(arena_needed ? arena_needed : 1u);
    runs = vertex_count / 3u;
    plan = (TwlhdPicaBindPlan *)calloc(runs ? runs : 1u, sizeof(*plan));
    textures = (TwlhdPicaFrameTexture *)calloc(
        entry_count ? entry_count : 1u, sizeof(*textures));
    texcoords = (TwlhdPicaTexCoord *)calloc(vertex_count ? vertex_count : 1u,
                                            sizeof(*texcoords));
    entry_texture = (int32_t *)calloc(runs ? runs : 1u, sizeof(*entry_texture));
    entry_status = (TwlhdPicaFrameEntryStatus *)calloc(
        runs ? runs : 1u, sizeof(*entry_status));
    if (!arena || !plan || !textures || !texcoords || !entry_texture ||
        !entry_status) {
        fprintf(stderr, "out of memory\n");
        bad = 1;
        goto cleanup;
    }

    result = twlhd_pica_frame_prepare(
        tex, vertex_count, &view, plan, runs, textures, entry_count, arena,
        arena_needed, texcoords, vertex_count, entry_texture, entry_status,
        runs, scratch, scratch_size, &entry_count_out, &texture_count_out,
        &arena_used);

    /* Parsed input echo, so both sides see the exact same records/bytes. */
    printf("# input\n");
    for (i = 0; i < twlhd_v2_txds_count(&view); i++) {
        TwlhdV2TexRecord rec;
        (void)twlhd_v2_txds_record(&view, i, &rec);
        printf("record %u param=%u pltt=%u texel_off=%u texel_size=%u "
               "palette_off=%u palette_size=%u index_off=%u index_size=%u\n",
               (unsigned)i, (unsigned)rec.teximage_param,
               (unsigned)rec.pltt_base, (unsigned)rec.texel_offset,
               (unsigned)rec.texel_size, (unsigned)rec.palette_offset,
               (unsigned)rec.palette_size, (unsigned)rec.index_offset,
               (unsigned)rec.index_size);
    }
    printf("txdt %u\n", (unsigned)view.txdt_size);
    print_hex(view.txdt, view.txdt_size);
    printf("pldt %u\n", (unsigned)view.pldt_size);
    print_hex(view.pldt, view.pldt_size);

    printf("# result\n");
    printf("result=%s\n", twlhd_pica_frame_result_string(result));
    printf("entry_count=%u\n", (unsigned)entry_count_out);
    printf("texture_count=%u\n", (unsigned)texture_count_out);
    printf("arena_used=%u\n", (unsigned)arena_used);
    for (i = 0; i < entry_count_out; i++) {
        printf("entry %u first=%u count=%u param=%u pltt=%u untextured=%d "
               "status=%s tex=%d\n",
               (unsigned)i, (unsigned)plan[i].first_vertex,
               (unsigned)plan[i].vertex_count,
               (unsigned)plan[i].teximage_param, (unsigned)plan[i].pltt_base,
               plan[i].untextured,
               twlhd_pica_frame_entry_status_string(entry_status[i]),
               (int)entry_texture[i]);
    }
    for (i = 0; i < texture_count_out; i++) {
        printf("texture %u offset=%u width=%u height=%u wrap_s=%u wrap_t=%u\n",
               (unsigned)i, (unsigned)textures[i].offset,
               (unsigned)textures[i].width, (unsigned)textures[i].height,
               (unsigned)textures[i].wrap_s, (unsigned)textures[i].wrap_t);
    }
    for (i = 0; i < vertex_count; i++) {
        printf("texcoord %u u=%08x v=%08x\n", (unsigned)i,
               (unsigned)float_bits(texcoords[i].u),
               (unsigned)float_bits(texcoords[i].v));
    }
    printf("arena %u\n", (unsigned)arena_used);
    print_hex(arena, arena_used);

cleanup:
    if (entries) {
        for (i = 0; i < entry_count; i++) {
            free((void *)entries[i].texel);
            free((void *)entries[i].palette);
            free((void *)entries[i].index);
        }
    }
    free(entries);
    free(tex);
    free(file);
    free(scratch);
    free(arena);
    free(plan);
    free(textures);
    free(texcoords);
    free(entry_texture);
    free(entry_status);
    return bad ? 2 : 0;
}
