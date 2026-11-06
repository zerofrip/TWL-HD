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
    uint32_t unsupported;         /* VTX_DIFF and anything deliberately skipped. */
    uint32_t raw_fifo_skipped;    /* RAW_FIFO opaque entries. */
    uint32_t stack_errors;        /* stack overflow/underflow attempts. */
    uint32_t capacity_dropped;    /* vertices dropped by out[] overflow. */
    uint32_t geometry_incomplete; /* VTX_DIFF inside BEGIN..END: output may
                                       miss expected geometry. */
    uint32_t alpha_translucent_polys; /* BEGIN groups with applied raw alpha
                                         1..30 (one per group, not per poly). */
    uint32_t alpha_wireframe_polys;   /* BEGIN groups with applied raw alpha 0
                                         (outline-only drawing NOT implemented). */
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

const char *twlhd_gx_result_string(TwlhdGxResult result);

#ifdef __cplusplus
}
#endif
