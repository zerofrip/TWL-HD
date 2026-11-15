// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_gx_record.c texture-aware serialize path
 * and common/twlhd_tex_source.c.
 */

#include <stdint.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_gx_interp.h"
#include "twlhd_gx_record.h"
#include "twlhd_pica_frame.h"
#include "twlhd_tex_capture.h"
#include "twlhd_tex_source.h"
#include "twlhd_v2_parse.h"
#include "twlhd_v2_write.h"

static TwlhdGxRecorder g_rec;
static TwlhdGxRecTexScratch g_scratch;
static uint8_t g_out[TWLHD_GX_REC_SERIALIZE_TEX_MAX];

static uint8_t g_texel[128];
static uint8_t g_palette[64];
static uint8_t g_index[64];
static uint8_t g_big_texel[131072];

static uint32_t param_of(uint32_t fmt, uint32_t shift_s, uint32_t shift_t,
                         uint32_t offset) {
    return ((fmt & 7u) << 26) | ((shift_s & 7u) << 20) |
           ((shift_t & 7u) << 23) | (offset & 0xFFFFu);
}

static void recorder_begin(void) {
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);
}

static void rec_write(uint32_t addr, uint32_t value) {
    twlhd_gx_rec_write(&g_rec, addr, value, 4u);
}

static void rec_write_to(TwlhdGxRecorder *rec, uint32_t addr,
                         uint32_t value) {
    twlhd_gx_rec_write(rec, addr, value, 4u);
}

static void source_one_key(TwlhdTexSourceCtx *ctx, uint32_t param,
                           uint32_t pltt) {
    uint32_t texel_base = (param & 0xFFFFu) << 3;
    uint32_t palette_base = (pltt << 4); /* I4: non-format-2 uses << 4 */

    twlhd_tex_source_reset(ctx);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_TEXEL,
                                      texel_base, 32u, g_texel),
                 0);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_PALETTE,
                                      palette_base, 32u, g_palette),
                 0);
}

static void source_two_keys(TwlhdTexSourceCtx *ctx, uint32_t param1,
                            uint32_t pltt1, uint32_t param2,
                            uint32_t pltt2) {
    uint32_t texel_base1 = (param1 & 0xFFFFu) << 3;
    uint32_t palette_base1 = (pltt1 << 4);
    uint32_t texel_base2 = (param2 & 0xFFFFu) << 3;
    uint32_t palette_base2 = (pltt2 << 4);

    twlhd_tex_source_reset(ctx);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_TEXEL,
                                      texel_base1, 32u, g_texel),
                 0);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_PALETTE,
                                      palette_base1, 32u, g_palette),
                 0);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_TEXEL,
                                      texel_base2, 32u, g_texel + 32u),
                 0);
    CHECK_EQ_INT(twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_PALETTE,
                                      palette_base2, 32u, g_palette + 32u),
                 0);
}

static void texture_bytes_fill(void) {
    uint32_t i;

    for (i = 0; i < sizeof(g_texel); i++) g_texel[i] = (uint8_t)(0xA0u + i);
    for (i = 0; i < sizeof(g_palette); i++) g_palette[i] = (uint8_t)(0x50u + i);
    for (i = 0; i < sizeof(g_index); i++) g_index[i] = (uint8_t)(0x10u + i);
}

static void draw_textured_quad(uint32_t param, uint32_t pltt) {
    rec_write(0x040004A8u, param);  /* TEXIMAGE_PARAM */
    rec_write(0x040004ACu, pltt);   /* PLTT_BASE */
    rec_write(0x04000500u, 1u);     /* BEGIN_VTXS */
    rec_write(0x0400048Cu, 0u);     /* VTX_16 */
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);     /* END_VTXS */
}

/* ------------------------------------------------------------------------- */
/* 1. One textured quad -> parsed TXDS/TXDT/PLDT.                           */
/* ------------------------------------------------------------------------- */
static void test_one_textured_quad(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    TwlhdV2TexRecord rec;
    size_t size = 0;
    uint32_t param = param_of(3u, 0u, 0u, 0u); /* 8x8 I4 */
    uint32_t pltt = 0u;
    const uint8_t *p;

    texture_bytes_fill();
    recorder_begin();
    source_one_key(&src, param, pltt);
    draw_textured_quad(param, pltt);
    rec_write(0x04000540u, 0u); /* SWAP_BUFFERS */

    twlhd_tex_capture_default_limits(&limits);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK(size > 0u);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(view.gxcm_count, 9u);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, view.gxcm_count - 1u),
                  TWLHD_V2_CMD_SWAP_BUFFERS);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.txdt_size, 32u);
    CHECK_EQ_INT(view.pldt_size, 32u);

    CHECK(twlhd_v2_txds_record(&view, 0u, &rec));
    CHECK_EQ_INT(rec.teximage_param, param);
    CHECK_EQ_INT(rec.pltt_base, pltt);

    p = twlhd_v2_tex_texel_ptr(&view, 0u);
    CHECK(p != NULL);
    CHECK(memcmp(p, g_texel, 32u) == 0);
    p = twlhd_v2_tex_palette_ptr(&view, 0u);
    CHECK(p != NULL);
    CHECK(memcmp(p, g_palette, 32u) == 0);

    test_report("gx-record-tex/one-quad");
}

/* ------------------------------------------------------------------------- */
/* 2. Two distinct keys, both kept.                                         */
/* ------------------------------------------------------------------------- */
static void test_two_keys(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    size_t size = 0;
    uint32_t param1 = param_of(3u, 0u, 0u, 0u);
    uint32_t pltt1 = 0u;
    uint32_t param2 = param_of(3u, 0u, 0u, 4u); /* texel base 4 << 3 = 32 */
    uint32_t pltt2 = 2u;                        /* palette base 2 << 4 = 32 */

    texture_bytes_fill();
    recorder_begin();
    source_two_keys(&src, param1, pltt1, param2, pltt2);

    rec_write(0x040004A8u, param1);
    rec_write(0x040004ACu, pltt1);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x040004A8u, param2);
    rec_write(0x040004ACu, pltt2);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x04000540u, 0u);

    twlhd_tex_capture_default_limits(&limits);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 2u);
    CHECK_EQ_INT(view.txdt_size, 64u);
    CHECK_EQ_INT(view.pldt_size, 64u);
    CHECK(memcmp(view.txdt + 0u, g_texel, 32u) == 0);
    CHECK(memcmp(view.txdt + 32u, g_texel + 32u, 32u) == 0);
    CHECK(memcmp(view.pldt + 0u, g_palette, 32u) == 0);
    CHECK(memcmp(view.pldt + 32u, g_palette + 32u, 32u) == 0);

    test_report("gx-record-tex/two-keys");
}

/* ------------------------------------------------------------------------- */
/* 2b. Large texture exercises the full default TXDT budget.                */
/* ------------------------------------------------------------------------- */
static void test_large_texture_budget(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    size_t size = 0;
    /* Format 7 (16-bit direct), 256x256, texel base 0. */
    uint32_t param = param_of(7u, 5u, 5u, 0u);

    memset(g_big_texel, 0xBBu, sizeof(g_big_texel));
    recorder_begin();
    twlhd_tex_source_reset(&src);
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL, 0u,
                                      sizeof(g_big_texel), g_big_texel),
                  0);

    rec_write(0x040004A8u, param);
    rec_write(0x040004ACu, 0u);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);
    rec_write(0x04000540u, 0u);

    twlhd_tex_capture_default_limits(&limits);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.txdt_size, sizeof(g_big_texel));
    CHECK_EQ_INT(view.pldt_size, 0u);
    CHECK(memcmp(view.txdt, g_big_texel, sizeof(g_big_texel)) == 0);

    test_report("gx-record-tex/large-texture-budget");
}

/* ------------------------------------------------------------------------- */
/* 3. Budget drop: file OVERFLOW, recorder flags clean, others kept.        */
/* ------------------------------------------------------------------------- */
static void test_budget_drop(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    TwlhdTexCapStats stats;
    size_t size = 0;
    uint32_t param1 = param_of(3u, 0u, 0u, 0u);
    uint32_t pltt1 = 0u;
    uint32_t param2 = param_of(3u, 0u, 0u, 4u);
    uint32_t pltt2 = 2u;

    texture_bytes_fill();
    recorder_begin();
    source_two_keys(&src, param1, pltt1, param2, pltt2);

    rec_write(0x040004A8u, param1);
    rec_write(0x040004ACu, pltt1);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x040004A8u, param2);
    rec_write(0x040004ACu, pltt2);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x04000540u, 0u);

    /* Exactly one 8x8 I4 key fits. */
    limits.max_txdt_bytes = 32u;
    limits.max_pldt_bytes = 32u;
    limits.max_records = TWLHD_V2_TXDS_MAX_RECORDS;

    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src,
                                            &stats),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags,
                  TWLHD_V2_FLAG_FRAME_COMPLETE | TWLHD_V2_FLAG_OVERFLOW);
    CHECK_EQ_INT(twlhd_v2_gxcm_cmd_word(&view, view.gxcm_count - 1u),
                  TWLHD_V2_CMD_SWAP_BUFFERS);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(stats.keys_kept, 1u);
    CHECK_EQ_INT(stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(stats.keys_dropped_read_error, 0u);
    CHECK_EQ_INT(stats.keys_dropped_unsupported, 0u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_OVERFLOW, 0u);
    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 0);

    test_report("gx-record-tex/budget-drop");
}

/* ------------------------------------------------------------------------- */
/* 4. Read-error drop: unregistered palette -> OVERFLOW file, kept key ok.  */
/* ------------------------------------------------------------------------- */
static void test_read_error_drop(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    TwlhdTexCapStats stats;
    size_t size = 0;
    uint32_t param1 = param_of(3u, 0u, 0u, 0u);
    uint32_t pltt1 = 0u;
    uint32_t param2 = param_of(3u, 0u, 0u, 4u);
    uint32_t pltt2 = 2u; /* palette region deliberately not registered */

    texture_bytes_fill();
    recorder_begin();
    /* Only first key's regions. */
    source_one_key(&src, param1, pltt1);
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                      (param2 & 0xFFFFu) << 3, 32u,
                                      g_texel + 32u),
                  0);

    rec_write(0x040004A8u, param1);
    rec_write(0x040004ACu, pltt1);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x040004A8u, param2);
    rec_write(0x040004ACu, pltt2);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x04000540u, 0u);

    twlhd_tex_capture_default_limits(&limits);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src,
                                            &stats),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags,
                  TWLHD_V2_FLAG_FRAME_COMPLETE | TWLHD_V2_FLAG_OVERFLOW);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(stats.keys_dropped_read_error, 1u);
    CHECK_EQ_INT(stats.keys_dropped_budget, 0u);
    CHECK_EQ_INT(stats.keys_dropped_unsupported, 0u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_OVERFLOW, 0u);

    test_report("gx-record-tex/read-error-drop");
}

/* ------------------------------------------------------------------------- */
/* 5. No-texture frame: byte-identical to twlhd_gx_rec_serialize.           */
/* ------------------------------------------------------------------------- */
static void test_no_texture_byte_identical(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    size_t size1 = 0;
    size_t size2 = 0;

    recorder_begin();
    rec_write(0x04000440u, 2u);      /* MTX_MODE */
    rec_write(0x04000454u, 0u);      /* MTX_TRANS */
    rec_write(0x04000500u, 1u);      /* BEGIN_VTXS */
    rec_write(0x0400048Cu, 0u);      /* VTX_16 */
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);      /* END_VTXS */
    rec_write(0x04000540u, 0u);      /* SWAP_BUFFERS */

    twlhd_tex_source_reset(&src);
    twlhd_tex_capture_default_limits(&limits);

    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof(g_out), &size1),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size2, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(size1, size2);
    /* g_out now holds the texture-aware serialize; compare against a fresh
     * buffer from the non-texture path. */
    {
        uint8_t other[TWLHD_GX_REC_SERIALIZE_MAX];
        CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, other, sizeof(other),
                                            &size1),
                      TWLHD_V2_OK);
        CHECK(memcmp(g_out, other, size2) == 0);
    }

    test_report("gx-record-tex/no-texture-identical");
}

/* ------------------------------------------------------------------------- */
/* 6. Too-small output buffer: error, out_size untouched.                   */
/* ------------------------------------------------------------------------- */
static void test_small_output_buffer(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    size_t size = 0xDEADBEEFu;
    uint32_t param = param_of(3u, 0u, 0u, 0u);

    texture_bytes_fill();
    recorder_begin();
    source_one_key(&src, param, 0u);
    draw_textured_quad(param, 0u);
    rec_write(0x04000540u, 0u);

    twlhd_tex_capture_default_limits(&limits);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out, 32u,
                                            &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_ERR_SIZE);
    CHECK_EQ_INT(size, 0xDEADBEEFu);

    test_report("gx-record-tex/small-buffer");
}

/* ------------------------------------------------------------------------- */
/* 7. NULL arguments.                                                       */
/* ------------------------------------------------------------------------- */
static void test_null_args(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    size_t size = 0;
    uint32_t param = param_of(3u, 0u, 0u, 0u);

    recorder_begin();
    source_one_key(&src, param, 0u);
    draw_textured_quad(param, 0u);
    rec_write(0x04000540u, 0u);
    twlhd_tex_capture_default_limits(&limits);

    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(NULL, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, NULL, g_out, sizeof(g_out),
                                            &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, NULL,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, NULL,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_ERR_NULL);
    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            NULL, &src, NULL),
                  TWLHD_V2_ERR_NULL);

    test_report("gx-record-tex/null-args");
}

/* ------------------------------------------------------------------------- */
/* 8. Source-buffer backend unit cases.                                     */
/* ------------------------------------------------------------------------- */
static void test_source_backend(void) {
    TwlhdTexSourceCtx src;
    uint8_t buf[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t high_buf[256];
    uint8_t tmp[16];
    uint32_t i;

    for (i = 0; i < sizeof(high_buf); i++) high_buf[i] = (uint8_t)(0xC0u + i);

    twlhd_tex_source_reset(&src);
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL, 0x100u,
                                      16u, buf),
                  0);

    /* Registered exact read. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL, 0x100u,
                                        8u, tmp),
                  0);
    CHECK(memcmp(tmp, buf, 8u) == 0);

    /* Exact end read. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0x100u + 8u, 8u, tmp),
                  0);
    CHECK(memcmp(tmp, buf + 8u, 8u) == 0);

    /* Partial overlap at the end must fail and copy nothing. */
    memset(tmp, 0xCCu, sizeof(tmp));
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0x100u + 12u, 8u, tmp),
                  1);
    CHECK_EQ_INT(tmp[0], 0xCCu);

    /* Unregistered offset. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL, 0u, 1u,
                                        tmp),
                  1);

    /* Unknown kind. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, 99u, 0x100u, 1u, tmp), 1);

    /* Zero-length read succeeds inside the region, fails outside. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0x100u + 15u, 0u, tmp),
                  0);
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0x100u + 16u, 0u, tmp),
                  1);

    /* Probe (dst == NULL) reports availability without copying. */
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL, 0x100u,
                                        8u, NULL),
                  0);
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL, 0x200u,
                                        1u, NULL),
                  1);

    /* uint32 wraparound of offset+bytes: a region near 0xFFFFFFFF must still
     * fail when rel+bytes exceeds its length (not wrap back to a small value). */
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                      0xFFFFFF00u, 0x100u, high_buf),
                  0);
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0xFFFFFF80u, 0x100u, tmp),
                  1);
    CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                        0xFFFFFFFFu, 1u, tmp),
                  0);
    CHECK_EQ_INT(tmp[0], high_buf[0xFFu]);

    /* Adding a zero-length region is rejected. */
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_PALETTE, 0u,
                                      0u, buf),
                  1);

    test_report("gx-record-tex/source-backend");
}

/* ------------------------------------------------------------------------- */
/* 8b. Source-buffer backend region capacity and null-argument handling.     */
/* ------------------------------------------------------------------------- */
static void test_source_region_capacity(void) {
    TwlhdTexSourceCtx src;
    uint8_t bufs[TWLHD_TEX_SOURCE_REGION_MAX][1];
    uint32_t i;

    twlhd_tex_source_reset(&src);
    for (i = 0; i < TWLHD_TEX_SOURCE_REGION_MAX; i++) {
        bufs[i][0] = (uint8_t)i;
        CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                          i, 1u, bufs[i]),
                     0);
    }
    for (i = 0; i < TWLHD_TEX_SOURCE_REGION_MAX; i++) {
        uint8_t tmp = 0xFF;
        CHECK_EQ_INT(twlhd_tex_source_read(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                           i, 1u, &tmp),
                     0);
        CHECK_EQ_INT(tmp, (uint8_t)i);
    }
    /* Table full: one more add must fail. */
    CHECK(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL,
                               0x1000u, 1u, bufs[0]) != 0);

    test_report("gx-record-tex/source-region-capacity");
}

static void test_source_null_args(void) {
    TwlhdTexSourceCtx src;
    uint8_t buf[16];

    twlhd_tex_source_reset(&src);
    CHECK(twlhd_tex_source_add(NULL, TWLHD_TEX_CAP_READ_TEXEL, 0u, 1u, buf) !=
          0);
    CHECK(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL, 0u, 1u,
                               NULL) != 0);
    CHECK(twlhd_tex_source_read(NULL, TWLHD_TEX_CAP_READ_TEXEL, 0u, 1u,
                                buf) != 0);

    test_report("gx-record-tex/source-null-args");
}

/* ------------------------------------------------------------------------- */
/* 9. pica_frame status: kept key OK, dropped key NO_DATA.                  */
/* ------------------------------------------------------------------------- */
static void test_pica_frame_status(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    TwlhdGxTexVertex tex[6];
    TwlhdPicaBindPlan plan[2];
    TwlhdPicaFrameTexture textures[2];
    TwlhdPicaTexCoord texcoords[6];
    int32_t entry_texture[2];
    TwlhdPicaFrameEntryStatus entry_status[2];
    uint8_t arena[256];
    uint8_t scratch[256];
    uint32_t entry_count = 0;
    uint32_t texture_count = 0;
    uint32_t arena_used = 0;
    size_t size = 0;
    uint32_t i;
    uint32_t param1 = param_of(3u, 0u, 0u, 0u);
    uint32_t pltt1 = 0u;
    uint32_t param2 = param_of(3u, 0u, 0u, 4u);
    uint32_t pltt2 = 2u;

    texture_bytes_fill();
    recorder_begin();
    source_two_keys(&src, param1, pltt1, param2, pltt2);

    rec_write(0x040004A8u, param1);
    rec_write(0x040004ACu, pltt1);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x040004A8u, param2);
    rec_write(0x040004ACu, pltt2);
    rec_write(0x04000500u, 1u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x0400048Cu, 0u);
    rec_write(0x04000504u, 0u);

    rec_write(0x04000540u, 0u);

    /* Drop the second key by budget. */
    limits.max_txdt_bytes = 32u;
    limits.max_pldt_bytes = 32u;
    limits.max_records = TWLHD_V2_TXDS_MAX_RECORDS;

    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);

    for (i = 0; i < 3u; i++) {
        tex[i].s = 0.0f;
        tex[i].t = 0.0f;
        tex[i].teximage_param = param1;
        tex[i].pltt_base = pltt1;
    }
    for (i = 3u; i < 6u; i++) {
        tex[i].s = 0.0f;
        tex[i].t = 0.0f;
        tex[i].teximage_param = param2;
        tex[i].pltt_base = pltt2;
    }

    CHECK_EQ_INT(twlhd_pica_frame_prepare(
                      tex, 6u, &view, plan, 2u, textures, 2u, arena,
                      (uint32_t)sizeof(arena), texcoords, 6u, entry_texture,
                      entry_status, 2u, scratch, (uint32_t)sizeof(scratch),
                      &entry_count, &texture_count, &arena_used),
                  TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(entry_count, 2u);
    CHECK_EQ_INT(texture_count, 1u);
    CHECK_EQ_INT(entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(entry_status[1], TWLHD_PICA_FRAME_ENTRY_NO_DATA);
    CHECK_EQ_INT(entry_texture[0], 0);
    CHECK_EQ_INT(entry_texture[1], -1);

    test_report("gx-record-tex/pica-frame-status");
}

/* ------------------------------------------------------------------------- */
/* 10. Recorder-level GXCM overflow still sets OVERFLOW and refuses gate.   */
/* ------------------------------------------------------------------------- */
static void test_recorder_overflow_unchanged(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    TwlhdV2View view;
    size_t size = 0;
    uint32_t i;

    recorder_begin();
    twlhd_tex_source_reset(&src);
    twlhd_tex_capture_default_limits(&limits);

    for (i = 0; i < TWLHD_GX_REC_MAX_ENTRIES + 1u; i++) {
        rec_write(0x04000500u, 1u); /* BEGIN_VTXS repeatedly to overflow */
    }

    CHECK_EQ_INT(twlhd_gx_rec_overflowed(&g_rec), 1);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec) & TWLHD_V2_FLAG_OVERFLOW,
                  TWLHD_V2_FLAG_OVERFLOW);

    CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(&g_rec, &g_scratch, g_out,
                                            sizeof(g_out), &size, &limits,
                                            twlhd_tex_source_read, &src, NULL),
                  TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags & TWLHD_V2_FLAG_OVERFLOW,
                  TWLHD_V2_FLAG_OVERFLOW);
    CHECK_EQ_INT(view.flags & TWLHD_V2_FLAG_FRAME_COMPLETE, 0u);

    test_report("gx-record-tex/recorder-overflow");
}

/* ------------------------------------------------------------------------- */
/* 11. OOB guard harness: 0x00/0xFF two-pass over every output buffer.      */
/* ------------------------------------------------------------------------- */
#define OOB_GUARD 128u
#define OOB_FILL 0x5Bu

static _Alignas(8) uint8_t
    g_oob_scratch[OOB_GUARD + sizeof(TwlhdGxRecTexScratch) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_out[OOB_GUARD + TWLHD_GX_REC_SERIALIZE_TEX_MAX + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_rec[OOB_GUARD + sizeof(TwlhdGxRecorder) + OOB_GUARD];

#define OOB_SCRATCH ((TwlhdGxRecTexScratch *)(g_oob_scratch + OOB_GUARD))
#define OOB_OUT (g_oob_out + OOB_GUARD)
#define OOB_REC ((TwlhdGxRecorder *)(g_oob_rec + OOB_GUARD))

static int guard_intact(const uint8_t *base, size_t used, int fill) {
    size_t i;

    for (i = 0; i < OOB_GUARD; i++) {
        if (base[i] != (uint8_t)fill) return 0;
        if (base[OOB_GUARD + used + i] != (uint8_t)fill) return 0;
    }
    return 1;
}

static void test_out_of_bounds(void) {
    TwlhdTexSourceCtx src;
    TwlhdTexCapLimits limits;
    uint8_t scratch_copy[2][sizeof(g_oob_scratch)];
    uint8_t out_copy[2][sizeof(g_oob_out)];
    uint32_t pass;
    uint32_t param = param_of(3u, 0u, 0u, 0u);

    texture_bytes_fill();
    twlhd_tex_source_reset(&src);
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_TEXEL,
                                      (param & 0xFFFFu) << 3, 32u, g_texel),
                  0);
    CHECK_EQ_INT(twlhd_tex_source_add(&src, TWLHD_TEX_CAP_READ_PALETTE,
                                      0u, 32u, g_palette),
                  0);
    twlhd_tex_capture_default_limits(&limits);

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        size_t size = 0;

        memset(g_oob_rec, fill, sizeof(g_oob_rec));
        twlhd_gx_rec_init(OOB_REC);
        twlhd_gx_rec_arm(OOB_REC);
        rec_write_to(OOB_REC, 0x040004A8u, param);
        rec_write_to(OOB_REC, 0x040004ACu, 0u);
        rec_write_to(OOB_REC, 0x04000500u, 1u);
        rec_write_to(OOB_REC, 0x0400048Cu, 0u);
        rec_write_to(OOB_REC, 0x0400048Cu, 0u);
        rec_write_to(OOB_REC, 0x0400048Cu, 0u);
        rec_write_to(OOB_REC, 0x0400048Cu, 0u);
        rec_write_to(OOB_REC, 0x04000504u, 0u);
        rec_write_to(OOB_REC, 0x04000540u, 0u);

        memset(g_oob_scratch, OOB_FILL, sizeof(g_oob_scratch));
        memset(g_oob_out, OOB_FILL, sizeof(g_oob_out));

        CHECK_EQ_INT(twlhd_gx_rec_serialize_tex(
                          OOB_REC, OOB_SCRATCH, OOB_OUT,
                          TWLHD_GX_REC_SERIALIZE_TEX_MAX, &size, &limits,
                          twlhd_tex_source_read, &src, NULL),
                      TWLHD_V2_OK);
        CHECK(size > 0u);

        CHECK(guard_intact(g_oob_scratch, sizeof(TwlhdGxRecTexScratch),
                            OOB_FILL));
        CHECK(guard_intact(g_oob_out, size, OOB_FILL));
        CHECK(guard_intact(g_oob_rec, sizeof(TwlhdGxRecorder), fill));

        memcpy(scratch_copy[pass], g_oob_scratch, sizeof(g_oob_scratch));
        memcpy(out_copy[pass], g_oob_out, sizeof(g_oob_out));
    }

    CHECK(memcmp(scratch_copy[0], scratch_copy[1], sizeof(g_oob_scratch)) ==
          0);
    CHECK(memcmp(out_copy[0], out_copy[1], sizeof(g_oob_out)) == 0);

    test_report("gx-record-tex/oob-guards");
}

int main(void) {
    test_one_textured_quad();
    test_two_keys();
    test_large_texture_budget();
    test_budget_drop();
    test_read_error_drop();
    test_no_texture_byte_identical();
    test_small_output_buffer();
    test_null_args();
    test_source_backend();
    test_source_region_capacity();
    test_source_null_args();
    test_pica_frame_status();
    test_recorder_overflow_unchanged();
    test_out_of_bounds();
    return test_exit_code();
}
