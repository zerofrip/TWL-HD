// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_vertex_convert.c.
 */

#include "twlhd_vertex_convert.h"

#include "tests_host.h"

#define MAX_CMDS 64
#define MAX_OUT  4096

/* Raw little-endian parameter storage keeps the tests independent of host
 * struct layout and alignment. */
static uint8_t g_param_storage[MAX_CMDS][16 * 4];
static TwlhdParsedFrame g_frame;
static TwlhdVertex g_out[MAX_OUT];

static void frame_reset(void) {
    memset(&g_frame, 0, sizeof g_frame);
}

static void add_cmd(uint32_t type, uint8_t count, const uint32_t *params) {
    TwlhdParsedCmd *cmd = &g_frame.cmds[g_frame.cmd_count];
    uint32_t i;

    cmd->type = type;
    cmd->param_count = count;
    cmd->params = g_param_storage[g_frame.cmd_count];

    for (i = 0; i < count && i < 16u; i++) {
        uint32_t value = params[i];
        g_param_storage[g_frame.cmd_count][i * 4 + 0] =
            (uint8_t)(value & 0xFFu);
        g_param_storage[g_frame.cmd_count][i * 4 + 1] =
            (uint8_t)((value >> 8) & 0xFFu);
        g_param_storage[g_frame.cmd_count][i * 4 + 2] =
            (uint8_t)((value >> 16) & 0xFFu);
        g_param_storage[g_frame.cmd_count][i * 4 + 3] =
            (uint8_t)((value >> 24) & 0xFFu);
    }

    g_frame.cmd_count++;
}

static void add_simple(uint32_t type, uint32_t value) {
    add_cmd(type, 1, &value);
}

static void add_vertex(uint32_t x, uint32_t y, uint32_t z) {
    uint32_t params[2];
    params[0] = (y << 16) | (x & 0xFFFFu);
    params[1] = z;
    add_cmd(TWLHD_CMD_VTX_16, 2, params);
}

static void begin(uint32_t mode) {
    add_simple(TWLHD_CMD_BEGIN_VTXS, mode);
}

static void end(void) {
    add_cmd(TWLHD_CMD_END_VTXS, 0, NULL);
}

static TwlhdConvertResult convert(uint32_t capacity, uint32_t *count,
                                  TwlhdConvertStats *stats) {
    return twlhd_convert_frame(&g_frame, g_out, capacity, count, stats);
}

static void test_position_decode(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    begin(0);
    add_vertex(0x1000u, 0xF000u, 0x0800u); /*  1, -1, 0.5  */
    add_vertex(0xF000u, 0x1000u, 0xF000u); /* -1,  1, -1   */
    add_vertex(0xFFFFu, 0x0000u, 0x0001u); /* -1/4096, 0, 1/4096 */
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(stats.triangles, 1);
    CHECK_EQ_INT(stats.input_vertices, 3);
    CHECK_NEAR(g_out[0].pos[0], 1.0, 1e-6);
    CHECK_NEAR(g_out[0].pos[1], -1.0, 1e-6);
    CHECK_NEAR(g_out[0].pos[2], 0.5, 1e-6);
    CHECK_NEAR(g_out[1].pos[0], -1.0, 1e-6);
    CHECK_NEAR(g_out[1].pos[1], 1.0, 1e-6);
    CHECK_NEAR(g_out[1].pos[2], -1.0, 1e-6);
    CHECK_NEAR(g_out[2].pos[0], -1.0 / 4096.0, 1e-9);
    CHECK_NEAR(g_out[2].pos[1], 0.0, 1e-9);
    CHECK_NEAR(g_out[2].pos[2], 1.0 / 4096.0, 1e-9);
}

static void test_attribute_defaults(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_NEAR(g_out[0].rgba[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[1], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[2], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[3], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].normal[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].normal[1], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].normal[2], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].uv[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].uv[1], 0.0, 1e-9);
}

static void test_color_decode(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    add_simple(TWLHD_CMD_COLOR, 0x0000u); /* black */
    begin(0);
    add_vertex(0, 0, 0);
    add_simple(TWLHD_CMD_COLOR, 0x7FFFu); /* 31,31,31 */
    add_vertex(0, 0, 0);
    add_simple(TWLHD_CMD_COLOR, 0x001Fu); /* 31,0,0 */
    add_vertex(0, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_NEAR(g_out[0].rgba[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[1], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[2], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[3], 1.0, 1e-9);
    CHECK_NEAR(g_out[1].rgba[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[1].rgba[1], 1.0, 1e-9);
    CHECK_NEAR(g_out[1].rgba[2], 1.0, 1e-9);
    CHECK_NEAR(g_out[2].rgba[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[2].rgba[1], 0.0, 1e-9);
    CHECK_NEAR(g_out[2].rgba[2], 0.0, 1e-9);
}

static void test_normal_decode(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    add_simple(TWLHD_CMD_NORMAL, 0x000001FFu); /* +X = +511/512 */
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_NEAR(g_out[0].normal[0], 511.0 / 512.0, 1e-7);
    CHECK_NEAR(g_out[0].normal[1], 0.0, 1e-7);
    CHECK_NEAR(g_out[0].normal[2], 0.0, 1e-7);

    /* The cube packs -1.0 as 0x201, decoding to -511/512 (documented). */
    frame_reset();
    add_simple(TWLHD_CMD_NORMAL, 0x00000201u);
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_NEAR(g_out[0].normal[0], -511.0 / 512.0, 1e-7);

    frame_reset();
    add_simple(TWLHD_CMD_NORMAL, 0x3FF00000u); /* z = 0x3FF = -1/512 */
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_NEAR(g_out[0].normal[2], -1.0 / 512.0, 1e-7);
}

static void test_texcoord_decode(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    add_simple(TWLHD_CMD_TEX_COORD, 0x00100010u);
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_NEAR(g_out[0].uv[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].uv[1], 1.0, 1e-9);

    frame_reset();
    add_simple(TWLHD_CMD_TEX_COORD, 0xFFF0FFF0u);
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_NEAR(g_out[0].uv[0], -1.0, 1e-9);
    CHECK_NEAR(g_out[0].uv[1], -1.0, 1e-9);
}

static void test_outside_begin(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    add_vertex(0x1000, 0, 0);
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();
    add_vertex(0x2000, 0, 0);

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(stats.input_vertices, 5);
    CHECK_EQ_INT(stats.ignored_outside_begin, 2);
    CHECK_EQ_INT(stats.triangles, 1);
}

static void test_triangle_list(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    begin(0);
    add_vertex(0x0000, 0, 0);
    add_vertex(0x1000, 0, 0);
    add_vertex(0x2000, 0, 0);
    add_vertex(0x3000, 0, 0);
    add_vertex(0x4000, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(stats.triangles, 1);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);
    CHECK_NEAR(g_out[0].pos[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[1].pos[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[2].pos[0], 2.0, 1e-9);
}

static void test_quad_list(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    begin(1);
    add_vertex(0x0000, 0, 0);
    add_vertex(0x1000, 0, 0);
    add_vertex(0x2000, 0, 0);
    add_vertex(0x3000, 0, 0);
    add_vertex(0x4000, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 6);
    CHECK_EQ_INT(stats.triangles, 2);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);
    CHECK_NEAR(g_out[0].pos[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[1].pos[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[2].pos[0], 2.0, 1e-9);
    CHECK_NEAR(g_out[3].pos[0], 0.0, 1e-9);
    CHECK_NEAR(g_out[4].pos[0], 2.0, 1e-9);
    CHECK_NEAR(g_out[5].pos[0], 3.0, 1e-9);
}

static void test_triangle_strip(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;
    static const float expected[] = {0, 1, 2, 2, 1, 3, 2, 3, 4};
    uint32_t i;

    frame_reset();
    begin(2);
    for (i = 0; i < 5; i++) add_vertex(i * 0x1000u, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 9);
    CHECK_EQ_INT(stats.triangles, 3);
    CHECK_EQ_INT(stats.dropped_incomplete, 0);
    for (i = 0; i < 9; i++) {
        CHECK_NEAR(g_out[i].pos[0], expected[i], 1e-9);
    }

    /* Two vertices cannot form a triangle. */
    frame_reset();
    begin(2);
    add_vertex(0x0000, 0, 0);
    add_vertex(0x1000, 0, 0);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 0);
    CHECK_EQ_INT(stats.triangles, 0);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);
}

static void test_quad_strip(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;
    static const float expected[] = {0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4};
    uint32_t i;

    frame_reset();
    begin(3);
    for (i = 0; i < 6; i++) add_vertex(i * 0x1000u, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 12);
    CHECK_EQ_INT(stats.triangles, 4);
    CHECK_EQ_INT(stats.dropped_incomplete, 0);
    for (i = 0; i < 12; i++) {
        CHECK_NEAR(g_out[i].pos[0], expected[i], 1e-9);
    }

    /* One pending column vertex is dropped. */
    frame_reset();
    begin(3);
    for (i = 0; i < 5; i++) add_vertex(i * 0x1000u, 0, 0);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 6);
    CHECK_EQ_INT(stats.triangles, 2);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);
}

static void test_incomplete_and_capacity(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0x1000, 0, 0);
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 0);
    CHECK_EQ_INT(stats.triangles, 0);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);

    /* Unclosed group at end of frame is also dropped, not lost silently. */
    frame_reset();
    begin(1);
    add_vertex(0, 0, 0);
    add_vertex(0x1000, 0, 0);
    add_vertex(0x2000, 0, 0);
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 0);
    CHECK_EQ_INT(stats.dropped_incomplete, 1);

    frame_reset();
    begin(0);
    add_vertex(0, 0, 0);
    add_vertex(0x1000, 0, 0);
    add_vertex(0x2000, 0, 0);
    end();
    CHECK_EQ_INT(convert(2, &count, &stats), TWLHD_CONVERT_ERR_CAPACITY);
    CHECK_EQ_INT(count, 0);
    CHECK_EQ_INT(convert(3, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(twlhd_convert_vertex_bound(10), 30);
    CHECK_EQ_INT(twlhd_convert_vertex_bound(0), 0);
}

static void test_unsupported_and_unknown(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;

    frame_reset();
    add_simple(TWLHD_CMD_MTX_MODE, 1);
    add_cmd(TWLHD_CMD_MTX_IDENTITY, 0, NULL);
    add_simple(TWLHD_CMD_VIEWPORT, 0x12345678u);
    add_simple(TWLHD_CMD_VTX_10, 0);
    add_simple(0x99u, 0);
    begin(0);
    add_simple(TWLHD_CMD_MTX_MULT_3x3, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    add_vertex(0, 0, 0);
    end();
    begin(9); /* invalid primitive mode */
    end();

    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(stats.unsupported_matrix, 3);
    CHECK_EQ_INT(stats.unsupported_viewport, 1);
    CHECK_EQ_INT(stats.unsupported_vertex, 1);
    /* 0x99, invalid BEGIN mode */
    CHECK_EQ_INT(stats.unsupported_unknown, 2);
    CHECK_EQ_INT(stats.triangles, 1);
}

static void test_bounds_and_empty(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;
    float min[3] = {0}, max[3] = {0};
    uint32_t i;

    CHECK_EQ_INT(twlhd_vertex_bounds(NULL, 0, min, max), 0);
    CHECK_EQ_INT(twlhd_vertex_bounds(g_out, 0, min, max), 0);

    frame_reset();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 0);
    CHECK_EQ_INT(stats.triangles, 0);

    frame_reset();
    begin(0);
    for (i = 0; i < 3; i++) add_vertex(0x1000, 0xF000, 0x2000);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(twlhd_vertex_bounds(g_out, count, min, max), 1);
    for (i = 0; i < 3; i++) {
        CHECK_NEAR(min[i], max[i], 1e-9);
    }
    CHECK_NEAR(min[0], 1.0, 1e-9);
    CHECK_NEAR(min[1], -1.0, 1e-9);
    CHECK_NEAR(min[2], 2.0, 1e-9);

    frame_reset();
    begin(0);
    add_vertex(0xF000, 0x1000, 0xE000);
    add_vertex(0x1000, 0x3000, 0x2000);
    add_vertex(0x0000, 0x2000, 0x0000);
    end();
    CHECK_EQ_INT(convert(MAX_OUT, &count, &stats), TWLHD_CONVERT_OK);
    CHECK_EQ_INT(twlhd_vertex_bounds(g_out, count, min, max), 1);
    CHECK_NEAR(min[0], -1.0, 1e-9);
    CHECK_NEAR(min[1], 1.0, 1e-9);
    CHECK_NEAR(min[2], -2.0, 1e-9);
    CHECK_NEAR(max[0], 1.0, 1e-9);
    CHECK_NEAR(max[1], 3.0, 1e-9);
    CHECK_NEAR(max[2], 2.0, 1e-9);

    CHECK_EQ_INT(twlhd_convert_frame(NULL, g_out, MAX_OUT, &count, &stats),
                 TWLHD_CONVERT_ERR_NULL);
    frame_reset();
    CHECK_EQ_INT(twlhd_convert_frame(&g_frame, NULL, MAX_OUT, &count, &stats),
                 TWLHD_CONVERT_ERR_NULL);
}

int main(void) {
    test_position_decode();
    test_report("converter/position-decode");
    test_attribute_defaults();
    test_report("converter/attribute-defaults");
    test_color_decode();
    test_report("converter/color-decode");
    test_normal_decode();
    test_report("converter/normal-decode");
    test_texcoord_decode();
    test_report("converter/texcoord-decode");
    test_outside_begin();
    test_report("converter/outside-begin");
    test_triangle_list();
    test_report("converter/triangle-list");
    test_quad_list();
    test_report("converter/quad-list");
    test_triangle_strip();
    test_report("converter/triangle-strip");
    test_quad_strip();
    test_report("converter/quad-strip");
    test_incomplete_and_capacity();
    test_report("converter/incomplete-and-capacity");
    test_unsupported_and_unknown();
    test_report("converter/unsupported-and-unknown");
    test_bounds_and_empty();
    test_report("converter/bounds-and-empty");
    return test_exit_code();
}
