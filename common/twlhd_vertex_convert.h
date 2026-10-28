// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - TwlhdFrame to PICA200 vertex converter.
 *
 * HARDWARE UNVERIFIED: host-tested only.
 */
#pragma once

#include <stdint.h>

#include "twlhd_frame_parse.h"

/* 12 floats: pos xyz, rgba, normal xyz, uv. Matches the renderer's 48-byte
 * vertex stride. */
typedef struct {
    float pos[3];
    float rgba[4];
    float normal[3];
    float uv[2];
} TwlhdVertex;

typedef struct {
    uint32_t input_vertices;        /* VTX_16 commands seen, inside or not */
    uint32_t triangles;             /* emitted triangles */
    uint32_t ignored_outside_begin; /* VTX_16 outside BEGIN_VTXS..END_VTXS */
    uint32_t unsupported_matrix;    /* MTX_* commands */
    uint32_t unsupported_viewport;  /* VIEWPORT commands */
    uint32_t unsupported_vertex;    /* VTX_10/XY/XZ/YZ commands */
    uint32_t unsupported_unknown;   /* any other type, incl. invalid BEGIN mode */
    uint32_t dropped_incomplete;    /* primitive groups with leftover vertices */
} TwlhdConvertStats;

typedef enum {
    TWLHD_CONVERT_OK = 0,
    TWLHD_CONVERT_ERR_NULL,
    TWLHD_CONVERT_ERR_CAPACITY,
} TwlhdConvertResult;

/* Conservative worst case: every command after BEGIN_VTXS emits a triangle. */
uint32_t twlhd_convert_vertex_bound(uint32_t cmd_count);

/* Converts a parsed frame into a non-indexed triangle list. `out_vertices`
 * must hold at least `capacity_vertices` entries; no allocation is performed
 * and the function never writes past that capacity. On ERR_CAPACITY the
 * partial output and stats are still written for diagnostics. */
TwlhdConvertResult twlhd_convert_frame(const TwlhdParsedFrame *frame,
                                       TwlhdVertex *out_vertices,
                                       uint32_t capacity_vertices,
                                       uint32_t *out_vertex_count,
                                       TwlhdConvertStats *out_stats);

/* Axis-aligned bounds over the first `count` positions. Returns 0 for a
 * NULL/empty input or when every coordinate on an axis is NaN. Degenerate
 * bounds (min == max) are valid. */
int twlhd_vertex_bounds(const TwlhdVertex *vertices, uint32_t count,
                        float out_min[3], float out_max[3]);
