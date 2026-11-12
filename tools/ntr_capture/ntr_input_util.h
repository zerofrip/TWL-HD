// SPDX-License-Identifier: Apache-2.0
/*
 * Pure, header-only helpers for tools/ntr_capture/ntr_input.c.
 *
 * No sockets here: everything is libc-only so tests/host can include this
 * header under the same -std=c11 -Wall -Wextra -Werror flags as the rest of
 * the host suite.  Protocol facts are transcribed from the MIT-licensed
 * NTRViewer-HR and Luma3DS sources (see tools/ntr_capture/README.md for the
 * file:line citations); the receiver on the console is Rosalina's input
 * redirection, which listens on UDP port 4950.
 */
#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> /* struct timespec for the scheduling helpers */

#include "ntr_capture_util.h" /* ntr_cap_put_u32le; same directory */

/* Rosalina input redirection port (ntr_rp.c:2738). Not user-selectable. */
#define NTR_INPUT_UDP_PORT 4950
#define NTR_INPUT_FRAME_SIZE 20u

/* Idle frame values (ui_input_redirection.c:467-473). */
#define NTR_INPUT_HID_PAD_IDLE 0x00000fffu
#define NTR_INPUT_TOUCH_IDLE 0x02000000u
#define NTR_INPUT_CIRCLE_PAD_IDLE 0x007ff7ffu
#define NTR_INPUT_CPP_IDLE 0x80800081u

/* Bottom screen: pixel coordinates and their 12-bit raw range. */
#define NTR_INPUT_TOUCH_MAX_X 319
#define NTR_INPUT_TOUCH_MAX_Y 239
#define NTR_INPUT_TOUCH_SCREEN_WIDTH 320
#define NTR_INPUT_TOUCH_SCREEN_HEIGHT 240
#define NTR_INPUT_TOUCH_RANGE 0x0fffu

/* CLI limits. */
#define NTR_INPUT_HOLD_MS_DEFAULT 150
#define NTR_INPUT_HOLD_MS_MIN 50
#define NTR_INPUT_HOLD_MS_MAX 5000
#define NTR_INPUT_REPEAT_DEFAULT 1
#define NTR_INPUT_REPEAT_MAX 20
#define NTR_INPUT_GAP_MS_DEFAULT 250
#define NTR_INPUT_GAP_MS_MAX 60000
#define NTR_INPUT_RELEASE_FRAMES_DEFAULT 6
#define NTR_INPUT_RELEASE_FRAMES_MAX 600

/* --repeat bounds: the CLI's only range check, kept pure so tests/host can
 * pin it without opening a socket. */
static inline int ntr_input_repeat_ok(long v) {
    return v >= 1 && v <= NTR_INPUT_REPEAT_MAX;
}

/* Strict decimal parse, range-checked: -1 on empty, non-numeric, trailing
 * junk, overflow or out of [min, max]. Shared by every numeric CLI option. */
static inline int ntr_input_parse_int(const char *s, int min, int max,
                                      int *out) {
    char *end = NULL;
    long v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > max) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

/* ui_input_redirection.c:481 encodes (1 << 24) | (y << 12) | x, and the
 * viewer feeds it TOUCH_SCREEN_COORD_RANGE (0xfff) scaled values
 * (ui_input_redirection.c:383,417-429,478-481). At a 1:1 window
 * (ctx_width=320, ctx_height=240) the viewer's expression reduces to
 * (0xfff * pixel + dimension / 2) / dimension: round-to-nearest, then
 * clamped to 0xfff. The console's HID module converts those 12-bit raw
 * coordinates to pixels. */
static inline uint32_t ntr_input_touch_raw(int pixel, int dimension) {
    uint32_t raw = ((uint32_t)pixel * NTR_INPUT_TOUCH_RANGE +
                    (uint32_t)dimension / 2u) /
                   (uint32_t)dimension;

    return raw > NTR_INPUT_TOUCH_RANGE ? NTR_INPUT_TOUCH_RANGE : raw;
}

/*
 * hidPad bit index for a whitelisted button name, or -1.
 * hidPad is the HID module's active-low pad register: a cleared bit is a
 * pressed button (ntr-hr/include/func.h:34, ui_input_redirection.c:651-703).
 */
static inline int ntr_input_button_bit(const char *name) {
    static const struct {
        const char *name;
        unsigned bit;
    } buttons[] = {
        {"A", 0},     {"B", 1},      {"SELECT", 2}, {"START", 3},
        {"RIGHT", 4}, {"LEFT", 5},   {"UP", 6},     {"DOWN", 7},
        {"R", 8},     {"L", 9},      {"X", 10},     {"Y", 11},
    };
    size_t i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (strcmp(name, buttons[i].name) == 0) {
            return (int)buttons[i].bit;
        }
    }
    return -1;
}

/* Parse "A,B,X" into a pressed-button mask of (1 << bit) values, or -1. */
static inline int ntr_input_parse_buttons(const char *list, uint32_t *mask) {
    char name[8]; /* longest whitelisted name is "SELECT" */
    size_t len = 0;
    uint32_t out = 0;

    if (list == NULL || mask == NULL) {
        return -1;
    }
    for (;;) {
        char c = *list;
        if (c == ',' || c == '\0') {
            int bit;
            if (len == 0) {
                return -1;
            }
            name[len] = '\0';
            bit = ntr_input_button_bit(name);
            if (bit < 0) {
                return -1;
            }
            out |= 1u << (unsigned)bit;
            len = 0;
            if (c == '\0') {
                break;
            }
        } else {
            if (len + 1 >= sizeof(name)) {
                return -1;
            }
            name[len++] = c;
        }
        list++;
    }
    *mask = out;
    return 0;
}

/*
 * Build one 20-byte input redirection frame:
 * hidPad, touchScreenState, circlePadState, cppState, interfaceButtons,
 * little-endian (ntr_rp.h:6-12; the viewer memcpy()s the native struct and
 * the reference build is little-endian).
 *
 * `interfaceButtons` has no parameter on purpose: HOME/POWER and the long
 * POWER press are system-level actions, so there is no code path that can
 * make the field nonzero. Returns -1 on a touch coordinate outside the
 * bottom screen or a mask with bits above the 12 pad bits.
 */
static inline int ntr_input_build_frame(uint8_t out[NTR_INPUT_FRAME_SIZE],
                                        uint32_t hid_pad_pressed_mask,
                                        int touch_x, int touch_y,
                                        int touch_valid) {
    uint32_t touch = NTR_INPUT_TOUCH_IDLE;

    if (out == NULL || (hid_pad_pressed_mask & ~NTR_INPUT_HID_PAD_IDLE) != 0u) {
        return -1;
    }
    if (touch_valid) {
        if (touch_x < 0 || touch_x > NTR_INPUT_TOUCH_MAX_X || touch_y < 0 ||
            touch_y > NTR_INPUT_TOUCH_MAX_Y) {
            return -1;
        }
        touch = (1u << 24) |
                (ntr_input_touch_raw(touch_y, NTR_INPUT_TOUCH_SCREEN_HEIGHT)
                 << 12) |
                ntr_input_touch_raw(touch_x, NTR_INPUT_TOUCH_SCREEN_WIDTH);
    }
    ntr_cap_put_u32le(out + 0, NTR_INPUT_HID_PAD_IDLE & ~hid_pad_pressed_mask);
    ntr_cap_put_u32le(out + 4, touch);
    ntr_cap_put_u32le(out + 8, NTR_INPUT_CIRCLE_PAD_IDLE);
    ntr_cap_put_u32le(out + 12, NTR_INPUT_CPP_IDLE);
    ntr_cap_put_u32le(out + 16, 0); /* interfaceButtons: always 0 */
    return 0;
}

/* Strict dotted-quad IPv4; no hostnames and no DNS resolution. */
static inline int ntr_input_validate_ipv4(const char *s) {
    struct in_addr addr;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    return inet_pton(AF_INET, s, &addr) == 1 ? 1 : 0;
}

static inline int ntr_input_clamp(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/* Number of ~60 Hz frames that cover hold_ms, rounded up. */
static inline int ntr_input_hold_frames(int hold_ms) {
    return (hold_ms * 60 + 999) / 1000;
}

/* One ~60 Hz frame period in nanoseconds: 1e9/60 rounded to the nearest ns,
 * matching the viewer's 60 Hz input timer (main.c:883). */
#define NTR_INPUT_FRAME_NS 16666667L

/* Advance a monotonic deadline by `ns`, carrying whole seconds out of tv_nsec.
 * This is the frame-to-frame arithmetic wait_next_frame() uses. */
static inline void ntr_input_deadline_advance(struct timespec *t, long ns) {
    t->tv_nsec += ns;
    while (t->tv_nsec >= 1000000000L) {
        t->tv_nsec -= 1000000000L;
        t->tv_sec++;
    }
}

/* Re-base the frame deadline onto `now` at the start of an iteration. Without
 * this, every iteration after the first would send its frames back-to-back:
 * the previous iteration's deadlines are already in the past. */
static inline void ntr_input_iter_rebase(struct timespec *next,
                                         const struct timespec *now) {
    *next = *now;
}

/* Earliest start of the next iteration: one full iteration (hold + release
 * frames at the frame period) plus the inter-iteration gap. */
static inline void ntr_input_iter_advance(struct timespec *t, int hold_frames,
                                          int release_frames, int gap_ms) {
    long ns = (long)(hold_frames + release_frames) * NTR_INPUT_FRAME_NS +
              (long)gap_ms * 1000000L;
    ntr_input_deadline_advance(t, ns);
}
