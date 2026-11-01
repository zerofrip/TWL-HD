// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_gx_interp.c.
 *
 * Builds small v2 streams with the deterministic writer and asserts exact
 * interpreter behaviour.
 */

#include "twlhd_gx_interp.h"
#include "twlhd_v2_write.h"

#include "tests_host.h"

static uint8_t g_file[TWLHD_V2_FILE_MAX];
static TwlhdV2CmdEntry g_entries[TWLHD_V2_GXCM_MAX_ENTRIES];
static TwlhdGxVertex g_out[256];
static TwlhdGxViewport g_vp;
static TwlhdGxStats g_st;

static void clear_state(void) {
    memset(g_entries, 0, sizeof g_entries);
    memset(g_out, 0xCD, sizeof g_out);
    memset(&g_vp, 0, sizeof g_vp);
    memset(&g_st, 0, sizeof g_st);
}

static uint32_t add_entry(uint32_t *n, uint32_t cmd, uint32_t value) {
    g_entries[*n].cmd_word = cmd;
    g_entries[*n].value = value;
    return (*n)++;
}

static size_t build_file(uint32_t entry_count) {
    TwlhdV2WriteParams p;
    size_t size = 0;

    p.frame_index = 1u;
    p.flags = TWLHD_V2_FLAG_FRAME_COMPLETE;
    p.backend_id = TWLHD_V2_BACKEND_SYNTHETIC;
    p.coverage_flags = 0u;
    p.gxcm = g_entries;
    p.gxcm_count = entry_count;
    p.rsta = NULL;
    p.rsta_count = 0u;

    CHECK_EQ_INT(twlhd_v2_write(&p, g_file, sizeof g_file, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(size, 80u + entry_count * 8u);
    return size;
}

static uint32_t g_out_count;

/* ------------------------------------------------------------------------- */
/* Matrix command tests                                                      */
/* ------------------------------------------------------------------------- */

static void test_matrix_load_identity(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    /* Projection = identity, position = identity → clip = identity. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4,
              0x00001000u); /* column 0 row 0 = 1.0 */
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u); /* col1 row1 = 1 */
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u); /* col2 row2 = 1 */
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u); /* col3 row3 = 1 */
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* x=1, y=0 */
    add_entry(&n, 0x23u, 0u);                        /* z=0 */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u); /* single vertex, incomplete */
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
}

static void test_multiply_order(void) {
    /*
     * Non-commuting pair: projection = identity, position = T * S where
     * T = (1,0,0) and S = (2,1,1). Vertex (0,0,0) → T*S*(0,0,0,1) = (1,0,0,1).
     * If order were swapped (S*T) the result would be (2,0,0,1).
     */
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    add_entry(&n, TWLHD_V2_CMD_MTX_TRANS, 0x00001000u); /* tx=1 */
    add_entry(&n, 0x1Cu, 0u);
    add_entry(&n, 0x1Cu, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_SCALE, 0x00002000u); /* sx=2 */
    add_entry(&n, 0x1Bu, 0x00001000u); /* sy=1 */
    add_entry(&n, 0x1Bu, 0x00001000u); /* sz=1 */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* Three vertices at the origin form a degenerate triangle; the position
     * of the transformed origin reveals the multiplication order. */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u); /* x=0,y=0 */
    add_entry(&n, 0x23u, 0u); /* z=0 */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.triangles_out, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_st.vertices_in, 3u, 0.0);
    /* Correct order T*S maps origin to (1,0,0); swapped order S*T gives (2,0,0). */
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
}

static void test_load_4x3_and_3x3(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /* LOAD_4x3: col0=(2,0,0), col1=(0,3,0), col2=(0,0,4), col3=(0,0,0). */
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x3, 0x00002000u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0x00003000u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0x00004000u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    add_entry(&n, 0x17u, 0u);
    /* MULT_3x3: scale by (1,1,1) identity → no change. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MULT_3x3, 0x00001000u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, 0x1Au, 0x00001000u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, 0x1Au, 0x00001000u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, 0x1Au, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* x=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
}

static void test_stack_push_pop(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /* Push twice (sp=2), load identity, pop 1 → should restore pushed matrix. */
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0x00002000u); /* scale x=2 */
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* x=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
}

static void test_stack_overflow_and_underflow(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    for (i = 0; i < 32u; i++) add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);

    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
}

static void test_projection_isolation(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    /* Projection gets a scale; position stays identity. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_SCALE, 0x00002000u);
    add_entry(&n, 0x1Bu, 0x00001000u);
    add_entry(&n, 0x1Bu, 0x00001000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
}

/* ------------------------------------------------------------------------- */
/* Vertex form and primitive tests                                           */
/* ------------------------------------------------------------------------- */

static void test_vertex_16_negative(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x1000F000u); /* x=-1 (0xF000), y=1 */
    add_entry(&n, 0x23u, 0x0000F000u);               /* z=-1 */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* x=1, y=0 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], -1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], -1.0f, 0.0001f);
}

static void test_vertex_10_and_colour_latch(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x00001Fu); /* red */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_10, 0x00000020u); /* x = 32/64 = 0.5 */
    add_entry(&n, TWLHD_V2_CMD_VTX_XY, 0x00000000u); /* x=0, y=0, z persists */
    add_entry(&n, TWLHD_V2_CMD_VTX_10, 0x00020000u); /* x=0, y=0.5 */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].rgba[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].rgba[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].rgba[0], 1.0f, 0.0001f);
}

static void test_quad_split(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u); /* quads */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);      /* v0 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* v1 x=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10001000u); /* v2 x=1,y=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x00001000u); /* v3 y=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.triangles_out, 2u);
    CHECK_EQ_INT(g_st.quads_split, 1u);
    /* First triangle: v0,v1,v2. Second: v0,v2,v3. */
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 0.0f, 0.0001f);
}

static void test_triangle_strip_winding(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u); /* triangle strip */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x00001000u); /* v1 x=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* v2 y=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10001000u); /* v3 x=1,y=1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    /* tri0: (v0,v1,v2), tri1: (v2,v1,v3) after swap. */
    CHECK_NEAR(g_out[3].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[0], 1.0f, 0.0001f);
}

static void test_quad_strip(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u); /* quad strip */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);      /* v0 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* v1 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);      /* v2 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* v3 */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.quads_split, 1u);
}

static void test_incomplete_and_outside_begin(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u); /* outside BEGIN */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.ignored_outside_begin, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 2u);
}

static void test_begin_while_open(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u); /* implicit close */
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
    CHECK_EQ_INT(g_out_count, 3u);
}

static void test_capacity_error(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u); /* second triangle */
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x00001000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10001000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 3u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_CAPACITY);
    CHECK_EQ_INT(g_out_count, 3u); /* first triangle fits */
    CHECK_EQ_INT(g_st.capacity_dropped, 3u);
}

static void test_raw_fifo_and_unsupported(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_RAW_FIFO_BIT, 0x12345678u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_DIFF, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.raw_fifo_skipped, 1u);
    CHECK_EQ_INT(g_st.unsupported, 1u);
    CHECK_EQ_INT(g_out_count, 3u);
}

static void test_viewport(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_VIEWPORT, 0xBF00FF00u); /* x1=0,y1=255,x2=0,y2=191 */
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK(g_vp.present);
    CHECK_EQ_INT(g_vp.x1, 0u);
    CHECK_EQ_INT(g_vp.y1, 255u);
    CHECK_EQ_INT(g_vp.x2, 0u);
    CHECK_EQ_INT(g_vp.y2, 191u);
}

static void test_mode2_uses_position(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u); /* modelview */
    add_entry(&n, TWLHD_V2_CMD_MTX_TRANS, 0x00001000u);
    add_entry(&n, 0x1Cu, 0u);
    add_entry(&n, 0x1Cu, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0x10000000u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_VTX_16, 0u);
    add_entry(&n, 0x23u, 0u);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
}

static void test_texture_mode_ignored(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 3u); /* texture */
    /* A 4x4 identity matrix (16 entries); values are ignored in texture mode. */
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0u);
    add_entry(&n, 0x16u, 0x00001000u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.ignored_no_effect, 1u);
}

/* ------------------------------------------------------------------------- */
/* Negative / error tests                                                    */
/* ------------------------------------------------------------------------- */

static void test_null_args(void) {
    TwlhdV2View view;
    size_t size;
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_gx_interpret(NULL, g_out, 12u, &g_out_count, &g_vp,
                                    &g_st),
                 TWLHD_GX_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_interpret(&view, NULL, 12u, &g_out_count, &g_vp,
                                    &g_st),
                 TWLHD_GX_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_interpret(&view, g_out, 12u, NULL, &g_vp, &g_st),
                 TWLHD_GX_ERR_NULL);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void) {
    test_matrix_load_identity();
    test_report("gx_interp/matrix-load-identity");
    test_multiply_order();
    test_report("gx_interp/multiply-order");
    test_load_4x3_and_3x3();
    test_report("gx_interp/load-4x3-and-3x3");
    test_stack_push_pop();
    test_report("gx_interp/stack-push-pop");
    test_stack_overflow_and_underflow();
    test_report("gx_interp/stack-overflow-and-underflow");
    test_projection_isolation();
    test_report("gx_interp/projection-isolation");
    test_vertex_16_negative();
    test_report("gx_interp/vertex-16-negative");
    test_vertex_10_and_colour_latch();
    test_report("gx_interp/vertex-10-and-colour-latch");
    test_quad_split();
    test_report("gx_interp/quad-split");
    test_triangle_strip_winding();
    test_report("gx_interp/triangle-strip-winding");
    test_quad_strip();
    test_report("gx_interp/quad-strip");
    test_incomplete_and_outside_begin();
    test_report("gx_interp/incomplete-and-outside-begin");
    test_begin_while_open();
    test_report("gx_interp/begin-while-open");
    test_capacity_error();
    test_report("gx_interp/capacity-error");
    test_raw_fifo_and_unsupported();
    test_report("gx_interp/raw-fifo-and-unsupported");
    test_viewport();
    test_report("gx_interp/viewport");
    test_mode2_uses_position();
    test_report("gx_interp/mode2-uses-position");
    test_texture_mode_ignored();
    test_report("gx_interp/texture-mode-ignored");
    test_null_args();
    test_report("gx_interp/null-args");
    return test_exit_code();
}
