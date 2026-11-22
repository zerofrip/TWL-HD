// SPDX-License-Identifier: Apache-2.0
//
// ds-vtxcolor-test: static DS top-screen probe for the hardware's *initial*
// current vertex colour.  Quad A is drawn with the GX state the ROM inherits
// (no glColor before its vertices); quads B..E are explicit reference colours.
//
// Only libnds DS 3D is used; no TWL-HD GX code, no animation.
//
// The projection call and the raw integer vertices mirror libnds gl2d.c
// (SetOrtho / gxVertex3i), which documents a 1:1 vertex-to-screen mapping:
// glOrthof32 takes its extents as plain pixel counts and the vertex is written
// unshifted, so vertex (x, y) lands on DS pixel (x, y).

#include <nds.h>
#include <stdio.h>

#include "quads.h"

#if defined(DSVTX_VARIANT_RESET)
#define VARIANT_LABEL "after-empty-frame"
#else
#define VARIANT_LABEL "first-frame"
#endif

// libnds GL2D's gxVertex3i: raw 16-bit vertex, no 4.12 shift.
static inline void vertex_px(int x, int y) {
    glVertex3v16((v16)x, (v16)y, 0);
}

static void setup_projection(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Integer pixel extents (libnds GL2D SetOrtho convention): x 0..256,
    // y 0..192 with top=0 so +y is down the screen.  See README section 4.
    glOrthof32(0, 256, 192, 0, -1 << 12, 1 << 12);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// The list is submitted once.  Re-submitting it every frame would leave the
// current vertex colour at qE's red (the GX colour state persists across
// display lists), so quad A would no longer show the default colour.  With no
// further GFX_FLUSH the first frame stays displayed (README section 2).
// Order is quads.json order: A (no colour write), then B, C, D, E.
static void draw_quads(void) {
    const VtxQuad *q;
    int i;

    glBegin(GL_QUADS);
    for (i = 0; i < (int)VTX_QUAD_COUNT; i++) {
        q = &VTX_QUADS[i];
        if (q->has_colour)
            glColor(RGB15(q->r5, q->g5, q->b5));
        vertex_px(q->x, q->y);
        vertex_px(q->x + q->w, q->y);
        vertex_px(q->x + q->w, q->y + q->h);
        vertex_px(q->x, q->y + q->h);
    }
    glEnd();
}

int main(void) {
    videoSetMode(MODE_0_3D);
    glInit();

    // glInit() already leaves GFX_CONTROL = 0 (README section 3); spell the
    // disabled features out.  Lighting has no GFX_CONTROL bit: it is enabled
    // per polygon by the POLY_FORMAT_LIGHT0..3 bits, which stay 0.
    glDisable(GL_TEXTURE_2D | GL_FOG | GL_OUTLINE | GL_ANTIALIAS);

    glClearColor(DSVTX_CLEAR_R, DSVTX_CLEAR_G, DSVTX_CLEAR_B, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    setup_projection();
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);

#if defined(DSVTX_VARIANT_RESET)
    // VARIANT=2: display one empty frame before quad A, so the owner can
    // compare "first frame" with "after an empty displayed frame".  This is a
    // display-list-level reset, not a power-on reset (README section 6).
    glResetMatrixStack();
    glFlush(0);
    swiWaitForVBlank();
    swiWaitForVBlank();
    setup_projection();
#endif

    consoleDemoInit();  // sub engine, VRAM_C, BG0
    iprintf("\n  DS VTXCOLOR TEST\n  id: %s\n  variant: %s\n",
            DSVTX_ID, VARIANT_LABEL);

    draw_quads();
    glFlush(0);

    // No further GL submissions: the first frame (with quad A in the default
    // colour) stays on screen.
    while (1)
        swiWaitForVBlank();

    return 0;
}
