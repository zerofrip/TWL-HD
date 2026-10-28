// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - GX recorder core (capture backend B).
 *
 * Classifies register writes, appends GXCM/RSTA entries, detects frame
 * boundaries (SWAP_BUFFERS) and serializes the result as a v2 container.
 * Pure C11: the shim (arm9v2/include/twlhd_gx_shim.hpp) calls
 * twlhd_gx_rec_hook() for every proxied port write.
 *
 * The recorder is NOT interrupt-safe: a GX write from an interrupt handler
 * in the middle of an append can corrupt the counts. Arm/disarm and capture
 * only from thread/task context.
 *
 * glCallList (DMA display lists) bypasses the shim; captured files declare
 * that gap via TWLHD_V2_COVERAGE_DMA_DISPLAY_LISTS.
 *
 * HARDWARE UNVERIFIED: host-tested only. The DS-side behavior (every libnds
 * helper really routing through the shim) has never been run on hardware.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "twlhd_v2_format.h"
#include "twlhd_v2_parse.h"

#define TWLHD_GX_REC_MAX_ENTRIES   16384u
#define TWLHD_GX_REC_MAX_RSTA      256u

/*
 * 32 (file header) + 3 * 16 (chunk headers) + 16 (META payload)
 * + 16384 * 8 (GXCM) + 256 * 16 (RSTA) = 135264 bytes.
 *
 * Note: the task contract's text says 135248, which omits the mandatory
 * 16-byte META payload; the expression below is the actual maximum this
 * recorder can produce and is what the demo buffer is sized from.
 */
#define TWLHD_GX_REC_SERIALIZE_MAX                                           \
    (TWLHD_V2_HEADER_SIZE + 3u * TWLHD_V2_CHUNK_HEADER_SIZE +                \
     TWLHD_V2_META_PAYLOAD_SIZE +                                            \
     TWLHD_GX_REC_MAX_ENTRIES * TWLHD_V2_GXCM_ENTRY_SIZE +                   \
     TWLHD_GX_REC_MAX_RSTA * TWLHD_V2_RSTA_ENTRY_SIZE)

#define TWLHD_GX_REC_FIFO_ADDR     0x04000400u
#define TWLHD_GX_REC_PORT_FIRST    0x04000440u
#define TWLHD_GX_REC_PORT_LAST     0x040005FFu

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TwlhdV2CmdEntry gxcm[TWLHD_GX_REC_MAX_ENTRIES];
    TwlhdV2RstaEntry rsta[TWLHD_GX_REC_MAX_RSTA];
    uint32_t gxcm_count;
    uint32_t rsta_count;
    uint32_t frame_index;
    uint32_t flags;
    uint32_t backend_id;
    uint32_t coverage_flags;
    uint32_t ignored_writes;
    uint32_t invalid_writes;
    uint32_t dropped_entries;
    uint32_t dropped_rsta;
    int armed;
    int overflow;
} TwlhdGxRecorder;

/*
 * Zero the recorder and set backend_id = register proxy, coverage_flags = all
 * three known gaps, frame_index = 0.
 */
void twlhd_gx_rec_init(TwlhdGxRecorder *rec);

/*
 * Arm a new frame: clear counts, flags and counters; drop the previous frame's
 * contents; increment frame_index. The first armed frame is frame 1.
 */
void twlhd_gx_rec_arm(TwlhdGxRecorder *rec);

/*
 * Feed one register write. `width` is the access size in bytes (1, 2 or 4).
 * Not armed: ignored_writes++. Non-captured address: ignored_writes++.
 * Command window with a bad id/width: invalid_writes++.
 * Capacity exhausted: OVERFLOW is set and the entry is dropped.
 *
 * SWAP_BUFFERS resolution: a clean frame records the swap, sets
 * FRAME_COMPLETE and disarms. If any drop has occurred, the swap itself is
 * dropped (counted), OVERFLOW stays set and FRAME_COMPLETE stays clear, so
 * every producible file satisfies the parser's "FRAME_COMPLETE iff the last
 * entry is SWAP" rule.
 */
void twlhd_gx_rec_write(TwlhdGxRecorder *rec, uint32_t addr, uint32_t value,
                        uint32_t width);

/* Route a write into the process-wide recorder (used by the shim). */
void twlhd_gx_rec_hook(uint32_t addr, uint32_t value, uint32_t width);

/* The process-wide recorder instance used by twlhd_gx_rec_hook(). */
TwlhdGxRecorder *twlhd_gx_rec_global(void);

/* Serialize the current contents (same validity rules as the writer). */
TwlhdV2Result twlhd_gx_rec_serialize(const TwlhdGxRecorder *rec, uint8_t *out,
                                     size_t capacity, size_t *out_size);

uint32_t twlhd_gx_rec_entry_count(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_rsta_count(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_flags(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_frame_index(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_ignored_writes(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_invalid_writes(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_dropped_entries(const TwlhdGxRecorder *rec);
uint32_t twlhd_gx_rec_dropped_rsta(const TwlhdGxRecorder *rec);
int twlhd_gx_rec_is_armed(const TwlhdGxRecorder *rec);
int twlhd_gx_rec_overflowed(const TwlhdGxRecorder *rec);

#ifdef __cplusplus
}
#endif
