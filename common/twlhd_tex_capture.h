// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5C-1 - pure-C texture capture planner.
 *
 * Allocation-free, deterministic, no libc beyond <string.h>/<stdint.h>, no
 * float. It walks a recorder GXCM command list, finds the distinct
 * (TEXIMAGE_PARAM, PLTT_BASE) keys a draw actually uses, computes their
 * TXDT/PLDT byte needs with the shared format helpers, applies the capture
 * budget with a whole-record first-fit drop policy, and produces a plan that
 * fills caller-owned TXDT/PLDT/TXDS buffers through an abstract read callback.
 *
 * BUILD/UNIT only. The planner does not touch the recorder (that is 5C-2),
 * MMIO, VRAM or any format flag. See docs/TEXTURE_CAPTURE_DESIGN.md,
 * "Status 5C-1".
 *
 * Key rules (design section 3.1):
 *   - the current TEXIMAGE_PARAM/PLTT_BASE pair is a key;
 *   - it counts only when a VTX command (0x23..0x28) executes inside a
 *     BEGIN_VTXS group while the pair is current;
 *   - packed format 0 (untextured) is never captured;
 *   - keys are deduplicated by the exact (param, pltt) pair within this frame.
 *     There is no content dedupe: two different keys with identical bytes are
 *     kept twice, because TXDS records are keyed, not content-addressed
 *     (design section 3.2).
 *
 * Same-key/different-bytes (D7): the plan emits one record per key, in
 * first-use stream order, and apply reads that record's bytes exactly once.
 * The first-use record is therefore the only snapshot. The planner does not
 * hash bytes (design section 3.1 leaves the hash as an unapproved option); it
 * counts a conflict when either texture register was re-written between two
 * uses of the same key.
 *
 * Read callback: return 0 on success, nonzero on failure. dst != NULL copies
 * exactly `bytes` bytes; dst == NULL is an availability probe that copies
 * nothing and only reports whether the range is readable. The plan pass uses
 * probes and, for format 5, real 2-byte index reads (the minimal palette window
 * needs the index content); twlhd_tex_capture_layout() is the read-free sizing
 * call. apply never re-decides the kept set.
 */
#pragma once

#include <stdint.h>

#include "twlhd_v2_format.h"
#include "twlhd_v2_write.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed key/record capacity; a 257th distinct key is budget-dropped. */
#define TWLHD_TEX_CAP_KEY_MAX TWLHD_V2_TXDS_MAX_RECORDS

/*
 * Kind of source bytes. Design section 5.1 had no kind parameter; the DS has
 * three disjoint address spaces (texel slot, palette slot, 4x4 index block),
 * so 5C-2's backend needs it to route. This is a deliberate refinement.
 */
#define TWLHD_TEX_CAP_READ_TEXEL 0u   /* offset = (param & 0xFFFF) << 3        */
#define TWLHD_TEX_CAP_READ_PALETTE 1u /* offset = pltt << (format 2 ? 3 : 4)  */
#define TWLHD_TEX_CAP_READ_INDEX 2u   /* offset = indexBase + (param & 0x3FFF)
                                       * << 2; indexBase is 0x20000, or 0x30000
                                       * when (param & 0xC000) == 0x8000      */

typedef int (*TwlhdTexCapReadFn)(void *ctx, uint32_t kind, uint32_t offset,
                                 uint32_t bytes, uint8_t *dst);

/* One kept key, in the exact form TXDS needs (and the writer consumes). */
typedef struct {
    uint32_t teximage_param;
    uint32_t pltt_base;
    uint32_t texel_bytes;
    uint32_t palette_bytes;
    uint32_t index_bytes;
    uint32_t texel_offset;   /* TXDT-relative; 0 when texel_bytes == 0     */
    uint32_t palette_offset; /* PLDT-relative; 0 when palette_bytes == 0   */
    uint32_t index_offset;   /* TXDT-relative; 0 when index_bytes == 0     */
} TwlhdTexCapRecord;

/* Totals. overflow is a plan field only; 5C-2 maps it to the format flag. */
typedef struct {
    uint32_t keys_seen;
    uint32_t keys_kept;
    uint32_t keys_dropped_budget;
    uint32_t keys_dropped_read_error;
    uint32_t keys_dropped_unsupported;
    uint32_t conflicts_same_key;
    uint32_t bytes_txdt;
    uint32_t bytes_pldt; /* sum of palette bytes, before the writer's 4-pad */
    int overflow;
} TwlhdTexCapStats;

typedef struct {
    uint32_t max_txdt_bytes;
    uint32_t max_pldt_bytes;
    uint32_t max_records;
} TwlhdTexCapLimits;

/*
 * One plan. It owns the kept-record array, so the caller-provided capacity is
 * TWLHD_TEX_CAP_KEY_MAX. limits holds the effective (clamped) caps the pass
 * actually used.
 */
typedef struct {
    TwlhdTexCapLimits limits;
    TwlhdTexCapStats stats;
    uint32_t record_count;
    TwlhdTexCapRecord records[TWLHD_TEX_CAP_KEY_MAX];
} TwlhdTexCapPlan;

typedef enum {
    TWLHD_TEX_CAP_OK = 0,
    TWLHD_TEX_CAP_ERR_NULL,     /* a required pointer is NULL */
    TWLHD_TEX_CAP_ERR_CAPACITY, /* an output buffer is too small */
    TWLHD_TEX_CAP_ERR_READ,     /* a source probe/read failed */
    TWLHD_TEX_CAP_ERR_PLAN      /* not a plan produced by this module */
} TwlhdTexCapResult;

/*
 * Compile-time budgets: TXDT 131,072 B + PLDT 32,768 B + 256 records
 * (design section 3.4 / D5). The full variant uses the format caps from
 * twlhd_v2_format.h and is meant for tests and the full-cap build.
 * NULL is ignored.
 */
void twlhd_tex_capture_default_limits(TwlhdTexCapLimits *out);
void twlhd_tex_capture_full_limits(TwlhdTexCapLimits *out);

/*
 * Build a plan from the recorder's GXCM entries. No output byte is copied; the
 * pass only probes availability and reads format-5 index entries. keys_seen
 * counts distinct used keys, including those dropped later. Limits are clamped
 * to the format caps. On ERR_NULL *out is untouched. gxcm must be non-NULL even
 * when gxcm_count is 0.
 */
TwlhdTexCapResult twlhd_tex_capture_plan(const TwlhdTexCapLimits *limits,
                                         const TwlhdV2CmdEntry *gxcm,
                                         uint32_t gxcm_count,
                                         TwlhdTexCapReadFn read, void *read_ctx,
                                         TwlhdTexCapPlan *out);

/*
 * Read-free sizing pass: recompute every record offset and the two byte totals
 * from the kept set, mirroring the writer's running sums (including the
 * zero-offset rule for zero-size blocks). Idempotent. Returns ERR_NULL for a
 * NULL plan and ERR_CAPACITY when record_count exceeds the fixed capacity.
 */
TwlhdTexCapResult twlhd_tex_capture_layout(TwlhdTexCapPlan *plan);

/*
 * Fill the caller's TXDT/PLDT bytes and TwlhdV2TexEntry[] from an immutable
 * plan. The kept set is not re-decided. Capacities are exact byte/entry counts.
 * All NULL/capacity/plan-consistency errors are detected before the first
 * write, so those error paths leave every output untouched; a source read
 * failure is also probed before the first write for the same reason. A backend
 * that fails a real read after a successful probe leaves partial output and
 * returns ERR_READ (the backend is expected to be stable between probe and
 * copy).
 */
TwlhdTexCapResult twlhd_tex_capture_apply(const TwlhdTexCapPlan *plan,
                                          TwlhdTexCapReadFn read, void *read_ctx,
                                          uint8_t *txdt, uint32_t txdt_capacity,
                                          uint8_t *pldt, uint32_t pldt_capacity,
                                          TwlhdV2TexEntry *entries,
                                          uint32_t entries_capacity);

const char *twlhd_tex_capture_result_string(TwlhdTexCapResult result);

#ifdef __cplusplus
}
#endif
