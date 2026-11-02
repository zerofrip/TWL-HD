// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 5A-2 - DS clip space to PICA200 clip space fixup matrix.
 *
 * See common/twlhd_clipfix.h for the derivation and API contract.
 */

#include "twlhd_clipfix.h"

#include "twlhd_gx_interp.h"

void twlhd_clipfix_matrix(const TwlhdGxViewport *vp, float m[4][4]) {
    uint8_t x1 = 0, y1 = 0, x2 = 255, y2 = 191;
    int width;
    int height;
    float hw, hh, cx, cy;

    if (vp != NULL && vp->present) {
        x1 = vp->x1;
        y1 = vp->y1;
        x2 = vp->x2;
        y2 = vp->y2;
    }

    /* Unsigned wraparound rule: the DS stores the exclusive edge bytes. */
    width = (int)(uint8_t)(x2 - x1) + 1;
    height = (int)(uint8_t)(y2 - y1) + 1;
    hw = (float)width / 256.0f;
    hh = (float)height / 192.0f;
    cx = (float)(2 * (int)x1 + width) / 256.0f - 1.0f;
    cy = (float)(2 * (int)y1 + height) / 192.0f - 1.0f;

    m[0][0] = 0.0f;  m[0][1] = hh;    m[0][2] = 0.0f;  m[0][3] = cy;
    m[1][0] = -0.8f * hw; m[1][1] = 0.0f; m[1][2] = 0.0f; m[1][3] = -0.8f * cx;
    m[2][0] = 0.0f;  m[2][1] = 0.0f;  m[2][2] = 0.5f;  m[2][3] = -0.5f;
    m[3][0] = 0.0f;  m[3][1] = 0.0f;  m[3][2] = 0.0f;  m[3][3] = 1.0f;
}
