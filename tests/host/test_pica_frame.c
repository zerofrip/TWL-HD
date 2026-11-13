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
/* Format 4 (I8), 8x8: 64 texel B, 512 palette B. */
#define PARAM_FMT4_8 0x10000000u
/* Format 6 (A5I3), 8x8: 64 texel B, 16 palette B. */
#define PARAM_FMT6_8 0x18000000u
/* Format 5 (4x4), 8x8: 16 texel B, 8 palette B, 8 index B. */
#define PARAM_FMT5_8 0x14000000u

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
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 6u, &view, &needed),
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
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 6u, &view, &needed),
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
                                               &needed),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, NULL,
                                               &needed),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view, NULL),
                 TWLHD_PICA_FRAME_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 4u, &view, &needed),
                 TWLHD_PICA_FRAME_ERR_COUNT);

    /* Sizing needs no scratch buffer at all: the exact answer comes out with
     * no work buffer, and only *out_arena_bytes is written. (The old
     * small-scratch ERR_CAPACITY no longer exists; the remaining sizing
     * capacity error, >MAX textures, is pinned by
     * pica-frame/texture-cap-256.) */
    needed = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, ARENA_BYTES);

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

/* ------------------------------------------------------------------------- */
/* Round-3: arena_needed is a pure sizing pass and answers per-entry status.  */
/* ------------------------------------------------------------------------- */

/* Inputs and dedicated output buffers snapshotted by the no-write test. */
static uint8_t g_snap_txds[TWLHD_V2_TXDS_MAX_RECORDS * TWLHD_V2_TXDS_RECORD_SIZE];
static uint8_t g_snap_txdt[TWLHD_V2_TXDT_MAX_BYTES];
static uint8_t g_snap_pldt[TWLHD_V2_PLDT_MAX_BYTES];
static uint8_t g_snap_arena[ARENA_BYTES];
static TwlhdPicaTexCoord g_snap_texcoords[VERTEX_COUNT];

/*
 * The sizing pass writes only *out_arena_bytes: the vertex array, the parsed
 * view (struct and TXDS/TXDT/PLDT bytes) and any arena/texcoords buffers the
 * caller happens to pass must be byte-identical afterwards.
 */
static void test_arena_needed_no_writes(void) {
    TwlhdGxTexVertex tex[VERTEX_COUNT];
    TwlhdGxTexVertex tex_copy[VERTEX_COUNT];
    TwlhdV2View view;
    TwlhdV2View view_copy;
    uint32_t needed = 0xDEADBEEFu;
    size_t txds_bytes;

    fill_tex_a();
    fill_pal_b();
    fill_tex_b();
    make_main_vertices(tex);
    CHECK_EQ_INT(write_and_parse(g_main_entries, 2u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_txds_count(&view), 2u);

    txds_bytes = (size_t)view.txds_count * TWLHD_V2_TXDS_RECORD_SIZE;
    CHECK(txds_bytes <= sizeof(g_snap_txds));
    CHECK((size_t)view.txdt_size <= sizeof(g_snap_txdt));
    CHECK((size_t)view.pldt_size <= sizeof(g_snap_pldt));

    memcpy(tex_copy, tex, sizeof(tex));
    memcpy(&view_copy, &view, sizeof(view));
    memcpy(g_snap_txds, view.txds, txds_bytes);
    memcpy(g_snap_txdt, view.txdt, view.txdt_size);
    memcpy(g_snap_pldt, view.pldt, view.pldt_size);
    memset(g_snap_arena, 0x5B, sizeof(g_snap_arena));
    memset(g_snap_texcoords, 0x5B, sizeof(g_snap_texcoords));

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, VERTEX_COUNT, &view,
                                               &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, ARENA_BYTES); /* two textures: 256 + 512 */

    CHECK(memcmp(tex, tex_copy, sizeof(tex)) == 0);
    CHECK(memcmp(&view, &view_copy, sizeof(view)) == 0);
    CHECK(memcmp(view.txds, g_snap_txds, txds_bytes) == 0);
    CHECK(memcmp(view.txdt, g_snap_txdt, view.txdt_size) == 0);
    CHECK(memcmp(view.pldt, g_snap_pldt, view.pldt_size) == 0);
    CHECK(all_bytes_are(g_snap_arena, sizeof(g_snap_arena), 0x5B));
    CHECK(all_bytes_are(g_snap_texcoords, sizeof(g_snap_texcoords), 0x5B));

    test_report("pica-frame/arena-needed-no-writes");
}

/*
 * One run whose record the decoder would refuse: sizing must answer 0 and
 * prepare must agree with UNSUPPORTED (same shared predicate), storing no
 * texture. The views are hand-built because the parser rejects these records
 * (common/twlhd_v2_parse.c:379-384), exactly as in the broken-record test.
 */
static void expect_sizing_unsupported_zero(const TwlhdV2View *broken,
                                           uint32_t param) {
    TwlhdGxTexVertex one[3];
    uint32_t needed = 0xDEADBEEFu;

    set_vtx(&one[0], 0.0f, 0.0f, param, 0u);
    set_vtx(&one[1], 2.0f, 2.0f, param, 0u);
    set_vtx(&one[2], 4.0f, 4.0f, param, 0u);

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(one, 3u, broken, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 0u);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     one, 3u, broken, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 1u);
    CHECK_EQ_INT(g_out.texture_count, 0u);
    CHECK_EQ_INT(g_out.arena_used, 0u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED);
    CHECK_EQ_INT(g_out.entry_texture[0], -1);
}

/* Hand-built format-5 view: 8x8, 16 texel bytes, 8 index bytes, and
 * `palette_size` palette bytes whose four block words are words[0..3]. */
static void make_fmt5_view(TwlhdV2View *view, uint8_t *record, uint8_t *txdt,
                           uint8_t *pldt, uint32_t palette_size,
                           const uint16_t *words) {
    uint32_t block;

    memset(record, 0, TWLHD_V2_TXDS_RECORD_SIZE);
    put_le32(record + 0u, PARAM_FMT5_8);
    put_le32(record + 12u, 16u);          /* texel_size */
    put_le32(record + 20u, palette_size); /* palette_size */
    put_le32(record + 24u, 16u);          /* index_offset */
    put_le32(record + 28u, 8u);           /* index_size */
    memset(txdt, 0, 24u);
    for (block = 0; block < 4u; block++) {
        put_le16(txdt + 16u + block * 2u, words[block]);
    }
    memset(pldt, 0x44, palette_size);

    memset(view, 0, sizeof(*view));
    view->txds = record;
    view->txds_count = 1u;
    view->txdt = txdt;
    view->txdt_size = 24u;
    view->pldt = pldt;
    view->pldt_size = palette_size;
}

/* The same frame must size to one 8x8 RGBA8 texture and prepare it. */
static void expect_sizing_ok_256(const TwlhdV2View *view5) {
    TwlhdGxTexVertex one[3];
    uint32_t needed = 0xDEADBEEFu;

    set_vtx(&one[0], 0.0f, 0.0f, PARAM_FMT5_8, 0u);
    set_vtx(&one[1], 2.0f, 2.0f, PARAM_FMT5_8, 0u);
    set_vtx(&one[2], 4.0f, 4.0f, PARAM_FMT5_8, 0u);

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(one, 3u, view5, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 256u);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     one, 3u, view5, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                     &g_out.entry_count, &g_out.texture_count,
                     &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 1u);
    CHECK_EQ_INT(g_out.texture_count, 1u);
    CHECK_EQ_INT(g_out.arena_used, 256u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
}

static void test_arena_needed_status(void) {
    /* (a) texel_size does not match twlhd_v2_tex_sizes(): :171-173. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[16];
        TwlhdV2View broken;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_MISSING); /* format 7, 8x8 */
        put_le32(record + 4u, 0u);
        put_le32(record + 8u, 0u);  /* texel_offset */
        put_le32(record + 12u, 4u); /* texel_size: wrong (128) */
        memset(txdt, 0x22, sizeof(txdt));

        memset(&broken, 0, sizeof(broken));
        broken.txds = record;
        broken.txds_count = 1u;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);

        expect_sizing_unsupported_zero(&broken, PARAM_MISSING);
    }

    /* (b) format 5, valid pointers and sizes, but the first index word's
     * palette window leaves palette_size: :239-247. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24]; /* 16 texel + 8 index bytes */
        uint8_t pldt[8];
        TwlhdV2View broken;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_FMT5_8); /* 4x4, 8x8 */
        put_le32(record + 4u, 0u);
        put_le32(record + 8u, 0u);   /* texel_offset */
        put_le32(record + 12u, 16u); /* texel_size */
        put_le32(record + 16u, 0u);  /* palette_offset */
        put_le32(record + 20u, 8u);  /* palette_size */
        put_le32(record + 24u, 16u); /* index_offset */
        put_le32(record + 28u, 8u);  /* index_size */
        memset(txdt, 0x33, sizeof(txdt));
        /* Word 1 = mode 0, palette offset 1 -> 4 + 6 = 10 > 8. */
        put_le16(txdt + 16u, 1u);
        memset(pldt, 0x44, sizeof(pldt));

        memset(&broken, 0, sizeof(broken));
        broken.txds = record;
        broken.txds_count = 1u;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);
        broken.pldt = pldt;
        broken.pldt_size = sizeof(pldt);

        expect_sizing_unsupported_zero(&broken, PARAM_FMT5_8);
    }

    /* (c) format 4 with palette_size below the fixed 256-entry table: :174-181.
     * Palette sizes must also pass twlhd_v2_tex_palette_size_ok(), so the
     * record is refused at that check and the fixed-table rule backs it up. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[64];
        uint8_t pldt[8];
        TwlhdV2View broken;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_FMT4_8); /* I8, 8x8 */
        put_le32(record + 4u, 0u);
        put_le32(record + 8u, 0u);   /* texel_offset */
        put_le32(record + 12u, 64u); /* texel_size */
        put_le32(record + 16u, 0u);  /* palette_offset */
        put_le32(record + 20u, 8u);  /* palette_size: 512 required */
        memset(txdt, 0x55, sizeof(txdt));
        memset(pldt, 0x66, sizeof(pldt));

        memset(&broken, 0, sizeof(broken));
        broken.txds = record;
        broken.txds_count = 1u;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);
        broken.pldt = pldt;
        broken.pldt_size = sizeof(pldt);

        expect_sizing_unsupported_zero(&broken, PARAM_FMT4_8);
    }

    /* (d) exact fit: block offset 4 + mode-1 window 4 == palette_size 8, the
     * decoder's `end > palette_size` boundary (:242-246) accepts equality. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24]; /* 16 texel + 8 index bytes */
        uint8_t pldt[8];
        TwlhdV2View view5;
        TwlhdGxTexVertex one[3];
        uint32_t needed = 0xDEADBEEFu;
        uint32_t block;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_FMT5_8);
        put_le32(record + 12u, 16u); /* texel_size */
        put_le32(record + 20u, 8u);  /* palette_size */
        put_le32(record + 24u, 16u); /* index_offset */
        put_le32(record + 28u, 8u);  /* index_size */
        memset(txdt, 0x33, sizeof(txdt));
        /* Word 0x4001 = mode 1, palette offset 1 -> 4 + 4 == 8. */
        for (block = 0; block < 4u; block++) {
            put_le16(txdt + 16u + block * 2u, 0x4001u);
        }
        memset(pldt, 0x44, sizeof(pldt));

        memset(&view5, 0, sizeof(view5));
        view5.txds = record;
        view5.txds_count = 1u;
        view5.txdt = txdt;
        view5.txdt_size = sizeof(txdt);
        view5.pldt = pldt;
        view5.pldt_size = sizeof(pldt);

        set_vtx(&one[0], 0.0f, 0.0f, PARAM_FMT5_8, 0u);
        set_vtx(&one[1], 2.0f, 2.0f, PARAM_FMT5_8, 0u);
        set_vtx(&one[2], 4.0f, 4.0f, PARAM_FMT5_8, 0u);

        CHECK_EQ_INT(twlhd_pica_frame_arena_needed(one, 3u, &view5, &needed),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(needed, 256u);

        out_reset(&g_out);
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         one, 3u, &view5, g_out.plan, RUN_COUNT,
                         g_out.textures, RUN_COUNT, g_out.arena, ARENA_BYTES,
                         g_out.texcoords, VERTEX_COUNT, g_out.entry_texture,
                         g_out.entry_status, RUN_COUNT, g_out.scratch,
                         SCRATCH_BYTES, &g_out.entry_count,
                         &g_out.texture_count, &g_out.arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(g_out.entry_count, 1u);
        CHECK_EQ_INT(g_out.texture_count, 1u);
        CHECK_EQ_INT(g_out.arena_used, 256u);
        CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
        CHECK_EQ_INT(g_out.entry_texture[0], 0);
        CHECK_EQ_INT(g_out.textures[0].width, 8u);
        CHECK_EQ_INT(g_out.textures[0].height, 8u);
    }

    /* (e) near miss: mode 1 at palette offset 2 needs 8 + 4 = 12 > 10. The
     * predicate must reject it, otherwise arena_needed would count a texture
     * the write-pass decode refuses (:244) and sizing would disagree. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24];
        uint8_t pldt[10];
        TwlhdV2View broken;
        uint32_t block;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_FMT5_8);
        put_le32(record + 12u, 16u); /* texel_size */
        put_le32(record + 20u, 10u); /* palette_size */
        put_le32(record + 24u, 16u); /* index_offset */
        put_le32(record + 28u, 8u);  /* index_size */
        memset(txdt, 0x33, sizeof(txdt));
        for (block = 0; block < 4u; block++) {
            put_le16(txdt + 16u + block * 2u, 0x4002u);
        }
        memset(pldt, 0x44, sizeof(pldt));

        memset(&broken, 0, sizeof(broken));
        broken.txds = record;
        broken.txds_count = 1u;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);
        broken.pldt = pldt;
        broken.pldt_size = sizeof(pldt);

        expect_sizing_unsupported_zero(&broken, PARAM_FMT5_8);
    }

    /* (f) exact fit for every block mode: mode 0 needs 6 bytes, mode 2 needs
     * 8, mode 3 (the default) needs 4 (:88-95). Equality must be accepted. */
    {
        static const struct {
            uint32_t palette_size;
            uint16_t word;
        } exact[] = {
            {6u, 0x0000u},  /* mode 0, offset 0: 0 + 6 == 6 */
            {8u, 0x8000u},  /* mode 2, offset 0: 0 + 8 == 8 */
            {10u, 0xC001u}, /* mode 3, offset 1: 4 + 4 == 10 */
        };
        uint32_t c;

        for (c = 0; c < sizeof(exact) / sizeof(exact[0]); c++) {
            uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
            uint8_t txdt[24];
            uint8_t pldt[16];
            TwlhdV2View view5;
            uint16_t words[4];

            words[0] = exact[c].word;
            words[1] = exact[c].word;
            words[2] = exact[c].word;
            words[3] = exact[c].word;
            make_fmt5_view(&view5, record, txdt, pldt, exact[c].palette_size,
                           words);
            expect_sizing_ok_256(&view5);
        }
    }

    /* (g) the per-block range check must visit every block and use the whole
     * offset field: out of range in block 0 only, then in block 1 only. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24];
        uint8_t pldt[6];
        TwlhdV2View broken;
        uint16_t words[4];

        words[0] = 0x0001u; /* offset 4, mode 0: 4 + 6 > 6 */
        words[1] = 0x0000u;
        words[2] = 0x0000u;
        words[3] = 0x0000u;
        make_fmt5_view(&broken, record, txdt, pldt, 6u, words);
        expect_sizing_unsupported_zero(&broken, PARAM_FMT5_8);

        words[0] = 0x0000u;
        words[1] = 0x0001u;
        words[2] = 0x0000u;
        words[3] = 0x0000u;
        make_fmt5_view(&broken, record, txdt, pldt, 6u, words);
        expect_sizing_unsupported_zero(&broken, PARAM_FMT5_8);

        /* Only block 2 is out of range: the block count must not shrink. */
        words[0] = 0x0000u;
        words[1] = 0x0000u;
        words[2] = 0x0001u;
        words[3] = 0x0000u;
        make_fmt5_view(&broken, record, txdt, pldt, 6u, words);
        expect_sizing_unsupported_zero(&broken, PARAM_FMT5_8);
    }

    test_report("pica-frame/arena-needed-status");
}

/*
 * Format-5 palette windows pinned per block mode, for arena_needed AND prepare
 * (the helpers assert the two agree). A mode-0 block needs 6 palette bytes and
 * a mode-2 block 8 (:88-95); a predicate that used 4 for every mode would let
 * a window that fits 4 bytes but not 6/8 through and size a texture the decode
 * pass refuses.
 */
static void test_fmt5_palette_window(void) {
    /* (a) mode 0 at offset 4: 4 + 6 == 10 exact fit, then 2 bytes short. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24];
        uint8_t pldt[16];
        TwlhdV2View view5;
        uint16_t words[4] = {0x0001u, 0x0001u, 0x0001u, 0x0001u};

        make_fmt5_view(&view5, record, txdt, pldt, 10u, words);
        expect_sizing_ok_256(&view5);
        make_fmt5_view(&view5, record, txdt, pldt, 8u, words);
        expect_sizing_unsupported_zero(&view5, PARAM_FMT5_8);
    }

    /* (b) mode 2 at offset 4: 4 + 8 == 12 exact fit; the same window fits 4
     * but not 8 bytes at palette_size 8. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24];
        uint8_t pldt[16];
        TwlhdV2View view5;
        uint16_t words[4] = {0x8001u, 0x8001u, 0x8001u, 0x8001u};

        make_fmt5_view(&view5, record, txdt, pldt, 12u, words);
        expect_sizing_ok_256(&view5);
        make_fmt5_view(&view5, record, txdt, pldt, 8u, words);
        expect_sizing_unsupported_zero(&view5, PARAM_FMT5_8);
    }

    /* (c) the 4-byte modes: 1 at offset 4 and 3 (the default) at offset 4:
     * 4 + 4 == 8 exact fit, then 2 bytes short. */
    {
        static const uint16_t four_byte_words[] = {0x4001u, 0xC001u};
        uint32_t c;

        for (c = 0; c < 2u; c++) {
            uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
            uint8_t txdt[24];
            uint8_t pldt[16];
            TwlhdV2View view5;
            uint16_t words[4];

            words[0] = four_byte_words[c];
            words[1] = four_byte_words[c];
            words[2] = four_byte_words[c];
            words[3] = four_byte_words[c];
            make_fmt5_view(&view5, record, txdt, pldt, 8u, words);
            expect_sizing_ok_256(&view5);
            make_fmt5_view(&view5, record, txdt, pldt, 6u, words);
            expect_sizing_unsupported_zero(&view5, PARAM_FMT5_8);
        }
    }

    /* (d) the loop bounds: the mode-2 offender is the LAST block, then the
     * FIRST block; 0x4000 (mode 1, offset 0) is a benign filler at 4 <= 8. */
    {
        uint8_t record[TWLHD_V2_TXDS_RECORD_SIZE];
        uint8_t txdt[24];
        uint8_t pldt[16];
        TwlhdV2View view5;
        uint16_t words[4];

        words[0] = 0x4000u;
        words[1] = 0x4000u;
        words[2] = 0x4000u;
        words[3] = 0x8001u;
        make_fmt5_view(&view5, record, txdt, pldt, 8u, words);
        expect_sizing_unsupported_zero(&view5, PARAM_FMT5_8);

        words[0] = 0x8001u;
        words[1] = 0x4000u;
        words[2] = 0x4000u;
        words[3] = 0x4000u;
        make_fmt5_view(&view5, record, txdt, pldt, 8u, words);
        expect_sizing_unsupported_zero(&view5, PARAM_FMT5_8);
    }

    test_report("pica-frame/fmt5-palette-window");
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

/* ------------------------------------------------------------------------- */
/* Round-6: vertex_count == 0 is an empty frame, not a run at index 0.        */
/* ------------------------------------------------------------------------- */

/*
 * The write pass used to treat i == j == vertex_count == 0 as a run boundary:
 * it classified tex[0] and wrote entry_status[0] / entry_texture[0] (and
 * textures[0] plus arena bytes with a matching TXDS record) although every
 * capacity was 0. tex[0] is a valid textured key with a matching record, so
 * the old code really decoded and swizzled; the guards catch the writes.
 */
static void test_zero_vertex_guards(void) {
    TwlhdGxTexVertex payload[1];
    TwlhdV2View view;
    uint8_t plan_copy[2][sizeof(g_oob_plan)];
    uint8_t texs_copy[2][sizeof(g_oob_texs)];
    uint8_t tc_copy[2][sizeof(g_oob_tc)];
    uint8_t et_copy[2][sizeof(g_oob_et)];
    uint8_t es_copy[2][sizeof(g_oob_es)];
    uint8_t arena_copy[2][sizeof(g_oob_arena)];
    uint32_t count_copy[2][3];
    const size_t one = sizeof(TwlhdGxTexVertex);
    uint32_t pass;

    fill_tex_a();
    set_vtx(&payload[0], 0.0f, 0.0f, PARAM_A, 0u);
    CHECK_EQ_INT(write_and_parse(g_a_only, 1u, &view), TWLHD_V2_OK);

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        uint32_t entry_count = 0xDEADBEEFu;
        uint32_t texture_count = 0xDEADBEEFu;
        uint32_t arena_used = 0xDEADBEEFu;

        /* One-element tex array: only tex[0] is payload, the rest is poison. */
        memset(g_oob_tex, fill, sizeof(g_oob_tex));
        memcpy(g_oob_tex + OOB_GUARD, payload, sizeof(payload));

        memset(g_oob_plan, OOB_FILL, sizeof(g_oob_plan));
        memset(g_oob_texs, OOB_FILL, sizeof(g_oob_texs));
        memset(g_oob_tc, OOB_FILL, sizeof(g_oob_tc));
        memset(g_oob_et, OOB_FILL, sizeof(g_oob_et));
        memset(g_oob_es, OOB_FILL, sizeof(g_oob_es));
        memset(g_oob_arena, OOB_FILL, sizeof(g_oob_arena));

        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         OOB_TEX, 0u, &view, OOB_PLAN, 0u, OOB_TEXS, 0u,
                         OOB_ARENA, 0u, OOB_TC, 0u, OOB_ET, OOB_ES, 0u,
                         g_out.scratch, SCRATCH_BYTES, &entry_count,
                         &texture_count, &arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(entry_count, 0u);
        CHECK_EQ_INT(texture_count, 0u);
        CHECK_EQ_INT(arena_used, 0u);

        CHECK(guard_intact(g_oob_tex, one, fill));
        CHECK(guard_intact(g_oob_plan, 0u, OOB_FILL));
        CHECK(guard_intact(g_oob_texs, 0u, OOB_FILL));
        CHECK(guard_intact(g_oob_tc, 0u, OOB_FILL));
        CHECK(guard_intact(g_oob_et, 0u, OOB_FILL));
        CHECK(guard_intact(g_oob_es, 0u, OOB_FILL));
        CHECK(guard_intact(g_oob_arena, 0u, OOB_FILL));

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

    /* An over-read of the tex[1..] tail (0x00 vs 0xFF) would diverge here. */
    CHECK(memcmp(plan_copy[0], plan_copy[1], sizeof(g_oob_plan)) == 0);
    CHECK(memcmp(texs_copy[0], texs_copy[1], sizeof(g_oob_texs)) == 0);
    CHECK(memcmp(tc_copy[0], tc_copy[1], sizeof(g_oob_tc)) == 0);
    CHECK(memcmp(et_copy[0], et_copy[1], sizeof(g_oob_et)) == 0);
    CHECK(memcmp(es_copy[0], es_copy[1], sizeof(g_oob_es)) == 0);
    CHECK(memcmp(arena_copy[0], arena_copy[1], sizeof(g_oob_arena)) == 0);
    CHECK(memcmp(count_copy[0], count_copy[1], sizeof(count_copy[0])) == 0);

    /* arena_needed with zero vertices writes only *out_arena_bytes. */
    {
        TwlhdGxTexVertex tex_copy[1];
        TwlhdV2View view_copy;
        uint32_t needed = 0xDEADBEEFu;

        memcpy(tex_copy, payload, sizeof(payload));
        memcpy(&view_copy, &view, sizeof(view));

        CHECK_EQ_INT(twlhd_pica_frame_arena_needed(OOB_TEX, 0u, &view,
                                                   &needed),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(needed, 0u);
        CHECK(memcmp(OOB_TEX, tex_copy, sizeof(tex_copy)) == 0);
        CHECK(memcmp(&view, &view_copy, sizeof(view)) == 0);
    }

    /* The documented NULL rule still wins for tex over an empty frame. */
    {
        uint32_t entry_count = 0xDEADBEEFu;
        uint32_t texture_count = 0xDEADBEEFu;
        uint32_t arena_used = 0xDEADBEEFu;
        uint32_t needed = 0xDEADBEEFu;

        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         NULL, 0u, &view, OOB_PLAN, 0u, OOB_TEXS, 0u,
                         OOB_ARENA, 0u, OOB_TC, 0u, OOB_ET, OOB_ES, 0u,
                         g_out.scratch, SCRATCH_BYTES, &entry_count,
                         &texture_count, &arena_used),
                     TWLHD_PICA_FRAME_ERR_NULL);
        CHECK_EQ_INT(entry_count, 0xDEADBEEFu);
        CHECK_EQ_INT(texture_count, 0xDEADBEEFu);
        CHECK_EQ_INT(arena_used, 0xDEADBEEFu);

        CHECK_EQ_INT(twlhd_pica_frame_arena_needed(NULL, 0u, &view, &needed),
                     TWLHD_PICA_FRAME_ERR_NULL);
        CHECK_EQ_INT(needed, 0xDEADBEEFu);
    }

    test_report("pica-frame/zero-vertex-guards");
}

/* ------------------------------------------------------------------------- */
/* Round-1 mutation survivors: exact boundaries and last-element cases.       */
/* ------------------------------------------------------------------------- */

/* Format 7, 1024x8 (S shift 7) and 8x1024 (T shift 7). */
#define PARAM_BIG_W 0x1C700000u
#define PARAM_BIG_H 0x1F800000u

/*
 * The largest reachable sizes: 1024x8 and 8x1024 both pass the PICA size
 * check, use 32768 scratch bytes each and 65536 arena bytes together. A
 * mutated upper bound (1024 -> 1023, <= -> <) turns both entries UNSUPPORTED.
 */
static void test_size_bounds_1024(void) {
    static uint8_t tex[2][16384]; /* 8192 texels x 2 bytes each */
    static uint8_t arena[65536];
    static uint8_t scratch[32768];
    static const TwlhdV2TexEntry entries[2] = {
        {PARAM_BIG_W, 0u, tex[0], NULL, NULL, 16384u, 0u, 0u},
        {PARAM_BIG_H, 1u, tex[1], NULL, NULL, 16384u, 0u, 0u},
    };
    TwlhdGxTexVertex verts[6];
    TwlhdV2View view;
    TwlhdPicaBindPlan plan[2];
    TwlhdPicaFrameTexture textures[2];
    TwlhdPicaTexCoord texcoords[6];
    int32_t entry_texture[2];
    TwlhdPicaFrameEntryStatus entry_status[2];
    uint32_t entry_count = 0;
    uint32_t texture_count = 0;
    uint32_t arena_used = 0;
    uint32_t needed = 0xDEADBEEFu;
    uint32_t i;

    /* 0x801F = opaque red, 0x83E0 = opaque green; both formats are 16bpp. */
    for (i = 0; i < 8192u; i++) {
        put_le16(tex[0] + i * 2u, 0x801Fu);
        put_le16(tex[1] + i * 2u, 0x83E0u);
    }
    set_vtx(&verts[0], 0.0f, 0.0f, PARAM_BIG_W, 0u);
    set_vtx(&verts[1], 1.0f, 1.0f, PARAM_BIG_W, 0u);
    set_vtx(&verts[2], 2.0f, 2.0f, PARAM_BIG_W, 0u);
    set_vtx(&verts[3], 0.0f, 0.0f, PARAM_BIG_H, 1u);
    set_vtx(&verts[4], 1.0f, 1.0f, PARAM_BIG_H, 1u);
    set_vtx(&verts[5], 2.0f, 2.0f, PARAM_BIG_H, 1u);

    CHECK_EQ_INT(write_and_parse(entries, 2u, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(verts, 6u, &view, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 65536u);

    memset(plan, 0, sizeof(plan));
    memset(textures, 0, sizeof(textures));
    memset(texcoords, 0, sizeof(texcoords));
    memset(entry_texture, 0, sizeof(entry_texture));
    memset(entry_status, 0, sizeof(entry_status));
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     verts, 6u, &view, plan, 2u, textures, 2u, arena, 65536u,
                     texcoords, 6u, entry_texture, entry_status, 2u, scratch,
                     32768u, &entry_count, &texture_count, &arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(entry_count, 2u);
    CHECK_EQ_INT(texture_count, 2u);
    CHECK_EQ_INT(arena_used, 65536u);
    CHECK_EQ_INT(plan[0].first_vertex, 0u);
    CHECK_EQ_INT(plan[0].vertex_count, 3u);
    CHECK_EQ_INT(plan[1].first_vertex, 3u);
    CHECK_EQ_INT(plan[1].vertex_count, 3u);
    CHECK_EQ_INT(entry_texture[0], 0);
    CHECK_EQ_INT(entry_texture[1], 1);
    CHECK_EQ_INT(entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(entry_status[1], TWLHD_PICA_FRAME_ENTRY_OK);

    CHECK_EQ_INT(textures[0].offset, 0u);
    CHECK_EQ_INT(textures[0].width, 1024u);
    CHECK_EQ_INT(textures[0].height, 8u);
    CHECK_EQ_INT(textures[0].wrap_s, TWLHD_PICA_WRAP_CLAMP_TO_EDGE);
    CHECK_EQ_INT(textures[0].wrap_t, TWLHD_PICA_WRAP_CLAMP_TO_EDGE);
    CHECK_EQ_INT(textures[1].offset, 32768u);
    CHECK_EQ_INT(textures[1].width, 8u);
    CHECK_EQ_INT(textures[1].height, 1024u);

    /* PICA texel 42 is source (0,0) for both shapes; texel 63 is (7,0).
     * The 8x1024 layout puts source row 1023 in tile row 0 (texel 42) and
     * source row 0 in tile row 127 (texel 8170). */
    CHECK_EQ_INT(arena[42u * 4u + 0u], 255u);
    CHECK_EQ_INT(arena[42u * 4u + 1u], 0u);
    CHECK_EQ_INT(arena[42u * 4u + 2u], 0u);
    CHECK_EQ_INT(arena[42u * 4u + 3u], 255u);
    CHECK_EQ_INT(arena[63u * 4u + 3u], 255u);
    CHECK_EQ_INT(arena[32768u + 42u * 4u + 0u], 255u);
    CHECK_EQ_INT(arena[32768u + 42u * 4u + 1u], 0u);
    CHECK_EQ_INT(arena[32768u + 42u * 4u + 2u], 255u);
    CHECK_EQ_INT(arena[32768u + 42u * 4u + 3u], 0u);
    CHECK_EQ_INT(arena[32768u + 8170u * 4u + 2u], 255u);

    test_report("pica-frame/size-bounds-1024");
}

/*
 * Prepare's scratch exactly the decoded size must succeed; one byte less must
 * fail with the documented capacity error and leave every output untouched.
 * The sizing pass is scratch-free, so it reports the same 256 bytes in both
 * cases.
 */
static void test_scratch_exact(void) {
    TwlhdGxTexVertex tex[3];
    TwlhdV2View view;
    uint32_t needed = 0xDEADBEEFu;

    fill_tex_a();
    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&tex[1], 2.0f, 2.0f, PARAM_A, 0u);
    set_vtx(&tex[2], 4.0f, 4.0f, PARAM_A, 0u);
    CHECK_EQ_INT(write_and_parse(g_a_only, 1u, &view), TWLHD_V2_OK);

    /* 8x8 RGBA8 = 256 bytes. */
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 3u, &view, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 256u);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 3u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, 256u, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 1u);
    CHECK_EQ_INT(g_out.texture_count, 1u);
    CHECK_EQ_INT(g_out.arena_used, 256u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
    CHECK_EQ_INT(g_out.textures[0].offset, 0u);
    CHECK_EQ_INT(g_out.textures[0].width, 8u);
    CHECK_EQ_INT(g_out.textures[0].height, 8u);
    expect_a_bytes(&g_out);

    needed = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 3u, &view, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, 256u);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 3u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords,
                     VERTEX_COUNT, g_out.entry_texture, g_out.entry_status,
                     RUN_COUNT, g_out.scratch, 255u, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_ERR_CAPACITY);
    CHECK(outputs_untouched(&g_out));

    test_report("pica-frame/scratch-exact");
}

/*
 * Exactly TWLHD_PICA_FRAME_MAX_TEXTURES distinct textures must all be stored,
 * and the defensive MAX check must still fire for a view with one more record
 * (only reachable hand-built: the parsed format caps TXDS at 256 records).
 */
#define CAP_TEXTURES 256u
#define CAP_TEX_BYTES 128u /* 8x8 16bpp */
#define CAP_EXTRA 257u

static TwlhdV2TexEntry g_cap_entries[CAP_TEXTURES];
static uint8_t g_cap_tex[CAP_TEXTURES][CAP_TEX_BYTES];
static TwlhdGxTexVertex g_cap_vertices[CAP_TEXTURES * 3u];
static TwlhdPicaBindPlan g_cap_plan[CAP_TEXTURES];
static TwlhdPicaFrameTexture g_cap_textures[CAP_TEXTURES];
static TwlhdPicaTexCoord g_cap_texcoords[CAP_TEXTURES * 3u];
static int32_t g_cap_entry_texture[CAP_TEXTURES];
static TwlhdPicaFrameEntryStatus g_cap_entry_status[CAP_TEXTURES];
static uint8_t g_cap_arena[CAP_TEXTURES * 256u];
static uint8_t g_cap_scratch[256];

static uint8_t g_raw_txds[CAP_EXTRA * 40u];
static uint8_t g_raw_txdt[CAP_EXTRA * CAP_TEX_BYTES];
static TwlhdGxTexVertex g_raw_vertices[CAP_EXTRA * 3u];
static TwlhdPicaBindPlan g_raw_plan[CAP_EXTRA];
static TwlhdPicaFrameTexture g_raw_textures[CAP_EXTRA];
static TwlhdPicaTexCoord g_raw_texcoords[CAP_EXTRA * 3u];
static int32_t g_raw_entry_texture[CAP_EXTRA];
static TwlhdPicaFrameEntryStatus g_raw_entry_status[CAP_EXTRA];
static uint8_t g_raw_arena[CAP_EXTRA * 256u];

static void test_texture_cap_256(void) {
    TwlhdV2View view;
    TwlhdV2View raw;
    uint32_t entry_count = 0;
    uint32_t texture_count = 0;
    uint32_t arena_used = 0;
    uint32_t needed = 0xDEADBEEFu;
    uint32_t k;
    uint32_t x;
    uint32_t y;
    uint32_t v;

    memset(g_cap_entries, 0, sizeof(g_cap_entries));
    for (k = 0; k < CAP_TEXTURES; k++) {
        uint32_t i;

        g_cap_entries[k].teximage_param = PARAM_A;
        g_cap_entries[k].pltt_base = k; /* unique (param, pltt) per record */
        g_cap_entries[k].texel = g_cap_tex[k];
        g_cap_entries[k].texel_bytes = CAP_TEX_BYTES;
        for (i = 0; i < CAP_TEX_BYTES / 2u; i++) {
            put_le16(g_cap_tex[k] + i * 2u,
                     (uint16_t)(0x8000u | ((k * 37u + i * 11u) & 0x7FFFu)));
        }
        for (v = 0; v < 3u; v++) {
            set_vtx(&g_cap_vertices[k * 3u + v], 1.0f, 2.0f, PARAM_A, k);
        }
    }
    CHECK_EQ_INT(write_and_parse(g_cap_entries, CAP_TEXTURES, &view),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_txds_count(&view), CAP_TEXTURES);

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(
                     g_cap_vertices, CAP_TEXTURES * 3u, &view, &needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(needed, CAP_TEXTURES * 256u);

    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     g_cap_vertices, CAP_TEXTURES * 3u, &view, g_cap_plan,
                     CAP_TEXTURES, g_cap_textures, CAP_TEXTURES, g_cap_arena,
                     CAP_TEXTURES * 256u, g_cap_texcoords, CAP_TEXTURES * 3u,
                     g_cap_entry_texture, g_cap_entry_status, CAP_TEXTURES,
                     g_cap_scratch, CAP_TEX_BYTES * 2u, &entry_count,
                     &texture_count, &arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(entry_count, CAP_TEXTURES);
    CHECK_EQ_INT(texture_count, CAP_TEXTURES);
    CHECK_EQ_INT(arena_used, CAP_TEXTURES * 256u);
    CHECK_EQ_INT(g_cap_plan[CAP_TEXTURES - 1u].first_vertex,
                 (CAP_TEXTURES - 1u) * 3u);
    CHECK_EQ_INT(g_cap_plan[CAP_TEXTURES - 1u].vertex_count, 3u);

    for (k = 0; k < CAP_TEXTURES; k++) {
        CHECK_EQ_INT(g_cap_textures[k].offset, k * 256u);
        CHECK_EQ_INT(g_cap_textures[k].width, 8u);
        CHECK_EQ_INT(g_cap_textures[k].height, 8u);
        CHECK_EQ_INT(g_cap_entry_texture[k], (int32_t)k);
        CHECK_EQ_INT(g_cap_entry_status[k], TWLHD_PICA_FRAME_ENTRY_OK);
        for (v = 0; v < 3u; v++) {
            CHECK_NEAR(g_cap_texcoords[k * 3u + v].u, 0.125, 1e-6);
            CHECK_NEAR(g_cap_texcoords[k * 3u + v].v, 0.75, 1e-6);
        }
        for (y = 0; y < 8u; y++) {
            for (x = 0; x < 8u; x++) {
                uint32_t n = y * 8u + x;
                uint32_t dest = kMorton8x8[y][x];
                uint16_t c = (uint16_t)(0x8000u |
                                        ((k * 37u + n * 11u) & 0x7FFFu));
                const uint8_t *p = g_cap_arena + k * 256u + dest * 4u;

                CHECK_EQ_INT(p[0], 255u);
                CHECK_EQ_INT(p[1], FIVE_TO_EIGHT[(c >> 10) & 31u]);
                CHECK_EQ_INT(p[2], FIVE_TO_EIGHT[(c >> 5) & 31u]);
                CHECK_EQ_INT(p[3], FIVE_TO_EIGHT[c & 31u]);
            }
        }
    }

    /* MAX+1: a hand-built view with 257 valid records. The parsed format
     * cannot reach this state, so this pins the module's own defensive limit. */
    memset(g_raw_txds, 0, sizeof(g_raw_txds));
    memset(g_raw_txdt, 0x80, sizeof(g_raw_txdt));
    for (k = 0; k < CAP_EXTRA; k++) {
        uint8_t *p = g_raw_txds + (size_t)k * 40u;

        put_le32(p + 0u, PARAM_A);
        put_le32(p + 4u, k);
        put_le32(p + 8u, k * CAP_TEX_BYTES);
        put_le32(p + 12u, CAP_TEX_BYTES);
        for (v = 0; v < 3u; v++) {
            set_vtx(&g_raw_vertices[k * 3u + v], 0.0f, 0.0f, PARAM_A, k);
        }
    }
    memset(&raw, 0, sizeof(raw));
    raw.txds = g_raw_txds;
    raw.txds_count = CAP_EXTRA;
    raw.txdt = g_raw_txdt;
    raw.txdt_size = CAP_EXTRA * CAP_TEX_BYTES;

    needed = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(
                     g_raw_vertices, CAP_EXTRA * 3u, &raw, &needed),
                 TWLHD_PICA_FRAME_ERR_CAPACITY);
    CHECK_EQ_INT(needed, 0xDEADBEEFu);

    entry_count = 0xDEADBEEFu;
    texture_count = 0xDEADBEEFu;
    arena_used = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     g_raw_vertices, CAP_EXTRA * 3u, &raw, g_raw_plan,
                     CAP_EXTRA, g_raw_textures, CAP_EXTRA, g_raw_arena,
                     CAP_EXTRA * 256u, g_raw_texcoords, CAP_EXTRA * 3u,
                     g_raw_entry_texture, g_raw_entry_status, CAP_EXTRA,
                     g_cap_scratch, CAP_TEX_BYTES * 2u, &entry_count,
                     &texture_count, &arena_used),
                 TWLHD_PICA_FRAME_ERR_CAPACITY);

    test_report("pica-frame/texture-cap-256");
}

/* Format 4 and 6 need no index data; format 5 does. The `format == 5u` test
 * must not fire `NO_DATA` for the index-less formats. */

static uint8_t g_fmt4_tex[64];
static uint8_t g_fmt4_pal[512];
static uint8_t g_fmt6_tex[64];
static uint8_t g_fmt6_pal[16];
static uint8_t g_fmt5_tex[16];
static uint8_t g_fmt5_pal[8];
static uint8_t g_fmt5_idx[8];

static void fmt_frame_ok(const TwlhdV2TexEntry *entry, uint32_t param,
                         uint32_t first_alpha, uint32_t first_red) {
    TwlhdGxTexVertex tex[3];
    TwlhdV2View view;

    set_vtx(&tex[0], 0.0f, 0.0f, param, 0u);
    set_vtx(&tex[1], 2.0f, 2.0f, param, 0u);
    set_vtx(&tex[2], 4.0f, 4.0f, param, 0u);
    CHECK_EQ_INT(write_and_parse(entry, 1u, &view), TWLHD_V2_OK);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 3u, &view, g_out.plan, RUN_COUNT, g_out.textures,
                     RUN_COUNT, g_out.arena, ARENA_BYTES, g_out.texcoords, 3u,
                     g_out.entry_texture, g_out.entry_status, RUN_COUNT,
                     g_out.scratch, SCRATCH_BYTES, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 1u);
    CHECK_EQ_INT(g_out.texture_count, 1u);
    CHECK_EQ_INT(g_out.arena_used, 256u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[0], 0);
    CHECK_EQ_INT(g_out.textures[0].width, 8u);
    CHECK_EQ_INT(g_out.textures[0].height, 8u);
    /* Source texel (0,0) -> PICA texel 42 (A,B,G,R). */
    CHECK_EQ_INT(g_out.arena[42u * 4u + 0u], first_alpha);
    CHECK_EQ_INT(g_out.arena[42u * 4u + 1u], 0u);
    CHECK_EQ_INT(g_out.arena[42u * 4u + 2u], 0u);
    CHECK_EQ_INT(g_out.arena[42u * 4u + 3u], first_red);
}

static void test_format_index_matrix(void) {
    static const TwlhdV2TexEntry fmt4 = {
        PARAM_FMT4_8, 0u, g_fmt4_tex, g_fmt4_pal, NULL, 64u, 512u, 0u};
    static const TwlhdV2TexEntry fmt6 = {
        PARAM_FMT6_8, 0u, g_fmt6_tex, g_fmt6_pal, NULL, 64u, 16u, 0u};
    static const TwlhdV2TexEntry fmt5 = {
        PARAM_FMT5_8, 0u, g_fmt5_tex, g_fmt5_pal, g_fmt5_idx, 16u, 8u, 8u};
    uint32_t i;

    for (i = 0; i < 64u; i++) {
        g_fmt4_tex[i] = (uint8_t)i;
        g_fmt6_tex[i] = (uint8_t)(i & 7u);
    }
    for (i = 0; i < 256u; i++) {
        put_le16(g_fmt4_pal + i * 2u, (uint16_t)i);
    }
    for (i = 0; i < 8u; i++) {
        put_le16(g_fmt6_pal + i * 2u, (uint16_t)(i * 3u));
    }
    for (i = 0; i < 4u; i++) {
        put_le16(g_fmt5_pal + i * 2u, (uint16_t)(i * 7u));
    }
    for (i = 0; i < 4u; i++) {
        put_le32(g_fmt5_tex + i * 4u, 0x03020100u);
        put_le16(g_fmt5_idx + i * 2u, 0x4000u); /* mode 1, palette offset 0 */
    }

    /* (a) format 4 has no index data and must be OK. */
    fmt_frame_ok(&fmt4, PARAM_FMT4_8, 255u, 0u);
    /* (b) format 6 has no index data and must be OK. */
    fmt_frame_ok(&fmt6, PARAM_FMT6_8, 0u, 0u);
    /* (c) format 5 with index data must be OK. */
    fmt_frame_ok(&fmt5, PARAM_FMT5_8, 255u, 0u);

    /* (d) format 5 without usable index data: hand-built view, the parser
     * refuses out-of-bounds index offsets so no parsed frame can show this. */
    {
        uint8_t record[40];
        uint8_t txdt[32];
        TwlhdV2View broken;
        TwlhdGxTexVertex tex[3];
        uint32_t needed = 0xDEADBEEFu;

        memset(record, 0, sizeof(record));
        put_le32(record + 0u, PARAM_FMT5_8);
        put_le32(record + 4u, 0u);
        put_le32(record + 8u, 0u);   /* texel_offset */
        put_le32(record + 12u, 16u); /* texel_size */
        put_le32(record + 16u, 0u);  /* palette_offset */
        put_le32(record + 20u, 8u);  /* palette_size */
        put_le32(record + 24u, 100u);/* index_offset: past txdt_size */
        put_le32(record + 28u, 8u);  /* index_size */
        memcpy(txdt, g_fmt5_tex, 16u);
        memset(txdt + 16u, 0, 16u);

        memset(&broken, 0, sizeof(broken));
        broken.txds = record;
        broken.txds_count = 1u;
        broken.txdt = txdt;
        broken.txdt_size = sizeof(txdt);
        broken.pldt = g_fmt5_pal;
        broken.pldt_size = sizeof(g_fmt5_pal);

        set_vtx(&tex[0], 0.0f, 0.0f, PARAM_FMT5_8, 0u);
        set_vtx(&tex[1], 2.0f, 2.0f, PARAM_FMT5_8, 0u);
        set_vtx(&tex[2], 4.0f, 4.0f, PARAM_FMT5_8, 0u);

        CHECK_EQ_INT(twlhd_pica_frame_arena_needed(tex, 3u, &broken, &needed),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(needed, 0u);

        out_reset(&g_out);
        CHECK_EQ_INT(twlhd_pica_frame_prepare(
                         tex, 3u, &broken, g_out.plan, RUN_COUNT,
                         g_out.textures, RUN_COUNT, g_out.arena, ARENA_BYTES,
                         g_out.texcoords, 3u, g_out.entry_texture,
                         g_out.entry_status, RUN_COUNT, g_out.scratch,
                         SCRATCH_BYTES, &g_out.entry_count,
                         &g_out.texture_count, &g_out.arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(g_out.entry_count, 1u);
        CHECK_EQ_INT(g_out.texture_count, 0u);
        CHECK_EQ_INT(g_out.arena_used, 0u);
        CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_NO_DATA);
        CHECK_EQ_INT(g_out.entry_texture[0], -1);
        CHECK_NEAR(g_out.texcoords[0].u, 0.0, 1e-6);
        CHECK_NEAR(g_out.texcoords[0].v, 0.0, 1e-6);
    }

    test_report("pica-frame/format-index-matrix");
}

/*
 * A failed (NO_DATA) run and an untextured run before the first textured run
 * must consume no texture slot: with texture_capacity == the one real texture,
 * a phantom slot makes the sizing pass report ERR_CAPACITY.
 */
static void test_failure_run_before_textured(void) {
    TwlhdGxTexVertex tex[9];
    TwlhdV2View view;

    fill_tex_a();
    set_vtx(&tex[0], 0.0f, 0.0f, PARAM_MISSING, 0u);
    set_vtx(&tex[1], 0.0f, 0.0f, PARAM_MISSING, 0u);
    set_vtx(&tex[2], 0.0f, 0.0f, PARAM_MISSING, 0u);
    set_vtx(&tex[3], 0.0f, 0.0f, 0u, 0u);
    set_vtx(&tex[4], 0.0f, 0.0f, 0u, 0u);
    set_vtx(&tex[5], 0.0f, 0.0f, 0u, 0u);
    set_vtx(&tex[6], 1.0f, 1.0f, PARAM_A, 0u);
    set_vtx(&tex[7], 3.0f, 3.0f, PARAM_A, 0u);
    set_vtx(&tex[8], 5.0f, 5.0f, PARAM_A, 0u);
    CHECK_EQ_INT(write_and_parse(g_a_only, 1u, &view), TWLHD_V2_OK);

    out_reset(&g_out);
    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                     tex, 9u, &view, g_out.plan, RUN_COUNT, g_out.textures, 1u,
                     g_out.arena, ARENA_BYTES, g_out.texcoords, 9u,
                     g_out.entry_texture, g_out.entry_status, RUN_COUNT,
                     g_out.scratch, SCRATCH_BYTES, &g_out.entry_count,
                     &g_out.texture_count, &g_out.arena_used),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(g_out.entry_count, 3u);
    CHECK_EQ_INT(g_out.texture_count, 1u);
    CHECK_EQ_INT(g_out.arena_used, 256u);
    CHECK_EQ_INT(g_out.entry_status[0], TWLHD_PICA_FRAME_ENTRY_NO_DATA);
    CHECK_EQ_INT(g_out.entry_status[1], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_status[2], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_out.entry_texture[0], -1);
    CHECK_EQ_INT(g_out.entry_texture[1], -1);
    CHECK_EQ_INT(g_out.entry_texture[2], 0);
    CHECK_EQ_INT(g_out.textures[0].offset, 0u);
    CHECK_EQ_INT(g_out.textures[0].width, 8u);
    CHECK_EQ_INT(g_out.textures[0].height, 8u);
    CHECK_NEAR(g_out.texcoords[0].u, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[2].v, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[5].u, 0.0, 0.0);
    CHECK_NEAR(g_out.texcoords[6].u, 0.125, 1e-6);
    CHECK_NEAR(g_out.texcoords[6].v, 0.875, 1e-6);
    CHECK_NEAR(g_out.texcoords[8].u, 0.625, 1e-6);
    CHECK_NEAR(g_out.texcoords[8].v, 0.375, 1e-6);
    expect_a_bytes(&g_out);

    test_report("pica-frame/failure-run-before-textured");
}

/*
 * Last run untextured, exact-size guarded texcoords: the zeroing loop must
 * not write texcoords[vertex_count] (one element past the payload).
 */
static void test_texcoord_tail_guard(void) {
    TwlhdGxTexVertex payload[9];
    TwlhdV2View view;
    uint8_t tc_copy[2][sizeof(g_oob_tc)];
    uint32_t count_copy[2][3];
    uint32_t pass;

    fill_tex_a();
    set_vtx(&payload[0], 0.0f, 0.0f, PARAM_A, 0u);
    set_vtx(&payload[1], 2.0f, 2.0f, PARAM_A, 0u);
    set_vtx(&payload[2], 4.0f, 4.0f, PARAM_A, 0u);
    set_vtx(&payload[3], 0.0f, 0.0f, 0u, 0u);
    set_vtx(&payload[4], 1.0f, 1.0f, 0u, 0u);
    set_vtx(&payload[5], 2.0f, 2.0f, 0u, 0u);
    set_vtx(&payload[6], 0.0f, 0.0f, 0u, 1u);
    set_vtx(&payload[7], 1.0f, 1.0f, 0u, 1u);
    set_vtx(&payload[8], 2.0f, 2.0f, 0u, 1u);
    CHECK_EQ_INT(write_and_parse(g_a_only, 1u, &view), TWLHD_V2_OK);

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
                         OOB_TEX, 9u, &view, OOB_PLAN, RUN_COUNT, OOB_TEXS,
                         RUN_COUNT, OOB_ARENA, ARENA_BYTES, OOB_TC, 9u, OOB_ET,
                         OOB_ES, RUN_COUNT, g_out.scratch, SCRATCH_BYTES,
                         &entry_count, &texture_count, &arena_used),
                     TWLHD_PICA_FRAME_OK);
        CHECK_EQ_INT(entry_count, 3u);
        CHECK_EQ_INT(texture_count, 1u);
        CHECK_EQ_INT(arena_used, 256u);
        CHECK_EQ_INT(OOB_ES[2], TWLHD_PICA_FRAME_ENTRY_OK);
        CHECK_EQ_INT(OOB_ET[2], -1);
        CHECK_NEAR(OOB_TC[0].u, 0.0, 1e-6);
        CHECK_NEAR(OOB_TC[1].v, 0.75, 1e-6);
        CHECK_NEAR(OOB_TC[8].v, 0.0, 0.0);

        CHECK(guard_intact(g_oob_tex, 9u * sizeof(TwlhdGxTexVertex), fill));
        CHECK(guard_intact(g_oob_plan, 3u * sizeof(TwlhdPicaBindPlan),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_texs, sizeof(TwlhdPicaFrameTexture),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_tc, 9u * sizeof(TwlhdPicaTexCoord), OOB_FILL));
        CHECK(guard_intact(g_oob_et, 3u * sizeof(int32_t), OOB_FILL));
        CHECK(guard_intact(g_oob_es, 3u * sizeof(TwlhdPicaFrameEntryStatus),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_arena, 256u, OOB_FILL));

        memcpy(tc_copy[pass], g_oob_tc, sizeof(g_oob_tc));
        count_copy[pass][0] = entry_count;
        count_copy[pass][1] = texture_count;
        count_copy[pass][2] = arena_used;
    }

    CHECK(memcmp(tc_copy[0], tc_copy[1], sizeof(g_oob_tc)) == 0);
    CHECK(memcmp(count_copy[0], count_copy[1], sizeof(count_copy[0])) == 0);

    test_report("pica-frame/texcoord-tail-guard");
}

int main(void) {
    test_prepare_basic();
    test_merged_run_indexing();
    test_mixed_vertex_runs();
    test_dedupe_key_halves();
    test_errors();
    test_missing_and_unsupported();
    test_arena_needed_no_writes();
    test_arena_needed_status();
    test_fmt5_palette_window();
    test_out_of_bounds();
    test_zero_vertex_guards();
    test_size_bounds_1024();
    test_scratch_exact();
    test_texture_cap_256();
    test_format_index_matrix();
    test_failure_run_before_textured();
    test_texcoord_tail_guard();
    return test_exit_code();
}
