// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_tex_decode.c.
 *
 * Every expected pixel below is hand-computed from the DeSmuME formulas
 * documented in docs/DS_GX_RESEARCH.md ("Decoder (5B-2c)"); none of the
 * literals came out of the decoder. Sizes stay at 8x8/16x8/8x32 so the
 * stack/static buffers stay small and ASan-clean.
 *
 * The two expansion tables are pinned as literals copied from DeSmuME
 * utils/colorspacehandler/colorspacehandler.cpp, so a changed expansion
 * formula fails here even though the formula is (c << 3) | (c >> 2) /
 * (a << 5) | (a << 2) | (a >> 1).
 */

#include "twlhd_tex_decode.h"

#include "tests_host.h"

static uint8_t g_texel[2048];
static uint8_t g_palette[2048];
static uint8_t g_big_palette[65540]; /* largest format-5 palette window */
static uint8_t g_index[512];
static uint8_t g_out[16u * 32u * 4u];

/* Literal 5-bit -> 8-bit and 3-bit -> 8-bit tables (DeSmuME source). */
static const unsigned FIVE_TO_EIGHT[32] = {
    0x00, 0x08, 0x10, 0x18, 0x21, 0x29, 0x31, 0x39, 0x42, 0x4A, 0x52,
    0x5A, 0x63, 0x6B, 0x73, 0x7B, 0x84, 0x8C, 0x94, 0x9C, 0xA5, 0xAD,
    0xB5, 0xBD, 0xC6, 0xCE, 0xD6, 0xDE, 0xE7, 0xEF, 0xF7, 0xFF};
static const unsigned THREE_TO_EIGHT[8] = {0x00, 0x24, 0x49, 0x6D,
                                           0x92, 0xB6, 0xDB, 0xFF};

static uint32_t make_param(unsigned format, unsigned shift_s, unsigned shift_t,
                           int key0) {
    return ((uint32_t)format << 26) | ((uint32_t)shift_s << 20) |
           ((uint32_t)shift_t << 23) | (key0 ? 0x20000000u : 0u);
}

static TwlhdV2TexRecord make_rec(uint32_t param, uint32_t texel_size,
                                 uint32_t palette_size, uint32_t index_size) {
    TwlhdV2TexRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.teximage_param = param;
    rec.texel_size = texel_size;
    rec.palette_size = palette_size;
    rec.index_size = index_size;
    return rec;
}

static uint16_t rgb555(unsigned r, unsigned g, unsigned b, unsigned top) {
    return (uint16_t)((r & 0x1Fu) | ((g & 0x1Fu) << 5) | ((b & 0x1Fu) << 10) |
                      ((top & 1u) << 15));
}

static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)(value >> 8);
}

static void fill(uint8_t *p, size_t count, uint8_t value) {
    memset(p, value, count);
}

/* Check pixel `idx` of g_out (row-major) against hand-computed bytes. */
static void expect_px(const char *what, uint32_t idx, unsigned r, unsigned g,
                      unsigned b, unsigned a) {
    const uint8_t *p = g_out + (size_t)idx * 4u;

    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        g_checks_failed++;
        fprintf(stderr,
                "%s:%d: %s pixel %u: expected (%u,%u,%u,%u) got (%u,%u,%u,%u)\n",
                __FILE__, __LINE__, what, (unsigned)idx, r, g, b, a,
                (unsigned)p[0], (unsigned)p[1], (unsigned)p[2],
                (unsigned)p[3]);
    }
}

static void test_dimensions(void) {
    uint32_t width = 999u;
    uint32_t height = 999u;

    CHECK_EQ_INT(twlhd_tex_dimensions(0u, &width, &height), 0);
    CHECK_EQ_INT(width, 999u); /* untouched for format 0 */
    CHECK_EQ_INT(height, 999u);

    CHECK_EQ_INT(twlhd_tex_dimensions(make_param(1, 0, 0, 0), &width, &height),
                 1);
    CHECK_EQ_INT(width, 8u);
    CHECK_EQ_INT(height, 8u);
    CHECK_EQ_INT(twlhd_tex_dimensions(make_param(4, 1, 0, 0), &width, &height),
                 1);
    CHECK_EQ_INT(width, 16u);
    CHECK_EQ_INT(height, 8u);
    CHECK_EQ_INT(twlhd_tex_dimensions(make_param(7, 0, 2, 0), &width, &height),
                 1);
    CHECK_EQ_INT(width, 8u);
    CHECK_EQ_INT(height, 32u);
    CHECK_EQ_INT(twlhd_tex_dimensions(make_param(5, 7, 7, 0), &width, &height),
                 1);
    CHECK_EQ_INT(width, 1024u);
    CHECK_EQ_INT(height, 1024u);
    /* NULL outputs are ignored, the format check still applies. */
    CHECK_EQ_INT(twlhd_tex_dimensions(make_param(2, 0, 0, 0), NULL, NULL), 1);

    test_report("tex-decode/dimensions");
}

/* Pin both component tables with their literal DeSmuME values. */
static void test_expansion_tables(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;

    /* 16bpp 8x8: texel i uses r=i%32, g=31-i%32, b=(i*7)%32, opaque. */
    for (i = 0; i < 64u; i++) {
        unsigned k = i % 32u;
        put16(g_texel + i * 2u, rgb555(k, 31u - k, (k * 7u) % 32u, 1u));
    }
    rec = make_rec(make_param(7, 0, 0, 0), 128u, 0u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    for (i = 0; i < 64u; i++) {
        unsigned k = i % 32u;
        expect_px("5-bit table", i, FIVE_TO_EIGHT[k], FIVE_TO_EIGHT[31u - k],
                  FIVE_TO_EIGHT[(k * 7u) % 32u], 255u);
    }

    /* A3I5 8x8: texel a uses index a and alpha a, palette entry a = (a,0,0). */
    for (i = 0; i < 8u; i++) {
        g_texel[i] = (uint8_t)((i << 5) | i);
        put16(g_palette + i * 2u, rgb555(i, 0u, 0u, 0u));
    }
    fill(g_texel + 8u, 56u, 0u);
    rec = make_rec(make_param(1, 0, 0, 0), 64u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    for (i = 0; i < 8u; i++) {
        expect_px("3-bit table", i, FIVE_TO_EIGHT[i], 0u, 0u,
                  THREE_TO_EIGHT[i]);
    }

    test_report("tex-decode/expansion-tables");
}

static void test_a3i5_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 64u);
    put16(g_palette + 0u, rgb555(31u, 0u, 0u, 0u));   /* (255,0,0) */
    put16(g_palette + 2u, rgb555(0u, 31u, 0u, 1u));   /* bit 15 ignored */
    put16(g_palette + 10u, rgb555(0u, 0u, 31u, 1u)); /* entry 5, bit 15 set */
    put16(g_palette + 62u, rgb555(3u, 5u, 7u, 0u));   /* entry 31 */

    fill(g_texel, 64u, 0xE1u); /* index 1, alpha 7 */
    g_texel[0] = (uint8_t)((0u << 5) | 0u); /* alpha 0, index 0 */
    g_texel[1] = (uint8_t)((7u << 5) | 1u); /* alpha 7, index 1 */
    g_texel[2] = (uint8_t)((2u << 5) | 5u); /* alpha 2 -> 73, index 5 */
    g_texel[3] = (uint8_t)((5u << 5) | 31u); /* alpha 5 -> 182, index 31 */

    rec = make_rec(make_param(1, 0, 0, 1), 64u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("a3i5", 0, 255u, 0u, 0u, 0u);          /* alpha 0, RGB kept */
    expect_px("a3i5", 1, 0u, 255u, 0u, 255u);        /* opaque, top bit off */
    expect_px("a3i5", 2, 0u, 0u, 255u, 73u);         /* bit 15 ignored */
    expect_px("a3i5", 3, 24u, 41u, 57u, 182u);       /* last index 31 */
    expect_px("a3i5", 63, 0u, 255u, 0u, 255u);

    test_report("tex-decode/a3i5-8x8");
}

static void test_a3i5_16x8_key0_ignored(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;

    memset(g_palette, 0, 64u);
    put16(g_palette + 0u, rgb555(1u, 2u, 3u, 0u)); /* (8,16,24) */
    fill(g_texel, 128u, 0xE0u); /* index 0, alpha 7 */
    rec = make_rec(make_param(1, 1, 0, 1), 128u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    /* KeyColor0 must be ignored for A3I5: index 0 stays opaque. */
    expect_px("a3i5 key0 ignored", 0, 8u, 16u, 24u, 255u);
    expect_px("a3i5 key0 ignored", 127, 8u, 16u, 24u, 255u);
    for (i = 0; i < 128u; i++) {
        if (g_out[i * 4u + 3u] != 255u) g_checks_failed++;
    }

    test_report("tex-decode/a3i5-16x8-key0-ignored");
}

static void test_a5i3_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 16u);
    put16(g_palette + 0u, rgb555(31u, 31u, 0u, 0u));  /* (255,255,0) */
    put16(g_palette + 6u, rgb555(0u, 0u, 31u, 1u));   /* entry 3 */
    put16(g_palette + 14u, rgb555(4u, 8u, 12u, 0u));  /* entry 7 */

    fill(g_texel, 64u, 0xFBu); /* index 3, alpha 31 */
    g_texel[0] = 0u;                   /* index 0, alpha 0 */
    g_texel[1] = (uint8_t)((31u << 3) | 3u); /* alpha 31, index 3 */
    g_texel[2] = (uint8_t)((1u << 3) | 0u);  /* alpha 1 -> 8, index 0 */
    g_texel[3] = (uint8_t)((5u << 3) | 7u);  /* alpha 5 -> 182, index 7 */

    rec = make_rec(make_param(6, 0, 0, 0), 64u, 16u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("a5i3", 0, 255u, 255u, 0u, 0u);    /* alpha 0, RGB kept */
    expect_px("a5i3", 1, 0u, 0u, 255u, 255u);
    expect_px("a5i3", 2, 255u, 255u, 0u, 8u);
    expect_px("a5i3", 3, 33u, 66u, 99u, 41u);   /* last index 7 */
    expect_px("a5i3", 63, 0u, 0u, 255u, 255u);

    test_report("tex-decode/a5i3-8x8");
}

static void test_a5i3_8x32_key0_ignored(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 16u);
    put16(g_palette + 14u, rgb555(4u, 8u, 12u, 1u)); /* entry 7 (33,66,99) */
    fill(g_texel, 256u, 0x07u); /* index 7, alpha 0 */
    rec = make_rec(make_param(6, 0, 2, 1), 256u, 16u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    /* KeyColor0 is ignored for A5I3; alpha is 0 but the RGB survives. */
    expect_px("a5i3 key0 ignored", 0, 33u, 66u, 99u, 0u);
    expect_px("a5i3 key0 ignored", 255, 33u, 66u, 99u, 0u);

    test_report("tex-decode/a5i3-8x32-key0-ignored");
}

static void test_i2_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 8u);
    put16(g_palette + 0u, rgb555(10u, 20u, 30u, 0u)); /* (82,165,247) */
    put16(g_palette + 2u, rgb555(31u, 0u, 0u, 0u));   /* (255,0,0) */
    put16(g_palette + 4u, rgb555(0u, 31u, 0u, 0u));   /* (0,255,0) */
    put16(g_palette + 6u, rgb555(0u, 0u, 31u, 1u));   /* (0,0,255) */

    fill(g_texel, 16u, 0xE4u); /* low bits first: 0,1,2,3 */
    rec = make_rec(make_param(2, 0, 0, 0), 16u, 8u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i2", 0, 82u, 165u, 247u, 255u);
    expect_px("i2", 1, 255u, 0u, 0u, 255u);
    expect_px("i2", 2, 0u, 255u, 0u, 255u);
    expect_px("i2", 3, 0u, 0u, 255u, 255u); /* last palette index 3 */
    expect_px("i2", 63, 0u, 0u, 255u, 255u);

    /* KeyColor0 on: only index 0 becomes fully transparent. */
    rec = make_rec(make_param(2, 0, 0, 1), 16u, 8u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i2 key0", 0, 0u, 0u, 0u, 0u);
    expect_px("i2 key0", 1, 255u, 0u, 0u, 255u);
    expect_px("i2 key0", 2, 0u, 255u, 0u, 255u);
    expect_px("i2 key0", 3, 0u, 0u, 255u, 255u);

    test_report("tex-decode/i2-8x8");
}

static void test_i2_16x8_key0_nonzero_opaque(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 8u);
    put16(g_palette + 0u, rgb555(4u, 8u, 12u, 0u));
    put16(g_palette + 6u, rgb555(31u, 31u, 31u, 0u));
    fill(g_texel, 32u, 0xFFu); /* index 3 everywhere */
    rec = make_rec(make_param(2, 1, 0, 1), 32u, 8u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i2 16x8", 0, 255u, 255u, 255u, 255u);
    expect_px("i2 16x8", 127, 255u, 255u, 255u, 255u);

    test_report("tex-decode/i2-16x8-key0-nonzero-opaque");
}

static void test_i4_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 32u);
    put16(g_palette + 0u, rgb555(1u, 2u, 3u, 0u));     /* (8,16,24) */
    put16(g_palette + 14u, rgb555(0u, 0u, 0u, 0u));    /* entry 7 black */
    put16(g_palette + 30u, rgb555(31u, 31u, 31u, 1u)); /* entry 15 */

    fill(g_texel, 32u, 0xF0u); /* low nibble 0, high nibble 15 */
    rec = make_rec(make_param(3, 0, 0, 0), 32u, 32u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i4", 0, 8u, 16u, 24u, 255u);
    expect_px("i4", 1, 255u, 255u, 255u, 255u); /* last index 15 */
    expect_px("i4", 62, 8u, 16u, 24u, 255u);
    expect_px("i4", 63, 255u, 255u, 255u, 255u);

    rec = make_rec(make_param(3, 0, 0, 1), 32u, 32u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i4 key0", 0, 0u, 0u, 0u, 0u);
    expect_px("i4 key0", 1, 255u, 255u, 255u, 255u);

    test_report("tex-decode/i4-8x8");
}

static void test_i4_8x32_key0(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 32u);
    put16(g_palette + 14u, rgb555(31u, 0u, 0u, 0u));  /* entry 7 */
    put16(g_palette + 30u, rgb555(0u, 31u, 0u, 0u));  /* entry 15 */
    fill(g_texel, 128u, 0x7Fu); /* low 15, high 7 */
    rec = make_rec(make_param(3, 0, 2, 1), 128u, 32u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i4 8x32", 0, 0u, 255u, 0u, 255u);
    expect_px("i4 8x32", 1, 255u, 0u, 0u, 255u);
    expect_px("i4 8x32", 255, 255u, 0u, 0u, 255u); /* last texel is 7 */

    test_report("tex-decode/i4-8x32-key0");
}

static void test_i8_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 512u);
    put16(g_palette + 0u, rgb555(31u, 31u, 31u, 0u));
    put16(g_palette + 2u, rgb555(0u, 0u, 31u, 1u));
    put16(g_palette + 510u, rgb555(4u, 8u, 12u, 0u)); /* entry 255 */

    fill(g_texel, 64u, 255u);
    g_texel[0] = 0u;
    g_texel[1] = 1u;
    rec = make_rec(make_param(4, 0, 0, 0), 64u, 512u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i8", 0, 255u, 255u, 255u, 255u);
    expect_px("i8", 1, 0u, 0u, 255u, 255u);
    expect_px("i8", 2, 33u, 66u, 99u, 255u); /* last index 255 */
    expect_px("i8", 63, 33u, 66u, 99u, 255u);

    rec = make_rec(make_param(4, 0, 0, 1), 64u, 512u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i8 key0", 0, 0u, 0u, 0u, 0u);
    expect_px("i8 key0", 1, 0u, 0u, 255u, 255u);

    test_report("tex-decode/i8-8x8");
}

static void test_i8_16x8_key0(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 512u);
    put16(g_palette + 510u, rgb555(31u, 31u, 31u, 0u)); /* entry 255 */
    fill(g_texel, 128u, 255u);
    rec = make_rec(make_param(4, 1, 0, 1), 128u, 512u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("i8 16x8", 0, 255u, 255u, 255u, 255u);
    expect_px("i8 16x8", 127, 255u, 255u, 255u, 255u);

    test_report("tex-decode/i8-16x8-key0");
}

static void test_16bpp_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;

    put16(g_texel + 0u, rgb555(31u, 0u, 0u, 1u));
    put16(g_texel + 2u, rgb555(0u, 31u, 0u, 0u)); /* bit 15 clear */
    put16(g_texel + 4u, rgb555(0u, 0u, 31u, 1u));
    put16(g_texel + 6u, rgb555(31u, 31u, 31u, 1u));
    for (i = 4u; i < 64u; i++) put16(g_texel + i * 2u, 0x8000u);

    /* KeyColor0 must be ignored for 16bpp. */
    rec = make_rec(make_param(7, 0, 0, 1), 128u, 0u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("16bpp", 0, 255u, 0u, 0u, 255u);
    expect_px("16bpp", 1, 0u, 0u, 0u, 0u); /* transparent black */
    expect_px("16bpp", 2, 0u, 0u, 255u, 255u);
    expect_px("16bpp", 3, 255u, 255u, 255u, 255u);
    expect_px("16bpp", 63, 0u, 0u, 0u, 255u); /* 0x8000 = black opaque */

    test_report("tex-decode/16bpp-8x8");
}

static void test_16bpp_8x32(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;

    for (i = 0; i < 256u; i++) put16(g_texel + i * 2u, 0xFFFFu);
    put16(g_texel + 255u * 2u, rgb555(4u, 8u, 12u, 0u)); /* last transparent */
    rec = make_rec(make_param(7, 0, 2, 1), 512u, 0u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("16bpp 8x32", 0, 255u, 255u, 255u, 255u);
    expect_px("16bpp 8x32", 254, 255u, 255u, 255u, 255u);
    expect_px("16bpp 8x32", 255, 0u, 0u, 0u, 0u);

    test_report("tex-decode/16bpp-8x32");
}

/*
 * 4x4 8x8, key colour 0 on (it must be ignored). Blocks are filled by hand:
 *   block 0 (top-left):     mode 0, palette byte offset 0
 *   block 1 (top-right):    mode 1, palette byte offset 8
 *   block 2 (bottom-left):  mode 2, palette byte offset 16
 *   block 3 (bottom-right): mode 3, palette byte offset 0
 * Every texel row byte is 0xE4, i.e. texel values 0,1,2,3 across.
 */
static void test_4x4_modes_8x8(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;

    memset(g_palette, 0, 32u);
    put16(g_palette + 0u, rgb555(31u, 0u, 0u, 0u));   /* 0: (255,0,0) */
    put16(g_palette + 2u, rgb555(0u, 31u, 0u, 1u));   /* 1: (0,255,0) */
    put16(g_palette + 4u, rgb555(0u, 0u, 31u, 1u));   /* 2: (0,0,255) */
    put16(g_palette + 8u, rgb555(4u, 8u, 12u, 0u));   /* 4: (33,66,99) */
    put16(g_palette + 10u, rgb555(20u, 20u, 20u, 0u)); /* 5: (165,165,165) */
    put16(g_palette + 16u, rgb555(31u, 31u, 0u, 0u)); /* 8: (255,255,0) */
    put16(g_palette + 18u, rgb555(0u, 31u, 31u, 0u)); /* 9: (0,255,255) */
    put16(g_palette + 20u, rgb555(31u, 0u, 31u, 1u)); /* 10: (255,0,255) */
    put16(g_palette + 22u, rgb555(7u, 8u, 9u, 0u));   /* 11: (57,66,74) */

    fill(g_texel, 16u, 0xE4u);
    /* Block 2 (bottom-left) gets distinct rows so row placement is visible. */
    g_texel[8] = 0xE4u;
    g_texel[9] = 0x1Bu;
    g_texel[10] = 0xE4u;
    g_texel[11] = 0xB1u;
    put16(g_index + 0u, 0x0000u); /* mode 0, offset 0 */
    put16(g_index + 2u, 0x4002u); /* mode 1, offset 2 * 4 bytes */
    put16(g_index + 4u, 0x8004u); /* mode 2, offset 4 * 4 bytes */
    put16(g_index + 6u, 0xC000u); /* mode 3, offset 0 */

    rec = make_rec(make_param(5, 0, 0, 1), 16u, 32u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    /* Block 0, mode 0: two palette colours + third colour + transparent. */
    expect_px("4x4 mode0", 0, 255u, 0u, 0u, 255u);
    expect_px("4x4 mode0", 1, 0u, 255u, 0u, 255u);
    expect_px("4x4 mode0", 2, 0u, 0u, 255u, 255u);
    expect_px("4x4 mode0", 3, 0u, 0u, 0u, 0u);

    /* Block 1, mode 1: average of (33,66,99) and (165,165,165). */
    expect_px("4x4 mode1", 4, 33u, 66u, 99u, 255u);
    expect_px("4x4 mode1", 5, 165u, 165u, 165u, 255u);
    expect_px("4x4 mode1", 6, 99u, 115u, 132u, 255u);
    expect_px("4x4 mode1", 7, 0u, 0u, 0u, 0u);

    /* Block 2, mode 2: four palette colours. */
    expect_px("4x4 mode2", 32, 255u, 255u, 0u, 255u);
    expect_px("4x4 mode2", 33, 0u, 255u, 255u, 255u);
    expect_px("4x4 mode2", 34, 255u, 0u, 255u, 255u);
    expect_px("4x4 mode2", 35, 57u, 66u, 74u, 255u);

    /* Block 2 rows 1 and 3 use 0x1B / 0xB1, so row order is observable. */
    expect_px("4x4 mode2 row1", 40, 57u, 66u, 74u, 255u);  /* 0x1B: 3,2,1,0 */
    expect_px("4x4 mode2 row1", 41, 255u, 0u, 255u, 255u);
    expect_px("4x4 mode2 row1", 42, 0u, 255u, 255u, 255u);
    expect_px("4x4 mode2 row1", 43, 255u, 255u, 0u, 255u);
    expect_px("4x4 mode2 row3", 56, 0u, 255u, 255u, 255u);  /* 0xB1: 1,0,3,2 */
    expect_px("4x4 mode2 row3", 57, 255u, 255u, 0u, 255u);
    expect_px("4x4 mode2 row3", 58, 57u, 66u, 74u, 255u);
    expect_px("4x4 mode2 row3", 59, 255u, 0u, 255u, 255u);

    /* Block 3, mode 3: 5:3 then 3:5 on the expanded 8-bit channels. */
    expect_px("4x4 mode3", 36, 255u, 0u, 0u, 255u);
    expect_px("4x4 mode3", 37, 0u, 255u, 0u, 255u);
    expect_px("4x4 mode3", 38, 156u, 90u, 0u, 255u);
    expect_px("4x4 mode3", 39, 90u, 156u, 0u, 255u);

    /* Blocks 1 and 3 repeat the 0xE4 row on every row of the block. */
    for (i = 4u; i < 8u; i++) {
        if (memcmp(g_out + (3u * 8u + i) * 4u, g_out + i * 4u, 4u) != 0 ||
            memcmp(g_out + (7u * 8u + i) * 4u,
                   g_out + (4u * 8u + i) * 4u, 4u) != 0) {
            g_checks_failed++;
            fprintf(stderr, "%s:%d: 4x4 block rows not repeated i=%u\n",
                    __FILE__, __LINE__, (unsigned)i);
            break;
        }
    }

    test_report("tex-decode/4x4-modes-8x8");
}

/* 16x8: eight blocks, each mode 2 with its own palette offset, so the block
 * row-major ordering across a wide texture is visible in the pixel placement. */
static void test_4x4_16x8_block_order(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t block;

    memset(g_palette, 0, 64u);
    for (block = 0; block < 8u; block++) {
        uint32_t entry;
        for (entry = 0; entry < 4u; entry++) {
            put16(g_palette + (block * 4u + entry) * 2u,
                  rgb555(block, block, block, 0u));
        }
        put16(g_index + block * 2u,
              (uint16_t)(0x8000u | (block * 2u)));
    }
    fill(g_texel, 32u, 0x00u); /* texel value 0 selects the first colour */

    rec = make_rec(make_param(5, 1, 0, 0), 32u, 64u, 16u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("4x4 order b0", 0, 0u, 0u, 0u, 255u);
    expect_px("4x4 order b1", 4, 8u, 8u, 8u, 255u);
    expect_px("4x4 order b2", 8, 16u, 16u, 16u, 255u);
    expect_px("4x4 order b3", 12, 24u, 24u, 24u, 255u);
    expect_px("4x4 order b4", 4u * 16u, 33u, 33u, 33u, 255u);
    expect_px("4x4 order b5", 4u * 16u + 4u, 41u, 41u, 41u, 255u);
    expect_px("4x4 order b6", 4u * 16u + 8u, 49u, 49u, 49u, 255u);
    expect_px("4x4 order b7", 4u * 16u + 12u, 57u, 57u, 57u, 255u);
    expect_px("4x4 order last", 127u, 57u, 57u, 57u, 255u);

    test_report("tex-decode/4x4-16x8-block-order");
}

static void test_4x4_window_boundaries(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 32u);
    put16(g_palette + 0u, rgb555(31u, 0u, 0u, 0u));
    put16(g_palette + 2u, rgb555(0u, 31u, 0u, 0u));
    put16(g_palette + 4u, rgb555(0u, 0u, 31u, 0u));
    put16(g_palette + 6u, rgb555(31u, 31u, 31u, 0u));
    fill(g_texel, 16u, 0xE4u); /* texel values 0,1,2,3 across each row */

    /* Window of 8 bytes, mode 2 everywhere: reads bytes 0..7, exactly the end. */
    put16(g_index + 0u, 0x8000u);
    put16(g_index + 2u, 0x8000u);
    put16(g_index + 4u, 0x8000u);
    put16(g_index + 6u, 0x8000u);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("4x4 window end", 3, 255u, 255u, 255u, 255u);

    /* Six-byte window: the 4th entry (bytes 6,7) is one entry past -> error. */
    rec.palette_size = 6u;
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    /* Mode 0 needs exactly three entries, so six bytes are enough. */
    put16(g_index + 0u, 0x0000u);
    put16(g_index + 2u, 0x0000u);
    put16(g_index + 4u, 0x0000u);
    put16(g_index + 6u, 0x0000u);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 6u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    expect_px("4x4 window 6 bytes", 2, 0u, 0u, 255u, 255u);
    expect_px("4x4 window 6 bytes", 3, 0u, 0u, 0u, 0u);

    /* A four-byte window is enough for modes 1 and 3 (two entries only). */
    put16(g_index + 0u, 0x4000u);
    put16(g_index + 2u, 0xC000u);
    put16(g_index + 4u, 0xC000u);
    put16(g_index + 6u, 0x4000u);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 4u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    /* But mode 0 needs the third entry, so the same window must fail. */
    put16(g_index + 0u, 0x0000u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    /* Palette offsets near the 0x3FFF maximum are offset*4 bytes in. */
    put16(g_index + 0u, 0xC001u); /* mode 3, offset 4 bytes */
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);
    put16(g_index + 0u, 0xC002u); /* offset 8 bytes: one entry past the end */
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    /* Odd window sizes are rejected by the shared helper before decoding. */
    rec.palette_size = 7u;
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_SIZE);

    test_report("tex-decode/4x4-window-boundaries");
}

/*
 * Full 14-bit palette offset field: the window is the largest legal 65,540
 * bytes, so offsets up to 0x3FFF*4 = 65,532 are reachable and the last block
 * ends exactly at the window end. All literals hand-computed.
 */
static void test_4x4_high_palette_offsets(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_big_palette, 0, sizeof g_big_palette);
    /* Block 0 (mode 3) reads 65,532/65,534. */
    put16(g_big_palette + 65532u, rgb555(31u, 0u, 0u, 0u));
    put16(g_big_palette + 65534u, rgb555(0u, 31u, 0u, 0u));
    /* Block 1 (mode 3) at 0x3001 * 4 = 49,156; 0x1FFF would give 16,388. */
    put16(g_big_palette + 49156u, rgb555(0u, 0u, 31u, 0u));
    put16(g_big_palette + 49158u, rgb555(31u, 31u, 31u, 0u));
    /* Block 2 (mode 3) at 4 bytes. */
    put16(g_big_palette + 4u, rgb555(4u, 8u, 12u, 0u));   /* (33,66,99) */
    put16(g_big_palette + 6u, rgb555(1u, 2u, 3u, 0u));    /* (8,16,24) */
    /* Block 3 (mode 2) at 0x3FFE * 4 = 65,528: four entries to 65,535. */
    put16(g_big_palette + 65528u, rgb555(31u, 31u, 0u, 0u));
    put16(g_big_palette + 65530u, rgb555(0u, 31u, 31u, 0u));

    fill(g_texel, 16u, 0xE4u); /* texel values 0,1,2,3 on every row */
    put16(g_index + 0u, (uint16_t)(0xC000u | 0x3FFFu));
    put16(g_index + 2u, (uint16_t)(0xC000u | 0x3001u));
    put16(g_index + 4u, 0xC001u);
    put16(g_index + 6u, (uint16_t)(0x8000u | 0x3FFEu));

    rec = make_rec(make_param(5, 0, 0, 0), 16u, (uint32_t)sizeof g_big_palette,
                   8u);
    result = twlhd_tex_decode(&rec, g_texel, g_big_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    expect_px("4x4 high offset b0", 0, 255u, 0u, 0u, 255u);
    expect_px("4x4 high offset b0", 1, 0u, 255u, 0u, 255u);
    expect_px("4x4 high offset b0", 2, 156u, 90u, 0u, 255u);
    expect_px("4x4 high offset b0", 3, 90u, 156u, 0u, 255u);

    expect_px("4x4 high offset b1", 4, 0u, 0u, 255u, 255u);
    expect_px("4x4 high offset b1", 5, 255u, 255u, 255u, 255u);
    expect_px("4x4 high offset b1", 6, 90u, 90u, 255u, 255u);
    expect_px("4x4 high offset b1", 7, 156u, 156u, 255u, 255u);

    expect_px("4x4 high offset b2", 32, 33u, 66u, 99u, 255u);
    expect_px("4x4 high offset b2", 33, 8u, 16u, 24u, 255u);
    expect_px("4x4 high offset b2", 34, 16u, 41u, 66u, 255u);
    expect_px("4x4 high offset b2", 35, 16u, 33u, 49u, 255u);

    expect_px("4x4 high offset b3", 36, 255u, 255u, 0u, 255u);
    expect_px("4x4 high offset b3", 37, 0u, 255u, 255u, 255u);
    expect_px("4x4 high offset b3", 38, 255u, 0u, 0u, 255u);
    expect_px("4x4 high offset b3", 39, 0u, 255u, 0u, 255u);

    /* Mode 2 at 0x3FFF ends exactly at byte 65,540: legal. */
    put16(g_index + 0u, (uint16_t)(0x8000u | 0x3FFFu));
    result = twlhd_tex_decode(&rec, g_texel, g_big_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    /* One 16-bit entry past that window: 65,538 bytes must fail. */
    rec.palette_size = 65538u;
    result = twlhd_tex_decode(&rec, g_texel, g_big_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    /* Offset 0x2000 is 32,768 bytes in; a 20,000-byte window must fail (a
     * *2 offset formula would wrongly accept 16,384). */
    put16(g_index + 0u, (uint16_t)(0xC000u | 0x2000u));
    rec.palette_size = 20000u;
    result = twlhd_tex_decode(&rec, g_texel, g_big_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    /* Offset 0x2001 is 32,772 bytes in; an 0x1FFF mask would see 4. */
    put16(g_index + 0u, (uint16_t)(0xC000u | 0x2001u));
    rec.palette_size = 8u;
    result = twlhd_tex_decode(&rec, g_texel, g_big_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);

    test_report("tex-decode/4x4-high-palette-offsets");
}

static void test_error_paths(void) {
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;

    memset(g_palette, 0, 64u);
    memset(g_texel, 0, 128u);

    /* NULL arguments. */
    rec = make_rec(make_param(1, 0, 0, 0), 64u, 64u, 0u);
    result = twlhd_tex_decode(NULL, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);
    result = twlhd_tex_decode(&rec, NULL, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, NULL,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);

    /* Format 0 is rejected before any size check. */
    rec = make_rec(make_param(0, 0, 0, 0), 0u, 0u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_FORMAT);
    /* The format check also wins over the missing-palette check. */
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_FORMAT);

    /* Size mismatches. */
    rec = make_rec(make_param(1, 0, 0, 0), 63u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_SIZE);
    /* One byte too large is also a mismatch, not an excuse to read past. */
    rec = make_rec(make_param(1, 0, 0, 0), 65u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_SIZE);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 7u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_SIZE);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 9u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_SIZE);

    /* Palette sizes: wrong table size, 4x4 window too small, odd window. */
    rec = make_rec(make_param(1, 0, 0, 0), 64u, 62u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_SIZE);
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 0u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_SIZE);

    /* Missing 4x4 index block. */
    rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 8u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);

    /* Output buffer one byte short (8x8 needs 256). */
    rec = make_rec(make_param(1, 0, 0, 0), 64u, 64u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out, 255u);
    CHECK_EQ_INT(result, TWLHD_TEX_ERR_OUTPUT_SIZE);
    /* Exactly width*height*4 bytes is enough. */
    result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out, 256u);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    /* 16bpp has no palette: a NULL palette pointer is legal. */
    rec = make_rec(make_param(7, 0, 0, 0), 128u, 0u, 0u);
    result = twlhd_tex_decode(&rec, g_texel, NULL, NULL, g_out,
                              (uint32_t)sizeof g_out);
    CHECK_EQ_INT(result, TWLHD_TEX_OK);

    CHECK(strcmp(twlhd_tex_result_string(TWLHD_TEX_ERR_PALETTE_RANGE),
                 "ERR_PALETTE_RANGE") == 0);
    CHECK(strcmp(twlhd_tex_result_string((TwlhdTexResult)999), "ERR_UNKNOWN") ==
          0);

    test_report("tex-decode/error-paths");
}

/*
 * Every rejected call must leave the output buffer byte-identical. Each case
 * runs last-arg validation or a 4x4 palette pre-scan failure, which is
 * precisely where a partial write would show up.
 */
static void test_error_leaves_output_untouched(void) {
    static const uint8_t canary = 0xA5u;
    static const uint8_t palette_entry[8] = {0x1F, 0x00, 0xE0, 0x03,
                                             0x00, 0x7C, 0xFF, 0x7F};
    TwlhdV2TexRecord rec;
    TwlhdTexResult result;
    uint32_t i;
    int cases;

    for (cases = 0; cases < 5; cases++) {
        memset(g_out, canary, sizeof g_out);
        memset(g_texel, 0, 16u);
        memset(g_palette, 0, 32u);
        memcpy(g_palette, palette_entry, sizeof palette_entry);
        put16(g_index + 0u, 0x8000u); /* mode 2, needs entries 0-3 */

        switch (cases) {
        case 0: /* palette window one entry past, block 0 */
            rec = make_rec(make_param(5, 0, 0, 0), 16u, 6u, 8u);
            result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                                      (uint32_t)sizeof g_out);
            CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);
            break;
        case 1: /* block 3 is the first out-of-range block */
            put16(g_index + 0u, 0x0000u);
            put16(g_index + 2u, 0x0000u);
            put16(g_index + 4u, 0x0000u);
            put16(g_index + 6u, 0xC002u); /* offset 8 into a 8-byte window */
            rec = make_rec(make_param(5, 0, 0, 0), 16u, 8u, 8u);
            result = twlhd_tex_decode(&rec, g_texel, g_palette, g_index, g_out,
                                      (uint32_t)sizeof g_out);
            CHECK_EQ_INT(result, TWLHD_TEX_ERR_PALETTE_RANGE);
            break;
        case 2: /* wrong texel size */
            rec = make_rec(make_param(4, 0, 0, 0), 63u, 512u, 0u);
            result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                                      (uint32_t)sizeof g_out);
            CHECK_EQ_INT(result, TWLHD_TEX_ERR_SIZE);
            break;
        case 3: /* small output */
            rec = make_rec(make_param(4, 0, 0, 0), 64u, 512u, 0u);
            result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, g_out,
                                      16u);
            CHECK_EQ_INT(result, TWLHD_TEX_ERR_OUTPUT_SIZE);
            break;
        default: /* NULL output */
            rec = make_rec(make_param(4, 0, 0, 0), 64u, 512u, 0u);
            result = twlhd_tex_decode(&rec, g_texel, g_palette, NULL, NULL,
                                      (uint32_t)sizeof g_out);
            CHECK_EQ_INT(result, TWLHD_TEX_ERR_NULL);
            break;
        }

        for (i = 0; i < (uint32_t)sizeof g_out; i++) {
            if (g_out[i] != canary) {
                g_checks_failed++;
                fprintf(stderr, "%s:%d: case %d wrote g_out[%u]\n", __FILE__,
                        __LINE__, cases, (unsigned)i);
                break;
            }
        }
    }

    test_report("tex-decode/error-leaves-output-untouched");
}

int main(void) {
    test_dimensions();
    test_expansion_tables();
    test_a3i5_8x8();
    test_a3i5_16x8_key0_ignored();
    test_a5i3_8x8();
    test_a5i3_8x32_key0_ignored();
    test_i2_8x8();
    test_i2_16x8_key0_nonzero_opaque();
    test_i4_8x8();
    test_i4_8x32_key0();
    test_i8_8x8();
    test_i8_16x8_key0();
    test_16bpp_8x8();
    test_16bpp_8x32();
    test_4x4_modes_8x8();
    test_4x4_16x8_block_order();
    test_4x4_window_boundaries();
    test_4x4_high_palette_offsets();
    test_error_paths();
    test_error_leaves_output_untouched();
    return test_exit_code();
}
