// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - v2 capture format parser.
 *
 * See common/twlhd_v2_parse.h and docs/TWLHD_V2_FORMAT.md.
 * HARDWARE UNVERIFIED (host-tested only).
 */

#include "twlhd_v2_parse.h"

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
        uint32_t gx_index = read_le32(p + 8);
        uint32_t width = read_le32(p + 12);

        if (!twlhd_v2_rsta_whitelisted(addr, width)) return TWLHD_V2_ERR_RSTA;
        if (gx_index > gxcm_count) return TWLHD_V2_ERR_RSTA;
        if (i != 0u && gx_index < previous_index) return TWLHD_V2_ERR_RSTA;
        previous_index = gx_index;
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
    uint32_t unknown = 0;
    uint32_t seen_gxcm = 0;
    uint32_t seen_meta = 0;
    uint32_t seen_rsta = 0;
    uint64_t offset;
    uint32_t i;
    TwlhdV2Result result;

    if (!data || !view) return TWLHD_V2_ERR_NULL;

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

    view->total_size = total_size;
    view->flags = flags;
    view->frame_index = frame_index;
    view->chunk_count = chunk_count;
    view->unknown_chunks_skipped = unknown;
    view->gxcm = data + gxcm_offset;
    view->rsta = seen_rsta ? data + rsta_offset : NULL;
    view->rsta_count = 0;
    view->meta_backend_id = 0;
    view->meta_coverage_flags = 0;

    result = validate_gxcm(view->gxcm, gxcm_size, flags, &view->gxcm_count);
    if (result != TWLHD_V2_OK) return result;

    if (seen_rsta) {
        result = validate_rsta(view->rsta, rsta_size, view->gxcm_count,
                               &view->rsta_count);
        if (result != TWLHD_V2_OK) return result;
    }

    result = validate_meta(data + meta_offset, meta_size,
                           &view->meta_backend_id,
                           &view->meta_coverage_flags);
    if (result != TWLHD_V2_OK) return result;

    return TWLHD_V2_OK;
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
