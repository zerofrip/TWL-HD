// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - minimal citro3d/PICA200 debug renderer.
 *
 * HARDWARE UNVERIFIED: this code is build-verified only. The API patterns
 * follow the official devkitPro gpu example (examples/3ds/graphics/gpu/
 * simple_tri); no example code was pasted.
 */
#pragma once

#include <stdint.h>

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

/* Uploads a non-indexed triangle list (stride 48 bytes). Call only between
 * frames: it waits for P3D before freeing the previous buffer. A zero count
 * clears the scene. */
int renderer_set_geometry(const TwlhdVertex *vertices, uint32_t vertex_count);

/* Restores the deterministic built-in debug triangle. */
int renderer_use_builtin_triangle(void);

/* Records clear + uniforms + draw into the current C3D frame. Must be called
 * between C3D_FrameBegin()/C3D_FrameEnd(). */
void renderer_draw(void);

int renderer_is_ready(void);
uint32_t renderer_vertex_count(void);
const char *renderer_result_string(int result);
