// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - v2 capture format parser.
 *
 * See common/twlhd_v2_parse.h and docs/TWLHD_V2_FORMAT.md.
 * HARDWARE UNVERIFIED (host-tested only).
 */

#include "twlhd_v2_parse.h"

#include <string.h>

#define RSTA_DISP3DCNT_START 0x04000060u
#define RSTA_EDGE_START      0x04000330u
#define RSTA_ALPHA_START     0x04000340u
#define RSTA_CLEAR_START     0x04000350u
#define RSTA_FOG_COLOR_START 0x04000358u
#define RSTA_FOG_TABLE_START 0x04000360u
#define RSTA_TOON_START      0x04000380u
#define RSTA_CUTOFF_START    0x04000610u

typedef struct {
    uint32_t start; /* inclusive */
    uint32_t end;   /* exclusive */
} RstaRange;

/*
 * Whitelisted rendering-engine registers. Every range is a contiguous block
 * taken from GBATEK's 3D rendering-engine I/O map; see the spec for the
 * per-range labels.
 */
static const RstaRange g_rsta_ranges[] = {
    {RSTA_DISP3DCNT_START, 0x04000062u}, /* DISP3DCNT                 */
    {RSTA_EDGE_START,      0x04000340u}, /* edge colours + clear id   */
    {RSTA_ALPHA_START,     0x04000342u}, /* alpha test                */
    {RSTA_CLEAR_START,     0x04000358u}, /* clear colour/depth/offset */
    {RSTA_FOG_COLOR_START, 0x04000360u}, /* fog colour/offset         */
    {RSTA_FOG_TABLE_START, 0x04000380u}, /* fog table                 */
    {RSTA_TOON_START,      0x040003C0u}, /* toon table                */
    {RSTA_CUTOFF_START,    0x04000612u}, /* 1-dot depth               */
};

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int twlhd_v2_cmd_param_count(uint32_t id, uint8_t *out_count) {
    uint8_t count;

    switch (id) {
    case TWLHD_V2_CMD_MTX_MODE:       count = 1;  break;
    case TWLHD_V2_CMD_MTX_PUSH:       count = 0;  break;
    case TWLHD_V2_CMD_MTX_POP:        count = 1;  break;
    case TWLHD_V2_CMD_MTX_STORE:      count = 1;  break;
    case TWLHD_V2_CMD_MTX_RESTORE:    count = 1;  break;
    case TWLHD_V2_CMD_MTX_IDENTITY:   count = 0;  break;
    case TWLHD_V2_CMD_MTX_LOAD_4x4:   count = 16; break;
    case TWLHD_V2_CMD_MTX_LOAD_4x3:   count = 12; break;
    case TWLHD_V2_CMD_MTX_MULT_4x4:   count = 16; break;
    case TWLHD_V2_CMD_MTX_MULT_4x3:   count = 12; break;
    case TWLHD_V2_CMD_MTX_MULT_3x3:   count = 9;  break;
    case TWLHD_V2_CMD_MTX_SCALE:      count = 3;  break;
    case TWLHD_V2_CMD_MTX_TRANS:      count = 3;  break;
    case TWLHD_V2_CMD_COLOR:          count = 1;  break;
    case TWLHD_V2_CMD_NORMAL:         count = 1;  break;
    case TWLHD_V2_CMD_TEX_COORD:      count = 1;  break;
    case TWLHD_V2_CMD_VTX_16:         count = 2;  break;
    case TWLHD_V2_CMD_VTX_10:         count = 1;  break;
    case TWLHD_V2_CMD_VTX_XY:         count = 1;  break;
    case TWLHD_V2_CMD_VTX_XZ:         count = 1;  break;
    case TWLHD_V2_CMD_VTX_YZ:         count = 1;  break;
    case TWLHD_V2_CMD_VTX_DIFF:       count = 1;  break;
    case TWLHD_V2_CMD_POLYGON_ATTR:   count = 1;  break;
    case TWLHD_V2_CMD_TEXIMAGE_PARAM: count = 1;  break;
    case TWLHD_V2_CMD_PLTT_BASE:      count = 1;  break;
    case TWLHD_V2_CMD_DIF_AMB:        count = 1;  break;
    case TWLHD_V2_CMD_SPE_EMI:        count = 1;  break;
    case TWLHD_V2_CMD_LIGHT_VECTOR:   count = 1;  break;
    case TWLHD_V2_CMD_LIGHT_COLOR:    count = 1;  break;
    case TWLHD_V2_CMD_SHININESS:      count = 32; break;
    case TWLHD_V2_CMD_BEGIN_VTXS:     count = 1;  break;
    case TWLHD_V2_CMD_END_VTXS:       count = 0;  break;
    case TWLHD_V2_CMD_SWAP_BUFFERS:   count = 1;  break;
    case TWLHD_V2_CMD_VIEWPORT:       count = 1;  break;
    case TWLHD_V2_CMD_BOX_TEST:       count = 3;  break;
    case TWLHD_V2_CMD_POS_TEST:       count = 2;  break;
    case TWLHD_V2_CMD_VEC_TEST:       count = 1;  break;
    default:
        return 0;
    }

    if (out_count) *out_count = count;
    return 1;
}

uint8_t twlhd_v2_cmd_entry_count(uint32_t id) {
    uint8_t count = 0;

    (void)twlhd_v2_cmd_param_count(id, &count);
    return count ? count : 1u;
}

int twlhd_v2_rsta_addr_in_range(uint32_t addr) {
    size_t i;

    for (i = 0; i < sizeof g_rsta_ranges / sizeof g_rsta_ranges[0]; i++) {
        if (addr >= g_rsta_ranges[i].start && addr < g_rsta_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

int twlhd_v2_rsta_whitelisted(uint32_t addr, uint32_t width) {
    uint64_t end;
    size_t i;

    if (width != 1u && width != 2u && width != 4u) return 0;
    if (addr % width != 0u) return 0;

    end = (uint64_t)addr + (uint64_t)width;
    for (i = 0; i < sizeof g_rsta_ranges / sizeof g_rsta_ranges[0]; i++) {
        if ((uint64_t)addr >= (uint64_t)g_rsta_ranges[i].start &&
            end <= (uint64_t)g_rsta_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

int twlhd_v2_tex_sizes(uint32_t teximage_param, uint32_t *texel_bytes,
                       uint32_t *palette_bytes, uint32_t *index_bytes) {
    static const uint32_t texel_eighths[8] = {0u, 8u, 2u, 4u, 8u, 2u, 8u, 16u};
    static const uint32_t palette_by_format[8] = {0u, 64u, 8u, 32u,
                                                  512u, 0u, 16u, 0u};
    uint32_t format = (teximage_param >> 26) & 7u;
    uint32_t width;
    uint32_t height;
    uint64_t texels;

    if (format == 0u) return 0;

    width = 8u << ((teximage_param >> 20) & 7u);
    height = 8u << ((teximage_param >> 23) & 7u);
    texels = (uint64_t)width * (uint64_t)height;

    if (texel_bytes) {
        *texel_bytes = (uint32_t)(texels * texel_eighths[format] >> 3);
    }
    if (palette_bytes) *palette_bytes = palette_by_format[format];
    if (index_bytes) {
        *index_bytes = format == 5u ? (uint32_t)(texels >> 3) : 0u;
    }
    return 1;
}

int twlhd_v2_tex_palette_size_ok(uint32_t teximage_param,
                                 uint32_t palette_bytes) {
    uint32_t format = (teximage_param >> 26) & 7u;
    uint32_t fixed_bytes;

    if (!twlhd_v2_tex_sizes(teximage_param, NULL, &fixed_bytes, NULL)) {
        return 0;
    }
    if (format == 5u) {
        return palette_bytes >= TWLHD_V2_TEX_PALETTE_4X4_MIN_BYTES &&
               palette_bytes <= TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES &&
               (palette_bytes & 1u) == 0u;
    }
    return palette_bytes == fixed_bytes;
}

static int is_raw_entry(uint32_t word) {
    return word == TWLHD_V2_RAW_FIFO_BIT;
}

static int entry_is_swap(uint32_t word) {
    return !is_raw_entry(word) &&
           (word & TWLHD_V2_CMD_ID_MASK) == TWLHD_V2_CMD_SWAP_BUFFERS;
}

static TwlhdV2Result validate_gxcm(const uint8_t *payload, uint32_t size,
                                   uint32_t flags, uint32_t *out_count) {
    uint32_t count;
    uint32_t i;

    if (size % TWLHD_V2_GXCM_ENTRY_SIZE != 0u) return TWLHD_V2_ERR_GXCM;

    count = size / TWLHD_V2_GXCM_ENTRY_SIZE;
    if (count == 0u || count > TWLHD_V2_GXCM_MAX_ENTRIES) {
        return TWLHD_V2_ERR_GXCM;
    }

    /* Pass 1: cmd_word bit layout and command id validity. */
    for (i = 0; i < count; i++) {
        uint32_t word = read_le32(payload + (size_t)i * TWLHD_V2_GXCM_ENTRY_SIZE);

        if (word == TWLHD_V2_RAW_FIFO_BIT) continue;

        if ((word & TWLHD_V2_RAW_FIFO_BIT) != 0u) {
            return TWLHD_V2_ERR_GXCM_CMD;
        }
        if ((word & TWLHD_V2_CMD_NONRAW_MASK) != 0u) {
            return TWLHD_V2_ERR_GXCM_CMD;
        }
        if (!twlhd_v2_cmd_param_count(word & TWLHD_V2_CMD_ID_MASK, NULL)) {
            return TWLHD_V2_ERR_GXCM_CMD;
        }
    }

    /* Pass 2: strict grouping (one complete group per command). */
    i = 0;
    while (i < count) {
        uint32_t word = read_le32(payload + (size_t)i * TWLHD_V2_GXCM_ENTRY_SIZE);
        uint32_t id;
        uint32_t expected;
        uint32_t j;

        if (is_raw_entry(word)) {
            i++;
            continue;
        }

        id = word & TWLHD_V2_CMD_ID_MASK;
        expected = twlhd_v2_cmd_entry_count(id);
        j = i;
        while (j < count && j - i < expected) {
            uint32_t next = read_le32(
                payload + (size_t)j * TWLHD_V2_GXCM_ENTRY_SIZE);

            if (is_raw_entry(next) || (next & TWLHD_V2_CMD_ID_MASK) != id) {
                break;
            }
            j++;
        }

        if (j - i == expected) {
            i = j;
            continue;
        }

        /*
         * An incomplete trailing group is only meaningful when the recorder
         * ran out of capacity: anything else means the stream is malformed.
         */
        if (j == count && (flags & TWLHD_V2_FLAG_OVERFLOW) != 0u) {
            break;
        }
        return TWLHD_V2_ERR_GXCM_GROUP;
    }

    /* Pass 3: SWAP_BUFFERS may only be the last entry, iff FRAME_COMPLETE. */
    for (i = 0; i < count; i++) {
        uint32_t word = read_le32(payload + (size_t)i * TWLHD_V2_GXCM_ENTRY_SIZE);

        if (entry_is_swap(word) && i + 1u != count) {
            return TWLHD_V2_ERR_GXCM_FRAME;
        }
    }

    {
        uint32_t last = read_le32(
            payload + (size_t)(count - 1u) * TWLHD_V2_GXCM_ENTRY_SIZE);
        int complete = (flags & TWLHD_V2_FLAG_FRAME_COMPLETE) != 0u;

        if (complete != entry_is_swap(last)) {
            return TWLHD_V2_ERR_GXCM_FRAME;
        }
    }

    *out_count = count;
    return TWLHD_V2_OK;
}

static TwlhdV2Result validate_rsta(const uint8_t *payload, uint32_t size,
                                   uint32_t gxcm_count, uint32_t *out_count) {
    uint32_t count;
    uint32_t previous_index = 0;
    uint32_t i;

    if (size % TWLHD_V2_RSTA_ENTRY_SIZE != 0u) return TWLHD_V2_ERR_RSTA;

    count = size / TWLHD_V2_RSTA_ENTRY_SIZE;
    if (count > TWLHD_V2_RSTA_MAX_ENTRIES) return TWLHD_V2_ERR_RSTA;

    for (i = 0; i < count; i++) {
        const uint8_t *p = payload + (size_t)i * TWLHD_V2_RSTA_ENTRY_SIZE;
        uint32_t addr = read_le32(p + 0);
        uint32_t value = read_le32(p + 4);
        uint32_t gx_index = read_le32(p + 8);
        uint32_t width = read_le32(p + 12);

        if (!twlhd_v2_rsta_whitelisted(addr, width)) return TWLHD_V2_ERR_RSTA;
        if (width == 1u && (value & ~0xFFu) != 0u) return TWLHD_V2_ERR_RSTA;
        if (width == 2u && (value & ~0xFFFFu) != 0u) return TWLHD_V2_ERR_RSTA;
        if (gx_index > gxcm_count) return TWLHD_V2_ERR_RSTA;
        if (i != 0u && gx_index < previous_index) return TWLHD_V2_ERR_RSTA;
        previous_index = gx_index;
    }

    *out_count = count;
    return TWLHD_V2_OK;
}

static void read_tex_record(const uint8_t *p, TwlhdV2TexRecord *rec) {
    rec->teximage_param = read_le32(p + 0);
    rec->pltt_base = read_le32(p + 4);
    rec->texel_offset = read_le32(p + 8);
    rec->texel_size = read_le32(p + 12);
    rec->palette_offset = read_le32(p + 16);
    rec->palette_size = read_le32(p + 20);
    rec->index_offset = read_le32(p + 24);
    rec->index_size = read_le32(p + 28);
}

/*
 * TXDS/TXDT/PLDT validation. Normative order (docs/TWLHD_V2_FORMAT.md s.8):
 * structural record checks, duplicate keys, size formula, TXDT/PLDT presence,
 * bounds, then chunk payload limits. TXDT/PLDT without TXDS never reaches this
 * function: twlhd_v2_parse() rejects that orphan case with ERR_MISSING_CHUNK
 * before the payload-size caps.
 */
static TwlhdV2Result validate_tex(const uint8_t *txds, uint32_t txds_size,
                                  int have_txdt, uint32_t txdt_size,
                                  int have_pldt, uint32_t pldt_size,
                                  uint32_t *out_count) {
    uint32_t count;
    uint32_t i;
    uint32_t j;
    int need_pldt = 0;

    if (txds_size % TWLHD_V2_TXDS_RECORD_SIZE != 0u) return TWLHD_V2_ERR_TXDS;

    count = txds_size / TWLHD_V2_TXDS_RECORD_SIZE;
    if (count == 0u || count > TWLHD_V2_TXDS_MAX_RECORDS) {
        return TWLHD_V2_ERR_TXDS;
    }

    for (i = 0; i < count; i++) {
        const uint8_t *p = txds + (size_t)i * TWLHD_V2_TXDS_RECORD_SIZE;
        TwlhdV2TexRecord rec;

        read_tex_record(p, &rec);
        if (read_le32(p + 32) != 0u || read_le32(p + 36) != 0u) {
            return TWLHD_V2_ERR_TXDS;
        }
        if (!twlhd_v2_tex_sizes(rec.teximage_param, NULL, NULL, NULL)) {
            return TWLHD_V2_ERR_TXDS;
        }
    }

    for (i = 0; i < count; i++) {
        TwlhdV2TexRecord a;

        read_tex_record(txds + (size_t)i * TWLHD_V2_TXDS_RECORD_SIZE, &a);
        for (j = 0; j < i; j++) {
            TwlhdV2TexRecord b;

            read_tex_record(txds + (size_t)j * TWLHD_V2_TXDS_RECORD_SIZE, &b);
            if (a.teximage_param == b.teximage_param &&
                a.pltt_base == b.pltt_base) {
                return TWLHD_V2_ERR_TXDS_KEY;
            }
        }
    }

    for (i = 0; i < count; i++) {
        TwlhdV2TexRecord rec;
        uint32_t texel_bytes;
        uint32_t index_bytes;

        read_tex_record(txds + (size_t)i * TWLHD_V2_TXDS_RECORD_SIZE, &rec);
        (void)twlhd_v2_tex_sizes(rec.teximage_param, &texel_bytes, NULL,
                                 &index_bytes);

        if (rec.texel_size != texel_bytes ||
            !twlhd_v2_tex_palette_size_ok(rec.teximage_param,
                                          rec.palette_size) ||
            rec.index_size != index_bytes) {
            return TWLHD_V2_ERR_TXDS_SIZE;
        }
        if (rec.texel_size == 0u && rec.texel_offset != 0u) {
            return TWLHD_V2_ERR_TXDS_SIZE;
        }
        if (rec.palette_size == 0u && rec.palette_offset != 0u) {
            return TWLHD_V2_ERR_TXDS_SIZE;
        }
        if (rec.index_size == 0u && rec.index_offset != 0u) {
            return TWLHD_V2_ERR_TXDS_SIZE;
        }
        if (rec.palette_size != 0u) need_pldt = 1;
    }

    if (!have_txdt) return TWLHD_V2_ERR_MISSING_CHUNK;
    if (need_pldt && !have_pldt) return TWLHD_V2_ERR_MISSING_CHUNK;

    for (i = 0; i < count; i++) {
        TwlhdV2TexRecord rec;
        uint64_t end;

        read_tex_record(txds + (size_t)i * TWLHD_V2_TXDS_RECORD_SIZE, &rec);

        end = (uint64_t)rec.texel_offset + (uint64_t)rec.texel_size;
        if (end > (uint64_t)txdt_size) return TWLHD_V2_ERR_TXDS_BOUNDS;
        end = (uint64_t)rec.index_offset + (uint64_t)rec.index_size;
        if (end > (uint64_t)txdt_size) return TWLHD_V2_ERR_TXDS_BOUNDS;
        end = (uint64_t)rec.palette_offset + (uint64_t)rec.palette_size;
        if (end > (uint64_t)pldt_size) return TWLHD_V2_ERR_TXDS_BOUNDS;
    }

    if (have_txdt && txdt_size > TWLHD_V2_TXDT_MAX_BYTES) {
        return TWLHD_V2_ERR_TXDT;
    }
    if (have_pldt && pldt_size > TWLHD_V2_PLDT_MAX_BYTES) {
        return TWLHD_V2_ERR_PLDT;
    }

    *out_count = count;
    return TWLHD_V2_OK;
}

static TwlhdV2Result validate_meta(const uint8_t *payload, uint32_t size,
                                   uint32_t *out_backend,
                                   uint32_t *out_coverage) {
    uint32_t backend;
    uint32_t coverage;

    if (size != TWLHD_V2_META_PAYLOAD_SIZE) return TWLHD_V2_ERR_META;

    backend = read_le32(payload + 0);
    coverage = read_le32(payload + 4);

    if ((coverage & TWLHD_V2_COVERAGE_RESERVED_MASK) != 0u) {
        return TWLHD_V2_ERR_META;
    }
    if (read_le32(payload + 8) != 0u || read_le32(payload + 12) != 0u) {
        return TWLHD_V2_ERR_META;
    }

    *out_backend = backend;
    *out_coverage = coverage;
    return TWLHD_V2_OK;
}

TwlhdV2Result twlhd_v2_parse(const uint8_t *data, size_t size,
                             TwlhdV2View *view) {
    uint32_t header_size;
    uint32_t total_size;
    uint32_t flags;
    uint32_t frame_index;
    uint32_t chunk_count;
    uint32_t gxcm_offset = 0;
    uint32_t gxcm_size = 0;
    uint32_t gxcm_flags = 0;
    uint32_t meta_offset = 0;
    uint32_t meta_size = 0;
    uint32_t meta_flags = 0;
    uint32_t rsta_offset = 0;
    uint32_t rsta_size = 0;
    uint32_t rsta_flags = 0;
    uint32_t txds_offset = 0;
    uint32_t txds_size = 0;
    uint32_t txds_flags = 0;
    uint32_t txdt_offset = 0;
    uint32_t txdt_size = 0;
    uint32_t txdt_flags = 0;
    uint32_t pldt_offset = 0;
    uint32_t pldt_size = 0;
    uint32_t pldt_flags = 0;
    uint32_t unknown = 0;
    uint32_t seen_gxcm = 0;
    uint32_t seen_meta = 0;
    uint32_t seen_rsta = 0;
    uint32_t seen_txds = 0;
    uint32_t seen_txdt = 0;
    uint32_t seen_pldt = 0;
    uint64_t offset;
    uint32_t i;
    TwlhdV2Result result;
    TwlhdV2View local_view;

    if (!view) return TWLHD_V2_ERR_NULL;
    memset(view, 0, sizeof *view);
    if (!data) return TWLHD_V2_ERR_NULL;

    /* Version first whenever the word exists, so v1 is not mislabelled. */
    if (size >= 4u && read_le32(data) != TWLHD_V2_VERSION) {
        return TWLHD_V2_ERR_VERSION;
    }
    if (size < TWLHD_V2_HEADER_SIZE) return TWLHD_V2_ERR_TRUNCATED;

    header_size = read_le32(data + 4);
    total_size = read_le32(data + 8);
    flags = read_le32(data + 12);
    frame_index = read_le32(data + 16);
    chunk_count = read_le32(data + 20);

    if (header_size != TWLHD_V2_HEADER_SIZE) return TWLHD_V2_ERR_HEADER;
    if (read_le32(data + 24) != 0u || read_le32(data + 28) != 0u) {
        return TWLHD_V2_ERR_HEADER;
    }

    if ((flags & TWLHD_V2_FLAG_RESERVED_MASK) != 0u) {
        return TWLHD_V2_ERR_FLAGS;
    }
    if (chunk_count == 0u || chunk_count > TWLHD_V2_CHUNK_COUNT_MAX) {
        return TWLHD_V2_ERR_CHUNK_COUNT;
    }
    if ((uint64_t)total_size != (uint64_t)size ||
        total_size > TWLHD_V2_FILE_MAX) {
        return TWLHD_V2_ERR_SIZE;
    }

    offset = TWLHD_V2_HEADER_SIZE;
    for (i = 0; i < chunk_count; i++) {
        uint32_t type;
        uint32_t payload_size;
        uint32_t chunk_flags;
        uint64_t payload_end;
        uint32_t payload_offset;

        if (offset + TWLHD_V2_CHUNK_HEADER_SIZE > (uint64_t)total_size) {
            return TWLHD_V2_ERR_CHUNK_BOUNDS;
        }

        payload_offset = (uint32_t)(offset + TWLHD_V2_CHUNK_HEADER_SIZE);
        type = read_le32(data + (size_t)offset + 0);
        payload_size = read_le32(data + (size_t)offset + 4);
        chunk_flags = read_le32(data + (size_t)offset + 8);

        payload_end = (uint64_t)payload_offset + (uint64_t)payload_size;
        if (payload_end > (uint64_t)total_size) {
            return TWLHD_V2_ERR_CHUNK_BOUNDS;
        }
        if (payload_size % 4u != 0u) return TWLHD_V2_ERR_CHUNK_ALIGN;
        if ((chunk_flags & TWLHD_V2_CHUNK_FLAG_RESERVED) != 0u) {
            return TWLHD_V2_ERR_CHUNK_FLAGS;
        }
        if (read_le32(data + (size_t)offset + 12) != 0u) {
            return TWLHD_V2_ERR_CHUNK_FLAGS;
        }

        if (type == TWLHD_V2_CHUNK_GXCM) {
            if (seen_gxcm) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_gxcm = 1;
            gxcm_offset = payload_offset;
            gxcm_size = payload_size;
            gxcm_flags = chunk_flags;
        } else if (type == TWLHD_V2_CHUNK_META) {
            if (seen_meta) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_meta = 1;
            meta_offset = payload_offset;
            meta_size = payload_size;
            meta_flags = chunk_flags;
        } else if (type == TWLHD_V2_CHUNK_RSTA) {
            if (seen_rsta) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_rsta = 1;
            rsta_offset = payload_offset;
            rsta_size = payload_size;
            rsta_flags = chunk_flags;
        } else if (type == TWLHD_V2_CHUNK_TXDS) {
            if (seen_txds) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_txds = 1;
            txds_offset = payload_offset;
            txds_size = payload_size;
            txds_flags = chunk_flags;
        } else if (type == TWLHD_V2_CHUNK_TXDT) {
            if (seen_txdt) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_txdt = 1;
            txdt_offset = payload_offset;
            txdt_size = payload_size;
            txdt_flags = chunk_flags;
        } else if (type == TWLHD_V2_CHUNK_PLDT) {
            if (seen_pldt) return TWLHD_V2_ERR_DUP_CHUNK;
            seen_pldt = 1;
            pldt_offset = payload_offset;
            pldt_size = payload_size;
            pldt_flags = chunk_flags;
        } else {
            if ((chunk_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
                return TWLHD_V2_ERR_UNSUPPORTED_REQUIRED;
            }
            unknown++;
        }

        offset = payload_end;
    }

    if (offset != (uint64_t)total_size) return TWLHD_V2_ERR_CHUNK_BOUNDS;
    if (!seen_gxcm || !seen_meta) return TWLHD_V2_ERR_MISSING_CHUNK;

    /*
     * Orphan texture payloads: TXDT/PLDT only make sense with TXDS. This keys
     * on chunk presence, not record count, and runs before the payload-size
     * caps so an oversized orphan reports ERR_MISSING_CHUNK.
     */
    if ((seen_txdt || seen_pldt) && !seen_txds) {
        return TWLHD_V2_ERR_MISSING_CHUNK;
    }

    /* Known chunks have fixed REQUIRED-bit expectations. */
    if ((gxcm_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) == 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }
    if ((meta_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }
    if (seen_rsta && (rsta_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }
    if (seen_txds && (txds_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }
    if (seen_txdt && (txdt_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }
    if (seen_pldt && (pldt_flags & TWLHD_V2_CHUNK_FLAG_REQUIRED) != 0u) {
        return TWLHD_V2_ERR_CHUNK_FLAGS;
    }

    memset(&local_view, 0, sizeof local_view);
    local_view.total_size = total_size;
    local_view.flags = flags;
    local_view.frame_index = frame_index;
    local_view.chunk_count = chunk_count;
    local_view.unknown_chunks_skipped = unknown;
    local_view.gxcm = data + gxcm_offset;
    local_view.rsta = seen_rsta ? data + rsta_offset : NULL;
    local_view.txds = seen_txds ? data + txds_offset : NULL;
    local_view.txdt = seen_txdt ? data + txdt_offset : NULL;
    local_view.txdt_size = txdt_size;
    local_view.pldt = seen_pldt ? data + pldt_offset : NULL;
    local_view.pldt_size = pldt_size;

    result = validate_gxcm(local_view.gxcm, gxcm_size, flags,
                           &local_view.gxcm_count);
    if (result != TWLHD_V2_OK) goto fail;

    if (seen_rsta) {
        result = validate_rsta(local_view.rsta, rsta_size,
                               local_view.gxcm_count,
                               &local_view.rsta_count);
        if (result != TWLHD_V2_OK) goto fail;
    }

    if (seen_txds) {
        result = validate_tex(local_view.txds, txds_size, seen_txdt, txdt_size,
                              seen_pldt, pldt_size, &local_view.txds_count);
        if (result != TWLHD_V2_OK) goto fail;
    }

    result = validate_meta(data + meta_offset, meta_size,
                           &local_view.meta_backend_id,
                           &local_view.meta_coverage_flags);
    if (result != TWLHD_V2_OK) goto fail;

    memcpy(view, &local_view, sizeof local_view);
    return TWLHD_V2_OK;

fail:
    memset(view, 0, sizeof *view);
    return result;
}

const char *twlhd_v2_result_string(TwlhdV2Result result) {
    switch (result) {
    case TWLHD_V2_OK:                        return "OK";
    case TWLHD_V2_ERR_NULL:                  return "ERR_NULL";
    case TWLHD_V2_ERR_TRUNCATED:             return "ERR_TRUNCATED";
    case TWLHD_V2_ERR_VERSION:               return "ERR_VERSION";
    case TWLHD_V2_ERR_HEADER:                return "ERR_HEADER";
    case TWLHD_V2_ERR_SIZE:                  return "ERR_SIZE";
    case TWLHD_V2_ERR_FLAGS:                 return "ERR_FLAGS";
    case TWLHD_V2_ERR_CHUNK_COUNT:           return "ERR_CHUNK_COUNT";
    case TWLHD_V2_ERR_CHUNK_BOUNDS:          return "ERR_CHUNK_BOUNDS";
    case TWLHD_V2_ERR_CHUNK_ALIGN:           return "ERR_CHUNK_ALIGN";
    case TWLHD_V2_ERR_CHUNK_FLAGS:           return "ERR_CHUNK_FLAGS";
    case TWLHD_V2_ERR_DUP_CHUNK:             return "ERR_DUP_CHUNK";
    case TWLHD_V2_ERR_MISSING_CHUNK:         return "ERR_MISSING_CHUNK";
    case TWLHD_V2_ERR_UNSUPPORTED_REQUIRED:  return "ERR_UNSUPPORTED_REQUIRED";
    case TWLHD_V2_ERR_META:                  return "ERR_META";
    case TWLHD_V2_ERR_GXCM:                  return "ERR_GXCM";
    case TWLHD_V2_ERR_GXCM_CMD:              return "ERR_GXCM_CMD";
    case TWLHD_V2_ERR_GXCM_GROUP:            return "ERR_GXCM_GROUP";
    case TWLHD_V2_ERR_GXCM_FRAME:            return "ERR_GXCM_FRAME";
    case TWLHD_V2_ERR_RSTA:                  return "ERR_RSTA";
    case TWLHD_V2_ERR_TXDS:                  return "ERR_TXDS";
    case TWLHD_V2_ERR_TXDS_KEY:              return "ERR_TXDS_KEY";
    case TWLHD_V2_ERR_TXDS_SIZE:             return "ERR_TXDS_SIZE";
    case TWLHD_V2_ERR_TXDS_BOUNDS:           return "ERR_TXDS_BOUNDS";
    case TWLHD_V2_ERR_TXDT:                  return "ERR_TXDT";
    case TWLHD_V2_ERR_PLDT:                  return "ERR_PLDT";
    default:                                 return "ERR_UNKNOWN";
    }
}

uint32_t twlhd_v2_gxcm_cmd_word(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->gxcm || index >= view->gxcm_count) return 0;
    return read_le32(view->gxcm + (size_t)index * TWLHD_V2_GXCM_ENTRY_SIZE);
}

uint32_t twlhd_v2_gxcm_value(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->gxcm || index >= view->gxcm_count) return 0;
    return read_le32(view->gxcm + (size_t)index * TWLHD_V2_GXCM_ENTRY_SIZE + 4);
}

uint32_t twlhd_v2_rsta_addr(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->rsta || index >= view->rsta_count) return 0;
    return read_le32(view->rsta + (size_t)index * TWLHD_V2_RSTA_ENTRY_SIZE + 0);
}

uint32_t twlhd_v2_rsta_value(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->rsta || index >= view->rsta_count) return 0;
    return read_le32(view->rsta + (size_t)index * TWLHD_V2_RSTA_ENTRY_SIZE + 4);
}

uint32_t twlhd_v2_rsta_gx_index(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->rsta || index >= view->rsta_count) return 0;
    return read_le32(view->rsta + (size_t)index * TWLHD_V2_RSTA_ENTRY_SIZE + 8);
}

uint32_t twlhd_v2_rsta_width(const TwlhdV2View *view, uint32_t index) {
    if (!view || !view->rsta || index >= view->rsta_count) return 0;
    return read_le32(view->rsta + (size_t)index * TWLHD_V2_RSTA_ENTRY_SIZE + 12);
}

uint32_t twlhd_v2_txds_count(const TwlhdV2View *view) {
    return view ? view->txds_count : 0u;
}

int twlhd_v2_txds_record(const TwlhdV2View *view, uint32_t index,
                         TwlhdV2TexRecord *out) {
    if (!view || !out || !view->txds || index >= view->txds_count) return 0;
    read_tex_record(view->txds + (size_t)index * TWLHD_V2_TXDS_RECORD_SIZE, out);
    return 1;
}

const uint8_t *twlhd_v2_tex_texel_ptr(const TwlhdV2View *view,
                                      uint32_t index) {
    TwlhdV2TexRecord rec;

    if (!view || !view->txdt) return NULL;
    if (!twlhd_v2_txds_record(view, index, &rec)) return NULL;
    if (rec.texel_size == 0u) return NULL;
    if ((uint64_t)rec.texel_offset + (uint64_t)rec.texel_size >
        (uint64_t)view->txdt_size) {
        return NULL;
    }
    return view->txdt + rec.texel_offset;
}

const uint8_t *twlhd_v2_tex_palette_ptr(const TwlhdV2View *view,
                                        uint32_t index) {
    TwlhdV2TexRecord rec;

    if (!view || !view->pldt) return NULL;
    if (!twlhd_v2_txds_record(view, index, &rec)) return NULL;
    if (rec.palette_size == 0u) return NULL;
    if ((uint64_t)rec.palette_offset + (uint64_t)rec.palette_size >
        (uint64_t)view->pldt_size) {
        return NULL;
    }
    return view->pldt + rec.palette_offset;
}

const uint8_t *twlhd_v2_tex_index_ptr(const TwlhdV2View *view,
                                      uint32_t index) {
    TwlhdV2TexRecord rec;

    if (!view || !view->txdt) return NULL;
    if (!twlhd_v2_txds_record(view, index, &rec)) return NULL;
    if (rec.index_size == 0u) return NULL;
    if ((uint64_t)rec.index_offset + (uint64_t)rec.index_size >
        (uint64_t)view->txdt_size) {
        return NULL;
    }
    return view->txdt + rec.index_offset;
}

TwlhdFileFormat twlhd_detect_format(const uint8_t *data, size_t size) {
    uint32_t version;

    if (!data || size < 4u) return TWLHD_FILE_UNKNOWN;

    version = read_le32(data);
    if (version == 1u) {
        return size == TWLHD_V2_V1_WIRE_SIZE ? TWLHD_FILE_V1
                                             : TWLHD_FILE_UNKNOWN;
    }
    if (version == TWLHD_V2_VERSION) {
        return size >= TWLHD_V2_HEADER_SIZE ? TWLHD_FILE_V2
                                            : TWLHD_FILE_UNKNOWN;
    }
    return TWLHD_FILE_UNKNOWN;
}
