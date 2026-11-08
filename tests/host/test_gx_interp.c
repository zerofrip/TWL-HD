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

/*
 * Some interpreter edge cases (SWAP not at end, no SWAP at all) are legal GX
 * streams but rejected by the v2 parser/writer because a valid capture file
 * must end with SWAP_BUFFERS.  This helper exposes entries directly to the
 * interpreter without v2 validation.
 */
static void build_view(uint32_t entry_count, TwlhdV2View *view) {
    uint32_t i;
    for (i = 0; i < entry_count; i++) {
        g_file[i * 8u + 0u] = (uint8_t)(g_entries[i].cmd_word & 0xFFu);
        g_file[i * 8u + 1u] = (uint8_t)((g_entries[i].cmd_word >> 8) & 0xFFu);
        g_file[i * 8u + 2u] = (uint8_t)((g_entries[i].cmd_word >> 16) & 0xFFu);
        g_file[i * 8u + 3u] = (uint8_t)((g_entries[i].cmd_word >> 24) & 0xFFu);
        g_file[i * 8u + 4u] = (uint8_t)(g_entries[i].value & 0xFFu);
        g_file[i * 8u + 5u] = (uint8_t)((g_entries[i].value >> 8) & 0xFFu);
        g_file[i * 8u + 6u] = (uint8_t)((g_entries[i].value >> 16) & 0xFFu);
        g_file[i * 8u + 7u] = (uint8_t)((g_entries[i].value >> 24) & 0xFFu);
    }
    memset(view, 0, sizeof *view);
    view->gxcm = g_file;
    view->gxcm_count = entry_count;
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

static void add_mult_4x3(uint32_t *n, const uint32_t *m) {
    uint32_t i;
    add_entry(n, TWLHD_V2_CMD_MTX_MULT_4x3, m[0]);
    for (i = 1; i < 12u; i++) {
        add_entry(n, TWLHD_V2_CMD_MTX_MULT_4x3, m[i]);
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
/* Lighting helpers (Phase 5B-1)                                             */
/* ------------------------------------------------------------------------- */

static uint32_t rgb555(uint32_t r, uint32_t g, uint32_t b) {
    return (r & 0x1Fu) | ((g & 0x1Fu) << 5) | ((b & 0x1Fu) << 10);
}

/* 20.12 component -> 10-bit field (1 sign + 9 fractional bits). */
static uint32_t comp10(int32_t value) {
    return ((uint32_t)(value / 8)) & 0x3FFu;
}

static uint32_t light_vector_word(int32_t x, int32_t y, int32_t z,
                                  uint32_t light) {
    return comp10(x) | (comp10(y) << 10) | (comp10(z) << 20) |
           ((light & 0x3u) << 30);
}

static uint32_t normal_word(int32_t x, int32_t y, int32_t z) {
    return light_vector_word(x, y, z, 0u);
}

static uint32_t light_colour_word(uint32_t r, uint32_t g, uint32_t b,
                                  uint32_t light) {
    return rgb555(r, g, b) | ((light & 0x3u) << 30);
}

static void add_shininess(uint32_t *n, const uint32_t *words) {
    uint32_t i;
    for (i = 0u; i < 32u; i++) {
        add_entry(n, TWLHD_V2_CMD_SHININESS, words[i]);
    }
}

/*
 * Three VTX_16 vertices carrying the current latched colour. NORMAL is sent
 * inside BEGIN when normal_inside is set (the light mask applied at BEGIN is
 * then visible to it), otherwise before BEGIN (mask still 0).
 */
static void add_lit_triangle(uint32_t *n, uint32_t normal, int normal_inside,
                             int apply_mask, uint32_t mask) {
    if (apply_mask) {
        add_entry(n, TWLHD_V2_CMD_POLYGON_ATTR, (31u << 16) | (mask & 0xFu));
    }
    if (normal_inside) add_entry(n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_entry(n, TWLHD_V2_CMD_NORMAL, normal);
    if (!normal_inside) add_entry(n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(n, 0, 0, 0);
    add_vtx_16(n, 0x1000, 0, 0);
    add_vtx_16(n, 0, 0x1000, 0);
    add_entry(n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
}

static void run_view(uint32_t entry_count) {
    TwlhdV2View view;
    build_view(entry_count, &view);
    CHECK_EQ_INT(twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp,
                                    &g_st),
                 TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
}

/* Exact literals: the interpreter divides the 5-bit channel by 31.0f. */
static void check_lit_rgb(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t i;
    for (i = 0u; i < g_out_count; i++) {
        CHECK_NEAR(g_out[i].rgba[0], (float)r / 31.0f, 0.0f);
        CHECK_NEAR(g_out[i].rgba[1], (float)g / 31.0f, 0.0f);
        CHECK_NEAR(g_out[i].rgba[2], (float)b / 31.0f, 0.0f);
        CHECK_NEAR(g_out[i].rgba[3], 1.0f, 0.0f);
    }
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

static void test_multiply_side_non_commuting(void) {
    /*
     * All three operations must apply the operand on the right of the current
     * matrix.  Non-commuting operands make the wrong side produce a different
     * result, and the assertions check every clip component.
     */
    uint32_t n = 0u;
    TwlhdV2View view;
    size_t size;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    /* SCALE(2,1,1) then TRANS(1,0,0): right side gives (2*(x+1), y, z, 1). */
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_trans(&n, 0x1000u, 0u, 0u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0); /* x=1 */
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
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);

    /* LOAD translation(1,2,3) then MULT_4x4 scale(2,1,1). */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    {
        const uint32_t trans[16] = {
            0x1000u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0x1000u, 0x2000u, 0x3000u, 0x1000u,
        };
        const uint32_t scale[16] = {
            0x2000u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0u, 0u, 0u, 0x1000u,
        };
        add_load_4x4(&n, trans);
        add_mult_4x4(&n, scale);
    }
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000); /* (1,1,1) */
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    /* Right product T*S: (2x+1, y+2, z+3, 1).  Wrong side would be (4,3,4,1). */
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 4.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);

    /* SCALE(2,1,1) then MULT_4x3 translation(1,2,3). */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    {
        const uint32_t trans_4x3[12] = {
            0x1000u, 0u, 0u,
            0u, 0x1000u, 0u,
            0u, 0u, 0x1000u,
            0x1000u, 0x2000u, 0x3000u,
        };
        add_mult_4x3(&n, trans_4x3);
    }
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    /* Right product S*T: (2*(x+1), y+2, z+3, 1).  Wrong side would be (3,3,4,1). */
    CHECK_NEAR(g_out[0].clip[0], 4.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 4.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

static void test_proj_pos_product_order(void) {
    /*
     * clip = proj * pos.  proj = T(1,0,0), pos = S(2,1,1), vertex (1,1,1):
     * proj*pos*v = T*S*v = (3,1,1,1).  The swapped product pos*proj gives
     * (4,1,1,1), so every clip component is asserted.
     */
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u); /* projection */
    add_trans(&n, 0x1000u, 0u, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 1.0f, 0.0001f);
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
    /* RESTORE 31 fails on the out-of-range index; no prior STORE is needed. */
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

static void test_projection_stack_semantics(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* Projection stack is exactly one entry deep: a second PUSH overflows. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
    CHECK_EQ_INT(g_out_count, 0u);

    /* POP on an empty projection stack is an underflow. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
    CHECK_EQ_INT(g_out_count, 0u);

    /* PUSH, POP, PUSH succeeds: POP resets the projection stack pointer. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_st.stack_errors, 0u);
    CHECK_EQ_INT(g_out_count, 0u);

    /* Two POPs after one PUSH: the second one underflows. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
    CHECK_EQ_INT(g_out_count, 0u);
}

static void test_clip_matrix_freshness_after_pop_restore(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* Position mode: scale x=2, emit a triangle, push, scale x=3, emit a
     * triangle, pop 1, emit a triangle. The last triangle must match the
     * first (sx=2), not the middle (sx=6). */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

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
    CHECK_EQ_INT(g_out_count, 9u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 6.0f, 0.0001f);
    CHECK_NEAR(g_out[6].clip[0], 2.0f, 0.0001f);

    /* Position mode: store 3, modify, emit a triangle, restore 3, emit a
     * triangle (original value again). */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 3u);
    add_scale(&n, 0x4000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_MTX_RESTORE, 3u);
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
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_NEAR(g_out[0].clip[0], 8.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 2.0f, 0.0001f);

    /* Projection stack: push/pop of the projection matrix between vertices. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

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
    CHECK_EQ_INT(g_out_count, 9u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 6.0f, 0.0001f);
    CHECK_NEAR(g_out[6].clip[0], 2.0f, 0.0001f);
}

/* ------------------------------------------------------------------------- */
/* Table-driven clip-matrix freshness test for every matrix-changing command */
/* ------------------------------------------------------------------------- */

typedef enum {
    FRESH_IDENTITY,
    FRESH_LOAD_4x4,
    FRESH_LOAD_4x3,
    FRESH_MULT_4x4,
    FRESH_MULT_4x3,
    FRESH_MULT_3x3,
    FRESH_SCALE,
    FRESH_TRANS,
    FRESH_POP,
    FRESH_RESTORE,
    FRESH_PROJ_IDENTITY,
    FRESH_PROJ_LOAD_4x4,
    FRESH_PROJ_MULT_4x4,
    FRESH_PROJ_TRANS
} FreshnessOp;

typedef struct {
    const char *label;
    uint32_t mode;
    FreshnessOp op;
    float first;
    float second;
} FreshnessCase;

static void add_degenerate_triangle_at_x(uint32_t *n, int32_t x) {
    add_vtx_16(n, x, 0, 0);
    add_vtx_16(n, 0, 0, 0);
    add_vtx_16(n, 0, 0, 0);
}

static void run_freshness_case(const FreshnessCase *c) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;
    char report_name[128];

    static const uint32_t scale4_4x4[16] = {
        0x4000u, 0u, 0u, 0u,
        0u, 0x1000u, 0u, 0u,
        0u, 0u, 0x1000u, 0u,
        0u, 0u, 0u, 0x1000u,
    };
    static const uint32_t scale3_4x4[16] = {
        0x3000u, 0u, 0u, 0u,
        0u, 0x1000u, 0u, 0u,
        0u, 0u, 0x1000u, 0u,
        0u, 0u, 0u, 0x1000u,
    };
    static const uint32_t scale4_4x3[12] = {
        0x4000u, 0u, 0u,
        0u, 0x1000u, 0u,
        0u, 0u, 0x1000u,
        0u, 0u, 0u,
    };
    static const uint32_t scale3_4x3[12] = {
        0x3000u, 0u, 0u,
        0u, 0x1000u, 0u,
        0u, 0u, 0x1000u,
        0u, 0u, 0u,
    };
    static const uint32_t scale3_3x3[9] = {
        0x3000u, 0u, 0u,
        0u, 0x1000u, 0u,
        0u, 0u, 0x1000u,
    };

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, c->mode);

    /* Establish a known non-identity matrix and (for POP/RESTORE) stack state. */
    switch (c->op) {
    case FRESH_POP:
        add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
        add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
        break;
    case FRESH_RESTORE:
        add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
        add_entry(&n, TWLHD_V2_CMD_MTX_STORE, 0u);
        add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
        break;
    default:
        add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
        break;
    }

    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u); /* triangle list */
    add_degenerate_triangle_at_x(&n, 0x1000);   /* clears clip_dirty */

    /* Issue the command under test. */
    switch (c->op) {
    case FRESH_IDENTITY:
    case FRESH_PROJ_IDENTITY:
        add_entry(&n, TWLHD_V2_CMD_MTX_IDENTITY, 0u);
        break;
    case FRESH_LOAD_4x4:
    case FRESH_PROJ_LOAD_4x4:
        add_load_4x4(&n, scale4_4x4);
        break;
    case FRESH_LOAD_4x3:
        add_load_4x3(&n, scale4_4x3);
        break;
    case FRESH_MULT_4x4:
    case FRESH_PROJ_MULT_4x4:
        add_mult_4x4(&n, scale3_4x4);
        break;
    case FRESH_MULT_4x3:
        add_mult_4x3(&n, scale3_4x3);
        break;
    case FRESH_MULT_3x3:
        add_mult_3x3(&n, scale3_3x3);
        break;
    case FRESH_SCALE:
        add_scale(&n, 0x3000u, 0x1000u, 0x1000u);
        break;
    case FRESH_TRANS:
    case FRESH_PROJ_TRANS:
        add_trans(&n, 0x1000u, 0u, 0u);
        break;
    case FRESH_POP:
        add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
        break;
    case FRESH_RESTORE:
        add_entry(&n, TWLHD_V2_CMD_MTX_RESTORE, 0u);
        break;
    }

    /* Second triangle: must use the new matrix. */
    add_degenerate_triangle_at_x(&n, 0x1000);
    /* Third triangle: must be identical to the second (stability). */
    add_degenerate_triangle_at_x(&n, 0x1000);

    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 9u);

    CHECK_NEAR(g_out[0].clip[0], c->first, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], c->second, 0.0001f);
    CHECK_NEAR(g_out[6].clip[0], c->second, 0.0001f);

    /* The checked component must actually change; the other components stay
     * at trivial values with the matrices chosen above. */
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[3], 1.0f, 0.0001f);

    snprintf(report_name, sizeof report_name,
             "gx_interp/clip-freshness-every-matrix-command/%s", c->label);
    test_report(report_name);
}

static void test_clip_matrix_freshness_every_matrix_command(void) {
    static const FreshnessCase cases[] = {
        /* Position matrix, mode 1 (position). */
        { "identity/mode1",       1u, FRESH_IDENTITY,     2.0f, 1.0f },
        { "load-4x4/mode1",       1u, FRESH_LOAD_4x4,     2.0f, 4.0f },
        { "load-4x3/mode1",       1u, FRESH_LOAD_4x3,     2.0f, 4.0f },
        { "mult-4x4/mode1",       1u, FRESH_MULT_4x4,     2.0f, 6.0f },
        { "mult-4x3/mode1",       1u, FRESH_MULT_4x3,     2.0f, 6.0f },
        { "mult-3x3/mode1",       1u, FRESH_MULT_3x3,     2.0f, 6.0f },
        { "scale/mode1",          1u, FRESH_SCALE,        2.0f, 6.0f },
        { "trans/mode1",          1u, FRESH_TRANS,        2.0f, 4.0f },
        { "pop/mode1",            1u, FRESH_POP,          2.0f, 1.0f },
        { "restore/mode1",        1u, FRESH_RESTORE,      1.0f, 2.0f },

        /* Position matrix, mode 2 (modelview). */
        { "identity/mode2",       2u, FRESH_IDENTITY,     2.0f, 1.0f },
        { "load-4x4/mode2",       2u, FRESH_LOAD_4x4,     2.0f, 4.0f },
        { "load-4x3/mode2",       2u, FRESH_LOAD_4x3,     2.0f, 4.0f },
        { "mult-4x4/mode2",       2u, FRESH_MULT_4x4,     2.0f, 6.0f },
        { "mult-4x3/mode2",       2u, FRESH_MULT_4x3,     2.0f, 6.0f },
        { "mult-3x3/mode2",       2u, FRESH_MULT_3x3,     2.0f, 6.0f },
        { "scale/mode2",          2u, FRESH_SCALE,        2.0f, 6.0f },
        { "trans/mode2",          2u, FRESH_TRANS,        2.0f, 4.0f },
        { "pop/mode2",            2u, FRESH_POP,          2.0f, 1.0f },
        { "restore/mode2",        2u, FRESH_RESTORE,      1.0f, 2.0f },

        /* Projection matrix, mode 0. */
        { "identity/projection",  0u, FRESH_PROJ_IDENTITY, 2.0f, 1.0f },
        { "load-4x4/projection",  0u, FRESH_PROJ_LOAD_4x4, 2.0f, 4.0f },
        { "mult-4x4/projection",  0u, FRESH_PROJ_MULT_4x4, 2.0f, 6.0f },
        { "trans/projection",     0u, FRESH_PROJ_TRANS,    2.0f, 4.0f },
    };
    uint32_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        run_freshness_case(&cases[i]);
    }
}

static void test_mode_switch_does_not_change_output(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_scale(&n, 0x2000u, 0x1000u, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_degenerate_triangle_at_x(&n, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    add_degenerate_triangle_at_x(&n, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_NEAR(g_out[0].clip[0], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[0], 2.0f, 0.0001f);
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

static void test_scale_non_unit_z(void) {
    /* SCALE(1,1,3) must scale z as well: vertex (1,1,1) -> (1,1,3,1). */
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    add_scale(&n, 0x1000u, 0x1000u, 0x3000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_vtx_16(&n, 0x1000, 0x1000, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 3.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

/* ------------------------------------------------------------------------- */
/* Vertex form and primitive tests                                           */
/* ------------------------------------------------------------------------- */

static void test_rounding_negative_fraction(void) {
    /*
     * Build clip[0][0] = floor((-0x0800) * 0x0001 / 4096).
     * The raw product is -0x0800; arithmetic shift right 12 gives -1,
     * whereas truncation toward zero would give 0.  With vertex x=1.0,
     * clip.x becomes exactly -1/4096.
     */
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 0u); /* projection */
    {
        const uint32_t proj[16] = {
            0xFFFFF800u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0u, 0u, 0u, 0x1000u,
        };
        add_load_4x4(&n, proj);
    }
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u); /* position */
    {
        const uint32_t pos[16] = {
            0x00000001u, 0u, 0u, 0u,
            0u, 0x1000u, 0u, 0u,
            0u, 0u, 0x1000u, 0u,
            0u, 0u, 0u, 0x1000u,
        };
        add_load_4x4(&n, pos);
    }
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0); /* x=1.0 */
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].clip[0], -1.0f / 4096.0f, 0.0000001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
}

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

static void test_vertex_10_sign_extension(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    /* x: raw 0x200 = -512 -> -8.0; raw 0x1FF = +511 -> +7.984375. */
    add_vtx_10(&n, 0x200, 0, 0);
    add_vtx_10(&n, 0x1FF, 0, 0);
    add_vtx_10(&n, 0, 0, 0);
    /* y */
    add_vtx_10(&n, 0, 0x200, 0);
    add_vtx_10(&n, 0, 0x1FF, 0);
    add_vtx_10(&n, 0, 0, 0);
    /* z */
    add_vtx_10(&n, 0, 0, 0x200);
    add_vtx_10(&n, 0, 0, 0x1FF);
    add_vtx_10(&n, 0, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 9u);
    CHECK_NEAR(g_out[0].clip[0], -8.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 7.984375f, 0.0001f);
    CHECK_NEAR(g_out[3].clip[1], -8.0f, 0.0001f);
    CHECK_NEAR(g_out[4].clip[1], 7.984375f, 0.0001f);
    CHECK_NEAR(g_out[6].clip[2], -8.0f, 0.0001f);
    CHECK_NEAR(g_out[7].clip[2], 7.984375f, 0.0001f);
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
    /* Pure red, green, blue, then a mixed value to catch swapped channels. */
    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x00001Fu); /* R=31, G=0, B=0 */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x0003E0u); /* R=0, G=31, B=0 */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x007C00u); /* R=0, G=0, B=31 */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    /* 0x1234 -> R=20, G=17, B=4. */
    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x001234u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_vtx_10(&n, 0x20, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 12u);

    CHECK_NEAR(g_out[0].rgba[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[3], 1.0f, 0.0001f);

    CHECK_NEAR(g_out[3].rgba[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[3].rgba[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[3].rgba[2], 0.0f, 0.0001f);

    CHECK_NEAR(g_out[6].rgba[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[6].rgba[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[6].rgba[2], 1.0f, 0.0001f);

    CHECK_NEAR(g_out[9].rgba[0], 20.0f / 31.0f, 0.0001f);
    CHECK_NEAR(g_out[9].rgba[1], 17.0f / 31.0f, 0.0001f);
    CHECK_NEAR(g_out[9].rgba[2], 4.0f / 31.0f, 0.0001f);
    CHECK_NEAR(g_out[9].rgba[3], 1.0f, 0.0001f);
}

static void test_default_colour(void) {
    /* No COLOR command: latched colour defaults to opaque black. */
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_NEAR(g_out[0].rgba[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].rgba[3], 1.0f, 0.0001f);
}

static void add_triangle_group(uint32_t *n) {
    add_entry(n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(n, 0, 0, 0);
    add_vtx_16(n, 0x1000, 0, 0);
    add_vtx_16(n, 0, 0x1000, 0);
    add_entry(n, TWLHD_V2_CMD_END_VTXS, 0u);
}

static void test_alpha_literal_scale(void) {
    static const struct {
        uint32_t alpha;
        float expected;
    } cases[] = {
        {31u, 1.0f},
        {16u, 0.53125f},
        {4u, 0.15625f},
        {1u, 0.0625f},
        {0u, 0.0f},
    };
    uint32_t c;
    uint32_t i;

    for (c = 0u; c < sizeof cases / sizeof cases[0]; c++) {
        uint32_t n = 0u;
        TwlhdV2View view;
        TwlhdGxResult r;

        clear_state();
        add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, cases[c].alpha << 16);
        add_triangle_group(&n);
        build_view(n, &view);
        r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp,
                               &g_st);
        CHECK_EQ_INT(r, TWLHD_GX_OK);
        CHECK_EQ_INT(g_out_count, 3u);
        for (i = 0u; i < g_out_count; i++) {
            /* Exact literals, not derived from gx_alpha_float(). */
            CHECK_NEAR(g_out[i].rgba[3], cases[c].expected, 0.0f);
        }
        CHECK_EQ_INT(g_st.alpha_translucent_polys,
                     (cases[c].alpha >= 1u && cases[c].alpha <= 30u) ? 1u
                                                                     : 0u);
        CHECK_EQ_INT(g_st.alpha_wireframe_polys,
                     cases[c].alpha == 0u ? 1u : 0u);
        /* POLYGON_ATTR is no longer counted as having no effect. */
        CHECK_EQ_INT(g_st.ignored_no_effect, 0u);
    }
}

static void test_alpha_pending_until_begin(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 31u << 16);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    /* Written inside BEGIN..END: must not affect this group. */
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 4u << 16);
    add_vtx_16(&n, 0x800, 0x800, 0);
    add_vtx_16(&n, 0x400, 0x400, 0);
    add_vtx_16(&n, 0x200, 0x200, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    /* New BEGIN with no fresh attr: pending 4 is applied now. */
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 9u);
    for (i = 0u; i < 6u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 1.0f, 0.0f);
    }
    for (i = 6u; i < 9u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 0.15625f, 0.0f);
    }
    CHECK_EQ_INT(g_st.alpha_translucent_polys, 1u);
    CHECK_EQ_INT(g_st.alpha_wireframe_polys, 0u);
}

static void test_alpha_two_groups(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 16u << 16);
    add_triangle_group(&n);
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 0u << 16);
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    for (i = 0u; i < 3u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 0.53125f, 0.0f);
    }
    for (i = 3u; i < 6u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 0.0f, 0.0f);
    }
    CHECK_EQ_INT(g_st.alpha_translucent_polys, 1u);
    CHECK_EQ_INT(g_st.alpha_wireframe_polys, 1u);
}

static void test_alpha_default_and_extra_bits(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    /* No POLYGON_ATTR at all: opaque and no alpha statistics. */
    clear_state();
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    for (i = 0u; i < g_out_count; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 1.0f, 0.0f);
    }
    CHECK_EQ_INT(g_st.alpha_translucent_polys, 0u);
    CHECK_EQ_INT(g_st.alpha_wireframe_polys, 0u);

    /* Only bits 16..20 are the alpha; cull (6-7) and bit 14 are ignored. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR,
              (16u << 16) | 0x0000C0u | 0x00004000u);
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    for (i = 0u; i < g_out_count; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 0.53125f, 0.0f);
    }
    CHECK_EQ_INT(g_st.alpha_translucent_polys, 1u);

    /* 0x1F0000 (alpha 31) plus cull bits: opaque and still no statistics. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 0x001F0000u | 0x0000C0u);
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    for (i = 0u; i < g_out_count; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 1.0f, 0.0f);
    }
    CHECK_EQ_INT(g_st.alpha_translucent_polys, 0u);
    CHECK_EQ_INT(g_st.alpha_wireframe_polys, 0u);
}

static void test_alpha_rgb_unaffected(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_COLOR, 0x7FFFu); /* R=G=B=31 */
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 4u << 16);
    add_triangle_group(&n);
    add_entry(&n, TWLHD_V2_CMD_POLYGON_ATTR, 31u << 16);
    add_triangle_group(&n);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    for (i = 0u; i < 6u; i++) {
        CHECK_NEAR(g_out[i].rgba[0], 1.0f, 0.0f);
        CHECK_NEAR(g_out[i].rgba[1], 1.0f, 0.0f);
        CHECK_NEAR(g_out[i].rgba[2], 1.0f, 0.0f);
    }
    for (i = 0u; i < 3u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 0.15625f, 0.0f);
    }
    for (i = 3u; i < 6u; i++) {
        CHECK_NEAR(g_out[i].rgba[3], 1.0f, 0.0f);
    }
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

static void test_quad_strip_six_vertices(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u); /* quad strip */
    /* Six distinct vertices so the roll order is value-checkable. */
    add_vtx_16(&n, 0, 0, 0);       /* v0 */
    add_vtx_16(&n, 0x1000, 0, 0);  /* v1 */
    add_vtx_16(&n, 0, 0x1000, 0);  /* v2 */
    add_vtx_16(&n, 0x1000, 0x1000, 0); /* v3 */
    add_vtx_16(&n, 0, 0x2000, 0);  /* v4 */
    add_vtx_16(&n, 0x1000, 0x2000, 0); /* v5 */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 16u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 12u);
    CHECK_EQ_INT(g_st.quads_split, 2u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);

    /* Quad 0: (0,1,3),(0,3,2). */
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

    /* Quad 1: (2,3,5),(2,5,4). */
    CHECK_NEAR(g_out[6].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[6].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[7].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[7].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[8].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[8].clip[1], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[9].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[9].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[10].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[10].clip[1], 2.0f, 0.0001f);
    CHECK_NEAR(g_out[11].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[11].clip[1], 2.0f, 0.0001f);
}

static void test_quad_list_leftovers(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* 5 vertices: one complete quad plus one leftover. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u); /* quad list */
    add_vtx_16(&n, 0, 0, 0);       /* v0 */
    add_vtx_16(&n, 0x1000, 0, 0);  /* v1 */
    add_vtx_16(&n, 0x1000, 0x1000, 0); /* v2 */
    add_vtx_16(&n, 0, 0x1000, 0);  /* v3 */
    add_vtx_16(&n, 0x2000, 0, 0);  /* v4 leftover */
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);

    /* 6 vertices: one complete quad plus two leftovers. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 2u);
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);

    /* 7 vertices: one complete quad plus three leftovers. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0x2000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 3u);
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);
}

static void test_strip_leftover_counts(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* Triangle strip: 2 vertices form no primitive -> 0 out, 2 dropped. */
    clear_state();
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

    /* Triangle strip: 3 vertices -> 1 triangle, 0 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);

    /* Triangle strip: 4 vertices -> 2 triangles, 0 dropped. */
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

    /* Triangle strip: 5 vertices -> 3 triangles, 0 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 2u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 9u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);

    /* Quad strip: 4 vertices -> 1 quad (2 triangles), 0 dropped. */
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

    /* Quad strip: 5 vertices -> 1 quad, 1 leftover vertex dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);

    /* Quad strip: 6 vertices -> 2 quads (4 triangles), 0 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 12u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);

    /* Quad strip: 7 vertices -> 2 quads, 1 leftover vertex dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 3u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0x2000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 12u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
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
    uint32_t cap;
    uint32_t i;
    static const TwlhdGxVertex sentinel = {
        { 12345.0f, 12345.0f, 12345.0f, 12345.0f },
        { 6789.0f, 6789.0f, 6789.0f, 6789.0f }
    };

    for (cap = 3u; cap <= 5u; cap++) {
        clear_state();
        n = 0u;
        add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
        add_vtx_16(&n, 0, 0, 0);
        add_vtx_16(&n, 0x1000, 0, 0);
        add_vtx_16(&n, 0, 0, 0);
        add_vtx_16(&n, 0x1000, 0, 0); /* second triangle */
        add_vtx_16(&n, 0, 0x1000, 0);
        add_vtx_16(&n, 0x1000, 0x1000, 0);
        add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
        add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

        /* Guard canary: anything beyond the declared capacity must stay clean. */
        for (i = cap; i < 256u; i++) {
            memcpy(&g_out[i], &sentinel, sizeof(TwlhdGxVertex));
        }

        size = build_file(n);
        CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
        r = twlhd_gx_interpret(&view, g_out, cap, &g_out_count, &g_vp, &g_st);
        CHECK_EQ_INT(r, TWLHD_GX_ERR_CAPACITY);
        CHECK_EQ_INT(g_out_count, 3u); /* first triangle fits */
        CHECK_EQ_INT(g_st.capacity_dropped, 3u);

        for (i = cap; i < 256u; i++) {
            CHECK_EQ_INT(memcmp(&g_out[i], &sentinel, sizeof(TwlhdGxVertex)), 0);
        }
    }
}

static void test_stream_error_partial_output(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;

    /*
     * Malformed streams are fed through build_view: the parser rejects both
     * cases, but the interpreter contract promises partial output on error.
     */

    /* (a) Unknown command id after a complete triangle. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0, 0, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, 0x05u, 0u); /* not a valid command id */
    build_view(n, &view);

    g_out_count = 0xDEADBEEFu;
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STREAM);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.vertices_in, 3u);
    CHECK_EQ_INT(g_st.commands, 5u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[1], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[2], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[1], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[2], 1.0f, 0.0001f);

    /* (b) Truncated MTX_LOAD_4x4: 16 entries announced, view ends early. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0, 0, 0x1000);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0x1000u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_LOAD_4x4, 0u);
    build_view(n, &view);

    g_out_count = 0xDEADBEEFu;
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STREAM);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.vertices_in, 3u);
    CHECK_EQ_INT(g_st.commands, 5u);
    CHECK_NEAR(g_out[0].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[0].clip[3], 1.0f, 0.0001f);
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

static void test_swap_stops_interpretation(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u); /* triangle list */
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    /* After SWAP_BUFFERS the interpreter must stop; this second triangle is
     * ignored even though the stream is otherwise well-formed. */
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_vtx_16(&n, 0x1000, 0x2000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);

    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 3u);
    CHECK_EQ_INT(g_st.triangles_out, 1u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 0u);
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[1].clip[0], 1.0f, 0.0001f);
    CHECK_NEAR(g_out[2].clip[1], 1.0f, 0.0001f);
}

/* ------------------------------------------------------------------------- */
/* Negative / error tests                                                    */
/* ------------------------------------------------------------------------- */

static void test_pop_zero_and_negative_position(void) {
    uint32_t n = 0u;
    size_t size;
    TwlhdV2View view;
    TwlhdGxResult r;
    uint32_t i;

    /* POP 0 in position mode with a full stack (sp=31) must error. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    for (i = 0; i < 31u; i++) add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);

    /* POP -1 (6-bit two's complement 0x3F) with a full stack must error. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 1u);
    for (i = 0; i < 31u; i++) add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 0x3Fu);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    size = build_file(n);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, size, &view), TWLHD_V2_OK);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_ERR_STACK);
    CHECK_EQ_INT(g_st.stack_errors, 1u);
}

static void test_unterminated_begin(void) {
    uint32_t n = 0u;
    TwlhdV2View view;
    TwlhdGxResult r;

    /* Triangle list: 1 leftover vertex dropped at end of stream. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);

    /* Triangle list: 2 leftover vertices dropped at end of stream. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 0u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 2u);

    /* Quad list: 5 vertices -> 1 complete quad + 1 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 1u);
    CHECK_NEAR(g_out[0].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[0], 0.0f, 0.0001f);
    CHECK_NEAR(g_out[5].clip[1], 1.0f, 0.0001f);

    /* Quad list: 6 vertices -> 1 complete quad + 2 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 2u);

    /* Quad list: 7 vertices -> 1 complete quad + 3 dropped. */
    clear_state();
    n = 0u;
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0x1000, 0x1000, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0, 0);
    add_vtx_16(&n, 0x2000, 0x1000, 0);
    add_vtx_16(&n, 0x2000, 0x2000, 0);
    build_view(n, &view);
    r = twlhd_gx_interpret(&view, g_out, 12u, &g_out_count, &g_vp, &g_st);
    CHECK_EQ_INT(r, TWLHD_GX_OK);
    CHECK_EQ_INT(g_out_count, 6u);
    CHECK_EQ_INT(g_st.dropped_incomplete, 3u);
}

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
/* Lighting tests (Phase 5B-1)                                               */
/* ------------------------------------------------------------------------- */

static void test_light_ambient_only(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB,
              rgb555(0, 0, 0) | (rgb555(16, 8, 4) << 16));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* amb = (ambient * light) >> 5 = 16*31>>5, 8*31>>5, 4*31>>5. */
    check_lit_rgb(15, 7, 3);
}

static void test_light_diffuse_only(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 16, 8));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* fixed_diffuse = 4088. The 3x3 transform accumulates the products in
     * 64 bits and shifts >>12 (saturating); (diffuse*light*4088) >> 17. */
    check_lit_rgb(29, 15, 7);
}

static void test_light_emission_only(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(10, 20, 30) << 16);
    /* NORMAL before any BEGIN: the applied light mask is still 0. */
    add_lit_triangle(&n, normal_word(0, 0, 4088), 0, 0, 0u);
    run_view(n);
    /* Vertex colour starts at emission, not black. */
    check_lit_rgb(10, 20, 30);
}

static void test_light_specular_without_table(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* Half vector normalize: sqrt(dot)<<6, abs >>6, component*64/len, stored
     * negated (-H); dot_h>0 then 2*(4088*4088>>12) - 4096 = 4064. */
    check_lit_rgb(29, 29, 29);
}

static void test_light_specular_with_table(void) {
    uint32_t n = 0u;
    uint32_t words[32];

    clear_state();
    memset(words, 0, sizeof words);
    words[31] = 200u << 24; /* applied entry 127 = 200 */
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31) | 0x8000u);
    add_shininess(&n, words);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* specular bit15 selects the table; table[4064>>5] << 4 = 200<<4 = 3200;
     * (31*31*3200) >> 17 = 23. */
    check_lit_rgb(23, 23, 23);
}

static void test_light_two_lights(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 0, 0, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(0, 31, 0, 1));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 1));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 3u);
    run_view(n);
    check_lit_rgb(29, 29, 0);
}

static void test_light_each_index_alone(void) {
    uint32_t light;

    for (light = 0u; light < 4u; light++) {
        uint32_t n = 0u;

        clear_state();
        add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
        add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR,
                  light_colour_word(16, 12, 20, light));
        add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
                  light_vector_word(0, 0, -4096, light));
        add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u << light);
        run_view(n);
        /* Mask bit test: only light `light` is enabled and only it has a
         * non-zero colour/direction. */
        check_lit_rgb(15, 11, 19);
        CHECK_EQ_INT(g_st.stack_errors, 0u);
    }
}

static void test_light_saturation_at_31(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB,
              rgb555(31, 31, 31) | (rgb555(31, 31, 31) << 16));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* 29 (diffuse) + 30 (ambient) clamps with min(31, ...). */
    check_lit_rgb(31, 31, 31);
}

static void test_light_rotated_vec_normal(void) {
    static const uint32_t rot[16] = {
        0u, 4096u, 0u, 0u,
        (uint32_t)(int32_t)-4096, 0u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, rot);
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(-4096, 0, 0, 0));
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* Both N and L rotate by +90 deg about z; dot is preserved. */
    check_lit_rgb(29, 29, 29);
}

static void test_light_mode1_vec_untouched(void) {
    static const uint32_t diag[16] = {
        8192u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t mode;

    for (mode = 1u; mode <= 2u; mode++) {
        uint32_t n = 0u;

        clear_state();
        add_entry(&n, TWLHD_V2_CMD_MTX_MODE, mode);
        add_load_4x4(&n, diag);
        add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
        add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR,
                  light_colour_word(31, 31, 31, 0));
        add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
                  light_vector_word(-4096, 0, 0, 0));
        add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
        run_view(n);
        /* Mode 1 leaves the vector matrix at identity: 29. Mode 2 doubles
         * x, so fixed_diffuse is 16352 and the result clamps to 31. */
        if (mode == 1u) {
            check_lit_rgb(29, 29, 29);
        } else {
            check_lit_rgb(31, 31, 31);
        }
    }
}

static void test_light_scale_scales_position_only(void) {
    static const uint32_t diag[16] = {
        2048u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, diag);
    add_scale(&n, 2048u, 4096u, 4096u); /* position only */
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(-4096, 0, 0, 0));
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* vec stays at 0.5: fixed_diffuse = 1022 -> (31*31*1022)>>17 = 7.
     * If SCALE also hit the vector matrix it would be 255 -> 1. */
    check_lit_rgb(7, 7, 7);
}

static void test_light_push_pop_restores_vec(void) {
    static const uint32_t half[16] = {
        2048u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    static const uint32_t quarter[16] = {
        1024u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, half);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_load_4x4(&n, quarter);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(-4096, 0, 0, 0));
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* POP restores the vector matrix to 0.5, not the pushed-under 0.25. */
    check_lit_rgb(7, 7, 7);
}

static void test_light_dif_amb_bit15(void) {
    uint32_t n = 0u;

    /* Bit 15 set: the diffuse colour becomes the vertex colour. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(10, 20, 30) | 0x8000u);
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    run_view(n);
    check_lit_rgb(10, 20, 30);

    /* Bit 15 clear: the previous COLOR wins. */
    n = 0u;
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_COLOR, rgb555(4, 8, 16));
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(10, 20, 30));
    add_entry(&n, TWLHD_V2_CMD_BEGIN_VTXS, 0u);
    add_vtx_16(&n, 0, 0, 0);
    add_vtx_16(&n, 0x1000, 0, 0);
    add_vtx_16(&n, 0, 0x1000, 0);
    add_entry(&n, TWLHD_V2_CMD_END_VTXS, 0u);
    add_entry(&n, TWLHD_V2_CMD_SWAP_BUFFERS, 0u);
    run_view(n);
    check_lit_rgb(4, 8, 16);
}

static void test_light_diffuse_negative_dot_clamped(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    /* Light behind the surface: max(0, -dot) must clamp the term to 0. */
    add_lit_triangle(&n, normal_word(0, 0, -4088), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(0, 0, 0);
}

static void test_light_shininess_clamp_at_4095(void) {
    static const uint32_t doubled_z[16] = {
        4096u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 8192u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, doubled_z);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* dot_h = 8176 -> raw shininess 28544 clamps to 4095 -> 30. Without the
     * clamp the specular term would push the channel to 31. */
    check_lit_rgb(30, 30, 30);
}

static void test_light_direction_transformed_at_command_time(void) {
    static const uint32_t half[16] = {
        2048u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    static const uint32_t doubled[16] = {
        8192u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, half);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(-4096, 0, 0, 0));
    /* Changing the matrix afterwards must not move the light. */
    add_load_4x4(&n, doubled);
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* L was fixed at -2048, N becomes 8176: fd = 4088 -> 29.
     * A lazy transform with the doubled matrix would give 31. */
    check_lit_rgb(29, 29, 29);
}

static void test_light_shininess_applied_only_after_32_words(void) {
    uint32_t n = 0u;
    uint32_t words[32];

    /* Specular table enabled but no SHININESS command: table is all zero. */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31) | 0x8000u);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(0, 0, 0);

    /* Two complete SHININESS commands: the second table replaces the first. */
    n = 0u;
    clear_state();
    memset(words, 0, sizeof words);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31) | 0x8000u);
    add_shininess(&n, words);
    words[31] = 200u << 24;
    add_shininess(&n, words);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(23, 23, 23);
}

static void test_light_commands_not_ignored(void) {
    uint32_t n = 0u;
    uint32_t words[32];

    clear_state();
    memset(words, 0, sizeof words);
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(1, 2, 3));
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(4, 5, 6));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR, light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(7, 8, 9, 0));
    add_entry(&n, TWLHD_V2_CMD_NORMAL, normal_word(0, 0, 4088));
    add_shininess(&n, words);
    add_entry(&n, TWLHD_V2_CMD_TEX_COORD, 0u);
    add_entry(&n, TWLHD_V2_CMD_TEXIMAGE_PARAM, 0u);
    add_entry(&n, TWLHD_V2_CMD_PLTT_BASE, 0u);
    add_entry(&n, TWLHD_V2_CMD_BOX_TEST, 0u);
    add_entry(&n, TWLHD_V2_CMD_BOX_TEST, 0u);
    add_entry(&n, TWLHD_V2_CMD_BOX_TEST, 0u);
    add_entry(&n, TWLHD_V2_CMD_POS_TEST, 0u);
    add_entry(&n, TWLHD_V2_CMD_POS_TEST, 0u);
    add_entry(&n, TWLHD_V2_CMD_VEC_TEST, 0u);
    {
        TwlhdV2View view;
        build_view(n, &view);
        CHECK_EQ_INT(twlhd_gx_interpret(&view, g_out, 12u, &g_out_count,
                                        &g_vp, &g_st),
                     TWLHD_GX_OK);
    }
    /* The six lighting commands have an effect now; the other six do not. */
    CHECK_EQ_INT(g_st.ignored_no_effect, 6u);
}

static void test_light_mask_zero_ignores_configured_lights(void) {
    uint32_t n = 0u;

    clear_state();
    /* Every light is fully configured; mask 0 must still skip all of them. */
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 0, 0, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(0, 31, 0, 1));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(0, 0, 31, 2));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(8, 16, 24, 3));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 1));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 2));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 3));
    /* Non-zero diffuse material: if the mask test were dropped, these lights
     * would visibly add to the emission-only colour. */
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(3, 4, 5));
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(5, 7, 9) << 16);
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 0u);
    run_view(n);
    /* Mask test: no light term, so only emission remains. */
    check_lit_rgb(5, 7, 9);
}

static void test_light_mask_one_skips_other_configured_lights(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 0, 0, 1));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(0, 31, 0, 2));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(0, 0, 31, 3));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 1));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 2));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 3));
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(9, 9, 9));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* Only light 0: (9*31*4088) >> 17 = 8, not a mask != 0 all-lights sum. */
    check_lit_rgb(8, 8, 8);
}

static void test_light_mask_each_index_with_others_configured(void) {
    static const uint32_t colours[4][3] = {
        {31u, 0u, 0u}, {0u, 31u, 0u}, {0u, 0u, 31u}, {8u, 16u, 24u},
    };
    static const uint32_t expected[4][3] = {
        {29u, 0u, 0u}, {0u, 29u, 0u}, {0u, 0u, 29u}, {7u, 15u, 23u},
    };
    uint32_t light;

    for (light = 0u; light < 4u; light++) {
        uint32_t n = 0u;
        uint32_t index;

        clear_state();
        add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
        for (index = 0u; index < 4u; index++) {
            add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR,
                      light_colour_word(colours[index][0], colours[index][1],
                                        colours[index][2], index));
            add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
                      light_vector_word(0, 0, -4096, index));
        }
        /* Per-index mask test: only `light` is enabled even though all four
         * have distinct colours that would change the sum if applied. */
        add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u << light);
        run_view(n);
        check_lit_rgb(expected[light][0], expected[light][1],
                      expected[light][2]);
    }
}

static void test_light_shininess_clamp_table_entry_127(void) {
    static const uint32_t doubled_z[16] = {
        4096u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 8192u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;
    uint32_t words[32];

    clear_state();
    memset(words, 0, sizeof words);
    words[31] = 200u << 24; /* applied entry 127 = 200 */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, doubled_z);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31) | 0x8000u);
    add_shininess(&n, words);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* dot_h = 8176 -> raw shininess 28544 clamps to 4095; index 4095>>5 = 127
     * (200) -> 3200 -> (31*31*3200)>>17 = 23. A 4096 clamp indexes 128, the
     * byte after shininess_applied (LSB of shininess_index = 0) and gives 0. */
    check_lit_rgb(23, 23, 23);
}

static void test_light_shininess_clamp_uses_4095(void) {
    static const uint32_t doubled_z[16] = {
        4096u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 8192u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, doubled_z);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(2, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(16, 31, 31));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* No table: red uses specular 16 * light 2. (32*4095)>>17 = 0 while
     * (32*4096)>>17 = 1; green/blue stay 30 at either bound. */
    check_lit_rgb(0, 30, 30);
}

static void test_light_shininess_4096_constant_boundary(void) {
    static const uint32_t scaled_z[16] = {
        4096u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 3180u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    clear_state();
    /* k = 3180: n_z = (4088*3180)>>12 = 3173, so raw shininess j = 818.
     * (961*818)>>17 = 5, but an off-by-one 4096 constant gives j = 819 -> 6. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_load_4x4(&n, scaled_z);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_lit_triangle(&n, normal_word(0, 0, 4088), 1, 1, 1u);
    run_view(n);
    /* Raw shininess 818 is below the clamp: (31*31*818)>>17 = 5. */
    check_lit_rgb(5, 5, 5);
}

static void test_light_dot_zero_table_entry_zero(void) {
    /* EQUIVALENT MUTANT DECLARATION — mutant 3b: `if (dot >= 0)` vs `if (dot > 0)`
     * common/twlhd_gx_interp.c lines 462-468: shininess is initialized to 0 (line 462)
     * before the guard (line 463). At dot==0, the mutant path computes
     * 2*mul_fixed32(0,0)-4096 = -4096, which the `< 0` clamp (line 468) resets to 0,
     * identical to the initialized value from the correct path.
     * Table lookup (line 471) and specular accumulation (line 475) are outside the guard
     * and see shininess=0 in both paths. No test can distinguish the two guards without
     * a C change (prohibited). This test verifies correct behavior but does NOT kill
     * mutant 3b. EQUIVALENT MUTANT: cannot be killed without C change. */
    uint32_t n = 0u;
    uint32_t words[32];

    clear_state();
    memset(words, 0, sizeof words);
    words[0] = 255u; /* applied entry 0 = 255 */
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31) | 0x8000u);
    add_shininess(&n, words);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    /* Perpendicular normal: dot_h == 0 -> shininess 0 -> table[0]. */
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* (31*31*(255<<4))>>17 = 29. */
    check_lit_rgb(29, 29, 29);
}

static void test_light_negative_dot_specular_ignored(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI,
              rgb555(31, 31, 31) | (rgb555(5, 5, 5) << 16));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    /* dot_h = -4088: only dot > 0 may enter the specular path. */
    add_lit_triangle(&n, normal_word(0, 0, -4088), 1, 1, 1u);
    run_view(n);
    /* A dot >= -100000 branch would add specular 29 and clamp to 31. */
    check_lit_rgb(5, 5, 5);
}

static void test_light_negative_shininess_clamped_to_zero(void) {
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(16, 16, 16) << 16);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI,
              rgb555(16, 31, 31) | (rgb555(9, 0, 0) << 16));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(2, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(0, 0, -4096, 0));
    /* dot_h = 8 -> raw shininess 2*0 - 4096 = -4096; the < 0 clamp keeps the
     * specular term at zero. Emission (9,0,0) + ambient (1,15,15). */
    add_lit_triangle(&n, normal_word(0, 0, 8), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(10, 15, 15);
}

static void test_light_saturating_transform_clamps_both_ends(void) {
    static const uint32_t positive_row[16] = {
        0x7FFFFFFFu, 0u, 0u, 0u,
        0x7FFFFFFFu, 0u, 0u, 0u,
        0u, 0u, 0u, 0u,
        0u, 0u, 0u, 4096u,
    };
    static const uint32_t negative_row[16] = {
        0x80000001u, 0u, 0u, 0u,
        0x80000001u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u,
        0u, 0u, 0u, 4096u,
    };
    uint32_t n = 0u;

    /* Positive overflow: 2 * INT32_MAX * 4088 exceeds 0x7FFFFFFFFFF, so the
     * saturating shift returns INT32_MAX (a fall-through cast would go
     * negative and lose the specular term). */
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR, light_vector_word(-8, 0, -4096, 0));
    add_load_4x4(&n, positive_row);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, 0u);
    add_lit_triangle(&n, normal_word(4088, 4088, 0), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(30, 30, 30);

    /* Negative overflow: the lower clamp must return INT32_MIN so the dot is
     * negative and the final colour stays black. */
    n = 0u;
    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR, light_vector_word(-8, 0, -4096, 0));
    add_load_4x4(&n, negative_row);
    add_entry(&n, TWLHD_V2_CMD_SPE_EMI, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, 0u);
    add_lit_triangle(&n, normal_word(4088, 4088, 0), 1, 1, 1u);
    run_view(n);
    check_lit_rgb(0, 0, 0);
}

static void test_light_mode3_stack_commands_ignored(void) {
    static const uint32_t half[16] = {
        8192u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    static const uint32_t quarter[16] = {
        1024u, 0u, 0u, 0u,
        0u, 4096u, 0u, 0u,
        0u, 0u, 4096u, 0u,
        0u, 0u, 0u, 4096u,
    };
    static const uint32_t zeros[16] = {0u};
    uint32_t n = 0u;

    clear_state();
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_load_4x4(&n, half);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_load_4x4(&n, quarter);
    /* Mode 3 ignores PUSH and matrix loads; POP afterwards restores `half`,
     * not the mode-3-pushed `quarter`. */
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 3u);
    add_entry(&n, TWLHD_V2_CMD_MTX_PUSH, 0u);
    add_load_4x4(&n, zeros);
    add_entry(&n, TWLHD_V2_CMD_MTX_MODE, 2u);
    add_entry(&n, TWLHD_V2_CMD_MTX_POP, 1u);
    add_entry(&n, TWLHD_V2_CMD_DIF_AMB, rgb555(31, 31, 31));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_COLOR, light_colour_word(31, 31, 31, 0));
    add_entry(&n, TWLHD_V2_CMD_LIGHT_VECTOR,
              light_vector_word(-4096, 0, 0, 0));
    add_lit_triangle(&n, normal_word(4088, 0, 0), 1, 1, 1u);
    run_view(n);
    /* vec = half (x2): fixed_diffuse = 8176 saturates the channel to 31. */
    check_lit_rgb(31, 31, 31);
    CHECK_EQ_INT(g_st.ignored_no_effect, 2u);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(void) {
    test_matrix_load_identity();
    test_report("gx_interp/matrix-load-identity");
    test_multiply_order();
    test_report("gx_interp/multiply-order");
    test_multiply_side_non_commuting();
    test_report("gx_interp/multiply-side-non-commuting");
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
    test_clip_matrix_freshness_after_pop_restore();
    test_report("gx_interp/clip-matrix-freshness-after-pop-restore");
    test_clip_matrix_freshness_every_matrix_command();
    test_mode_switch_does_not_change_output();
    test_report("gx_interp/mode-switch-does-not-change-output");
    test_trans_and_scale_by_value();
    test_report("gx_interp/trans-and-scale-by-value");
    test_rounding_negative_fraction();
    test_report("gx_interp/rounding-negative-fraction");
    test_vertex_16_negative();
    test_report("gx_interp/vertex-16-negative");
    test_vertex_10_negative();
    test_report("gx_interp/vertex-10-negative");
    test_vertex_10_sign_extension();
    test_report("gx_interp/vertex-10-sign-extension");
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
    test_quad_strip_six_vertices();
    test_report("gx_interp/quad-strip-six-vertices");
    test_quad_list_leftovers();
    test_report("gx_interp/quad-list-leftovers");
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
    test_swap_stops_interpretation();
    test_report("gx_interp/swap-stops-interpretation");
    test_pop_zero_and_negative_position();
    test_report("gx_interp/pop-zero-and-negative-position");
    test_unterminated_begin();
    test_report("gx_interp/unterminated-begin");
    test_null_args();
    test_report("gx_interp/null-args");
    test_stream_error_partial_output();
    test_report("gx_interp/stream-error-partial-output");
    test_projection_stack_semantics();
    test_report("gx_interp/projection-stack-semantics");
    test_scale_non_unit_z();
    test_report("gx_interp/scale-non-unit-z");
    test_proj_pos_product_order();
    test_report("gx_interp/proj-pos-product-order");
    test_default_colour();
    test_report("gx_interp/default-colour");
    test_alpha_literal_scale();
    test_report("gx_interp/alpha-literal-scale");
    test_alpha_pending_until_begin();
    test_report("gx_interp/alpha-pending-until-begin");
    test_alpha_two_groups();
    test_report("gx_interp/alpha-two-groups");
    test_alpha_default_and_extra_bits();
    test_report("gx_interp/alpha-default-and-extra-bits");
    test_alpha_rgb_unaffected();
    test_report("gx_interp/alpha-rgb-unaffected");
    test_light_commands_not_ignored();
    test_report("gx_interp/light-commands-not-ignored");
    test_light_ambient_only();
    test_report("gx_interp/light-ambient-only");
    test_light_diffuse_only();
    test_report("gx_interp/light-diffuse-only");
    test_light_emission_only();
    test_report("gx_interp/light-emission-only");
    test_light_specular_without_table();
    test_report("gx_interp/light-specular-without-table");
    test_light_specular_with_table();
    test_report("gx_interp/light-specular-with-table");
    test_light_two_lights();
    test_report("gx_interp/light-two-lights");
    test_light_each_index_alone();
    test_report("gx_interp/light-each-index-alone");
    test_light_saturation_at_31();
    test_report("gx_interp/light-saturation-at-31");
    test_light_rotated_vec_normal();
    test_report("gx_interp/light-rotated-vec-normal");
    test_light_mode1_vec_untouched();
    test_report("gx_interp/light-mode1-vec-untouched");
    test_light_scale_scales_position_only();
    test_report("gx_interp/light-scale-position-only");
    test_light_push_pop_restores_vec();
    test_report("gx_interp/light-push-pop-restores-vec");
    test_light_dif_amb_bit15();
    test_report("gx_interp/light-dif-amb-bit15");
    test_light_diffuse_negative_dot_clamped();
    test_report("gx_interp/light-diffuse-negative-dot");
    test_light_shininess_clamp_at_4095();
    test_report("gx_interp/light-shininess-clamp");
    test_light_direction_transformed_at_command_time();
    test_report("gx_interp/light-direction-at-command-time");
    test_light_shininess_applied_only_after_32_words();
    test_report("gx_interp/light-shininess-after-32-words");
    test_light_mask_zero_ignores_configured_lights();
    test_report("gx_interp/light-mask-zero-ignores-lights");
    test_light_mask_one_skips_other_configured_lights();
    test_report("gx_interp/light-mask-one-skips-others");
    test_light_mask_each_index_with_others_configured();
    test_report("gx_interp/light-mask-each-index-others-configured");
    test_light_shininess_clamp_table_entry_127();
    test_report("gx_interp/light-shininess-clamp-table-127");
    test_light_shininess_clamp_uses_4095();
    test_report("gx_interp/light-shininess-clamp-uses-4095");
    test_light_shininess_4096_constant_boundary();
    test_report("gx_interp/light-shininess-4096-constant-boundary");
    test_light_dot_zero_table_entry_zero();
    test_report("gx_interp/light-dot-zero-table-entry-zero");
    test_light_negative_dot_specular_ignored();
    test_report("gx_interp/light-negative-dot-specular");
    test_light_negative_shininess_clamped_to_zero();
    test_report("gx_interp/light-negative-shininess-clamped");
    test_light_saturating_transform_clamps_both_ends();
    test_report("gx_interp/light-saturating-transform");
    test_light_mode3_stack_commands_ignored();
    test_report("gx_interp/light-mode3-stack-ignored");
    return test_exit_code();
}
