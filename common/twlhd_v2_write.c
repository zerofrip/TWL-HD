// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - deterministic v2 capture writer.
 * See common/twlhd_v2_write.h. HARDWARE UNVERIFIED (host-tested only).
 */

#include "twlhd_v2_write.h"

#include <string.h>

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static size_t write_chunk_header(uint8_t *out, size_t offset, uint32_t type,
                                 uint32_t payload_size, uint32_t chunk_flags) {
    put_le32(out + offset + 0, type);
    put_le32(out + offset + 4, payload_size);
    put_le32(out + offset + 8, chunk_flags);
    put_le32(out + offset + 12, 0);
    return offset + TWLHD_V2_CHUNK_HEADER_SIZE;
}

TwlhdV2Result twlhd_v2_write(const TwlhdV2WriteParams *params, uint8_t *out,
                             size_t capacity, size_t *out_size) {
    uint64_t total;
    size_t offset;
    uint32_t chunk_count;
    uint32_t i;
    uint32_t txdt_bytes = 0;
    uint32_t pldt_bytes = 0;
    uint32_t pldt_pad;
    uint32_t pldt_total;
    TwlhdV2View view;
    TwlhdV2Result result;

    if (!params || !out) return TWLHD_V2_ERR_NULL;
    if (params->gxcm_count == 0u ||
        params->gxcm_count > TWLHD_V2_GXCM_MAX_ENTRIES) {
        return TWLHD_V2_ERR_GXCM;
    }
    if (!params->gxcm) return TWLHD_V2_ERR_NULL;
    if (params->rsta_count > TWLHD_V2_RSTA_MAX_ENTRIES) {
        return TWLHD_V2_ERR_RSTA;
    }
    if (params->rsta_count != 0u && !params->rsta) return TWLHD_V2_ERR_NULL;
    for (i = 0; i < params->rsta_count; i++) {
        uint32_t width = params->rsta[i].width;
        uint32_t value = params->rsta[i].value;

        if (width != 1u && width != 2u && width != 4u) {
            return TWLHD_V2_ERR_RSTA;
        }
        if (width == 1u && (value & ~0xFFu) != 0u) {
            return TWLHD_V2_ERR_RSTA;
        }
        if (width == 2u && (value & ~0xFFFFu) != 0u) {
            return TWLHD_V2_ERR_RSTA;
        }
    }

    if (params->tex_count > TWLHD_V2_TXDS_MAX_RECORDS) {
        return TWLHD_V2_ERR_TXDS;
    }
    if (params->tex_count != 0u && !params->tex) return TWLHD_V2_ERR_NULL;
    for (i = 0; i < params->tex_count; i++) {
        const TwlhdV2TexEntry *e = &params->tex[i];
        uint32_t texel_bytes;
        uint32_t index_bytes;
        uint32_t j;

        if (!twlhd_v2_tex_sizes(e->teximage_param, &texel_bytes, NULL,
                                &index_bytes)) {
            return TWLHD_V2_ERR_TXDS;
        }
        if (e->texel_bytes != texel_bytes ||
            !twlhd_v2_tex_palette_size_ok(e->teximage_param,
                                          e->palette_bytes) ||
            e->index_bytes != index_bytes) {
            return TWLHD_V2_ERR_TXDS_SIZE;
        }
        if ((texel_bytes != 0u && !e->texel) ||
            (e->palette_bytes != 0u && !e->palette) ||
            (index_bytes != 0u && !e->index)) {
            return TWLHD_V2_ERR_TXDS;
        }
        for (j = 0; j < i; j++) {
            if (params->tex[j].teximage_param == e->teximage_param &&
                params->tex[j].pltt_base == e->pltt_base) {
                return TWLHD_V2_ERR_TXDS_KEY;
            }
        }
        txdt_bytes += texel_bytes + index_bytes;
        pldt_bytes += e->palette_bytes;
    }

    /*
     * Format-5 windows are only even, so the concatenated PLDT payload may be
     * 2 mod 4. Zero-pad the chunk (never a record) to keep it 4-byte aligned.
     * The parser applies the PLDT cap to the padded payload length too.
     */
    pldt_pad = (4u - (pldt_bytes % 4u)) % 4u;
    pldt_total = pldt_bytes + pldt_pad;
    if (pldt_bytes != 0u && pldt_total > TWLHD_V2_PLDT_MAX_BYTES) {
        return TWLHD_V2_ERR_PLDT;
    }

    total = (uint64_t)TWLHD_V2_HEADER_SIZE +
            2u * (uint64_t)TWLHD_V2_CHUNK_HEADER_SIZE +
            (uint64_t)params->gxcm_count * TWLHD_V2_GXCM_ENTRY_SIZE +
            TWLHD_V2_META_PAYLOAD_SIZE;
    if (params->rsta_count != 0u) {
        total += (uint64_t)TWLHD_V2_CHUNK_HEADER_SIZE +
                 (uint64_t)params->rsta_count * TWLHD_V2_RSTA_ENTRY_SIZE;
    }
    if (params->tex_count != 0u) {
        total += 2u * (uint64_t)TWLHD_V2_CHUNK_HEADER_SIZE +
                 (uint64_t)params->tex_count * TWLHD_V2_TXDS_RECORD_SIZE +
                 (uint64_t)txdt_bytes;
        if (pldt_bytes != 0u) total += TWLHD_V2_CHUNK_HEADER_SIZE;
        total += (uint64_t)pldt_total;
    }

    if (total > (uint64_t)capacity) return TWLHD_V2_ERR_SIZE;

    chunk_count = params->rsta_count != 0u ? 3u : 2u;
    if (params->tex_count != 0u) {
        chunk_count += 2u;
        if (pldt_bytes != 0u) chunk_count += 1u;
    }

    put_le32(out + 0, TWLHD_V2_VERSION);
    put_le32(out + 4, TWLHD_V2_HEADER_SIZE);
    put_le32(out + 8, (uint32_t)total);
    put_le32(out + 12, params->flags);
    put_le32(out + 16, params->frame_index);
    put_le32(out + 20, chunk_count);
    put_le32(out + 24, 0);
    put_le32(out + 28, 0);

    offset = TWLHD_V2_HEADER_SIZE;

    offset = write_chunk_header(
        out, offset, TWLHD_V2_CHUNK_GXCM,
        params->gxcm_count * TWLHD_V2_GXCM_ENTRY_SIZE,
        TWLHD_V2_CHUNK_FLAG_REQUIRED);
    for (i = 0; i < params->gxcm_count; i++) {
        put_le32(out + offset + 0, params->gxcm[i].cmd_word);
        put_le32(out + offset + 4, params->gxcm[i].value);
        offset += TWLHD_V2_GXCM_ENTRY_SIZE;
    }

    offset = write_chunk_header(out, offset, TWLHD_V2_CHUNK_META,
                                TWLHD_V2_META_PAYLOAD_SIZE, 0);
    put_le32(out + offset + 0, params->backend_id);
    put_le32(out + offset + 4, params->coverage_flags);
    put_le32(out + offset + 8, 0);
    put_le32(out + offset + 12, 0);
    offset += TWLHD_V2_META_PAYLOAD_SIZE;

    if (params->rsta_count != 0u) {
        offset = write_chunk_header(
            out, offset, TWLHD_V2_CHUNK_RSTA,
            params->rsta_count * TWLHD_V2_RSTA_ENTRY_SIZE, 0);
        for (i = 0; i < params->rsta_count; i++) {
            put_le32(out + offset + 0, params->rsta[i].addr);
            put_le32(out + offset + 4, params->rsta[i].value);
            put_le32(out + offset + 8, params->rsta[i].gx_index);
            put_le32(out + offset + 12, params->rsta[i].width);
            offset += TWLHD_V2_RSTA_ENTRY_SIZE;
        }
    }

    if (params->tex_count != 0u) {
        uint32_t texel_pos = 0;
        uint32_t palette_pos = 0;
        size_t record_offset;

        offset = write_chunk_header(
            out, offset, TWLHD_V2_CHUNK_TXDS,
            params->tex_count * TWLHD_V2_TXDS_RECORD_SIZE, 0);
        record_offset = offset;
        for (i = 0; i < params->tex_count; i++) {
            const TwlhdV2TexEntry *e = &params->tex[i];
            uint32_t texel_bytes;
            uint32_t index_bytes;

            (void)twlhd_v2_tex_sizes(e->teximage_param, &texel_bytes, NULL,
                                     &index_bytes);
            put_le32(out + record_offset + 0, e->teximage_param);
            put_le32(out + record_offset + 4, e->pltt_base);
            put_le32(out + record_offset + 8, texel_pos);
            put_le32(out + record_offset + 12, texel_bytes);
            put_le32(out + record_offset + 16,
                     e->palette_bytes != 0u ? palette_pos : 0u);
            put_le32(out + record_offset + 20, e->palette_bytes);
            put_le32(out + record_offset + 24,
                     index_bytes != 0u ? texel_pos + texel_bytes : 0u);
            put_le32(out + record_offset + 28, index_bytes);
            put_le32(out + record_offset + 32, 0);
            put_le32(out + record_offset + 36, 0);
            record_offset += TWLHD_V2_TXDS_RECORD_SIZE;
            texel_pos += texel_bytes + index_bytes;
            palette_pos += e->palette_bytes;
        }
        offset += (size_t)params->tex_count * TWLHD_V2_TXDS_RECORD_SIZE;

        offset = write_chunk_header(out, offset, TWLHD_V2_CHUNK_TXDT,
                                    txdt_bytes, 0);
        for (i = 0; i < params->tex_count; i++) {
            const TwlhdV2TexEntry *e = &params->tex[i];

            if (e->texel_bytes != 0u) {
                memcpy(out + offset, e->texel, e->texel_bytes);
                offset += e->texel_bytes;
            }
            if (e->index_bytes != 0u) {
                memcpy(out + offset, e->index, e->index_bytes);
                offset += e->index_bytes;
            }
        }

        if (pldt_bytes != 0u) {
            offset = write_chunk_header(out, offset, TWLHD_V2_CHUNK_PLDT,
                                        pldt_total, 0);
            for (i = 0; i < params->tex_count; i++) {
                const TwlhdV2TexEntry *e = &params->tex[i];

                if (e->palette_bytes != 0u) {
                    memcpy(out + offset, e->palette, e->palette_bytes);
                    offset += e->palette_bytes;
                }
            }
            /* Explicit zero stores: the output buffer may hold stale bytes. */
            if (pldt_pad != 0u) {
                memset(out + offset, 0, pldt_pad);
                offset += pldt_pad;
            }
        }
    }

    /*
     * Source of truth for validity: the parser that will read this file
     * elsewhere. This also catches caller-supplied flag combinations.
     */
    result = twlhd_v2_parse(out, (size_t)total, &view);
    if (result != TWLHD_V2_OK) return result;

    if (out_size) *out_size = (size_t)total;
    return TWLHD_V2_OK;
}
