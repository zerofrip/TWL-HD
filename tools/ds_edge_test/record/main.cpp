// SPDX-License-Identifier: Apache-2.0
//
// ds-edge-record: the ds-edge-test scene built with the TWL-HD GX recorder.
//
// It draws the exact geometry of source/main.c (quads.h) and writes the
// completed v2 GX frame to /twlhd_frame_v2.bin, the way arm9v2's dump_frame()
// does (arm9v2/source/main.cpp:90-170,240-268). Only this ROM links common/
// code; the plain ds-edge-test target stays libnds-only.
//
// The recorder must be armed before any state the frame needs (viewport,
// projection), so the setup order differs from source/main.c: glInit() first,
// then init, then the recorded state, then arm, draw, glFlush().

#include "twlhd_gx_shim.hpp"

#include <fat.h>
#include <nds.h>
#include <stdio.h>

#include "quads.h"
#include "twlhd_gx_record.h"

/* Static, heap-free: ~135 KB. */
static uint8_t g_serialize[TWLHD_GX_REC_SERIALIZE_MAX];
static int g_fat_ready;

/* libnds GL2D's gxVertex3i: raw 16-bit vertex, no 4.12 shift. */
static inline void vertex_px(int x, int y, int z) {
    glVertex3v16((v16)x, (v16)y, (v16)z);
}

static void setup_projection(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof32(0, 256, 192, 0, -1 << 12, 1 << 12);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_poly(const EdgePoly *p) {
    glPolyFmt(POLY_ALPHA(p->alpha) | POLY_CULL_NONE | POLY_ID(p->poly_id));
    glColor(RGB15(p->r5, p->g5, p->b5));
    glBegin(GL_QUADS);
    vertex_px(p->x, p->y, p->z);
    vertex_px(p->x + p->w, p->y, p->z);
    vertex_px(p->x + p->w, p->y + p->h, p->z);
    vertex_px(p->x, p->y + p->h, p->z);
    glEnd();
}

static void draw_scene(void) {
    int i;
    for (i = 0; i < (int)EDGE_BACKDROP_COUNT; i++)
        draw_poly(&EDGE_BACKDROPS[i]);
    for (i = 0; i < (int)EDGE_QUAD_COUNT; i++)
        draw_poly(&EDGE_QUADS[i]);
}

/* Same sequence as arm9v2/source/main.cpp:90-170. */
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
    TwlhdGxRecorder *rec = twlhd_gx_rec_global();

    videoSetMode(MODE_0_3D);
    glInit();

    twlhd_gx_rec_init(rec);

    consoleDemoInit();  // sub engine, VRAM_C, BG0; not part of the frame

    /* Armed before every state write the frame needs, so the frame carries the
     * clear state, viewport and projection (arm9v2 draws after arm as well). */
    twlhd_gx_rec_arm(rec);
    glDisable(GL_BLEND | GL_TEXTURE_2D | GL_ALPHA_TEST | GL_ANTIALIAS |
              GL_OUTLINE | GL_FOG);
    glClearColor(DSEDGE_CLEAR_R, DSEDGE_CLEAR_G, DSEDGE_CLEAR_B, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    setup_projection();

    draw_scene();
    glFlush(0);  // records SWAP_BUFFERS and completes the frame
    (void)dump_frame();

    while (1)
        swiWaitForVBlank();

    return 0;
}
