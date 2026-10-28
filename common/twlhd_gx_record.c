// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - GX recorder core.
 * See common/twlhd_gx_record.h. HARDWARE UNVERIFIED (host-tested only).
 */

#include "twlhd_gx_record.h"

#include <string.h>

#include "twlhd_v2_write.h"

static TwlhdGxRecorder g_recorder;

static void mark_overflow(TwlhdGxRecorder *rec) {
    rec->overflow = 1;
    rec->flags |= TWLHD_V2_FLAG_OVERFLOW;
}

static void append_entry(TwlhdGxRecorder *rec, uint32_t cmd_word,
                         uint32_t value) {
    if (rec->gxcm_count >= TWLHD_GX_REC_MAX_ENTRIES) {
        mark_overflow(rec);
        rec->dropped_entries++;
        return;
    }

    rec->gxcm[rec->gxcm_count].cmd_word = cmd_word;
    rec->gxcm[rec->gxcm_count].value = value;
    rec->gxcm_count++;
}

static void append_rsta(TwlhdGxRecorder *rec, uint32_t addr, uint32_t value,
                        uint32_t width) {
    if (rec->rsta_count >= TWLHD_GX_REC_MAX_RSTA) {
        mark_overflow(rec);
        rec->dropped_rsta++;
        return;
    }

    rec->rsta[rec->rsta_count].addr = addr;
    rec->rsta[rec->rsta_count].value = value;
    rec->rsta[rec->rsta_count].gx_index = rec->gxcm_count;
    rec->rsta[rec->rsta_count].width = width;
    rec->rsta_count++;
}

void twlhd_gx_rec_init(TwlhdGxRecorder *rec) {
    if (!rec) return;

    memset(rec, 0, sizeof *rec);
    rec->backend_id = TWLHD_V2_BACKEND_REGISTER_PROXY;
    rec->coverage_flags = TWLHD_V2_COVERAGE_LIBRARY_INTERNAL_WRITES |
                          TWLHD_V2_COVERAGE_DMA_DISPLAY_LISTS |
                          TWLHD_V2_COVERAGE_INITIAL_STATE_UNKNOWN;
}

void twlhd_gx_rec_arm(TwlhdGxRecorder *rec) {
    if (!rec) return;

    rec->gxcm_count = 0;
    rec->rsta_count = 0;
    rec->flags = 0;
    rec->overflow = 0;
    rec->ignored_writes = 0;
    rec->invalid_writes = 0;
    rec->dropped_entries = 0;
    rec->dropped_rsta = 0;
    rec->armed = 1;
    rec->frame_index++;
}

void twlhd_gx_rec_write(TwlhdGxRecorder *rec, uint32_t addr, uint32_t value,
                        uint32_t width) {
    uint32_t id;

    if (!rec) return;

    if (!rec->armed) {
        rec->ignored_writes++;
        return;
    }

    if (addr == TWLHD_GX_REC_FIFO_ADDR) {
        if (width != 4u) {
            rec->ignored_writes++;
            return;
        }
        append_entry(rec, TWLHD_V2_RAW_FIFO_BIT, value);
        return;
    }

    if (addr >= TWLHD_GX_REC_PORT_FIRST && addr <= TWLHD_GX_REC_PORT_LAST) {
        if (width != 4u || (addr & 3u) != 0u) {
            rec->invalid_writes++;
            return;
        }

        id = (addr - TWLHD_GX_REC_FIFO_ADDR) >> 2;
        if (!twlhd_v2_cmd_param_count(id, NULL)) {
            rec->invalid_writes++;
            return;
        }

        if (id == TWLHD_V2_CMD_SWAP_BUFFERS) {
            if (rec->overflow ||
                rec->gxcm_count >= TWLHD_GX_REC_MAX_ENTRIES) {
                /*
                 * Overflow resolution: drop the swap too, stay incomplete.
                 * Rec.counts it as a dropped entry so the loss is visible.
                 */
                if (!rec->overflow) mark_overflow(rec);
                rec->flags &= ~TWLHD_V2_FLAG_FRAME_COMPLETE;
                rec->armed = 0;
                rec->dropped_entries++;
                return;
            }

            append_entry(rec, id, value);
            rec->flags |= TWLHD_V2_FLAG_FRAME_COMPLETE;
            rec->armed = 0;
            return;
        }

        append_entry(rec, id, value);
        return;
    }

    if (twlhd_v2_rsta_whitelisted(addr, width)) {
        append_rsta(rec, addr, value, width);
        return;
    }

    rec->ignored_writes++;
}

void twlhd_gx_rec_hook(uint32_t addr, uint32_t value, uint32_t width) {
    twlhd_gx_rec_write(&g_recorder, addr, value, width);
}

TwlhdGxRecorder *twlhd_gx_rec_global(void) {
    return &g_recorder;
}

TwlhdV2Result twlhd_gx_rec_serialize(const TwlhdGxRecorder *rec, uint8_t *out,
                                     size_t capacity, size_t *out_size) {
    TwlhdV2WriteParams params;

    if (!rec) return TWLHD_V2_ERR_NULL;

    params.frame_index = rec->frame_index;
    params.flags = rec->flags;
    params.backend_id = rec->backend_id;
    params.coverage_flags = rec->coverage_flags;
    params.gxcm = rec->gxcm;
    params.gxcm_count = rec->gxcm_count;
    params.rsta = rec->rsta;
    params.rsta_count = rec->rsta_count;

    return twlhd_v2_write(&params, out, capacity, out_size);
}

uint32_t twlhd_gx_rec_entry_count(const TwlhdGxRecorder *rec) {
    return rec ? rec->gxcm_count : 0u;
}

uint32_t twlhd_gx_rec_rsta_count(const TwlhdGxRecorder *rec) {
    return rec ? rec->rsta_count : 0u;
}

uint32_t twlhd_gx_rec_flags(const TwlhdGxRecorder *rec) {
    return rec ? rec->flags : 0u;
}

uint32_t twlhd_gx_rec_frame_index(const TwlhdGxRecorder *rec) {
    return rec ? rec->frame_index : 0u;
}

uint32_t twlhd_gx_rec_ignored_writes(const TwlhdGxRecorder *rec) {
    return rec ? rec->ignored_writes : 0u;
}

uint32_t twlhd_gx_rec_invalid_writes(const TwlhdGxRecorder *rec) {
    return rec ? rec->invalid_writes : 0u;
}

uint32_t twlhd_gx_rec_dropped_entries(const TwlhdGxRecorder *rec) {
    return rec ? rec->dropped_entries : 0u;
}

uint32_t twlhd_gx_rec_dropped_rsta(const TwlhdGxRecorder *rec) {
    return rec ? rec->dropped_rsta : 0u;
}

int twlhd_gx_rec_is_armed(const TwlhdGxRecorder *rec) {
    return rec ? rec->armed : 0;
}

int twlhd_gx_rec_overflowed(const TwlhdGxRecorder *rec) {
    return rec ? rec->overflow : 0;
}
