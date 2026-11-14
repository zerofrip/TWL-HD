// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2d - host-side DS texture preparation for the PICA200.
 *
 * Pure C11, allocation-free (the caller owns every buffer), no libm. This is
 * BUILD/UNIT-only work: the PICA facts below were read from the Azahar and
 * Panda3DS emulators and the libctru headers (see docs/DS_GX_RESEARCH.md,
 * "PICA texture upload (5B-2d)"); nothing here is hardware-verified.
 *
 * The texture layout produced by twlhd_pica_tex_swizzle_rgba8() assumes the
 * texture is sampled with a separate texcoord attribute buffer: the 32-byte
 * TwlhdGxVertex layout is unchanged and vertex i of the main buffer pairs with
 * texcoord i of the array produced by twlhd_pica_texcoords_from_gx().
 */
#pragma once

#include <stdint.h>

#include "twlhd_gx_interp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TWLHD_PICA_TEX_OK = 0,
    TWLHD_PICA_TEX_ERR_NULL,        /* a required pointer is NULL */
    TWLHD_PICA_TEX_ERR_SIZE,        /* width/height not a power of two in 8..1024 */
    TWLHD_PICA_TEX_ERR_OUTPUT_SIZE, /* dst_size < width*height*4 */
    TWLHD_PICA_TEX_ERR_COUNT,       /* vertex count is not a multiple of 3 */
    TWLHD_PICA_TEX_ERR_CAPACITY     /* caller buffer holds too few entries */
} TwlhdPicaTexResult;

/*
 * PICA200 wrap modes (GPUREG_TEXUNITi_WRAP_FILTER bits 8-10 = T, 12-14 = S).
 * Values verified from Azahar regs_texturing.h:27-31 and libctru
 * 3ds/gpu/enums.h:33-41; also listed by 3DBrew "GPU/Internal_Registers",
 * wrap values 0-3.
 */
typedef enum {
    TWLHD_PICA_WRAP_CLAMP_TO_EDGE = 0,
    TWLHD_PICA_WRAP_CLAMP_TO_BORDER = 1,
    TWLHD_PICA_WRAP_REPEAT = 2,
    TWLHD_PICA_WRAP_MIRRORED_REPEAT = 3
} TwlhdPicaWrap;

/* Tightly packed separate texture-coordinate attribute: 8 bytes per vertex. */
typedef struct {
    float u, v;
} TwlhdPicaTexCoord;

/* One run of consecutive triangles sharing the same texture state. */
typedef struct {
    uint32_t first_vertex;   /* first vertex of the run in the main buffer */
    uint32_t vertex_count;   /* multiple of 3 */
    uint32_t teximage_param; /* DS TEXIMAGE_PARAM of the run */
    uint32_t pltt_base;      /* DS PLTT_BASE of the run */
    int untextured;          /* TEXIMAGE_PARAM PackedFormat bits 26-28 == 0 */
} TwlhdPicaBindPlan;

/*
 * Convert a row-major, top-down RGBA8 image (bytes R,G,B,A) into PICA200
 * RGBA8 texture memory: 8x8 tiles in row-major tile order, Morton (Z) order
 * inside each tile, texel bytes stored A,B,G,R (Azahar color.h:195-199,
 * Panda3DS textures.cpp:175-182) and tile rows stored top-down (memory tile
 * row 0 is the image top row: Azahar's linear buffer is bottom-up for OpenGL
 * (texture_codec.h:892-897), so its tile walk starts tile row 0 at linear row
 * height-8 and DecodeTile (:577-604) maps in-tile row y to linear row 7-y
 * within the tile; because the tile base is at linear row height-8, tile row 0
 * lands on linear row height-1, i.e. the image top row.
 * Panda3DS indexes memory by v directly (textures.cpp:94-107)).
 *
 * src: width*height*4 bytes. dst: width*height*4 bytes, fully overwritten.
 * src and dst must not overlap. width and height must both be powers of two
 * in 8..1024. dst is untouched on every error path.
 */
TwlhdPicaTexResult twlhd_pica_tex_swizzle_rgba8(const uint8_t *src,
                                                uint32_t width, uint32_t height,
                                                uint8_t *dst, uint32_t dst_size);

/* Inverse of twlhd_pica_tex_swizzle_rgba8(); used by tests only. */
TwlhdPicaTexResult twlhd_pica_tex_unswizzle_rgba8(const uint8_t *src,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  uint8_t *dst,
                                                  uint32_t dst_size);

/*
 * Map DS TEXIMAGE_PARAM bits 16-19 to per-axis PICA wrap modes:
 *   bit 16 repeat S, bit 17 repeat T, bit 18 flip S, bit 19 flip T.
 * repeat+flip -> MIRRORED_REPEAT, repeat -> REPEAT, otherwise CLAMP_TO_EDGE
 * (a flip bit without its repeat bit behaves as clamp; DS_GX_RESEARCH.md
 * "TEXIMAGE_PARAM", DeSmuME rasterize.cpp:1814-1843).
 */
TwlhdPicaTexResult twlhd_pica_wrap_from_teximage(uint32_t teximage_param,
                                                 TwlhdPicaWrap *s_wrap,
                                                 TwlhdPicaWrap *t_wrap);

/*
 * Normalise DS texel-space coordinates (TwlhdGxTexVertex.s/t, t growing down
 * from the top row) to PICA u/v:
 *
 *     u = s / width
 *     v = 1 - t / height
 *
 * PICA v=0 samples the last memory row (image bottom), v=1 samples memory
 * row 0 (image top). DS t=0 is the top row, so it maps to v=1; DS t=height-1
 * is the bottom row, so it maps to v=0. Sampling the swizzled texture with
 * these coordinates reproduces the DS image upright.
 *
 * tex[i] must describe main-buffer vertex i; out[i] receives its coordinate.
 * count vertices are written; width/height must be powers of two in 8..1024;
 * out_capacity must be >= count. Writes only out, never tex. Buffers are
 * untouched on every error path.
 */
TwlhdPicaTexResult twlhd_pica_texcoords_from_gx(const TwlhdGxTexVertex *tex,
                                                uint32_t count, uint32_t width,
                                                uint32_t height,
                                                TwlhdPicaTexCoord *out,
                                                uint32_t out_capacity);

/*
 * Group a triangle list (vertex_count % 3 == 0) into consecutive runs of
 * triangles sharing the same (teximage_param, pltt_base), keyed by the run's
 * first vertex (the same way the interpreter latches texture state per
 * vertex). Each entry is one draw call's worth of vertices.
 *
 * untextured is 1 when PackedFormat (TEXIMAGE_PARAM bits 26-28) is 0, which
 * includes the interpreter's "param 0 = untextured" case.
 *
 * capacity bounds the number of plan entries (not vertices); if the runs
 * would not fit, TWLHD_PICA_TEX_ERR_CAPACITY is returned before any write.
 * out_count receives the number of entries. Buffers are untouched on error.
 */
TwlhdPicaTexResult twlhd_pica_tex_bind_plan(const TwlhdGxTexVertex *tex,
                                            uint32_t vertex_count,
                                            TwlhdPicaBindPlan *out,
                                            uint32_t capacity,
                                            uint32_t *out_count);

const char *twlhd_pica_tex_result_string(TwlhdPicaTexResult result);

#ifdef __cplusplus
}
#endif
