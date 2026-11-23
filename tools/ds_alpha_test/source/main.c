// SPDX-License-Identifier: Apache-2.0
//
// ds-alpha-test: static DS top-screen probe that decides whether the DS 3D
// engine blends a translucent polygon with weight A1 = a/31 or A2 = (a+1)/32
// (a = 5-bit polygon alpha).  Two opaque backdrops (black left, mid-grey right)
// carry a row of translucent white quads with seven alpha values each.  Every
// quad has its own polygon id, so translucent polygons never blend into each
// other and each result depends only on its own backdrop.
//
// Only libnds DS 3D is used; no TWL-HD GX code, no animation.
//
// Projection and raw integer vertices mirror libnds gl2d.c (SetOrtho /
// gxVertex3i): vertex (x, y) lands on DS pixel (x, y) (README section 4).

#include <nds.h>

#include "quads.h"

// libnds GL2D's gxVertex3i: raw 16-bit vertex, no 4.12 shift.  z is passed
// through so the backdrops (z=0) and the translucent quads (z=+64) get
// different depth values.  A larger source z maps to a smaller post-transform
// z, which is nearer under the default LESS depth test (README section 3).
static inline void vertex_px(int x, int y, int z) {
    glVertex3v16((v16)x, (v16)y, (v16)z);
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

static void draw_poly(const AlphaPoly *p) {
    // Per-polygon polygon alpha, polygon id, no culling, no fog, no lighting
    // (POLY_FORMAT_LIGHT0..3 stay 0).  POLY_ID keeps every quad independent.
    glPolyFmt(POLY_ALPHA(p->alpha) | POLY_CULL_NONE | POLY_ID(p->poly_id));
    glColor(RGB15(p->r5, p->g5, p->b5));
    glBegin(GL_QUADS);
    vertex_px(p->x, p->y, p->z);
    vertex_px(p->x + p->w, p->y, p->z);
    vertex_px(p->x + p->w, p->y + p->h, p->z);
    vertex_px(p->x, p->y + p->h, p->z);
    glEnd();
}

// Opaque backdrops first, then the translucent quads at a larger source z
// (+64), which lands nearer under the default LESS depth test, so the quads
// stay visible.  The list is submitted once; the DS keeps scanning out the
// last swapped buffer.
static void draw_scene(void) {
    int i;
    for (i = 0; i < (int)ALPHA_BACKDROP_COUNT; i++)
        draw_poly(&ALPHA_BACKDROPS[i]);
    for (i = 0; i < (int)ALPHA_QUAD_COUNT; i++)
        draw_poly(&ALPHA_QUADS[i]);
}

int main(void) {
    videoSetMode(MODE_0_3D);
    glInit();

    // glInit() leaves GFX_CONTROL = 0.  Enable alpha blending only; disable
    // the features this test must not use: textures, alpha test, antialiasing,
    // edge marking, fog.  GL_TOON_HIGHLIGHT stays 0 (disabled) and lighting is
    // per-polygon (POLY_FORMAT_LIGHT0..3 = 0), so no toon/highlight shading.
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS | GL_OUTLINE |
              GL_FOG);

    glClearColor(DSALPHA_CLEAR_R, DSALPHA_CLEAR_G, DSALPHA_CLEAR_B, 31);
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
