// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2e-2 - host-testable draw-plan logic for the ARM11 textured
 * renderer path.
 *
 * Pure C11, allocation-free, no libctru. This module takes the already-prepared
 * bind plan, texture table and per-entry status from
 * twlhd_pica_frame_prepare() and turns it into a sequence of draw descriptors
 * the renderer can execute. All decisions that do not need the GPU live here.
 *
 * BUILD/UNIT only; no hardware verification.
 */
#pragma once

#include <stdint.h>

#include "twlhd_pica_frame.h"
#include "twlhd_pica_tex.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TWLHD_TEX_DRAW_OK = 0,
    /* The frame has no textured runs; caller should use the untextured path. */
    TWLHD_TEX_DRAW_NO_TEXTURE,
    /* A budget/capacity cap was exceeded; fall back whole-frame untextured. */
    TWLHD_TEX_DRAW_FALLBACK_BUDGET,
    /* An input count/capacity mismatch was detected (defensive). */
    TWLHD_TEX_DRAW_FALLBACK_CAPACITY,
    /* The arena pointer is not 128-byte aligned (hardware assumption). */
    TWLHD_TEX_DRAW_FALLBACK_ALIGN,
} TwlhdTexDrawVerdict;

/* Lifecycle actions returned by the pure state machine in
 * twlhd_tex_lifecycle_*.  They are bit-flags so a single transition can
 * request both pending and active release. */
typedef enum {
    TWLHD_TEX_LIFECYCLE_NONE = 0,
    TWLHD_TEX_LIFECYCLE_FREE_PENDING_NOW = 1,
    TWLHD_TEX_LIFECYCLE_FREE_ACTIVE_NOW = 2,
    TWLHD_TEX_LIFECYCLE_FREE_ALL = 3,
} TwlhdTexLifecycleAction;

/* Pure state for the pending/active texture-set lifecycle.  It mirrors the
 * ownership rule used by arm11/source/renderer.c: pending is always owned by
 * the CPU, active may still be read by the GPU until the next commit or until
 * the render target has been deleted. */
typedef struct {
    int pending_ready;
    int active_ready;
    int textured_mode;
    int drop_active_on_commit;
} TwlhdTexLifecycleState;

/*
 * PICA200 wrap/filter codes exported by this module. They are intentionally
 * bit-identical to libctru GPU_TEXTURE_WRAP_PARAM / GPU_TEXTURE_FILTER_PARAM
 * (3ds/gpu/enums.h:35-41, :62-65) so the renderer can pass them straight to
 * C3D_TexSetWrap / C3D_TexSetFilter.
 */
#define TWLHD_TEX_DRAW_WRAP_CLAMP_TO_EDGE   0u
#define TWLHD_TEX_DRAW_WRAP_CLAMP_TO_BORDER 1u
#define TWLHD_TEX_DRAW_WRAP_REPEAT          2u
#define TWLHD_TEX_DRAW_WRAP_MIRRORED_REPEAT 3u
#define TWLHD_TEX_DRAW_FILTER_NEAREST       0u
#define TWLHD_TEX_DRAW_FILTER_LINEAR        1u

/* One renderer draw call. */
typedef struct {
    uint32_t first_vertex;
    uint32_t vertex_count;
    int32_t texture_slot; /* -1 for untextured / failed entries. */
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t filter;
    int untextured;
} TwlhdTexDrawEntry;

typedef struct {
    TwlhdTexDrawVerdict verdict;
    uint32_t entry_count;
    uint32_t texture_count;
    int all_untextured; /* 1 when every entry is untextured or failed. */
} TwlhdTexDrawPlanResult;

/*
 * Map a prepared frame to a draw plan.
 *
 * Inputs are exactly the outputs of twlhd_pica_frame_prepare() that the
 * renderer needs. The function writes one TwlhdTexDrawEntry per plan entry into
 * out_entries (capacity out_capacity entries). It never allocates.
 *
 * Budget checks (all must pass for TEXTURED_OK):
 *   - arena_used <= max_arena_bytes
 *   - texture_count <= max_textures
 *   - every texture's width*height*4 <= max_texture_bytes
 *   - vertex_count <= max_texcoords
 *   - entry_count <= out_capacity
 *   - arena pointer is 128-byte aligned (UNVERIFIED hardware assumption)
 *
 * Per-entry status NO_DATA / UNSUPPORTED is treated as untextured (texture_slot
 * -1) with untextured == 1. The caller draws those entries with the existing
 * REPLACE/primary-colour path.
 *
 * Wrap codes are mapped from TwlhdPicaWrap 0..3 without re-mapping; filter is
 * forced to TWLHD_TEX_DRAW_FILTER_NEAREST as the DS-fidelity decision.
 */
TwlhdTexDrawVerdict twlhd_tex_draw_plan_build(
    const TwlhdPicaBindPlan *plan, uint32_t entry_count,
    const int32_t *entry_texture,
    const TwlhdPicaFrameEntryStatus *entry_status,
    const TwlhdPicaFrameTexture *textures, uint32_t texture_count,
    const uint8_t *arena, uint32_t arena_used,
    uint32_t max_textures, uint32_t max_arena_bytes,
    uint32_t max_texture_bytes, uint32_t max_texcoords,
    uint32_t vertex_count,
    TwlhdTexDrawEntry *out_entries, uint32_t out_capacity,
    TwlhdTexDrawPlanResult *out_result);

const char *twlhd_tex_draw_verdict_string(TwlhdTexDrawVerdict verdict);

/* Pure lifecycle state machine.  init/clear/stage/commit/shutdown update the
 * state and return the action(s) the caller must perform on the real GPU
 * resources.  The helper itself never touches pointers or memory. */
void twlhd_tex_lifecycle_init(TwlhdTexLifecycleState *s);
TwlhdTexLifecycleAction twlhd_tex_lifecycle_stage(TwlhdTexLifecycleState *s);
TwlhdTexLifecycleAction twlhd_tex_lifecycle_clear(TwlhdTexLifecycleState *s);
TwlhdTexLifecycleAction twlhd_tex_lifecycle_commit(TwlhdTexLifecycleState *s);
TwlhdTexLifecycleAction twlhd_tex_lifecycle_shutdown(TwlhdTexLifecycleState *s);

#ifdef __cplusplus
}
#endif
