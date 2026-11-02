// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_clipfix.c.
 */

#include "twlhd_clipfix.h"

#include "tests_host.h"

static void check_row(const float row[4], const float expected[4],
                      const char *name) {
    int i;

    for (i = 0; i < 4; i++) {
        CHECK_NEAR(row[i], expected[i], 1e-6);
    }
    test_report(name);
}

static void check_matrix(const TwlhdGxViewport *vp, const float expected[4][4],
                         const char *name) {
    float m[4][4];
    int r, c;

    twlhd_clipfix_matrix(vp, m);
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            CHECK_NEAR(m[r][c], expected[r][c], 1e-6);
        }
    }
    test_report(name);
}

/* out = m * (x, y, z, w); verifies the documented transform. */
static void check_apply(const TwlhdGxViewport *vp, float x, float y, float z,
                        float w, const float expected[4], const char *name) {
    float m[4][4];
    float in[4];
    float out[4];
    float row[4];
    int r, c;

    twlhd_clipfix_matrix(vp, m);
    in[0] = x;
    in[1] = y;
    in[2] = z;
    in[3] = w;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            row[c] = m[r][c];
        }
        out[r] = row[0] * in[0] + row[1] * in[1] + row[2] * in[2] +
                 row[3] * in[3];
        CHECK_NEAR(out[r], expected[r], 1e-6);
    }
    test_report(name);
}

static void check_row_of(const TwlhdGxViewport *vp, int row_index,
                         const float expected[4], const char *name) {
    float m[4][4];

    twlhd_clipfix_matrix(vp, m);
    check_row(m[row_index], expected, name);
}

int main(void) {
    const TwlhdGxViewport full = {0, 0, 255, 191, 1};
    const TwlhdGxViewport left_half = {0, 0, 127, 191, 1};
    const TwlhdGxViewport centre_box = {64, 48, 191, 143, 1};
    const TwlhdGxViewport bottom_half = {0, 96, 255, 191, 1};
    const TwlhdGxViewport wrapped = {200, 0, 100, 191, 1};
    const TwlhdGxViewport y1_wrap_191 = {0, 191, 255, 191, 1};
    const TwlhdGxViewport y1_wrap_192 = {0, 192, 255, 191, 1};
    const TwlhdGxViewport y1_wrap_253 = {0, 253, 255, 191, 1};
    const TwlhdGxViewport y1_wrap_255 = {0, 255, 255, 191, 1};
    const TwlhdGxViewport y_wrap_height = {0, 150, 255, 50, 1};
    const TwlhdGxViewport unpresent_nonfull = {10, 20, 100, 150, 0};

    static const float full_matrix[4][4] = {
        {0.0f, 1.0f, 0.0f, 0.0f},
        {-0.8f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.5f, -0.5f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    static const float left_half_r0[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    static const float left_half_r1[4] = {-0.4f, 0.0f, 0.0f, 0.4f};
    static const float centre_box_r1[4] = {-0.4f, 0.0f, 0.0f, 0.0f};
    static const float bottom_half_r0[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    static const float origin_out[4] = {0.0f, 0.0f, -0.5f, 1.0f};
    static const float unit_y_out[4] = {1.0f, 0.0f, -0.5f, 1.0f};
    static const float unit_x_out[4] = {0.0f, -0.8f, -0.5f, 1.0f};
    static const float neg_w_z_out[4] = {0.0f, 0.0f, -1.0f, 1.0f};
    static const float pos_w_z_out[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float negative_w_out[4] = {1.0f, -0.8f, 1.0f, -2.0f};
    static const float wrap_r0[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    static const float wrap_r1[4] = {-0.8f * 157.0f / 256.0f, 0.0f, 0.0f,
                                     -0.8f * (557.0f / 256.0f - 1.0f)};
    /* y1 > 191 wraps by -0x00FF; height stays the inclusive byte delta. */
    static const float y1_191_r0[4] = {0.0f, 1.0f / 192.0f, 0.0f,
                                       191.0f / 192.0f};
    static const float y1_192_r0[4] = {0.0f, 256.0f / 192.0f, 0.0f,
                                       -62.0f / 192.0f};
    static const float y1_253_r0[4] = {0.0f, 195.0f / 192.0f, 0.0f,
                                       -1.0f / 192.0f};
    static const float y1_255_r0[4] = {0.0f, 193.0f / 192.0f, 0.0f,
                                       1.0f / 192.0f};
    static const float y_wrap_height_r0[4] = {0.0f, 157.0f / 192.0f, 0.0f,
                                              265.0f / 192.0f};

    /* (a) NULL viewport means the full DS viewport; all 16 elements. */
    check_matrix(NULL, full_matrix, "clipfix/full-matrix-null");
    check_matrix(&full, full_matrix, "clipfix/full-matrix-present");

    /* (b) Left half of the screen: hw=0.5, cx=-0.5, hh=1, cy=0. */
    check_row_of(&left_half, 0, left_half_r0, "clipfix/left-half-r0");
    check_row_of(&left_half, 1, left_half_r1, "clipfix/left-half-r1");

    /* (c) Centred 128x96 box: hw=hh=0.5, cx=cy=0. */
    check_row_of(&centre_box, 1, centre_box_r1, "clipfix/centre-box-r1");

    /* (c) Bottom half (y from the bottom): hh=0.5, cy=+0.5. */
    check_row_of(&bottom_half, 0, bottom_half_r0, "clipfix/bottom-half-r0");

    /* (e) Apply the full-viewport matrix. */
    check_apply(NULL, 0.0f, 0.0f, 0.0f, 1.0f, origin_out,
                "clipfix/apply-origin");
    check_apply(NULL, 0.0f, 1.0f, 0.0f, 1.0f, unit_y_out,
                "clipfix/apply-unit-y");
    check_apply(NULL, 1.0f, 0.0f, 0.0f, 1.0f, unit_x_out,
                "clipfix/apply-unit-x");
    check_apply(NULL, 0.0f, 0.0f, -1.0f, 1.0f, neg_w_z_out,
                "clipfix/apply-z-minus-w");
    check_apply(NULL, 0.0f, 0.0f, 1.0f, 1.0f, pos_w_z_out,
                "clipfix/apply-z-plus-w");
    check_apply(NULL, 1.0f, 1.0f, 0.0f, -2.0f, negative_w_out,
                "clipfix/apply-negative-w");

    /* (f) x2 < x1 wraps: width = (uint8_t)(100 - 200) + 1 = 157. */
    check_row_of(&wrapped, 0, wrap_r0, "clipfix/wrap-r0");
    check_row_of(&wrapped, 1, wrap_r1, "clipfix/wrap-r1");

    /* (g) y1 > 191 wraps: y1 - 0x00FF, height from the raw bytes. */
    check_row_of(&y1_wrap_191, 0, y1_191_r0, "clipfix/y1-wrap-191");
    check_row_of(&y1_wrap_192, 0, y1_192_r0, "clipfix/y1-wrap-192");
    check_row_of(&y1_wrap_253, 0, y1_253_r0, "clipfix/y1-wrap-253");
    check_row_of(&y1_wrap_255, 0, y1_255_r0, "clipfix/y1-wrap-255");

    /* (h) y2 < y1: height = (uint8_t)(50 - 150) + 1 = 157. */
    check_row_of(&y_wrap_height, 0, y_wrap_height_r0,
                 "clipfix/y-wrap-height-cast");

    /* (i) `present` clear ignores the bytes even when they are not full. */
    check_matrix(&unpresent_nonfull, full_matrix,
                 "clipfix/full-matrix-unpresent-nonfull");

    return test_exit_code();
}
