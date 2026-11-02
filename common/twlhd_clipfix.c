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
    int y1v = 0; /* y1 as used by cy; may be negative after the wrap rule. */
    int width;
    int height;
    float hw, hh, cx, cy;

    if (vp != NULL && vp->present) {
        x1 = vp->x1;
        y1 = vp->y1;
        x2 = vp->x2;
        y2 = vp->y2;
        /* DeSmuME GFX3D_ViewportParse (emulator-observed, not a hardware
           measurement): y1 > 191 wraps by subtracting 0x00FF. Test case:
           Homie Rollerz's character select sets y = 253 -> -2. */
        y1v = (y1 > 191) ? (int)y1 - 0x00FF : (int)y1;
    }

    /* X2/Y2 are inclusive edge bytes, hence the +1; the (uint8_t) cast makes
       the byte difference wrap at 256 the way the DS viewport registers do. */
    width = (int)(uint8_t)(x2 - x1) + 1;
    height = (int)(uint8_t)(y2 - y1) + 1;
    hw = (float)width / 256.0f;
    hh = (float)height / 192.0f;
    cx = (float)(2 * (int)x1 + width) / 256.0f - 1.0f;
    cy = (float)(2 * y1v + height) / 192.0f - 1.0f;

    m[0][0] = 0.0f;  m[0][1] = hh;    m[0][2] = 0.0f;  m[0][3] = cy;
    m[1][0] = -0.8f * hw; m[1][1] = 0.0f; m[1][2] = 0.0f; m[1][3] = -0.8f * cx;
    m[2][0] = 0.0f;  m[2][1] = 0.0f;  m[2][2] = 0.5f;  m[2][3] = -0.5f;
    m[3][0] = 0.0f;  m[3][1] = 0.0f;  m[3][2] = 0.0f;  m[3][3] = 1.0f;
}
