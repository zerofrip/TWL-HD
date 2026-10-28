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
    return test_exit_code();
}
