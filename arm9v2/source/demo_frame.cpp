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

#ifndef TWLHD_DEMO_DIAGSCREEN
#define TWLHD_DEMO_DIAGSCREEN 0
#endif

#include "demo_frame.hpp"

#if TWLHD_DEMO_DIAGSCREEN == 2 || TWLHD_DEMO_DIAGSCREEN >= 5
/* Recorder bypass: compile libnds' inline helpers against raw GFX_* macros. */
#include <nds.h>
#include "twlhd_gx_shim.hpp"
#else
#include "twlhd_gx_shim.hpp"
#include <nds.h>
#endif

#if TWLHD_DEMO_DIAGSCREEN == 6
/* Counter-driven rotation for the Simple_Tri variant. File-scope POD statics
 * avoid a function-local dynamic init that would emit __cxa_guard_acquire. */
static float simple_tri_rotateX = 0.0f;
static float simple_tri_rotateY = 0.0f;
#endif

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

/*
 * Alternative primitive scene, build-time option (SCENE=1..4 in the Makefile).
 * 0 (the default) keeps today's cube path. 1 draws a triangle list, 2 a
 * triangle strip, 3 a quad strip, 4 four quads each preceded by its own
 * POLYGON_ATTR alpha (31, 16, 4, 0). The two options cannot be combined.
 */
#ifndef TWLHD_DEMO_SCENE
#define TWLHD_DEMO_SCENE 0
#endif

#if TWLHD_DEMO_SCENE != 0 && TWLHD_DEMO_CUBES != 1
#error "TWLHD_DEMO_SCENE and TWLHD_DEMO_CUBES > 1 are mutually exclusive"
#endif

#if TWLHD_DEMO_DIAGSCREEN >= 7
#if TWLHD_DEMO_SCENE != 0 || TWLHD_DEMO_CUBES > 1
#error "DIAGSCREEN >= 7 variants are only defined for the single-cube base scene"
#endif
#endif

/*
 * Scene 5: one textured quad using a known main-RAM source texture.
 * The texture and palette arrays are registered with the capture backend so
 * the recorder can read them back at dump time. libnds' glTexImage2D and
 * glColorTableEXT are out-of-line and bypass the shim, so the draw issues
 * explicit GFX_TEX_FORMAT / GFX_PAL_FORMAT writes through the proxy.
 */
#if TWLHD_DEMO_SCENE == 5
/* Aligned so VRAM uploads can use 16-bit stores (the DS ignores 8-bit VRAM). */
static const uint8_t scene5_texel[32] __attribute__((aligned(4))) = {
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
};
static const uint8_t scene5_palette[32] __attribute__((aligned(4))) = {
    0x00, 0x00, 0xFF, 0x0F, 0x00, 0xF8, 0xE0, 0x07,
    0x1F, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0x7F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void twlhd_demo_scene5_register_sources(TwlhdTexSourceCtx *ctx) {
    /* I4 8x8: texel base = (param & 0xFFFF) << 3, palette base = pltt << 4. */
    twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_TEXEL, 0u, 32u,
                         scene5_texel);
    twlhd_tex_source_add(ctx, TWLHD_TEX_CAP_READ_PALETTE, 0u, 32u,
                         scene5_palette);
}

/*
 * One-time upload to texture VRAM. Mapped banks cannot be CPU-written, so we
 * map them to LCDC, copy with 16-bit stores, then switch to texture/palette
 * memory. HARDWARE UNVERIFIED: build-only fix from DeSmuME/GBATEK model.
 */
void twlhd_demo_scene5_upload_textures(void) {
    uint32_t i;

    vramSetBankA(VRAM_A_LCD);
    vramSetBankE(VRAM_E_LCD);
    for (i = 0; i < 16u; i++) {
        ((volatile uint16_t *)0x06800000u)[i] =
            ((const uint16_t *)scene5_texel)[i];
        ((volatile uint16_t *)0x06880000u)[i] =
            ((const uint16_t *)scene5_palette)[i];
    }
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
}

static void draw_scene5_textured_quad(void) {
    /* 8x8 I4 texture, slot offset 0, palette slot 0. */
    uint32_t param = (3u << 26);
    uint32_t pltt = 0u;

    /*
     * Explicit texture-register writes through the shim. libnds'
     * glTexImage2D / glColorTableEXT are out-of-line and bypass the proxy, so
     * the keys must be recorded this way (D6).
     */
    GFX_TEX_FORMAT = param;
    GFX_PAL_FORMAT = pltt;

    /* Texturing is off by default; enable it before the textured quad. */
    glEnable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);
    /* Raw texel-scaled coords; glTexCoord2f needs active-texture size state. */
    GFX_TEX_COORD = TEXTURE_PACK(inttot16(0), inttot16(0));
    glVertex3v16((v16)(-V16_ONE), (v16)(-V16_ONE), 0);
    GFX_TEX_COORD = TEXTURE_PACK(inttot16(8), inttot16(0));
    glVertex3v16((v16)V16_ONE, (v16)(-V16_ONE), 0);
    GFX_TEX_COORD = TEXTURE_PACK(inttot16(8), inttot16(8));
    glVertex3v16((v16)V16_ONE, (v16)V16_ONE, 0);
    GFX_TEX_COORD = TEXTURE_PACK(inttot16(0), inttot16(8));
    glVertex3v16((v16)(-V16_ONE), (v16)V16_ONE, 0);
    glEnd();
}
#endif

#if TWLHD_DEMO_SCENE
/* Flat (z = 0) shapes drawn at 2x scale, so scaled vertices reach |x| = 1.25;
 * the frames stay in view by the hand-derived frustum bound documented in the
 * host test. Scenes 1-3 give every vertex a unique red channel (i + 1 in
 * RGB15) so interpreted output maps back to input order. */
#if TWLHD_DEMO_SCENE == 1
static const int16_t scene_tri_vertices[24][2] = {
    {-2243, -1424}, {-1443, -1424}, {-1843, -624},
    {-1014, -1424}, { -214, -1424}, { -614, -624},
    {  214, -1424}, { 1014, -1424}, {  614, -624},
    { 1443, -1424}, { 2243, -1424}, { 1843, -624},
    {-2243,   624}, {-1443,   624}, {-1843, 1424},
    {-1014,   624}, { -214,   624}, { -614, 1424},
    {  214,   624}, { 1014,   624}, {  614, 1424},
    { 1443,   624}, { 2243,   624}, { 1843, 1424},
};
#elif TWLHD_DEMO_SCENE == 2 || TWLHD_DEMO_SCENE == 3
static const int16_t scene_strip_vertices[12][2] = {
    {-2560, -1024}, {-2560, 1024},
    {-1536, -1024}, {-1536, 1024},
    { -512, -1024}, { -512, 1024},
    {  512, -1024}, {  512, 1024},
    { 1536, -1024}, { 1536, 1024},
    { 2560, -1024}, { 2560, 1024},
};
#else
/* Scene 4: four flat quads in one quad-list each, with one POLYGON_ATTR per
 * BEGIN so the interpreter latches alpha 31, 16, 4 and 0. Four distinct solid
 * colours; the alpha-0 quad is counted as wireframe (outline drawing is NOT
 * implemented, so the host interpreter emits it with vertex alpha 0.0). */
static const int16_t scene_alpha_vertices[4][4][2] = {
    {{-2400, -1600}, { -400, -1600}, { -400, -200}, {-2400,  -200}},
    {{  400, -1600}, { 2400, -1600}, { 2400,  -200}, {  400,  -200}},
    {{-2400,   200}, { -400,   200}, { -400,  1600}, {-2400,  1600}},
    {{  400,   200}, { 2400,   200}, { 2400,  1600}, {  400,  1600}},
};
static const uint8_t scene_alpha[4] = {31u, 16u, 4u, 0u};
static const uint8_t scene_colors[4][3] = {
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255},
};
#endif

#if TWLHD_DEMO_SCENE != 4
static void draw_scene_vertices(const int16_t (*verts)[2], int count) {
    int i;

    for (i = 0; i < count; i++) {
        /* glColor3b keeps the top 5 bits: red == i + 1, green/blue == 0. */
        glColor3b((u8)((i + 1) << 3), 0, 0);
        glVertex3v16((v16)verts[i][0], (v16)verts[i][1], 0);
    }
}
#endif

#if TWLHD_DEMO_SCENE == 1
static void draw_scene_tri_list(void) {
    glBegin(GL_TRIANGLES);
    draw_scene_vertices(scene_tri_vertices, 24);
    glEnd();
}
#elif TWLHD_DEMO_SCENE == 2
static void draw_scene_tri_strip(void) {
    glBegin(GL_TRIANGLE_STRIP);
    draw_scene_vertices(scene_strip_vertices, 12);
    glEnd();
}
#elif TWLHD_DEMO_SCENE == 3
static void draw_scene_quad_strip(void) {
    glBegin(GL_QUAD_STRIP);
    draw_scene_vertices(scene_strip_vertices, 12);
    glEnd();
}
#else
static void draw_scene_alpha_quads(void) {
    int q;
    int i;

    for (q = 0; q < 4; q++) {
        glPolyFmt(POLY_ALPHA(scene_alpha[q]));
        glBegin(GL_QUADS);
        for (i = 0; i < 4; i++) {
            glColor3b(scene_colors[q][0], scene_colors[q][1],
                      scene_colors[q][2]);
            glVertex3v16((v16)scene_alpha_vertices[q][i][0],
                         (v16)scene_alpha_vertices[q][i][1], 0);
        }
        glEnd();
    }
}
#endif
#endif

void twlhd_demo_draw_frame(unsigned angle) {
#if TWLHD_DEMO_DIAGSCREEN == 6
    /* Exact Simple_Tri example draw logic, including the quirk that
     * glMatrixMode(GL_MODELVIEW) is called after the projection-space rotates.
     * Rotates by a counter so a frozen picture is distinguishable from animation.
     * GL_TRIANGLE is the libnds name used by the example. */
    glPushMatrix();
    glTranslatef32(0, 0, floattof32(-1));
    glRotateX(simple_tri_rotateX);
    glRotateY(simple_tri_rotateY);

    glMatrixMode(GL_MODELVIEW);

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);

    glBegin(GL_TRIANGLE);
    glColor3b(255, 0, 0);
    glVertex3v16(inttov16(-1), inttov16(-1), 0);
    glColor3b(0, 255, 0);
    glVertex3v16(inttov16(1), inttov16(-1), 0);
    glColor3b(0, 0, 255);
    glVertex3v16(inttov16(0), inttov16(1), 0);
    glEnd();

    glPopMatrix(1);

    simple_tri_rotateX += 3.0f;
    simple_tri_rotateY += 3.0f;

    glFlush(0);
    return;
#endif
#if TWLHD_DEMO_SCENE != 0
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
    glScalef32(inttof32(2), inttof32(2), inttof32(2));

    glPushMatrix();
#if TWLHD_DEMO_SCENE == 4
    /* Each quad issues its own POLYGON_ATTR alpha. */
    draw_scene_alpha_quads();
#else
    glPolyFmt(POLY_ALPHA(31));
#if TWLHD_DEMO_SCENE == 1
    draw_scene_tri_list();
#elif TWLHD_DEMO_SCENE == 2
    draw_scene_tri_strip();
#elif TWLHD_DEMO_SCENE == 3
    draw_scene_quad_strip();
#elif TWLHD_DEMO_SCENE == 5
    draw_scene5_textured_quad();
#else
    draw_scene_quad_strip();
#endif
#endif
    glPopMatrix(1);

    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
#elif TWLHD_DEMO_CUBES <= 1
    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
#if TWLHD_DEMO_DIAGSCREEN == 8
    gluPerspective(70, 256.0 / 192.0, 0.1, 40.0);
#else
    gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);
#endif

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
    glRotateXi((int)angle);
    glRotateYi((int)angle);
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(1), inttof32(1), inttof32(1));

    glPushMatrix();
#if TWLHD_DEMO_DIAGSCREEN == 7
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#elif TWLHD_DEMO_DIAGSCREEN == 9
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK);
#else
    glPolyFmt(POLY_ALPHA(31));
#endif
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
