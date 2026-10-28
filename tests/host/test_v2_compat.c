// SPDX-License-Identifier: Apache-2.0
/*
 * v1 <-> v2 compatibility tests: both parsers must fail closed on the other
 * format, and the detector must never mislabel either one.
 */

#include "twlhd_frame_parse.h"
#include "twlhd_v2_parse.h"

#include "tests_host.h"

static uint8_t g_v1[TWLHD_V2_V1_WIRE_SIZE];
static uint8_t g_v2[TWLHD_V2_V1_WIRE_SIZE + 64u];

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void build_v1(void) {
    memset(g_v1, 0, sizeof g_v1);
    put_le32(g_v1 + 0, TWLHD_FORMAT_VERSION);
    put_le32(g_v1 + 4, 5u);
    put_le32(g_v1 + 8, 2u);

    /* COLOR then VTX_16, laid out like tools/twlhd_dump.py expects. */
    put_le32(g_v1 + 12 + 0, TWLHD_CMD_COLOR);
    g_v1[12 + 4] = 1;
    put_le32(g_v1 + 12 + 8, 0x1Fu);

    put_le32(g_v1 + 12 + 72 + 0, TWLHD_CMD_VTX_16);
    g_v1[12 + 72 + 4] = 2;
    put_le32(g_v1 + 12 + 72 + 8, 0x1000u);
    put_le32(g_v1 + 12 + 72 + 12, 0x2000u);
}

/*
 * A syntactically valid v2 file padded to exactly the v1 size with an unknown,
 * optional chunk. Used to prove the v1 parser's version check fires once the
 * size check passes.
 */
static size_t build_v2_at_v1_size(void) {
    size_t offset = 32u;
    const uint32_t pad = 73636u;

    memset(g_v2, 0, sizeof g_v2);

    put_le32(g_v2 + 0, TWLHD_V2_VERSION);
    put_le32(g_v2 + 4, TWLHD_V2_HEADER_SIZE);
    put_le32(g_v2 + 8, TWLHD_V2_V1_WIRE_SIZE);
    put_le32(g_v2 + 12, 0);
    put_le32(g_v2 + 16, 0);
    put_le32(g_v2 + 20, 3);
    put_le32(g_v2 + 24, 0);
    put_le32(g_v2 + 28, 0);

    put_le32(g_v2 + offset + 0, TWLHD_V2_CHUNK_GXCM);
    put_le32(g_v2 + offset + 4, 8u);
    put_le32(g_v2 + offset + 8, TWLHD_V2_CHUNK_FLAG_REQUIRED);
    put_le32(g_v2 + offset + 12, 0);
    put_le32(g_v2 + offset + 16, TWLHD_V2_CMD_BEGIN_VTXS);
    put_le32(g_v2 + offset + 20, 1u);
    offset += 24u;

    put_le32(g_v2 + offset + 0, TWLHD_V2_CHUNK_META);
    put_le32(g_v2 + offset + 4, TWLHD_V2_META_PAYLOAD_SIZE);
    put_le32(g_v2 + offset + 8, 0);
    put_le32(g_v2 + offset + 12, 0);
    put_le32(g_v2 + offset + 16, TWLHD_V2_BACKEND_SYNTHETIC);
    put_le32(g_v2 + offset + 20, 0);
    put_le32(g_v2 + offset + 24, 0);
    put_le32(g_v2 + offset + 28, 0);
    offset += 32u;

    put_le32(g_v2 + offset + 0, TWLHD_FOURCC('P', 'A', 'D', '0'));
    put_le32(g_v2 + offset + 4, pad);
    put_le32(g_v2 + offset + 8, 0);
    put_le32(g_v2 + offset + 12, 0);
    offset += 16u + pad;

    CHECK_EQ_INT(offset, TWLHD_V2_V1_WIRE_SIZE);
    return offset;
}

static void test_detect(void) {
    size_t v2_size;

    build_v1();
    CHECK_EQ_INT(twlhd_detect_format(NULL, 0), TWLHD_FILE_UNKNOWN);
    CHECK_EQ_INT(twlhd_detect_format(g_v1, 0), TWLHD_FILE_UNKNOWN);
    CHECK_EQ_INT(twlhd_detect_format(g_v1, 3), TWLHD_FILE_UNKNOWN);
    CHECK_EQ_INT(twlhd_detect_format(g_v1, sizeof g_v1), TWLHD_FILE_V1);

    put_le32(g_v1 + 0, TWLHD_FORMAT_VERSION);
    CHECK_EQ_INT(twlhd_detect_format(g_v1, sizeof g_v1 - 1u),
                 TWLHD_FILE_UNKNOWN);
    put_le32(g_v1 + 0, 3u);
    CHECK_EQ_INT(twlhd_detect_format(g_v1, sizeof g_v1), TWLHD_FILE_UNKNOWN);
    put_le32(g_v1 + 0, TWLHD_FORMAT_VERSION);

    v2_size = build_v2_at_v1_size();
    CHECK_EQ_INT(twlhd_detect_format(g_v2, v2_size), TWLHD_FILE_V2);
    CHECK_EQ_INT(twlhd_detect_format(g_v2, 31u), TWLHD_FILE_UNKNOWN);
    CHECK_EQ_INT(twlhd_detect_format(g_v2, 32u), TWLHD_FILE_V2);
}

static void test_parsers_fail_closed(void) {
    TwlhdParsedFrame v1_view;
    TwlhdV2View v2_view;
    size_t v2_size;

    build_v1();
    CHECK_EQ_INT(twlhd_frame_parse(g_v1, sizeof g_v1, &v1_view),
                 TWLHD_PARSE_OK);
    CHECK_EQ_INT(v1_view.cmd_count, 2u);

    /* v2 cannot be read as v1: size first, then version at the v1 size. */
    v2_size = build_v2_at_v1_size();
    CHECK_EQ_INT(twlhd_frame_parse(g_v2, v2_size, &v1_view),
                 TWLHD_PARSE_ERR_VERSION);
    CHECK_EQ_INT(twlhd_frame_parse(g_v2, 176u, &v1_view),
                 TWLHD_PARSE_ERR_SIZE);
    CHECK_EQ_INT(twlhd_v2_parse(g_v2, v2_size, &v2_view), TWLHD_V2_OK);

    /* v1 cannot be read as v2: the version is checked before the size. */
    CHECK_EQ_INT(twlhd_v2_parse(g_v1, sizeof g_v1, &v2_view),
                 TWLHD_V2_ERR_VERSION);

    /* A short buffer that claims version 2 is truncated, not mislabelled. */
    put_le32(g_v2 + 0, TWLHD_V2_VERSION);
    CHECK_EQ_INT(twlhd_v2_parse(g_v2, 31u, &v2_view),
                 TWLHD_V2_ERR_TRUNCATED);

    /* The v1 parser still accepts the untouched v1 file. */
    CHECK_EQ_INT(twlhd_frame_parse(g_v1, sizeof g_v1, &v1_view),
                 TWLHD_PARSE_OK);
}

int main(void) {
    test_detect();
    test_report("v2-compat/detect");
    test_parsers_fail_closed();
    test_report("v2-compat/fail-closed");
    return test_exit_code();
}
