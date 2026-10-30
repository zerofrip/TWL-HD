// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - minimal citro3d/PICA200 debug renderer.
 *
 * This is deliberately a debug view, not a DS reconstruction: Phase 4A found
 * that the capture contains no projection/view/model matrix and no texture
 * data, so the renderer substitutes a fixed DEBUG camera and flat per-vertex
 * color. Nothing here should be read as emulating the original DS output.
 *
 * API usage patterns adapted from the official devkitPro gpu examples; the
 * display-transfer flag combination and attribute/permutation setup follow
 * that boilerplate.
 *
 * HARDWARE UNVERIFIED: build-verified only. Never executed in the sandbox.
 */

#include <3ds.h>
#include <citro3d.h>

#include <math.h>
#include <string.h>

#include "renderer.h"
#include "twlhd_geometry_slots.h"

#include "twlhd_vshader_shbin.h"

#define RENDER_CLEAR_COLOR 0x101018FFu
#define RENDER_FOV_DEGREES  45.0f
#define RENDER_FIXED_DIR_X   0.6f
#define RENDER_FIXED_DIR_Y   0.4f
#define RENDER_FIXED_DIR_Z   1.0f

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct {
    int c3d_inited;
    int program_inited;
    int ready;
    C3D_RenderTarget *target;
    DVLB_s *dvlb;
    shaderProgram_s program;
    int u_loc_projection;
    int u_loc_modelview;
    TwlhdGeometrySlots slots;
    uint32_t vertex_count;
    C3D_Mtx projection;
    C3D_Mtx modelview;
    C3D_Mtx pending_modelview;
} RendererState;

static RendererState g_renderer;

/* Diagnostic trace hook. NULL by default: renderer_trace() is then a no-op and
 * the normal build is unchanged. Deliberately outside RendererState so it
 * survives the memset() at the top of renderer_init().
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy. */
static TwlhdRendererTraceFn g_trace;

void renderer_set_trace(TwlhdRendererTraceFn cb) {
    g_trace = cb;
}

void renderer_trace(const char *step) {
    if (g_trace) g_trace(step);
}

/* Deterministic built-in triangle, used on startup and after Y. */
static const TwlhdVertex g_builtin_triangle[3] = {
    {
        {-0.8f, -0.6f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f},
    },
    {
        {0.8f, -0.6f, 0.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f},
    },
    {
        {0.0f, 0.8f, 0.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.5f, 1.0f},
    },
};

static void update_camera(const TwlhdVertex *vertices, uint32_t count) {
    float minimum[3];
    float maximum[3];
    float center[3];
    float radius;
    float distance;
    C3D_FVec direction;
    C3D_FVec eye;

    if (!twlhd_vertex_bounds(vertices, count, minimum, maximum)) {
        center[0] = center[1] = center[2] = 0.0f;
        radius = 0.0f;
    } else {
        int axis;

        for (axis = 0; axis < 3; axis++) {
            center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
        }

        {
            float dx = maximum[0] - minimum[0];
            float dy = maximum[1] - minimum[1];
            float dz = maximum[2] - minimum[2];
            radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
        }
    }

    /* Degenerate bounds (a point or a flat/empty scene) still get a valid
     * camera rather than NaNs or a divide by zero. */
    if (!(radius > 1e-4f)) radius = 1.0f;

    /* Bounding-sphere fit for a 45 degree vertical FOV, with margin. */
    distance = radius * 2.8f;
    direction = FVec4_Normalize(FVec4_New(RENDER_FIXED_DIR_X,
                                          RENDER_FIXED_DIR_Y,
                                          RENDER_FIXED_DIR_Z, 0.0f));
    eye = FVec4_New(center[0] + direction.x * distance,
                    center[1] + direction.y * distance,
                    center[2] + direction.z * distance, 1.0f);

    Mtx_LookAt(&g_renderer.pending_modelview, eye,
               FVec4_New(center[0], center[1], center[2], 1.0f),
               FVec4_New(0.0f, 1.0f, 0.0f, 1.0f), false);
}

/* Idempotent shutdown. The caller must NOT be inside a C3D frame:
 * C3D_RenderTargetDelete panics if C3D_FrameBegin was called without a matching
 * C3D_FrameEnd. */
void renderer_shutdown(void) {
    /* Pending was never bound to a submitted frame; free it immediately. */
    twlhd_geometry_slots_release_pending(&g_renderer.slots);

    if (g_renderer.program_inited) {
        shaderProgramFree(&g_renderer.program);
        g_renderer.program_inited = 0;
    }

    if (g_renderer.dvlb) {
        DVLB_Free(g_renderer.dvlb);
        g_renderer.dvlb = NULL;
    }

    /* C3D_RenderTargetDelete drains the GPU queue, so the active buffer is
     * safe to free only after it returns. See upstream citro3d
     * renderqueue.c/base.c, read from the devkitPro/citro3d repository;
     * HARDWARE UNVERIFIED (reasoned from source, never executed). */
    if (g_renderer.target) {
        C3D_RenderTargetDelete(g_renderer.target);
        g_renderer.target = NULL;
    }

    twlhd_geometry_slots_release_active(&g_renderer.slots);

    if (g_renderer.c3d_inited) {
        C3D_Fini();
        g_renderer.c3d_inited = 0;
    }

    g_renderer.ready = 0;
    g_renderer.vertex_count = 0;
    g_renderer.u_loc_projection = -1;
    g_renderer.u_loc_modelview = -1;
}

int renderer_init(void) {
    C3D_AttrInfo *attr_info;
    C3D_TexEnv *tex_env;
    int result = TWLHD_RENDER_OK;
    int i;

    if (g_renderer.ready) return TWLHD_RENDER_OK;

    memset(&g_renderer, 0, sizeof g_renderer);
    g_renderer.u_loc_projection = -1;
    g_renderer.u_loc_modelview = -1;
    twlhd_geometry_slots_init(&g_renderer.slots, linearAlloc, linearFree);

    renderer_trace("C3D_Init");
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return TWLHD_RENDER_ERR_C3D_INIT;
    }
    g_renderer.c3d_inited = 1;

    renderer_trace("RenderTargetCreate");
    g_renderer.target = C3D_RenderTargetCreate(
        240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!g_renderer.target) {
        result = TWLHD_RENDER_ERR_TARGET;
        goto fail;
    }
    renderer_trace("SetOutput");
    C3D_RenderTargetSetOutput(g_renderer.target, GFX_TOP, GFX_LEFT,
                              DISPLAY_TRANSFER_FLAGS);

    renderer_trace("DVLB_ParseFile");
    g_renderer.dvlb = DVLB_ParseFile(
        (u32 *)(void *)twlhd_vshader_shbin,
        (u32)twlhd_vshader_shbin_size);
    if (!g_renderer.dvlb) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }

    renderer_trace("shaderProgramInit");
    if (R_FAILED(shaderProgramInit(&g_renderer.program))) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }
    g_renderer.program_inited = 1;

    renderer_trace("SetVsh");
    if (R_FAILED(shaderProgramSetVsh(&g_renderer.program,
                                     &g_renderer.dvlb->DVLE[0]))) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }
    renderer_trace("BindProgram");
    C3D_BindProgram(&g_renderer.program);

    renderer_trace("uniform");
    g_renderer.u_loc_projection = shaderInstanceGetUniformLocation(
        g_renderer.program.vertexShader, "projection");
    g_renderer.u_loc_modelview = shaderInstanceGetUniformLocation(
        g_renderer.program.vertexShader, "modelview");
    if (g_renderer.u_loc_projection < 0 ||
        g_renderer.u_loc_modelview < 0) {
        result = TWLHD_RENDER_ERR_UNIFORM;
        goto fail;
    }

    renderer_trace("attr");
    attr_info = C3D_GetAttrInfo();
    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3); /* v0 = position */
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4); /* v1 = color    */

    renderer_trace("PerspTilt");
    Mtx_PerspTilt(&g_renderer.projection, C3D_AngleFromDegrees(
                      RENDER_FOV_DEGREES), C3D_AspectRatioTop,
                  0.01f, 1000.0f, false);

    /* Flat pass-through of the per-vertex color; no texture is captured. */
    renderer_trace("TexEnv");
    for (i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    tex_env = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tex_env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(tex_env, C3D_Both, GPU_REPLACE);

    /* Debug view: depth on, culling off so mis-wound geometry is visible. */
    renderer_trace("DepthTest");
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    renderer_trace("CullFace");
    C3D_CullFace(GPU_CULL_NONE);

    g_renderer.ready = 1;

    renderer_trace("builtin");
    if (renderer_use_builtin_triangle() != TWLHD_RENDER_OK) {
        result = TWLHD_RENDER_ERR_GEOMETRY_ALLOC;
        goto fail;
    }

    return TWLHD_RENDER_OK;

fail:
    renderer_shutdown();
    return result;
}

int renderer_set_geometry(const TwlhdVertex *vertices,
                          uint32_t vertex_count) {
    size_t bytes;

    if (!g_renderer.ready) return TWLHD_RENDER_ERR_NOT_READY;
    if (!vertices && vertex_count != 0) return TWLHD_RENDER_ERR_NULL;

    bytes = (size_t)vertex_count * sizeof(TwlhdVertex);

    /* Stage the new geometry as pending. The active buffer is intentionally
     * left untouched: it may still be read by the frame currently in flight.
     * It will be promoted to active (and the old active freed) inside
     * renderer_draw(), which runs after C3D_FrameBegin has drained the prior
     * frame's queue. See upstream citro3d renderqueue.c/base.c, read from the
     * devkitPro/citro3d repository; HARDWARE UNVERIFIED. */
    if (twlhd_geometry_slots_stage(&g_renderer.slots, vertices, bytes) != 0) {
        return TWLHD_RENDER_ERR_GEOMETRY_ALLOC;
    }

    g_renderer.vertex_count = vertex_count;

    if (vertex_count != 0) {
        update_camera(vertices, vertex_count);
    }
    return TWLHD_RENDER_OK;
}

int renderer_use_builtin_triangle(void) {
    return renderer_set_geometry(g_builtin_triangle, 3);
}

void renderer_draw(void) {
    if (!g_renderer.ready) return;

    /* By documented precondition this runs between a successful
     * C3D_FrameBegin and C3D_FrameEnd. FrameBegin drains the previous frame's
     * queue, so the old active buffer is no longer read by the GPU and can be
     * freed here. See upstream citro3d renderqueue.c/base.c, read from the
     * devkitPro/citro3d repository; HARDWARE UNVERIFIED. */
    renderer_trace("commit/BufInfo");
    if (g_renderer.slots.pending) {
        C3D_BufInfo *pending_buf_info;

        twlhd_geometry_slots_commit(&g_renderer.slots);

        /* linearAlloc() memory is CPU-cached on 3DS; the GPU DMA/read path
         * does not snoop the CPU cache, so the memcpy in stage() can leave
         * stale lines unless flushed. Hang suspect #1: works in emulator,
         * stalls on hardware. The Result is deliberately ignored; a silent
         * failure would mask the fix, which is acceptable for this
         * diagnostic build. HARDWARE UNVERIFIED (build-verified only);
         * this build's purpose is real-hardware bisection. Do not deploy. */
        GSPGPU_FlushDataCache(g_renderer.slots.active,
                              (u32)g_renderer.slots.active_size);

        pending_buf_info = C3D_GetBufInfo();
        BufInfo_Init(pending_buf_info);
        BufInfo_Add(pending_buf_info, g_renderer.slots.active,
                    sizeof(TwlhdVertex), 2, 0x10);

        g_renderer.modelview = g_renderer.pending_modelview;
    }

    renderer_trace("RenderTargetClear");
    C3D_RenderTargetClear(g_renderer.target, C3D_CLEAR_ALL,
                          RENDER_CLEAR_COLOR, 0);
    renderer_trace("FrameDrawOn");
    C3D_FrameDrawOn(g_renderer.target);

    renderer_trace("BindProgram");
    C3D_BindProgram(&g_renderer.program);
    renderer_trace("FVUnif");
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_projection,
                     &g_renderer.projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_modelview,
                     &g_renderer.modelview);

    renderer_trace("DrawArrays");
    if (g_renderer.slots.active && g_renderer.vertex_count != 0) {
        C3D_DrawArrays(GPU_TRIANGLES, 0, (int)g_renderer.vertex_count);
    }
}

int renderer_is_ready(void) {
    return g_renderer.ready;
}

uint32_t renderer_vertex_count(void) {
    return g_renderer.vertex_count;
}

const char *renderer_result_string(int result) {
    switch (result) {
    case TWLHD_RENDER_OK:               return "OK";
    case TWLHD_RENDER_ERR_C3D_INIT:     return "C3D init failed";
    case TWLHD_RENDER_ERR_TARGET:       return "render target failed";
    case TWLHD_RENDER_ERR_SHADER:       return "shader load failed";
    case TWLHD_RENDER_ERR_UNIFORM:      return "shader uniform missing";
    case TWLHD_RENDER_ERR_GEOMETRY_ALLOC: return "vertex buffer alloc failed";
    case TWLHD_RENDER_ERR_NULL:         return "NULL vertex data";
    case TWLHD_RENDER_ERR_NOT_READY:    return "renderer not initialized";
    default:                            return "unknown renderer error";
    }
}
