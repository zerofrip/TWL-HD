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
#include <stdbool.h>
#include <string.h>

#include "renderer.h"
#include "twlhd_attr_pad.h"
#include "twlhd_clipfix.h"
#include "twlhd_geometry_slots.h"

#include "twlhd_clip_vshader_shbin.h"
#include "twlhd_vshader_shbin.h"

/* The clip vertex layout is exactly two float4 loaders. */
_Static_assert(sizeof(TwlhdGxVertex) == 32,
               "TwlhdGxVertex must be 32 bytes for the clip vertex layout");

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
    int layout_mode;
    int layout_no_pad;
    DVLB_s *clip_dvlb;
    shaderProgram_s clip_program;
    int clip_program_inited;
    int u_loc_clipfix;
    float clipfix_pending[4][4];
    float clipfix_active[4][4];
    int pending_mode;
    int active_mode;
} RendererState;

typedef enum {
    RENDER_MODE_DEBUG = 0,
    RENDER_MODE_CLIP,
} RenderMode;

typedef enum {
    LAYOUT_NORMAL = 0,
    LAYOUT_OFFICIAL,
    LAYOUT_FIXED48,
    LAYOUT_PACKED28,
    LAYOUT_SPLIT,
    LAYOUT_STRIDE,
    LAYOUT_NORMAL_COPY,
} LayoutMode;

static RendererState g_renderer;

/* Diagnostic trace hook. NULL by default: renderer_trace() is then a no-op and
 * the normal build is unchanged. Deliberately outside RendererState so it
 * survives the memset() at the top of renderer_init().
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy. */
static TwlhdRendererTraceFn g_trace;

/* Runtime debug toggles. Deliberately outside RendererState so they survive
 * the memset() at the top of renderer_init(). */
static uint32_t g_debug_flags;

/* Positions-only buffer used when TWLHD_RDBG_OFFICIAL_ATTR is set, matching
 * the official simple_tri attribute/buffer layout. */
static void *g_official_buf;

/* Buffers for the draw-time layout bisection modes. Each is allocated lazily
 * on first use and freed in renderer_shutdown(). */
static void *g_fixed48_buf;
static void *g_packed28_buf;
static void *g_split_pos_buf;
static void *g_split_clr_buf;

/* Parametric stride-layout state. Kept outside RendererState so it survives
 * the memset() at the top of renderer_init(). */
static uint32_t g_debug_stride = 48;
static uint32_t g_debug_vertex_count = 3;
static uint32_t g_debug_pad = 0;
static void *g_stride_buf;
static uint32_t g_stride_buf_stride;
static uint32_t g_stride_buf_count;
static uint32_t g_stride_buf_pad;

void renderer_set_trace(TwlhdRendererTraceFn cb) {
    g_trace = cb;
}

void renderer_trace(const char *step) {
    if (g_trace) g_trace(step);
}

__attribute__((noinline, used)) void renderer_set_debug_flags(uint32_t flags) {
    g_debug_flags = flags;
    /* Reference the getter so the API pair stays together and reachable. */
    (void)renderer_get_debug_flags();
}

__attribute__((noinline, used)) uint32_t renderer_get_debug_flags(void) {
    return g_debug_flags;
}

__attribute__((noinline, used)) void renderer_set_debug_stride(uint32_t bytes) {
    if (bytes < 28) bytes = 28;
    if (bytes > 128) bytes = 128;
    bytes &= ~3u;
    g_debug_stride = bytes;
}

__attribute__((noinline, used)) void renderer_set_debug_vertex_count(uint32_t n) {
    if (n < 3 || n > 3000 || (n % 3) != 0) return;
    g_debug_vertex_count = n;
}

__attribute__((noinline, used)) void renderer_set_debug_pad(uint32_t bytes) {
    if (bytes > 8192) return;
    g_debug_pad = bytes;
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

/* Positions-only copy of g_builtin_triangle for the simple_tri-style path. */
static const float g_builtin_positions[3][3] = {
    {-0.8f, -0.6f, 0.0f},
    { 0.8f, -0.6f, 0.0f},
    { 0.0f,  0.8f, 0.0f},
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

static void setup_normal_attr_only(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3); /* v0 = position */
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4); /* v1 = color    */
}

/* Add one PICA200 attribute buffer, declaring padding components whenever the
 * buffer's stride is larger than its declared attribute bytes. Hardware Run 5
 * showed that the normal 48-byte interleaved layout works only when the gap
 * is declared (S1), while the same layout without the declaration wedged the
 * GPU (S3). The PICA200 requires the total declared bytes of an attribute
 * buffer to equal its stride; any unused bytes must be exposed as padding
 * components. The NO_PAD flag skips the padding declaration and restores the
 * old behaviour for diagnostic contrast. */
static void add_bufinfo_padded(void *buf, uint32_t stride,
                               uint32_t real_components,
                               uint64_t real_perm,
                               uint32_t declared_bytes) {
    C3D_BufInfo *buf_info = C3D_GetBufInfo();
    uint64_t perm = real_perm;
    uint32_t components = real_components;

    if (!(g_debug_flags & TWLHD_RDBG_NO_PAD)) {
        if (twlhd_attr_pad_perm(stride, declared_bytes, real_components,
                                real_perm, &perm, &components) == 0) {
            if (components != real_components || perm != real_perm) {
                renderer_trace("RDBG pad decl");
            }
        } else {
            renderer_trace("RDBG pad fail");
            perm = real_perm;
            components = real_components;
        }
    }

    BufInfo_Add(buf_info, buf, stride, (s8)components, perm);
}

static void setup_normal_attr_buffer(void) {
    setup_normal_attr_only();
    if (!g_renderer.slots.active) return;

    BufInfo_Init(C3D_GetBufInfo());
    add_bufinfo_padded(g_renderer.slots.active, sizeof(TwlhdVertex),
                       2, 0x10, 28);
}

static void setup_official_attr(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    C3D_BufInfo *buf_info;

    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddFixed(attr_info, 1);
    C3D_FixedAttribSet(1, 1.0f, 1.0f, 1.0f, 1.0f);

    if (!g_official_buf) {
        g_official_buf = linearAlloc(sizeof g_builtin_positions);
        if (g_official_buf) {
            memcpy(g_official_buf, g_builtin_positions,
                   sizeof g_builtin_positions);
            GSPGPU_FlushDataCache(g_official_buf,
                                  (u32)sizeof g_builtin_positions);
        }
    }

    if (!g_official_buf) return;

    buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    BufInfo_Add(buf_info, g_official_buf, sizeof(float) * 3, 1, 0x0);
}

/* Allocate and flush the shared private 48-byte copy of the built-in triangle.
 * Used by both FIXED48 (one loader, fixed color) and NORMAL_COPY (normal
 * two-loader layout from a private buffer instead of slots.active). */
static void ensure_fixed48_buf(void) {
    if (!g_fixed48_buf) {
        g_fixed48_buf = linearAlloc(sizeof g_builtin_triangle);
        if (g_fixed48_buf) {
            memcpy(g_fixed48_buf, g_builtin_triangle,
                   sizeof g_builtin_triangle);
            GSPGPU_FlushDataCache(g_fixed48_buf,
                                  (u32)sizeof g_builtin_triangle);
        }
    }
}

static void setup_fixed48_attr(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();

    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddFixed(attr_info, 1);
    C3D_FixedAttribSet(1, 1.0f, 1.0f, 1.0f, 1.0f);

    ensure_fixed48_buf();
    if (!g_fixed48_buf) return;

    BufInfo_Init(C3D_GetBufInfo());
    add_bufinfo_padded(g_fixed48_buf, sizeof(TwlhdVertex), 1, 0x0, 12);
}

static void setup_normalcopy_attr(void) {
    ensure_fixed48_buf();
    if (!g_fixed48_buf) return;

    setup_normal_attr_only();

    BufInfo_Init(C3D_GetBufInfo());
    add_bufinfo_padded(g_fixed48_buf, sizeof(TwlhdVertex), 2, 0x10, 28);
}

static void setup_packed28_attr(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    C3D_BufInfo *buf_info;
    int i;

    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4);

    if (!g_packed28_buf) {
        /* 3 vertices * (3 pos + 4 rgba) floats = 84 bytes. */
        g_packed28_buf = linearAlloc(3 * 28);
        if (g_packed28_buf) {
            float *dst = (float *)g_packed28_buf;
            for (i = 0; i < 3; i++) {
                dst[0] = g_builtin_triangle[i].pos[0];
                dst[1] = g_builtin_triangle[i].pos[1];
                dst[2] = g_builtin_triangle[i].pos[2];
                dst[3] = g_builtin_triangle[i].rgba[0];
                dst[4] = g_builtin_triangle[i].rgba[1];
                dst[5] = g_builtin_triangle[i].rgba[2];
                dst[6] = g_builtin_triangle[i].rgba[3];
                dst += 7;
            }
            GSPGPU_FlushDataCache(g_packed28_buf, (u32)(3 * 28));
        }
    }

    if (!g_packed28_buf) return;

    buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    BufInfo_Add(buf_info, g_packed28_buf, 28, 2, 0x10);
}

static void setup_split_attr(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();
    C3D_BufInfo *buf_info;
    int i;

    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4);

    if (!g_split_pos_buf) {
        g_split_pos_buf = linearAlloc(3 * sizeof(float[3]));
        if (g_split_pos_buf) {
            float (*dst)[3] = (float (*)[3])g_split_pos_buf;
            for (i = 0; i < 3; i++) {
                dst[i][0] = g_builtin_triangle[i].pos[0];
                dst[i][1] = g_builtin_triangle[i].pos[1];
                dst[i][2] = g_builtin_triangle[i].pos[2];
            }
            GSPGPU_FlushDataCache(g_split_pos_buf,
                                  (u32)(3 * sizeof(float[3])));
        }
    }

    if (!g_split_clr_buf) {
        g_split_clr_buf = linearAlloc(3 * sizeof(float[4]));
        if (g_split_clr_buf) {
            float (*dst)[4] = (float (*)[4])g_split_clr_buf;
            for (i = 0; i < 3; i++) {
                dst[i][0] = g_builtin_triangle[i].rgba[0];
                dst[i][1] = g_builtin_triangle[i].rgba[1];
                dst[i][2] = g_builtin_triangle[i].rgba[2];
                dst[i][3] = g_builtin_triangle[i].rgba[3];
            }
            GSPGPU_FlushDataCache(g_split_clr_buf,
                                  (u32)(3 * sizeof(float[4])));
        }
    }

    if (!g_split_pos_buf || !g_split_clr_buf) return;

    buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    /* Verified against devkitPro's multiple_buf example: permutation 0x0
     * maps buffer A to attribute 0, permutation 0x1 maps buffer B to
     * attribute 1. */
    BufInfo_Add(buf_info, g_split_pos_buf, sizeof(float[3]), 1, 0x0);
    BufInfo_Add(buf_info, g_split_clr_buf, sizeof(float[4]), 1, 0x1);
}

/* Rebuild the parametric stride buffer if the requested parameters differ from
 * the ones the current buffer was built with. This runs inside renderer_draw()
 * before the BufInfo/AttrInfo setup, so the GPU is idle: the diag only changes
 * parameters after a completed (probed) frame. */
static void ensure_stride_buf(void) {
    size_t size;
    uint32_t i;

    if (g_stride_buf &&
        g_stride_buf_stride == g_debug_stride &&
        g_stride_buf_count == g_debug_vertex_count &&
        g_stride_buf_pad == g_debug_pad) {
        return;
    }

    if (g_stride_buf) {
        linearFree(g_stride_buf);
        g_stride_buf = NULL;
    }

    size = (size_t)g_debug_stride * g_debug_vertex_count + g_debug_pad;
    g_stride_buf = linearAlloc(size);
    if (!g_stride_buf) return;

    memset(g_stride_buf, 0, size);
    for (i = 0; i < g_debug_vertex_count; i++) {
        const TwlhdVertex *v = &g_builtin_triangle[i % 3];
        float *pos = (float *)((uint8_t *)g_stride_buf +
                               (size_t)i * g_debug_stride);
        float *rgba = (float *)((uint8_t *)g_stride_buf +
                                (size_t)i * g_debug_stride + 12);
        pos[0] = v->pos[0];
        pos[1] = v->pos[1];
        pos[2] = v->pos[2];
        rgba[0] = v->rgba[0];
        rgba[1] = v->rgba[1];
        rgba[2] = v->rgba[2];
        rgba[3] = v->rgba[3];
    }

    g_stride_buf_stride = g_debug_stride;
    g_stride_buf_count = g_debug_vertex_count;
    g_stride_buf_pad = g_debug_pad;
    GSPGPU_FlushDataCache(g_stride_buf, (u32)size);
}

static void setup_stride_attr(void) {
    C3D_AttrInfo *attr_info = C3D_GetAttrInfo();

    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4);

    if (!g_stride_buf) return;

    BufInfo_Init(C3D_GetBufInfo());
    add_bufinfo_padded(g_stride_buf, g_debug_stride, 2, 0x10, 28);
}

static LayoutMode compute_target_layout(void) {
    if (g_debug_flags & TWLHD_RDBG_OFFICIAL_ATTR) return LAYOUT_OFFICIAL;
    if (g_debug_flags & TWLHD_RDBG_LAYOUT_SPLIT) return LAYOUT_SPLIT;
    if (g_debug_flags & TWLHD_RDBG_LAYOUT_STRIDE) return LAYOUT_STRIDE;
    if (g_debug_flags & TWLHD_RDBG_LAYOUT_PACKED28) return LAYOUT_PACKED28;
    if (g_debug_flags & TWLHD_RDBG_LAYOUT_FIXED48) return LAYOUT_FIXED48;
    if (g_debug_flags & TWLHD_RDBG_LAYOUT_NORMAL_COPY) return LAYOUT_NORMAL_COPY;
    return LAYOUT_NORMAL;
}

static int layout_buffer_ready(LayoutMode mode) {
    switch (mode) {
    case LAYOUT_OFFICIAL:     return g_official_buf != NULL;
    case LAYOUT_FIXED48:      return g_fixed48_buf != NULL;
    case LAYOUT_NORMAL_COPY:  return g_fixed48_buf != NULL;
    case LAYOUT_PACKED28:     return g_packed28_buf != NULL;
    case LAYOUT_SPLIT:        return g_split_pos_buf != NULL &&
                                     g_split_clr_buf != NULL;
    case LAYOUT_STRIDE:       return g_stride_buf != NULL;
    default:                  return 0;
    }
}

static void apply_layout(LayoutMode mode, int trace) {
    switch (mode) {
    case LAYOUT_OFFICIAL:
        renderer_trace("RDBG official attr");
        setup_official_attr();
        break;
    case LAYOUT_FIXED48:
        renderer_trace("RDBG fixed48 layout");
        setup_fixed48_attr();
        break;
    case LAYOUT_NORMAL_COPY:
        renderer_trace("RDBG normalcopy layout");
        setup_normalcopy_attr();
        break;
    case LAYOUT_PACKED28:
        renderer_trace("RDBG packed28 layout");
        setup_packed28_attr();
        break;
    case LAYOUT_SPLIT:
        renderer_trace("RDBG split layout");
        setup_split_attr();
        break;
    case LAYOUT_STRIDE:
        /* The stride buffer and attribute/buffer info are set up inside
         * renderer_draw() so that parameter changes can be applied while the
         * GPU is idle. */
        break;
    default:
        if (trace) renderer_trace("RDBG restore attr");
        setup_normal_attr_buffer();
        break;
    }
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

    if (g_renderer.clip_program_inited) {
        shaderProgramFree(&g_renderer.clip_program);
        g_renderer.clip_program_inited = 0;
    }

    if (g_renderer.clip_dvlb) {
        DVLB_Free(g_renderer.clip_dvlb);
        g_renderer.clip_dvlb = NULL;
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

    if (g_official_buf) {
        linearFree(g_official_buf);
        g_official_buf = NULL;
    }
    if (g_fixed48_buf) {
        linearFree(g_fixed48_buf);
        g_fixed48_buf = NULL;
    }
    if (g_packed28_buf) {
        linearFree(g_packed28_buf);
        g_packed28_buf = NULL;
    }
    if (g_split_pos_buf) {
        linearFree(g_split_pos_buf);
        g_split_pos_buf = NULL;
    }
    if (g_split_clr_buf) {
        linearFree(g_split_clr_buf);
        g_split_clr_buf = NULL;
    }
    if (g_stride_buf) {
        linearFree(g_stride_buf);
        g_stride_buf = NULL;
    }
    g_stride_buf_stride = 0;
    g_stride_buf_count = 0;
    g_stride_buf_pad = 0;

    if (g_renderer.c3d_inited) {
        C3D_Fini();
        g_renderer.c3d_inited = 0;
    }

    g_renderer.ready = 0;
    g_renderer.vertex_count = 0;
    g_renderer.u_loc_projection = -1;
    g_renderer.u_loc_modelview = -1;
    g_renderer.u_loc_clipfix = -1;
    g_renderer.layout_mode = LAYOUT_NORMAL;
    g_renderer.layout_no_pad = 0;
    g_renderer.pending_mode = RENDER_MODE_DEBUG;
    g_renderer.active_mode = RENDER_MODE_DEBUG;
}

int renderer_init(void) {
    C3D_TexEnv *tex_env;
    int result = TWLHD_RENDER_OK;
    int i;

    if (g_renderer.ready) return TWLHD_RENDER_OK;

    memset(&g_renderer, 0, sizeof g_renderer);
    g_renderer.u_loc_projection = -1;
    g_renderer.u_loc_modelview = -1;
    g_renderer.u_loc_clipfix = -1;
    /* Refresh the debug flags after the RendererState wipe. This also keeps
     * the debug-flag API reachable in the normal (non-diag) ELF so that nm
     * reports it; the call is a no-op when the flags are zero. */
    renderer_set_debug_flags(g_debug_flags);
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

    renderer_trace("clip DVLB_ParseFile");
    g_renderer.clip_dvlb = DVLB_ParseFile(
        (u32 *)(void *)twlhd_clip_vshader_shbin,
        (u32)twlhd_clip_vshader_shbin_size);
    if (!g_renderer.clip_dvlb) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }

    renderer_trace("clip shaderProgramInit");
    if (R_FAILED(shaderProgramInit(&g_renderer.clip_program))) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }
    g_renderer.clip_program_inited = 1;

    renderer_trace("clip SetVsh");
    if (R_FAILED(shaderProgramSetVsh(&g_renderer.clip_program,
                                     &g_renderer.clip_dvlb->DVLE[0]))) {
        result = TWLHD_RENDER_ERR_SHADER;
        goto fail;
    }

    renderer_trace("clip uniform loc");
    g_renderer.u_loc_clipfix = shaderInstanceGetUniformLocation(
        g_renderer.clip_program.vertexShader, "clipfix");
    if (g_renderer.u_loc_clipfix < 0) {
        result = TWLHD_RENDER_ERR_UNIFORM;
        goto fail;
    }

    renderer_trace("attr");
    g_renderer.layout_mode = compute_target_layout();
    apply_layout(g_renderer.layout_mode, 0);

    if (g_debug_flags & TWLHD_RDBG_ORTHO) {
        renderer_trace("OrthoTilt");
        Mtx_OrthoTilt(&g_renderer.projection, -2.0f, 2.0f,
                      -1.5f, 1.5f, 0.0f, 10.0f, true);
        Mtx_Identity(&g_renderer.modelview);
        Mtx_Identity(&g_renderer.pending_modelview);
    } else {
        renderer_trace("PerspTilt");
        Mtx_PerspTilt(&g_renderer.projection, C3D_AngleFromDegrees(
                          RENDER_FOV_DEGREES), C3D_AspectRatioTop,
                      0.01f, 1000.0f, false);
    }

    if (!(g_debug_flags & TWLHD_RDBG_NO_STATE)) {
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
    }

    g_renderer.ready = 1;

    renderer_trace("builtin");
    if (renderer_use_builtin_triangle() != TWLHD_RENDER_OK) {
        result = TWLHD_RENDER_ERR_GEOMETRY_ALLOC;
        goto fail;
    }

    if (g_debug_flags & TWLHD_RDBG_ORTHO) {
        Mtx_Identity(&g_renderer.modelview);
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
    g_renderer.pending_mode = RENDER_MODE_DEBUG;

    if (vertex_count != 0 && !(g_debug_flags & TWLHD_RDBG_ORTHO)) {
        update_camera(vertices, vertex_count);
    }
    return TWLHD_RENDER_OK;
}

int renderer_use_builtin_triangle(void) {
    return renderer_set_geometry(g_builtin_triangle, 3);
}

int renderer_set_clip_scene(const TwlhdGxVertex *vertices,
                            uint32_t vertex_count,
                            const TwlhdGxViewport *viewport) {
    size_t bytes;

    if (!g_renderer.ready) return TWLHD_RENDER_ERR_NOT_READY;
    if (!vertices && vertex_count != 0) return TWLHD_RENDER_ERR_NULL;

    bytes = (size_t)vertex_count * sizeof(TwlhdGxVertex);

    /* Same deferred-upload contract as renderer_set_geometry(): the active
     * buffer may still be read by the in-flight frame. */
    if (twlhd_geometry_slots_stage(&g_renderer.slots, vertices, bytes) != 0) {
        return TWLHD_RENDER_ERR_GEOMETRY_ALLOC;
    }

    g_renderer.vertex_count = vertex_count;
    g_renderer.pending_mode = RENDER_MODE_CLIP;
    twlhd_clipfix_matrix(viewport, g_renderer.clipfix_pending);
    return TWLHD_RENDER_OK;
}

/* Existing DEBUG render path. Kept as the cut-pasted original body except
 * for the hoisted commit: commit is now done once in renderer_draw(). */
static void draw_debug_internal(int committed) {
    LayoutMode target;
    int target_no_pad;

    /* By documented precondition this runs between a successful
     * C3D_FrameBegin and C3D_FrameEnd. FrameBegin drains the previous frame's
     * queue, so the old active buffer is no longer read by the GPU and can be
     * freed here. See upstream citro3d renderqueue.c/base.c, read from the
     * devkitPro/citro3d repository; HARDWARE UNVERIFIED. */

    target = compute_target_layout();
    target_no_pad = (g_debug_flags & TWLHD_RDBG_NO_PAD) ? 1 : 0;
    if (target == LAYOUT_STRIDE) {
        renderer_trace("RDBG stride layout");
        ensure_stride_buf();
        setup_stride_attr();
        g_renderer.layout_mode = target;
        g_renderer.layout_no_pad = target_no_pad;
    } else if (target != g_renderer.layout_mode ||
               target_no_pad != g_renderer.layout_no_pad) {
        apply_layout(target, 1);
        g_renderer.layout_mode = target;
        g_renderer.layout_no_pad = target_no_pad;
    }

    if (target == LAYOUT_NORMAL) {
        if (committed) {
            C3D_BufInfo *pending_buf_info;

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
            add_bufinfo_padded(g_renderer.slots.active, sizeof(TwlhdVertex),
                               2, 0x10, 28);

            g_renderer.modelview = g_renderer.pending_modelview;
            if (g_debug_flags & TWLHD_RDBG_ORTHO) {
                Mtx_Identity(&g_renderer.modelview);
            }
        }
    } else if (layout_buffer_ready(target)) {
        renderer_trace("commit/BufInfo");
        if (g_debug_flags & TWLHD_RDBG_ORTHO) {
            Mtx_Identity(&g_renderer.modelview);
        } else {
            g_renderer.modelview = g_renderer.pending_modelview;
        }
    }

    if (!(g_debug_flags & TWLHD_RDBG_SKIP_CLEAR)) {
        renderer_trace("RenderTargetClear");
        C3D_RenderTargetClear(g_renderer.target, C3D_CLEAR_ALL,
                              RENDER_CLEAR_COLOR, 0);
    }
    renderer_trace("FrameDrawOn");
    C3D_FrameDrawOn(g_renderer.target);

    if (!(g_debug_flags & TWLHD_RDBG_NO_BIND)) {
        renderer_trace("BindProgram");
        C3D_BindProgram(&g_renderer.program);
    }
    if (!(g_debug_flags & TWLHD_RDBG_NO_UNIFORMS)) {
        renderer_trace("FVUnif");
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_projection,
                         &g_renderer.projection);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_modelview,
                         &g_renderer.modelview);
    }

    if (!(g_debug_flags & TWLHD_RDBG_SKIP_DRAW)) {
        renderer_trace("DrawArrays");
        if (target == LAYOUT_NORMAL) {
            if (g_renderer.slots.active && g_renderer.vertex_count != 0) {
                C3D_DrawArrays(GPU_TRIANGLES, 0, (int)g_renderer.vertex_count);
            }
        } else if (layout_buffer_ready(target)) {
            uint32_t layout_count;
            if (target == LAYOUT_STRIDE) {
                layout_count = g_debug_vertex_count;
            } else {
                /* Other layout-mode buffers always hold exactly 3 built-in
                 * vertices. */
                layout_count = g_renderer.vertex_count;
                if (layout_count > 3) layout_count = 3;
            }
            if (layout_count != 0) {
                C3D_DrawArrays(GPU_TRIANGLES, 0, (int)layout_count);
            }
        }
    }
}

/* Clip-space render path. RDBG flags are deliberately ignored here: they only
 * describe the DEBUG view, and letting them alter this path would blur the two
 * modes' semantics. AttrInfo/BufInfo/program/uniform are rebuilt on every clip
 * draw so no DEBUG state can leak into a clip frame. */
static void draw_clip_internal(int committed) {
    C3D_AttrInfo *attr_info;
    C3D_BufInfo *buf_info;
    C3D_Mtx m4;
    uint64_t perm = 0x10;
    uint32_t components = 2;
    int i;

    if (twlhd_attr_pad_perm(32, 32, 2, 0x10u, &perm, &components) != 0 ||
        components != 2 || perm != 0x10u) {
        renderer_trace("clip pad fail");
        return; /* refuse to draw with an unchecked layout this frame */
    }

    if (committed) {
        GSPGPU_FlushDataCache(g_renderer.slots.active,
                              (u32)g_renderer.slots.active_size);
        memcpy(g_renderer.clipfix_active, g_renderer.clipfix_pending,
               sizeof g_renderer.clipfix_active);
    }

    attr_info = C3D_GetAttrInfo();
    AttrInfo_Init(attr_info);
    AttrInfo_AddLoader(attr_info, 0, GPU_FLOAT, 4); /* v0 = clip xyzw */
    AttrInfo_AddLoader(attr_info, 1, GPU_FLOAT, 4); /* v1 = rgba      */

    buf_info = C3D_GetBufInfo();
    BufInfo_Init(buf_info);
    BufInfo_Add(buf_info, g_renderer.slots.active, 32, (s8)components, perm);

    renderer_trace("RenderTargetClear");
    C3D_RenderTargetClear(g_renderer.target, C3D_CLEAR_ALL,
                          RENDER_CLEAR_COLOR, 0);

    C3D_FrameDrawOn(g_renderer.target);

    /* HARD RULE: set render state explicitly on every clip frame;
     * never rely on the DEBUG path having run first. */
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);

    /* TexEnv slot 0: pass primary vertex colour through unchanged (same as DEBUG path). */
    {
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    }

    renderer_trace("clip program");
    C3D_BindProgram(&g_renderer.clip_program);

    renderer_trace("clip uniform");
    /* C3D_Mtx rows are stored w,z,y,x; copy the row-major float[4][4] by name. */
    for (i = 0; i < 4; i++) {
        m4.r[i].x = g_renderer.clipfix_active[i][0];
        m4.r[i].y = g_renderer.clipfix_active[i][1];
        m4.r[i].z = g_renderer.clipfix_active[i][2];
        m4.r[i].w = g_renderer.clipfix_active[i][3];
    }
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_renderer.u_loc_clipfix, &m4);

    renderer_trace("clip DrawArrays");
    if (g_renderer.slots.active && g_renderer.vertex_count) {
        C3D_DrawArrays(GPU_TRIANGLES, 0, (int)g_renderer.vertex_count);
    }

    /* This path clobbered the DEBUG AttrInfo/BufInfo; force the next DEBUG
     * draw to rebuild them through apply_layout(). */
    g_renderer.layout_mode = -1;
    g_renderer.layout_no_pad = -1;
}

void renderer_draw(void) {
    int committed = 0;

    if (!g_renderer.ready) return;

    /* The active buffer and active_mode always change together — commit() is
     * the ONLY place a pending buffer is promoted and the ONLY place
     * active_mode is written, right beside it. Staging sets pending_mode only
     * after the stage succeeded, so active_mode always describes the format of
     * slots.active (DEBUG: TwlhdVertex = 48 B/vertex, CLIP: TwlhdGxVertex =
     * 32 B/vertex); a failed set_geometry can never make the next draw run the
     * 48 B DEBUG layout on the 32 B clip buffer. */
    if (g_renderer.slots.pending) {
        renderer_trace(g_renderer.pending_mode == RENDER_MODE_CLIP
                           ? "clip commit/BufInfo" : "commit/BufInfo");
        twlhd_geometry_slots_commit(&g_renderer.slots);
        g_renderer.active_mode = g_renderer.pending_mode;
        committed = 1;
    }

    if (g_renderer.active_mode == RENDER_MODE_CLIP) {
        draw_clip_internal(committed);
        return;
    }
    draw_debug_internal(committed);
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
