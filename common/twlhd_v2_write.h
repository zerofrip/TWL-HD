// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - deterministic v2 capture writer.
 *
 * Serializes one GXCM + META (+ optional RSTA) container into the caller's
 * buffer, then re-parses its own output with twlhd_v2_parse(). A file that the
 * parser would reject is never reported as written.
 *
 * HARDWARE UNVERIFIED: host-tested only.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "twlhd_v2_parse.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frame_index;
    uint32_t flags;
    uint32_t backend_id;
    uint32_t coverage_flags;
    const TwlhdV2CmdEntry *gxcm;
    uint32_t gxcm_count;
    const TwlhdV2RstaEntry *rsta; /* may be NULL when rsta_count == 0 */
    uint32_t rsta_count;
} TwlhdV2WriteParams;

/*
 * Returns TWLHD_V2_OK and the exact byte count in *out_size, or an ERR_* value.
 * On error the contents of `out` are undefined. The RSTA chunk is omitted when
 * rsta_count is zero.
 */
TwlhdV2Result twlhd_v2_write(const TwlhdV2WriteParams *params, uint8_t *out,
                             size_t capacity, size_t *out_size);

#ifdef __cplusplus
}
#endif
