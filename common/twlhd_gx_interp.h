// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A-1 - DS GX matrix/vertex interpreter.
 *
 * Converts a parsed v2 GXCM stream into a triangle list in DS clip space.
 * Allocation-free, pure C11, no libc beyond <stdint.h>, <stddef.h> and
 * <string.h>.
 *
 * HARDWARE UNVERIFIED: host-tested only.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "twlhd_v2_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 32 bytes: clip-space position plus latched RGBA. */
typedef struct {
    float clip[4]; /* x, y, z, w in DS clip space (no divide, no clip). */
    float rgba[4]; /* r, g, b, a in 0..1 (alpha from POLYGON_ATTR, see stats). */
} TwlhdGxVertex;

/* 16 bytes: texture state at the VTX command that produced the vertex. */
typedef struct {
    float s, t;              /* s1.11.4 latched (and transformed per mode). */
    uint32_t teximage_param; /* TEXIMAGE_PARAM current at VTX (0 = untextured). */
    uint32_t pltt_base;      /* PLTT_BASE current at VTX. */
} TwlhdGxTexVertex;

typedef struct {
    uint8_t x1, y1, x2, y2;
    int present; /* non-zero after a VIEWPORT command is seen. */
} TwlhdGxViewport;

typedef struct {
    uint32_t commands;            /* command groups processed. */
    uint32_t vertices_in;         /* vertex commands emitted. */
    uint32_t triangles_out;       /* triangles written to out[]. */
    uint32_t quads_split;         /* quads converted to triangle pairs. */
    uint32_t dropped_incomplete;  /* vertices left over at END/BEGIN. */
    uint32_t ignored_outside_begin; /* vertex commands outside BEGIN..END. */
    uint32_t ignored_no_effect;   /* commands with no visible effect here. */
    uint32_t unsupported;         /* anything deliberately skipped (none since VTX_DIFF is implemented). */
    uint32_t raw_fifo_skipped;    /* RAW_FIFO opaque entries. */
    uint32_t stack_errors;        /* stack overflow/underflow attempts. */
    uint32_t capacity_dropped;    /* vertices dropped by out[] overflow. */
    uint32_t geometry_incomplete; /* reserved; always 0 since VTX_DIFF is
                                       implemented. */
    uint32_t alpha_translucent_polys; /* BEGIN groups with applied raw alpha
                                         1..30 (one per group, not per poly). */
    uint32_t alpha_wireframe_polys;   /* BEGIN groups with applied raw alpha 0
                                         (outline-only drawing NOT implemented). */
    uint32_t tex_transform_unsupported; /* always 0: modes 2/3 are implemented
                                           (kept for CLI/output compatibility). */
    uint32_t culled_polygons;     /* polygons dropped by the POLYGON_ATTR
                                     culling rule (TWLHD_GX_OPT_CULL only). */
    uint32_t cull_undecided;      /* polygons with w <= 0 on some vertex whose
                                     facing was not decided (drawn). */
} TwlhdGxStats;

typedef enum {
    TWLHD_GX_OK = 0,
    TWLHD_GX_ERR_NULL,
    TWLHD_GX_ERR_CAPACITY,
    TWLHD_GX_ERR_STACK,
    TWLHD_GX_ERR_STREAM
} TwlhdGxResult;

/*
 * Interpret a validated v2 GXCM stream.
 *
 * out: caller-owned array of at least out_capacity_vertices TwlhdGxVertex.
 * out_vertex_count: receives the number of vertices written (always a multiple
 *   of 3). On error it reflects the partial output already written.
 * viewport: receives the last VIEWPORT command, or zeroes if none.
 * stats: receives counters; all fields are zeroed before interpretation.
 *
 * Returns TWLHD_GX_OK on success. On capacity overflow partial output is kept
 * and TWLHD_GX_ERR_CAPACITY is returned. Stack overflow/underflow stops
 * interpretation and returns TWLHD_GX_ERR_STACK (the DS wraps/sets flags; this
 * host interpreter deliberately deviates and reports the error).
 */
TwlhdGxResult twlhd_gx_interpret(const TwlhdV2View *view,
                                 TwlhdGxVertex *out,
                                 uint32_t out_capacity_vertices,
                                 uint32_t *out_vertex_count,
                                 TwlhdGxViewport *viewport,
                                 TwlhdGxStats *stats);

/*
 * Same as twlhd_gx_interpret(), plus an optional parallel texture array.
 *
 * tex_out: NULL behaves exactly like twlhd_gx_interpret(). Otherwise it must
 *   have at least out_capacity_vertices entries; tex_out[i] describes out[i]
 *   (same ordering, quad splitting and capacity truncation). Texture data is
 *   latched at each VTX command; TEXIMAGE_PARAM/PLTT_BASE latch immediately.
 */
TwlhdGxResult twlhd_gx_interpret_ex(const TwlhdV2View *view,
                                    TwlhdGxVertex *out,
                                    uint32_t out_capacity_vertices,
                                    uint32_t *out_vertex_count,
                                    TwlhdGxViewport *viewport,
                                    TwlhdGxStats *stats,
                                    TwlhdGxTexVertex *tex_out);

/* Option flags for twlhd_gx_interpret_opts(). */
#define TWLHD_GX_OPT_CULL 0x1u
#define TWLHD_GX_OPT_DS_STACK 0x2u

/*
 * Same as twlhd_gx_interpret_ex(), with option flags.
 *
 * TWLHD_GX_OPT_CULL: apply the POLYGON_ATTR bits 6-7 culling rule measured on
 *   real DS hardware (docs/reports/eq4zl-zm-zn-probes-hw-result.md section 4):
 *   counter-clockwise on screen is the front face; value 3 draws both faces,
 *   value 2 only the front, value 1 only the back, value 0 neither. The facing
 *   is decided from the first three vertices of a polygon in the order
 *   BEGIN_VTXS assembles them, after the projection divide; polygons with a
 *   vertex at w <= 0 are drawn (not measured) and counted in cull_undecided.
 *   Without the flag every polygon is emitted as before.
 *
 * TWLHD_GX_OPT_DS_STACK: model the matrix stacks as DeSmuME does
 *   (gfx3d.cpp:831-1012): a six-bit wrapping stack index, MTX_POP loads the
 *   matrix at the resulting slot (also for a pop of 0), STORE/RESTORE index
 *   modulo 32, no error return. Needed for captures of retail software,
 *   which use these idioms; without the flag the stricter model (stack
 *   errors) is unchanged.
 */
TwlhdGxResult twlhd_gx_interpret_opts(const TwlhdV2View *view,
                                      TwlhdGxVertex *out,
                                      uint32_t out_capacity_vertices,
                                      uint32_t *out_vertex_count,
                                      TwlhdGxViewport *viewport,
                                      TwlhdGxStats *stats,
                                      TwlhdGxTexVertex *tex_out,
                                      uint32_t flags);

const char *twlhd_gx_result_string(TwlhdGxResult result);

#ifdef __cplusplus
}
#endif
