// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A - sequence playback state machine.
 *
 * Pure C11, allocation-free, header-only. Tracks the next frame index,
 * wrap-to-zero behaviour, loop count and the first-pass frame count for the
 * sdmc:/twlhd_seq/seq_NNN.bin playback mode.
 *
 * BUILD/UNIT only: this module has no hardware or emulator claims.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_PERIOD     20u
#define SEQ_MAX_FRAMES 64u

typedef struct {
    uint32_t index;           /* next frame to load, 0 .. SEQ_MAX_FRAMES-1 */
    uint32_t displayed;       /* last successfully loaded frame */
    uint32_t loop_count;      /* increments every time playback wraps */
    uint32_t first_pass_count; /* 0 until the first wrap, then frames in pass */
} TwlhdSeqState;

/* Reset playback to the first frame. */
static inline void twlhd_seq_reset(TwlhdSeqState *s) {
    s->index = 0;
    s->displayed = 0;
    s->loop_count = 0;
    s->first_pass_count = 0;
}

/*
 * Format the path for `index` into `buf`/`size`.
 *
 * Returns 0 on success. Returns -1 if the buffer is NULL, zero-sized, or the
 * formatted string (including the terminating NUL) does not fit.
 */
static inline int twlhd_seq_path(char *buf, size_t size, uint32_t index) {
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }

    n = snprintf(buf, size, "sdmc:/twlhd_seq/seq_%03u.bin", (unsigned)index);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }

    return 0;
}

/*
 * Advance to the next frame. If `file_exists` is false the sequence wraps
 * immediately (e.g. a missing file ends the current pass). The loop counter is
 * incremented on every wrap and the first-pass frame count is recorded on the
 * first wrap only.
 */
static inline void twlhd_seq_advance(TwlhdSeqState *s, int file_exists) {
    if (!file_exists) {
        if (s->first_pass_count == 0) {
            s->first_pass_count = s->index;
        }
        s->index = 0;
        s->loop_count++;
        return;
    }

    s->displayed = s->index;
    if (s->index + 1 >= SEQ_MAX_FRAMES) {
        if (s->first_pass_count == 0) {
            s->first_pass_count = SEQ_MAX_FRAMES;
        }
        s->index = 0;
        s->loop_count++;
    } else {
        s->index++;
    }
}

#ifdef __cplusplus
}
#endif
