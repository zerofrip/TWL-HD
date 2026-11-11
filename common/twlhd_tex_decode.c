// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5B-2c - host-side DS texture decoder (formats 1-7) to RGBA8.
 *
 * Every formula below is taken from DeSmuME desmume/src/texcache.cpp (commit
 * cited in docs/DS_GX_RESEARCH.md) and re-verified against it for 5B-2c; the
 * per-format source lines are listed in docs/DS_GX_RESEARCH.md,
 * "Decoder (5B-2c)". Pure C11: no malloc, no libm, no hardware access.
 */

#include "twlhd_tex_decode.h"

#include <stddef.h>

#define TEX_FORMAT(param) (((param) >> 26) & 7u)
#define TEX_KEYCOLOR0(param) (((param) & 0x20000000u) != 0u)

/* material_5bit_to_8bit[] / material_3bit_to_8bit[] in the DeSmuME source. */
static uint32_t expand5(uint32_t c) { return (c << 3) | (c >> 2); }

static uint32_t expand3(uint32_t a) { return (a << 5) | (a << 2) | (a >> 1); }

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* RGB555 -> RGBA8. Bit 15 of a palette entry is always ignored. */
static void store_rgba(uint8_t *dst, uint16_t color555, uint8_t alpha) {
    dst[0] = (uint8_t)expand5((uint32_t)color555 & 0x1Fu);
    dst[1] = (uint8_t)expand5(((uint32_t)color555 >> 5) & 0x1Fu);
    dst[2] = (uint8_t)expand5(((uint32_t)color555 >> 10) & 0x1Fu);
    dst[3] = alpha;
}

static void store_transparent(uint8_t *dst) {
    dst[0] = 0u;
    dst[1] = 0u;
    dst[2] = 0u;
    dst[3] = 0u;
}

/*
 * Fixed palette table size in 16-bit entries for the formats whose palette is
 * an exact table. Formats 5 (window) and 7 (none) return 0.
 *
 * twlhd_v2_tex_palette_size_ok() already pins these sizes, so the check in
 * twlhd_tex_decode() is a fail-safe, never the primary validation.
 */
static uint32_t fixed_palette_entries(uint32_t format) {
    switch (format) {
    case 1u: return 32u;  /* A3I5 */
    case 2u: return 4u;   /* I2 */
    case 3u: return 16u;  /* I4 */
    case 4u: return 256u; /* I8 */
    case 6u: return 8u;   /* A5I3 */
    default: return 0u;
    }
}

int twlhd_tex_dimensions(uint32_t teximage_param, uint32_t *width,
                         uint32_t *height) {
    if (TEX_FORMAT(teximage_param) == 0u) return 0;
    /* 3-bit shifts: 8 << 7 = 1024 and 1024*1024 fits uint32_t. */
    if (width) *width = 8u << ((teximage_param >> 20) & 7u);
    if (height) *height = 8u << ((teximage_param >> 23) & 7u);
    return 1;
}

const char *twlhd_tex_result_string(TwlhdTexResult result) {
    switch (result) {
    case TWLHD_TEX_OK: return "OK";
    case TWLHD_TEX_ERR_NULL: return "ERR_NULL";
    case TWLHD_TEX_ERR_FORMAT: return "ERR_FORMAT";
    case TWLHD_TEX_ERR_SIZE: return "ERR_SIZE";
    case TWLHD_TEX_ERR_PALETTE_SIZE: return "ERR_PALETTE_SIZE";
    case TWLHD_TEX_ERR_OUTPUT_SIZE: return "ERR_OUTPUT_SIZE";
    case TWLHD_TEX_ERR_PALETTE_RANGE: return "ERR_PALETTE_RANGE";
    default: return "ERR_UNKNOWN";
    }
}

/* Return the 4x4 palette bytes needed by one block of `mode`. */
static uint32_t mode_palette_bytes(uint32_t mode) {
    /* Mode 0 reads entries 0-2; modes 1/3 only entries 0-1; mode 2 reads all. */
    switch (mode) {
    case 0u: return 6u;
    case 2u: return 8u;
    default: return 4u;
    }
}

static void decode_4x4_colours(uint32_t mode, const uint8_t *base, uint8_t col[4][4]) {
    uint16_t c0 = (uint16_t)(read_le16(base + 0u) & 0x7FFFu);
    uint16_t c1 = (uint16_t)(read_le16(base + 2u) & 0x7FFFu);

    store_rgba(col[0], c0, 255u);
    store_rgba(col[1], c1, 255u);

    switch (mode) {
    case 0u:
        store_rgba(col[2], (uint16_t)(read_le16(base + 4u) & 0x7FFFu), 255u);
        store_transparent(col[3]);
        break;
    case 1u: {
        uint32_t i;
        /* texcache.cpp:1806-1815: per-8-bit-channel average, alpha forced 255. */
        for (i = 0; i < 3u; i++) {
            col[2][i] =
                (uint8_t)(((uint32_t)col[0][i] + (uint32_t)col[1][i]) >> 1);
        }
        col[2][3] = 255u;
        store_transparent(col[3]);
        break;
    }
    case 2u:
        store_rgba(col[2], (uint16_t)(read_le16(base + 4u) & 0x7FFFu), 255u);
        store_rgba(col[3], (uint16_t)(read_le16(base + 6u) & 0x7FFFu), 255u);
        break;
    default: {
        /*
         * texcache.cpp:1824-1851: 5:3 and 3:5 blends computed on the expanded
         * 8-bit channels, then folded back to 5 bits with >>6 and expanded
         * again through the 5-bit table.
         */
        uint32_t r0 = (uint32_t)col[0][0], g0 = (uint32_t)col[0][1];
        uint32_t b0 = (uint32_t)col[0][2];
        uint32_t r1 = (uint32_t)col[1][0], g1 = (uint32_t)col[1][1];
        uint32_t b1 = (uint32_t)col[1][2];
        uint16_t t1 = (uint16_t)(((r0 * 5u + r1 * 3u) >> 6) |
                                 (((g0 * 5u + g1 * 3u) >> 6) << 5) |
                                 (((b0 * 5u + b1 * 3u) >> 6) << 10));
        uint16_t t2 = (uint16_t)(((r0 * 3u + r1 * 5u) >> 6) |
                                 (((g0 * 3u + g1 * 5u) >> 6) << 5) |
                                 (((b0 * 3u + b1 * 5u) >> 6) << 10));
        store_rgba(col[2], t1, 255u);
        store_rgba(col[3], t2, 255u);
        break;
    }
    }
}

TwlhdTexResult twlhd_tex_decode(const TwlhdV2TexRecord *rec,
                                const uint8_t *texel, const uint8_t *palette,
                                const uint8_t *index, uint8_t *out,
                                uint32_t out_size) {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t texel_bytes;
    uint32_t index_bytes;
    uint32_t entries;
    uint32_t i;
    uint64_t pixels;

    if (!rec || !texel || !out) return TWLHD_TEX_ERR_NULL;

    format = TEX_FORMAT(rec->teximage_param);
    if (format == 0u) return TWLHD_TEX_ERR_FORMAT;
    if (format != 7u && !palette) return TWLHD_TEX_ERR_NULL;
    if (format == 5u && !index) return TWLHD_TEX_ERR_NULL;

    if (!twlhd_v2_tex_sizes(rec->teximage_param, &texel_bytes, NULL,
                            &index_bytes)) {
        return TWLHD_TEX_ERR_FORMAT; /* unreachable after the format check */
    }
    if (rec->texel_size != texel_bytes || rec->index_size != index_bytes) {
        return TWLHD_TEX_ERR_SIZE;
    }
    if (!twlhd_v2_tex_palette_size_ok(rec->teximage_param, rec->palette_size)) {
        return TWLHD_TEX_ERR_PALETTE_SIZE;
    }

    entries = fixed_palette_entries(format);
    if (entries != 0u && (uint64_t)rec->palette_size < (uint64_t)entries * 2u) {
        return TWLHD_TEX_ERR_PALETTE_RANGE;
    }

    if (!twlhd_tex_dimensions(rec->teximage_param, &width, &height)) {
        return TWLHD_TEX_ERR_FORMAT;
    }
    pixels = (uint64_t)width * (uint64_t)height;
    if ((uint64_t)out_size < pixels * 4u) return TWLHD_TEX_ERR_OUTPUT_SIZE;

    switch (format) {
    case 1u: /* A3I5: bits 0-4 index, 5-7 alpha. texcache.cpp:1524-1525. */
        for (i = 0; i < texel_bytes; i++) {
            uint8_t t = texel[i];
            store_rgba(out + (size_t)i * 4u,
                       (uint16_t)(read_le16(palette + (size_t)(t & 0x1Fu) * 2u) &
                                  0x7FFFu),
                       (uint8_t)expand3((uint32_t)t >> 5));
        }
        break;

    case 2u: /* I2: four indices per byte, low bits first. */
    case 3u: /* I4: two per byte, low nibble first. */
    case 4u: { /* I8: one per byte. */
        uint32_t per_byte = format == 2u ? 4u : (format == 3u ? 2u : 1u);
        uint32_t bits = format == 2u ? 2u : (format == 3u ? 4u : 8u);
        uint32_t mask = (1u << bits) - 1u;
        int key0 = TEX_KEYCOLOR0(rec->teximage_param);

        for (i = 0; i < texel_bytes; i++) {
            uint32_t j;
            for (j = 0; j < per_byte; j++) {
                uint32_t idx = ((uint32_t)texel[i] >> (j * bits)) & mask;
                uint8_t *dst = out + ((size_t)i * per_byte + j) * 4u;
                /* texcache.cpp:429-436 + 1057-1112/1341-1386/1389-1405. */
                if (key0 && idx == 0u) {
                    store_transparent(dst);
                } else {
                    store_rgba(dst,
                               (uint16_t)(read_le16(palette +
                                                    (size_t)idx * 2u) &
                                          0x7FFFu),
                               255u);
                }
            }
        }
        break;
    }

    case 5u: { /* 4x4 compressed: texcache.cpp:1752, 1789-1843. */
        uint32_t x_blocks = width >> 2;
        uint32_t y_blocks = height >> 2;
        uint32_t blocks = x_blocks * y_blocks;
        uint32_t block;

        /*
         * Understood to be contiguous: pal1offset = (word & 0x3FFF) << 1 is a
         * 16-bit entry index and PAL4X4(o) reads byte palAddress + o*2, so the
         * byte offset is (word & 0x3FFF)*4 (texcache.cpp:1752/1791).
         */
        for (block = 0; block < blocks; block++) {
            uint16_t word = read_le16(index + (size_t)block * 2u);
            uint32_t offset = (uint32_t)(word & 0x3FFFu) * 4u;
            uint64_t end = (uint64_t)offset +
                           (uint64_t)mode_palette_bytes((uint32_t)word >> 14);
            if (end > (uint64_t)rec->palette_size) {
                return TWLHD_TEX_ERR_PALETTE_RANGE;
            }
        }

        for (block = 0; block < blocks; block++) {
            uint32_t block_x = block % x_blocks;
            uint32_t block_y = block / x_blocks;
            uint32_t data = read_le32(texel + (size_t)block * 4u);
            uint16_t word = read_le16(index + (size_t)block * 2u);
            const uint8_t *base =
                palette + (size_t)((uint32_t)(word & 0x3FFFu) * 4u);
            uint8_t col[4][4];
            uint32_t sy;

            decode_4x4_colours((uint32_t)word >> 14, base, col);
            for (sy = 0; sy < 4u; sy++) {
                uint32_t row = (data >> (sy * 8u)) & 0xFFu;
                uint8_t *dst = out + (((size_t)(block_y * 4u + sy) * width) +
                                      (size_t)block_x * 4u) *
                                         4u;
                dst[0] = col[row & 3u][0];
                dst[1] = col[row & 3u][1];
                dst[2] = col[row & 3u][2];
                dst[3] = col[row & 3u][3];
                dst[4] = col[(row >> 2) & 3u][0];
                dst[5] = col[(row >> 2) & 3u][1];
                dst[6] = col[(row >> 2) & 3u][2];
                dst[7] = col[(row >> 2) & 3u][3];
                dst[8] = col[(row >> 4) & 3u][0];
                dst[9] = col[(row >> 4) & 3u][1];
                dst[10] = col[(row >> 4) & 3u][2];
                dst[11] = col[(row >> 4) & 3u][3];
                dst[12] = col[(row >> 6) & 3u][0];
                dst[13] = col[(row >> 6) & 3u][1];
                dst[14] = col[(row >> 6) & 3u][2];
                dst[15] = col[(row >> 6) & 3u][3];
            }
        }
        break;
    }

    case 6u: /* A5I3: bits 0-2 index, 3-7 alpha. texcache.cpp:1745-1746. */
        for (i = 0; i < texel_bytes; i++) {
            uint8_t t = texel[i];
            store_rgba(out + (size_t)i * 4u,
                       (uint16_t)(read_le16(palette + (size_t)(t & 0x07u) * 2u) &
                                  0x7FFFu),
                       (uint8_t)expand5((uint32_t)t >> 3));
        }
        break;

    default: /* 7: 16bpp. texcache.cpp:2007-2026. */
        for (i = 0; i < texel_bytes / 2u; i++) {
            uint16_t c = read_le16(texel + (size_t)i * 2u);
            if ((c & 0x8000u) != 0u) {
                store_rgba(out + (size_t)i * 4u, (uint16_t)(c & 0x7FFFu), 255u);
            } else {
                store_transparent(out + (size_t)i * 4u);
            }
        }
        break;
    }

    return TWLHD_TEX_OK;
}
