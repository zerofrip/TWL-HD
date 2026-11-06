// SPDX-License-Identifier: Apache-2.0
/*
 * Pure, header-only helpers for tools/azahar_host/azahar_host.c.
 *
 * Kept free of libretro and libc extensions so tests/host can include this
 * header under the same -std=c11 -Wall -Wextra -Werror flags as the rest of
 * the host suite.
 */
#pragma once

#include <limits.h>
#include <stddef.h>
#include <string.h>

/*
 * Button ids are the libretro RETRO_DEVICE_ID_JOYPAD_* numbers. They are
 * transcribed here so this header stays independent of libretro.h; the host
 * source includes the real header (or the minimal mirror) and both use the
 * same values. See externals/libretro-common/.../libretro.h in Azahar.
 */
#define AH_BTN_B 0
#define AH_BTN_Y 1
#define AH_BTN_SELECT 2
#define AH_BTN_START 3
#define AH_BTN_UP 4
#define AH_BTN_DOWN 5
#define AH_BTN_LEFT 6
#define AH_BTN_RIGHT 7
#define AH_BTN_A 8
#define AH_BTN_X 9
#define AH_BTN_L 10
#define AH_BTN_R 11

/*
 * Convert an XRGB8888 (little-endian B,G,R,X) buffer to packed top-down RGB24.
 *
 * pitch_bytes is the distance between scanlines. A zero pitch means 4 * width.
 * A negative pitch means the source rows are stored bottom-up; the output is
 * still written top-down. Returns 0 on success, -1 for null/empty input or a
 * pitch too small to hold one row.
 */
static inline int xrgb8888_to_rgb24(const unsigned char *src, unsigned char *dst,
                                    unsigned width, unsigned height,
                                    long pitch_bytes) {
    size_t stride;
    unsigned y;

    if (src == NULL || dst == NULL || width == 0 || height == 0) {
        return -1;
    }
    if (pitch_bytes == 0) {
        pitch_bytes = (long)width * 4;
    }
    if (pitch_bytes < 0) {
        /* -(LONG_MIN) is undefined; this form is not. */
        stride = (size_t)(-(pitch_bytes + 1)) + 1u;
    } else {
        stride = (size_t)pitch_bytes;
    }
    if (stride < (size_t)width * 4u) {
        return -1;
    }

    for (y = 0; y < height; y++) {
        unsigned src_y = pitch_bytes < 0 ? height - 1u - y : y;
        const unsigned char *row = src + (size_t)src_y * stride;
        unsigned char *out = dst + (size_t)y * width * 3u;
        unsigned x;

        for (x = 0; x < width; x++) {
            out[x * 3u + 0u] = row[x * 4u + 2u];
            out[x * 3u + 1u] = row[x * 4u + 1u];
            out[x * 3u + 2u] = row[x * 4u + 0u];
        }
    }
    return 0;
}

/* Parse one decimal u32 at *pp, advancing past it. Returns 0 on success. */
static inline int ah_parse_u32(const char **pp, unsigned *out) {
    const char *p = *pp;
    unsigned value = 0;
    int digits = 0;

    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p - '0');
        if (value > (UINT_MAX - digit) / 10u) {
            return -1;
        }
        value = value * 10u + digit;
        digits = 1;
        p++;
    }
    if (!digits) {
        return -1;
    }
    *pp = p;
    *out = value;
    return 0;
}

/* Button name -> libretro joypad id, or -1 for an unknown name. */
static inline int ah_button_id(const char *name) {
    static const struct {
        const char *name;
        int id;
    } table[] = {
        {"B", AH_BTN_B},           {"Y", AH_BTN_Y},
        {"SELECT", AH_BTN_SELECT}, {"START", AH_BTN_START},
        {"UP", AH_BTN_UP},         {"DOWN", AH_BTN_DOWN},
        {"LEFT", AH_BTN_LEFT},     {"RIGHT", AH_BTN_RIGHT},
        {"A", AH_BTN_A},           {"X", AH_BTN_X},
        {"L", AH_BTN_L},           {"R", AH_BTN_R},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) {
            return table[i].id;
        }
    }
    return -1;
}

/*
 * Parse "NAME:from-to" (both ends inclusive), e.g. "A:60-63".
 * Returns 0 on success, -1 on any malformed input (unknown button, missing
 * separator, overflow, or from > to).
 */
static inline int parse_button_spec(const char *s, int *id, unsigned *from,
                                    unsigned *to) {
    const char *colon;
    const char *p;
    char name[16];
    size_t name_len;
    int button;
    unsigned first;
    unsigned last;

    if (s == NULL || id == NULL || from == NULL || to == NULL) {
        return -1;
    }
    colon = strchr(s, ':');
    if (colon == NULL) {
        return -1;
    }
    name_len = (size_t)(colon - s);
    if (name_len == 0 || name_len >= sizeof(name)) {
        return -1;
    }
    memcpy(name, s, name_len);
    name[name_len] = '\0';
    button = ah_button_id(name);
    if (button < 0) {
        return -1;
    }

    p = colon + 1;
    if (ah_parse_u32(&p, &first) != 0) {
        return -1;
    }
    if (*p != '-') {
        return -1;
    }
    p++;
    if (ah_parse_u32(&p, &last) != 0) {
        return -1;
    }
    if (*p != '\0' || first > last) {
        return -1;
    }

    *id = button;
    *from = first;
    *to = last;
    return 0;
}

/*
 * Parse a comma-separated frame list ("150,199") into out[0..cap-1].
 * Returns the number of entries, or -1 for empty input, a trailing comma,
 * a non-numeric entry, a u32 overflow, or more than cap entries.
 * Duplicate frame numbers are allowed (the caller writes the same file twice).
 */
static inline int parse_frame_list(const char *s, unsigned *out, size_t cap) {
    const char *p;
    size_t count = 0;

    if (s == NULL || out == NULL || cap == 0 || *s == '\0') {
        return -1;
    }

    p = s;
    for (;;) {
        unsigned value;

        if (ah_parse_u32(&p, &value) != 0) {
            return -1;
        }
        if (count >= cap) {
            return -1;
        }
        out[count++] = value;

        if (*p == '\0') {
            break;
        }
        if (*p != ',') {
            return -1;
        }
        p++;
        if (*p == '\0') {
            return -1;
        }
    }
    return (int)count;
}
