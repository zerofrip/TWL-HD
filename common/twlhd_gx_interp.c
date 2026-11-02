// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A-1 - DS GX matrix/vertex interpreter.
 *
 * See docs/PHASE5A_V2_RENDERER_DESIGN.md and docs/TWLHD_V2_FORMAT.md.
 * HARDWARE UNVERIFIED: host-tested only.
 */

#include "twlhd_gx_interp.h"

#include <string.h>

/* Fixed-point scale: matrices are 20.12 (raw word = value * 4096). */
#define FX_SHIFT 12
#define FX_ONE   (1 << FX_SHIFT)

/* Per-mode stack capacities (entries). */
#define PROJ_STACK_CAP 1u
#define POS_STACK_CAP  31u

/* Parameter buffer. The largest GX command (LOAD_4x4 / MULT_4x4) has 16 words. */
#define PARAM_BUF_SIZE 16u

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
    uint32_t out_capacity;
    uint32_t out_count;
    TwlhdGxViewport *viewport;
    TwlhdGxStats *stats;

    GxMatrix proj;
    GxMatrix pos;
    GxStack proj_stack;
    GxStack pos_stack;
    GxMatrix clip;          /* proj * pos, recomputed after matrix changes. */
    int clip_dirty;

    uint32_t mode;          /* 0=projection, 1/2=position, 3=texture (ignored). */
    GxVertexPos cur;        /* last emitted/updated vertex position. */
    float rgba[4];          /* latched colour. */

    /* Primitive assembly state. */
    int in_begin;
    uint32_t begin_mode;
    TwlhdGxVertex prim_buf[4]; /* max vertices needed for a quad. */
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

static void matrix_mult_4x3(GxMatrix *current, const uint32_t *params) {
    GxMatrix op;

    matrix_load_4x3(&op, params);
    matrix_multiply(current, &op);
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

static void matrix_mult_3x3(GxMatrix *current, const uint32_t *params) {
    GxMatrix op;

    matrix_load_3x3(&op, params);
    matrix_multiply(current, &op);
}

static void matrix_scale(GxMatrix *current, const uint32_t *params) {
    GxMatrix op;

    matrix_identity(&op);
    op.m[0] = (int32_t)params[0];
    op.m[4 + 1] = (int32_t)params[1];
    op.m[8 + 2] = (int32_t)params[2];
    matrix_multiply(current, &op);
}

static void matrix_translate(GxMatrix *current, const uint32_t *params) {
    GxMatrix op;

    matrix_identity(&op);
    op.m[12 + 0] = (int32_t)params[0];
    op.m[12 + 1] = (int32_t)params[1];
    op.m[12 + 2] = (int32_t)params[2];
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
    return &g->pos_stack;
}

static GxMatrix *current_matrix(GxInterp *g) {
    if (g->mode == 0u) return &g->proj;
    return &g->pos;
}

static int stack_push(GxInterp *g) {
    GxStack *s = current_stack(g);

    if (s->sp >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(&s->stack[s->sp], current_matrix(g));
    s->sp++;
    return 1;
}

static int stack_pop(GxInterp *g, int32_t n) {
    GxStack *s = current_stack(g);
    uint32_t new_sp;

    if (g->mode == 0u) {
        /* Projection stack is one entry deep; the parameter is ignored and
         * POP always restores slot 0. */
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
    s->sp = new_sp;
    return 1;
}

static int stack_store(GxInterp *g, uint32_t index) {
    GxStack *s = current_stack(g);

    if (g->mode == 0u) {
        /* Projection stack only has slot 0; the index parameter is ignored. */
        matrix_copy(&s->stack[0], current_matrix(g));
        return 1;
    }

    if (index >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(&s->stack[index], current_matrix(g));
    return 1;
}

static int stack_restore(GxInterp *g, uint32_t index) {
    GxStack *s = current_stack(g);

    if (g->mode == 0u) {
        /* Projection stack only has slot 0; the index parameter is ignored. */
        matrix_copy(current_matrix(g), &s->stack[0]);
        return 1;
    }

    if (index >= s->capacity) {
        g->stats->stack_errors++;
        return 0;
    }
    matrix_copy(current_matrix(g), &s->stack[index]);
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

static void set_rgba_from_rgb555(GxInterp *g, uint32_t value) {
    g->rgba[0] = (float)(value & 0x1Fu) / 31.0f;
    g->rgba[1] = (float)((value >> 5) & 0x1Fu) / 31.0f;
    g->rgba[2] = (float)((value >> 10) & 0x1Fu) / 31.0f;
    g->rgba[3] = 1.0f;
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
                         const TwlhdGxVertex *b, const TwlhdGxVertex *c) {
    if (g->out_capacity - g->out_count < 3u) {
        g->stats->capacity_dropped += 3u;
        return 0;
    }
    g->out[g->out_count++] = *a;
    g->out[g->out_count++] = *b;
    g->out[g->out_count++] = *c;
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

static int add_primitive_vertex(GxInterp *g, const TwlhdGxVertex *v) {
    uint32_t mode = g->begin_mode;

    if (mode == 0u) { /* triangle list */
        g->prim_buf[g->prim_count++] = *v;
        if (g->prim_count == 3u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[2]))
                return 0;
            g->prim_emitted = 1;
            g->prim_count = 0u;
        }
        return 1;
    }

    if (mode == 1u) { /* quad list */
        g->prim_buf[g->prim_count++] = *v;
        if (g->prim_count == 4u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[2]))
                return 0;
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[2],
                               &g->prim_buf[3]))
                return 0;
            g->stats->quads_split++;
            g->prim_emitted = 1;
            g->prim_count = 0u;
        }
        return 1;
    }

    if (mode == 2u) { /* triangle strip */
        g->prim_buf[g->prim_count++] = *v;
        if (g->prim_count == 3u) {
            if ((g->prim_tri_index & 1u) == 0u) {
                if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                                   &g->prim_buf[2]))
                    return 0;
            } else {
                if (!emit_triangle(g, &g->prim_buf[1], &g->prim_buf[0],
                                   &g->prim_buf[2]))
                    return 0;
            }
            g->prim_emitted = 1;
            g->prim_tri_index++;
            /* Roll the buffer: keep the last two vertices. */
            g->prim_buf[0] = g->prim_buf[1];
            g->prim_buf[1] = g->prim_buf[2];
            g->prim_count = 2u;
        }
        return 1;
    }

    if (mode == 3u) { /* quad strip */
        g->prim_buf[g->prim_count++] = *v;
        if (g->prim_count == 4u) {
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[1],
                               &g->prim_buf[3]))
                return 0;
            if (!emit_triangle(g, &g->prim_buf[0], &g->prim_buf[3],
                               &g->prim_buf[2]))
                return 0;
            g->stats->quads_split++;
            g->prim_emitted = 1;
            /* Roll the buffer: keep the last two vertices. */
            g->prim_buf[0] = g->prim_buf[2];
            g->prim_buf[1] = g->prim_buf[3];
            g->prim_count = 2u;
        }
        return 1;
    }

    return 1;
}

static TwlhdGxResult emit_vertex(GxInterp *g, float x, float y, float z) {
    TwlhdGxVertex v;

    g->stats->vertices_in++;
    g->cur.x = x;
    g->cur.y = y;
    g->cur.z = z;

    if (!g->in_begin) {
        g->stats->ignored_outside_begin++;
        return TWLHD_GX_OK;
    }

    transform_vertex(g, x, y, z, &v);
    if (!add_primitive_vertex(g, &v)) return TWLHD_GX_ERR_CAPACITY;
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
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            if (!stack_push(g)) return TWLHD_GX_ERR_STACK;
        }
        break;

    case TWLHD_V2_CMD_MTX_POP: {
        int32_t n = (int32_t)(params[0] & 0x3Fu);
        if ((params[0] & 0x20u) != 0u) n |= ~0x3F; /* sign-extend 6-bit. */
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            if (!stack_pop(g, n)) return TWLHD_GX_ERR_STACK;
            mark_matrix_dirty(g);
        }
        break;
    }

    case TWLHD_V2_CMD_MTX_STORE:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            if (!stack_store(g, params[0] & 0x1Fu)) return TWLHD_GX_ERR_STACK;
        }
        break;

    case TWLHD_V2_CMD_MTX_RESTORE:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            if (!stack_restore(g, params[0] & 0x1Fu))
                return TWLHD_GX_ERR_STACK;
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_IDENTITY:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_identity(current_matrix(g));
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_LOAD_4x4:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_load_4x4(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_LOAD_4x3:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_load_4x3(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_MULT_4x4:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            GxMatrix op;
            matrix_load_4x4(&op, params);
            matrix_multiply(current_matrix(g), &op);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_MULT_4x3:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_mult_4x3(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_MULT_3x3:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_mult_3x3(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_SCALE:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_scale(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_MTX_TRANS:
        if (g->mode == 3u) {
            g->stats->ignored_no_effect++;
        } else {
            matrix_translate(current_matrix(g), params);
            mark_matrix_dirty(g);
        }
        break;

    case TWLHD_V2_CMD_COLOR:
        set_rgba_from_rgb555(g, params[0]);
        break;

    case TWLHD_V2_CMD_NORMAL:
    case TWLHD_V2_CMD_TEX_COORD:
    case TWLHD_V2_CMD_POLYGON_ATTR:
    case TWLHD_V2_CMD_TEXIMAGE_PARAM:
    case TWLHD_V2_CMD_PLTT_BASE:
    case TWLHD_V2_CMD_DIF_AMB:
    case TWLHD_V2_CMD_SPE_EMI:
    case TWLHD_V2_CMD_LIGHT_VECTOR:
    case TWLHD_V2_CMD_LIGHT_COLOR:
    case TWLHD_V2_CMD_SHININESS:
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
    g.out_capacity = out_capacity_vertices;
    g.out_count = 0u;
    g.viewport = viewport;
    g.stats = stats;

    matrix_identity(&g.proj);
    matrix_identity(&g.pos);
    matrix_identity(&g.clip);
    g.clip_dirty = 1;
    g.mode = 0u;
    g.rgba[0] = g.rgba[1] = g.rgba[2] = 0.0f;
    g.rgba[3] = 1.0f;

    g.proj_stack.capacity = PROJ_STACK_CAP;
    g.pos_stack.capacity = POS_STACK_CAP;

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
            return TWLHD_GX_ERR_STREAM;
        }
        if (param_count > PARAM_BUF_SIZE) {
            return TWLHD_GX_ERR_STREAM;
        }
        entry_count = param_count ? param_count : 1u;

        /* Guard against malformed views that escaped the parser. */
        if (i + entry_count > view->gxcm_count) {
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
