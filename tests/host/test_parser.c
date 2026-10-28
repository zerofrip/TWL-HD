// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_frame_parse.c.
 */

#include "twlhd_frame_parse.h"

#include "tests_host.h"

static uint8_t g_frame[TWLHD_FRAME_WIRE_SIZE];

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void frame_init(uint32_t version, uint32_t frame_index,
                       uint32_t cmd_count) {
    memset(g_frame, 0, sizeof g_frame);
    put_le32(g_frame + 0, version);
    put_le32(g_frame + 4, frame_index);
    put_le32(g_frame + 8, cmd_count);
}

static uint8_t *cmd_slot(uint32_t index) {
    return g_frame + TWLHD_FRAME_HEADER_SIZE + (size_t)index * TWLHD_CMD_WIRE_SIZE;
}

static void put_cmd(uint32_t index, uint32_t type, uint8_t param_count,
                    const uint32_t *params) {
    uint8_t *slot = cmd_slot(index);
    uint32_t i;

    memset(slot, 0, TWLHD_CMD_WIRE_SIZE);
    put_le32(slot, type);
    slot[4] = param_count;
    for (i = 0; i < param_count && i < 16u; i++) {
        put_le32(slot + 8 + i * 4u, params[i]);
    }
}

static void test_null_and_size(void) {
    TwlhdParsedFrame parsed;
    static const size_t bad_sizes[] = {0, 1, 11, 12, 73739, 73741};
    size_t i;

    frame_init(TWLHD_FORMAT_VERSION, 0, 0);

    CHECK_EQ_INT(twlhd_frame_parse(NULL, TWLHD_FRAME_WIRE_SIZE, &parsed),
                 TWLHD_PARSE_ERR_NULL);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, TWLHD_FRAME_WIRE_SIZE, NULL),
                 TWLHD_PARSE_ERR_NULL);

    for (i = 0; i < sizeof bad_sizes / sizeof bad_sizes[0]; i++) {
        CHECK_EQ_INT(twlhd_frame_parse(g_frame, bad_sizes[i], &parsed),
                     TWLHD_PARSE_ERR_SIZE);
    }

    CHECK_EQ_INT(twlhd_frame_parse(g_frame, TWLHD_FRAME_WIRE_SIZE, &parsed),
                 TWLHD_PARSE_OK);
}

static void test_version(void) {
    TwlhdParsedFrame parsed;

    frame_init(0, 1, 0);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_VERSION);

    frame_init(2, 1, 0);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_VERSION);

    frame_init(TWLHD_FORMAT_VERSION, 1, 0);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_OK);
}

static void test_cmd_count(void) {
    TwlhdParsedFrame parsed;
    uint32_t i;

    frame_init(TWLHD_FORMAT_VERSION, 0, TWLHD_RING_CAPACITY);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_OK);
    CHECK_EQ_INT(parsed.cmd_count, TWLHD_RING_CAPACITY);
    for (i = 0; i < TWLHD_RING_CAPACITY; i++) {
        CHECK_EQ_INT(parsed.cmds[i].param_count, 0);
    }

    frame_init(TWLHD_FORMAT_VERSION, 0, TWLHD_RING_CAPACITY + 1);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_CMD_COUNT);
}

static void test_param_counts(void) {
    TwlhdParsedFrame parsed;
    uint32_t params[16] = {0};
    uint32_t i;

    frame_init(TWLHD_FORMAT_VERSION, 0, 1);

    put_cmd(0, TWLHD_CMD_COLOR, 17, params);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_PARAM_COUNT);

    /* Known type with the wrong (but representable) count. */
    put_cmd(0, TWLHD_CMD_COLOR, 0, params);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_PARAM_COUNT);

    put_cmd(0, TWLHD_CMD_VTX_16, 1, params);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_PARAM_COUNT);

    /* Unknown type with any count <= 16 is tolerated. */
    put_cmd(0, 0x99u, 16, params);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_OK);
    CHECK_EQ_INT(parsed.cmds[0].type, 0x99u);
    CHECK_EQ_INT(parsed.cmds[0].param_count, 16);

    put_cmd(0, 0x99u, 17, params);
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_ERR_PARAM_COUNT);

    /* Every known type with its table count must be accepted. */
    frame_init(TWLHD_FORMAT_VERSION, 0, 22);
    {
        static const uint32_t types[] = {
            TWLHD_CMD_MTX_MODE, TWLHD_CMD_MTX_PUSH, TWLHD_CMD_MTX_POP,
            TWLHD_CMD_MTX_STORE, TWLHD_CMD_MTX_RESTORE,
            TWLHD_CMD_MTX_IDENTITY, TWLHD_CMD_MTX_LOAD_4x4,
            TWLHD_CMD_MTX_LOAD_4x3, TWLHD_CMD_MTX_MULT_4x4,
            TWLHD_CMD_MTX_MULT_4x3, TWLHD_CMD_MTX_MULT_3x3,
            TWLHD_CMD_COLOR, TWLHD_CMD_NORMAL, TWLHD_CMD_TEX_COORD,
            TWLHD_CMD_VTX_16, TWLHD_CMD_VTX_10, TWLHD_CMD_VTX_XY,
            TWLHD_CMD_VTX_XZ, TWLHD_CMD_VTX_YZ, TWLHD_CMD_BEGIN_VTXS,
            TWLHD_CMD_END_VTXS, TWLHD_CMD_VIEWPORT,
        };
        uint8_t expected;

        for (i = 0; i < sizeof types / sizeof types[0]; i++) {
            CHECK_EQ_INT(twlhd_cmd_type_param_count(types[i], &expected), 1);
            put_cmd(i, types[i], expected, params);
        }
    }
    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_OK);
    CHECK_EQ_INT(parsed.cmd_count, 22);

    {
        uint8_t count = 55;
        CHECK_EQ_INT(twlhd_cmd_type_param_count(TWLHD_CMD_UNKNOWN, &count), 0);
        CHECK_EQ_INT(twlhd_cmd_type_param_count(0x99u, &count), 0);
    }
}

static void test_field_offsets(void) {
    TwlhdParsedFrame parsed;
    uint32_t params[16] = {0};

    frame_init(TWLHD_FORMAT_VERSION, 0x11223344u, 2);

    params[0] = 0xABCDEF01u;
    put_cmd(0, TWLHD_CMD_COLOR, 1, params);

    params[0] = 0x12345678u;
    params[1] = 0x9ABCDEF0u;
    put_cmd(1, TWLHD_CMD_VTX_16, 2, params);

    CHECK_EQ_INT(twlhd_frame_parse(g_frame, sizeof g_frame, &parsed),
                 TWLHD_PARSE_OK);
    CHECK_EQ_INT(parsed.version, TWLHD_FORMAT_VERSION);
    CHECK_EQ_INT(parsed.frame_index, 0x11223344u);
    CHECK_EQ_INT(parsed.cmd_count, 2);
    CHECK_EQ_INT(parsed.cmds[0].type, TWLHD_CMD_COLOR);
    CHECK_EQ_INT(parsed.cmds[0].param_count, 1);
    CHECK_EQ_INT(twlhd_cmd_param(&parsed.cmds[0], 0), 0xABCDEF01u);
    CHECK_EQ_INT(parsed.cmds[1].type, TWLHD_CMD_VTX_16);
    CHECK_EQ_INT(parsed.cmds[1].param_count, 2);
    CHECK_EQ_INT(twlhd_cmd_param(&parsed.cmds[1], 0), 0x12345678u);
    CHECK_EQ_INT(twlhd_cmd_param(&parsed.cmds[1], 1), 0x9ABCDEF0u);
    CHECK_EQ_INT(twlhd_cmd_param(&parsed.cmds[1], 16), 0);
    CHECK_EQ_INT(twlhd_cmd_param(NULL, 0), 0);
}

static void test_result_strings(void) {
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_OK), "OK") == 0);
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_ERR_SIZE),
                 "ERR_SIZE") == 0);
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_ERR_VERSION),
                 "ERR_VERSION") == 0);
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_ERR_CMD_COUNT),
                 "ERR_CMD_COUNT") == 0);
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_ERR_PARAM_COUNT),
                 "ERR_PARAM_COUNT") == 0);
    CHECK(strcmp(twlhd_parse_result_string(TWLHD_PARSE_ERR_NULL),
                 "ERR_NULL") == 0);
    CHECK(twlhd_parse_result_string((TwlhdParseResult)999) != NULL);
}

int main(void) {
    test_null_and_size();
    test_report("parser/null-and-size");
    test_version();
    test_report("parser/version");
    test_cmd_count();
    test_report("parser/cmd-count");
    test_param_counts();
    test_report("parser/param-counts");
    test_field_offsets();
    test_report("parser/field-offsets");
    test_result_strings();
    test_report("parser/result-strings");
    return test_exit_code();
}
