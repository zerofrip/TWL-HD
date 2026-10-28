// SPDX-License-Identifier: Apache-2.0
/*
 * Round-trip test: the exact steady-state cube frame from
 * arm9/source/main.c:69-88 (40 commands, 24 VTX_16) must parse and expand to
 * 12 triangles.
 */

#include "twlhd_frame_parse.h"
#include "twlhd_vertex_convert.h"

#include "tests_host.h"

#define V16_ONE ((int16_t)(1 << 12))

static const int16_t cube_vertices[8][3] = {
    {-V16_ONE, -V16_ONE,  V16_ONE},
    { V16_ONE, -V16_ONE,  V16_ONE},
    { V16_ONE,  V16_ONE,  V16_ONE},
    {-V16_ONE,  V16_ONE,  V16_ONE},
    {-V16_ONE, -V16_ONE, -V16_ONE},
    { V16_ONE, -V16_ONE, -V16_ONE},
    { V16_ONE,  V16_ONE, -V16_ONE},
    {-V16_ONE,  V16_ONE, -V16_ONE},
};

static const uint8_t cube_faces[6][4] = {
    {0, 1, 2, 3},
    {5, 4, 7, 6},
    {4, 0, 3, 7},
    {1, 5, 6, 2},
    {3, 2, 6, 7},
    {4, 5, 1, 0},
};

static const uint32_t cube_normals[6] = {
    0x1FF00000u, 0x20100000u, 0x00000201u,
    0x000001FFu, 0x0007FC00u, 0x00080400u,
};

static const uint8_t cube_colors[6][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
    {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
};

static uint8_t g_frame[TWLHD_FRAME_WIRE_SIZE];
static uint32_t g_param_storage[64][16];
static TwlhdParsedFrame g_parsed;
static TwlhdVertex g_out[3 * TWLHD_RING_CAPACITY];

static uint32_t g_cmd_index;

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void add_cmd(uint32_t type, uint8_t count, const uint32_t *params) {
    uint8_t *slot = g_frame + TWLHD_FRAME_HEADER_SIZE +
                    (size_t)g_cmd_index * TWLHD_CMD_WIRE_SIZE;
    uint32_t i;

    put_le32(slot, type);
    slot[4] = count;
    for (i = 0; i < count; i++) {
        g_param_storage[g_cmd_index][i] = params[i];
        put_le32(slot + 8 + i * 4u, params[i]);
    }
    g_cmd_index++;
}

static void add_simple(uint32_t type, uint32_t value) {
    add_cmd(type, 1, &value);
}

static void add_vertex(const int16_t v[3]) {
    uint32_t params[2];
    params[0] = ((uint32_t)(uint16_t)v[1] << 16) | ((uint16_t)v[0] & 0xFFFFu);
    params[1] = (uint32_t)(uint16_t)v[2];
    add_cmd(TWLHD_CMD_VTX_16, 2, params);
}

static void build_cube_frame(void) {
    uint32_t mode = 1; /* GL_QUADS */
    int face;
    int corner;

    memset(g_frame, 0, sizeof g_frame);
    memset(g_param_storage, 0, sizeof g_param_storage);
    put_le32(g_frame + 0, TWLHD_FORMAT_VERSION);
    put_le32(g_frame + 4, 59);
    g_cmd_index = 0;

    add_simple(TWLHD_CMD_MTX_MODE, 2);      /* modelview */
    add_cmd(TWLHD_CMD_MTX_IDENTITY, 0, NULL);
    add_cmd(TWLHD_CMD_BEGIN_VTXS, 1, &mode);

    for (face = 0; face < 6; face++) {
        uint32_t packed_color = (uint32_t)(cube_colors[face][0] >> 3) |
                                ((uint32_t)(cube_colors[face][1] >> 3) << 5) |
                                ((uint32_t)(cube_colors[face][2] >> 3) << 10);

        add_simple(TWLHD_CMD_NORMAL, cube_normals[face]);
        add_simple(TWLHD_CMD_COLOR, packed_color);
        for (corner = 0; corner < 4; corner++) {
            add_vertex(cube_vertices[cube_faces[face][corner]]);
        }
    }

    add_cmd(TWLHD_CMD_END_VTXS, 0, NULL);
    put_le32(g_frame + 8, g_cmd_index);
}

int main(void) {
    uint32_t count = 0;
    TwlhdConvertStats stats;
    TwlhdParseResult parsed;
    uint32_t i;

    build_cube_frame();
    CHECK_EQ_INT(g_cmd_index, 40);

    parsed = twlhd_frame_parse(g_frame, sizeof g_frame, &g_parsed);
    CHECK_EQ_INT(parsed, TWLHD_PARSE_OK);

    CHECK_EQ_INT(twlhd_convert_frame(&g_parsed, g_out,
                                     twlhd_convert_vertex_bound(g_parsed.cmd_count),
                                     &count, &stats),
                 TWLHD_CONVERT_OK);
    CHECK_EQ_INT(g_parsed.frame_index, 59);
    CHECK_EQ_INT(g_parsed.cmd_count, 40);
    CHECK_EQ_INT(stats.input_vertices, 24);
    CHECK_EQ_INT(stats.triangles, 12);
    CHECK_EQ_INT(stats.dropped_incomplete, 0);
    CHECK_EQ_INT(stats.ignored_outside_begin, 0);
    CHECK_EQ_INT(stats.unsupported_matrix, 2);
    CHECK_EQ_INT(count, 36);

    /* Front face: (0,1,2), color red, normal +Z. */
    CHECK_NEAR(g_out[0].pos[0], -1.0, 1e-6);
    CHECK_NEAR(g_out[0].pos[1], -1.0, 1e-6);
    CHECK_NEAR(g_out[0].pos[2], 1.0, 1e-6);
    CHECK_NEAR(g_out[1].pos[0], 1.0, 1e-6);
    CHECK_NEAR(g_out[2].pos[1], 1.0, 1e-6);
    CHECK_NEAR(g_out[0].rgba[0], 1.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[1], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].rgba[2], 0.0, 1e-9);
    CHECK_NEAR(g_out[0].normal[2], 511.0 / 512.0, 1e-7);

    /* Every output vertex must carry a unit-length-ish color alpha. */
    for (i = 0; i < count; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 1.0, 1e-9);
    }

    test_report("roundtrip/cube-24-vertices-12-triangles");
    return test_exit_code();
}
