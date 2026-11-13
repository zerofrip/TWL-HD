// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_tex_draw_plan.c (stage 5B-2e-2).
 *
 * Pure C11, no GPU. Hand-computed expectations; every fallback path, a
 * guard-byte harness, and the lifecycle state machine are exercised.
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
    CHECK_EQ_INT(g_result.entry_count, 1);
    CHECK_EQ_INT(g_result.texture_count, 0);
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
    CHECK_EQ_INT(g_result.entry_count, 2);
    CHECK_EQ_INT(g_result.texture_count, 1);
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
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    /* texture_count exceeds max_textures. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     2u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    /* vertex_count exceeds max_texcoords. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     2u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    /* entry_count exceeds out_capacity. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     MAX_VERTICES, 3u, g_out, 1u, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

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
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    test_report("tex-draw-plan/alignment-fallback");
}

/* D(5b): arena_used == 1 with a NULL arena pointer is still rejected by the
 * alignment guard (kills the `0 -> 1` mutant on the arena_used threshold). */
static void test_arena_used_one_null(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 1;
    g_entry_texture[0] = -1;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     0u, NULL, 1u, 0u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    test_report("tex-draw-plan/arena-used-one-null");
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
    textures[0].offset = 0u;

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
    CHECK(strcmp(twlhd_tex_draw_verdict_string(TWLHD_TEX_DRAW_FALLBACK_RANGE),
                 "FALLBACK_RANGE") == 0);
    test_report("tex-draw-plan/strings");
}

/* D(1): entry_texture[i] == texture_count must be treated as untextured and
 * must not read textures[texture_count].  The line-78 mutant
 * `slot >= texture_count` -> `slot > texture_count` would accept the equality
 * case and read out of the texture array; the guard-byte harness detects that
 * the mutant is not equivalent. */
static void test_slot_equals_count(void) {
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

    textures[0].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    textures[0].width = 8u;
    textures[0].height = 8u;
    textures[0].offset = 0u;

    et[0] = 1;   /* equal to texture_count */
    es[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_NO_TEXTURE);
    CHECK_EQ_INT(result.all_untextured, 1);
    CHECK_EQ_INT(result.entry_count, 1);
    CHECK_EQ_INT(result.texture_count, 1);
    CHECK_EQ_INT(out[0].texture_slot, -1);
    CHECK_EQ_INT(out[0].untextured, 1);

    CHECK(guard_intact(g_oob_textures,
                       1u * sizeof(TwlhdPicaFrameTexture), OOB_FILL));

    test_report("tex-draw-plan/slot-equals-count");
}

/* D(2): power-of-two guard and per-texture byte cap boundaries. */
static void test_texture_size_guard(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_entry_texture[0] = 0;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    /* 1024x1024 accepted at the exact byte cap. */
    g_textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 1024u;
    g_textures[0].height = 1024u;
    g_textures[0].offset = 0u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 1024u * 1024u * 4u, 1u, 1024u * 1024u * 4u,
                     1024u * 1024u * 4u, 3u, 3u, g_out, MAX_ENTRIES,
                     &g_result),
                 TWLHD_TEX_DRAW_OK);

    /* 2048 rejected for exceeding the 1024 maximum. */
    g_textures[0].width = 2048u;
    g_textures[0].height = 1024u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 1024u * 1024u * 4u, 1u, 1024u * 1024u * 4u,
                     1024u * 1024u * 4u, 3u, 3u, g_out, MAX_ENTRIES,
                     &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    /* 4 rejected for being below the 8 minimum. */
    g_textures[0].width = 4u;
    g_textures[0].height = 4u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 1024u * 1024u * 4u, 1u, 1024u * 1024u * 4u,
                     1024u * 1024u * 4u, 3u, 3u, g_out, MAX_ENTRIES,
                     &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    /* 24 rejected for not being a power of two. */
    g_textures[0].width = 24u;
    g_textures[0].height = 24u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 1024u * 1024u * 4u, 1u, 1024u * 1024u * 4u,
                     1024u * 1024u * 4u, 3u, 3u, g_out, MAX_ENTRIES,
                     &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);

    /* Exactly the per-texture byte cap (8x8x4 = 256) accepted. */
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 256u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);

    /* One byte over the cap rejected. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 255u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    test_report("tex-draw-plan/texture-size-guard");
}

/* D(3): budget boundaries: equality -> OK, equality+1 -> FALLBACK_BUDGET. */
static void test_budget_boundaries(void) {
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

    /* arena_used == max_arena_bytes -> OK. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 512u, 1u, 512u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);
    CHECK_EQ_INT(g_result.all_untextured, 0);

    /* arena_used == max_arena_bytes + 1 -> FALLBACK_BUDGET. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 513u, 1u, 512u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    /* texture_count == max_textures -> OK. */
    g_entry_texture[0] = 0;
    g_textures[1] = g_textures[0];
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);

    /* texture_count == max_textures + 1 -> FALLBACK_BUDGET. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     2u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    /* vertex_count == max_texcoords -> OK. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);

    /* vertex_count == max_texcoords + 1 -> FALLBACK_BUDGET. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 4u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    /* entry_count == out_capacity -> OK. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, 1u, &g_result),
                 TWLHD_TEX_DRAW_OK);

    /* entry_count == out_capacity + 1 -> FALLBACK_BUDGET. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, 1u, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    test_report("tex-draw-plan/budget-boundaries");
}

/* D(4): all_untextured flag in a mixed frame and after fallback returns. */
static void test_all_untextured_flag(void) {
    reset_all();

    /* Mixed frame: one textured, one forced untextured. */
    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_plan[1].first_vertex = 3u;
    g_plan[1].vertex_count = 3u;
    g_plan[1].untextured = 0;

    g_textures[0].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 8u;
    g_textures[0].height = 8u;

    g_entry_texture[0] = 0;
    g_entry_texture[1] = -1;
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;
    g_entry_status[1] = TWLHD_PICA_FRAME_ENTRY_OK;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     6u, 6u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_OK);
    CHECK_EQ_INT(g_result.all_untextured, 0);

    /* All-untextured frame. */
    g_entry_texture[0] = -1;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 2u, g_entry_texture, g_entry_status, g_textures,
                     1u, g_arena, 256u, 1u, 1024u, 1024u * 1024u * 4u,
                     6u, 6u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_NO_TEXTURE);
    CHECK_EQ_INT(g_result.all_untextured, 1);

    test_report("tex-draw-plan/all-untextured-flag");
}

/* D(5): line 60 survivor (arena_used == 1 with misaligned arena). */
static void test_misaligned_single_byte(void) {
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
                     1u, misaligned, 1u, 1u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    test_report("tex-draw-plan/misaligned-single-byte");
}

/* D(5): line 66 survivor (invalid texture 0, valid texture 1 referenced). */
static void test_skip_first_texture(void) {
    reset_all();

    g_plan[0].first_vertex = 0u;
    g_plan[0].vertex_count = 3u;
    g_plan[0].untextured = 0;
    g_entry_texture[0] = 1;  /* references the second texture */
    g_entry_status[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    g_textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    g_textures[0].width = 4u;  /* invalid: below minimum */
    g_textures[0].height = 8u;

    g_textures[1].wrap_s = TWLHD_PICA_WRAP_REPEAT;
    g_textures[1].wrap_t = TWLHD_PICA_WRAP_REPEAT;
    g_textures[1].width = 8u;
    g_textures[1].height = 8u;

    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     g_plan, 1u, g_entry_texture, g_entry_status, g_textures,
                     2u, g_arena, 256u, 2u, 1024u, 1024u * 1024u * 4u,
                     3u, 3u, g_out, MAX_ENTRIES, &g_result),
                 TWLHD_TEX_DRAW_FALLBACK_BUDGET);
    CHECK_EQ_INT(g_result.all_untextured, 1);
    CHECK_EQ_INT(g_result.entry_count, 0);
    CHECK_EQ_INT(g_result.texture_count, 0);

    test_report("tex-draw-plan/skip-first-texture");
}

/* Lifecycle helper tests (A): stage->draw->stage->draw, stage-then-fail,
 * shutdown, clear-then-commit, and flag+swap no double-free. */
static void test_lifecycle_stage_draw_twice(void) {
    TwlhdTexLifecycleState s;

    twlhd_tex_lifecycle_init(&s);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.pending_ready, 1);
    CHECK_EQ_INT(s.active_ready, 0);
    CHECK_EQ_INT(s.textured_mode, 1);

    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.pending_ready, 0);
    CHECK_EQ_INT(s.active_ready, 1);
    CHECK_EQ_INT(s.textured_mode, 1);

    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.pending_ready, 1);

    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s),
                 TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW);
    CHECK_EQ_INT(s.active_ready, 1);
    CHECK_EQ_INT(s.pending_ready, 0);

    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.active_ready, 1);

    test_report("tex-draw-plan/lifecycle-stage-draw-twice");
}

static void test_lifecycle_stage_then_fail(void) {
    TwlhdTexLifecycleState s;

    twlhd_tex_lifecycle_init(&s);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_clear(&s),
                 TWLHD_TEX_LIFECYCLE_FREE_PENDING_NOW);
    CHECK_EQ_INT(s.pending_ready, 0);
    CHECK_EQ_INT(s.active_ready, 0);
    CHECK_EQ_INT(s.drop_active_on_commit, 0);

    test_report("tex-draw-plan/lifecycle-stage-then-fail");
}

static void test_lifecycle_clear_then_commit(void) {
    TwlhdTexLifecycleState s;
    TwlhdTexLifecycleAction action;
    int free_active_count = 0;

    twlhd_tex_lifecycle_init(&s);

    /* Renderer sequence for finding 5: textured frame is staged, drawn
     * (commit), then a v1/debug frame load calls clear(), then the next
     * debug draw calls commit() again. The active texture set must be freed
     * exactly once. */
    action = twlhd_tex_lifecycle_stage(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.pending_ready, 1);
    CHECK_EQ_INT(s.active_ready, 0);

    action = twlhd_tex_lifecycle_commit(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.active_ready, 1);
    CHECK_EQ_INT(s.pending_ready, 0);

    /* Load an untextured frame while the textured set is active. */
    action = twlhd_tex_lifecycle_clear(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.textured_mode, 0);
    CHECK_EQ_INT(s.drop_active_on_commit, 1);

    /* Draw the debug frame: commit frees the old active exactly once. */
    action = twlhd_tex_lifecycle_commit(&s);
    if (action & TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW) {
        free_active_count++;
    }
    CHECK_EQ_INT(free_active_count, 1);
    CHECK_EQ_INT(s.active_ready, 0);
    CHECK_EQ_INT(s.drop_active_on_commit, 0);
    CHECK_EQ_INT(s.textured_mode, 0);

    /* A second commit with no new stage must not free again. */
    action = twlhd_tex_lifecycle_commit(&s);
    CHECK_EQ_INT(action & TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW, 0);
    CHECK_EQ_INT(free_active_count, 1);

    test_report("tex-draw-plan/lifecycle-clear-then-commit");
}

static void test_lifecycle_flag_then_swap(void) {
    TwlhdTexLifecycleState s;

    twlhd_tex_lifecycle_init(&s);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.active_ready, 1);

    /* Force a clear, then stage a new textured frame before the next commit.
     * The flag says the old active must drop; the new pending must promote.
     * commit() must not report the same pointer twice. */
    CHECK_EQ_INT(twlhd_tex_lifecycle_clear(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(s.drop_active_on_commit, 1);
    CHECK_EQ_INT(s.pending_ready, 1);

    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s),
                 TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW);
    CHECK_EQ_INT(s.active_ready, 1);
    CHECK_EQ_INT(s.pending_ready, 0);
    CHECK_EQ_INT(s.drop_active_on_commit, 0);
    CHECK_EQ_INT(s.textured_mode, 1);

    test_report("tex-draw-plan/lifecycle-flag-then-swap");
}

static void test_lifecycle_shutdown(void) {
    TwlhdTexLifecycleState s;

    twlhd_tex_lifecycle_init(&s);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_commit(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_stage(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_shutdown(&s),
                 TWLHD_TEX_LIFECYCLE_FREE_ALL);
    CHECK_EQ_INT(s.pending_ready, 0);
    CHECK_EQ_INT(s.active_ready, 0);
    CHECK_EQ_INT(s.textured_mode, 0);
    CHECK_EQ_INT(s.drop_active_on_commit, 0);

    /* Shutdown from an already-cleared state. */
    twlhd_tex_lifecycle_init(&s);
    CHECK_EQ_INT(twlhd_tex_lifecycle_clear(&s), TWLHD_TEX_LIFECYCLE_NONE);
    CHECK_EQ_INT(twlhd_tex_lifecycle_shutdown(&s),
                 TWLHD_TEX_LIFECYCLE_NONE);

    test_report("tex-draw-plan/lifecycle-shutdown");
}

/* Hook save/restore helpers.  g_current_hook simulates the renderer's trace
 * hook variable; apply_action performs the renderer_set_trace() side effect
 * that arm11/source/main.c's tex_hook_apply() does. */
static TwlhdTexHookFn g_current_hook;

static void diag_hook(const char *step) {
    (void)step;
}

static void tex_hook(const char *step) {
    (void)step;
}

static void apply_action(TwlhdTexHookState *s, TwlhdTexHookAction action) {
    switch (action) {
    case TWLHD_TEX_HOOK_TAKE:
    case TWLHD_TEX_HOOK_REINSTALL:
        g_current_hook = tex_hook;
        break;
    case TWLHD_TEX_HOOK_RESTORE:
        g_current_hook = s->saved;
        break;
    case TWLHD_TEX_HOOK_NONE:
    default:
        break;
    }
}

static void test_tex_hook_basic(void) {
    TwlhdTexHookState s;

    twlhd_tex_hook_init(&s);
    g_current_hook = diag_hook;
    CHECK_EQ_INT(twlhd_tex_hook_take(&s, g_current_hook, tex_hook),
                 TWLHD_TEX_HOOK_TAKE);
    CHECK_EQ_INT(s.installed, 1);
    CHECK(s.saved == diag_hook);
    apply_action(&s, TWLHD_TEX_HOOK_TAKE);
    CHECK(g_current_hook == tex_hook);

    CHECK_EQ_INT(twlhd_tex_hook_leave(&s), TWLHD_TEX_HOOK_RESTORE);
    CHECK_EQ_INT(s.installed, 0);
    apply_action(&s, TWLHD_TEX_HOOK_RESTORE);
    CHECK(g_current_hook == diag_hook);

    CHECK_EQ_INT(twlhd_tex_hook_leave(&s), TWLHD_TEX_HOOK_NONE);
    CHECK(g_current_hook == diag_hook);

    test_report("tex-draw-plan/hook-basic");
}

static void test_tex_hook_reinstall_keeps_saved(void) {
    TwlhdTexHookState s;

    twlhd_tex_hook_init(&s);
    g_current_hook = diag_hook;
    CHECK_EQ_INT(twlhd_tex_hook_take(&s, g_current_hook, tex_hook),
                 TWLHD_TEX_HOOK_TAKE);
    apply_action(&s, TWLHD_TEX_HOOK_TAKE);

    /* A second textured frame while we already own the hook must reinstall
     * ours without overwriting the saved DIAG hook. */
    CHECK_EQ_INT(twlhd_tex_hook_take(&s, tex_hook, tex_hook),
                 TWLHD_TEX_HOOK_REINSTALL);
    CHECK(s.saved == diag_hook);
    apply_action(&s, TWLHD_TEX_HOOK_REINSTALL);
    CHECK(g_current_hook == tex_hook);

    CHECK_EQ_INT(twlhd_tex_hook_leave(&s), TWLHD_TEX_HOOK_RESTORE);
    apply_action(&s, TWLHD_TEX_HOOK_RESTORE);
    CHECK(g_current_hook == diag_hook);

    test_report("tex-draw-plan/hook-reinstall-keeps-saved");
}

static void test_tex_hook_required_sequence(void) {
    TwlhdTexHookState s;
    TwlhdTexHookAction action;

    twlhd_tex_hook_init(&s);
    g_current_hook = diag_hook;

    /* Untextured frame: no texture path attempted, hook untouched. */
    action = twlhd_tex_hook_leave(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_NONE);
    apply_action(&s, action);
    CHECK(g_current_hook == diag_hook);

    /* Textured frame: take the hook, install the texture callback. */
    action = twlhd_tex_hook_take(&s, g_current_hook, tex_hook);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_TAKE);
    apply_action(&s, action);
    CHECK(g_current_hook == tex_hook);
    CHECK(s.saved == diag_hook);

    /* Back to an untextured frame: restore the DIAG hook. */
    action = twlhd_tex_hook_leave(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_RESTORE);
    apply_action(&s, action);
    CHECK(g_current_hook == diag_hook);

    test_report("tex-draw-plan/hook-required-sequence");
}

static void test_tex_hook_textured_then_fallback_then_restore(void) {
    TwlhdTexHookState s;
    TwlhdTexHookAction action;

    twlhd_tex_hook_init(&s);
    g_current_hook = diag_hook;

    action = twlhd_tex_hook_take(&s, g_current_hook, tex_hook);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_TAKE);
    apply_action(&s, action);

    /* A budget/prepare/staging fallback calls leave() to restore the old hook. */
    action = twlhd_tex_hook_leave(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_RESTORE);
    apply_action(&s, action);
    CHECK(g_current_hook == diag_hook);

    /* A later untextured frame must not touch the hook again. */
    action = twlhd_tex_hook_leave(&s);
    CHECK_EQ_INT(action, TWLHD_TEX_HOOK_NONE);
    CHECK(g_current_hook == diag_hook);

    test_report("tex-draw-plan/hook-textured-then-fallback-then-restore");
}

/* D(6): draw-range guards.  end = first_vertex + vertex_count must fit in
 * uint32, must not exceed the supplied vertex_count, and vertex_count must be
 * a multiple of 3. */
static void test_draw_range_guard(void) {
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

    textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    textures[0].width = 8u;
    textures[0].height = 8u;
    textures[0].offset = 0u;
    et[0] = 0;
    es[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    /* Exact boundary: end == vertex_count. */
    plan[0].first_vertex = 3u;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 6u, 6u, out, 1u, &result),
                 TWLHD_TEX_DRAW_OK);

    /* One step past: end == vertex_count + 1. */
    plan[0].first_vertex = 4u;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 6u, 6u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_RANGE);
    CHECK_EQ_INT(result.all_untextured, 1);
    CHECK_EQ_INT(result.entry_count, 0);
    CHECK_EQ_INT(result.texture_count, 0);

    /* uint32 overflow of end. */
    plan[0].first_vertex = 0xFFFFFFFDu;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 6u, 6u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_RANGE);

    /* Exact fit at UINT32_MAX: end == UINT32_MAX must pass. */
    plan[0].first_vertex = 0xFFFFFFFCu;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                     out, 1u, &result),
                 TWLHD_TEX_DRAW_OK);

    /* One step past UINT32_MAX. */
    plan[0].first_vertex = 0xFFFFFFFDu;
    plan[0].vertex_count = 3u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                     out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_RANGE);

    /* vertex_count not a multiple of 3. */
    plan[0].first_vertex = 0u;
    plan[0].vertex_count = 2u;
    plan[0].untextured = 0;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 6u, 6u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_RANGE);

    CHECK(guard_intact(g_oob_out, 1u * sizeof(TwlhdTexDrawEntry), OOB_FILL));

    test_report("tex-draw-plan/draw-range-guard");
}

/* D(7): per-texture arena guard.  offset must be 128-byte aligned and
 * offset + width*height*4 must not exceed arena_used; the product is computed
 * in uint64_t so it cannot overflow before the comparison. */
static void test_texture_arena_guard(void) {
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
    et[0] = 0;
    es[0] = TWLHD_PICA_FRAME_ENTRY_OK;

    textures[0].wrap_s = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    textures[0].wrap_t = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;
    textures[0].width = 8u;
    textures[0].height = 8u;

    /* Exact boundary: offset 0 + 8*8*4 == 256 == arena_used. */
    textures[0].offset = 0u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 256u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_OK);

    /* One byte past: offset 0 + 256 > 255. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 255u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);
    CHECK_EQ_INT(result.all_untextured, 1);
    CHECK_EQ_INT(result.entry_count, 0);
    CHECK_EQ_INT(result.texture_count, 0);

    /* Aligned offset further into the arena: 128 + 256 <= 384. */
    textures[0].offset = 128u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 384u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_OK);

    /* One byte past with a non-zero aligned offset. */
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 383u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);

    /* Misaligned offset. */
    textures[0].offset = 1u;
    CHECK_EQ_INT(twlhd_tex_draw_plan_build(
                     plan, 1u, et, es, textures, 1u, g_arena, 1024u, 1u,
                     1024u, 1024u * 1024u * 4u, 3u, 3u, out, 1u, &result),
                 TWLHD_TEX_DRAW_FALLBACK_ALIGN);

    CHECK(guard_intact(g_oob_textures,
                       1u * sizeof(TwlhdPicaFrameTexture), OOB_FILL));
    CHECK(guard_intact(g_oob_out, 1u * sizeof(TwlhdTexDrawEntry), OOB_FILL));

    test_report("tex-draw-plan/texture-arena-guard");
}

int main(void) {
    test_basic_mapping();
    test_no_texture();
    test_failed_entries_untextured();
    test_budget_fallback();
    test_alignment_fallback();
    test_arena_used_one_null();
    test_null_inputs();
    test_out_of_bounds();
    test_strings();
    test_slot_equals_count();
    test_texture_size_guard();
    test_budget_boundaries();
    test_all_untextured_flag();
    test_misaligned_single_byte();
    test_skip_first_texture();
    test_lifecycle_stage_draw_twice();
    test_lifecycle_stage_then_fail();
    test_lifecycle_clear_then_commit();
    test_lifecycle_flag_then_swap();
    test_lifecycle_shutdown();
    test_tex_hook_basic();
    test_tex_hook_reinstall_keeps_saved();
    test_tex_hook_required_sequence();
    test_tex_hook_textured_then_fallback_then_restore();
    test_draw_range_guard();
    test_texture_arena_guard();
    return test_exit_code();
}
