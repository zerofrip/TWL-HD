// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5C-1 - host tests for the texture capture planner.
 *
 * Every expected number is literal and the arithmetic is in a comment. The
 * fake memory is one flat array; the read callback serves the three address
 * spaces from it and can inject per-kind failures.
 */
#include <stdint.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_gx_interp.h"
#include "twlhd_pica_frame.h"
#include "twlhd_tex_capture.h"
#include "twlhd_v2_parse.h"
#include "twlhd_v2_write.h"

#define MEM_MAX 0x60000u

static uint8_t g_mem[MEM_MAX];
static const uint8_t *g_mem_ptr = g_mem;
static uint32_t g_mem_size;
static uint32_t g_fail;
static uint32_t g_data_reads[3];

#define FAIL_TEXEL 0x1u
#define FAIL_PALETTE 0x2u
#define FAIL_INDEX 0x4u        /* every index access fails */
#define FAIL_INDEX_SMALL 0x8u  /* only 2-byte content reads fail (fallback) */

static int fake_read(void *ctx, uint32_t kind, uint32_t offset, uint32_t bytes,
                     uint8_t *dst) {
    (void)ctx;
    if (kind > 2u) return 1;
    if (kind == TWLHD_TEX_CAP_READ_TEXEL) {
        if (g_fail & FAIL_TEXEL) return 1;
    } else if (kind == TWLHD_TEX_CAP_READ_PALETTE) {
        if (g_fail & FAIL_PALETTE) return 1;
    } else {
        if (g_fail & FAIL_INDEX) return 1;
        if (dst && bytes == 2u && (g_fail & FAIL_INDEX_SMALL)) return 1;
    }
    if ((uint64_t)offset + (uint64_t)bytes > (uint64_t)g_mem_size) return 1;
    if (dst) {
        memcpy(dst, g_mem_ptr + offset, bytes);
        g_data_reads[kind]++;
    }
    return 0;
}

static void mem_reset(uint32_t size) {
    uint32_t i;

    g_mem_ptr = g_mem;
    g_mem_size = size <= MEM_MAX ? size : MEM_MAX;
    g_fail = 0;
    memset(g_data_reads, 0, sizeof(g_data_reads));
    for (i = 0; i < g_mem_size; i++) {
        g_mem[i] = (uint8_t)(i * 37u + 11u);
    }
}

#define GXCM_MAX 1024u
static TwlhdV2CmdEntry g_gxcm[GXCM_MAX];
static uint32_t g_gxcm_count;

static void gxcm_reset(void) { g_gxcm_count = 0; }

static void gxcm_add(uint32_t id, uint32_t value) {
    g_gxcm[g_gxcm_count].cmd_word = id;
    g_gxcm[g_gxcm_count].value = value;
    g_gxcm_count++;
}

static uint32_t param_of(uint32_t fmt, uint32_t shift_s, uint32_t shift_t,
                         uint32_t offset) {
    return ((fmt & 7u) << 26) | ((shift_s & 7u) << 20) |
           ((shift_t & 7u) << 23) | (offset & 0xFFFFu);
}

static TwlhdTexCapPlan g_plan;
static TwlhdTexCapPlan g_saved_plan;
static uint8_t g_txdt[70000];
static uint8_t g_pldt[100000];
static TwlhdV2TexEntry g_entries[TWLHD_V2_TXDS_MAX_RECORDS];

static TwlhdTexCapLimits limits_default(void) {
    TwlhdTexCapLimits limits;

    twlhd_tex_capture_default_limits(&limits);
    return limits;
}

static TwlhdTexCapResult plan_now(const TwlhdTexCapLimits *limits) {
    return twlhd_tex_capture_plan(limits, g_gxcm, g_gxcm_count, fake_read, NULL,
                                  &g_plan);
}

static TwlhdTexCapResult apply_now(void) {
    return twlhd_tex_capture_apply(
        &g_plan, fake_read, NULL, g_txdt, (uint32_t)sizeof(g_txdt), g_pldt,
        (uint32_t)sizeof(g_pldt), g_entries, TWLHD_V2_TXDS_MAX_RECORDS);
}

static void set_words_at(uint32_t offset, const uint16_t *words,
                         uint32_t count) {
    uint32_t i;

    for (i = 0; i < count; i++) {
        g_mem[offset + i * 2u] = (uint8_t)(words[i] & 0xFFu);
        g_mem[offset + i * 2u + 1u] = (uint8_t)(words[i] >> 8);
    }
}

static void set_index_words(uint32_t param, const uint16_t *words,
                            uint32_t count) {
    set_words_at(0x20000u + ((param & 0x3FFFu) << 2), words, count);
}

/* ------------------------------------------------------------------------- */
/* 1. One textured draw: sizes, offsets, bytes copied.                       */
/* ------------------------------------------------------------------------- */

static void test_single_draw(void) {
    TwlhdTexCapLimits limits = limits_default();
    /* I4 8x8 = format 3, shifts 0/0, texel base 4*8 = 32.
     * 64 texels * 1/2 B = 32 texel bytes; I4 palette table = 16 * 2 = 32 B. */
    uint32_t param = param_of(3u, 0u, 0u, 4u);
    uint32_t pltt = 0x12u; /* palette byte base 0x12 << 4 = 0x120 */

    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, pltt);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_unsupported, 0u);
    CHECK_EQ_INT(g_plan.stats.conflicts_same_key, 0u);
    CHECK_EQ_INT(g_plan.stats.bytes_txdt, 32u);
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, 32u);
    CHECK_EQ_INT(g_plan.stats.overflow, 0);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.records[0].teximage_param, param);
    CHECK_EQ_INT(g_plan.records[0].pltt_base, pltt);
    CHECK_EQ_INT(g_plan.records[0].texel_bytes, 32u);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 32u);
    CHECK_EQ_INT(g_plan.records[0].index_bytes, 0u);
    CHECK_EQ_INT(g_plan.records[0].texel_offset, 0u);
    CHECK_EQ_INT(g_plan.records[0].palette_offset, 0u);
    CHECK_EQ_INT(g_plan.records[0].index_offset, 0u);
    /* The clamped default caps are reported back. */
    CHECK_EQ_INT(g_plan.limits.max_txdt_bytes, 131072u);
    CHECK_EQ_INT(g_plan.limits.max_pldt_bytes, 32768u);
    CHECK_EQ_INT(g_plan.limits.max_records, 256u);

    memset(g_txdt, 0xEE, sizeof(g_txdt));
    memset(g_pldt, 0xEE, sizeof(g_pldt));
    memset(g_entries, 0xEE, sizeof(g_entries));
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK(g_entries[0].texel == g_txdt);
    CHECK(g_entries[0].palette == g_pldt);
    CHECK(g_entries[0].index == NULL);
    CHECK_EQ_INT(g_entries[0].texel_bytes, 32u);
    CHECK_EQ_INT(g_entries[0].palette_bytes, 32u);
    CHECK_EQ_INT(g_entries[0].index_bytes, 0u);
    /* texel address = (param & 0xFFFF) << 3 = 4 << 3 = 32. */
    CHECK(memcmp(g_txdt, g_mem + 32u, 32u) == 0);
    /* palette address = pltt << 4 = 0x120. */
    CHECK(memcmp(g_pldt, g_mem + 0x120u, 32u) == 0);

    test_report("tex-capture/single-draw");
}

/* ------------------------------------------------------------------------- */
/* 2. Two draws with the same key: one record, no conflict.                  */
/* ------------------------------------------------------------------------- */

static void test_same_key_dedupe(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param = param_of(3u, 0u, 0u, 0x40u);

    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    /* Two VTX_10 entries are two separate one-entry commands. */
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.stats.conflicts_same_key, 0u);
    CHECK_EQ_INT(g_plan.record_count, 1u);

    /* The plan only probes; the single snapshot is read once in apply. */
    CHECK_EQ_INT(g_data_reads[TWLHD_TEX_CAP_READ_TEXEL], 0u);
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_data_reads[TWLHD_TEX_CAP_READ_TEXEL], 1u);
    CHECK_EQ_INT(g_data_reads[TWLHD_TEX_CAP_READ_PALETTE], 1u);

    test_report("tex-capture/dedupe");
}

/* ------------------------------------------------------------------------- */
/* 3. Same key, register rewritten between draws: first record, conflict.   */
/* ------------------------------------------------------------------------- */

static void test_same_key_conflict(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param = param_of(3u, 0u, 0u, 0x80u);

    mem_reset(0x800u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    /* Rewrite both registers with the same values, then draw again. */
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    /* A third use after only a TEXIMAGE_PARAM rewrite is a second conflict. */
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    /* D7: the first-use record is the only snapshot; the rewrites are counted
     * but no bytes are hashed or re-read. */
    CHECK_EQ_INT(g_plan.stats.conflicts_same_key, 2u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 1u);
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_data_reads[TWLHD_TEX_CAP_READ_TEXEL], 1u);
    CHECK(memcmp(g_txdt, g_mem + 0x400u, 32u) == 0);

    test_report("tex-capture/conflict");
}

/* ------------------------------------------------------------------------- */
/* 4. A key that is set but never drawn is not captured.                    */
/* ------------------------------------------------------------------------- */

static void test_unused_key(void) {
    TwlhdTexCapLimits limits = limits_default();

    mem_reset(0x400u);
    gxcm_reset();
    /* Set both registers, then an empty BEGIN..END group. */
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 0u, 0u, 0x40u));
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    /* A VTX outside BEGIN is not a draw. */
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(g_plan.stats.bytes_txdt, 0u);
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, 0u);

    test_report("tex-capture/unused-key");
}

/* ------------------------------------------------------------------------- */
/* 5. Format 0 (untextured) never needs texture data.                       */
/* ------------------------------------------------------------------------- */

static void test_format_zero(void) {
    TwlhdTexCapLimits limits = limits_default();

    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(0u, 0u, 0u, 0x40u));
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    test_report("tex-capture/format-zero");
}

static void test_default_registers(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param = param_of(3u, 0u, 0u, 0x40u);

    /* No PLTT_BASE write: the latch starts at 0, palette byte 0. */
    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.records[0].pltt_base, 0u);
    CHECK_EQ_INT(g_plan.records[0].palette_offset, 0u);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 32u);

    /* A VTX before any BEGIN is not a draw. */
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    /* A non-VTX command inside BEGIN is not a draw. */
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_TEX_COORD, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    test_report("tex-capture/default-registers");
}

/* ------------------------------------------------------------------------- */
/* 6. All seven formats plus non-square and large textures.                 */
/* ------------------------------------------------------------------------- */

static void test_format_sizes(void) {
    static const struct {
        uint32_t fmt, shift_s, shift_t, offset;
        uint32_t texel_bytes, palette_bytes, index_bytes;
    } cases[] = {
        /* A3I5 8x8: 64 texels * 1 B = 64; palette 32 entries * 2 = 64. */
        {1u, 0u, 0u, 0x0100u, 64u, 64u, 0u},
        /* I2 8x8: 64 * 1/4 B = 16; palette 4 * 2 = 8. */
        {2u, 0u, 0u, 0x0200u, 16u, 8u, 0u},
        /* I4 8x8: 64 * 1/2 B = 32; palette 16 * 2 = 32. */
        {3u, 0u, 0u, 0x0300u, 32u, 32u, 0u},
        /* I8 8x8: 64 * 1 B = 64; palette 256 * 2 = 512. */
        {4u, 0u, 0u, 0x0400u, 64u, 512u, 0u},
        /* 4x4 8x8: 64 / 4 = 16 texel, 64 / 8 = 8 index; zero index words
         * give window 4*0 + 8 = 8. */
        {5u, 0u, 0u, 0x0500u, 16u, 8u, 8u},
        /* A5I3 8x8: 64 * 1 B = 64; palette 8 * 2 = 16. */
        {6u, 0u, 0u, 0x0600u, 64u, 16u, 0u},
        /* 16bpp 8x8: 64 * 2 B = 128; no palette. */
        {7u, 0u, 0u, 0x0700u, 128u, 0u, 0u},
        /* I4 16x8: 128 * 1/2 B = 64; palette 32. */
        {3u, 1u, 0u, 0x0800u, 64u, 32u, 0u},
        /* I4 16x16: 256 * 1/2 B = 128; palette 32. */
        {3u, 1u, 1u, 0x0900u, 128u, 32u, 0u},
        /* I4 256x256: 65536 * 1/2 B = 32768; palette 32. */
        {3u, 5u, 5u, 0x1000u, 32768u, 32u, 0u},
    };
    const uint32_t count = (uint32_t)(sizeof(cases) / sizeof(cases[0]));
    TwlhdTexCapLimits limits = limits_default();
    uint32_t i;
    uint32_t txdt_total = 0;
    uint32_t pldt_total = 0;

    mem_reset(0x22000u);
    /* Four zero 16-bit index entries at the 4x4 index base. */
    {
        static const uint16_t zeros[4] = {0u, 0u, 0u, 0u};
        set_index_words(param_of(5u, 0u, 0u, 0x0500u), zeros, 4u);
    }
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    for (i = 0; i < count; i++) {
        gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM,
                 param_of(cases[i].fmt, cases[i].shift_s, cases[i].shift_t,
                          cases[i].offset));
        /* Cover the whole VTX command range 0x23..0x28. */
        gxcm_add(TWLHD_V2_CMD_VTX_16 + (i % 6u), 0u);
    }
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, count);
    CHECK_EQ_INT(g_plan.stats.keys_kept, count);
    CHECK_EQ_INT(g_plan.record_count, count);
    for (i = 0; i < count; i++) {
        CHECK_EQ_INT(g_plan.records[i].texel_bytes, cases[i].texel_bytes);
        CHECK_EQ_INT(g_plan.records[i].palette_bytes, cases[i].palette_bytes);
        CHECK_EQ_INT(g_plan.records[i].index_bytes, cases[i].index_bytes);
        CHECK_EQ_INT(g_plan.records[i].index_offset,
                     cases[i].index_bytes != 0u
                         ? g_plan.records[i].texel_offset +
                               cases[i].texel_bytes
                         : 0u);
        txdt_total += cases[i].texel_bytes + cases[i].index_bytes;
        pldt_total += cases[i].palette_bytes;
    }
    /* 64+16+32+64+16+8+64+128+64+128+32768 = 33352. */
    CHECK_EQ_INT(g_plan.stats.bytes_txdt, 33352u);
    CHECK_EQ_INT(g_plan.stats.bytes_txdt, txdt_total);
    /* 64+8+32+512+8+16+0+32+32+32 = 736. */
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, 736u);
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, pldt_total);

    test_report("tex-capture/format-sizes");
}

/* ------------------------------------------------------------------------- */
/* 7. Format-5 minimal window, edge, evenness and full-window fallback.     */
/* ------------------------------------------------------------------------- */

static void test_fmt5_windows(void) {
    TwlhdTexCapLimits limits = limits_default();
    TwlhdTexCapLimits full;
    uint32_t param = param_of(5u, 0u, 0u, 0x40u);
    uint16_t words[4];

    twlhd_tex_capture_full_limits(&full);

    /* max entry 3: 4*3 + 8 = 20 bytes (already even/4-aligned). */
    mem_reset(0x21000u);
    words[0] = 3u;
    words[1] = 0u;
    words[2] = 0u;
    words[3] = 0u;
    set_index_words(param, words, 4u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 20u);
    CHECK_EQ_INT(g_plan.records[0].index_bytes, 8u); /* 64 texels / 8 */

    /* max entry 2: 4*2 + 8 = 16. */
    words[0] = 2u;
    set_index_words(param, words, 4u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 16u);

    /* max entry 1: 4*1 + 8 = 12. */
    words[0] = 1u;
    set_index_words(param, words, 4u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 12u);

    /* The maximum may be in the LAST block: 4x4, 8x8 has index_bytes 8 and
     * 8/2 = 4 blocks, so a scan of two blocks would miss this 3. */
    words[0] = 0u;
    words[1] = 0u;
    words[2] = 0u;
    words[3] = 3u;
    set_index_words(param, words, 4u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 20u);

    /* Mode bits 14-15 are not part of the palette offset: 0xC003 has mode 3
     * and offset 3, so 4*3 + 8 = 20, not 4*0x4003 + 8. */
    words[0] = 0xC003u;
    words[1] = 0u;
    words[2] = 0u;
    words[3] = 0u;
    set_index_words(param, words, 4u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 20u);

    /*
     * The index block lives at 0x30000, not 0x20000, when the parameter's
     * VRAMOffset has (param & 0xC000) == 0x8000 (design section 2.1). The
     * correct window is 20; the other base holds zeroes and would give 8.
     */
    {
        uint32_t high = param_of(5u, 0u, 0u, 0x8000u);
        static const uint16_t high_words[4] = {3u, 0u, 0u, 0u};
        static const uint16_t low_words[4] = {0u, 0u, 0u, 0u};

        mem_reset(0x41000u);
        set_words_at(0x20000u, low_words, 4u);
        set_words_at(0x30000u, high_words, 4u);
        gxcm_reset();
        gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, high);
        gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
        gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
        gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
        gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
        CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
        CHECK_EQ_INT(g_plan.record_count, 1u);
        CHECK_EQ_INT(g_plan.records[0].palette_bytes, 20u);
    }

    /*
     * Same high base with a low offset bit set: the selector is exactly
     * (param & 0xC000) == 0x8000, so the offset must not disturb it. Offset 1
     * puts the index at 0x30000 + (1 << 2) = 0x30004 and max 3 still gives 20.
     */
    {
        uint32_t high = param_of(5u, 0u, 0u, 0x8001u);
        static const uint16_t high_words[4] = {3u, 0u, 0u, 0u};

        mem_reset(0x41000u);
        set_words_at(0x30004u, high_words, 4u);
        gxcm_reset();
        gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, high);
        gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
        gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
        gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
        gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
        CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
        CHECK_EQ_INT(g_plan.record_count, 1u);
        CHECK_EQ_INT(g_plan.records[0].palette_bytes, 20u);
    }

    /* Restore the low-base param for the edge/fallback cases below. */
    mem_reset(0x21000u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    /*
     * Edge 0x3FFF: 4*16383 + 8 = 65540 (the format cap). The default PLDT cap
     * is 32768, so this only fits with the full caps.
     */
    mem_reset(0x21000u);
    words[0] = 0x3FFFu;
    words[1] = 0u;
    words[2] = 0u;
    words[3] = 0u;
    set_index_words(param, words, 4u);
    CHECK_EQ_INT(plan_now(&full), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 65540u);
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, 65540u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 0u);
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK(memcmp(g_pldt, g_mem, 65540u) == 0);

    /* The same key under the default budget is dropped whole. */
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 1);

    /*
     * Full-window fallback: the whole index block is available (dst == NULL
     * probe succeeds) but the 2-byte content scan fails. The plan keeps the
     * key with 65540 palette bytes and apply still copies the block normally.
     */
    mem_reset(0x21000u);
    words[0] = 1u;
    set_index_words(param, words, 4u);
    g_fail = FAIL_INDEX_SMALL;
    CHECK_EQ_INT(plan_now(&full), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.records[0].palette_bytes, 65540u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 0u);
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_data_reads[TWLHD_TEX_CAP_READ_INDEX], 1u);
    CHECK(memcmp(g_txdt + g_plan.records[0].index_offset,
                 g_mem + 0x20100u, 8u) == 0);

    /* A missing whole index block is a read-error drop, not a fallback. */
    g_fail = FAIL_INDEX;
    CHECK_EQ_INT(plan_now(&full), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 1u);

    test_report("tex-capture/fmt5-window");
}

/* ------------------------------------------------------------------------- */
/* 8. Budget boundaries.                                                    */
/* ------------------------------------------------------------------------- */

static void test_budget_boundaries(void) {
    TwlhdTexCapLimits limits;
    uint32_t param = param_of(3u, 0u, 0u, 0x40u);
    uint32_t i;
    uint32_t param2;

    /* TXDT exactly fits: 8x8 I4 = 32 texel bytes. */
    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    limits = limits_default();
    limits.max_txdt_bytes = 32u;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 0);

    limits.max_txdt_bytes = 31u; /* one byte over */
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 1);

    /* PLDT exactly fits: I4 palette table = 32 bytes, already 4-aligned. */
    limits = limits_default();
    limits.max_pldt_bytes = 32u;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);

    limits.max_pldt_bytes = 31u; /* one byte over */
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);

    /* Format-5 window 12: exact fit and one byte over. */
    {
        static const uint16_t words[4] = {1u, 0u, 0u, 0u};
        uint32_t p5 = param_of(5u, 0u, 0u, 0x80u);

        mem_reset(0x21000u);
        set_index_words(p5, words, 4u);
        gxcm_reset();
        gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, p5);
        gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
        gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
        gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
        gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

        limits = limits_default();
        limits.max_pldt_bytes = 12u;
        CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
        CHECK_EQ_INT(g_plan.record_count, 1u);
        CHECK_EQ_INT(g_plan.records[0].palette_bytes, 12u);

        limits.max_pldt_bytes = 11u;
        CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
        CHECK_EQ_INT(g_plan.record_count, 0u);
        CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    }

    /* The 257th distinct key is dropped by the 256-entry record cap. */
    mem_reset(0x1000u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    for (i = 1u; i <= 257u; i++) {
        gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 0u, 0u, i));
        gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    }
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    limits = limits_default();
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 257u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 256u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 1);
    CHECK_EQ_INT(g_plan.record_count, 256u);
    /* 256 * 32 = 8192 texel bytes and 256 * 32 = 8192 palette bytes. */
    CHECK_EQ_INT(g_plan.stats.bytes_txdt, 8192u);
    CHECK_EQ_INT(g_plan.stats.bytes_pldt, 8192u);
    CHECK_EQ_INT(g_plan.records[0].teximage_param, param_of(3u, 0u, 0u, 1u));
    CHECK_EQ_INT(g_plan.records[255].teximage_param,
                 param_of(3u, 0u, 0u, 256u));
    /* The full 256-record plan applies: this pins the "> CAP_KEY_MAX" guards
     * against a ">= CAP_KEY_MAX" mutation. */
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_entries[255].texel_bytes, 32u);
    CHECK_EQ_INT(g_entries[255].palette_bytes, 32u);

    /* max_records is an independent cap. */
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 0u, 0u, 1u));
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 0u, 0u, 2u));
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    limits = limits_default();
    limits.max_records = 1u;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    param2 = g_plan.records[0].teximage_param;
    CHECK_EQ_INT(param2, param_of(3u, 0u, 0u, 1u));

    test_report("tex-capture/budget");
}

/* ------------------------------------------------------------------------- */
/* 9. Drop order: a big record is dropped, a later small one is kept.       */
/* ------------------------------------------------------------------------- */

static void test_drop_order(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t big = param_of(3u, 5u, 5u, 0x1000u); /* 256x256 I4 = 32768 B */
    uint32_t small = param_of(3u, 0u, 0u, 0x0200u); /* 8x8 I4 = 32 B */

    mem_reset(0x22000u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, big);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, small);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    limits.max_txdt_bytes = 100u;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 2u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 1);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.records[0].teximage_param, small);
    CHECK_EQ_INT(g_plan.records[0].texel_bytes, 32u);

    test_report("tex-capture/drop-order");
}

/* ------------------------------------------------------------------------- */
/* 10. Read errors and static unsupported keys.                             */
/* ------------------------------------------------------------------------- */

static void test_read_errors(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param = param_of(3u, 0u, 0u, 0x40u);

    /* Texel probe failure: whole key dropped. */
    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    g_fail = FAIL_TEXEL;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 1u);
    CHECK_EQ_INT(g_plan.stats.overflow, 0);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    /* Palette probe failure: same. */
    g_fail = FAIL_PALETTE;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 1u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    /* A palette address outside the fake memory is also a read-error drop,
     * while a later key with a valid address is still kept. */
    mem_reset(0x800u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0xFFFFu); /* 0xFFFF << 4 is far away */
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 0u, 0u, 0x80u));
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    g_fail = 0;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 2u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 1u);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.records[0].pltt_base, 0u);

    /* Static unsupported: 1024x1024 16bpp = 2,097,152 texel bytes. */
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(7u, 7u, 7u, 0u));
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_kept, 0u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_unsupported, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    /* 1024x1024 I4 = 524288 texel bytes exactly: supported (the static check
     * is "> TXDT_MAX"), but the small fake memory makes the probe fail, so the
     * key is a read-error drop, not an unsupported one. */
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param_of(3u, 7u, 7u, 0u));
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    mem_reset(0x400u);
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.stats.keys_seen, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_read_error, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_unsupported, 0u);
    CHECK_EQ_INT(g_plan.record_count, 0u);

    test_report("tex-capture/read-errors");
}

/* ------------------------------------------------------------------------- */
/* 11. NULL and capacity error paths leave every output untouched.          */
/* ------------------------------------------------------------------------- */

static void test_null_capacity(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param = param_of(3u, 0u, 0u, 0x40u);
    uint8_t txdt[64];
    uint8_t pldt[64];
    TwlhdV2TexEntry entries[1];
    uint32_t i;
    int untouched;

    /* Plan: every required pointer is checked before any write. */
    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, 0u);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);

    memset(&g_plan, 0xA5, sizeof(g_plan));
    memcpy(&g_saved_plan, &g_plan, sizeof(g_plan));
    CHECK_EQ_INT(twlhd_tex_capture_plan(NULL, g_gxcm, g_gxcm_count, fake_read,
                                        NULL, &g_plan),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK(memcmp(&g_plan, &g_saved_plan, sizeof(g_plan)) == 0);
    CHECK_EQ_INT(twlhd_tex_capture_plan(&limits, NULL, g_gxcm_count, fake_read,
                                        NULL, &g_plan),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK(memcmp(&g_plan, &g_saved_plan, sizeof(g_plan)) == 0);
    CHECK_EQ_INT(twlhd_tex_capture_plan(&limits, g_gxcm, g_gxcm_count, NULL,
                                        NULL, &g_plan),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK(memcmp(&g_plan, &g_saved_plan, sizeof(g_plan)) == 0);
    CHECK_EQ_INT(twlhd_tex_capture_plan(&limits, g_gxcm, g_gxcm_count,
                                        fake_read, NULL, NULL),
                 TWLHD_TEX_CAP_ERR_NULL);

    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);

    memset(txdt, 0xA5, sizeof(txdt));
    memset(pldt, 0xA5, sizeof(pldt));
    memset(entries, 0xA5, sizeof(entries));

    /* A source that fails at apply time is caught by the probe pass before the
     * first write; record 0's probe must run too. apply_now() uses the global
     * buffers, so sentinel-fill those. */
    memset(g_txdt, 0xA5, sizeof(g_txdt));
    memset(g_pldt, 0xA5, sizeof(g_pldt));
    g_fail = FAIL_PALETTE;
    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_ERR_READ);
    g_fail = 0;
    untouched = 1;
    for (i = 0; i < sizeof(g_txdt); i++) {
        if (g_txdt[i] != 0xA5u) untouched = 0;
    }
    for (i = 0; i < sizeof(g_pldt); i++) {
        if (g_pldt[i] != 0xA5u) untouched = 0;
    }
    CHECK(untouched);

    CHECK_EQ_INT(twlhd_tex_capture_apply(NULL, fake_read, NULL, txdt, 64u, pldt,
                                         64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, NULL, NULL, txdt, 64u, pldt,
                                         64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 31u,
                                         pldt, 64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         pldt, 31u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         pldt, 64u, entries, 0u),
                 TWLHD_TEX_CAP_ERR_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, NULL, 64u,
                                         pldt, 64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         NULL, 64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_NULL);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         pldt, 64u, NULL, 1u),
                 TWLHD_TEX_CAP_ERR_NULL);

    untouched = 1;
    for (i = 0; i < sizeof(txdt); i++) {
        if (txdt[i] != 0xA5u) untouched = 0;
    }
    for (i = 0; i < sizeof(pldt); i++) {
        if (pldt[i] != 0xA5u) untouched = 0;
    }
    for (i = 0; i < sizeof(entries); i++) {
        if (((const uint8_t *)entries)[i] != 0xA5u) untouched = 0;
    }
    CHECK(untouched);

    /* A plan whose recorded offsets do not match its sizes is refused. */
    g_plan.records[0].texel_offset = 1u;
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         pldt, 64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_PLAN);
    g_plan.records[0].texel_offset = 0u;
    untouched = 1;
    for (i = 0; i < sizeof(txdt); i++) {
        if (txdt[i] != 0xA5u) untouched = 0;
    }
    CHECK(untouched);

    /* Only the recorded totals are inconsistent: the two totals check is an
     * OR, so one bad total is enough to refuse the plan. */
    g_plan.stats.bytes_txdt = 31u;
    memset(txdt, 0xA5, sizeof(txdt));
    memset(pldt, 0xA5, sizeof(pldt));
    memset(entries, 0xA5, sizeof(entries));
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, txdt, 64u,
                                         pldt, 64u, entries, 1u),
                 TWLHD_TEX_CAP_ERR_PLAN);
    untouched = 1;
    for (i = 0; i < sizeof(txdt); i++) {
        if (txdt[i] != 0xA5u) untouched = 0;
    }
    for (i = 0; i < sizeof(pldt); i++) {
        if (pldt[i] != 0xA5u) untouched = 0;
    }
    CHECK(untouched);
    g_plan.stats.bytes_txdt = 32u;

    /* A zero-record plan accepts NULL buffers with zero capacities. */
    gxcm_reset();
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 0u);
    CHECK_EQ_INT(twlhd_tex_capture_apply(&g_plan, fake_read, NULL, NULL, 0u,
                                         NULL, 0u, NULL, 0u),
                 TWLHD_TEX_CAP_OK);

    /* layout's own guards. */
    CHECK_EQ_INT(twlhd_tex_capture_layout(NULL), TWLHD_TEX_CAP_ERR_NULL);
    g_plan.record_count = TWLHD_TEX_CAP_KEY_MAX + 1u;
    CHECK_EQ_INT(twlhd_tex_capture_layout(&g_plan),
                 TWLHD_TEX_CAP_ERR_CAPACITY);
    g_plan.record_count = 0u;

    /* NULL limit helpers are no-ops. */
    twlhd_tex_capture_default_limits(NULL);
    twlhd_tex_capture_full_limits(NULL);

    test_report("tex-capture/null-capacity");
}

/* ------------------------------------------------------------------------- */
/* 12. OOB guard harness: 128 bytes before AND after every output, input    */
/*     tail 0x00/0xFF two-pass memcmp.                                      */
/* ------------------------------------------------------------------------- */

#define OOB_GUARD 128u
#define OOB_FILL 0x5Bu
#define OOB_MEM_SIZE 0x22000u
#define OOB_TXDT_CAP 128u
#define OOB_PLDT_CAP 128u
#define OOB_ENTRY_CAP 4u
#define OOB_GXCM_CAP 16u

static _Alignas(8) uint8_t
    g_oob_plan[OOB_GUARD + sizeof(TwlhdTexCapPlan) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_txdt[OOB_GUARD + OOB_TXDT_CAP + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_pldt[OOB_GUARD + OOB_PLDT_CAP + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_entries[OOB_GUARD + OOB_ENTRY_CAP * sizeof(TwlhdV2TexEntry) +
                  OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_gxcm[OOB_GUARD + OOB_GXCM_CAP * sizeof(TwlhdV2CmdEntry) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_mem[OOB_GUARD + OOB_MEM_SIZE + OOB_GUARD];

#define OOB_PLAN ((TwlhdTexCapPlan *)(g_oob_plan + OOB_GUARD))
#define OOB_TXDT (g_oob_txdt + OOB_GUARD)
#define OOB_PLDT (g_oob_pldt + OOB_GUARD)
#define OOB_ENTRIES ((TwlhdV2TexEntry *)(g_oob_entries + OOB_GUARD))
#define OOB_GXCM ((TwlhdV2CmdEntry *)(g_oob_gxcm + OOB_GUARD))

static int guard_intact(const uint8_t *base, size_t used, int fill) {
    size_t i;

    for (i = 0; i < OOB_GUARD; i++) {
        if (base[i] != (uint8_t)fill) return 0;
        if (base[OOB_GUARD + used + i] != (uint8_t)fill) return 0;
    }
    return 1;
}

static void test_out_of_bounds(void) {
    static const TwlhdV2CmdEntry payload[] = {
        {TWLHD_V2_CMD_TEXIMAGE_PARAM, 0}, /* filled below */
        {TWLHD_V2_CMD_PLTT_BASE, 0x10u},
        {TWLHD_V2_CMD_BEGIN_VTXS, 1u},
        {TWLHD_V2_CMD_VTX_10, 0u},
        {TWLHD_V2_CMD_TEXIMAGE_PARAM, 0},
        {TWLHD_V2_CMD_VTX_10, 0u},
        {TWLHD_V2_CMD_END_VTXS, 0u},
    };
    const uint32_t payload_count =
        (uint32_t)(sizeof(payload) / sizeof(payload[0]));
    TwlhdV2CmdEntry scene[sizeof(payload) / sizeof(payload[0])];
    uint8_t plan_copy[2][sizeof(g_oob_plan)];
    uint8_t txdt_copy[2][sizeof(g_oob_txdt)];
    uint8_t pldt_copy[2][sizeof(g_oob_pldt)];
    uint8_t entries_copy[2][sizeof(g_oob_entries)];
    TwlhdTexCapStats stats_copy[2];
    uint32_t pass;
    uint32_t i;
    static const uint16_t words[4] = {1u, 0u, 0u, 0u};

    memcpy(scene, payload, sizeof(payload));
    scene[0].value = param_of(3u, 0u, 0u, 0x0400u); /* 8x8 I4 */
    scene[4].value = param_of(5u, 0u, 0u, 0x0500u); /* 8x8 4x4 */

    mem_reset(OOB_MEM_SIZE);
    set_index_words(scene[4].value, words, 4u);

    for (pass = 0; pass < 2u; pass++) {
        int fill = pass == 0 ? 0x00 : 0xFF;
        TwlhdTexCapLimits limits = limits_default();

        memset(g_oob_gxcm, fill, sizeof(g_oob_gxcm));
        memcpy(OOB_GXCM, scene, sizeof(scene));
        memset(g_oob_mem, fill, sizeof(g_oob_mem));
        memcpy(g_oob_mem + OOB_GUARD, g_mem, OOB_MEM_SIZE);
        g_mem_ptr = g_oob_mem + OOB_GUARD;
        g_mem_size = OOB_MEM_SIZE;
        g_fail = 0;

        memset(g_oob_plan, OOB_FILL, sizeof(g_oob_plan));
        memset(g_oob_txdt, OOB_FILL, sizeof(g_oob_txdt));
        memset(g_oob_pldt, OOB_FILL, sizeof(g_oob_pldt));
        memset(g_oob_entries, OOB_FILL, sizeof(g_oob_entries));

        CHECK_EQ_INT(twlhd_tex_capture_plan(&limits, OOB_GXCM, payload_count,
                                            fake_read, NULL, OOB_PLAN),
                     TWLHD_TEX_CAP_OK);
        CHECK_EQ_INT(OOB_PLAN->record_count, 2u);
        CHECK_EQ_INT(twlhd_tex_capture_apply(
                         OOB_PLAN, fake_read, NULL, OOB_TXDT, OOB_TXDT_CAP,
                         OOB_PLDT, OOB_PLDT_CAP, OOB_ENTRIES, OOB_ENTRY_CAP),
                     TWLHD_TEX_CAP_OK);

        /* 32 texel + 16 texel + 8 index = 56 TXDT bytes; 32 + 12 = 44 PLDT. */
        CHECK_EQ_INT(OOB_PLAN->stats.bytes_txdt, 56u);
        CHECK_EQ_INT(OOB_PLAN->stats.bytes_pldt, 44u);

        CHECK(guard_intact(g_oob_plan, sizeof(TwlhdTexCapPlan), OOB_FILL));
        CHECK(guard_intact(g_oob_txdt, OOB_PLAN->stats.bytes_txdt, OOB_FILL));
        CHECK(guard_intact(g_oob_pldt, OOB_PLAN->stats.bytes_pldt, OOB_FILL));
        CHECK(guard_intact(g_oob_entries,
                           OOB_PLAN->record_count * sizeof(TwlhdV2TexEntry),
                           OOB_FILL));
        CHECK(guard_intact(g_oob_gxcm, sizeof(scene), fill));
        CHECK(guard_intact(g_oob_mem, OOB_MEM_SIZE, fill));

        memcpy(plan_copy[pass], g_oob_plan, sizeof(g_oob_plan));
        memcpy(txdt_copy[pass], g_oob_txdt, sizeof(g_oob_txdt));
        memcpy(pldt_copy[pass], g_oob_pldt, sizeof(g_oob_pldt));
        memcpy(entries_copy[pass], g_oob_entries, sizeof(g_oob_entries));
        stats_copy[pass] = OOB_PLAN->stats;
    }

    /* An over-read of either input tail (0x00 vs 0xFF) changes an output. */
    CHECK(memcmp(plan_copy[0], plan_copy[1], sizeof(g_oob_plan)) == 0);
    CHECK(memcmp(txdt_copy[0], txdt_copy[1], sizeof(g_oob_txdt)) == 0);
    CHECK(memcmp(pldt_copy[0], pldt_copy[1], sizeof(g_oob_pldt)) == 0);
    CHECK(memcmp(entries_copy[0], entries_copy[1], sizeof(g_oob_entries)) == 0);
    CHECK(memcmp(&stats_copy[0], &stats_copy[1], sizeof(stats_copy[0])) == 0);

    /* Restore the normal fake memory. */
    g_mem_ptr = g_mem;
    g_mem_size = OOB_MEM_SIZE;
    for (i = 0; i < OOB_MEM_SIZE && i < MEM_MAX; i++) {
        g_mem[i] = (uint8_t)(i * 37u + 11u);
    }

    test_report("tex-capture/oob");
}

/* ------------------------------------------------------------------------- */
/* 13. Round-trip: plan -> apply -> writer -> parser -> pica_frame.          */
/* ------------------------------------------------------------------------- */

static uint8_t g_file[TWLHD_V2_FILE_MAX];
static TwlhdGxTexVertex g_tex[6];
static uint8_t g_arena[256];
static uint8_t g_scratch[256];
static TwlhdPicaBindPlan g_bind[2];
static TwlhdPicaFrameTexture g_textures[2];
static TwlhdPicaTexCoord g_texcoords[6];
static int32_t g_entry_texture[2];
static TwlhdPicaFrameEntryStatus g_entry_status[2];

static void test_round_trip(void) {
    TwlhdTexCapLimits limits = limits_default();
    uint32_t param1 = param_of(3u, 0u, 0u, 0x10u);
    uint32_t param2 = param_of(3u, 0u, 0u, 0x40u);
    uint32_t pltt = 0x20u;
    TwlhdV2WriteParams params;
    TwlhdV2View view;
    TwlhdPicaFrameResult result;
    size_t file_size = 0;
    uint32_t arena_needed = 0;
    uint32_t entry_count = 0;
    uint32_t texture_count = 0;
    uint32_t arena_used = 0;
    uint32_t i;

    mem_reset(0x400u);
    gxcm_reset();
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param1);
    gxcm_add(TWLHD_V2_CMD_PLTT_BASE, pltt);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    gxcm_add(TWLHD_V2_CMD_TEXIMAGE_PARAM, param2);
    gxcm_add(TWLHD_V2_CMD_BEGIN_VTXS, 1u);
    gxcm_add(TWLHD_V2_CMD_VTX_10, 0u);
    gxcm_add(TWLHD_V2_CMD_END_VTXS, 0u);
    gxcm_add(TWLHD_V2_CMD_SWAP_BUFFERS, 0u);

    /* TXDT 32 and PLDT 32 hold exactly the first 8x8 I4 key; the second is
     * dropped whole, so the consumer reports NO_DATA for it. */
    limits.max_txdt_bytes = 32u;
    limits.max_pldt_bytes = 32u;
    CHECK_EQ_INT(plan_now(&limits), TWLHD_TEX_CAP_OK);
    CHECK_EQ_INT(g_plan.record_count, 1u);
    CHECK_EQ_INT(g_plan.stats.keys_dropped_budget, 1u);
    CHECK_EQ_INT(g_plan.records[0].teximage_param, param1);

    CHECK_EQ_INT(apply_now(), TWLHD_TEX_CAP_OK);

    memset(&params, 0, sizeof(params));
    params.frame_index = 1u;
    params.flags = TWLHD_V2_FLAG_FRAME_COMPLETE;
    params.backend_id = TWLHD_V2_BACKEND_SYNTHETIC;
    params.coverage_flags = 0u;
    params.gxcm = g_gxcm;
    params.gxcm_count = g_gxcm_count;
    params.rsta = NULL;
    params.rsta_count = 0u;
    params.tex = g_entries;
    params.tex_count = g_plan.record_count;
    CHECK_EQ_INT(twlhd_v2_write(&params, g_file, sizeof(g_file), &file_size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_file, file_size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.txds_count, 1u);
    CHECK_EQ_INT(view.txdt_size, 32u);
    CHECK_EQ_INT(view.pldt_size, 32u);
    /* The file's first record bytes equal the fake source memory. */
    CHECK(memcmp(view.txdt, g_mem + ((param1 & 0xFFFFu) << 3), 32u) == 0);
    CHECK(memcmp(view.pldt, g_mem + (pltt << 4), 32u) == 0);

    /* Two triangles: one kept key, one dropped key. */
    for (i = 0; i < 6u; i++) {
        g_tex[i].s = 0.0f;
        g_tex[i].t = 0.0f;
        g_tex[i].teximage_param = i < 3u ? param1 : param2;
        g_tex[i].pltt_base = pltt;
    }

    CHECK_EQ_INT(twlhd_pica_frame_arena_needed(g_tex, 6u, &view,
                                               &arena_needed),
                 TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(arena_needed, 256u); /* 8x8 * 4 B */

    result = twlhd_pica_frame_prepare(
        g_tex, 6u, &view, g_bind, 2u, g_textures, 2u, g_arena,
        (uint32_t)sizeof(g_arena), g_texcoords, 6u, g_entry_texture,
        g_entry_status, 2u, g_scratch, (uint32_t)sizeof(g_scratch),
        &entry_count, &texture_count, &arena_used);
    CHECK_EQ_INT(result, TWLHD_PICA_FRAME_OK);
    CHECK_EQ_INT(entry_count, 2u);
    CHECK_EQ_INT(texture_count, 1u);
    CHECK_EQ_INT(arena_used, 256u);
    CHECK_EQ_INT(g_entry_status[0], TWLHD_PICA_FRAME_ENTRY_OK);
    CHECK_EQ_INT(g_entry_status[1], TWLHD_PICA_FRAME_ENTRY_NO_DATA);
    CHECK(g_entry_status[0] != TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED);
    CHECK(g_entry_texture[0] >= 0);
    CHECK_EQ_INT(g_entry_texture[1], -1);
    CHECK_EQ_INT(g_textures[0].width, 8u);
    CHECK_EQ_INT(g_textures[0].height, 8u);

    test_report("tex-capture/round-trip");
}

int main(void) {
    test_single_draw();
    test_same_key_dedupe();
    test_same_key_conflict();
    test_unused_key();
    test_format_zero();
    test_default_registers();
    test_format_sizes();
    test_fmt5_windows();
    test_budget_boundaries();
    test_drop_order();
    test_read_errors();
    test_null_capacity();
    test_out_of_bounds();
    test_round_trip();
    return test_exit_code();
}
