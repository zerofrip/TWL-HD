// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_pica_frame.c (stage 5B-2e-1).
 *
 * The frame is built with the real v2 writer and parsed with the real parser,
 * so the TXDS/TXDT/PLDT layout is the production one. Every expected arena
 * byte is hand-computed from the literal 5-bit expansion table and the
 * hand-written 8x8 Morton table below (never by calling the swizzle under
 * test). BUILD/UNIT only; no GPU or hardware is involved.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_pica_frame.h"
#include "twlhd_pica_tex.h"
#include "twlhd_v2_parse.h"
#include "twlhd_v2_write.h"

#define VERTEX_COUNT 12u
#define RUN_COUNT 4u
#define ARENA_BYTES 768u /* 8x8 RGBA8 (256) + 16x8 RGBA8 (512) */
#define SCRATCH_BYTES 1024u

/* Format 7 (16bpp), 8x8, repeat S and T. */
#define PARAM_A 0x1C030000u
/* Format 3 (I4), 16x8, clamp both axes. */
#define PARAM_B 0x0C100000u
/* Format 7, 8x8, no wrap bits: matches no record in the "missing" frame. */
#define PARAM_MISSING 0x1C000000u

/* Literal 5-bit -> 8-bit table (DeSmuME colorspacehandler.cpp). */
static const unsigned FIVE_TO_EIGHT[32] = {
    0x00, 0x08, 0x10, 0x18, 0x21, 0x29, 0x31, 0x39, 0x42, 0x4A, 0x52,
    0x5A, 0x63, 0x6B, 0x73, 0x7B, 0x84, 0x8C, 0x94, 0x9C, 0xA5, 0xAD,
    0xB5, 0xBD, 0xC6, 0xCE, 0xD6, 0xDE, 0xE7, 0xEF, 0xF7, 0xFF};

/* Hand-written PICA destination texel index for source row y, column x. */
static const uint32_t kMorton8x8[8][8] = {
    {42, 43, 46, 47, 58, 59, 62, 63},
    {40, 41, 44, 45, 56, 57, 60, 61},
    {34, 35, 38, 39, 50, 51, 54, 55},
    {32, 33, 36, 37, 48, 49, 52, 53},
    {10, 11, 14, 15, 26, 27, 30, 31},
    {8, 9, 12, 13, 24, 25, 28, 29},
    {2, 3, 6, 7, 18, 19, 22, 23},
    {0, 1, 4, 5, 16, 17, 20, 21},
};

static uint8_t g_tex_a[128];  /* 8x8 format 7: 2 bytes per texel */
static uint8_t g_tex_b[64];   /* 16x8 I4: 128 texels / 2 per byte */
static uint8_t g_pal_b[32];   /* 16 RGB555 entries */
static uint8_t g_file[TWLHD_V2_FILE_MAX];

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void set_vtx(TwlhdGxTexVertex *v, float s, float t, uint32_t param,
                    uint32_t pltt) {
    v->s = s;
    v->t = t;
    v->teximage_param = param;
    v->pltt_base = pltt;
}

/* Pixel n: R=expand5(n&31), G=expand5((3n)&31), B=expand5((5n)&31). */
static void fill_tex_a(void) {
    uint32_t n;
    for (n = 0; n < 64u; n++) {
        uint32_t r5 = n & 31u;
        uint32_t g5 = (n * 3u) & 31u;
        uint32_t b5 = (n * 5u) & 31u;
        put_le16(g_tex_a + n * 2u, (uint16_t)(0x8000u | r5 | (g5 << 5) |
                                              (b5 << 10)));
    }
}

/* Palette entry k: R=expand5(2k), G=expand5(31-2k), B=expand5(k). */
static void fill_pal_b(void) {
    uint32_t k;
    for (k = 0; k < 16u; k++) {
        uint32_t r5 = k * 2u;
        uint32_t g5 = 31u - k * 2u;
        uint32_t b5 = k;
        put_le16(g_pal_b + k * 2u, (uint16_t)(r5 | (g5 << 5) | (b5 << 10)));
    }
}

/* I4 texel byte i packs pixel 2i in the low nibble and 2i+1 in the high. */
static void fill_tex_b(void) {
    uint32_t i;
    for (i = 0; i < 64u; i++) {
        uint32_t lo = (i * 2u) & 15u;
        uint32_t hi = (i * 2u + 1u) & 15u;
        g_tex_b[i] = (uint8_t)(lo | (hi << 4));
    }
}

static const TwlhdV2TexEntry g_main_entries[2] = {
    {PARAM_A, 0u, g_tex_a, NULL, NULL, 128u, 0u, 0u},
    {PARAM_B, 1u, g_tex_b, g_pal_b, NULL, 64u, 32u, 0u},
};

static const TwlhdV2TexEntry g_a_only[1] = {
    {PARAM_A, 0u, g_tex_a, NULL, NULL, 128u, 0u, 0u},
};

/*
 * Four runs: A (reused later), B (paletted), untextured, A again.
 * Run keys are the triangle's first vertex.
 */
static void make_main_vertices(TwlhdGxTexVertex *tex) {
    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[1], 2.0f, 2.0f, PARAM_A, 0u);
    set_vtx(&tex[2], 4.0f, 4.0f, PARAM_A, 0u);

    set_vtx(&tex[3], 0.0f, 0.0f, PARAM_B, 1u);
    set_vtx(&tex[4], 8.0f, 4.0f, PARAM_B, 1u);
    set_vtx(&tex[5], 16.0f, 8.0f, PARAM_B, 1u);

    set_vtx(&tex[6], 1.0f, 1.0f, 0u, 0u);
    set_vtx(&tex[7], 2.0f, 2.0f, 0u, 0u);
    set_vtx(&tex[8], 3.0f, 3.0f, 0u, 0u);

    set_vtx(&tex[9], 1.0f, 1.0f, PARAM_A, 0u);
    set_vtx(&tex[10], 3.0f, 3.0f, PARAM_A, 0u);
    set_vtx(&tex[11], 5.0f, 5.0f, PARAM_A, 0u);
}

static TwlhdV2Result write_and_parse(const TwlhdV2TexEntry *entries,
                                     uint32_t entry_count, TwlhdV2View *view) {
    static const TwlhdV2CmdEntry gxcm[1] = {{TWLHD_V2_CMD_BEGIN_VTXS, 1u}};
    TwlhdV2WriteParams params;
    size_t size = 0;

    params.frame_index = 1u;
    params.flags = 0u; /* not FRAME_COMPLETE: BEGIN_VTXS is then legal */
    params.backend_id = TWLHD_V2_BACKEND_SYNTHETIC;
    params.coverage_flags = 0u;
    params.gxcm = gxcm;
    params.gxcm_count = 1u;
    params.rsta = NULL;
    params.rsta_count = 0u;
    params.tex = entries;
    params.tex_count = entry_count;
    if (twlhd_v2_write(&params, g_file, sizeof(g_file), &size) != TWLHD_V2_OK)
        return TWLHD_V2_ERR_GXCM;
    return twlhd_v2_parse(g_file, size, view);
}

typedef struct {
    TwlhdPicaBindPlan plan[RUN_COUNT];
    TwlhdPicaFrameTexture textures[RUN_COUNT];
    uint8_t arena[ARENA_BYTES];
    TwlhdPicaTexCoord texcoords[VERTEX_COUNT];
    int32_t entry_texture[RUN_COUNT];
    TwlhdPicaFrameEntryStatus entry_status[RUN_COUNT];
    uint8_t scratch[SCRATCH_BYTES];
    uint32_t entry_count;
    uint32_t texture_count;
    uint32_t arena_used;
} FrameOut;

static void out_reset(FrameOut *out) {
    memset(out, 0x5B, offsetof(FrameOut, scratch));
    memset(out->scratch, 0x5B, sizeof(out->scratch));
    out->entry_count = 0xDEADBEEFu;
    out->texture_count = 0xDEADBEEFu;
    out->arena_used = 0xDEADBEEFu;
}

static int all_bytes_are(const void *p, size_t n, int value) {
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        if (b[i] != (uint8_t)value) return 0;
    }
    return 1;
}

static int outputs_untouched(const FrameOut *out) {
    return all_bytes_are(out->plan, sizeof(out->plan), 0x5B) &&
           all_bytes_are(out->textures, sizeof(out->textures), 0x5B) &&
           all_bytes_are(out->arena, sizeof(out->arena), 0x5B) &&
           all_bytes_are(out->texcoords, sizeof(out->texcoords), 0x5B) &&
           all_bytes_are(out->entry_texture, sizeof(out->entry_texture), 0x5B) &&
           all_bytes_are(out->entry_status, sizeof(out->entry_status), 0x5B) &&
           out->entry_count == 0xDEADBEEFu &&
           out->texture_count == 0xDEADBEEFu &&
           out->arena_used == 0xDEADBEEFu;
}

static TwlhdPicaFrameResult run_prepare(FrameOut *out, const TwlhdV2View *view,
                                        const TwlhdGxTexVertex *tex,
                                        uint32_t plan_cap, uint32_t tex_cap,
                                        uint32_t arena_cap, uint32_t tc_cap,
                                        uint32_t entry_cap,
                                        uint32_t scratch_size) {
    return twlhd_pica_frame_prepare(
        tex, VERTEX_COUNT, view, out->plan, plan_cap, out->textures, tex_cap,
        out->arena, arena_cap, out->texcoords, tc_cap, out->entry_texture,
        out->entry_status, entry_cap, out->scratch, scratch_size,
        &out->entry_count, &out->texture_count, &out->arena_used);
}

static FrameOut g_out;

static void expect_a_bytes(const FrameOut *out) {
    uint32_t x, y;
    for (y = 0; y < 8u; y++) {
        for (x = 0; x < 8u; x++) {
            uint32_t n = y * 8u + x;
            uint32_t dest = kMorton8x8[y][x];
            uint32_t r5 = n & 31u;
            uint32_t g5 = (n * 3u) & 31u;
            uint32_t b5 = (n * 5u) & 31u;
            const uint8_t *p = out->arena + dest * 4u;

            CHECK_EQ_INT(p[0], 255u);
            CHECK_EQ_INT(p[1], FIVE_TO_EIGHT[b5]);
            CHECK_EQ_INT(p[2], FIVE_TO_EIGHT[g5]);
            CHECK_EQ_INT(p[3], FIVE_TO_EIGHT[r5]);
        }
    }
    /* Explicit literals: (0,0) -> texel 42, (7,0) -> texel 63. */
    CHECK_EQ_INT(out->arena[42u * 4u + 0u], 255u);
    CHECK_EQ_INT(out->arena[42u * 4u + 1u], 0u);
    CHECK_EQ_INT(out->arena[42u * 4u + 2u], 0u);
    CHECK_EQ_INT(out->arena[42u * 4u + 3u], 0u);
    CHECK_EQ_INT(out->arena[63u * 4u + 0u], 255u);
    CHECK_EQ_INT(out->arena[63u * 4u + 1u], FIVE_TO_EIGHT[3]);
    CHECK_EQ_INT(out->arena[63u * 4u + 2u], FIVE_TO_EIGHT[21]);
    CHECK_EQ_INT(out->arena[63u * 4u + 3u], FIVE_TO_EIGHT[7]);
}

static void expect_b_bytes(const FrameOut *out) {
    uint32_t x, y;
    for (y = 0; y < 8u; y++) {
        for (x = 0; x < 16u; x++) {
            uint32_t n = y * 16u + x;
            uint32_t k = n & 15u;
            uint32_t dest = (x >> 3) * 64u + kMorton8x8[y][x & 7u];
            const uint8_t *p = out->arena + 256u + dest * 4u;

            CHECK_EQ_INT(p[0], 255u);
            CHECK_EQ_INT(p[1], FIVE_TO_EIGHT[k]);
            CHECK_EQ_INT(p[2], FIVE_TO_EIGHT[31u - k * 2u]);
            CHECK_EQ_INT(p[3], FIVE_TO_EIGHT[k * 2u]);
        }
    }
    /* (0,0) -> tile column 0 texel 42; index 0 is R=0,G=255,B=0. */
    CHECK_EQ_INT(out->arena[256u + 42u * 4u + 0u], 255u);
    CHECK_EQ_INT(out->arena[256u + 42u * 4u + 1u], 0u);
    CHECK_EQ_INT(out->arena[256u + 42u * 4u + 2u], 255u);
    CHECK_EQ_INT(out->arena[256u + 42u * 4u + 3u], 0u);
}

static void test_prepare_basic(void) {
    TwlhdGxTexVertex tex[VERTEX_COUNT];
    TwlhdV2View view;
    uint32_t needed = 0xDEADBEEFu;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();
    make_main_vertices(tex);

    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_txds_count(&view), 2u);

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               g_out.scratch, SCRATCH_BYTES,
                                               &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, ARENA_BYTES);

    out_reset(&g_out);
    CHECK_EQ_INT(run_prepare(&g_out, &view, tex, RUN_COUNT, RUN_COUNT,
                             ARENA_BYTES, VERTEX_COUNT, RUN_COUNT,
                             SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, RUN_COUNT);
    CHECK_EQ_INT(g_out.texture_count, 2u);
    CHECK_EQ_INT(g_out.arena_used, needed);

    /* Plan: one entry per consecutive run, keyed by the first vertex. */
    CHECK_EQ_INT(g_out.plan[0].first_vertex, 0u);
    CHECK_EQ_INT(g_out.plan[0].vertex_count, 3u);
    CHECK_EQ_INT(g_out.plan[0].teximage_param, PARAM_A);
    CHECK_EQ_INT(g_out.plan[0].pltt_base, 0u);
    CHECK_EQ_INT(g_out.plan[0].untextured, 0);
    CHECK_EQ_INT(g_out.plan[1].first_vertex, 3u);
    CHECK_EQ_INT(g_out.plan[1].teximage_param, PARAM_B);
    CHECK_EQ_INT(g_out.plan[1].pltt_base, 1u);
    CHECK_EQ_INT(g_out.plan[1].untextured, 0);
    CHECK_EQ_INT(g_out.plan[2].first_vertex, 6u);
    CHECK_EQ_INT(g_out.plan[2].untextured, 1);
    CHECK_EQ_INT(g_out.plan[3].first_vertex, 9u);
    CHECK_EQ_INT(g_out.plan[3].teximage_param, PARAM_A);
    CHECK_EQ_INT(g_out.plan[3].untextured, 0);

    /* A stored once at 0; B at 256; both 128-byte aligned. */
    CHECK_EQ_INT(g_out.textures[0].offset, 0u);
    CHECK_EQ_INT(g_out.textures[0].width, 8u);
    CHECK_EQ_INT(g_out.textures[0].height, 8u);
    CHECK_EQ_INT(g_out.textures[0].wrap_s, TWLHD_PICA_WRAP_REPEAT);
    CHECK_EQ_INT(g_out.textures[0].wrap_t, TWLHD_PICA_WRAP_REPEAT);
    CHECK_EQ_INT(g_out.textures[1].offset, 256u);
    CHECK_EQ_INT(g_out.textures[1].width, 16u);
    CHECK_EQ_INT(g_out.textures[1].height, 8u);
    CHECK_EQ_INT(g_out.textures[1].wrap_s, TWLHD_PICA_WRAP_CLAMP_TO_EDGE);
    CHECK_EQ_INT(g_out.textures[1].wrap_t, TWLHD_PICA_WRAP_CLAMP_TO_EDGE);
    CHECK_EQ_INT(g_out.textures[0].offset % 128u, 0u);
    CHECK_EQ_INT(g_out.textures[1].offset % 128u, 0u);

    /* Dedupe: runs 0 and 3 share texture 0; untextured run 2 has -1. */
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
    CHECK_EQ_INT(g_out.entry_texture[1], 1);
    CHECK_EQ_INT(g_out.entry_texture[2], -1);
    CHECK_EQ_INT(g_out.entry_texture[3], 0);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[1], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[2], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[3], TWLHD_PICA_FRAME_ENTRY_OK);

    expect_a_bytes(&g_out);
    expect_b_bytes(&g_out);

    /* Hand-computed texcoords: u=s/width, v=1-t/height. */
    CHECK_NEAR(g_out.texcoords[0].u, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[0].v, 1.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[1].u, 0.25, 1e-6);
    CHECK_NEAR(g_out.texcoords[1].v, 0.75, 1e-6);
    CHECK_NEAR(g_out.texcoords[2].u, 0.5, 1e-6);
    CHECK_NEAR(g_out.texcoords[2].v, 0.5, 1e-6);
    CHECK_NEAR(g_out.texcoords[3].u, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[3].v, 1.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[4].u, 0.5, 1e-6);
    CHECK_NEAR(g_out.texcoords[4].v, 0.5, 1e-6);
    CHECK_NEAR(g_out.texcoords[5].u, 1.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[5].v, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[6].u, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[6].v, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[7].u, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[7].v, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[8].u, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[8].v, 0.0, 1e-6);
    CHECK_NEAR(g_out.texcoords[9].u, 0.125, 1e-6);
    CHECK_NEAR(g_out.texcoords[9].v, 0.875, 1e-6);
    CHECK_NEAR(g_out.texcoords[10].u, 0.375, 1e-6);
    CHECK_NEAR(g_out.texcoords[10].v, 0.625, 1e-6);
    CHECK_NEAR(g_out.texcoords[11].u, 0.625, 1e-6);
    CHECK_NEAR(g_out.texcoords[11].v, 0.375, 1e-6);

    test_report("pica-frame/prepare-basic");
}

static void test_merged_run_indexing(void) {
    TwlhdGxTexVertex tex[9];
    TwlhdV2View view;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();

    /* Triangles A A B: the first two merge into one 6-vertex run, so the
     * plan/entry index is not vertex_index/3 (that is the old bug). */
    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[1], 1.0f, 1.0f, PARAM_A, 0u);
    set_vtx(&tex[2], 2.0f, 2.0f, PARAM_A, 0u);
    set_vtx(&tex[3], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[4], 3.0f, 3.0f, PARAM_A, 0u);
    set_vtx(&tex[5], 4.0f, 4.0f, PARAM_A, 0u);
    set_vtx(&tex[6], 0.0f, 0.0f, PARAM_B, 1u);
    set_vtx(&tex[7], 8.0f, 4.0f, PARAM_B, 1u);
    set_vtx(&tex[8], 16.0f, 8.0f, PARAM_B, 1u);

    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);
    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 9u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords, 9u,
                     g_out.entry_texture, g_out.entry_status, RUN_COUNT,
                     g_out.scratch, SCRATCH_BYTES, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 2u);
    CHECK_EQ_INT(g_out.plan[0].first_vertex, 0u);
    CHECK_EQ_INT(g_out.plan[0].vertex_count, 6u);
    CHECK_EQ_INT(g_out.plan[1].first_vertex, 6u);
    CHECK_EQ_INT(g_out.plan[1].vertex_count, 3u);
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
    CHECK_EQ_INT(g_out.entry_texture[1], 1);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[1], TWLHD_PICA_FRAME_ENTRY_OK);
    /* Nothing was written past the two real runs. */
    CHECK_EQ_INT(g_out.entry_texture[2], (int32_t)0x5B5B5B5B);
    test_report("pica-frame/merged-run-indexing");
}

static void test_mixed_vertex_runs(void) {
    TwlhdGxTexVertex tex[6];
    TwlhdGxTexVertex many[257u * 3u];
    TwlhdV2View view;
    TwlhdV2View empty;
    uint32_t needed = 0xDEADBEEFu;
    uint32_t k;
    uint32_t v;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();

    /* Only the first vertex of each triangle is the run key. tex[1] carries
     * the later run's key, which the i - 2u mutants mistake for it. */
    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[1], 1.0f, 1.0f, PARAM_B, 1u);
    set_vtx(&tex[2], 2.0f, 2.0f, PARAM_B, 1u);
    set_vtx(&tex[3], 0.0f, 0.0f, PARAM_B, 1u);
    set_vtx(&tex[4], 8.0f, 4.0f, PARAM_A, 0u);
    set_vtx(&tex[5], 16.0f, 8.0f, PARAM_A, 0u);

    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 6u, &view, g_out.scratch,
                                               SCRATCH_BYTES, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, ARENA_BYTES);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 6u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 2u);
    CHECK_EQ_INT(g_out.texture_count, 2u);
    CHECK_EQ_INT(g_out.arena_used, ARENA_BYTES);
    CHECK_EQ_INT(g_out.plan[0].first_vertex, 0u);
    CHECK_EQ_INT(g_out.plan[1].first_vertex, 3u);
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
    CHECK_EQ_INT(g_out.entry_texture[1], 1);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[1], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[2], (int32_t)0x5B5B5B5B);

    /* The sizing pass must see both runs: with room for only one texture the
     * correct call reports ERR_ARENA instead of silently dropping B. */
    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 6u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, 512u, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_ERR_ARENA);
    CHECK(outputs_untouched(&g_out));

    /* Two runs do not fit one entry slot. */
    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 6u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status, 1u,
                     g_out.scratch, SCRATCH_BYTES, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_ERR_CAPACITY);
    CHECK(outputs_untouched(&g_out));

    /* 257 untextured runs with distinct pltt_base keys: no texture is needed,
     * so arena_needed is exactly 0 (and the loop must stay in bounds). */
    for (k = 0; k < 257u; k++) {
        for (v = 0; v < 3u; v++)
            set_vtx(&many[k * 3u + v], 0.0f, 0.0f, 0u, k);
    }
    memset(&empty, 0, sizeof(empty));
    needed = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(many, 257u * 3u, &empty,
                                               g_out.scratch, SCRATCH_BYTES,
                                               &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 0u);

    test_report("pica-frame/mixed-vertex-runs");
}

/*
 * Texture dedupe compares BOTH halves of (teximage_param, pltt_base). Two
 * records that share only the pltt must both be counted by the sizing pass:
 * with an arena one byte short of the real need the call must fail with
 * ERR_ARENA rather than let the write pass discover the shortfall.
 */
static void test_dedupe_key_halves(void) {
    static const TwlhdV2TexEntry entries[2] = {
        {PARAM_A, 0u, g_tex_a, NULL, NULL, 128u, 0u, 0u},
        {PARAM_B, 0u, g_tex_b, g_pal_b, NULL, 64u, 32u, 0u},
    };
    TwlhdGxTexVertex tex[6];
    TwlhdV2View view;
    uint32_t needed = 0xDEADBEEFu;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();

    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[1], 1.0f, 1.0f, PARAM_A, 0u);
    set_vtx(&tex[2], 2.0f, 2.0f, PARAM_A, 0u);
    set_vtx(&tex[3], 0.0f, 0.0f, PARAM_B, 0u);
    set_vtx(&tex[4], 8.0f, 4.0f, PARAM_B, 0u);
    set_vtx(&tex[5], 16.0f, 8.0f, PARAM_B, 0u);

    CHECK_EQ_INT(write_and_parse(entries, 2u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 6u, &view, g_out.scratch,
                                               SCRATCH_BYTES, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, ARENA_BYTES);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 6u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES - 1u, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_ERR_ARENA);
    CHECK(outputs_untouched(&g_out));

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 6u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.texture_count, 2u);
    CHECK_EQ_INT(g_out.arena_used, ARENA_BYTES);

    test_report("pica-frame/dedupe-key-halves");
}

static void test_errors(void) {
    TwlhdGxTexVertex tex[VERTEX_COUNT];
    TwlhdV2View view;
    TwlhdPicaBindPlan plan[RUN_COUNT];
    TwlhdPicaFrameTexture textures[RUN_COUNT];
    uint8_t arena[ARENA_BYTES];
    TwlhdPicaTexCoord texcoords[VERTEX_COUNT];
    int32_t entry_texture[RUN_COUNT];
    TwlhdPicaFrameEntryStatus entry_status[RUN_COUNT];
    uint8_t scratch[SCRATCH_BYTES];
    uint32_t count = 0xDEADBEEFu;
    uint32_t tcount = 0xDEADBEEFu;
    uint32_t used = 0xDEADBEEFu;
    uint32_t needed = 0xDEADBEEFu;

#define PREPARE(p_tex, p_view, p_plan, plan_cap, p_texbuf, tex_cap, p_arena, \
                arena_cap, p_tc, tc_cap, p_et, p_es, entry_cap, p_scratch,   \
                scratch_size)                                                \
    twlhd_pica_frame_prepare((p_tex), VERTEX_COUNT, (p_view), (p_plan),      \
                             (plan_cap), (p_texbuf), (tex_cap), (p_arena),   \
                             (arena_cap), (p_tc), (tc_cap), (p_et), (p_es),  \
                             (entry_cap), (p_scratch), (scratch_size),       \
                             &count, &tcount, &used)

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();
    make_main_vertices(tex);
    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);

    /* NULL in every required pointer position. */
    CHECK_EQ_INT(PREPARE(NULL, &view, plan, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, texcoords, VERTEX_COUNT,
                         entry_texture, entry_status, RUN_COUNT, scratch,
                         SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, NULL, plan, RUN_COUNT, textures, RUN_COUNT, arena,
                         ARENA_BYTES, texcoords, VERTEX_COUNT, entry_texture,
                         entry_status, RUN_COUNT, scratch, SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, NULL, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, texcoords, VERTEX_COUNT,
                         entry_texture, entry_status, RUN_COUNT, scratch,
                         SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, NULL, RUN_COUNT, arena,
                         ARENA_BYTES, texcoords, VERTEX_COUNT, entry_texture,
                         entry_status, RUN_COUNT, scratch, SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, textures, RUN_COUNT, NULL,
                         ARENA_BYTES, texcoords, VERTEX_COUNT, entry_texture,
                         entry_status, RUN_COUNT, scratch, SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, NULL, VERTEX_COUNT, entry_texture,
                         entry_status, RUN_COUNT, scratch, SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, texcoords, VERTEX_COUNT, NULL,
                         entry_status, RUN_COUNT, scratch, SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, texcoords, VERTEX_COUNT,
                         entry_texture, NULL, RUN_COUNT, scratch,
                         SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(PREPARE(tex, &view, plan, RUN_COUNT, textures, RUN_COUNT,
                         arena, ARENA_BYTES, texcoords, VERTEX_COUNT,
                         entry_texture, entry_status, RUN_COUNT, NULL,
                         SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_ERR_NULL);
    {
        uint32_t *null_count = NULL;
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         tex, VERTEX_COUNT, &view, plan, RUN_COUNT, textures,
                         RUN_COUNT, arena, ARENA_BYTES, texcoords,
                         VERTEX_COUNT, entry_texture, entry_status, RUN_COUNT,
                         scratch, SCRATCH_BYTES, null_count, &tcount, &used),
                     TWLHD_PICA_FRAME_ERR_NULL);
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         tex, VERTEX_COUNT, &view, plan, RUN_COUNT, textures,
                         RUN_COUNT, arena, ARENA_BYTES, texcoords,
                         VERTEX_COUNT, entry_texture, entry_status, RUN_COUNT,
                         scratch, SCRATCH_BYTES, &count, null_count, &used),
                     TWLHD_PICA_FRAME_ERR_NULL);
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         tex, VERTEX_COUNT, &view, plan, RUN_COUNT, textures,
                         RUN_COUNT, arena, ARENA_BYTES, texcoords,
                         VERTEX_COUNT, entry_texture, entry_status, RUN_COUNT,
                         scratch, SCRATCH_BYTES, &count, &tcount, null_count),
                     TWLHD_PICA_FRAME_ERR_NULL);
    }

    /* Arena sizing errors. */
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(NULL, VERTEX_COUNT, &view,
                                               scratch, SCRATCH_BYTES,
                                               &needed),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, NULL, scratch,
                                               SCRATCH_BYTES, &needed),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view, NULL,
                                               SCRATCH_BYTES, &needed),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               scratch, SCRATCH_BYTES, NULL),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 4u, &view, scratch,
                                               SCRATCH_BYTES, &needed),
                 TWLHD_PICA_FRAME_ERR_COUNT);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               scratch, 100u, &needed),
                 TWLHD_PICA_FRAME_ERR_CAPACITY);

    /* Each capacity failure leaves every output byte untouched. */
    {
        struct {
            uint32_t plan_cap, tex_cap, arena_cap, tc_cap, entry_cap, scratch;
            TwlhdPicaFrameResult want;
        } cases[] = {
            {3u, RUN_COUNT, ARENA_BYTES, VERTEX_COUNT, RUN_COUNT, SCRATCH_BYTES,
             TWLHD_PICA_FRAME_ERR_CAPACITY},
            {RUN_COUNT, 1u, ARENA_BYTES, VERTEX_COUNT, RUN_COUNT, SCRATCH_BYTES,
             TWLHD_PICA_FRAME_ERR_CAPACITY},
            {RUN_COUNT, RUN_COUNT, ARENA_BYTES, VERTEX_COUNT - 1u, RUN_COUNT,
             SCRATCH_BYTES, TWLHD_PICA_FRAME_ERR_CAPACITY},
            {RUN_COUNT, RUN_COUNT, ARENA_BYTES, VERTEX_COUNT, 3u, SCRATCH_BYTES,
             TWLHD_PICA_FRAME_ERR_CAPACITY},
            {RUN_COUNT, RUN_COUNT, ARENA_BYTES, VERTEX_COUNT, RUN_COUNT, 100u,
             TWLHD_PICA_FRAME_ERR_CAPACITY},
            {RUN_COUNT, RUN_COUNT, ARENA_BYTES - 1u, VERTEX_COUNT, RUN_COUNT,
             SCRATCH_BYTES, TWLHD_PICA_FRAME_ERR_ARENA},
        };
        size_t k;

        for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
            out_reset(&g_out);
            CHECK_EQ_INT(run_prepare(&g_out, &view, tex, cases[k].plan_cap,
                                     cases[k].tex_cap, cases[k].arena_cap,
                                     cases[k].tc_cap, cases[k].entry_cap,
                                     cases[k].scratch),
                         cases[k].want);
            CHECK(outputs_untouched(&g_out));
        }
    }

    /* vertex_count % 3. */
    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 4u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_ERR_COUNT);
    CHECK(outputs_untouched(&g_out));

    /* Zero vertices: valid, empty frame. */
    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 0u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, 0u, g_out.texcoords, 0u,
                     g_out.entry_texture, g_out.entry_status, 0u, g_out.scratch,
                     SCRATCH_BYTES, &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 0u);
    CHECK_EQ_INT(g_out.texture_count, 0u);
    CHECK_EQ_INT(g_out.arena_used, 0u);

    /* The result and status name strings. */
    CHECK(strcmp(twlhd_pica_frame_result_string(TWLHD_PICA_FRAME_OK), "OK") ==
          0);
    CHECK(strcmp(twlhd_pica_frame_result_string(TWLHD_PICA_FRAME_ERR_NULL),
                 "ERR_NULL") == 0);
    CHECK(strcmp(twlhd_pica_frame_result_string(TWLHD_PICA_FRAME_ERR_CAPACITY),
                 "ERR_CAPACITY") == 0);
    CHECK(strcmp(twlhd_pica_frame_result_string(TWLHD_PICA_FRAME_ERR_ARENA),
                 "ERR_ARENA") == 0);
    CHECK(strcmp(twlhd_pica_frame_result_string(TWLHD_PICA_FRAME_ERR_COUNT),
                 "ERR_COUNT") == 0);
    CHECK(strcmp(twlhd_pica_frame_result_string((TwlhdPicaFrameResult)99),
                 "ERR_UNKNOWN") == 0);
    CHECK(strcmp(twlhd_pica_frame_entry_status_string(
                     TWLHD_PICA_FRAME_ENTRY_OK),
                 "OK") == 0);
    CHECK(strcmp(twlhd_pica_frame_entry_status_string(
                     TWLHD_PICA_FRAME_ENTRY_NO_DATA),
                 "NO_DATA") == 0);
    CHECK(strcmp(twlhd_pica_frame_entry_status_string(
                     TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED),
                 "UNSUPPORTED") == 0);
    CHECK(strcmp(twlhd_pica_frame_entry_status_string(
                     (TwlhdPicaFrameEntryStatus)99),
                 "ERR_UNKNOWN") == 0);

    /* The documented defaults and budget constants. */
    {
        TwlhdPicaFrameLimits limits;
        twlhd_pica_frame_default_limits(&limits);
        CHECK_EQ_INT(limits.max_textures, 256u);
        CHECK_EQ_INT(limits.arena_bytes, 1073741824u);
        CHECK_EQ_INT(limits.max_texcoords, 65536u);
        twlhd_pica_frame_default_limits(NULL);
        CHECK_EQ_INT(TWLHD_PICA_FRAME_MAX_TEXTURE_BYTES, 4194304u);
        CHECK_EQ_INT(TWLHD_PICA_FRAME_ARENA_PRACTICAL_BYTES, 8388608u);
        CHECK_EQ_INT(TWLHD_PICA_FRAME_SCRATCH_BYTES, 4194304u);
    }

    /* Unused stack buffers here are intentional: the macro takes all of them. */
#undef PREPARE
    test_report("pica-frame/errors");
}

static void test_missing_and_unsupported(void) {
    TwlhdGxTexVertex tex[VERTEX_COUNT];
    TwlhdV2View view;
    uint32_t needed = 0xDEADBEEFu;

    fill_tex_a();
    make_main_vertices(tex);

    /* Run 1 asks for a format-7 8x8 key that no record carries. */
    tex[3].teximage_param = PARAM_MISSING;
    tex[4].teximage_param = PARAM_MISSING;
    tex[5].teximage_param = PARAM_MISSING;
    tex[3].pltt_base = 0u;
    tex[4].pltt_base = 0u;
    tex[5].pltt_base = 0u;

    CHECK_EQ_INT(write_and_parse(g_a_only, 1u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               g_out.scratch, SCRATCH_BYTES,
                                               &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 256u);

    out_reset(&g_out);
    CHECK_EQ_INT(run_prepare(&g_out, &view, tex, RUN_COUNT, RUN_COUNT,
                             ARENA_BYTES, VERTEX_COUNT, RUN_COUNT,
                             SCRATCH_BYTES),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, RUN_COUNT);
    CHECK_EQ_INT(g_out.texture_count, 1u);
    CHECK_EQ_INT(g_out.arena_used, 256u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[1], TWLHD_PICA_FRAME_ENTRY_NO_DATA);
    CHECK_EQ_INT(g_out.entry_status[2], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[3], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[1], -1);
    CHECK_NEAR(g_out.texcoords[3].u, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[3].v, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[5].u, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[5].v, 0.0, 0.0);

    /* Hand-built view: a record whose texel_size is wrong -> UNSUPPORTED. */
    {
        uint8_t record[40];
        uint8_t txdt[16];
        TwlhdV2View broken;
        TwlhdGxTexVertex one[3];

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_MISSING);  /* format 7, 8x8 */
        put_le32(record + 4u, 0u);             /* pltt_base */
        put_le32(record + 8u, 0u);             /* texel_offset */
        put_le32(record + 12u, 4u);            /* texel_size: wrong (128) */
        memset(txdt, 0x22, sizeof(txdt));
        memset(&broken, 0, sizeof(broken));
        broken.txds_count = 1u;
        broken.txds = record;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);
        broken.pldt = NULL;
        broken.pldt_size = 0u;

        set_vtx(&one[0], 0.0f, 0.0f, PARAM_MISSING, 0u);
        set_vtx(&one[1], 1.0f, 1.0f, PARAM_MISSING, 0u);
        set_vtx(&one[2], 2.0f, 2.0f, PARAM_MISSING, 0u);

        out_reset(&g_out);
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         one, 3u, &broken, g_out.plan, RUN_COUNT,
                         g_out.textures, RUN_COUNT, g_out.arena, ARENA_BYTES,
                         g_out.texcoords, VERTEX_COUNT, g_out.entry_texture,
                         g_out.entry_status, RUN_COUNT, g_out.scratch,
                         SCRATCH_BYTES, &g_out.entry_count,
                         &g_out.texture_count, &g_out.arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(g_out.entry_count, 1u);
        CHECK_EQ_INT(g_out.texture_count, 0u);
        CHECK_EQ_INT(g_out.arena_used, 0u);
        CHECK_EQ_INT(g_out.entry_status[0],
                     TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED);
        CHECK_EQ_INT(g_out.entry_texture[0], -1);
        CHECK_NEAR(g_out.texcoords[0].u, 0.0, 0.0);
        CHECK_NEAR(g_out.texcoords[0].v, 0.0, 0.0);
    }

    test_report("pica-frame/missing-and-unsupported");
}

/*
 * Guard harness: [OOB_GUARD bytes][exact payload][OOB_GUARD bytes] around
 * every caller output and around the input vertex array. Guard bytes are
 * checked over their whole length; the input tail is filled 0x00 in pass A and
 * 0xFF in pass B with an identical payload, so any over-read of tex[] changes
 * an output and fails the memcmp.
 */
#define OOB_GUARD 128u
#define OOB_FILL 0x5B

static _Alignas(8) uint8_t
    g_oob_tex[OOB_GUARD + VERTEX_COUNT * sizeof(TwlhdGxTexVertex) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_plan[OOB_GUARD + RUN_COUNT * sizeof(TwlhdPicaBindPlan) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_texs[OOB_GUARD + RUN_COUNT * sizeof(TwlhdPicaFrameTexture) + OOB_GUARD];
static _Alignas(8) uint8_t g_oob_tc[OOB_GUARD +
                                  VERTEX_COUNT * sizeof(TwlhdPicaTexCoord) +
                                  OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_et[OOB_GUARD + RUN_COUNT * sizeof(int32_t) + OOB_GUARD];
static _Alignas(8) uint8_t g_oob_es[OOB_GUARD +
                                  RUN_COUNT * sizeof(TwlhdPicaFrameEntryStatus) +
                                  OOB_GUARD];
static _Alignas(128) uint8_t g_oob_arena[OOB_GUARD + ARENA_BYTES + OOB_GUARD];

#define OOB_TEX ((const TwlhdGxTexVertex *)(g_oob_tex + OOB_GUARD))
#define OOB_PLAN ((TwlhdPicaBindPlan *)(g_oob_plan + OOB_GUARD))
#define OOB_TEXS ((TwlhdPicaFrameTexture *)(g_oob_texs + OOB_GUARD))
#define OOB_TC ((TwlhdPicaTexCoord *)(g_oob_tc + OOB_GUARD))
#define OOB_ET ((int32_t *)(g_oob_et + OOB_GUARD))
#define OOB_ES ((TwlhdPicaFrameEntryStatus *)(g_oob_es + OOB_GUARD))
#define OOB_ARENA (g_oob_arena + OOB_GUARD)

static int guard_intact(const uint8_t *base, size_t used, int fill) {
    size_t i;
    for (i = 0; i < OOB_GUARD; i++) {
        if (base[i] != (uint8_t)fill) return 0;
        if (base[OOB_GUARD + used + i] != (uint8_t)fill) return 0;
    }
    return 1;
}

static void test_out_of_bounds(void) {
    TwlhdGxTexVertex payload[VERTEX_COUNT];
    TwlhdV2View view;
    uint8_t plan_copy[2][sizeof(g_oob_plan)];
    uint8_t texs_copy[2][sizeof(g_oob_texs)];
    uint8_t tc_copy[2][sizeof(g_oob_tc)];
    uint8_t et_copy[2][sizeof(g_oob_et)];
    uint8_t es_copy[2][sizeof(g_oob_es)];
    uint8_t arena_copy[2][sizeof(g_oob_arena)];
    uint32_t count_copy[2][3];
    uint32_t pass;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();
    make_main_vertices(payload);
    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        uint32_t entry_count = 0xDEADBEEFu;
        uint32_t texture_count = 0xDEADBEEFu;
        uint32_t arena_used = 0xDEADBEEFu;

        memset(g_oob_tex, fill, sizeof(g_oob_tex));
        memcpy(g_oob_tex + OOB_GUARD, payload, sizeof(payload));

        memset(g_oob_plan, OOB_FILL, sizeof(g_oob_plan));
        memset(g_oob_texs, OOB_FILL, sizeof(g_oob_texs));
        memset(g_oob_tc, OOB_FILL, sizeof(g_oob_tc));
        memset(g_oob_et, OOB_FILL, sizeof(g_oob_et));
        memset(g_oob_es, OOB_FILL, sizeof(g_oob_es));
        memset(g_oob_arena, OOB_FILL, sizeof(g_oob_arena));

        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         OOB_TEX, VERTEX_COUNT, &view, OOB_PLAN, RUN_COUNT,
                         OOB_TEXS, RUN_COUNT, OOB_ARENA, ARENA_BYTES, OOB_TC,
                         VERTEX_COUNT, OOB_ET, OOB_ES, RUN_COUNT,
                         g_out.scratch, SCRATCH_BYTES, &entry_count,
                         &texture_count, &arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(entry_count, RUN_COUNT);
        CHECK_EQ_INT(texture_count, 2u);
        CHECK_EQ_INT(arena_used, ARENA_BYTES);

        CHECK(guard_intact(g_oob_tex,
                           VERTEX_COUNT * sizeof(TwlhdGxTexVertex), fill));
        CHECK(guard_intact(g_oob_plan, RUN_COUNT * sizeof(TwlhdPicaBindPlan),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_texs,
                           RUN_COUNT * sizeof(TwlhdPicaFrameTexture), OOB_FILL));
        CHECK(guard_intact(g_oob_tc, VERTEX_COUNT * sizeof(TwlhdPicaTexCoord),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_et, RUN_COUNT * sizeof(int32_t), OOB_FILL));
        CHECK(guard_intact(g_oob_es,
                           RUN_COUNT * sizeof(TwlhdPicaFrameEntryStatus),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_arena, ARENA_BYTES, OOB_FILL));

        memcpy(plan_copy[pass], g_oob_plan, sizeof(g_oob_plan));
        memcpy(texs_copy[pass], g_oob_texs, sizeof(g_oob_texs));
        memcpy(tc_copy[pass], g_oob_tc, sizeof(g_oob_tc));
        memcpy(et_copy[pass], g_oob_et, sizeof(g_oob_et));
        memcpy(es_copy[pass], g_oob_es, sizeof(g_oob_es));
        memcpy(arena_copy[pass], g_oob_arena, sizeof(g_oob_arena));
        count_copy[pass][0] = entry_count;
        count_copy[pass][1] = texture_count;
        count_copy[pass][2] = arena_used;
    }

    /* A tex[-1]/tex[vertex_count] read sees 0x00 vs 0xFF and diverges. */
    CHECK(memcmp(plan_copy[0], plan_copy[1], sizeof(g_oob_plan)) == 0);
    CHECK(memcmp(texs_copy[0], texs_copy[1], sizeof(g_oob_texs)) == 0);
    CHECK(memcmp(tc_copy[0], tc_copy[1], sizeof(g_oob_tc)) == 0);
    CHECK(memcmp(et_copy[0], et_copy[1], sizeof(g_oob_et)) == 0);
    CHECK(memcmp(es_copy[0], es_copy[1], sizeof(g_oob_es)) == 0);
    CHECK(memcmp(arena_copy[0], arena_copy[1], sizeof(g_oob_arena)) == 0);
    CHECK(memcmp(count_copy[0], count_copy[1], sizeof(count_copy[0])) == 0);

    test_report("pica-frame/oob");
}

int main(void) {
    test_prepare_basic();
    test_merged_run_indexing();
    test_mixed_vertex_runs();
    test_dedupe_key_halves();
    test_errors();
    test_missing_and_unsupported();
    test_out_of_bounds();
    return test_exit_code();
}
