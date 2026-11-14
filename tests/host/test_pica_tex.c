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
/*
 * Byte-exact OOB regions: [OOB_GUARD bytes][exact payload][OOB_GUARD bytes].
 * The payload is accessed as a struct array, so both regions are 8-byte
 * aligned; OOB_GUARD (64) keeps the payload offset aligned too.
 */
#define OOB_COORD_BYTES (OOB_TEXCOORD_MAX * sizeof(TwlhdPicaTexCoord))
#define OOB_TEX_BYTES (OOB_TEXCOORD_MAX * sizeof(TwlhdGxTexVertex))
static _Alignas(8)
    uint8_t g_oob_coord[OOB_GUARD + OOB_COORD_BYTES + OOB_GUARD];
static _Alignas(8) uint8_t g_oob_tex[OOB_GUARD + OOB_TEX_BYTES + OOB_GUARD];

#define OOB_SRC (g_oob_src + OOB_GUARD)
#define OOB_DST (g_oob_dst + OOB_GUARD)
#define OOB_COORD ((TwlhdPicaTexCoord *)(g_oob_coord + OOB_GUARD))
#define OOB_TEX ((const TwlhdGxTexVertex *)(g_oob_tex + OOB_GUARD))

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
 * Hand-written from the Z-order table in Azahar utils.h:12-16: Morton X offset
 * = {0,1,4,5,16,17,20,21}, Y offset = {0,2,8,10,32,34,40,42}; texel index =
 * X[x&7] + Y[y&7]. Memory tile rows are image rows top-down, so source row 0
 * lands on Morton indexes 0..21 and source row 7 on 42..63.
 */
static const uint32_t kMorton8x8[8][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21},
    {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29},
    {10, 11, 14, 15, 26, 27, 30, 31},
    {32, 33, 36, 37, 48, 49, 52, 53},
    {34, 35, 38, 39, 50, 51, 54, 55},
    {40, 41, 44, 45, 56, 57, 60, 61},
    {42, 43, 46, 47, 58, 59, 62, 63},
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
    CHECK_EQ_INT(g_swizzled[0u], 192);
    CHECK_EQ_INT(g_swizzled[1u], 64);
    CHECK_EQ_INT(g_swizzled[2u], 128);
    CHECK_EQ_INT(g_swizzled[3u], 0);
    /* (7,0) -> texel 21. */
    CHECK_EQ_INT(g_swizzled[21u * 4u + 0u], 199);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 1u], 71);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 2u], 135);
    CHECK_EQ_INT(g_swizzled[21u * 4u + 3u], 7);
    /* (0,7) -> texel 42. */
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 248);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 1u], 120);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 2u], 184);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 3u], 56);
    /* (7,7) -> texel 63. */
    CHECK_EQ_INT(g_swizzled[63u * 4u + 0u], 255);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 1u], 127);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 2u], 191);
    CHECK_EQ_INT(g_swizzled[63u * 4u + 3u], 63);

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
    /* 16x8: two tiles per tile row; (0,0)->tile0 idx0, (8,0)->tile1 idx0,
     * (0,7)->tile0 idx42, (15,7)->tile1 idx63 (global 127). */
    memset(g_src, 0x5A, sizeof(g_src));
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 16u, 8u, g_swizzled, 512u),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(g_swizzled[0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[64u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[127u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 0x5A);

    /* 8x16: two tile rows; (0,0)->idx0, (0,7)->idx42, (0,8)->tile1 idx0
     * (global 64), (0,15)->tile1 idx42 (global 106). */
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 16u, g_swizzled, 512u),
                 TWLHD_PICA_TEX_OK);
    CHECK_EQ_INT(g_swizzled[0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[42u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[64u * 4u + 0u], 0x5A);
    CHECK_EQ_INT(g_swizzled[106u * 4u + 0u], 0x5A);
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
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 0u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    /* Valid width, invalid height and the reverse kill the `||` -> `&&`
     * size-check mutants at twlhd_pica_tex.c:39. */
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 12u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 2048u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(src, 8u, 8u, dst, 255u),
                 TWLHD_PICA_TEX_ERR_OUTPUT_SIZE);

    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(NULL, 8u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 8u, NULL, 256u),
                 TWLHD_PICA_TEX_ERR_NULL);
    /* The unswizzle size validation (twlhd_pica_tex.c:73) was untested. */
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 4u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 12u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 0u, 8u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 0u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 12u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
    CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(src, 8u, 2048u, dst, 256u),
                 TWLHD_PICA_TEX_ERR_SIZE);
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
        /* Valid width with invalid height and the reverse (the `||` -> `&&`
         * mutant at twlhd_pica_tex.c:133 must not skip validation). */
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 8u, 12u, p, 4u),
                     TWLHD_PICA_TEX_ERR_SIZE);
        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 8u, 0u, p, 4u),
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

/* Per-vertex override: the run key is documented as vertex 0 only. */
static void set_vertex(TwlhdGxTexVertex *tex, uint32_t v, uint32_t param,
                       uint32_t pltt) {
    tex[v].s = 0.0f;
    tex[v].t = 0.0f;
    tex[v].teximage_param = param;
    tex[v].pltt_base = pltt;
}

/* Five triangles: A A A B B, so the second run starts at vertex 9. */
static void set_aaabb(TwlhdGxTexVertex *tex) {
    uint32_t t;
    for (t = 0; t < 5u; t++)
        set_tri(tex, t, t < 3u ? 0x04000000u : 0x08000000u, 1u);
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
    {
        /* The whole caller buffer, not just the first byte, stays untouched. */
        uint8_t want[sizeof(plan)];
        memset(want, 0x5B, sizeof(want));
        CHECK(memcmp(plan, want, sizeof(plan)) == 0);
    }

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

    /* Only vertex 0 of each triangle is the run key (documented in the
     * header). Vertices 1 and 2 carry different state, so any key read at
     * tex[i-2u] (vertex 1) changes the run boundaries. */
    {
        TwlhdGxTexVertex weird[6];

        /* Merge direction: triangle 1's vertex 0 repeats triangle 0's vertex
         * 0. Correct is one run; tex[i-2u] compares triangle 0's vertex 1 and
         * splits. */
        set_vertex(weird, 0, 0x04000000u, 1u);
        set_vertex(weird, 1, 0x08000000u, 2u);
        set_vertex(weird, 2, 0x10000000u, 3u);
        set_vertex(weird, 3, 0x04000000u, 1u);
        set_vertex(weird, 4, 0x08000000u, 2u);
        set_vertex(weird, 5, 0x10000000u, 3u);

        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(weird, 6u, plan, 4u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 1u);
        CHECK_EQ_INT(plan[0].first_vertex, 0u);
        CHECK_EQ_INT(plan[0].vertex_count, 6u);
        CHECK_EQ_INT(plan[0].teximage_param, 0x04000000u);
        CHECK_EQ_INT(plan[0].pltt_base, 1u);

        /* Capacity 1: the correct single run fits; a spurious second run
         * from the mutant is rejected before any write. */
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(weird, 6u, plan, 1u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 1u);

        /* Split direction: triangle 1's vertex 0 equals triangle 0's vertex
         * 1, so tex[i-2u] merges runs that must split. */
        set_vertex(weird, 3, 0x08000000u, 2u);
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(weird, 6u, plan, 4u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 2u);
        CHECK_EQ_INT(plan[0].first_vertex, 0u);
        CHECK_EQ_INT(plan[0].vertex_count, 3u);
        CHECK_EQ_INT(plan[1].first_vertex, 3u);
        CHECK_EQ_INT(plan[1].vertex_count, 3u);
        CHECK_EQ_INT(plan[1].teximage_param, 0x08000000u);
        CHECK_EQ_INT(plan[1].pltt_base, 2u);

        /* A capacity of 1 must reject the two runs; an undercounted run count
         * from the counting-loop mutant would pass the gate and then write
         * past it. */
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(weird, 6u, plan, 1u, &count),
                     TWLHD_PICA_TEX_ERR_CAPACITY);
        CHECK_EQ_INT(count, 0xDEADBEEFu);
    }

    /* Runs of three or more triangles with a non-zero first_vertex: A A A B B.
     * The second run starts at vertex 9, which the i += 4u write-loop mutant
     * gets wrong. */
    {
        TwlhdGxTexVertex big[15];
        set_aaabb(big);
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(big, 15u, plan, 4u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 2u);
        CHECK_EQ_INT(plan[0].first_vertex, 0u);
        CHECK_EQ_INT(plan[0].vertex_count, 9u);
        CHECK_EQ_INT(plan[0].teximage_param, 0x04000000u);
        CHECK_EQ_INT(plan[0].pltt_base, 1u);
        CHECK_EQ_INT(plan[1].first_vertex, 9u);
        CHECK_EQ_INT(plan[1].vertex_count, 6u);
        CHECK_EQ_INT(plan[1].teximage_param, 0x08000000u);

        /* Capacity 1: the counting-loop i += 4u mutant sees one run and
         * passes the gate, then either overruns this 1-entry buffer or
         * returns OK where ERR_CAPACITY is required. */
        {
            TwlhdPicaBindPlan one[1];
            uint8_t want[sizeof(one)];
            memset(one, 0x5B, sizeof(one));
            memset(want, 0x5B, sizeof(want));
            count = 0xDEADBEEFu;
            CHECK_EQ_INT(twlhd_pica_tex_bind_plan(big, 15u, one, 1u, &count),
                         TWLHD_PICA_TEX_ERR_CAPACITY);
            CHECK_EQ_INT(count, 0xDEADBEEFu);
            CHECK(memcmp(one, want, sizeof(one)) == 0);
        }
    }

    /* Change of ONLY pltt_base, then ONLY teximage_param, at the boundary of
     * the five-triangle layout (both halves of the run key). */
    {
        TwlhdGxTexVertex big[15];
        uint32_t t;

        for (t = 0; t < 5u; t++)
            set_tri(big, t, 0x04000000u, t < 3u ? 1u : 4u);
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(big, 15u, plan, 4u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 2u);
        CHECK_EQ_INT(plan[0].first_vertex, 0u);
        CHECK_EQ_INT(plan[0].vertex_count, 9u);
        CHECK_EQ_INT(plan[0].teximage_param, 0x04000000u);
        CHECK_EQ_INT(plan[0].pltt_base, 1u);
        CHECK_EQ_INT(plan[1].first_vertex, 9u);
        CHECK_EQ_INT(plan[1].vertex_count, 6u);
        CHECK_EQ_INT(plan[1].teximage_param, 0x04000000u);
        CHECK_EQ_INT(plan[1].pltt_base, 4u);

        for (t = 0; t < 5u; t++)
            set_tri(big, t, t < 3u ? 0x04000000u : 0x0C000000u, 1u);
        count = 0xDEADBEEFu;
        CHECK_EQ_INT(twlhd_pica_tex_bind_plan(big, 15u, plan, 4u, &count),
                     TWLHD_PICA_TEX_OK);
        CHECK_EQ_INT(count, 2u);
        CHECK_EQ_INT(plan[0].first_vertex, 0u);
        CHECK_EQ_INT(plan[0].vertex_count, 9u);
        CHECK_EQ_INT(plan[0].teximage_param, 0x04000000u);
        CHECK_EQ_INT(plan[0].pltt_base, 1u);
        CHECK_EQ_INT(plan[1].first_vertex, 9u);
        CHECK_EQ_INT(plan[1].vertex_count, 6u);
        CHECK_EQ_INT(plan[1].teximage_param, 0x0C000000u);
        CHECK_EQ_INT(plan[1].pltt_base, 1u);
    }
    test_report("pica-tex/bind-plan");
}

static void test_swizzle_out_of_bounds(void) {
    uint8_t swizzle_out[2][OOB_IMAGE_BYTES];
    uint8_t unswizzle_out[2][OOB_IMAGE_BYTES];
    uint32_t pass;

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        memset(g_oob_src, fill, sizeof(g_oob_src));
        memset(g_oob_dst, OOB_FILL, sizeof(g_oob_dst));
        /* Same payload in both passes; only the guard bytes differ. An input
         * read outside [OOB_SRC, OOB_SRC + OOB_IMAGE_BYTES) therefore sees
         * 0x00 in pass A and 0xFF in pass B, and the copied outputs diverge. */
        fill_random(OOB_SRC, OOB_IMAGE_BYTES, 1234u);

        CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(OOB_SRC, 16u, 8u, OOB_DST,
                                                  OOB_IMAGE_BYTES),
                     TWLHD_PICA_TEX_OK);
        CHECK(guard_intact(g_oob_src, OOB_IMAGE_BYTES, fill));
        CHECK(guard_intact(g_oob_dst, OOB_IMAGE_BYTES, OOB_FILL));
        memcpy(swizzle_out[pass], OOB_DST, OOB_IMAGE_BYTES);

        memset(g_oob_dst, OOB_FILL, sizeof(g_oob_dst));
        CHECK_EQ_INT(twlhd_pica_tex_unswizzle_rgba8(OOB_SRC, 16u, 8u, OOB_DST,
                                                    OOB_IMAGE_BYTES),
                     TWLHD_PICA_TEX_OK);
        CHECK(guard_intact(g_oob_src, OOB_IMAGE_BYTES, fill));
        CHECK(guard_intact(g_oob_dst, OOB_IMAGE_BYTES, OOB_FILL));
        memcpy(unswizzle_out[pass], OOB_DST, OOB_IMAGE_BYTES);
    }
    /* Any first/last-texel input over-read changed a pass-A output byte. */
    CHECK(memcmp(swizzle_out[0], swizzle_out[1], OOB_IMAGE_BYTES) == 0);
    CHECK(memcmp(unswizzle_out[0], unswizzle_out[1], OOB_IMAGE_BYTES) == 0);
    test_report("pica-tex/oob-swizzle");
}

static void test_texcoord_out_of_bounds(void) {
    uint8_t out_bytes[2][OOB_COORD_BYTES];
    TwlhdGxTexVertex payload[OOB_TEXCOORD_MAX];
    uint32_t pass;
    uint32_t i;

    for (i = 0; i < OOB_TEXCOORD_MAX; i++) {
        payload[i].s = (float)i;
        payload[i].t = (float)(i * 2u);
        payload[i].teximage_param = 0u;
        payload[i].pltt_base = 0u;
    }

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        uint8_t snapshot[OOB_TEX_BYTES];

        /* Identical input payload both passes; guards 0x00 then 0xFF. */
        memset(g_oob_tex, fill, sizeof(g_oob_tex));
        memcpy(g_oob_tex + OOB_GUARD, payload, OOB_TEX_BYTES);
        memset(g_oob_coord, OOB_FILL, sizeof(g_oob_coord));
        memcpy(snapshot, g_oob_tex + OOB_GUARD, OOB_TEX_BYTES);

        CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(OOB_TEX, OOB_TEXCOORD_MAX,
                                                  64u, 32u, OOB_COORD,
                                                  OOB_TEXCOORD_MAX),
                     TWLHD_PICA_TEX_OK);
        /* Every guard byte around both regions, not just byte 63. */
        CHECK(guard_intact(g_oob_tex, OOB_TEX_BYTES, fill));
        CHECK(guard_intact(g_oob_coord, OOB_COORD_BYTES, OOB_FILL));
        CHECK(memcmp(snapshot, g_oob_tex + OOB_GUARD, OOB_TEX_BYTES) == 0);
        memcpy(out_bytes[pass], g_oob_coord + OOB_GUARD, OOB_COORD_BYTES);
    }
    /* tex[-1]/tex[count] over-reads see 0x00 vs 0xFF (0.0f vs NaN) and
     * change u/v, so the outputs of the two passes diverge. */
    CHECK(memcmp(out_bytes[0], out_bytes[1], OOB_COORD_BYTES) == 0);
    test_report("pica-tex/oob-texcoord");
}

static void test_orientation_rows(void) {
    /* 8x8 image: row 0 solid red (255,0,0,255), row 7 solid blue
     * (0,0,255,255), all other rows black. With top-down tile rows, memory
     * tile row 0 (image rows 0..7) occupies the whole image and its in-tile
     * rows match image rows. Row 0 texels are Morton indexes 0..21, row 7
     * texels are 42..63. */
    uint32_t x;
    memset(g_src, 0, sizeof(g_src));
    for (x = 0; x < 8u; x++) {
        uint32_t p = x * 4u;
        g_src[p + 0u] = 255; /* R */
        g_src[p + 1u] = 0;   /* G */
        g_src[p + 2u] = 0;   /* B */
        g_src[p + 3u] = 255; /* A */
    }
    for (x = 0; x < 8u; x++) {
        uint32_t p = (7u * 8u + x) * 4u;
        g_src[p + 0u] = 0;
        g_src[p + 1u] = 0;
        g_src[p + 2u] = 255;
        g_src[p + 3u] = 255;
    }
    memset(g_swizzled, 0x5B, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 8u, g_swizzled,
                                              MAX_IMAGE_BYTES),
                 TWLHD_PICA_TEX_OK);

    /* Row 0 (red) -> memory texels 0..21, stored A,B,G,R = 255,0,0,255. */
    for (x = 0; x < 8u; x++) {
        uint32_t dest = kMorton8x8[0][x];
        const uint8_t *p = g_swizzled + dest * 4u;
        CHECK_EQ_INT(p[0], 255);
        CHECK_EQ_INT(p[1], 0);
        CHECK_EQ_INT(p[2], 0);
        CHECK_EQ_INT(p[3], 255);
    }
    /* Row 7 (blue) -> memory texels 42..63, stored A,B,G,R = 255,255,0,0. */
    for (x = 0; x < 8u; x++) {
        uint32_t dest = kMorton8x8[7][x];
        const uint8_t *p = g_swizzled + dest * 4u;
        CHECK_EQ_INT(p[0], 255);
        CHECK_EQ_INT(p[1], 255);
        CHECK_EQ_INT(p[2], 0);
        CHECK_EQ_INT(p[3], 0);
    }
    test_report("pica-tex/orientation-rows");
}

static void test_orientation_endtoend(void) {
    /* Same red-row-0 / blue-row-7 image; swizzle it, then compute the texcoord
     * for DS (s,t)=(0,0). The renderer gives vertex t=0 the coordinate v=1,
     * and PICA v=1 samples memory row 0, which is the red row. */
    TwlhdGxTexVertex tex[1];
    TwlhdPicaTexCoord out[1];
    uint32_t x;

    memset(g_src, 0, sizeof(g_src));
    for (x = 0; x < 8u; x++) {
        uint32_t p = x * 4u;
        g_src[p + 0u] = 255;
        g_src[p + 1u] = 0;
        g_src[p + 2u] = 0;
        g_src[p + 3u] = 255;
    }
    for (x = 0; x < 8u; x++) {
        uint32_t p = (7u * 8u + x) * 4u;
        g_src[p + 0u] = 0;
        g_src[p + 1u] = 0;
        g_src[p + 2u] = 255;
        g_src[p + 3u] = 255;
    }
    memset(g_swizzled, 0, sizeof(g_swizzled));
    CHECK_EQ_INT(twlhd_pica_tex_swizzle_rgba8(g_src, 8u, 8u, g_swizzled,
                                              MAX_IMAGE_BYTES),
                 TWLHD_PICA_TEX_OK);

    tex[0].s = 0.0f;
    tex[0].t = 0.0f;
    tex[0].teximage_param = 0u;
    tex[0].pltt_base = 0u;
    CHECK_EQ_INT(twlhd_pica_texcoords_from_gx(tex, 1u, 8u, 8u, out, 1u),
                 TWLHD_PICA_TEX_OK);
    CHECK_NEAR(out[0].u, 0.0, 1e-6);
    CHECK_NEAR(out[0].v, 1.0, 1e-6);

    /* v=1 addresses memory row 0; texel (0,0) is at Morton index 0. */
    CHECK_EQ_INT(g_swizzled[0u], 255); /* A */
    CHECK_EQ_INT(g_swizzled[1u], 0);   /* B */
    CHECK_EQ_INT(g_swizzled[2u], 0);   /* G */
    CHECK_EQ_INT(g_swizzled[3u], 255); /* R */
    test_report("pica-tex/orientation-endtoend");
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
    test_orientation_rows();
    test_orientation_endtoend();
    return test_exit_code();
}
