// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5C-1 - pure-C texture capture planner.
 * See common/twlhd_tex_capture.h. BUILD/UNIT only.
 */

#include "twlhd_tex_capture.h"

#include <string.h>

#define CAP_KEY_MAX TWLHD_TEX_CAP_KEY_MAX

/* Private key state. A used key occupies one table slot even when dropped. */
#define CAP_DROP_NONE 0u
#define CAP_DROP_READ 1u
#define CAP_DROP_UNSUPPORTED 2u

static uint32_t cap_align4(uint32_t value) {
    return (value + 3u) & ~3u;
}

static uint32_t cap_format(uint32_t param) {
    return (param >> 26) & 7u;
}

static uint32_t cap_texel_address(uint32_t param) {
    return (param & 0xFFFFu) << 3;
}

static uint32_t cap_palette_address(uint32_t param, uint32_t pltt) {
    /* I2 uses 8-byte palette units, every other format 16-byte units. */
    return cap_format(param) == 2u ? (pltt << 3) : (pltt << 4);
}

static uint32_t cap_index_address(uint32_t param) {
    uint32_t base = (param & 0xC000u) == 0x8000u ? 0x30000u : 0x20000u;

    return base + ((param & 0x3FFFu) << 2);
}

/*
 * Compute the format-5 palette window. The caller has already proved the whole
 * index block is available with a dst == NULL probe; this scan reads the
 * per-block 16-bit entries two bytes at a time (no scratch buffer). A content
 * read failure falls back to the full window and is not a key drop: the index
 * block itself was available, only the scan could not complete.
 *
 * The result is 4*max(entry & 0x3FFF) + 8. 4*max is a multiple of 4 and +8
 * keeps it a multiple of 4, so the window is always even (and 4-aligned); no
 * rounding step exists.
 */
static void cap_fmt5_window(const TwlhdTexCapReadFn read, void *read_ctx,
                            uint32_t param, uint32_t index_bytes,
                            uint32_t *out_window) {
    uint32_t offset = cap_index_address(param);
    uint32_t blocks = index_bytes / 2u;
    uint32_t max = 0;
    uint32_t block;

    for (block = 0; block < blocks; block++) {
        uint8_t word[2];
        uint32_t entry;

        if (read(read_ctx, TWLHD_TEX_CAP_READ_INDEX, offset + block * 2u, 2u,
                 word) != 0) {
            *out_window = TWLHD_V2_TEX_PALETTE_4X4_MAX_BYTES;
            return;
        }
        entry = (uint32_t)word[0] | ((uint32_t)word[1] << 8);
        entry &= 0x3FFFu;
        if (entry > max) max = entry;
    }
    *out_window = max * 4u + 8u;
}

void twlhd_tex_capture_default_limits(TwlhdTexCapLimits *out) {
    if (!out) return;
    out->max_txdt_bytes = 131072u;
    out->max_pldt_bytes = 32768u;
    out->max_records = TWLHD_V2_TXDS_MAX_RECORDS;
}

void twlhd_tex_capture_full_limits(TwlhdTexCapLimits *out) {
    if (!out) return;
    out->max_txdt_bytes = TWLHD_V2_TXDT_MAX_BYTES;
    out->max_pldt_bytes = TWLHD_V2_PLDT_MAX_BYTES;
    out->max_records = TWLHD_V2_TXDS_MAX_RECORDS;
}

/*
 * Classify one newly discovered key: legal sizes, static impossibility,
 * format-5 window, availability probes. Returns CAP_DROP_*.
 */
static uint32_t cap_classify_key(const TwlhdTexCapReadFn read, void *read_ctx,
                                 uint32_t param, uint32_t pltt,
                                 TwlhdTexCapRecord *rec) {
    uint32_t texel_bytes = 0;
    uint32_t palette_bytes = 0;
    uint32_t index_bytes = 0;
    uint32_t format = cap_format(param);

    (void)twlhd_v2_tex_sizes(param, &texel_bytes, &palette_bytes, &index_bytes);
    rec->teximage_param = param;
    rec->pltt_base = pltt;
    rec->texel_bytes = texel_bytes;
    rec->palette_bytes = palette_bytes;
    rec->index_bytes = index_bytes;
    rec->texel_offset = 0;
    rec->palette_offset = 0;
    rec->index_offset = 0;

    /* Static impossibility is checked before the budget (REQ 4). */
    if ((uint64_t)texel_bytes + (uint64_t)index_bytes >
            (uint64_t)TWLHD_V2_TXDT_MAX_BYTES ||
        (uint64_t)palette_bytes > (uint64_t)TWLHD_V2_PLDT_MAX_BYTES) {
        return CAP_DROP_UNSUPPORTED;
    }

    if (read(read_ctx, TWLHD_TEX_CAP_READ_TEXEL, cap_texel_address(param),
             texel_bytes, NULL) != 0) {
        return CAP_DROP_READ;
    }

    if (format == 5u) {
        uint32_t index_address = cap_index_address(param);

        if (read(read_ctx, TWLHD_TEX_CAP_READ_INDEX, index_address, index_bytes,
                 NULL) != 0) {
            return CAP_DROP_READ;
        }
        cap_fmt5_window(read, read_ctx, param, index_bytes, &palette_bytes);
        rec->palette_bytes = palette_bytes;
    }

    if (palette_bytes != 0u &&
        read(read_ctx, TWLHD_TEX_CAP_READ_PALETTE,
             cap_palette_address(param, pltt), palette_bytes, NULL) != 0) {
        return CAP_DROP_READ;
    }

    return CAP_DROP_NONE;
}

TwlhdTexCapResult twlhd_tex_capture_plan(const TwlhdTexCapLimits *limits,
                                         const TwlhdV2CmdEntry *gxcm,
                                         uint32_t gxcm_count,
                                         TwlhdTexCapReadFn read, void *read_ctx,
                                         TwlhdTexCapPlan *out) {
    uint32_t last_write_seq[CAP_KEY_MAX];
    uint8_t drop[CAP_KEY_MAX];
    uint32_t key_count = 0;
    uint32_t kept = 0;
    uint32_t txdt_used = 0;
    uint32_t pldt_used = 0;
    uint32_t write_seq = 0;
    uint32_t param = 0;
    uint32_t pltt = 0;
    uint32_t in_begin = 0;
    uint32_t i;
    TwlhdTexCapRecord *records;
    TwlhdTexCapStats *stats;

    if (!limits || !gxcm || !read || !out) return TWLHD_TEX_CAP_ERR_NULL;

    memset(&out->stats, 0, sizeof(out->stats));
    out->record_count = 0;
    out->limits = *limits;
    if (out->limits.max_txdt_bytes > TWLHD_V2_TXDT_MAX_BYTES) {
        out->limits.max_txdt_bytes = TWLHD_V2_TXDT_MAX_BYTES;
    }
    if (out->limits.max_pldt_bytes > TWLHD_V2_PLDT_MAX_BYTES) {
        out->limits.max_pldt_bytes = TWLHD_V2_PLDT_MAX_BYTES;
    }
    if (out->limits.max_records > TWLHD_V2_TXDS_MAX_RECORDS) {
        out->limits.max_records = TWLHD_V2_TXDS_MAX_RECORDS;
    }
    records = out->records;
    stats = &out->stats;

    for (i = 0; i < gxcm_count; i++) {
        uint32_t word = gxcm[i].cmd_word;
        uint32_t id;
        uint32_t key;

        if (word == TWLHD_V2_RAW_FIFO_BIT) continue;
        id = word & TWLHD_V2_CMD_ID_MASK;

        if (id == TWLHD_V2_CMD_TEXIMAGE_PARAM) {
            param = gxcm[i].value;
            write_seq++;
        } else if (id == TWLHD_V2_CMD_PLTT_BASE) {
            pltt = gxcm[i].value;
            write_seq++;
        } else if (id == TWLHD_V2_CMD_BEGIN_VTXS) {
            in_begin = 1u;
        } else if (id == TWLHD_V2_CMD_END_VTXS) {
            in_begin = 0u;
        } else if (id >= TWLHD_V2_CMD_VTX_16 && id <= TWLHD_V2_CMD_VTX_DIFF) {
            if (!in_begin || cap_format(param) == 0u) continue;

            for (key = 0; key < key_count; key++) {
                if (records[key].teximage_param == param &&
                    records[key].pltt_base == pltt) {
                    break;
                }
            }
            if (key < key_count) {
                if (last_write_seq[key] != write_seq) {
                    stats->conflicts_same_key++;
                    last_write_seq[key] = write_seq;
                }
                continue;
            }

            stats->keys_seen++;
            if (key_count >= CAP_KEY_MAX) {
                /* The table is exactly the TXDS record capacity. */
                stats->keys_dropped_budget++;
                stats->overflow = 1;
                continue;
            }
            last_write_seq[key_count] = write_seq;
            drop[key_count] =
                cap_classify_key(read, read_ctx, param, pltt,
                                 &records[key_count]);
            if (drop[key_count] == CAP_DROP_READ) {
                stats->keys_dropped_read_error++;
            } else if (drop[key_count] == CAP_DROP_UNSUPPORTED) {
                stats->keys_dropped_unsupported++;
            }
            key_count++;
        }
    }

    /*
     * Deterministic first-fit in first-use order. A record that fails is
     * dropped whole and later (possibly smaller) records are still tried. The
     * PLDT check uses the exact padded running total, not the worst-case pad.
     */
    for (i = 0; i < key_count; i++) {
        TwlhdTexCapRecord *rec = &records[i];
        uint32_t need_txdt;

        if (drop[i] != CAP_DROP_NONE) continue;
        need_txdt = rec->texel_bytes + rec->index_bytes;
        if (kept >= out->limits.max_records ||
            (uint64_t)txdt_used + (uint64_t)need_txdt >
                (uint64_t)out->limits.max_txdt_bytes ||
            cap_align4(pldt_used + rec->palette_bytes) >
                out->limits.max_pldt_bytes) {
            stats->keys_dropped_budget++;
            stats->overflow = 1;
            continue;
        }
        if (kept != i) records[kept] = *rec;
        txdt_used += need_txdt;
        pldt_used += rec->palette_bytes;
        kept++;
    }

    stats->keys_kept = kept;
    out->record_count = kept;
    return twlhd_tex_capture_layout(out);
}

TwlhdTexCapResult twlhd_tex_capture_layout(TwlhdTexCapPlan *plan) {
    uint32_t txdt = 0;
    uint32_t pldt = 0;
    uint32_t i;

    if (!plan) return TWLHD_TEX_CAP_ERR_NULL;
    if (plan->record_count > CAP_KEY_MAX) return TWLHD_TEX_CAP_ERR_CAPACITY;

    for (i = 0; i < plan->record_count; i++) {
        TwlhdTexCapRecord *rec = &plan->records[i];

        rec->texel_offset = txdt;
        txdt += rec->texel_bytes;
        rec->index_offset = rec->index_bytes != 0u ? txdt : 0u;
        txdt += rec->index_bytes;
        rec->palette_offset = rec->palette_bytes != 0u ? pldt : 0u;
        pldt += rec->palette_bytes;
    }
    plan->stats.bytes_txdt = txdt;
    plan->stats.bytes_pldt = pldt;
    return TWLHD_TEX_CAP_OK;
}

TwlhdTexCapResult twlhd_tex_capture_apply(const TwlhdTexCapPlan *plan,
                                          TwlhdTexCapReadFn read, void *read_ctx,
                                          uint8_t *txdt, uint32_t txdt_capacity,
                                          uint8_t *pldt, uint32_t pldt_capacity,
                                          TwlhdV2TexEntry *entries,
                                          uint32_t entries_capacity) {
    uint32_t txdt_used = 0;
    uint32_t pldt_used = 0;
    uint32_t i;

    if (!plan || !read) return TWLHD_TEX_CAP_ERR_NULL;
    if (plan->record_count > CAP_KEY_MAX) return TWLHD_TEX_CAP_ERR_CAPACITY;
    if (plan->stats.bytes_txdt > txdt_capacity ||
        plan->stats.bytes_pldt > pldt_capacity ||
        plan->record_count > entries_capacity) {
        return TWLHD_TEX_CAP_ERR_CAPACITY;
    }
    if (plan->stats.bytes_txdt != 0u && !txdt) return TWLHD_TEX_CAP_ERR_NULL;
    if (plan->stats.bytes_pldt != 0u && !pldt) return TWLHD_TEX_CAP_ERR_NULL;
    if (plan->record_count != 0u && !entries) return TWLHD_TEX_CAP_ERR_NULL;

    /* Re-derive the layout: an inconsistent plan is never half-applied. */
    for (i = 0; i < plan->record_count; i++) {
        const TwlhdTexCapRecord *rec = &plan->records[i];

        if (rec->texel_offset != txdt_used) return TWLHD_TEX_CAP_ERR_PLAN;
        txdt_used += rec->texel_bytes;
        if (rec->index_offset != (rec->index_bytes != 0u ? txdt_used : 0u)) {
            return TWLHD_TEX_CAP_ERR_PLAN;
        }
        txdt_used += rec->index_bytes;
        if (rec->palette_offset != (rec->palette_bytes != 0u ? pldt_used : 0u)) {
            return TWLHD_TEX_CAP_ERR_PLAN;
        }
        pldt_used += rec->palette_bytes;
    }
    if (txdt_used != plan->stats.bytes_txdt ||
        pldt_used != plan->stats.bytes_pldt) {
        return TWLHD_TEX_CAP_ERR_PLAN;
    }

    /*
     * Probe every source before the first write, so a missing source leaves
     * the caller's buffers untouched. apply never re-decides the kept set.
     */
    for (i = 0; i < plan->record_count; i++) {
        const TwlhdTexCapRecord *rec = &plan->records[i];

        if (read(read_ctx, TWLHD_TEX_CAP_READ_TEXEL,
                 cap_texel_address(rec->teximage_param), rec->texel_bytes,
                 NULL) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }
        if (rec->index_bytes != 0u &&
            read(read_ctx, TWLHD_TEX_CAP_READ_INDEX,
                 cap_index_address(rec->teximage_param), rec->index_bytes,
                 NULL) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }
        if (rec->palette_bytes != 0u &&
            read(read_ctx, TWLHD_TEX_CAP_READ_PALETTE,
                 cap_palette_address(rec->teximage_param, rec->pltt_base),
                 rec->palette_bytes, NULL) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }
    }

    for (i = 0; i < plan->record_count; i++) {
        const TwlhdTexCapRecord *rec = &plan->records[i];

        if (rec->texel_bytes != 0u &&
            read(read_ctx, TWLHD_TEX_CAP_READ_TEXEL,
                 cap_texel_address(rec->teximage_param), rec->texel_bytes,
                 txdt + rec->texel_offset) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }
        if (rec->index_bytes != 0u &&
            read(read_ctx, TWLHD_TEX_CAP_READ_INDEX,
                 cap_index_address(rec->teximage_param), rec->index_bytes,
                 txdt + rec->index_offset) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }
        if (rec->palette_bytes != 0u &&
            read(read_ctx, TWLHD_TEX_CAP_READ_PALETTE,
                 cap_palette_address(rec->teximage_param, rec->pltt_base),
                 rec->palette_bytes, pldt + rec->palette_offset) != 0) {
            return TWLHD_TEX_CAP_ERR_READ;
        }

        entries[i].teximage_param = rec->teximage_param;
        entries[i].pltt_base = rec->pltt_base;
        entries[i].texel = rec->texel_bytes != 0u
                               ? txdt + rec->texel_offset
                               : NULL;
        entries[i].palette = rec->palette_bytes != 0u
                                 ? pldt + rec->palette_offset
                                 : NULL;
        entries[i].index = rec->index_bytes != 0u
                               ? txdt + rec->index_offset
                               : NULL;
        entries[i].texel_bytes = rec->texel_bytes;
        entries[i].palette_bytes = rec->palette_bytes;
        entries[i].index_bytes = rec->index_bytes;
    }

    return TWLHD_TEX_CAP_OK;
}

const char *twlhd_tex_capture_result_string(TwlhdTexCapResult result) {
    switch (result) {
    case TWLHD_TEX_CAP_OK: return "OK";
    case TWLHD_TEX_CAP_ERR_NULL: return "ERR_NULL";
    case TWLHD_TEX_CAP_ERR_CAPACITY: return "ERR_CAPACITY";
    case TWLHD_TEX_CAP_ERR_READ: return "ERR_READ";
    case TWLHD_TEX_CAP_ERR_PLAN: return "ERR_PLAN";
    default: return "ERR_UNKNOWN";
    }
}
