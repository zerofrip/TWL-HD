// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2d - host-side DS texture preparation for the PICA200.
 *
 * Layout facts and their citations are in docs/DS_GX_RESEARCH.md, section
 * "PICA texture upload (5B-2d)":
 *   - RGBA8 texels are stored as A,B,G,R bytes (Azahar color.h:195-199,
 *     Panda3DS renderer_gl/textures.cpp:175-182).
 *   - 8x8 tiles, Morton (Z) order inside a tile (Azahar video_core/utils.h:12-16,
 *     texture_codec.h:554-559; Panda3DS textures.cpp:94-107).
 *   - Tile rows are stored top-down: memory tile row 0 is the image top row.
 *     Azahar's linear buffer is bottom-up for OpenGL
 *     (texture_codec.h:892-897: "we read/write the linear buffer from the
 *     bottom up"), so its tile walk starts tile row 0 at linear row height-8
 *     and DecodeTile (:577-604) maps in-tile row y to linear row 7-y within
 *     the tile. Because the tile base is at linear row height-8, tile row 0
 *     lands on linear row height-1, i.e. the image top row.
 *     Panda3DS indexes memory by v directly (textures.cpp:94-107) and uploads
 *     row 0 first; with GL's bottom-up data layout that is the image top row.
 *
 * BUILD/UNIT only: no GPU calls, no hardware verification.
 */
#include "twlhd_pica_tex.h"

#include <stddef.h>

static const uint32_t kMortonX[8] = {0x00, 0x01, 0x04, 0x05,
                                     0x10, 0x11, 0x14, 0x15};
static const uint32_t kMortonY[8] = {0x00, 0x02, 0x08, 0x0a,
                                     0x20, 0x22, 0x28, 0x2a};

static int pica_tex_size_ok(uint32_t v) {
    return v >= 8u && v <= 1024u && (v & (v - 1u)) == 0u;
}

static uint32_t pica_tex_byte_size(uint32_t width, uint32_t height) {
    return width * height * 4u;
}

TwlhdPicaTexResult twlhd_pica_tex_swizzle_rgba8(const uint8_t *src,
                                                uint32_t width, uint32_t height,
                                                uint8_t *dst,
                                                uint32_t dst_size) {
    uint32_t y;
    if (!src || !dst) return TWLHD_PICA_TEX_ERR_NULL;
    if (!pica_tex_size_ok(width) || !pica_tex_size_ok(height))
        return TWLHD_PICA_TEX_ERR_SIZE;
    if (dst_size < pica_tex_byte_size(width, height))
        return TWLHD_PICA_TEX_ERR_OUTPUT_SIZE;

    for (y = 0; y < height; y++) {
        /* Memory tile rows are image rows top-down (see the header). */
        uint32_t tile_row = (y >> 3) * width * 8u;
        uint32_t x;
        for (x = 0; x < width; x++) {
            uint32_t morton =
                kMortonX[x & 7u] + kMortonY[y & 7u];
            uint32_t out_texel = tile_row + (x >> 3) * 64u + morton;
            uint32_t in = (y * width + x) * 4u;
            uint32_t out = out_texel * 4u;
            /* Source R,G,B,A -> PICA A,B,G,R. */
            dst[out + 0u] = src[in + 3u];
            dst[out + 1u] = src[in + 2u];
            dst[out + 2u] = src[in + 1u];
            dst[out + 3u] = src[in + 0u];
        }
    }
    return TWLHD_PICA_TEX_OK;
}

TwlhdPicaTexResult twlhd_pica_tex_unswizzle_rgba8(const uint8_t *src,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  uint8_t *dst,
                                                  uint32_t dst_size) {
    uint32_t y;
    if (!src || !dst) return TWLHD_PICA_TEX_ERR_NULL;
    if (!pica_tex_size_ok(width) || !pica_tex_size_ok(height))
        return TWLHD_PICA_TEX_ERR_SIZE;
    if (dst_size < pica_tex_byte_size(width, height))
        return TWLHD_PICA_TEX_ERR_OUTPUT_SIZE;

    for (y = 0; y < height; y++) {
        uint32_t tile_row = (y >> 3) * width * 8u;
        uint32_t x;
        for (x = 0; x < width; x++) {
            uint32_t morton =
                kMortonX[x & 7u] + kMortonY[y & 7u];
            uint32_t in_texel = tile_row + (x >> 3) * 64u + morton;
            uint32_t in = in_texel * 4u;
            uint32_t out = (y * width + x) * 4u;
            /* PICA A,B,G,R -> source R,G,B,A. */
            dst[out + 0u] = src[in + 3u];
            dst[out + 1u] = src[in + 2u];
            dst[out + 2u] = src[in + 1u];
            dst[out + 3u] = src[in + 0u];
        }
    }
    return TWLHD_PICA_TEX_OK;
}

TwlhdPicaTexResult twlhd_pica_wrap_from_teximage(uint32_t teximage_param,
                                                 TwlhdPicaWrap *s_wrap,
                                                 TwlhdPicaWrap *t_wrap) {
    uint32_t repeat_s = (teximage_param >> 16) & 1u;
    uint32_t repeat_t = (teximage_param >> 17) & 1u;
    uint32_t flip_s = (teximage_param >> 18) & 1u;
    uint32_t flip_t = (teximage_param >> 19) & 1u;

    if (!s_wrap || !t_wrap) return TWLHD_PICA_TEX_ERR_NULL;

    if (repeat_s && flip_s)
        *s_wrap = TWLHD_PICA_WRAP_MIRRORED_REPEAT;
    else if (repeat_s)
        *s_wrap = TWLHD_PICA_WRAP_REPEAT;
    else
        *s_wrap = TWLHD_PICA_WRAP_CLAMP_TO_EDGE; /* flip alone = clamp */

    if (repeat_t && flip_t)
        *t_wrap = TWLHD_PICA_WRAP_MIRRORED_REPEAT;
    else if (repeat_t)
        *t_wrap = TWLHD_PICA_WRAP_REPEAT;
    else
        *t_wrap = TWLHD_PICA_WRAP_CLAMP_TO_EDGE;

    return TWLHD_PICA_TEX_OK;
}

TwlhdPicaTexResult twlhd_pica_texcoords_from_gx(const TwlhdGxTexVertex *tex,
                                                uint32_t count, uint32_t width,
                                                uint32_t height,
                                                TwlhdPicaTexCoord *out,
                                                uint32_t out_capacity) {
    uint32_t i;

    if (!tex || !out) return TWLHD_PICA_TEX_ERR_NULL;
    if (!pica_tex_size_ok(width) || !pica_tex_size_ok(height))
        return TWLHD_PICA_TEX_ERR_SIZE;
    if (out_capacity < count) return TWLHD_PICA_TEX_ERR_CAPACITY;

    for (i = 0; i < count; i++) {
        out[i].u = tex[i].s / (float)width;
        /* v = 1 - t/height: PICA v=0 samples the last memory row (image
         * bottom), v=1 samples memory row 0 (image top); DS t=0 is the top
         * row, so it maps to v=1 (see the header). */
        out[i].v = 1.0f - tex[i].t / (float)height;
    }
    return TWLHD_PICA_TEX_OK;
}

TwlhdPicaTexResult twlhd_pica_tex_bind_plan(const TwlhdGxTexVertex *tex,
                                            uint32_t vertex_count,
                                            TwlhdPicaBindPlan *out,
                                            uint32_t capacity,
                                            uint32_t *out_count) {
    uint32_t runs = 0;
    uint32_t i;

    if (!tex || !out || !out_count) return TWLHD_PICA_TEX_ERR_NULL;
    if (vertex_count % 3u != 0u) return TWLHD_PICA_TEX_ERR_COUNT;

    /* Count runs first so the caller's buffer is untouched when it is too
     * small. */
    for (i = 0; i < vertex_count; i += 3u) {
        if (i == 0u || tex[i].teximage_param != tex[i - 3u].teximage_param ||
            tex[i].pltt_base != tex[i - 3u].pltt_base)
            runs++;
    }
    if (runs > capacity) return TWLHD_PICA_TEX_ERR_CAPACITY;

    runs = 0;
    for (i = 0; i < vertex_count; i += 3u) {
        if (i == 0u || tex[i].teximage_param != tex[i - 3u].teximage_param ||
            tex[i].pltt_base != tex[i - 3u].pltt_base) {
            out[runs].first_vertex = i;
            out[runs].vertex_count = 3u;
            out[runs].teximage_param = tex[i].teximage_param;
            out[runs].pltt_base = tex[i].pltt_base;
            out[runs].untextured =
                ((tex[i].teximage_param >> 26) & 7u) == 0u ? 1 : 0;
            runs++;
        } else {
            out[runs - 1u].vertex_count += 3u;
        }
    }

    *out_count = runs;
    return TWLHD_PICA_TEX_OK;
}

const char *twlhd_pica_tex_result_string(TwlhdPicaTexResult result) {
    switch (result) {
    case TWLHD_PICA_TEX_OK:
        return "OK";
    case TWLHD_PICA_TEX_ERR_NULL:
        return "ERR_NULL";
    case TWLHD_PICA_TEX_ERR_SIZE:
        return "ERR_SIZE";
    case TWLHD_PICA_TEX_ERR_OUTPUT_SIZE:
        return "ERR_OUTPUT_SIZE";
    case TWLHD_PICA_TEX_ERR_COUNT:
        return "ERR_COUNT";
    case TWLHD_PICA_TEX_ERR_CAPACITY:
        return "ERR_CAPACITY";
    default:
        return "ERR_UNKNOWN";
    }
}
