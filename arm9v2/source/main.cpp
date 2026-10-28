// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4D - v2 capture demo (arm9v2).
 *
 * The shim must be included before <nds.h> so every GFX_* / MATRIX_* port macro
 * used by libnds' inline helpers expands to the recording proxy.
 *
 * Per frame: arm the recorder, draw the Phase 1 cube with only header-inline
 * libnds helpers, glFlush(0) (records SWAP_BUFFERS and closes the frame).
 * KEY_A serializes the last complete frame to /twlhd_frame_v2.bin.
 *
 * HARDWARE UNVERIFIED: this target has never run on a DS. It is BUILD VERIFIED
 * only; all capture behavior is host-tested through the shim test.
 */

#include "twlhd_gx_shim.hpp"

#include <fat.h>
#include <nds.h>
#include <stdio.h>

#include "twlhd_gx_record.h"

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

/* Static, heap-free: ~270 KB total on the DS. */
static uint8_t g_serialize[TWLHD_GX_REC_SERIALIZE_MAX];
static int g_fat_ready;

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

static void refresh_console(void) {
    TwlhdGxRecorder *rec = twlhd_gx_rec_global();

    consoleClear();
    iprintf("TWL-HD Phase 4D v2 capture\n");
    iprintf("frame  %lu  flags 0x%02lX\n",
            (unsigned long)twlhd_gx_rec_frame_index(rec),
            (unsigned long)twlhd_gx_rec_flags(rec));
    iprintf("GXCM   %lu / %u\n",
            (unsigned long)twlhd_gx_rec_entry_count(rec),
            TWLHD_GX_REC_MAX_ENTRIES);
    iprintf("RSTA   %lu / %u\n",
            (unsigned long)twlhd_gx_rec_rsta_count(rec),
            TWLHD_GX_REC_MAX_RSTA);
    iprintf("drop   %lu / %lu\n",
            (unsigned long)twlhd_gx_rec_dropped_entries(rec),
            (unsigned long)twlhd_gx_rec_dropped_rsta(rec));
    iprintf("ignore %lu  invalid %lu\n",
            (unsigned long)twlhd_gx_rec_ignored_writes(rec),
            (unsigned long)twlhd_gx_rec_invalid_writes(rec));
    iprintf("v2 capture: HARDWARE UNVERIFIED (build-only)\n");
    iprintf("A = dump /twlhd_frame_v2.bin\n");
}

static int dump_frame(void) {
    TwlhdGxRecorder *rec = twlhd_gx_rec_global();
    TwlhdV2Result result;
    size_t written = 0;
    FILE *fp;

    if ((twlhd_gx_rec_flags(rec) & TWLHD_V2_FLAG_OVERFLOW) != 0u) {
        iprintf("frame overflowed; dump refused\n");
        return -1;
    }
    if ((twlhd_gx_rec_flags(rec) & TWLHD_V2_FLAG_FRAME_COMPLETE) == 0u) {
        iprintf("no complete frame to dump\n");
        return -1;
    }

    result = twlhd_gx_rec_serialize(rec, g_serialize, sizeof g_serialize,
                                    &written);
    if (result != TWLHD_V2_OK) {
        iprintf("serialize failed: %s\n", twlhd_v2_result_string(result));
        return -1;
    }

    /* libfat is initialised on the first dump so boot stays unaffected. */
    if (!g_fat_ready) {
        if (!fatInitDefault()) {
            iprintf("fatInitDefault failed\n");
            return -1;
        }
        g_fat_ready = 1;
    }

    fp = fopen("/twlhd_frame_v2.bin", "wb");
    if (!fp) {
        iprintf("open /twlhd_frame_v2.bin failed\n");
        return -1;
    }

    if (fwrite(g_serialize, 1, written, fp) != written) {
        fclose(fp);
        iprintf("short write\n");
        return -1;
    }
    fclose(fp);

    iprintf("dumped %lu bytes\n", (unsigned long)written);
    return (int)written;
}

int main(void) {
    int angle = 0;
    int frame = 0;
    TwlhdGxRecorder *rec = twlhd_gx_rec_global();

    videoSetMode(MODE_0_3D);
    consoleDemoInit(); /* statistics console on the sub screen */

    glInit(); /* library-internal writes are not proxied: INITIAL_STATE_UNKNOWN */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);

    twlhd_gx_rec_init(rec);
    refresh_console();

    while (1) {
        twlhd_gx_rec_arm(rec);

        glClearColor(0, 0, 0, 31);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
        glRotateXi(angle);
        glRotateYi(angle);
        glTranslatef32(0, 0, 0);
        glScalef32(inttof32(1), inttof32(1), inttof32(1));

        glPushMatrix();
        glPolyFmt(POLY_ALPHA(31));
        draw_cube();
        glPopMatrix(1);

        glFlush(0); /* records SWAP_BUFFERS (0x50) and disarms */
        swiWaitForVBlank();

        scanKeys();
        if (keysDown() & KEY_A) {
            (void)dump_frame();
        }

        /* binary angle: 32768 == 360 degrees */
        angle += 256;

        if (++frame % 60 == 0) refresh_console();
    }

    return 0;
}
