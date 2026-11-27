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

#ifndef TWLHD_DEMO_LIT
#define TWLHD_DEMO_LIT 0
#endif

#ifndef TWLHD_DEMO_TEXFMT
#define TWLHD_DEMO_TEXFMT 0
#endif

#ifndef TWLHD_DEMO_TEXMOD
#define TWLHD_DEMO_TEXMOD 0
#endif

#ifndef TWLHD_DEMO_TEXBEH
#define TWLHD_DEMO_TEXBEH 0
#endif

#ifndef TWLHD_DEMO_GRAD
#define TWLHD_DEMO_GRAD 0
#endif

#ifndef TWLHD_DEMO_CULL
#define TWLHD_DEMO_CULL 0
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
        glPolyFmt(POLY_ALPHA(scene_alpha[q]) | POLY_CULL_NONE);
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

#if TWLHD_DEMO_LIT
/*
 * DS-mode lighting reproduction for EQ-4zh.  Constants are copied verbatim from
 * tools/hw/gen_lit_frames.py.
 *
 * glLight(), glNormal() and glPolyFmt() are libnds static-inline helpers, so
 * they expand through the recording GFX_* proxy and stay in the captured v2
 * stream.
 *
 * glMaterialf() is only declared out-of-line in libnds (videoGL.h:523) and
 * glMaterialShinyness() installs a fixed 0,2,4,...,254 ramp out-of-line
 * (videoGL.h:727-740).  No libnds material API therefore goes through the
 * recording proxy; both the material words (GFX_DIFFUSE_AMBIENT and
 * GFX_SPECULAR_EMISSION) and the custom 1,3,5,...,255 shininess ramp are
 * emitted through the proxy directly.
 */
static const int16_t lit_quads[4][4][2] = {
    {{-3840, -2560}, {-640, -2560}, {-640, -320}, {-3840, -320}},
    {{640, -2560}, {3840, -2560}, {3840, -320}, {640, -320}},
    {{-3840, 320}, {-640, 320}, {-640, 2560}, {-3840, 2560}},
    {{640, 320}, {3840, 320}, {3840, 2560}, {640, 2560}},
};

static const uint32_t LIT_NORMAL_FACING = 0x1FF00000u;
static const uint32_t LIT_NORMAL_60 = (256u << 20) | 443u;
static const uint32_t LIT_NORMAL_GRAZING = 511u;
static const uint32_t LIT_NORMAL_AWAY = 513u << 20;
static const uint32_t LIT_NORMAL_75 = (128u << 20) | 495u;

static const uint32_t LIT_SHININESS[32] = {
    0x07050301u, 0x0F0D0B09u, 0x17151311u, 0x1F1D1B19u,
    0x27252321u, 0x2F2D2B29u, 0x37353331u, 0x3F3D3B39u,
    0x47454341u, 0x4F4D4B49u, 0x57555351u, 0x5F5D5B59u,
    0x67656361u, 0x6F6D6B69u, 0x77757371u, 0x7F7D7B79u,
    0x87858381u, 0x8F8D8B89u, 0x97959391u, 0x9F9D9B99u,
    0xA7A5A3A1u, 0xAFADABA9u, 0xB7B5B3B1u, 0xBFBDBBB9u,
    0xC7C5C3C1u, 0xCFCDCBC9u, 0xD7D5D3D1u, 0xDFDDDBD9u,
    0xE7E5E3E1u, 0xEFEDEBE9u, 0xF7F5F3F1u, 0xFFFDFBF9u,
};

/* Light vectors from gen_lit_frames.py are 20.12 fixed point; glLight takes
 * the raw 10-bit field, so each component is divided by 8 (exact, all values
 * are multiples of 8). */
static const v10 LIT_LIGHT0_X = 0;
static const v10 LIT_LIGHT0_Y = 0;
static const v10 LIT_LIGHT0_Z = -511;  /* -4088 / 8 */
static const v10 LIT_LIGHT1_X = 256;   /*  2048 / 8 */
static const v10 LIT_LIGHT1_Y = 0;
static const v10 LIT_LIGHT1_Z = -443;  /* -3544 / 8 */

static void draw_lit_quad(uint32_t alpha, uint32_t light_mask,
                          uint32_t normal, const int16_t corners[4][2]) {
    int i;

    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | light_mask);
    glBegin(GL_QUADS);
    glNormal(normal);
    for (i = 0; i < 4; i++) {
        glVertex3v16((v16)corners[i][0], (v16)corners[i][1], 0);
    }
    glEnd();
}

static void lit_material(uint32_t diffuse, uint32_t ambient,
                         uint32_t specular, uint32_t emission) {
    /* glMaterialf() is out-of-line in libnds, so it does not go through the recording proxy. */
    GFX_DIFFUSE_AMBIENT = (ambient << 16) | diffuse;
    GFX_SPECULAR_EMISSION = (emission << 16) | specular;
}

static void draw_lit_a(void) {
    glLight(0, RGB15(31, 31, 31),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    lit_material(RGB15(22, 22, 22), RGB15(4, 4, 4), 0, 0);

    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[0]);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_60, lit_quads[1]);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_GRAZING, lit_quads[2]);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_AWAY, lit_quads[3]);
}

static void draw_lit_b(void) {
    int i;

    glLight(0, RGB15(31, 0, 0),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    glLight(1, RGB15(0, 0, 31),
            LIT_LIGHT1_X, LIT_LIGHT1_Y, LIT_LIGHT1_Z);

    /* quad 0: diffuse + ambient lit by the red light */
    lit_material(RGB15(22, 22, 22), RGB15(4, 4, 4), 0, 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[0]);

    /* quad 1: emission only */
    lit_material(0, 0, 0, RGB15(4, 28, 4));
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_AWAY, lit_quads[1]);

    /* quad 2: specular through the shininess table, white light */
    lit_material(0, 0, RGB15(31, 31, 31) | BIT(15), 0);
    glLight(0, RGB15(31, 31, 31),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    for (i = 0; i < 32; i++) {
        GFX_SHININESS = LIT_SHININESS[i];
    }
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[2]);

    /* quad 3: two lights from different directions */
    lit_material(RGB15(22, 22, 22), 0, 0, 0);
    glLight(0, RGB15(31, 0, 0),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1,
                  LIT_NORMAL_FACING, lit_quads[3]);
}

/* lit_c: diffuse+ambient rounding probe (gen_lit_frames.py lit_c_entries). */
static void draw_lit_c(void) {
    glLight(0, RGB15(31, 31, 31),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);

    lit_material(RGB15(1, 1, 1), RGB15(1, 1, 1), 0, 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[0]);

    lit_material(RGB15(1, 1, 1), 0, 0, 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[1]);

    lit_material(RGB15(1, 1, 1), 0, 0, 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_60, lit_quads[2]);

    lit_material(RGB15(28, 28, 28), RGB15(8, 8, 8), 0, 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_75, lit_quads[3]);
}

/* lit_d: specular and two-light rounding probe (gen_lit_frames.py
 * lit_d_entries): two lights in one direction, colours 31 and 20. */
static void draw_lit_d(void) {
    int i;

    glLight(0, RGB15(31, 31, 31),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    glLight(1, RGB15(20, 20, 20),
            LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
    for (i = 0; i < 32; i++) {
        GFX_SHININESS = LIT_SHININESS[i];
    }

    lit_material(0, RGB15(1, 1, 1), RGB15(1, 1, 1) | BIT(15), 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1,
                  LIT_NORMAL_FACING, lit_quads[0]);

    lit_material(0, RGB15(1, 1, 1), RGB15(1, 1, 1) | BIT(15), 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[1]);

    lit_material(RGB15(8, 8, 8), 0, RGB15(4, 4, 4) | BIT(15), 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1,
                  LIT_NORMAL_FACING, lit_quads[2]);

    lit_material(RGB15(8, 8, 8), 0, RGB15(4, 4, 4) | BIT(15), 0);
    draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING, lit_quads[3]);
}

/* lit_e: specular shininess probe (gen_lit_frames.py lit_e_entries). */
static void draw_lit_e(void) {
    static const uint8_t spec[4] = {23, 25, 26, 20};
    static const uint8_t light[4] = {23, 25, 29, 31};
    int i;

    for (i = 0; i < 32; i++) {
        GFX_SHININESS = LIT_SHININESS[i];
    }
    lit_material(0, 0, 0, 0);
    for (i = 0; i < 4; i++) {
        glLight(0, RGB15(light[i], light[i], light[i]),
                LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
        lit_material(0, 0, RGB15(spec[i], spec[i], spec[i]) | BIT(15), 0);
        draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING,
                      lit_quads[i]);
    }
}

/* lit_f: diffuse factor probe (gen_lit_frames.py lit_f_entries). */
static void draw_lit_f(void) {
    static const uint8_t diff[4] = {17, 25, 26, 26};
    static const uint8_t light[4] = {21, 31, 31, 26};
    int i;

    for (i = 0; i < 4; i++) {
        glLight(0, RGB15(light[i], light[i], light[i]),
                LIT_LIGHT0_X, LIT_LIGHT0_Y, LIT_LIGHT0_Z);
        lit_material(RGB15(diff[i], diff[i], diff[i]), 0, 0, 0);
        draw_lit_quad(31, POLY_FORMAT_LIGHT0, LIT_NORMAL_FACING,
                      lit_quads[i]);
    }
}
#endif

#if TWLHD_DEMO_TEXFMT
/*
 * DS texture-format grid for EQ-4zj.  The VRAM images and per-quad words come
 * from tools/hw/gen_texfmt_rom_data.py (committed as texfmt_data.h); the quads
 * reproduce the eight-quad grid of tools/gen_tex_format_frames.py.
 *
 * No libnds texture helper is used: the texture registers are written
 * directly, like scene 5.  The ROM is not recorder-compatible (no texture
 * sources are registered), so TEXFMT cannot be combined with AUTODUMP.
 */
#include "texfmt_data.h"

/* Bank A = texture slot 0 (texels), bank B = slot 1 (4x4 indices), bank E =
 * texture palette.  Mapped banks cannot be CPU-written, so copy through LCDC
 * mappings with 16-bit stores first. */
void twlhd_demo_texfmt_upload_textures(void) {
    uint32_t i;

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankE(VRAM_E_LCD);
    for (i = 0; i < sizeof(texfmt_slot0) / 2u; i++) {
        ((volatile uint16_t *)0x06800000u)[i] = texfmt_slot0[i];
    }
    for (i = 0; i < sizeof(texfmt_slot1) / 2u; i++) {
        ((volatile uint16_t *)0x06820000u)[i] = texfmt_slot1[i];
    }
    for (i = 0; i < sizeof(texfmt_palette) / 2u; i++) {
        ((volatile uint16_t *)0x06880000u)[i] = texfmt_palette[i];
    }
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
}

static void draw_texfmt_quad(const struct TexfmtQuad *q) {
    /* Two triangles, texcoords as QUAD_TEXCOORDS in the generator. */
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t su[4] = {0, 1, 1, 0};
    static const uint8_t tv[4] = {0, 0, 1, 1};
    int i;

    GFX_TEX_FORMAT = q->param;
    GFX_PAL_FORMAT = q->pltt;
    GFX_COLOR = 0x7FFFu;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < 6; i++) {
        uint32_t k = order[i];
        int s = su[k] ? q->s_max : 0;
        int t = tv[k] ? q->t_max : 0;

        GFX_TEX_COORD = TEXTURE_PACK(s, t);
        glVertex3v16((v16)q->corners[k][0], (v16)q->corners[k][1], 0);
    }
    glEnd();
}

static void draw_texfmt(void) {
    int i;

    glEnable(GL_TEXTURE_2D);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
    for (i = 0; i < 8; i++) {
        draw_texfmt_quad(&texfmt_quads[i]);
    }
}
#endif

#if TWLHD_DEMO_TEXMOD
/*
 * DS MODULATE probe for EQ-4zk.  Eight flat quads, each with one solid 8x8
 * 16bpp texture and one flat vertex colour (tools/hw/gen_texmod_rom_data.py ->
 * texmod_data.h).  Texture registers are written directly, like scene 5;
 * the ROM is not recorder-compatible, so TEXMOD cannot be combined with
 * AUTODUMP.  Polygon mode is the default MODULATE (POLYGON_ATTR bits 4-5 = 0).
 */
#include "texmod_data.h"

void twlhd_demo_texmod_upload_textures(void) {
    uint32_t i;

    vramSetBankA(VRAM_A_LCD);
    for (i = 0; i < sizeof(texmod_slot0) / 2u; i++) {
        ((volatile uint16_t *)0x06800000u)[i] = texmod_slot0[i];
    }
    vramSetBankA(VRAM_A_TEXTURE);
}

static void draw_texmod_quad(const struct TexmodQuad *q) {
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    static const uint8_t su[4] = {0, 1, 1, 0};
    static const uint8_t tv[4] = {0, 0, 1, 1};
    int i;

    GFX_TEX_FORMAT = q->param;
    GFX_COLOR = q->colour;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < 6; i++) {
        uint32_t k = order[i];
        int s = su[k] ? 8 * 16 : 0;
        int t = tv[k] ? 8 * 16 : 0;

        GFX_TEX_COORD = TEXTURE_PACK(s, t);
        glVertex3v16((v16)q->corners[k][0], (v16)q->corners[k][1], 0);
    }
    glEnd();
}

static void draw_texmod(void) {
    int i;

    glEnable(GL_TEXTURE_2D);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
    for (i = 0; i < 8; i++) {
        draw_texmod_quad(&texmod_quads[i]);
    }
}
#endif

#if TWLHD_DEMO_TEXBEH
/*
 * DS texture-behaviour grid for EQ-4zl: texture alpha (A3I5, A5I3), KeyColor0,
 * repeat, clamp, mirrored repeat and flip-only, as in
 * tools/gen_tex_behaviour_frames.py (data: texbeh_data.h generated by
 * tools/hw/gen_texbeh_rom_data.py).  Texture registers are written directly,
 * like scene 5; not recorder-compatible, so no AUTODUMP.
 */
#include "texbeh_data.h"

void twlhd_demo_texbeh_upload_textures(void) {
    uint32_t i;

    vramSetBankA(VRAM_A_LCD);
    vramSetBankE(VRAM_E_LCD);
    for (i = 0; i < sizeof(texbeh_slot0) / 2u; i++) {
        ((volatile uint16_t *)0x06800000u)[i] = texbeh_slot0[i];
    }
    for (i = 0; i < sizeof(texbeh_palette) / 2u; i++) {
        ((volatile uint16_t *)0x06880000u)[i] = texbeh_palette[i];
    }
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
}

static void draw_texbeh_quad(const struct TexbehQuad *q) {
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    int i;

    GFX_TEX_FORMAT = q->param;
    GFX_PAL_FORMAT = q->pltt;
    GFX_COLOR = 0x7FFFu;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < 6; i++) {
        uint32_t k = order[i];

        GFX_TEX_COORD = ((uint32_t)(uint16_t)q->texcoords[i][0]) |
                        ((uint32_t)(uint16_t)q->texcoords[i][1] << 16);
        glVertex3v16((v16)q->corners[k][0], (v16)q->corners[k][1], 0);
    }
    glEnd();
}

static void draw_texbeh(void) {
    int i;

    glEnable(GL_TEXTURE_2D);
    /* DISP3DCNT bit 3 = alpha blending (the bit the libnds blend enable sets); written
     * directly so the BLEND falsification build keeps its single enable site
     * (tools/test_arm9v2_blend.py). Texture alpha needs it to blend. */
    GFX_CONTROL |= (1u << 3);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
    for (i = 0; i < 8; i++) {
        draw_texbeh_quad(&texbeh_quads[i]);
    }
}
#endif

#if TWLHD_DEMO_GRAD || TWLHD_DEMO_CULL
#include "gradcull_data.h"
#endif

#if TWLHD_DEMO_GRAD
/* Gouraud probe for EQ-4zm: two triangles with different corner colours. */
static void draw_grad(void) {
    int t;
    int i;

    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
    for (t = 0; t < 2; t++) {
        glBegin(GL_TRIANGLES);
        for (i = 0; i < 3; i++) {
            GFX_COLOR = grad_triangles[t].colours[i];
            glVertex3v16((v16)grad_triangles[t].vertices[i][0],
                         (v16)grad_triangles[t].vertices[i][1], 0);
        }
        glEnd();
    }
}
#endif

#if TWLHD_DEMO_CULL
/* Culling probe for EQ-4zn: eight white quads, two windings x attr bits 6-7. */
static void draw_cull(void) {
    int q;
    int i;

    GFX_COLOR = 0x7FFFu;
    for (q = 0; q < 8; q++) {
        glPolyFmt(POLY_ALPHA(31) | cull_quads[q].attr_bits);
        glBegin(GL_QUADS);
        for (i = 0; i < 4; i++) {
            glVertex3v16((v16)cull_quads[q].corners[i][0],
                         (v16)cull_quads[q].corners[i][1], 0);
        }
        glEnd();
    }
}
#endif

#ifdef TWLHD_DEMO_STATIC_ANGLE_DEG
/* Fixed modelview angle for every draw path (STATIC_ANGLE=0..359). */
#define TWLHD_DEMO_STATIC_ANGLE_BIN \
    ((int)((unsigned)TWLHD_DEMO_STATIC_ANGLE_DEG * 32768u / 360u))
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
#if TWLHD_DEMO_TEXBEH || TWLHD_DEMO_GRAD || TWLHD_DEMO_CULL
    /* Static probe scenes; rotation is deliberately absent. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

#if TWLHD_DEMO_TEXBEH
    draw_texbeh();
#elif TWLHD_DEMO_GRAD
    draw_grad();
#else
    draw_cull();
#endif

    glFlush(0);
    return;
#endif
#if TWLHD_DEMO_TEXMOD
    /* Static MODULATE probe; rotation is deliberately absent. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    draw_texmod();

    glFlush(0);
    return;
#endif
#if TWLHD_DEMO_TEXFMT
    /* Static texture-format grid; rotation is deliberately absent. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    draw_texfmt();

    glFlush(0);
    return;
#endif
#if TWLHD_DEMO_LIT
    /* Static lit scene; rotation is deliberately absent. */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(TWLHD_DEMO_VP_X1, TWLHD_DEMO_VP_Y1,
               TWLHD_DEMO_VP_X2, TWLHD_DEMO_VP_Y2);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

#if TWLHD_DEMO_LIT == 1
    draw_lit_a();
#elif TWLHD_DEMO_LIT == 2
    draw_lit_b();
#elif TWLHD_DEMO_LIT == 3
    draw_lit_c();
#elif TWLHD_DEMO_LIT == 4
    draw_lit_d();
#elif TWLHD_DEMO_LIT == 5
    draw_lit_e();
#else
    draw_lit_f();
#endif

    glFlush(0);
    return;
#endif
#if TWLHD_DEMO_SCENE != 0
    /* Re-issue all state needed to replay this frame independently. */
    glEnable(GL_ANTIALIAS);
#ifdef TWLHD_DEMO_BLEND
    /* Falsification build (EQ-4u §6.3): enable DS alpha blending. */
    glEnable(GL_BLEND);
#endif
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
#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
    /* Fixed modelview angle for every draw path (STATIC_ANGLE=0..359 in the
     * Makefile): the animated build rotates by the frame counter, in binary
     * angle units (32768 == 360 degrees, main.cpp:262-263). Degrees -> binary
     * units with integer truncation, exactly as the animated value would be at
     * that angle. */
    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
#else
    glRotateXi((int)angle);
    glRotateYi((int)angle);
#endif
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(2), inttof32(2), inttof32(2));

    glPushMatrix();
#if TWLHD_DEMO_SCENE == 4
    /* Each quad issues its own POLYGON_ATTR alpha. */
    draw_scene_alpha_quads();
#else
#if TWLHD_DEMO_SCENE == 5
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#else
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#endif
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
#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
    /* Fixed modelview angle for every draw path. */
    glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
    glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
#else
    glRotateXi((int)angle);
    glRotateYi((int)angle);
#endif
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(1), inttof32(1), inttof32(1));

    glPushMatrix();
#if TWLHD_DEMO_DIAGSCREEN == 7
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
#elif TWLHD_DEMO_DIAGSCREEN == 9
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK);
#else
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);
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
#if defined(TWLHD_DEMO_STATIC_ANGLE_DEG)
        /* Fixed modelview angle for every draw path. */
        glRotateXi(TWLHD_DEMO_STATIC_ANGLE_BIN);
        glRotateYi(TWLHD_DEMO_STATIC_ANGLE_BIN);
#else
        glRotateXi((int)angle);
        glRotateYi((int)angle);
#endif
        glScalef32(floattof32(0.12), floattof32(0.12), floattof32(0.12));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE); /* mirrors the single-cube path */
        draw_cube();
        glPopMatrix(1);
    }

    glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
#endif
}
