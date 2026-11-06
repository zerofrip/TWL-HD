// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for tools/azahar_host/azahar_host_util.h.
 */

#include "azahar_host_util.h"

#include "tests_host.h"

static void put_xrgb(unsigned char *buf, size_t offset, unsigned value) {
    /* little-endian B,G,R,X: value holds 0x00RRGGBB */
    buf[offset + 0] = (unsigned char)(value & 0xffu);
    buf[offset + 1] = (unsigned char)((value >> 8) & 0xffu);
    buf[offset + 2] = (unsigned char)((value >> 16) & 0xffu);
    buf[offset + 3] = 0;
}

static void test_convert_plain(void) {
    unsigned char src[6 * 4];
    unsigned char dst[6 * 3];
    static const unsigned values[6] = {
        0x00112233u, 0x00445566u, 0x00778899u,
        0x00aabbccu, 0x00ddeeffu, 0x00123456u,
    };
    int k;

    for (k = 0; k < 6; k++) {
        put_xrgb(src, (size_t)k * 4u, values[k]);
    }
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 2, 3, 2 * 4), 0);
    for (k = 0; k < 6; k++) {
        CHECK_EQ_INT(dst[k * 3 + 0], (values[k] >> 16) & 0xffu);
        CHECK_EQ_INT(dst[k * 3 + 1], (values[k] >> 8) & 0xffu);
        CHECK_EQ_INT(dst[k * 3 + 2], values[k] & 0xffu);
    }
    test_report("ppm/plain-2x3");
}

static void test_convert_padded_pitch(void) {
    unsigned char src[2 * (2 * 4 + 8)];
    unsigned char dst[4 * 3];
    int row;
    int x;

    memset(src, 0xee, sizeof src);
    for (row = 0; row < 2; row++) {
        for (x = 0; x < 2; x++) {
            put_xrgb(src, (size_t)row * 16u + (size_t)x * 4u,
                     (unsigned)(0x10 * (row * 2 + x + 1)) << 16);
        }
    }
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 2, 2, 16), 0);
    /* Row 0 pixel 0 has R=0x10, pixel 1 has R=0x20; row 1 has 0x30/0x40. */
    CHECK_EQ_INT(dst[0], 0x10);
    CHECK_EQ_INT(dst[3], 0x20);
    CHECK_EQ_INT(dst[6], 0x30);
    CHECK_EQ_INT(dst[9], 0x40);
    /* The 8 padding bytes per row must never reach the output. */
    CHECK_EQ_INT(dst[1], 0);
    CHECK_EQ_INT(dst[2], 0);
    test_report("ppm/padded-pitch");
}

static void test_convert_negative_pitch(void) {
    unsigned char src[3 * 8];
    unsigned char dst[3 * 2 * 3];

    /* Bottom-up storage: buffer row 0 is logical row 2. */
    put_xrgb(src, 0, 0x00300000u);
    put_xrgb(src, 4, 0x00300000u);
    put_xrgb(src, 8, 0x00200000u);
    put_xrgb(src, 12, 0x00200000u);
    put_xrgb(src, 16, 0x00100000u);
    put_xrgb(src, 20, 0x00100000u);

    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 2, 3, -8), 0);
    CHECK_EQ_INT(dst[0], 0x10);
    CHECK_EQ_INT(dst[3], 0x10);
    CHECK_EQ_INT(dst[6], 0x20);
    CHECK_EQ_INT(dst[9], 0x20);
    CHECK_EQ_INT(dst[12], 0x30);
    CHECK_EQ_INT(dst[15], 0x30);
    test_report("ppm/negative-pitch-bottom-up");
}

static void test_convert_rejects(void) {
    unsigned char src[16];
    unsigned char dst[16];

    memset(src, 0, sizeof src);
    CHECK_EQ_INT(xrgb8888_to_rgb24(NULL, dst, 1, 1, 4), -1);
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, NULL, 1, 1, 4), -1);
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 0, 1, 4), -1);
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 1, 0, 4), -1);
    CHECK_EQ_INT(xrgb8888_to_rgb24(src, dst, 2, 1, 4), -1);
    test_report("ppm/rejects");
}

static void test_button_specs(void) {
    int id;
    unsigned from;
    unsigned to;

    CHECK_EQ_INT(parse_button_spec("A:60-63", &id, &from, &to), 0);
    CHECK_EQ_INT(id, 8);
    CHECK_EQ_INT(from, 60);
    CHECK_EQ_INT(to, 63);

    CHECK_EQ_INT(parse_button_spec("START:0-0", &id, &from, &to), 0);
    CHECK_EQ_INT(id, 3);
    CHECK_EQ_INT(from, 0);
    CHECK_EQ_INT(to, 0);

    CHECK_EQ_INT(parse_button_spec("R:5-6", &id, &from, &to), 0);
    CHECK_EQ_INT(id, 11);

    CHECK_EQ_INT(parse_button_spec("DOWN:7-9", &id, &from, &to), 0);
    CHECK_EQ_INT(id, 5);
    test_report("button/accepts");
}

static void test_button_specs_reject(void) {
    int id;
    unsigned from;
    unsigned to;

    CHECK_EQ_INT(parse_button_spec("Z:1-2", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:5", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:5-4", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:x-y", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:4-", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:-1-2", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:1-2x", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:1-2-3", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("a:1-2", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("", &id, &from, &to), -1);
    CHECK_EQ_INT(parse_button_spec("A:4294967296-4294967296", &id, &from, &to),
                 -1);
    test_report("button/rejects");
}

static void test_frame_lists(void) {
    unsigned out[8];

    memset(out, 0, sizeof out);
    CHECK_EQ_INT(parse_frame_list("199", out, 8), 1);
    CHECK_EQ_INT(out[0], 199);

    CHECK_EQ_INT(parse_frame_list("10,199", out, 8), 2);
    CHECK_EQ_INT(out[0], 10);
    CHECK_EQ_INT(out[1], 199);

    CHECK_EQ_INT(parse_frame_list("0,1,2", out, 8), 3);
    CHECK_EQ_INT(out[0], 0);
    CHECK_EQ_INT(out[1], 1);
    CHECK_EQ_INT(out[2], 2);

    CHECK_EQ_INT(parse_frame_list("7,7", out, 8), 2);
    CHECK_EQ_INT(out[0], 7);
    CHECK_EQ_INT(out[1], 7);
    test_report("frames/accepts");
}

static void test_frame_lists_reject(void) {
    unsigned out[8];

    CHECK_EQ_INT(parse_frame_list("", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("1,", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list(",1", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("1,,2", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("abc", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("-1", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("1.5", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list(" 1", out, 8), -1);
    CHECK_EQ_INT(parse_frame_list("4294967296", out, 8), -1);
    /* Exactly cap entries is fine; cap + 1 is not. */
    CHECK_EQ_INT(parse_frame_list("1,2", out, 2), 2);
    CHECK_EQ_INT(parse_frame_list("1,2,3", out, 2), -1);
    test_report("frames/rejects");
}

int main(void) {
    test_convert_plain();
    test_convert_padded_pitch();
    test_convert_negative_pitch();
    test_convert_rejects();
    test_button_specs();
    test_button_specs_reject();
    test_frame_lists();
    test_frame_lists_reject();
    return test_exit_code();
}
