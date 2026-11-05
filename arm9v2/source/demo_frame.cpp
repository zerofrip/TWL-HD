// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - self-contained v2 capture demo frame body.
 *
 * See arm9v2/include/demo_frame.hpp. The shim must be included before <nds.h>
 * so every GFX_* / MATRIX_* port macro used by libnds' inline helpers expands
 * to the recording proxy.
 *
 * HARDWARE UNVERIFIED: this source is also compiled into a host test with
 * mocked registers; no DS runtime has executed it.
 */

#include "demo_frame.hpp"

#include "twlhd_gx_shim.hpp"
#include <nds.h>

/* 4.12 fixed point vertex for the unit cube. */
#define V16_ONE ((int16_t)(1 << 12))

static const int16_t cube_vertices[8][3] = {
    {-V16_ONE, -V16_ONE,  V16_ONE}, /* 0 front bottom left  */
    { V16_ONE, -V16_ONE,  V16_ONE}, /* 1 front bottom right */
    { V16_ONE,  V16_ONE,  V16_ONE}, /* 2 front top right    */
    {-V16_ONE,  V16_ONE,  V16_ONE}, /* 3 front top left     */
    {-V16_ONE, -V16_ONE, -V16_ONE}, /* 4 back bottom left   */
    { V16_ONE, -V16_ONE, -V16_ONE}, /* 5 back bottom right  */
    { V16_ONE,  V16_ONE, -V16_ONE}, /* 6 back top right     */
    {-V16_ONE,  V16_ONE, -V16_ONE}, /* 7 back top left      */
};

static const uint8_t cube_faces[6][4] = {
    {0, 1, 2, 3}, /* front  */
    {5, 4, 7, 6}, /* back   */
    {4, 0, 3, 7}, /* left   */
    {1, 5, 6, 2}, /* right  */
    {3, 2, 6, 7}, /* top    */
    {4, 5, 1, 0}, /* bottom */
};

/* Packed 10-bit normals (x | y << 10 | z << 20); +-1.0 encodes as 0x1FF/0x201. */
static const uint32_t cube_normals[6] = {
    0x1FF00000u, /* +Z */
    0x20100000u, /* -Z */
    0x00000201u, /* -X */
    0x000001FFu, /* +X */
    0x0007FC00u, /* +Y */
    0x00080400u, /* -Y */
};

static const uint8_t cube_colors[6][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
    {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
};

static void draw_cube(void) {
    int face;
    int corner;

    glBegin(GL_QUADS);

    for (face = 0; face < 6; face++) {
        glNormal(cube_normals[face]);
        glColor3b(cube_colors[face][0], cube_colors[face][1],
                  cube_colors[face][2]);

        for (corner = 0; corner < 4; corner++) {
            const int16_t *v = cube_vertices[cube_faces[face][corner]];
            glVertex3v16((v16)v[0], (v16)v[1], (v16)v[2]);
        }
    }

    glEnd();
}

/*
 * Viewport is a build-time option (VP_X1/VP_Y1/VP_X2/VP_Y2 in the Makefile) so
 * a non-default-viewport capture can be produced. The packed GFX_VIEWPORT word
 * is x1 | y1 << 8 | x2 << 16 | y2 << 24 (DS y is measured from the bottom).
 * The defaults keep the normal ROM byte-identical.
 */
#ifndef TWLHD_DEMO_VP_X1
#define TWLHD_DEMO_VP_X1 0
#endif
#ifndef TWLHD_DEMO_VP_Y1
#define TWLHD_DEMO_VP_Y1 0
#endif
#ifndef TWLHD_DEMO_VP_X2
#define TWLHD_DEMO_VP_X2 255
#endif
#ifndef TWLHD_DEMO_VP_Y2
#define TWLHD_DEMO_VP_Y2 191
#endif

/*
 * Number of demo cubes (1..170) drawn in a 17-column grid, build-time option
 * (CUBES=<n> in the Makefile). The default keeps the normal ROM byte-identical.
 */
#ifndef TWLHD_DEMO_CUBES
#define TWLHD_DEMO_CUBES 1
#endif

void twlhd_demo_draw_frame(unsigned angle) {
#if TWLHD_DEMO_CUBES <= 1
    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
    glRotateXi((int)angle);
    glRotateYi((int)angle);
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(1), inttof32(1), inttof32(1));

    glPushMatrix();
    glPolyFmt(POLY_ALPHA(31));
    draw_cube();
    glPopMatrix(1);

    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
#else
    int i;
    int col;
    int row;
    int rows;
    s32 x;
    s32 y;
    s32 half_spacing;

    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
    glRotateXi((int)angle);
    glRotateYi((int)angle);
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(1), inttof32(1), inttof32(1));

    rows = (TWLHD_DEMO_CUBES + 16) / 17;
    half_spacing = floattof32(0.42) >> 1;
    for (i = 0; i < TWLHD_DEMO_CUBES; i++) {
        col = i % 17;
        row = i / 17;
        x = mulf32(inttof32(2 * col - 16), half_spacing);
        y = mulf32(inttof32(2 * row - (rows - 1)), half_spacing);
        glPushMatrix();
        glTranslatef32(x, y, 0);
        glRotateXi((int)angle);
        glRotateYi((int)angle);
        glScalef32(floattof32(0.12), floattof32(0.12), floattof32(0.12));
        glPolyFmt(POLY_ALPHA(31)); /* mirrors the single-cube path */
        draw_cube();
        glPopMatrix(1);
    }

    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
#endif
}
