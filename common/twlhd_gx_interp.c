// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A-1 - DS GX matrix/vertex interpreter.
 *
 * See docs/PHASE5A_V2_RENDERER_DESIGN.md and docs/TWLHD_V2_FORMAT.md.
 * HARDWARE UNVERIFIED: host-tested only.
 */

#include "twlhd_gx_interp.h"

#include <string.h>

/* Host tests include the header from C++, and the libnds shim test compiles
 * this file as C++11; keep the size asserts spellable in both languages. */
#if defined(__cplusplus)
#define TWLHD_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define TWLHD_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

TWLHD_STATIC_ASSERT(sizeof(TwlhdGxVertex) == 32,
                    "TwlhdGxVertex must stay 32 bytes");
TWLHD_STATIC_ASSERT(sizeof(TwlhdGxTexVertex) == 16,
                    "TwlhdGxTexVertex must stay 16 bytes");

/* Fixed-point scale: matrices are 20.12 (raw word = value * 4096). */
#define FX_SHIFT 12
#define FX_ONE   (1 << FX_SHIFT)

/* Per-mode stack capacities (entries). */
#define PROJ_STACK_CAP 1u
#define POS_STACK_CAP  31u
#define TEX_STACK_CAP  1u

/* Parameter buffer. The largest GX command is SHININESS with 32 words. */
#define PARAM_BUF_SIZE 32u

#define LIGHT_COUNT 4u
#define SHININESS_TABLE_SIZE 128u

/* Persistent vertex component storage for VTX_XY/XZ/YZ. */
typedef struct {
    float x, y, z;
} GxVertexPos;

typedef struct {
    int32_t m[16]; /* column-major 4x4, 20.12 fixed point. */
} GxMatrix;

typedef struct {
    GxMatrix stack[31];     /* projection uses only index 0. */
    uint32_t sp;            /* next free slot; 0 means stack empty. */
    uint32_t capacity;      /* PROJ_STACK_CAP or POS_STACK_CAP. */
} GxStack;

typedef struct {
    const TwlhdV2View *view;
    TwlhdGxVertex *out;
    TwlhdGxTexVertex *tex_out; /* optional parallel texture array. */
    uint32_t out_capacity;
    uint32_t out_count;
    TwlhdGxViewport *viewport;
    TwlhdGxStats *stats;

    GxMatrix proj;
    GxMatrix pos;
    GxMatrix vec;           /* directional (lighting) matrix, modes 1/2. */
    GxMatrix tex;           /* texture matrix, mode 3. */
    GxStack proj_stack;
    GxStack pos_stack;
    GxStack vec_stack;      /* parallel to pos_stack, modes 1/2. */
    GxStack tex_stack;      /* one entry, mode 3. */
    GxMatrix clip;          /* proj * pos, recomputed after matrix changes. */
    int clip_dirty;

    uint32_t mode;          /* 0=projection, 1/2=position, 3=texture. */
    GxVertexPos cur;        /* last emitted/updated vertex position. */
    float rgba[4];          /* latched colour; rgba[3] comes from the alpha. */
    uint32_t pending_alpha; /* raw 5-bit POLYGON_ATTR alpha, default 31. */
    uint32_t applied_alpha; /* raw alpha copied from pending at BEGIN_VTXS. */

    /* Texture latch (DeSmuME gfx3d.cpp:1375-1414). */
    int32_t tex_s;          /* raw TEX_COORD low half, s1.11.4. */
    int32_t tex_t;          /* raw TEX_COORD high half, s1.11.4. */
    uint32_t teximage_param; /* raw TEXIMAGE_PARAM, 0 = untextured. */
    uint32_t pltt_base;      /* raw PLTT_BASE. */

    /* Lighting latch (DeSmuME gfx3d.cpp:1158-1331). */
    uint32_t diffuse;       /* RGB555, low 15 bits (DIF_AMB low16). */
    uint32_t ambient;       /* RGB555 (DIF_AMB high16). */
    uint32_t specular;      /* RGB555 + bit15 shininess-table enable. */
    uint32_t emission;      /* RGB555 (SPE_EMI high16). */
    uint32_t light_color[LIGHT_COUNT];      /* raw LIGHT_COLOR words. */
    int32_t light_dir[LIGHT_COUNT][3];      /* transformed, 20.12. */
    int32_t light_half_neg[LIGHT_COUNT][3]; /* -normalized half vector, 20.12. */
    uint8_t shininess_pending[SHININESS_TABLE_SIZE];
    uint8_t shininess_applied[SHININESS_TABLE_SIZE];
    uint32_t shininess_index; /* pending byte index (DeSmuME word counter*4). */
    uint32_t pending_light_mask; /* POLYGON_ATTR bits 0-3. */
    uint32_t applied_light_mask; /* copied from pending at BEGIN_VTXS. */

    /* Primitive assembly state. */
    int in_begin;
    uint32_t begin_mode;
    TwlhdGxVertex prim_buf[4]; /* max vertices needed for a quad. */
    TwlhdGxTexVertex prim_tex_buf[4]; /* parallel to prim_buf. */
    uint32_t prim_count;
    uint32_t prim_tri_index;   /* triangle index inside a strip (for winding). */
    int prim_emitted;          /* non-zero once a primitive has been emitted. */
} GxInterp;

static int32_t fx12_floor_div(uint64_t v) {
    /* The DS uses two's-complement fixed point; accumulate as unsigned to
     * avoid signed overflow UB, then reinterpret and arithmetic-shift down.
     * gcc/arm-none-eabi perform arithmetic right shift for int64_t. */
    return (int32_t)((int64_t)v >> FX_SHIFT);
}

/*
 * DeSmuME saturating 20.12 shiftdown (matrix.cpp:560-575). The clamp is
 * applied to the whole 64-bit sum, not to each partial sum: above
 * 0x7FFFFFFFFFF -> INT32_MAX, below -0x80000000000 -> INT32_MIN, otherwise
 * (s64)acc >> 12.
 */
static int32_t sat_shiftdown_accum(uint64_t acc) {
    int64_t s = (int64_t)acc;

    if (s > (int64_t)0x000007FFFFFFFFFFLL) return INT32_MAX;
    if (s < -0x80000000000LL) return INT32_MIN;
    return (int32_t)(s >> FX_SHIFT);
}

/* fx32_mul then arithmetic >>12 (gfx3d.cpp:476-479). */
static int32_t mul_fixed32(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> FX_SHIFT);
}

/* 3-component dot, no saturation (gfx3d.cpp:481-483). */
static int32_t vec3dot_fixed32(const int32_t a[3], const int32_t b[3]) {
    uint64_t acc = (uint64_t)((int64_t)a[0] * (int64_t)b[0]) +
                   (uint64_t)((int64_t)a[1] * (int64_t)b[1]) +
                   (uint64_t)((int64_t)a[2] * (int64_t)b[2]);
    return (int32_t)((int64_t)acc >> FX_SHIFT);
}

/* 3x3 of a column-major matrix times a 20.12 vector, saturating
 * (matrix.cpp:592-610). */
static void matrix_transform_vec3(const GxMatrix *m, int32_t v[3]) {
    uint32_t row;
    int32_t out[3];

    for (row = 0; row < 3u; row++) {
        uint64_t acc = (uint64_t)((int64_t)m->m[0 * 4 + row] * (int64_t)v[0]) +
                       (uint64_t)((int64_t)m->m[1 * 4 + row] * (int64_t)v[1]) +
                       (uint64_t)((int64_t)m->m[2 * 4 + row] * (int64_t)v[2]);
        out[row] = sat_shiftdown_accum(acc);
    }
    v[0] = out[0];
    v[1] = out[1];
    v[2] = out[2];
}

static void matrix_identity(GxMatrix *d) {
    uint32_t i;

    memset(d, 0, sizeof *d);
    for (i = 0; i < 4u; i++) d->m[i * 4 + i] = FX_ONE;
}

static void matrix_copy(GxMatrix *d, const GxMatrix *s) {
    memcpy(d->m, s->m, sizeof d->m);
}

/*
 * Multiply current = current * operand (OpenGL glMultMatrix column-vector
 * order). Each element is accumulated in 64 bits then shifted right by 12
 * using arithmetic floor division (truncate toward -infinity).
 */
static void matrix_multiply(GxMatrix *current, const GxMatrix *operand) {
    GxMatrix tmp;
    uint32_t col, row, k;

    for (col = 0; col < 4u; col++) {
        for (row = 0; row < 4u; row++) {
            uint64_t acc = 0;
            for (k = 0; k < 4u; k++) {
                acc += (uint64_t)(int64_t)current->m[k * 4 + row] *
                       (uint64_t)(int64_t)operand->m[col * 4 + k];
            }
            tmp.m[col * 4 + row] = fx12_floor_div(acc);
        }
    }
    matrix_copy(current, &tmp);
}

static void matrix_load_4x4(GxMatrix *d, const uint32_t *params) {
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        d->m[i] = (int32_t)params[i];
    }
}

static void matrix_load_4x3(GxMatrix *d, const uint32_t *params) {
    uint32_t col;

    memset(d, 0, sizeof *d);
    for (col = 0; col < 4u; col++) {
        d->m[col * 4 + 0] = (int32_t)params[col * 3 + 0];
        d->m[col * 4 + 1] = (int32_t)params[col * 3 + 1];
        d->m[col * 4 + 2] = (int32_t)params[col * 3 + 2];
    }
    d->m[3 * 4 + 3] = FX_ONE;
}

static void matrix_load_3x3(GxMatrix *d, const uint32_t *params) {
    uint32_t col;

    matrix_identity(d);
    for (col = 0; col < 3u; col++) {
        d->m[col * 4 + 0] = (int32_t)params[col * 3 + 0];
        d->m[col * 4 + 1] = (int32_t)params[col * 3 + 1];
        d->m[col * 4 + 2] = (int32_t)params[col * 3 + 2];
    }
}

static void matrix_scale(GxMatrix *current, const uint32_t *params) {
    GxMatrix op;

    matrix_identity(&op);
    op.m[0] = (int32_t)params[0];
    op.m[4 + 1] = (int32_t)params[1];
    op.m[8 + 2] = (int32_t)params[2];
    matrix_multiply(current, &op);
}

static void recompute_clip(GxInterp *g) {
    uint32_t col, row, k;

    for (col = 0; col < 4u; col++) {
        for (row = 0; row < 4u; row++) {
            uint64_t acc = 0;
            for (k = 0; k < 4u; k++) {
                acc += (uint64_t)(int64_t)g->proj.m[k * 4 + row] *
                       (uint64_t)(int64_t)g->pos.m[col * 4 + k];
            }
            g->clip.m[col * 4 + row] = fx12_floor_div(acc);
        }
    }
    g->clip_dirty = 0;
}

static void mark_matrix_dirty(GxInterp *g) {
    g->clip_dirty = 1;
}

static GxStack *current_stack(GxInterp *g) {
    if (g->mode == 0u) return &g->proj_stack;
    if (g->mode == 3u) return &g->tex_stack;
    return &g->pos_stack;
}

static GxMatrix *current_matrix(GxInterp *g) {
    if (g->mode == 0u) return &g->proj;
    if (g->mode == 3u) return &g->tex;
    return &g->pos;
}

static int stack_push(GxInterp *g) {
    GxStack *s = current_stack(g);

    /* Texture mode uses the one-entry _mtxStackTexture[0]; DeSmuME sets
     * gxstat.se on a second PUSH but keeps going (gfx3d.cpp:860-871). We keep
     * the pre-existing stricter behaviour: count a stack error and abort. */
    if (s->sp >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(&s->stack[s->sp], current_matrix(g));
    if (g->mode == 1u || g->mode == 2u) {
        /* DeSmuME pushes position and vector together (gfx3d.cpp:875-895);
         * texture mode keeps its own stack and must not touch the vector. */
        matrix_copy(&g->vec_stack.stack[g->vec_stack.sp], &g->vec);
        g->vec_stack.sp++;
    }
    s->sp++;
    return 1;
}

static int stack_pop(GxInterp *g, int32_t n) {
    GxStack *s = current_stack(g);
    uint32_t new_sp;

    if (g->mode == 0u || g->mode == 3u) {
        /* Projection and texture stacks are one entry deep; the parameter is
         * ignored and POP always restores slot 0 (gfx3d.cpp:918-929). DeSmuME
         * sets gxstat.se when the toggle wraps; we report a stack error. */
        if (s->sp == 0u) {
            g->stats->stack_errors++;
            return 0;
        }
        matrix_copy(current_matrix(g), &s->stack[0]);
        s->sp = 0u;
        return 1;
    }

    /* POP with n <= 0 in position mode is a deliberate stricter-than-DS
     * error (the real GX FIFO ignores or wraps it). */
    if (n <= 0) {
        g->stats->stack_errors++;
        return 0;
    }
    if ((uint32_t)n > s->sp) {
        g->stats->stack_errors++;
        return 0;
    }
    new_sp = s->sp - (uint32_t)n;
    matrix_copy(current_matrix(g), &s->stack[new_sp]);
    if (g->mode == 1u || g->mode == 2u) {
        matrix_copy(&g->vec, &g->vec_stack.stack[new_sp]);
        g->vec_stack.sp = new_sp;
    }
    s->sp = new_sp;
    return 1;
}

static int stack_store(GxInterp *g, uint32_t index) {
    GxStack *s = current_stack(g);

    if (g->mode == 0u || g->mode == 3u) {
        /* Projection and texture stacks have one slot; the index parameter is
         * ignored (DeSmuME gfx3d.cpp:955-967 — MatrixStore copies
         * _mtxStackTexture[0] with no error in texture mode). */
        matrix_copy(&s->stack[0], current_matrix(g));
        return 1;
    }

    if (index >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(&s->stack[index], current_matrix(g));
    if (g->mode == 1u || g->mode == 2u) {
        matrix_copy(&g->vec_stack.stack[index], &g->vec);
    }
    return 1;
}

static int stack_restore(GxInterp *g, uint32_t index) {
    GxStack *s = current_stack(g);

    if (g->mode == 0u || g->mode == 3u) {
        /* Projection and texture stacks have one slot; the index parameter is
         * ignored (DeSmuME gfx3d.cpp:986-999 — MatrixRestore copies
         * _mtxStackTexture[0] with no error in texture mode). */
        matrix_copy(current_matrix(g), &s->stack[0]);
        return 1;
    }

    if (index >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(current_matrix(g), &s->stack[index]);
    if (g->mode == 1u || g->mode == 2u) {
        matrix_copy(&g->vec, &g->vec_stack.stack[index]);
    }
    return 1;
}

static int16_t sign_extend16(uint32_t v) {
    return (int16_t)(v & 0xFFFFu);
}

static int32_t sign_extend10(uint32_t v) {
    uint32_t w = v & 0x3FFu;
    if (w & 0x200u) w |= 0xFFFFFC00u;
    return (int32_t)w;
}

/*
 * DS raw alpha -> output vertex alpha.
 *
 * TO BE VERIFIED: DeSmuME's blend weight is (alpha+1)/32 (rasterize.cpp:490-494);
 * hardware/GBATEK were not checked, and alpha/31 is the alternative. Alpha 0
 * means wireframe on the DS: it is counted as alpha_wireframe_polys, but
 * outline-only drawing is NOT implemented (the polygon is emitted with 0.0).
 */
static float gx_alpha_float(uint32_t alpha) {
    return alpha == 0u ? 0.0f : (float)(alpha + 1u) / 32.0f;
}

static void set_rgba_from_rgb555(GxInterp *g, uint32_t value) {
    g->rgba[0] = (float)(value & 0x1Fu) / 31.0f;
    g->rgba[1] = (float)((value >> 5) & 0x1Fu) / 31.0f;
    g->rgba[2] = (float)((value >> 10) & 0x1Fu) / 31.0f;
    /* rgba[3] is set from the applied POLYGON_ATTR alpha at BEGIN_VTXS. */
}

static void rgb555_unpack(uint32_t value, int32_t c[3]) {
    c[0] = (int32_t)(value & 0x1Fu);
    c[1] = (int32_t)((value >> 5) & 0x1Fu);
    c[2] = (int32_t)((value >> 10) & 0x1Fu);
}

/*
 * Integer floor(sqrt(v)) for v >= 0. Equivalent to DeSmuME's
 * (s32)sqrt((double)vec3dot_fixed32(...)) because the dot product is an
 * integer and doubles represent it exactly; avoids linking libm on the DS.
 */
static int32_t isqrt32(int32_t v) {
    uint32_t x;
    uint32_t r = 0u;
    uint32_t bit = 1u << 30;

    if (v <= 0) return 0;
    x = (uint32_t)v;
    while (bit > x) bit >>= 2;
    while (bit != 0u) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)r;
}

/*
 * Half vector from the line of sight (0,0,-1), stored negated
 * (DeSmuME gfx3d.cpp:2117-2150). The integer normalize is reproduced
 * verbatim, including the zero-length branch that leaves half unchanged.
 */
static void update_half_vector(const int32_t l[3], int32_t out_neg[3]) {
    int32_t half[3];
    int32_t half_length;
    uint32_t i;

    half[0] = l[0];
    half[1] = l[1];
    half[2] = l[2] - FX_ONE;

    half_length = (int32_t)((uint32_t)isqrt32(
        vec3dot_fixed32(half, half)) << 6);
    if (half_length != 0) {
        if (half_length < 0) half_length = -half_length;
        half_length >>= 6;
        for (i = 0; i < 3u; i++) {
            half[i] = (int32_t)(((int64_t)half[i] * 64) / half_length);
        }
    }
    for (i = 0; i < 3u; i++) out_neg[i] = -half[i];
}

/* Per-NORMAL vertex colour (DeSmuME gfx3d.cpp:1217-1331). */
static void gx_compute_normal_colour(GxInterp *g, uint32_t param) {
    int32_t n[3];
    int32_t diffuse[3];
    int32_t ambient[3];
    int32_t specular[3];
    int64_t vc[3];
    uint32_t i;
    uint32_t c;

    n[0] = sign_extend10(param) * 8;
    n[1] = sign_extend10(param >> 10) * 8;
    n[2] = sign_extend10(param >> 20) * 8;
    matrix_transform_vec3(&g->vec, n);

    rgb555_unpack(g->diffuse, diffuse);
    rgb555_unpack(g->ambient, ambient);
    rgb555_unpack(g->specular, specular);
    vc[0] = (int32_t)(g->emission & 0x1Fu);
    vc[1] = (int32_t)((g->emission >> 5) & 0x1Fu);
    vc[2] = (int32_t)((g->emission >> 10) & 0x1Fu);

    for (i = 0; i < LIGHT_COUNT; i++) {
        int32_t light[3];
        int32_t dot;
        /* int64 avoids signed-overflow UB in the negation and in 2*dot^2.
         * For shininess, the [0,4095] clamp makes the extreme case identical
         * to DeSmuME. For fixed_diffuse it is a deliberate defined-behaviour
         * divergence in the unreachable-in-practice case
         * vec3dot_fixed32 == INT32_MIN (DeSmuME: UB / wraps to 0 after
         * max(0,.); ours: clamps that channel high). */
        int64_t fixed_diffuse;
        int64_t shininess;

        if (((g->applied_light_mask >> i) & 1u) == 0u) continue;

        rgb555_unpack(g->light_color[i], light);

        fixed_diffuse = -(int64_t)vec3dot_fixed32(g->light_dir[i], n);
        if (fixed_diffuse < 0) fixed_diffuse = 0;

        dot = vec3dot_fixed32(g->light_half_neg[i], n);
        shininess = 0;
        if (dot > 0) {
            /* cos(2a) = 2*cos^2(a) - 1 in 20.12. */
            shininess = 2 * (int64_t)mul_fixed32(dot, dot) - FX_ONE;
        }
        if (shininess > 4095) shininess = 4095;
        if (shininess < 0) shininess = 0;
        if ((g->specular & 0x8000u) != 0u) {
            /* 128-entry table, 8-bit entries, back to 20.12. */
            shininess = (int32_t)g->shininess_applied[shininess >> 5] << 4;
        }

        for (c = 0; c < 3u; c++) {
            int64_t spec = ((int64_t)specular[c] * light[c] * shininess) >> 17;
            int64_t diff =
                ((int64_t)diffuse[c] * light[c] * fixed_diffuse) >> 17;
            int64_t amb = ((int64_t)ambient[c] * light[c]) >> 5;
            vc[c] += spec + diff + amb;
        }
    }

    for (c = 0; c < 3u; c++) {
        if (vc[c] > 31) vc[c] = 31;
    }
    set_rgba_from_rgb555(g, (uint32_t)vc[0] |
                               ((uint32_t)vc[1] << 5) |
                               ((uint32_t)vc[2] << 10));
}

static void transform_vertex(GxInterp *g, float x, float y, float z,
                             TwlhdGxVertex *out) {
    uint32_t row;
    double v[4];

    if (g->clip_dirty) recompute_clip(g);

    v[0] = (double)x;
    v[1] = (double)y;
    v[2] = (double)z;
    v[3] = 1.0;

    for (row = 0; row < 4u; row++) {
        double acc = 0.0;
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            acc += (double)g->clip.m[k * 4 + row] * v[k];
        }
        out->clip[row] = (float)(acc / 4096.0);
    }

    out->rgba[0] = g->rgba[0];
    out->rgba[1] = g->rgba[1];
    out->rgba[2] = g->rgba[2];
    out->rgba[3] = g->rgba[3];
}

static int emit_triangle(GxInterp *g, const TwlhdGxVertex *a,
                         const TwlhdGxVertex *b, const TwlhdGxVertex *c,
                         const TwlhdGxTexVertex *ta,
                         const TwlhdGxTexVertex *tb,
                         const TwlhdGxTexVertex *tc) {
    if (g->out_capacity - g->out_count < 3u) {
        g->stats->capacity_dropped += 3u;
        return 0;
    }
    g->out[g->out_count] = *a;
    if (g->tex_out) g->tex_out[g->out_count] = *ta;
    g->out_count++;
    g->out[g->out_count] = *b;
    if (g->tex_out) g->tex_out[g->out_count] = *tb;
    g->out_count++;
    g->out[g->out_count] = *c;
    if (g->tex_out) g->tex_out[g->out_count] = *tc;
    g->out_count++;
    g->stats->triangles_out++;
    return 1;
}

static void flush_primitive(GxInterp *g) {
    uint32_t n = g->prim_count;

    if (n == 0u) return;

    switch (g->begin_mode) {
    case 0u: /* triangle list */
        g->stats->dropped_incomplete += n % 3u;
        break;
    case 1u: /* quad list */
        g->stats->dropped_incomplete += n % 4u;
        break;
    case 2u: /* triangle strip */
    case 3u: /* quad strip */
        /* After a primitive is emitted the last two vertices are kept as the
         * rolling window, so only vertices beyond those two are leftover. */
        if (!g->prim_emitted) {
            g->stats->dropped_incomplete += n;
        } else if (n > 2u) {
            g->stats->dropped_incomplete += n - 2u;
        }
        break;
    }
    g->prim_count = 0u;
    g->prim_tri_index = 0u;
    g->prim_emitted = 0;
}

static int add_primitive_vertex(GxInterp *g, const TwlhdGxVertex *v,
                                const TwlhdGxTexVertex *tv) {
    uint32_t mode = g->begin_mode;

    if (mode == 0u) { /* triangle list */
        g->prim_buf[g->prim_count] = *v;
        g->prim_tex_buf[g->prim_count] = *tv;
        g->prim_count++;
        if (g->prim_count == 3u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[2], &g->prim_tex_buf[0],
                               &g->prim_tex_buf[1], &g->prim_tex_buf[2]))
                return 0;
            g->prim_emitted = 1;
            g->prim_count = 0u;
        }
        return 1;
    }

    if (mode == 1u) { /* quad list */
        g->prim_buf[g->prim_count] = *v;
        g->prim_tex_buf[g->prim_count] = *tv;
        g->prim_count++;
        if (g->prim_count == 4u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[2], &g->prim_tex_buf[0],
                               &g->prim_tex_buf[1], &g->prim_tex_buf[2]))
                return 0;
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[2],
                               &g->prim_buf[3], &g->prim_tex_buf[0],
                               &g->prim_tex_buf[2], &g->prim_tex_buf[3]))
                return 0;
            g->stats->quads_split++;
            g->prim_emitted = 1;
            g->prim_count = 0u;
        }
        return 1;
    }

    if (mode == 2u) { /* triangle strip */
        g->prim_buf[g->prim_count] = *v;
        g->prim_tex_buf[g->prim_count] = *tv;
        g->prim_count++;
        if (g->prim_count == 3u) {
            if ((g->prim_tri_index & 1u) == 0u) {
                if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                                   &g->prim_buf[2], &g->prim_tex_buf[0],
                                   &g->prim_tex_buf[1], &g->prim_tex_buf[2]))
                    return 0;
            } else {
                if (!emit_triangle(g, &g->prim_buf[1], &g->prim_buf[0],
                                   &g->prim_buf[2], &g->prim_tex_buf[1],
                                   &g->prim_tex_buf[0], &g->prim_tex_buf[2]))
                    return 0;
            }
            g->prim_emitted = 1;
            g->prim_tri_index++;
            /* Roll the buffer: keep the last two vertices. */
            g->prim_buf[0] = g->prim_buf[1];
            g->prim_buf[1] = g->prim_buf[2];
            g->prim_tex_buf[0] = g->prim_tex_buf[1];
            g->prim_tex_buf[1] = g->prim_tex_buf[2];
            g->prim_count = 2u;
        }
        return 1;
    }

    if (mode == 3u) { /* quad strip */
        g->prim_buf[g->prim_count] = *v;
        g->prim_tex_buf[g->prim_count] = *tv;
        g->prim_count++;
        if (g->prim_count == 4u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[3], &g->prim_tex_buf[0],
                               &g->prim_tex_buf[1], &g->prim_tex_buf[3]))
                return 0;
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[3],
                               &g->prim_buf[2], &g->prim_tex_buf[0],
                               &g->prim_tex_buf[3], &g->prim_tex_buf[2]))
                return 0;
            g->stats->quads_split++;
            g->prim_emitted = 1;
            /* Roll the buffer: keep the last two vertices. */
            g->prim_buf[0] = g->prim_buf[2];
            g->prim_buf[1] = g->prim_buf[3];
            g->prim_tex_buf[0] = g->prim_tex_buf[2];
            g->prim_tex_buf[1] = g->prim_tex_buf[3];
            g->prim_count = 2u;
        }
        return 1;
    }

    return 1;
}

/*
 * Texture state for the vertex about to be emitted.
 *
 * TEX_COORD is applied at VTX time (DeSmuME AddCurrentVertexToList,
 * gfx3d.cpp:1578-1593 as described in docs/DS_GX_RESEARCH.md). Mode 1 is the
 * texture-matrix rows 0/1 applied to the RAW sign-extended s1.11.4 latches:
 * the m[8]/m[9] and m[12]/m[13] addends are used as-is (no FX_ONE scaling),
 * the 64-bit sum is arithmetic-shifted down by 12, cast to int32 and converted
 * to texture units (/16.0). Modes 2 and 3 are not implemented: raw latched
 * coordinates are emitted and the vertex is counted in
 * tex_transform_unsupported (per VTX command).
 */
static TwlhdGxTexVertex compute_tex_vertex(GxInterp *g) {
    TwlhdGxTexVertex tv;
    uint32_t tmode = (g->teximage_param >> 30) & 0x3u;

    tv.teximage_param = g->teximage_param;
    tv.pltt_base = g->pltt_base;

    if (tmode == 1u) {
        /* DeSmuME gfx3d.cpp:1578-1593 (TexCoordTransformMode 1). Row 0/1 of
         * the 20.12 texture matrix (column-major m[col*4+row], so row 0 =
         * m[0,4,8,12]) times the RAW sign-extended s1.11.4 coords (no <<8
         * promotion), plus the raw m[8]/m[9] and m[12]/m[13] terms (no
         * FX_ONE multiplier), summed in 64 bits, arithmetic >>12, cast to
         * int32, then /16.0f like mode 0. */
        int64_t s_acc = (int64_t)g->tex.m[0] * g->tex_s +
                        (int64_t)g->tex.m[4] * g->tex_t +
                        (int64_t)g->tex.m[8] + (int64_t)g->tex.m[12];
        int64_t t_acc = (int64_t)g->tex.m[1] * g->tex_s +
                        (int64_t)g->tex.m[5] * g->tex_t +
                        (int64_t)g->tex.m[9] + (int64_t)g->tex.m[13];
        tv.s = (float)(int32_t)(s_acc >> FX_SHIFT) / 16.0f;
        tv.t = (float)(int32_t)(t_acc >> FX_SHIFT) / 16.0f;
        return tv;
    }

    if (tmode >= 2u) {
        g->stats->tex_transform_unsupported++;
    }
    tv.s = (float)g->tex_s / 16.0f;
    tv.t = (float)g->tex_t / 16.0f;
    return tv;
}

static TwlhdGxResult emit_vertex(GxInterp *g, float x, float y, float z) {
    TwlhdGxVertex v;
    TwlhdGxTexVertex tv;

    g->stats->vertices_in++;
    tv = compute_tex_vertex(g);
    g->cur.x = x;
    g->cur.y = y;
    g->cur.z = z;

    if (!g->in_begin) {
        g->stats->ignored_outside_begin++;
        return TWLHD_GX_OK;
    }

    transform_vertex(g, x, y, z, &v);
    if (!add_primitive_vertex(g, &v, &tv)) return TWLHD_GX_ERR_CAPACITY;
    return TWLHD_GX_OK;
}

static TwlhdGxResult process_command(GxInterp *g, uint32_t id,
                                     const uint32_t *params) {
    TwlhdGxResult r;

    switch (id) {
    case TWLHD_V2_CMD_MTX_MODE:
        g->mode = params[0] & 0x3u;
        break;

    case TWLHD_V2_CMD_MTX_PUSH:
        if (!stack_push(g)) return TWLHD_GX_ERR_STACK;
        break;

    case TWLHD_V2_CMD_MTX_POP: {
        int32_t n = (int32_t)(params[0] & 0x3Fu);
        if ((params[0] & 0x20u) != 0u) n |= ~0x3F; /* sign-extend 6-bit. */
        if (!stack_pop(g, n)) return TWLHD_GX_ERR_STACK;
        mark_matrix_dirty(g);
        break;
    }

    case TWLHD_V2_CMD_MTX_STORE:
        if (!stack_store(g, params[0] & 0x1Fu)) return TWLHD_GX_ERR_STACK;
        break;

    case TWLHD_V2_CMD_MTX_RESTORE:
        if (!stack_restore(g, params[0] & 0x1Fu))
            return TWLHD_GX_ERR_STACK;
        mark_matrix_dirty(g);
        break;

    case TWLHD_V2_CMD_MTX_IDENTITY:
        matrix_identity(current_matrix(g));
        if (g->mode == 2u) matrix_identity(&g->vec);
        mark_matrix_dirty(g);
        break;

    case TWLHD_V2_CMD_MTX_LOAD_4x4:
        matrix_load_4x4(current_matrix(g), params);
        if (g->mode == 2u) matrix_load_4x4(&g->vec, params);
        mark_matrix_dirty(g);
        break;

    case TWLHD_V2_CMD_MTX_LOAD_4x3:
        matrix_load_4x3(current_matrix(g), params);
        if (g->mode == 2u) matrix_load_4x3(&g->vec, params);
        mark_matrix_dirty(g);
        break;

    case TWLHD_V2_CMD_MTX_MULT_4x4: {
        GxMatrix op;
        matrix_load_4x4(&op, params);
        matrix_multiply(current_matrix(g), &op);
        if (g->mode == 2u) matrix_multiply(&g->vec, &op);
        mark_matrix_dirty(g);
        break;
    }

    case TWLHD_V2_CMD_MTX_MULT_4x3: {
        GxMatrix op;
        matrix_load_4x3(&op, params);
        matrix_multiply(current_matrix(g), &op);
        if (g->mode == 2u) matrix_multiply(&g->vec, &op);
        mark_matrix_dirty(g);
        break;
    }

    case TWLHD_V2_CMD_MTX_MULT_3x3: {
        GxMatrix op;
        matrix_load_3x3(&op, params);
        matrix_multiply(current_matrix(g), &op);
        if (g->mode == 2u) matrix_multiply(&g->vec, &op);
        mark_matrix_dirty(g);
        break;
    }

    case TWLHD_V2_CMD_MTX_SCALE:
        /* Vector matrix is deliberately left unscaled (DeSmuME
         * gfx3d.cpp:1118-1133: it keeps the vector matrix orthogonal). */
        matrix_scale(current_matrix(g), params);
        mark_matrix_dirty(g);
        break;

    case TWLHD_V2_CMD_MTX_TRANS: {
        GxMatrix op;
        matrix_identity(&op);
        op.m[12 + 0] = (int32_t)params[0];
        op.m[12 + 1] = (int32_t)params[1];
        op.m[12 + 2] = (int32_t)params[2];
        matrix_multiply(current_matrix(g), &op);
        if (g->mode == 2u) matrix_multiply(&g->vec, &op);
        mark_matrix_dirty(g);
        break;
    }

    case TWLHD_V2_CMD_COLOR:
        set_rgba_from_rgb555(g, params[0]);
        break;

    case TWLHD_V2_CMD_POLYGON_ATTR:
        /* Latch only; BEGIN_VTXS copies pending -> applied (DeSmuME
         * gfx3d.cpp:2681, 2729-2731). Alpha = bits 16..20, light mask =
         * bits 0..3. */
        g->pending_alpha = (params[0] >> 16) & 0x1Fu;
        g->pending_light_mask = params[0] & 0xFu;
        break;

    case TWLHD_V2_CMD_NORMAL:
        /* Lighting runs at the NORMAL command, even outside BEGIN..END. */
        gx_compute_normal_colour(g, params[0]);
        break;

    case TWLHD_V2_CMD_DIF_AMB:
        g->diffuse = params[0] & 0xFFFFu;
        g->ambient = (params[0] >> 16) & 0xFFFFu;
        if ((params[0] & 0x8000u) != 0u) {
            /* Bit 15 sets the vertex colour to the diffuse RGB555. */
            set_rgba_from_rgb555(g, params[0] & 0x7FFFu);
        }
        break;

    case TWLHD_V2_CMD_SPE_EMI:
        g->specular = params[0] & 0xFFFFu;
        g->emission = (params[0] >> 16) & 0xFFFFu;
        break;

    case TWLHD_V2_CMD_LIGHT_VECTOR: {
        uint32_t index = (params[0] >> 30) & 0x3u;
        int32_t dir[3];

        dir[0] = sign_extend10(params[0]) * 8;
        dir[1] = sign_extend10(params[0] >> 10) * 8;
        dir[2] = sign_extend10(params[0] >> 20) * 8;
        /* Direction is transformed by the vector matrix at command time. */
        matrix_transform_vec3(&g->vec, dir);
        g->light_dir[index][0] = dir[0];
        g->light_dir[index][1] = dir[1];
        g->light_dir[index][2] = dir[2];
        update_half_vector(dir, g->light_half_neg[index]);
        break;
    }

    case TWLHD_V2_CMD_LIGHT_COLOR:
        g->light_color[(params[0] >> 30) & 0x3u] = params[0];
        break;

    case TWLHD_V2_CMD_SHININESS: {
        uint32_t k;
        for (k = 0; k < 32u; k++) {
            uint32_t b;
            for (b = 0; b < 4u; b++) {
                g->shininess_pending[g->shininess_index++] =
                    (uint8_t)((params[k] >> (b * 8u)) & 0xFFu);
            }
            if (g->shininess_index >= SHININESS_TABLE_SIZE) {
                memcpy(g->shininess_applied, g->shininess_pending,
                       SHININESS_TABLE_SIZE);
                g->shininess_index = 0u;
            }
        }
        break;
    }

    case TWLHD_V2_CMD_TEX_COORD:
        /* Latch only; applied per vertex at VTX time (DeSmuME
         * gfx3d.cpp:1397-1414, transform at 1579-1611). */
        g->tex_s = sign_extend16(params[0]);
        g->tex_t = sign_extend16(params[0] >> 16);
        break;

    case TWLHD_V2_CMD_TEXIMAGE_PARAM:
        g->teximage_param = params[0];
        break;

    case TWLHD_V2_CMD_PLTT_BASE:
        g->pltt_base = params[0];
        break;

    case TWLHD_V2_CMD_BOX_TEST:
    case TWLHD_V2_CMD_POS_TEST:
    case TWLHD_V2_CMD_VEC_TEST:
        g->stats->ignored_no_effect++;
        break;

    case TWLHD_V2_CMD_VTX_16:
        r = emit_vertex(g, (float)sign_extend16(params[0]) / 4096.0f,
                        (float)sign_extend16(params[0] >> 16) / 4096.0f,
                        (float)sign_extend16(params[1]) / 4096.0f);
        if (r != TWLHD_GX_OK) return r;
        break;

    case TWLHD_V2_CMD_VTX_10:
        r = emit_vertex(g, (float)sign_extend10(params[0]) / 64.0f,
                        (float)sign_extend10(params[0] >> 10) / 64.0f,
                        (float)sign_extend10(params[0] >> 20) / 64.0f);
        if (r != TWLHD_GX_OK) return r;
        break;

    case TWLHD_V2_CMD_VTX_XY:
        r = emit_vertex(g, (float)sign_extend16(params[0]) / 4096.0f,
                        (float)sign_extend16(params[0] >> 16) / 4096.0f,
                        g->cur.z);
        if (r != TWLHD_GX_OK) return r;
        break;

    case TWLHD_V2_CMD_VTX_XZ:
        r = emit_vertex(g, (float)sign_extend16(params[0]) / 4096.0f,
                        g->cur.y,
                        (float)sign_extend16(params[0] >> 16) / 4096.0f);
        if (r != TWLHD_GX_OK) return r;
        break;

    case TWLHD_V2_CMD_VTX_YZ:
        r = emit_vertex(g, g->cur.x,
                        (float)sign_extend16(params[0]) / 4096.0f,
                        (float)sign_extend16(params[0] >> 16) / 4096.0f);
        if (r != TWLHD_GX_OK) return r;
        break;

    case TWLHD_V2_CMD_VTX_DIFF:
        g->stats->unsupported++;
        if (g->in_begin) {
            /* VTX_DIFF inside a primitive corrupts assembly; callers must know
             * the resulting geometry is incomplete. */
            g->stats->geometry_incomplete++;
        }
        break;

    case TWLHD_V2_CMD_BEGIN_VTXS:
        if (g->in_begin) {
            flush_primitive(g);
        }
        g->in_begin = 1;
        g->begin_mode = params[0] & 0x3u;
        g->prim_count = 0u;
        g->prim_tri_index = 0u;
        g->prim_emitted = 0;
        /* Apply the pending POLYGON_ATTR alpha and light mask once per BEGIN
         * group (DeSmuME gfx3d.cpp:2727-2729). */
        g->applied_alpha = g->pending_alpha;
        g->applied_light_mask = g->pending_light_mask;
        g->rgba[3] = gx_alpha_float(g->applied_alpha);
        if (g->applied_alpha == 0u) {
            g->stats->alpha_wireframe_polys++;
        } else if (g->applied_alpha < 31u) {
            g->stats->alpha_translucent_polys++;
        }
        break;

    case TWLHD_V2_CMD_END_VTXS:
        if (g->in_begin) {
            flush_primitive(g);
        }
        g->in_begin = 0;
        g->prim_count = 0u;
        g->prim_tri_index = 0u;
        break;

    case TWLHD_V2_CMD_SWAP_BUFFERS:
        return TWLHD_GX_OK; /* stop interpreting; caller will see OK. */

    case TWLHD_V2_CMD_VIEWPORT:
        g->viewport->x1 = (uint8_t)(params[0] & 0xFFu);
        g->viewport->y1 = (uint8_t)((params[0] >> 8) & 0xFFu);
        g->viewport->x2 = (uint8_t)((params[0] >> 16) & 0xFFu);
        g->viewport->y2 = (uint8_t)((params[0] >> 24) & 0xFFu);
        g->viewport->present = 1;
        break;

    default:
        return TWLHD_GX_ERR_STREAM;
    }
    return TWLHD_GX_OK;
}

TwlhdGxResult twlhd_gx_interpret(const TwlhdV2View *view, TwlhdGxVertex *out,
                                 uint32_t out_capacity_vertices,
                                 uint32_t *out_vertex_count,
                                 TwlhdGxViewport *viewport,
                                 TwlhdGxStats *stats) {
    return twlhd_gx_interpret_ex(view, out, out_capacity_vertices,
                                 out_vertex_count, viewport, stats, NULL);
}

TwlhdGxResult twlhd_gx_interpret_ex(const TwlhdV2View *view,
                                    TwlhdGxVertex *out,
                                    uint32_t out_capacity_vertices,
                                    uint32_t *out_vertex_count,
                                    TwlhdGxViewport *viewport,
                                    TwlhdGxStats *stats,
                                    TwlhdGxTexVertex *tex_out) {
    GxInterp g;
    uint32_t i = 0u;
    TwlhdGxResult result = TWLHD_GX_OK;

    if (!view || !out || !out_vertex_count || !viewport || !stats) {
        return TWLHD_GX_ERR_NULL;
    }

    memset(&g, 0, sizeof g);
    memset(stats, 0, sizeof *stats);
    memset(viewport, 0, sizeof *viewport);

    g.view = view;
    g.out = out;
    g.tex_out = tex_out;
    g.out_capacity = out_capacity_vertices;
    g.out_count = 0u;
    g.viewport = viewport;
    g.stats = stats;

    matrix_identity(&g.proj);
    matrix_identity(&g.pos);
    matrix_identity(&g.vec);
    matrix_identity(&g.tex);
    matrix_identity(&g.clip);
    g.clip_dirty = 1;
    g.mode = 0u;
    g.rgba[0] = g.rgba[1] = g.rgba[2] = 0.0f;
    g.pending_alpha = 31u; /* no-attr default: opaque. */
    g.applied_alpha = 31u;
    g.rgba[3] = gx_alpha_float(g.applied_alpha);

    g.proj_stack.capacity = PROJ_STACK_CAP;
    g.pos_stack.capacity = POS_STACK_CAP;
    g.vec_stack.capacity = POS_STACK_CAP;
    g.tex_stack.capacity = TEX_STACK_CAP;

    /* DeSmuME starts with zero light directions but a valid (negated) half
     * vector for each of them (gfx3d.cpp:608-621). */
    for (i = 0u; i < LIGHT_COUNT; i++) {
        int32_t zero[3] = {0, 0, 0};
        update_half_vector(zero, g.light_half_neg[i]);
    }
    i = 0u;

    while (i < view->gxcm_count) {
        uint32_t word = twlhd_v2_gxcm_cmd_word(view, i);
        uint32_t id;
        uint8_t param_count;
        uint8_t entry_count;
        uint32_t params[PARAM_BUF_SIZE];
        uint32_t j;

        if (word == TWLHD_V2_RAW_FIFO_BIT) {
            stats->raw_fifo_skipped++;
            i++;
            continue;
        }

        id = word & TWLHD_V2_CMD_ID_MASK;
        if (!twlhd_v2_cmd_param_count(id, &param_count)) {
            *out_vertex_count = g.out_count;
            return TWLHD_GX_ERR_STREAM;
        }
        if (param_count > PARAM_BUF_SIZE) {
            *out_vertex_count = g.out_count;
            return TWLHD_GX_ERR_STREAM;
        }
        entry_count = param_count ? param_count : 1u;

        /* Guard against malformed views that escaped the parser. */
        if (i + entry_count > view->gxcm_count) {
            *out_vertex_count = g.out_count;
            return TWLHD_GX_ERR_STREAM;
        }

        for (j = 0; j < param_count; j++) {
            params[j] = twlhd_v2_gxcm_value(view, i + j);
        }
        i += entry_count;

        result = process_command(&g, id, params);
        if (result == TWLHD_GX_ERR_CAPACITY) {
            *out_vertex_count = g.out_count;
            return TWLHD_GX_ERR_CAPACITY;
        }
        if (result != TWLHD_GX_OK) {
            *out_vertex_count = g.out_count;
            return result;
        }

        /* SWAP_BUFFERS ends the frame cleanly. */
        if (id == TWLHD_V2_CMD_SWAP_BUFFERS) break;

        stats->commands++;
    }

    if (g.in_begin) {
        flush_primitive(&g);
    }

    *out_vertex_count = g.out_count;
    return TWLHD_GX_OK;
}

const char *twlhd_gx_result_string(TwlhdGxResult result) {
    switch (result) {
    case TWLHD_GX_OK:         return "OK";
    case TWLHD_GX_ERR_NULL:   return "ERR_NULL";
    case TWLHD_GX_ERR_CAPACITY: return "ERR_CAPACITY";
    case TWLHD_GX_ERR_STACK:  return "ERR_STACK";
    case TWLHD_GX_ERR_STREAM: return "ERR_STREAM";
    default:                  return "ERR_UNKNOWN";
    }
}
