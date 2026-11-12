// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_pica_tex.c (stage 5B-2d).
 *
 * The 8x8 destination-index table is hand-written from the PICA Morton layout
 * documented in Azahar video_core/utils.h:12-16/29-38, not produced by the
 * code under test. Pixel values are chosen so every source byte (R,G,B,A) is
 * recoverable from the swizzled output, which pins the byte order and the
 * vertical convention.
 *
 * BUILD/UNIT only; no GPU or hardware is involved.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_pica_tex.h"

/* 1024x8 is the widest small texture used here (32 KiB per image). */
#define MAX_IMAGE_BYTES (1024u * 8u * 4u)
#define OOB_GUARD 64u
#define OOB_IMAGE_BYTES 512u /* 16x8 RGBA8 */
#define OOB_TEXCOORD_MAX 32u
#define OOB_FILL 0x5Bu

static uint8_t g_src[MAX_IMAGE_BYTES];
static uint8_t g_swizzled[MAX_IMAGE_BYTES];
static uint8_t g_roundtrip[MAX_IMAGE_BYTES];

static uint8_t g_oob_src[OOB_GUARD + OOB_IMAGE_BYTES + OOB_GUARD];
static uint8_t g_oob_dst[OOB_GUARD + OOB_IMAGE_BYTES + OOB_GUARD];
static TwlhdPicaTexCoord
    g_oob_coord[OOB_GUARD + OOB_TEXCOORD_MAX * sizeof(TwlhdPicaTexCoord) +
                OOB_GUARD];
static TwlhdGxTexVertex g_oob_tex[OOB_TEXCOORD_MAX + 2];

#define OOB_SRC (g_oob_src + OOB_GUARD)
#define OOB_DST (g_oob_dst + OOB_GUARD)
#define OOB_COORD \
    ((TwlhdPicaTexCoord *)(g_oob_coord + OOB_GUARD))

static int guard_intact(const uint8_t *base, size_t used, int fill) {
    size_t i;
    for (i = 0; i < OOB_GUARD; i++) {
        if (base[i] != (uint8_t)fill) return 0;
        if (base[OOB_GUARD + used + i] != (uint8_t)fill) return 0;
    }
    return 1;
}

/*
 * Destination texel index inside one 8x8 tile for source row y and column x.
 * Hand-written from the Z-order table in Azahar utils.h:29-38: the top source
 * row lands on the 42..63 indexes because PICA tile rows are stored bottom-up.
 */
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

static uint32_t lcg_state;

static uint8_t lcg_byte(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (uint8_t)(lcg_state >> 24);
}

static void fill_random(uint8_t *buf, uint32_t bytes, uint32_t seed) {
    uint32_t i;
    lcg_state = seed;
    for (i = 0; i < bytes; i++) buf[i] = lcg_byte();
}

/* Pixel n in the 8x8 image: R=n, G=128+n, B=64+n, A=192+n. */
static void fill_identity_8x8(uint8_t *buf) {
    uint32_t n;
    for (n = 0; n < 64u; n++) {
        buf[n * 4u + 0u] = (uint8_t)n;
        buf[n * 4u + 1u] = (uint8_t)(128u + n);
        buf[n * 4u + 2u] = (uint8_t)(64u + n);
        buf[n * 4u + 3u] = (uint8_t)(192u + n);
    }
}

static void test_swizzle_8x8_literals(void) {
    uint32_t x, y;

    memset(g_swizzled, 0x5B, sizeof(g_swizzled));
    fill_identity_8x8(g_src);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 8u, g_swizzled,
                                              MAX_IMAGE_BYTES),
                 TWLHD_PICA_TEX_OK);

    /* Explicit literal corner checks: index and all four bytes. (0,0). */
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 192);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 1u], 64);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 2u], 128);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 3u], 0);
    /* (7,0) -> texel 63. */
    CHECK_EQ_INT(g_swizzled[63u * 4u + 0u], 199);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 1u], 71);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 2u], 135);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 3u], 7);
    /* (0,7) -> texel 0. */
    CHECK_EQ_INT(g_swizzled[0u], 248);
    CHECK_EQ_INT(g_swizzled[1u], 120);
    CHECK_EQ_INT(g_swizzled[2u], 184);
    CHECK_EQ_INT(g_swizzled[3u], 56);
    /* (7,7) -> texel 21. */
    CHECK_EQ_INT(g_swizzled[21u * 4u + 0u], 255);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 1u], 127);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 2u], 191);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 3u], 63);

    /* First 16 pixels (top two rows): exact destination texel indexes. */
    for (y = 0; y < 2u; y++) {
        for (x = 0; x < 8u; x++) {
            uint32_t n = y * 8u + x;
            uint32_t dest = kMorton8x8[y][x];
            CHECK_EQ_INT(g_swizzled[dest * 4u + 0u], 192u + n);
            CHECK_EQ_INT(g_swizzled[dest * 4u + 1u], 64u + n);
            CHECK_EQ_INT(g_swizzled[dest * 4u + 2u], 128u + n);
            CHECK_EQ_INT(g_swizzled[dest * 4u + 3u], n);
        }
    }
    /* And every one of the 64 pixels through the same hand-written table. */
    for (y = 0; y < 8u; y++) {
        for (x = 0; x < 8u; x++) {
            uint32_t n = y * 8u + x;
            uint32_t dest = kMorton8x8[y][x];
            CHECK_EQ_INT(g_swizzled[dest * 4u + 0u], 192u + n);
            CHECK_EQ_INT(g_swizzled[dest * 4u + 3u], n);
        }
    }

    /* Byte order alone, distinct channels: R=0x11 G=0x22 B=0x33 A=0x44. */
    memset(g_src, 0, sizeof(g_src));
    for (y = 0; y < 64u; y++) {
        g_src[y * 4u + 0u] = 0x11u;
        g_src[y * 4u + 1u] = 0x22u;
        g_src[y * 4u + 2u] = 0x33u;
        g_src[y * 4u + 3u] = 0x44u;
    }
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 8u, g_swizzled, 256u),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(g_swizzled[0u], 0x44);
    CHECK_EQ_INT(g_swizzled[1u], 0x33);
    CHECK_EQ_INT(g_swizzled[2u], 0x22);
    CHECK_EQ_INT(g_swizzled[3u], 0x11);
    CHECK_EQ_INT(g_swizzled[255u], 0x11);
    test_report("pica-tex/swizzle-8x8-literals");
}

static void test_swizzle_nonsquare(void) {
    /* 16x8: two tiles per tile row; (8,0) is tile 1 index 42 -> 106. */
    memset(g_src, 0x5A, sizeof(g_src));
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 16u, 8u, g_swizzled, 512u),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[106u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[127u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[0u], 0x5A);

    /* 8x16: two tile rows; source (0,0) is in tile row 1 at index 64+42. */
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 16u, g_swizzled, 512u),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(g_swizzled[106u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[0u], 0x5A);
    test_report("pica-tex/swizzle-nonsquare");
}

static void roundtrip(uint32_t width, uint32_t height, uint32_t seed,
                      const char *name) {
    uint32_t bytes = width * height * 4u;
    fill_random(g_src, bytes, seed);
    memset(g_swizzled, 0, sizeof(g_swizzled));
    memset(g_roundtrip, 0, sizeof(g_roundtrip));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, width, height, g_swizzled,
                                              bytes),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(g_swizzled, width, height,
                                                g_roundtrip, bytes),
                 TWLHD_PICA_TEX_OK);
    CHECK(memcmp(g_src, g_roundtrip, bytes) == 0);

    /* Not a copy: at least one byte must have moved for these sizes. */
    if (width >= 16u || height >= 16u)
        CHECK(memcmp(g_src, g_swizzled, bytes) != 0);
    test_report(name);
}

static void test_errors(void) {
    const uint8_t *src = g_src;
    uint8_t *dst = g_swizzled;
    uint32_t save;

    memset(g_src, 0x5B, sizeof(g_src));
    memset(g_swizzled, 0x5B, sizeof(g_swizzled));
    save = g_swizzled[0];

    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(NULL, 8u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 8u, NULL, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 4u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 12u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 2048u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 0u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 8u, dst, 255u),
                 TWLHD_PICA_TEX_ERR_OUTPUT_SIZE);

    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(NULL, 8u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 8u, NULL, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 8u, dst, 255u),
                 TWLHD_PICA_TEX_ERR_OUTPUT_SIZE);

    /* Every rejected call leaves the destination byte untouched. */
    CHECK_EQ_INT(g_swizzled[0], save);

    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_OK), "OK") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_ERR_NULL),
                 "ERR_NULL") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_ERR_SIZE),
                 "ERR_SIZE") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_ERR_OUTPUT_SIZE),
                 "ERR_OUTPUT_SIZE") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_ERR_COUNT),
                 "ERR_COUNT") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string(TWLHD_PICA_TEX_ERR_CAPACITY),
                 "ERR_CAPACITY") == 0);
    CHECK(strcmp(twlhd_pica_tex_result_string((TwlhdPicaTexResult)99),
                 "ERR_UNKNOWN") == 0);
    test_report("pica-tex/errors");
}

static void test_wrap_all_16(void) {
    uint32_t bits;
    for (bits = 0; bits < 16u; bits++) {
        uint32_t param = bits << 16;
        uint32_t repeat_s = bits & 1u;
        uint32_t repeat_t = (bits >> 1) & 1u;
        uint32_t flip_s = (bits >> 2) & 1u;
        uint32_t flip_t = (bits >> 3) & 1u;
        TwlhdPicaWrap s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
        TwlhdPicaWrap t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
        TwlhdPicaWrap want_s;
        TwlhdPicaWrap want_t;

        want_s = repeat_s && flip_s
                     ? TWLHD_PICA_WRAP_MIRRORED_REPEAT
                     : (repeat_s ? TWLHD_PICA_WRAP_REPEAT
                                 : TWLHD_PICA_WRAP_CLAMP_TO_EDGE);
        want_t = repeat_t && flip_t
                     ? TWLHD_PICA_WRAP_MIRRORED_REPEAT
                     : (repeat_t ? TWLHD_PICA_WRAP_REPEAT
                                 : TWLHD_PICA_WRAP_CLAMP_TO_EDGE);

        CHECK_EQ_INT(twlhd_pica_wrap_from_teximage(param, &s, &t),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(s, want_s);
        CHECK_EQ_INT(t, want_t);
    }
    /* Numeric register values from Azahar regs_texturing.h:27-31. */
    CHECK_EQ_INT(TWLHD_PICA_WRAP_CLAMP_TO_EDGE, 0);
    CHECK_EQ_INT(TWLHD_PICA_WRAP_CLAMP_TO_BORDER, 1);
    CHECK_EQ_INT(TWLHD_PICA_WRAP_REPEAT, 2);
    CHECK_EQ_INT(TWLHD_PICA_WRAP_MIRRORED_REPEAT, 3);

    {
        TwlhdPicaWrap s, t;
        s = TWLHD_PICA_WRAP_REPEAT;
        t = TWLHD_PICA_WRAP_REPEAT;
        CHECK_EQ_INT(twlhd_pica_wrap_from_teximage(0u, NULL, &t),
                     TWLHD_PICA_TEX_ERR_NULL);
        CHECK_EQ_INT(twlhd_pica_wrap_from_teximage(0u, &s, NULL),
                     TWLHD_PICA_TEX_ERR_NULL);
        CHECK_EQ_INT(s, TWLHD_PICA_WRAP_REPEAT);
        CHECK_EQ_INT(t, TWLHD_PICA_WRAP_REPEAT);
    }
    test_report("pica-tex/wrap-16-combos");
}

static void test_texcoords(void) {
    TwlhdGxTexVertex tex[4];
    TwlhdPicaTexCoord out[4];
    uint32_t i;

    CHECK_EQ_INT(sizeof(TwlhdPicaTexCoord), 8u);
    CHECK_EQ_INT(offsetof(TwlhdPicaTexCoord, u), 0u);
    CHECK_EQ_INT(offsetof(TwlhdPicaTexCoord, v), 4u);

    memset(tex, 0, sizeof(tex));
    tex[0].s = 0.0f;
    tex[0].t = 0.0f;
    tex[1].s = 4.0f;
    tex[1].t = 4.0f;
    tex[2].s = 8.0f;
    tex[2].t = 8.0f;
    tex[3].s = 1.0f;
    tex[3].t = 6.0f;

    CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 4u, 8u, 8u, out, 4u),
                 TWLHD_PICA_TEX_OK);
    /* Hand-computed: u=s/8, v=1-t/8. */
    CHECK_NEAR(out[0].u, 0.0, 1e-6);
    CHECK_NEAR(out[0].v, 1.0, 1e-6);
    CHECK_NEAR(out[1].u, 0.5, 1e-6);
    CHECK_NEAR(out[1].v, 0.5, 1e-6);
    CHECK_NEAR(out[2].u, 1.0, 1e-6);
    CHECK_NEAR(out[2].v, 0.0, 1e-6);
    CHECK_NEAR(out[3].u, 0.125, 1e-6);
    CHECK_NEAR(out[3].v, 0.25, 1e-6);

    /* Non-square: 16x8 then 8x16. */
    tex[0].s = 8.0f;
    tex[0].t = 2.0f;
    CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 16u, 8u, out, 4u),
                 TWLHD_PICA_TEX_OK);
    CHECK_NEAR(out[0].u, 0.5, 1e-6);
    CHECK_NEAR(out[0].v, 0.75, 1e-6);

    tex[0].s = 2.0f;
    tex[0].t = 12.0f;
    CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 8u, 16u, out, 4u),
                 TWLHD_PICA_TEX_OK);
    CHECK_NEAR(out[0].u, 0.25, 1e-6);
    CHECK_NEAR(out[0].v, 0.25, 1e-6);

    /* The input array is never modified. */
    {
        TwlhdGxTexVertex before[4];
        memcpy(before, tex, sizeof(before));
        out[0].u = -1.0f;
        out[0].v = -1.0f;
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 4u, 8u, 8u, out, 4u),
                     TWLHD_PICA_TEX_OK);
        CHECK(memcmp(before, tex, sizeof(before)) == 0);
    }

    /* Errors: NULL, divisor, capacity. */
    {
        TwlhdPicaTexCoord guard[4];
        TwlhdPicaTexCoord *p = guard;
        for (i = 0; i < 4u; i++) {
            guard[i].u = 0x5Bu;
            guard[i].v = 0x5Bu;
        }
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(NULL, 1u, 8u, 8u, p, 4u),
                     TWLHD_PICA_TEX_ERR_NULL);
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 8u, 8u, NULL, 4u),
                     TWLHD_PICA_TEX_ERR_NULL);
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 0u, 8u, p, 4u),
                     TWLHD_PICA_TEX_ERR_SIZE);
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 12u, 8u, p, 4u),
                     TWLHD_PICA_TEX_ERR_SIZE);
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 4u, 8u, 8u, p, 3u),
                     TWLHD_PICA_TEX_ERR_CAPACITY);
        CHECK_NEAR(guard[0].u, 0x5Bu, 0.0);
    }
    test_report("pica-tex/texcoords");
}

static void set_tri(TwlhdGxTexVertex *tex, uint32_t tri, uint32_t param,
                    uint32_t pltt) {
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        tex[tri * 3u + i].s = 0.0f;
        tex[tri * 3u + i].t = 0.0f;
        tex[tri * 3u + i].teximage_param = param;
        tex[tri * 3u + i].pltt_base = pltt;
    }
}

static void test_bind_plan(void) {
    TwlhdGxTexVertex tex[9];
    TwlhdPicaBindPlan plan[4];
    uint32_t count = 0xDEADBEEFu;

    /* Two runs: triangles 0-1 share (A,X); triangle 2 is (B,X). */
    set_tri(tex, 0, 0x04000000u, 1u); /* PackedFormat 1 */
    set_tri(tex, 1, 0x04000000u, 1u);
    set_tri(tex, 2, 0x08000000u, 1u); /* PackedFormat 2 */
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 9u, plan, 4u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 2u);
    CHECK_EQ_INT(plan[0].first_vertex, 0u);
    CHECK_EQ_INT(plan[0].vertex_count, 6u);
    CHECK_EQ_INT(plan[0].teximage_param, 0x04000000u);
    CHECK_EQ_INT(plan[0].pltt_base, 1u);
    CHECK_EQ_INT(plan[0].untextured, 0);
    CHECK_EQ_INT(plan[1].first_vertex, 6u);
    CHECK_EQ_INT(plan[1].vertex_count, 3u);
    CHECK_EQ_INT(plan[1].teximage_param, 0x08000000u);
    CHECK_EQ_INT(plan[1].untextured, 0);

    /* PLTT_BASE change splits the run. */
    set_tri(tex, 0, 0x04000000u, 1u);
    set_tri(tex, 1, 0x04000000u, 2u);
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 6u, plan, 4u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 2u);
    CHECK_EQ_INT(plan[0].first_vertex, 0u);
    CHECK_EQ_INT(plan[0].vertex_count, 3u);
    CHECK_EQ_INT(plan[1].first_vertex, 3u);
    CHECK_EQ_INT(plan[1].pltt_base, 2u);

    /* PackedFormat 0 (including param 0) is untextured. */
    set_tri(tex, 0, 0u, 0u);
    set_tri(tex, 1, 0u, 0u);
    set_tri(tex, 2, 0x04000000u, 0u);
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 9u, plan, 4u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 2u);
    CHECK_EQ_INT(plan[0].first_vertex, 0u);
    CHECK_EQ_INT(plan[0].vertex_count, 6u);
    CHECK_EQ_INT(plan[0].untextured, 1);
    CHECK_EQ_INT(plan[1].untextured, 0);

    /* A non-zero TEXIMAGE_PARAM with PackedFormat 0 is untextured too. */
    set_tri(tex, 0, 0x00010000u, 0u); /* repeat S only, format 0 */
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 3u, plan, 4u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 1u);
    CHECK_EQ_INT(plan[0].untextured, 1);

    /* Bit 25 (part of SizeShiftT) must not be mistaken for PackedFormat. */
    set_tri(tex, 0, 0x02000000u, 0u); /* SizeShiftT = 4, format 0 */
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 3u, plan, 4u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 1u);
    CHECK_EQ_INT(plan[0].untextured, 1);

    /* capacity, count % 3, NULL, and zero vertices. */
    set_tri(tex, 0, 0x04000000u, 0u);
    set_tri(tex, 1, 0x08000000u, 0u);
    count = 0xDEADBEEFu;
    memset(plan, 0x5B, sizeof(plan));
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 6u, plan, 1u, &count),
                 TWLHD_PICA_TEX_ERR_CAPACITY);
    CHECK_EQ_INT(count, 0xDEADBEEFu);
    CHECK_EQ_INT(((const uint8_t *)plan)[0], 0x5Bu);

    /* Exactly fitting capacity is accepted. */
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 6u, plan, 2u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 2u);

    /* A one-entry plan buffer must be rejected before it is overrun. Both
     * splits are exercised: same param/different palette, and different
     * param/same palette (the two halves of the run key). */
    {
        TwlhdPicaBindPlan one[1];
        set_tri(tex, 0, 0x04000000u, 1u);
        set_tri(tex, 1, 0x04000000u, 2u);
        count = 0u;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 6u, one, 1u, &count),
                     TWLHD_PICA_TEX_ERR_CAPACITY);
        CHECK_EQ_INT(count, 0u);

        set_tri(tex, 0, 0x04000000u, 1u);
        set_tri(tex, 1, 0x08000000u, 1u);
        count = 0u;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 6u, one, 1u, &count),
                     TWLHD_PICA_TEX_ERR_CAPACITY);
        CHECK_EQ_INT(count, 0u);
    }

    count = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 4u, plan, 4u, &count),
                 TWLHD_PICA_TEX_ERR_COUNT);
    CHECK_EQ_INT(count, 0xDEADBEEFu);

    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(NULL, 3u, plan, 4u, &count),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 3u, NULL, 4u, &count),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 3u, plan, 4u, NULL),
                 TWLHD_PICA_TEX_ERR_NULL);

    count = 0xDEADBEEFu;
    CHECK_EQ_INT(twlhd_pica_tex_bind_plan(tex, 0u, plan, 0u, &count),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(count, 0u);
    test_report("pica-tex/bind-plan");
}

static void test_swizzle_out_of_bounds(void) {
    uint32_t pass;

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        memset(g_oob_src, fill, sizeof(g_oob_src));
        memset(g_oob_dst, OOB_FILL, sizeof(g_oob_dst));
        fill_random(OOB_SRC, OOB_IMAGE_BYTES, 1234u + pass);

        CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(OOB_SRC, 16u, 8u, OOB_DST,
                                                  OOB_IMAGE_BYTES),
                     TWLHD_PICA_TEX_OK);
        CHECK(guard_intact(g_oob_src, OOB_IMAGE_BYTES, fill));
        CHECK(guard_intact(g_oob_dst, OOB_IMAGE_BYTES, OOB_FILL));

        CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(OOB_SRC, 16u, 8u, OOB_DST,
                                                    OOB_IMAGE_BYTES),
                     TWLHD_PICA_TEX_OK);
        CHECK(guard_intact(g_oob_src, OOB_IMAGE_BYTES, fill));
        CHECK(guard_intact(g_oob_dst, OOB_IMAGE_BYTES, OOB_FILL));
    }
    test_report("pica-tex/oob-swizzle");
}

static void test_texcoord_out_of_bounds(void) {
    uint32_t pass;
    uint32_t i;

    for (i = 0; i < OOB_TEXCOORD_MAX + 2u; i++) {
        g_oob_tex[i].s = (float)i;
        g_oob_tex[i].t = (float)(i * 2u);
        g_oob_tex[i].teximage_param = 0u;
        g_oob_tex[i].pltt_base = 0u;
    }

    for (pass = 0; pass < 2u; pass++) {
        size_t bytes = sizeof(g_oob_tex);
        uint8_t snapshot[sizeof(g_oob_tex)];

        memcpy(snapshot, g_oob_tex, bytes);
        memset(g_oob_coord, OOB_FILL, sizeof(g_oob_coord));

        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(g_oob_tex, OOB_TEXCOORD_MAX,
                                                  64u, 32u, OOB_COORD,
                                                  OOB_TEXCOORD_MAX),
                     TWLHD_PICA_TEX_OK);
        {
            const uint8_t *cb = (const uint8_t *)g_oob_coord;
            CHECK_EQ_INT(cb[OOB_GUARD - 1u], OOB_FILL);
            for (i = 0; i < OOB_GUARD; i++) {
                CHECK_EQ_INT(
                    cb[OOB_GUARD + OOB_TEXCOORD_MAX * sizeof(TwlhdPicaTexCoord) + i],
                    OOB_FILL);
            }
        }
        CHECK(memcmp(snapshot, g_oob_tex, bytes) == 0);
    }
    test_report("pica-tex/oob-texcoord");
}

int main(void) {
    test_swizzle_8x8_literals();
    test_swizzle_nonsquare();
    roundtrip(8u, 8u, 1u, "pica-tex/roundtrip-8x8");
    roundtrip(16u, 8u, 2u, "pica-tex/roundtrip-16x8");
    roundtrip(8u, 16u, 3u, "pica-tex/roundtrip-8x16");
    roundtrip(64u, 32u, 4u, "pica-tex/roundtrip-64x32");
    roundtrip(1024u, 8u, 5u, "pica-tex/roundtrip-1024x8");
    test_errors();
    test_wrap_all_16();
    test_texcoords();
    test_bind_plan();
    test_swizzle_out_of_bounds();
    test_texcoord_out_of_bounds();
    return test_exit_code();
}
