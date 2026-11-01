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
/* Helpers for common command sequences                                      */
/* ------------------------------------------------------------------------- */

static void add_load_4x4(uint32_t *n, const uint32_t *m) {
    uint32_t i;
    add_entry(n, TWLHD_V2_CMD_MTX_LOAD_4x4, m[0]);
    for (i = 1; i < 16u; i++) {
        add_entry(n, TWLHD_V2_CMD_MTX_LOAD_4x4, m[i]);
    }
}

static void add_mult_4x4(uint32_t *n, const uint32_t *m) {
    uint32_t i;
    add_entry(n, TWLHD_V2_CMD_MTX_MULT_4x4, m[0]);
    for (i = 1; i < 16u; i++) {
        add_entry(n, TWLHD_V2_CMD_MTX_MULT_4x4, m[i]);
    }
}

static void add_load_4x3(uint32_t *n, const uint32_t *m) {
    uint32_t i;
    add_entry(n, TWLHD_V2_CMD_MTX_LOAD_4x3, m[0]);
    for (i = 1; i < 12u; i++) {
        add_entry(n, TWLHD_V2_CMD_MTX_LOAD_4x3, m[i]);
    }
}

static void add_mult_3x3(uint32_t *n, const uint32_t *m) {
    uint32_t i;
    add_entry(n, TWLHD_V2_CMD_MTX_MULT_3x3, m[0]);
    for (i = 1; i < 9u; i++) {
        add_entry(n, TWLHD_V2_CMD_MTX_MULT_3x3, m[i]);
    }
}

static void add_scale(uint32_t *n, uint32_t sx, uint32_t sy, uint32_t sz) {
    add_entry(n, TWLHD_V2_CMD_MTX_SCALE, sx);
    add_entry(n, TWLHD_V2_CMD_MTX_SCALE, sy);
    add_entry(n, TWLHD_V2_CMD_MTX_SCALE, sz);
}

static void add_trans(uint32_t *n, uint32_t tx, uint32_t ty, uint32_t tz) {
    add_entry(n, TWLHD_V2_CMD_MTX_TRANS, tx);
    add_entry(n, TWLHD_V2_CMD_MTX_TRANS, ty);
    add_entry(n, TWLHD_V2_CMD_MTX_TRANS, tz);
}

static void add_vtx_16(uint32_t *n, int32_t x, int32_t y, int32_t z) {
    add_entry(n, TWLHD_V2_CMD_VTX_16,
              ((uint32_t)(uint16_t)x) | ((uint32_t)(uint16_t)y << 16));
    add_entry(n, TWLHD_V2_CMD_VTX_16, (uint32_t)(uint16_t)z);
}

static void add_vtx_10(uint32_t *n, int32_t x, int32_t y, int32_t z) {
    uint32_t w = ((uint32_t)(x & 0x3FFu)) |
                 ((uint32_t)(y & 0x3FFu) << 10) |
                 ((uint32_t)(z & 0x3FFu) << 20);
    add_entry(n, TWLHD_V2_CMD_VTX_10, w);
}

static void add_vtx_xy(uint32_t *n, int32_t x, int32_t y) {
    add_entry(n, TWLHD_V2_CMD_VTX_XY,
              ((uint32_t)(uint16_t)x) | ((uint32_t)(uint16_t)y << 16));
}

static void add_vtx_xz(uint32_t *n, int32_t x, int32_t z) {
    add_entry(n, TWLHD_V2_CMD_VTX_XZ,
              ((uint32_t)(uint16_t)x) | ((uint32_t)(uint16_t)z << 16));
}

static void add_vtx_yz(uint32_t *n, int32_t y, int32_t z) {
    add_entry(n, TWLHD_V2_CMD_VTX_YZ,
              ((uint32_t)(uint16_t)y) | ((uint32_t)(uint16_t)z << 16));
}

/* ------------------------------------------------------------------------- */
/* Matrix command tests                                                      */
/* ------------------------------------------------------------------------- */

static void test_matrix_load_identity(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    /* Projection = identity, position = identity -> clip = identity. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    {
        const uint32_t id[16] = {
            0x1000u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0u, 0u, 0u, 0x1000u,
        };
        add_load_4x4(&n, id);
    }
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* Degenerate triangle at (1,0,0). */
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

static void test_multiply_order(void) {
    /*
     * Non-commuting pair: position = T * S where T = (1,0,0) and S = (2,1,1).
     * Vertex (0,0,0) -> T*S*(0,0,0,1) = (1,0,0,1).
     * If order were swapped (S*T) the result would be (2,0,0,1).
     */
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    add_trans(&n, 0x1000u, 0u, 0u);           /* tx=1 */
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u); /* sx=2, sy=1, sz=1 */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* Three vertices at the origin form a degenerate triangle. */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.triangles_out, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

static void test_load_4x3_and_3x3(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /*
     * LOAD_4x3: column-major 3 rows x 4 columns.
     * col0=(2,0,0), col1=(0,1,0), col2=(0,0,1), col3=(1,0,0).
     * 4x3 params are ordered col0row0, col0row1, col0row2, col1row0, ...
     */
    {
        const uint32_t m[12] = {
            0x2000u, 0u, 0u,       /* col0 */
            0u, 0x1000u, 0u,       /* col1 */
            0u, 0u, 0x1000u,       /* col2 */
            0x1000u, 0u, 0u,       /* col3 */
        };
        add_load_4x3(&n, m);
    }
    /*
     * MULT_3x3: column-major 3x3 in the upper-left 3x3.
     * col0=(1,1,0), col1=(0,1,0), col2=(0,0,1).
     * This maps (1,0,0) -> (1,1,0).
     */
    {
        const uint32_t m[9] = {
            0x1000u, 0x1000u, 0u,  /* col0 */
            0u, 0x1000u, 0u,       /* col1 */
            0u, 0u, 0x1000u,       /* col2 */
        };
        add_mult_3x3(&n, m);
    }
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    /*
     * Result = M * N * (1,0,0,1).
     * N * (1,0,0,1) = (1,1,0,1).
     * M * (1,1,0,1) = col0 + col1 + col3 = (2+0+1, 0+1+0, 0+0+0, 1)
     *               = (3, 1, 0, 1).
     */
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

static void test_stack_push_pop(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /* Push A (scale x=2), load identity, scale B (scale x=3), push B,
     * load identity, pop 1 -> must restore B, not A. */
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);
}

static void test_stack_pop_depths(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    /* push A (sx=2), push B (sx=3), pop 1 -> B. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);

    /* push A, push B, pop 2 -> A. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 2u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);

    /* STORE 0 then pop to depth 0 returns the stored matrix. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u); /* sp=1 so pop is legal. */
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_scale(&n, 0x4000u, 0x1000u, 0x1000u); /* C = scale x=4 */
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 0u); /* overwrite stack[0] with C. */
    add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u); /* pop to depth 0 -> stack[0]=C. */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 4.0f, 0.0001f);
}

static void test_stack_store_restore_index(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    /* STORE/RESTORE at index 30 succeeds. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 30u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_RESTORE, 30u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);

    /* STORE/RESTORE at index 31 -> ERR_STACK. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 31u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);

    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /* Need at least one valid store to make RESTORE 31 reach the check. */
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_RESTORE, 31u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);

    /* 32 pushes on the 31-entry position stack -> overflow on the 32nd. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    for (i = 0; i < 32u; i++) add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
}

static void test_stack_overflow_and_underflow(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
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
    /* Projection gets a scale x=2; position stays identity. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);
}

static void test_projection_stack_ignores_param(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    /* Projection stack is 1 deep; POP/STORE/RESTORE ignore the parameter. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    /* POP with n=2 should still restore slot 0 (scale x=2), not error. */
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 2u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);

    /* STORE 1 and RESTORE 1 are valid in projection mode (ignored -> slot 0). */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 1u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_RESTORE, 1u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);
}

static void test_trans_and_scale_by_value(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    /* position = T * S (interpreter applies operand on the right). */
    add_trans(&n, 0x1000u, 0, 0);            /* T = (1,0,0) */
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u); /* S = (2,1,1) */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* Vertex (1,1,1) -> T*S*(1,1,1,1) = T*(2,1,1,1) = (3,1,1,1). */
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
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
    add_vtx_16(&n, -0x1000, 0x1000, -0x1000); /* x=-1, y=1, z=-1 */
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
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

static void test_vertex_10_negative(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* x=-1.0 in 10.6 fixed point is raw -64 = 0x3C0. */
    add_vtx_10(&n, 0x3C0, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);  /* x=0.5 (32/64) */
    add_vtx_10(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], -1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 0.5f, 0.0001f);
}

static void test_vertex_xy_xz_yz_reuse(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* First vertex establishes the current z component. */
    add_vtx_16(&n, 0, 0, 0x2000); /* z=2 */
    /* XY reuses the previous z (2). */
    add_vtx_xy(&n, 0x1000, 0);    /* x=1, y=0, z=2 */
    /* XZ reuses the previous y (0). */
    add_vtx_xz(&n, 0x2000, 0x1000); /* x=2, y=0, z=1 */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[1].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[2], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[2], 1.0f, 0.0001f);

    /* YZ reuses the previous x component. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x3000, 0, 0); /* x=3 */
    add_vtx_yz(&n, 0x1000, 0x2000); /* x=3, y=1, z=2 */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_NEAR(g_out[1].clip[0], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[2], 2.0f, 0.0001f);
}

static void test_colour_latch(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x00001Fu); /* red */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_10(&n, 0x20, 0, 0);     /* x=0.5 */
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 0.5f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[3], 1.0f, 0.0001f);
}

static void test_quad_split(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u); /* quads */
    /* v0 = (0,0,0), v1 = (1,0,0), v2 = (1,1,0), v3 = (0,1,0). */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.triangles_out, 2u);
    CHECK_EQ_INT(g_st.quads_split, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    /* First triangle: v0,v1,v2. Second: v0,v2,v3. */
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);
}

static void test_triangle_strip_winding(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u); /* triangle strip */
    /* v0=(0,0), v1=(1,0), v2=(0,1), v3=(1,1). */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    /* tri0: (v0,v1,v2), tri1: (v2,v1,v3) after swap. */
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);
}

static void test_quad_strip(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u); /* quad strip */
    /* v0=(0,0), v1=(1,0), v2=(0,1), v3=(1,1). */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.quads_split, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    /* Split is (0,1,3)/(0,3,2). */
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);
}

static void test_strip_leftover_counts(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* Triangle strip with 1 leftover vertex. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);

    /* Triangle strip with 2 leftover vertices. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 2u);

    /* Fully consumed triangle strip: 4 vertices -> 2 triangles, 0 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);

    /* Fully consumed quad strip: 4 vertices -> 2 triangles, 0 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
}

static void test_incomplete_and_outside_begin(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_vtx_16(&n, 0, 0, 0); /* outside BEGIN */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
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
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u); /* implicit close */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
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
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0); /* second triangle */
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
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
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.raw_fifo_skipped, 1u);
    CHECK_EQ_INT(g_st.unsupported, 1u);
    CHECK_EQ_INT(g_st.geometry_incomplete, 1u);
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
    add_trans(&n, 0x1000u, 0u, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    /* Translation moves every vertex by +1 in x. */
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 1.0f, 0.0001f);
}

static void test_texture_mode_ignored(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 3u); /* texture */
    {
        const uint32_t id[16] = {
            0x1000u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0u, 0u, 0u, 0x1000u,
        };
        add_load_4x4(&n, id);
    }
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.ignored_no_effect, 1u);
}

static void test_signed_overflow_regression(void) {
    /*
     * Hostile stream: LOAD_4x4 then MULT_4x4, every word 0x80000000.
     * The raw products sum to 2^64; this must not trigger signed-overflow UB.
     */
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;
    uint32_t hostile[16];

    clear_state();
    for (i = 0; i < 16u; i++) hostile[i] = 0x80000000u;

    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_load_4x4(&n, hostile);
    add_mult_4x4(&n, hostile);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
}

static void test_current_vertex_persistence(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x2000, 0x3000); /* x=1, y=2, z=3 */
    add_vtx_xy(&n, 0x4000, 0x5000);         /* x=4, y=5, z=3 */
    add_vtx_xz(&n, 0x6000, 0x7000);         /* x=6, y=5, z=7 */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 4.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 5.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[2], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[0], 6.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 5.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[2], 7.0f, 0.0001f);
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
    CHECK_EQ_INT(twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, NULL,
                                    &g_st),
                 TWLHD_GX_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp,
                                    NULL),
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
    test_stack_pop_depths();
    test_report("gx_interp/stack-pop-depths");
    test_stack_store_restore_index();
    test_report("gx_interp/stack-store-restore-index");
    test_stack_overflow_and_underflow();
    test_report("gx_interp/stack-overflow-and-underflow");
    test_projection_isolation();
    test_report("gx_interp/projection-isolation");
    test_projection_stack_ignores_param();
    test_report("gx_interp/projection-stack-ignores-param");
    test_trans_and_scale_by_value();
    test_report("gx_interp/trans-and-scale-by-value");
    test_vertex_16_negative();
    test_report("gx_interp/vertex-16-negative");
    test_vertex_10_negative();
    test_report("gx_interp/vertex-10-negative");
    test_vertex_xy_xz_yz_reuse();
    test_report("gx_interp/vertex-xy-xz-yz-reuse");
    test_colour_latch();
    test_report("gx_interp/colour-latch");
    test_quad_split();
    test_report("gx_interp/quad-split");
    test_triangle_strip_winding();
    test_report("gx_interp/triangle-strip-winding");
    test_quad_strip();
    test_report("gx_interp/quad-strip");
    test_strip_leftover_counts();
    test_report("gx_interp/strip-leftover-counts");
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
    test_signed_overflow_regression();
    test_report("gx_interp/signed-overflow-regression");
    test_current_vertex_persistence();
    test_report("gx_interp/current-vertex-persistence");
    test_null_args();
    test_report("gx_interp/null-args");
    return test_exit_code();
}
