// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_tex_draw_plan.c (stage 5B-2e-2).
 *
 * Pure C11, no GPU. Hand-computed expectations; every fallback path and a
 * guard-byte harness are exercised.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_tex_draw_plan.h"

#define MAX_ENTRIES 8u
#define MAX_VERTICES 12u

static TwlhdPicaBindPlan g_plan[MAX_ENTRIES];
static TwlhdPicaFrameTexture g_textures[MAX_ENTRIES];
static int32_t g_entry_texture[MAX_ENTRIES];
static TwlhdPicaFrameEntryStatus g_entry_status[MAX_ENTRIES];
static TwlhdTexDrawEntry g_out[MAX_ENTRIES];
static TwlhdTexDrawPlanResult g_result;

static _Alignas(128) uint8_t g_arena[1024];

static void reset_all(void) {
    memset(g_plan, 0, sizeof(g_plan));
    memset(g_textures, 0, sizeof(g_textures));
    memset(g_entry_texture, 0, sizeof(g_entry_texture));
    memset(g_entry_status, 0, sizeof(g_entry_status));
    memset(g_out, 0x5B, sizeof(g_out));
    memset(&g_result, 0x5B, sizeof(g_result));
}

static void test_basic_mapping(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;

    g_plan[1].first_vertex = 3u;
    g_plan[1].vertex_count = 3u;
    g_plan[1].untextured = 1;

    g_plan[2].first_vertex = 6u;
    g_plan[2].vertex_count = 3u;
    g_plan[2].untextured = 0;

    g_textures[0].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;
    g_textures[1].wrap_s = TWLHD_PICA_WRAP_MIRRORED_REPEAT;
    g_textures[1].wrap_t = TWLHD_PICA_WRAP_REPEAT;
    g_textures[1].width = 8u;
    g_textures[1].height = 8u;

    g_entry_texture[0] = 0;
    g_entry_texture[1] = -1;
    g_entry_texture[2] = 1;

    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;
    g_entry_status[1] = TWLHD_PICA_FRAME_ENTRY_OK;
    g_entry_status[2] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 3u, g_entry_texture, g_entry_status, g_textures,
                     2u, g_arena, 256u, 2u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 9u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);
    CHECK_EQ_INT(g_result.entry_count, 3u);
    CHECK_EQ_INT(g_result.texture_count, 2u);
    CHECK_EQ_INT(g_result.all_untextured, 0);

    CHECK_EQ_INT(g_out[0].first_vertex, 0u);
    CHECK_EQ_INT(g_out[0].vertex_count, 3u);
    CHECK_EQ_INT(g_out[0].texture_slot, 0);
    CHECK_EQ_INT(g_out[0].wrap_s, TWLHD_TEX_DRAW_WRAP_REPEAT);
    CHECK_EQ_INT(g_out[0].wrap_t, TWLHD_TEX_DRAW_WRAP_CLAMP_TO_EDGE);
    CHECK_EQ_INT(g_out[0].filter, TWLHD_TEX_DRAW_FILTER_NEAREST);
    CHECK_EQ_INT(g_out[0].untextured, 0);

    CHECK_EQ_INT(g_out[1].first_vertex, 3u);
    CHECK_EQ_INT(g_out[1].vertex_count, 3u);
    CHECK_EQ_INT(g_out[1].texture_slot, -1);
    CHECK_EQ_INT(g_out[1].untextured, 1);

    CHECK_EQ_INT(g_out[2].first_vertex, 6u);
    CHECK_EQ_INT(g_out[2].vertex_count, 3u);
    CHECK_EQ_INT(g_out[2].texture_slot, 1);
    CHECK_EQ_INT(g_out[2].wrap_s, TWLHD_TEX_DRAW_WRAP_MIRRORED_REPEAT);
    CHECK_EQ_INT(g_out[2].wrap_t, TWLHD_TEX_DRAW_WRAP_REPEAT);
    CHECK_EQ_INT(g_out[2].untextured, 0);

    test_report("tex-draw-plan/basic-mapping");
}

static void test_no_texture(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 1;
    g_entry_texture[0] = -1;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     0u, g_arena, 0u, 2u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_NO_TEXTURE);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_out[0].texture_slot, -1);
    CHECK_EQ_INT(g_out[0].untextured, 1);

    test_report("tex-draw-plan/no-texture");
}

static void test_failed_entries_untextured(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_entry_texture[0] = -1;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_NO_DATA;

    g_plan[1].first_vertex = 3u;
    g_plan[1].vertex_count = 3u;
    g_plan[1].untextured = 0;
    g_entry_texture[1] = 0;
    g_entry_status[1] = TWLHD_PICA_FRAME_ENTRY_UNSUPPORTED;

    g_textures[0].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_REPEAT;
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 6u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_NO_TEXTURE);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_out[0].texture_slot, -1);
    CHECK_EQ_INT(g_out[0].untextured, 1);
    CHECK_EQ_INT(g_out[1].texture_slot, -1);
    CHECK_EQ_INT(g_out[1].untextured, 1);

    test_report("tex-draw-plan/failed-entries-untextured");
}

static void test_budget_fallback(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_entry_texture[0] = 0;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;
    g_textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;

    /* arena_used exceeds max_arena_bytes. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 513u, 1u, 512u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    /* texture_count exceeds max_textures. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     2u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    /* vertex_count exceeds max_texcoords. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     2u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    /* entry_count exceeds out_capacity. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, 1u, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    test_report("tex-draw-plan/budget-fallback");
}

static void test_alignment_fallback(void) {
    static uint8_t misaligned_arena[256];
    uint8_t *misaligned = misaligned_arena + 1;

    reset_all();
    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_entry_texture[0] = 0;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;
    g_textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;

    CHECK(((uintptr_t)misaligned & 127u) != 0u);

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, misaligned, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);

    test_report("tex-draw-plan/alignment-fallback");
}

static void test_null_inputs(void) {
    reset_all();

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     NULL, 0u, g_entry_texture, g_entry_status, g_textures,
                     0u, g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 0u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, NULL, g_entry_status, g_textures, 0u,
                     g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, NULL, g_textures, 0u,
                     g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, NULL, 0u,
                     g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     0u, g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, NULL, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     0u, g_arena, 0u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, NULL),
                 TWLHD_TEX_DRAW_FALLBACK_CAPACITY);

    test_report("tex-draw-plan/null-inputs");
}

#define OOB_GUARD 64u
#define OOB_FILL 0x5B

static _Alignas(8) uint8_t
    g_oob_plan[OOB_GUARD + MAX_ENTRIES * sizeof(TwlhdPicaBindPlan) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_textures[OOB_GUARD + MAX_ENTRIES * sizeof(TwlhdPicaFrameTexture) +
                   OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_et[OOB_GUARD + MAX_ENTRIES * sizeof(int32_t) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_es[OOB_GUARD +
             MAX_ENTRIES * sizeof(TwlhdPicaFrameEntryStatus) + OOB_GUARD];
static _Alignas(8) uint8_t
    g_oob_out[OOB_GUARD + MAX_ENTRIES * sizeof(TwlhdTexDrawEntry) + OOB_GUARD];

static int guard_intact(const uint8_t *base, size_t used, int fill) {
    size_t i;
    for (i = 0; i < OOB_GUARD; i++) {
        if (base[i] != (uint8_t)fill) return 0;
        if (base[OOB_GUARD + used + i] != (uint8_t)fill) return 0;
    }
    return 1;
}

static void test_out_of_bounds(void) {
    TwlhdPicaBindPlan *plan = (TwlhdPicaBindPlan *)(g_oob_plan + OOB_GUARD);
    TwlhdPicaFrameTexture *textures =
        (TwlhdPicaFrameTexture *)(g_oob_textures + OOB_GUARD);
    int32_t *et = (int32_t *)(g_oob_et + OOB_GUARD);
    TwlhdPicaFrameEntryStatus *es =
        (TwlhdPicaFrameEntryStatus *)(g_oob_es + OOB_GUARD);
    TwlhdTexDrawEntry *out = (TwlhdTexDrawEntry *)(g_oob_out + OOB_GUARD);
    TwlhdTexDrawPlanResult result;

    memset(g_oob_plan, OOB_FILL, sizeof(g_oob_plan));
    memset(g_oob_textures, OOB_FILL, sizeof(g_oob_textures));
    memset(g_oob_et, OOB_FILL, sizeof(g_oob_et));
    memset(g_oob_es, OOB_FILL, sizeof(g_oob_es));
    memset(g_oob_out, OOB_FILL, sizeof(g_oob_out));

    plan[0].first_vertex = 0u;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    plan[1].first_vertex = 3u;
    plan[1].vertex_count = 3u;
    plan[1].untextured = 1;

    textures[0].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    textures[0].wrap_t = TWLHD_PICA_WRAP_MIRRORED_REPEAT;
    textures[0].width = 8u;
    textures[0].height = 8u;

    et[0] = 0;
    et[1] = -1;
    es[0] = TWLHD_PICA_FRAME_ENTRY_OK;
    es[1] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 2u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, MAX_VERTICES, 6u, out, 2u,
                     &result),
                 TWLHD_TEX_DRAW_OK);

    CHECK(guard_intact(g_oob_plan,
                       2u * sizeof(TwlhdPicaBindPlan), OOB_FILL));
    CHECK(guard_intact(g_oob_textures,
                       1u * sizeof(TwlhdPicaFrameTexture), OOB_FILL));
    CHECK(guard_intact(g_oob_et, 2u * sizeof(int32_t), OOB_FILL));
    CHECK(guard_intact(g_oob_es,
                       2u * sizeof(TwlhdPicaFrameEntryStatus), OOB_FILL));
    CHECK(guard_intact(g_oob_out,
                       2u * sizeof(TwlhdTexDrawEntry), OOB_FILL));

    test_report("tex-draw-plan/oob");
}

static void test_strings(void) {
    CHECK(strcmp(twlhd_tex_draw_verdict_string(TWLHD_TEX_DRAW_OK), "OK") == 0);
    CHECK(strcmp(twlhd_tex_draw_verdict_string(TWLHD_TEX_DRAW_NO_TEXTURE),
                 "NO_TEXTURE") == 0);
    CHECK(strcmp(twlhd_tex_draw_verdict_string(TWLHD_TEX_DRAW_FALLBACK_BUDGET),
                 "FALLBACK_BUDGET") == 0);
    CHECK(strcmp(twlhd_tex_draw_verdict_string(
                     TWLHD_TEX_DRAW_FALLBACK_CAPACITY),
                 "FALLBACK_CAPACITY") == 0);
    CHECK(strcmp(twlhd_tex_draw_verdict_string(TWLHD_TEX_DRAW_FALLBACK_ALIGN),
                 "FALLBACK_ALIGN") == 0);
    test_report("tex-draw-plan/strings");
}

int main(void) {
    test_basic_mapping();
    test_no_texture();
    test_failed_entries_untextured();
    test_budget_fallback();
    test_alignment_fallback();
    test_null_inputs();
    test_out_of_bounds();
    test_strings();
    return test_exit_code();
}
