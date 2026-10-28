// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - TwlhdFrame to PICA200 vertex converter.
 *
 * Emulates the DS "current attribute" model (COLOR/NORMAL/TEX_COORD state is
 * latched and copied into every VTX_16) and expands the recorded primitive
 * groups into a plain, non-indexed triangle list.
 *
 * Fixed-point decoding follows GBATEK's DS 3D I/O Ports section and the
 * libnds macros used by arm9/source/fifo_hook.c:
 *   - VTX_16       1.3.12, signed 16-bit, /4096
 *   - COLOR        RGB15, 5 bits per channel, /31
 *   - NORMAL       three signed 10-bit components, /512
 *   - TEX_COORD    two signed 12.4 components, /16
 * The cube's -1.0 normal packs as 0x201 (-511/512), so decoded normals are
 * intentionally not clipped to exactly +-1.0.
 *
 * Quad-strip expansion uses quad k = stream vertices (2k, 2k+1, 2k+3, 2k+2),
 * the GBATEK/OpenGL quad-strip convention; GEOMETRY_FIFO.md does not pin the
 * winding, so docs/PHASE4_RENDERER.md records this check.
 *
 * HARDWARE UNVERIFIED: host-tested only; no allocation, no platform headers.
 */

#include "twlhd_vertex_convert.h"

#include <string.h>

typedef struct {
    float color[4];
    float normal[3];
    float uv[2];
} CurrentAttrib;

static int16_t sign_extend(uint32_t value, unsigned bits) {
    uint32_t sign = 1u << (bits - 1u);
    return (int16_t)((int32_t)((value ^ sign) - sign));
}

static void decode_color(uint32_t packed, float out[4]) {
    out[0] = (float)(packed & 0x1Fu) / 31.0f;
    out[1] = (float)((packed >> 5) & 0x1Fu) / 31.0f;
    out[2] = (float)((packed >> 10) & 0x1Fu) / 31.0f;
    out[3] = 1.0f;
}

static void decode_normal(uint32_t packed, float out[3]) {
    out[0] = (float)sign_extend(packed & 0x3FFu, 10) / 512.0f;
    out[1] = (float)sign_extend((packed >> 10) & 0x3FFu, 10) / 512.0f;
    out[2] = (float)sign_extend((packed >> 20) & 0x3FFu, 10) / 512.0f;
}

static void decode_uv(uint32_t packed, float out[2]) {
    out[0] = (float)sign_extend(packed & 0xFFFFu, 16) / 16.0f;
    out[1] = (float)sign_extend((packed >> 16) & 0xFFFFu, 16) / 16.0f;
}

static void decode_position(uint32_t w0, uint32_t w1, float out[3]) {
    out[0] = (float)sign_extend(w0 & 0xFFFFu, 16) / 4096.0f;
    out[1] = (float)sign_extend((w0 >> 16) & 0xFFFFu, 16) / 4096.0f;
    out[2] = (float)sign_extend(w1 & 0xFFFFu, 16) / 4096.0f;
}

static int known_matrix_type(uint32_t type) {
    return type >= TWLHD_CMD_MTX_MODE && type <= TWLHD_CMD_MTX_MULT_3x3;
}

static int other_vertex_type(uint32_t type) {
    return type == TWLHD_CMD_VTX_10 || type == TWLHD_CMD_VTX_XY ||
           type == TWLHD_CMD_VTX_XZ || type == TWLHD_CMD_VTX_YZ;
}

uint32_t twlhd_convert_vertex_bound(uint32_t cmd_count) {
    /* Every triangle-strip vertex after the first two can emit one triangle
     * (three output vertices). BEGIN_VTXS/END_VTXS only reduce this. */
    if (cmd_count > (UINT32_MAX / 3u)) return UINT32_MAX;
    return cmd_count * 3u;
}

typedef struct {
    TwlhdVertex *out;
    uint32_t capacity;
    uint32_t count;
    TwlhdConvertStats stats;

    /* Per-BEGIN_VTXS state. */
    int in_begin;
    uint32_t mode;
    uint32_t prim_vertices;

    /* Triangle/quad list staging (0..3 entries). */
    TwlhdVertex pending[4];
    uint32_t pending_count;

    /* Triangle strip: previous two vertices. */
    TwlhdVertex strip_prev2;
    TwlhdVertex strip_prev1;
    uint32_t strip_count;

    /* Quad strip: previous column and the pending even vertex. */
    TwlhdVertex quad_a;
    TwlhdVertex quad_b;
    TwlhdVertex quad_even;
    uint32_t quad_count;
    int quad_have_even;
} ConvertState;

static int emit_triangle(ConvertState *st,
                         const TwlhdVertex *a,
                         const TwlhdVertex *b,
                         const TwlhdVertex *c) {
    if (st->count > st->capacity || st->capacity - st->count < 3u) {
        return 0;
    }

    st->out[st->count++] = *a;
    st->out[st->count++] = *b;
    st->out[st->count++] = *c;
    st->stats.triangles++;
    return 1;
}

static void begin_primitive(ConvertState *st, uint32_t mode) {
    st->in_begin = 1;
    st->mode = mode;
    st->prim_vertices = 0;
    st->pending_count = 0;
    st->strip_count = 0;
    st->quad_count = 0;
    st->quad_have_even = 0;
}

static uint32_t dropped_vertices(const ConvertState *st) {
    uint32_t completed_quads;

    switch (st->mode) {
    case 0: /* triangles */
    case 1: /* quads */
        return st->pending_count;
    case 2: /* triangle strip: every vertex from the third forms a triangle */
        return st->strip_count < 3u ? st->strip_count : 0u;
    case 3: /* quad strip: 2 + 2 per quad after the first column */
        if (st->quad_count < 2u) return st->quad_count;
        completed_quads = (st->quad_count - 2u) / 2u;
        return st->quad_count - (2u + 2u * completed_quads);
    default:
        return 0;
    }
}

static void end_primitive(ConvertState *st) {
    if (!st->in_begin) return;

    if (dropped_vertices(st) > 0) st->stats.dropped_incomplete++;
    st->in_begin = 0;
}

static int push_vertex(ConvertState *st, const TwlhdVertex *v) {
    st->prim_vertices++;

    switch (st->mode) {
    case 0: /* triangles */
        st->pending[st->pending_count++] = *v;
        if (st->pending_count == 3) {
            if (!emit_triangle(st, &st->pending[0], &st->pending[1],
                               &st->pending[2])) {
                return 0;
            }
            st->pending_count = 0;
        }
        return 1;

    case 1: /* quads */
        st->pending[st->pending_count++] = *v;
        if (st->pending_count == 4) {
            if (!emit_triangle(st, &st->pending[0], &st->pending[1],
                               &st->pending[2]) ||
                !emit_triangle(st, &st->pending[0], &st->pending[2],
                               &st->pending[3])) {
                return 0;
            }
            st->pending_count = 0;
        }
        return 1;

    case 2: /* triangle strip, alternating winding */
        if (st->strip_count == 0) {
            st->strip_prev2 = *v;
        } else if (st->strip_count == 1) {
            st->strip_prev1 = *v;
        } else {
            uint32_t triangle_index = st->strip_count - 2u;
            int ok;

            if ((triangle_index & 1u) == 0) {
                ok = emit_triangle(st, &st->strip_prev2, &st->strip_prev1, v);
            } else {
                ok = emit_triangle(st, &st->strip_prev1, &st->strip_prev2, v);
            }
            if (!ok) return 0;

            st->strip_prev2 = st->strip_prev1;
            st->strip_prev1 = *v;
        }
        st->strip_count++;
        return 1;

    case 3: /* quad strip: quad k = (2k, 2k+1, 2k+3, 2k+2) */
        if (st->quad_count == 0) {
            st->quad_a = *v;
        } else if (st->quad_count == 1) {
            st->quad_b = *v;
        } else if (!st->quad_have_even) {
            st->quad_even = *v;
            st->quad_have_even = 1;
        } else {
            if (!emit_triangle(st, &st->quad_a, &st->quad_b, v) ||
                !emit_triangle(st, &st->quad_a, v, &st->quad_even)) {
                return 0;
            }
            st->quad_a = st->quad_even;
            st->quad_b = *v;
            st->quad_have_even = 0;
        }
        st->quad_count++;
        return 1;

    default:
        return 1;
    }
}

/* Returns 0 when the output buffer is full. */
static int append_vertex(ConvertState *st, const CurrentAttrib *attrib,
                         uint32_t w0, uint32_t w1) {
    TwlhdVertex v;

    decode_position(w0, w1, v.pos);
    memcpy(v.rgba, attrib->color, sizeof v.rgba);
    memcpy(v.normal, attrib->normal, sizeof v.normal);
    memcpy(v.uv, attrib->uv, sizeof v.uv);

    return push_vertex(st, &v);
}

TwlhdConvertResult twlhd_convert_frame(const TwlhdParsedFrame *frame,
                                       TwlhdVertex *out_vertices,
                                       uint32_t capacity_vertices,
                                       uint32_t *out_vertex_count,
                                       TwlhdConvertStats *out_stats) {
    ConvertState st;
    CurrentAttrib attrib;
    uint32_t i;

    if (!frame || !out_vertices) return TWLHD_CONVERT_ERR_NULL;

    memset(&st, 0, sizeof st);
    st.out = out_vertices;
    st.capacity = capacity_vertices;

    memset(&attrib, 0, sizeof attrib);
    attrib.color[0] = 1.0f;
    attrib.color[1] = 1.0f;
    attrib.color[2] = 1.0f;
    attrib.color[3] = 1.0f;

    for (i = 0; i < frame->cmd_count; i++) {
        const TwlhdParsedCmd *cmd = &frame->cmds[i];

        switch (cmd->type) {
        case TWLHD_CMD_COLOR:
            decode_color(twlhd_cmd_param(cmd, 0), attrib.color);
            break;

        case TWLHD_CMD_NORMAL:
            decode_normal(twlhd_cmd_param(cmd, 0), attrib.normal);
            break;

        case TWLHD_CMD_TEX_COORD:
            decode_uv(twlhd_cmd_param(cmd, 0), attrib.uv);
            break;

        case TWLHD_CMD_VTX_16:
            st.stats.input_vertices++;
            if (!st.in_begin) {
                st.stats.ignored_outside_begin++;
            } else {
                if (!append_vertex(&st, &attrib,
                                   twlhd_cmd_param(cmd, 0),
                                   twlhd_cmd_param(cmd, 1))) {
                    if (out_vertex_count) *out_vertex_count = st.count;
                    if (out_stats) *out_stats = st.stats;
                    return TWLHD_CONVERT_ERR_CAPACITY;
                }
            }
            break;

        case TWLHD_CMD_BEGIN_VTXS: {
            uint32_t mode = twlhd_cmd_param(cmd, 0);

            end_primitive(&st);
            if (mode <= 3u) {
                begin_primitive(&st, mode);
            } else {
                st.stats.unsupported_unknown++;
            }
            break;
        }

        case TWLHD_CMD_END_VTXS:
            end_primitive(&st);
            break;

        case TWLHD_CMD_VIEWPORT:
            st.stats.unsupported_viewport++;
            break;

        default:
            if (known_matrix_type(cmd->type)) {
                st.stats.unsupported_matrix++;
            } else if (other_vertex_type(cmd->type)) {
                st.stats.unsupported_vertex++;
            } else {
                st.stats.unsupported_unknown++;
            }
            break;
        }
    }

    end_primitive(&st);

    if (out_vertex_count) *out_vertex_count = st.count;
    if (out_stats) *out_stats = st.stats;
    return TWLHD_CONVERT_OK;
}

int twlhd_vertex_bounds(const TwlhdVertex *vertices, uint32_t count,
                        float out_min[3], float out_max[3]) {
    uint32_t i;
    int axis;

    if (!vertices || count == 0 || !out_min || !out_max) return 0;

    for (axis = 0; axis < 3; axis++) {
        int have_value = 0;

        for (i = 0; i < count; i++) {
            float value = vertices[i].pos[axis];

            if (value != value) continue; /* skip NaN */
            if (!have_value) {
                out_min[axis] = value;
                out_max[axis] = value;
                have_value = 1;
            } else {
                if (value < out_min[axis]) out_min[axis] = value;
                if (value > out_max[axis]) out_max[axis] = value;
            }
        }

        if (!have_value) return 0;
    }

    return 1;
}
