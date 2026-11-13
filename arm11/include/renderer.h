// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - minimal citro3d/PICA200 debug renderer.
 *
 * HARDWARE UNVERIFIED: this code is build-verified only. API usage patterns
 * are adapted from the official devkitPro gpu examples; the display-transfer
 * flag combination and attribute/permutation setup follow that boilerplate.
 */
#pragma once

#include <stdint.h>

#include "twlhd_gx_interp.h"
#include "twlhd_pica_frame.h"
#include "twlhd_pica_tex.h"
#include "twlhd_vertex_convert.h"

typedef enum {
    TWLHD_RENDER_OK = 0,
    TWLHD_RENDER_ERR_C3D_INIT,
    TWLHD_RENDER_ERR_TARGET,
    TWLHD_RENDER_ERR_SHADER,
    TWLHD_RENDER_ERR_UNIFORM,
    TWLHD_RENDER_ERR_GEOMETRY_ALLOC,
    TWLHD_RENDER_ERR_NULL,
    TWLHD_RENDER_ERR_NOT_READY,
} TwlhdRenderResult;

/* Initializes C3D, the top-screen target, shader program and the built-in
 * debug triangle. Returns TWLHD_RENDER_OK or an ERR_* code; on failure every
 * initialized step has already been unwound. */
int renderer_init(void);

/* Idempotent, safe after a partial renderer_init() failure. Does not call
 * gfxExit(); the caller owns gfxInitDefault()/gfxExit(). */
void renderer_shutdown(void);

/* Stages a non-indexed triangle list (stride 48 bytes). The upload is
 * deferred: the new buffer becomes active inside the next renderer_draw()
 * after C3D_FrameBegin has drained the prior frame's queue. A zero count
 * clears the pending buffer and sets the drawn vertex count to 0, so nothing
 * is drawn; the previous active buffer stays allocated (unused) until the
 * next non-empty commit or renderer_shutdown().
 * Never waits on the GPU. */
int renderer_set_geometry(const TwlhdVertex *vertices, uint32_t vertex_count);

/* Restores the deterministic built-in debug triangle. */
int renderer_use_builtin_triangle(void);

/* Stages a non-indexed triangle list of pre-computed DS clip-space vertices
 * (stride 32 bytes: clip x,y,z,w followed by rgba). The clipfix matrix for
 * `viewport` (NULL = full screen) is staged too. Like renderer_set_geometry(),
 * the upload is deferred: it becomes active inside the next renderer_draw(),
 * which then uses the clip-space shader path instead of the DEBUG path.
 * A zero count clears the pending buffer and draws nothing. */
int renderer_set_clip_scene(const TwlhdGxVertex *vertices,
                            uint32_t vertex_count,
                            const TwlhdGxViewport *viewport);

/* Stage a textured clip scene produced by twlhd_pica_frame_prepare(). On any
 * failure the renderer falls back to the untextured clip path (the caller may
 * also fall back by using renderer_set_clip_scene() instead). */
int renderer_set_clip_scene_textured(
    const TwlhdGxVertex *vertices, uint32_t vertex_count,
    const TwlhdGxViewport *viewport,
    const TwlhdPicaBindPlan *plan, uint32_t entry_count,
    const int32_t *entry_texture,
    const TwlhdPicaFrameEntryStatus *entry_status,
    const TwlhdPicaFrameTexture *textures, uint32_t texture_count,
    const uint8_t *arena, uint32_t arena_used,
    const TwlhdPicaTexCoord *texcoords,
    uint32_t texcoord_capacity,
    uint32_t max_textures, uint32_t max_arena_bytes,
    uint32_t max_texture_bytes);

/* Records clear + uniforms + draw into the current C3D frame. Must be called
 * between C3D_FrameBegin()/C3D_FrameEnd(). */
void renderer_draw(void);

int renderer_is_ready(void);
uint32_t renderer_vertex_count(void);
const char *renderer_result_string(int result);

/* Last textured staging status from renderer_set_clip_scene_textured(),
 * or an empty string if no textured staging has happened. */
const char *renderer_get_tex_staging_status(void);

/* Per-frame runtime debug toggles. All default to zero. Vertex buffer padding
 * is declared by default so that the declared bytes match the stride; NO_PAD
 * reverts to the old unpadded behaviour for diagnostic contrast. Init-time
 * flags (ORTHO, NO_STATE) take effect on the next renderer_init(); draw-time
 * flags are checked every renderer_draw().
 *
 * Layout flags are mutually exclusive; if several are set the precedence is
 * OFFICIAL_ATTR > LAYOUT_SPLIT > LAYOUT_STRIDE > LAYOUT_PACKED28 >
 * LAYOUT_FIXED48 > LAYOUT_NORMAL_COPY, and the normal interleaved 48-byte
 * layout is used when none are set. */
#define TWLHD_RDBG_SKIP_DRAW         0x01u
#define TWLHD_RDBG_SKIP_CLEAR        0x02u
#define TWLHD_RDBG_OFFICIAL_ATTR     0x04u
#define TWLHD_RDBG_ORTHO             0x08u
#define TWLHD_RDBG_NO_STATE          0x10u
#define TWLHD_RDBG_NO_UNIFORMS       0x20u
#define TWLHD_RDBG_NO_BIND           0x40u
#define TWLHD_RDBG_LAYOUT_FIXED48    0x80u
#define TWLHD_RDBG_LAYOUT_PACKED28   0x100u
#define TWLHD_RDBG_LAYOUT_SPLIT      0x200u
#define TWLHD_RDBG_LAYOUT_STRIDE     0x800u
#define TWLHD_RDBG_LAYOUT_NORMAL_COPY 0x400u
#define TWLHD_RDBG_NO_PAD            0x1000u

void renderer_set_debug_flags(uint32_t flags);
uint32_t renderer_get_debug_flags(void);

/* Clip-path diagnostic mode. 0=normal, 1=alpha-as-gray, 2=const-alpha-0.5.
 * Persists across renderer_init() because it lives outside RendererState. */
void renderer_set_diag_mode(unsigned mode);
unsigned renderer_get_diag_mode(void);

/* Parametric stride-layout parameters. Defaults are 48 bytes, 3 vertices and
 * 0 padding. Setters clamp/ignore out-of-range values. The active values are
 * read by renderer_draw() only when TWLHD_RDBG_LAYOUT_STRIDE is set. */
void renderer_set_debug_stride(uint32_t bytes);
void renderer_set_debug_vertex_count(uint32_t n);
void renderer_set_debug_pad(uint32_t bytes);

/* Diagnostic trace hook. renderer_trace() calls the installed callback (if
 * any) immediately BEFORE the named citro3d/PICA step, so the last step seen
 * names the call that stalled. The hook defaults to NULL, in which case
 * renderer_trace() is a no-op and the normal path is unchanged.
 *
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy. */
typedef void (*TwlhdRendererTraceFn)(const char *step);

void renderer_set_trace(TwlhdRendererTraceFn cb);
TwlhdRendererTraceFn renderer_get_trace(void);
void renderer_trace(const char *step);
