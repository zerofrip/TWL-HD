// SPDX-License-Identifier: Apache-2.0
//
// ds-edge-test: static DS top-screen probe that anchors the 3D frame origin so
// a one-row placement difference between real hardware and DeSmuME can be
// classified (whole-frame shift versus per-primitive rasterisation offset).
//
// One opaque black full-screen backdrop and ten opaque white polygons: bands
// touching the top/bottom scanlines and the left/right columns, four 32x32
// interior squares, and one 1-pixel-high and one 1-pixel-wide interior line.
// No alpha, texture, fog, edge marking, antialiasing or lighting is used.
//
// Only libnds DS 3D is used; no TWL-HD GX code, no animation.
//
// Projection and raw integer vertices mirror libnds gl2d.c (SetOrtho /
// gxVertex3i) and tools/ds_alpha_test/source/main.c: vertex (x, y) is the
// nominal DS pixel corner (x, y), so a quad covers the half-open vertex
// rectangle [x, x+w) x [y, y+h).  The DS rasteriser fill rule itself is not
// derived in this ROM; README section 3 states that limit.

#include <nds.h>

#include "quads.h"

// libnds GL2D's gxVertex3i: raw 16-bit vertex, no 4.12 shift.  z is passed
// through so the backdrop (z=0) and the white polygons (z=+64) get different
// depth values.  A larger source z maps to a smaller post-transform z, which
// is nearer under the default LESS depth test (same relation as ds_alpha_test).
static inline void vertex_px(int x, int y, int z) {
    glVertex3v16((v16)x, (v16)y, (v16)z);
}

static void setup_projection(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Integer pixel extents (libnds GL2D SetOrtho convention): x 0..256,
    // y 0..192 with top=0 so +y is down the screen.
    glOrthof32(0, 256, 192, 0, -1 << 12, 1 << 12);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_poly(const EdgePoly *p) {
    // Distinct polygon id, no culling, no fog, no lighting (POLY_FORMAT_LIGHT0..3
    // stay 0).  POLY_ALPHA(31) is fully opaque and GL_BLEND stays disabled.
    glPolyFmt(POLY_ALPHA(p->alpha) | POLY_CULL_NONE | POLY_ID(p->poly_id));
    glColor(RGB15(p->r5, p->g5, p->b5));
    glBegin(GL_QUADS);
    vertex_px(p->x, p->y, p->z);
    vertex_px(p->x + p->w, p->y, p->z);
    vertex_px(p->x + p->w, p->y + p->h, p->z);
    vertex_px(p->x, p->y + p->h, p->z);
    glEnd();
}

// Opaque backdrop first, then the opaque white polygons at a larger source z
// (+64), which lands nearer under the default LESS depth test, so they stay
// visible.  The list is submitted once; the DS keeps scanning out the last
// swapped buffer.
static void draw_scene(void) {
    int i;
    for (i = 0; i < (int)EDGE_BACKDROP_COUNT; i++)
        draw_poly(&EDGE_BACKDROPS[i]);
    for (i = 0; i < (int)EDGE_QUAD_COUNT; i++)
        draw_poly(&EDGE_QUADS[i]);
}

int main(void) {
    videoSetMode(MODE_0_3D);
    glInit();

    // glInit() leaves GFX_CONTROL = 0, so blending is already off; disable it
    // explicitly and disable the features this test must not use: textures,
    // alpha test, antialiasing, edge marking, fog.  GL_TOON_HIGHLIGHT stays 0
    // and lighting is per-polygon (POLY_FORMAT_LIGHT0..3 = 0).
    glDisable(GL_BLEND | GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS |
              GL_OUTLINE | GL_FOG);

    glClearColor(DSEDGE_CLEAR_R, DSEDGE_CLEAR_G, DSEDGE_CLEAR_B, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    setup_projection();

    consoleDemoInit();  // sub engine, VRAM_C, BG0; no console output written

    draw_scene();
    glFlush(0);

    while (1)
        swiWaitForVBlank();

    return 0;
}
