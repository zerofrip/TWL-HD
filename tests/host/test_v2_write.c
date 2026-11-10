// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_v2_write.c.
 */

#include "twlhd_v2_write.h"

#include "tests_host.h"

static uint8_t g_out[TWLHD_V2_FILE_MAX + 64u];
static TwlhdV2CmdEntry g_entries_buf[TWLHD_V2_GXCM_MAX_ENTRIES];
static TwlhdV2RstaEntry g_rsta_buf[TWLHD_V2_RSTA_MAX_ENTRIES];

/* Byte-exact golden vector, also printed in docs/TWLHD_V2_FORMAT.md. */
static const uint8_t g_golden[176] = {
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0xB0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x58, 0x43, 0x4D, 0x40, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
    0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4D, 0x45, 0x54, 0x41, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x52, 0x53, 0x54, 0x41, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x03, 0x00, 0x04, 0xFF, 0x3F, 0x1F, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
};

static const TwlhdV2CmdEntry g_golden_entries[8] = {
    {0x10u, 2u}, {0x15u, 0u}, {0x40u, 1u}, {0x20u, 0x1Fu},
    {0x23u, 0x1000u}, {0x23u, 0x2000u}, {0x41u, 0u}, {0x50u, 0u},
};

static const TwlhdV2RstaEntry g_golden_rsta[1] = {
    {0x04000350u, 0x001F3FFFu, 3u, 4u},
};

/*
 * Texture golden fixture, mirrored byte for byte in
 * docs/TWLHD_V2_FORMAT.md section 10.1: two 8x8 I2 records (16 texel bytes +
 * 8 palette bytes each), TXDS 80 B, TXDT 32 B, PLDT 16 B.
 */
static const uint8_t g_tex_golden[352] = {
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x60, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x58, 0x43, 0x4D, 0x40, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
    0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4D, 0x45, 0x54, 0x41, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x52, 0x53, 0x54, 0x41, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x03, 0x00, 0x04, 0xFF, 0x3F, 0x1F, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x54, 0x58, 0x44, 0x53, 0x50, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x58, 0x44, 0x54, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
    0x50, 0x4C, 0x44, 0x54, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1F, 0x10, 0x2E, 0x20, 0x3D, 0x30, 0x4C,
    0x40, 0x5B, 0x50, 0x6A, 0x60, 0x79, 0x70, 0x88,
};

static const uint8_t g_tex0_texel[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
static const uint8_t g_tex1_texel[16] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F,
};
static const uint8_t g_tex0_palette[8] = {
    0x00, 0x1F, 0x10, 0x2E, 0x20, 0x3D, 0x30, 0x4C,
};
static const uint8_t g_tex1_palette[8] = {
    0x40, 0x5B, 0x50, 0x6A, 0x60, 0x79, 0x70, 0x88,
};

static const TwlhdV2TexEntry g_tex_entries[2] = {
    {0x08000000u, 0u, g_tex0_texel, g_tex0_palette, NULL, 16u, 8u, 0u},
    {0x08000000u, 1u, g_tex1_texel, g_tex1_palette, NULL, 16u, 8u, 0u},
};

static uint8_t g_fmt_texel[7][128];
static uint8_t g_fmt_palette[7][512];
static uint8_t g_fmt_index[7][8];
static TwlhdV2TexEntry g_fmt_entries[7];

static TwlhdV2WriteParams golden_params(void) {
    TwlhdV2WriteParams params;

    params.frame_index = 1u;
    params.flags = TWLHD_V2_FLAG_FRAME_COMPLETE;
    params.backend_id = TWLHD_V2_BACKEND_REGISTER_PROXY;
    params.coverage_flags = TWLHD_V2_COVERAGE_LIBRARY_INTERNAL_WRITES |
                            TWLHD_V2_COVERAGE_DMA_DISPLAY_LISTS |
                            TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN;
    params.gxcm = g_golden_entries;
    params.gxcm_count = 8u;
    params.rsta = g_golden_rsta;
    params.rsta_count = 1u;
    params.tex = NULL;
    params.tex_count = 0u;
    return params;
}

static void test_golden(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2View view;
    size_t size = 0;

    memset(g_out, 0xCD, sizeof g_out);
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, 176u, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, 176u);
    CHECK(memcmp(g_out, g_golden, sizeof g_golden) == 0);
    CHECK_EQ_INT(g_out[176], 0xCD);

    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, 8u);
    CHECK_EQ_INT(view.rsta_count, 1u);
    CHECK_EQ_INT(view.frame_index, 1u);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(view.meta_backend_id, TWLHD_V2_BACKEND_REGISTER_PROXY);
    CHECK_EQ_INT(view.meta_coverage_flags, 7u);
    CHECK_EQ_INT(twlhd_v2_rsta_gx_index(&view, 0), 3u);
}

static void test_determinism(void) {
    TwlhdV2WriteParams params = golden_params();
    uint8_t second[176];
    size_t size_a = 0;
    size_t size_b = 0;

    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size_a),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_write(&params, second, sizeof second, &size_b),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size_a, size_b);
    CHECK(memcmp(g_out, second, size_a) == 0);
}

static void test_no_rsta_chunk(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2View view;
    size_t size = 0;

    params.rsta = NULL;
    params.rsta_count = 0u;

    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, 80u + 8u * 8u);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.chunk_count, 2u);
    CHECK_EQ_INT(view.rsta_count, 0);
    CHECK(view.rsta == NULL);
}

static void test_errors(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2CmdEntry one = {TWLHD_V2_CMD_BEGIN_VTXS, 0u};
    TwlhdV2RstaEntry bad_rsta = {0x04000350u, 0u, 0u, 3u};
    size_t size = 0;
    uint8_t guard;

    CHECK_EQ_INT(twlhd_v2_write(NULL, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_v2_write(&params, NULL, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, 175u, &size),
                 TWLHD_V2_ERR_SIZE);

    guard = g_out[175];
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, 175u, &size),
                 TWLHD_V2_ERR_SIZE);
    CHECK_EQ_INT(g_out[175], guard);

    params.gxcm_count = 0u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_GXCM);
    params = golden_params();

    params.gxcm = NULL;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
    params = golden_params();

    params.rsta = NULL;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
    params = golden_params();

    params.rsta_count = TWLHD_V2_RSTA_MAX_ENTRIES + 1u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_RSTA);
    params = golden_params();

    /* The re-parse is the validity gate: reserved flag bits are refused. */
    params.flags = 4u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_FLAGS);
    params = golden_params();

    /* FRAME_COMPLETE without a trailing swap is refused. */
    params.gxcm_count = 7u;
    params.rsta_count = 0u;
    params.rsta = NULL;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_GXCM_FRAME);
    params = golden_params();

    /* Bad RSTA width is refused by the re-parse. */
    params.rsta = &bad_rsta;
    params.rsta_count = 1u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_RSTA);
    params = golden_params();

    /* Incomplete trailing group needs OVERFLOW. */
    params.gxcm = &one;
    params.gxcm_count = 1u;
    params.rsta = NULL;
    params.rsta_count = 0u;
    params.flags = 0u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);

    params.flags = TWLHD_V2_FLAG_OVERFLOW;
    {
        TwlhdV2CmdEntry two = {TWLHD_V2_CMD_VTX_16, 0u};
        params.gxcm = &two;
        CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                     TWLHD_V2_OK);
    }
}

static void test_max_capacity(void) {
    TwlhdV2WriteParams params;
    TwlhdV2CmdEntry *entries = g_entries_buf;
    TwlhdV2RstaEntry *rsta = g_rsta_buf;
    TwlhdV2View view;
    uint64_t expected;
    uint32_t i;
    size_t size = 0;

    for (i = 0; i < TWLHD_V2_GXCM_MAX_ENTRIES; i++) {
        entries[i].cmd_word = TWLHD_V2_CMD_BEGIN_VTXS;
        entries[i].value = 1u;
    }
    for (i = 0; i < TWLHD_V2_RSTA_MAX_ENTRIES; i++) {
        rsta[i].addr = 0x04000350u;
        rsta[i].value = i;
        rsta[i].gx_index = 0u;
        rsta[i].width = 4u;
    }

    params.frame_index = 0u;
    params.flags = 0u;
    params.backend_id = TWLHD_V2_BACKEND_SYNTHETIC;
    params.coverage_flags = 0u;
    params.gxcm = entries;
    params.gxcm_count = TWLHD_V2_GXCM_MAX_ENTRIES;
    params.rsta = rsta;
    params.rsta_count = TWLHD_V2_RSTA_MAX_ENTRIES;
    params.tex = NULL;
    params.tex_count = 0u;

    expected = 32u + 3u * 16u + TWLHD_V2_META_PAYLOAD_SIZE +
               (uint64_t)TWLHD_V2_GXCM_MAX_ENTRIES * 8u +
               (uint64_t)TWLHD_V2_RSTA_MAX_ENTRIES * 16u;

    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, expected);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, TWLHD_V2_GXCM_MAX_ENTRIES);
    CHECK_EQ_INT(view.rsta_count, TWLHD_V2_RSTA_MAX_ENTRIES);
}

static void test_tex_golden(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    uint8_t second[352];
    size_t size = 0;
    size_t size_b = 0;

    params.tex = g_tex_entries;
    params.tex_count = 2u;

    memset(g_out, 0xCD, sizeof g_out);
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, 352u);
    CHECK(memcmp(g_out, g_tex_golden, sizeof g_tex_golden) == 0);
    CHECK_EQ_INT(g_out[352], 0xCD);

    /* Deterministic: a second run yields identical bytes. */
    CHECK_EQ_INT(twlhd_v2_write(&params, second, sizeof second, &size_b),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size_b, size);
    CHECK(memcmp(g_out, second, size) == 0);

    /* One byte short is refused before anything is written. */
    CHECK_EQ_INT(twlhd_v2_write(&params, second, 351u, &size_b),
                 TWLHD_V2_ERR_SIZE);

    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.chunk_count, 6u);
    CHECK_EQ_INT(view.txds_count, 2u);
    CHECK_EQ_INT(view.txdt_size, 32u);
    CHECK_EQ_INT(view.pldt_size, 16u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 1, &rec), 1);
    CHECK_EQ_INT(rec.teximage_param, 0x08000000u);
    CHECK_EQ_INT(rec.pltt_base, 1u);
    CHECK_EQ_INT(rec.texel_offset, 16u);
    CHECK_EQ_INT(rec.texel_size, 16u);
    CHECK_EQ_INT(rec.palette_offset, 8u);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK(twlhd_v2_tex_texel_ptr(&view, 1) == g_out + 0x120u + 16u);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 1) == g_out + 0x150u + 8u);
    CHECK(twlhd_v2_tex_index_ptr(&view, 1) == NULL);
}

static void test_tex_formats(void) {
    TwlhdV2WriteParams params = golden_params();
    static const uint32_t texel_sizes[7] = {64u, 16u, 32u, 64u, 16u, 64u, 128u};
    static const uint32_t palette_sizes[7] = {64u, 8u, 32u, 512u, 8u, 16u, 0u};
    static const uint32_t index_sizes[7] = {0u, 0u, 0u, 0u, 8u, 0u, 0u};
    static const uint32_t texel_offsets[7] = {0u, 64u, 80u, 112u,
                                              176u, 200u, 264u};
    static const uint32_t palette_offsets[7] = {0u, 64u, 72u, 104u,
                                                616u, 624u, 0u};
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    uint32_t expected_txdt = 0;
    uint32_t expected_pldt = 0;
    size_t size = 0;
    uint32_t i;
    uint32_t j;

    for (i = 0; i < 7u; i++) {
        for (j = 0; j < texel_sizes[i]; j++) {
            g_fmt_texel[i][j] = (uint8_t)(0x10u * (i + 1u) + (j & 0x0Fu));
        }
        for (j = 0; j < palette_sizes[i]; j++) {
            g_fmt_palette[i][j] = (uint8_t)(0x20u * (i + 1u) + (j & 0x0Fu));
        }
        for (j = 0; j < index_sizes[i]; j++) {
            g_fmt_index[i][j] = (uint8_t)(0x30u + i + j);
        }
        g_fmt_entries[i].teximage_param = (uint32_t)(i + 1u) << 26;
        g_fmt_entries[i].pltt_base = i;
        g_fmt_entries[i].texel = g_fmt_texel[i];
        g_fmt_entries[i].palette = palette_sizes[i] != 0u ? g_fmt_palette[i]
                                                          : NULL;
        g_fmt_entries[i].index = index_sizes[i] != 0u ? g_fmt_index[i] : NULL;
        g_fmt_entries[i].texel_bytes = texel_sizes[i];
        g_fmt_entries[i].palette_bytes = palette_sizes[i];
        g_fmt_entries[i].index_bytes = index_sizes[i];
        expected_txdt += texel_sizes[i] + index_sizes[i];
        expected_pldt += palette_sizes[i];
    }

    params.tex = g_fmt_entries;
    params.tex_count = 7u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 7u);
    CHECK_EQ_INT(view.txdt_size, expected_txdt);
    CHECK_EQ_INT(view.pldt_size, expected_pldt);

    for (i = 0; i < 7u; i++) {
        CHECK_EQ_INT(twlhd_v2_txds_record(&view, i, &rec), 1);
        CHECK_EQ_INT(rec.teximage_param, (uint32_t)(i + 1u) << 26);
        CHECK_EQ_INT(rec.pltt_base, i);
        CHECK_EQ_INT(rec.texel_offset, texel_offsets[i]);
        CHECK_EQ_INT(rec.texel_size, texel_sizes[i]);
        CHECK_EQ_INT(rec.palette_offset, palette_offsets[i]);
        CHECK_EQ_INT(rec.palette_size, palette_sizes[i]);
        CHECK_EQ_INT(rec.index_size, index_sizes[i]);
        if (index_sizes[i] != 0u) {
            CHECK_EQ_INT(rec.index_offset, texel_offsets[i] + texel_sizes[i]);
            CHECK(twlhd_v2_tex_index_ptr(&view, i) ==
                  view.txdt + rec.index_offset);
        } else {
            CHECK_EQ_INT(rec.index_offset, 0u);
            CHECK(twlhd_v2_tex_index_ptr(&view, i) == NULL);
        }
        CHECK(twlhd_v2_tex_texel_ptr(&view, i)[0] == g_fmt_texel[i][0]);
        CHECK(twlhd_v2_tex_texel_ptr(&view, i)[texel_sizes[i] - 1u] ==
              g_fmt_texel[i][texel_sizes[i] - 1u]);
        if (palette_sizes[i] != 0u) {
            CHECK(twlhd_v2_tex_palette_ptr(&view, i) ==
                  view.pldt + rec.palette_offset);
            CHECK(twlhd_v2_tex_palette_ptr(&view, i)[palette_sizes[i] - 1u] ==
                  g_fmt_palette[i][palette_sizes[i] - 1u]);
        } else {
            CHECK(twlhd_v2_tex_palette_ptr(&view, i) == NULL);
        }
    }

    /* A file whose textures carry no palette omits the PLDT chunk. */
    params.tex_count = 1u;
    params.tex = &g_fmt_entries[6]; /* 16bpp: texel only, no palette */
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.chunk_count, 5u);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.pldt_size, 0u);
    CHECK(view.pldt == NULL);
}

static void test_tex_4x4_palette(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2TexEntry entry;
    static uint8_t palette[TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES];
    static uint8_t texel[16];
    static uint8_t index[8];
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    size_t size = 0;
    uint32_t i;

    for (i = 0; i < sizeof palette; i++) palette[i] = (uint8_t)(i & 0xFFu);
    for (i = 0; i < sizeof texel; i++) texel[i] = (uint8_t)(0x40u + i);

    entry.teximage_param = 5u << 26;
    entry.pltt_base = 0u;
    entry.texel = texel;
    entry.palette = palette;
    entry.index = index;
    entry.texel_bytes = 16u;
    entry.palette_bytes = 8u;
    entry.index_bytes = 8u;

    params.tex = &entry;
    params.tex_count = 1u;

    /* 8-byte window round trip. */
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.pldt_size, 8u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 0, &rec), 1);
    CHECK_EQ_INT(rec.palette_offset, 0u);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 0)[7] == palette[7]);

    /* Maximum-size window round trip. */
    entry.palette_bytes = TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.pldt_size, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 0, &rec), 1);
    CHECK_EQ_INT(rec.palette_size, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES);
    if (twlhd_v2_tex_palette_ptr(&view, 0) != NULL) {
        CHECK(twlhd_v2_tex_palette_ptr(&view, 0)[sizeof palette - 1u] ==
              palette[sizeof palette - 1u]);
    }
}

/*
 * Byte-exact format-5 golden: one 8x8 4x4 texture, TXDS 40 B, TXDT 24 B
 * (16 texel + 8 index bytes), PLDT 8 B, no RSTA. Mirrored byte for byte in
 * tests/host/test_v2_parse.c.
 */
static const uint8_t g_fmt5_tex_golden[264] = {
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x58, 0x43, 0x4D, 0x40, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x23, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
    0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4D, 0x45, 0x54, 0x41, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x58, 0x44, 0x53, 0x28, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x54, 0x58, 0x44, 0x54, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x40, 0x51, 0x62, 0x73, 0x84, 0x95, 0xA6, 0xB7,
    0x50, 0x4C, 0x44, 0x54, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x1F, 0x10, 0x2E, 0x20, 0x3D, 0x30, 0x4C,
};

static const uint8_t g_fmt5_texel[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
static const uint8_t g_fmt5_index[8] = {
    0x40, 0x51, 0x62, 0x73, 0x84, 0x95, 0xA6, 0xB7,
};
static const uint8_t g_fmt5_palette[8] = {
    0x00, 0x1F, 0x10, 0x2E, 0x20, 0x3D, 0x30, 0x4C,
};

static const TwlhdV2TexEntry g_fmt5_entry = {
    5u << 26, 0u, g_fmt5_texel, g_fmt5_palette, g_fmt5_index, 16u, 8u, 8u,
};

static void test_tex_format5_golden(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    size_t size = 0;

    params.rsta = NULL;
    params.rsta_count = 0u;
    params.tex = &g_fmt5_entry;
    params.tex_count = 1u;

    memset(g_out, 0xCD, sizeof g_out);
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, 264u);
    CHECK(memcmp(g_out, g_fmt5_tex_golden, sizeof g_fmt5_tex_golden) == 0);
    CHECK_EQ_INT(g_out[264], 0xCD);

    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.chunk_count, 5u);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.txdt_size, 24u);
    CHECK_EQ_INT(view.pldt_size, 8u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 0, &rec), 1);
    CHECK_EQ_INT(rec.teximage_param, 5u << 26);
    CHECK_EQ_INT(rec.texel_offset, 0u);
    CHECK_EQ_INT(rec.texel_size, 16u);
    CHECK_EQ_INT(rec.palette_offset, 0u);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK_EQ_INT(rec.index_offset, 16u);
    CHECK_EQ_INT(rec.index_size, 8u);
    CHECK(twlhd_v2_tex_texel_ptr(&view, 0) == g_out + 0xD8u);
    CHECK(twlhd_v2_tex_index_ptr(&view, 0) == g_out + 0xE8u);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 0) == g_out + 0x100u);
    CHECK_EQ_INT(twlhd_v2_tex_texel_ptr(&view, 0)[0], 0x00u);
    CHECK_EQ_INT(twlhd_v2_tex_index_ptr(&view, 0)[0], 0x40u);
    CHECK_EQ_INT(twlhd_v2_tex_palette_ptr(&view, 0)[7], 0x4Cu);
}

static void test_tex_errors(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2TexEntry bad[2];
    static uint8_t over[TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES + 4u];
    size_t size = 0;

    memcpy(bad, g_tex_entries, sizeof bad);
    params.tex = bad;
    params.tex_count = 1u;

    bad[0].texel_bytes = 15u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_SIZE);
    memcpy(bad, g_tex_entries, sizeof bad);

    /* Format 5 accepts an even caller-chosen palette window. */
    bad[0].teximage_param = 5u << 26;
    bad[0].index = g_fmt_index[4];
    bad[0].index_bytes = 8u;
    bad[0].palette_bytes = 0u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_SIZE);
    bad[0].palette_bytes = 3u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_SIZE);
    bad[0].palette = over;
    bad[0].palette_bytes = 65541u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_SIZE);
    bad[0].palette_bytes = 65542u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_SIZE);
    memcpy(bad, g_tex_entries, sizeof bad);

    bad[0].teximage_param = 0u; /* format 0 */
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS);
    memcpy(bad, g_tex_entries, sizeof bad);

    bad[0].texel = NULL;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS);
    memcpy(bad, g_tex_entries, sizeof bad);

    bad[0].palette = NULL;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS);
    memcpy(bad, g_tex_entries, sizeof bad);

    params.tex_count = 2u;
    bad[1].pltt_base = 0u; /* duplicate (teximage_param, pltt_base) */
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS_KEY);

    params.tex = g_tex_entries;
    params.tex_count = TWLHD_V2_TXDS_MAX_RECORDS + 1u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_TXDS);

    params.tex = NULL;
    params.tex_count = 1u;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_NULL);
}

static void test_tex_only_adds_chunks(void) {
    TwlhdV2WriteParams plain = golden_params();
    TwlhdV2WriteParams params = golden_params();
    static uint8_t plain_bytes[176];
    size_t plain_size = 0;
    size_t size = 0;

    params.tex = g_tex_entries;
    params.tex_count = 2u;

    CHECK_EQ_INT(twlhd_v2_write(&plain, plain_bytes, sizeof plain_bytes,
                                 &plain_size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(plain_size, 176u);
    CHECK_EQ_INT(size, 352u);

    /* Only the header size/count and the three new chunks differ. */
    CHECK(memcmp(plain_bytes + 32u, g_out + 32u, 176u - 32u) == 0);
    CHECK(memcmp(plain_bytes + 0u, g_out + 0u, 8u) == 0);
    CHECK(memcmp(plain_bytes + 8u, g_out + 8u, 4u) != 0);
    CHECK_EQ_INT(g_out[20], 6u);
}

/*
 * One full 65,540-byte format-5 window plus a second palette-bearing record:
 * accepted while the concatenated PLDT stays under the 98,304-byte cap, and
 * ERR_PLDT once two full windows (2 x 65,540 = 131,080) exceed it.
 */
static void test_tex_4x4_pldt_limit(void) {
    TwlhdV2WriteParams params = golden_params();
    TwlhdV2TexEntry tex[2];
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    static uint8_t big[TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES];
    size_t size = 0;
    uint32_t i;

    for (i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i & 0xFFu);

    params.rsta = NULL;
    params.rsta_count = 0u;
    params.tex = tex;
    params.tex_count = 2u;

    /* Two full windows: the writer cannot share one, so PLDT overflows. */
    tex[0] = (TwlhdV2TexEntry){5u << 26, 0u, g_fmt5_texel, big, g_fmt5_index,
                               16u, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES, 8u};
    tex[1] = (TwlhdV2TexEntry){5u << 26, 1u, g_fmt5_texel, big, g_fmt5_index,
                               16u, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES, 8u};
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_ERR_PLDT);

    /* One full window plus an 8-byte I2 palette fits under the cap. */
    tex[1] = (TwlhdV2TexEntry){0x08000000u, 1u, g_tex0_texel,
                               g_tex0_palette, NULL, 16u, 8u, 0u};
    CHECK_EQ_INT(twlhd_v2_write(&params, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.pldt_size, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES + 8u);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 0, &rec), 1);
    CHECK_EQ_INT(rec.palette_size, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES);
    CHECK_EQ_INT(twlhd_v2_txds_record(&view, 1, &rec), 1);
    CHECK_EQ_INT(rec.palette_offset, TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES);
    CHECK_EQ_INT(rec.palette_size, 8u);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 0)[0] == big[0]);
    CHECK(twlhd_v2_tex_palette_ptr(&view, 1)[7] == g_tex0_palette[7]);
}

int main(void) {
    test_golden();
    test_report("v2-write/golden");
    test_determinism();
    test_report("v2-write/determinism");
    test_no_rsta_chunk();
    test_report("v2-write/no-rsta-chunk");
    test_errors();
    test_report("v2-write/errors");
    test_max_capacity();
    test_report("v2-write/max-capacity");
    test_tex_golden();
    test_report("v2-write/tex-golden");
    test_tex_formats();
    test_report("v2-write/tex-formats");
    test_tex_4x4_palette();
    test_report("v2-write/tex-4x4-palette");
    test_tex_format5_golden();
    test_report("v2-write/tex-format5-golden");
    test_tex_errors();
    test_report("v2-write/tex-errors");
    test_tex_4x4_pldt_limit();
    test_report("v2-write/tex-4x4-pldt-limit");
    test_tex_only_adds_chunks();
    test_report("v2-write/tex-only-adds-chunks");
    return test_exit_code();
}
