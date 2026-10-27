/*
 * TWL-HD Phase 1 - rotating cube capture demo.
 *
 * Draws a rotating cube on the top screen with the instrumented 3D calls and
 * prints the per-frame command statistics on the bottom screen.
 */

#include <nds.h>
#include <stdio.h>

#include "fifo_hook.h"

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

    fifo_hook_glBegin(GL_QUADS);

    for (face = 0; face < 6; face++) {
        fifo_hook_glNormal(cube_normals[face]);
        fifo_hook_glColor3b(cube_colors[face][0],
                            cube_colors[face][1],
                            cube_colors[face][2]);

        for (corner = 0; corner < 4; corner++) {
            const int16_t *v = cube_vertices[cube_faces[face][corner]];
            fifo_hook_glVertex3v16(v[0], v[1], v[2]);
        }
    }

    fifo_hook_glEnd();
}

static void print_stats(const TwlhdFrame *frame) {
    uint32_t vertices = 0;
    uint32_t colors = 0;
    uint32_t normals = 0;
    uint32_t viewports = 0;
    uint32_t i;

    for (i = 0; i < frame->cmd_count; i++) {
        switch (frame->cmds[i].type) {
        case TWLHD_CMD_VTX_16:     vertices++;  break;
        case TWLHD_CMD_COLOR:      colors++;    break;
        case TWLHD_CMD_NORMAL:     normals++;   break;
        case TWLHD_CMD_VIEWPORT:   viewports++; break;
        default:                                break;
        }
    }

    consoleClear();
    iprintf("TWL-HD Phase 1 capture\n");
    iprintf("format v%lu  frame %lu\n",
            (unsigned long)frame->version, (unsigned long)frame->frame_index);
    iprintf("commands %lu / %d\n",
            (unsigned long)frame->cmd_count, TWLHD_RING_CAPACITY);
    iprintf("VTX_16   %lu\n", (unsigned long)vertices);
    iprintf("COLOR    %lu\n", (unsigned long)colors);
    iprintf("NORMAL   %lu\n", (unsigned long)normals);
    iprintf("VIEWPORT %lu\n", (unsigned long)viewports);
}

int main(void) {
    int angle = 0;
    int frame = 0;

    videoSetMode(MODE_0_3D);
    consoleDemoInit(); /* statistics console on the sub screen */

    glInit();
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    fifo_hook_glViewport(0, 0, 255, 191);

    fifo_hook_init();
    fifo_hook_start_capture();

    fifo_hook_glMatrixMode(GL_PROJECTION);
    fifo_hook_glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 100.0);

    while (1) {
        fifo_hook_glMatrixMode(GL_MODELVIEW);
        fifo_hook_glLoadIdentity();
        gluLookAtf32(0, 0, inttof32(4), 0, 0, 0, 0, inttof32(1), 0);
        glRotateXi(angle);
        glRotateYi(angle);

        draw_cube();
        glFlush(0);
        swiWaitForVBlank();

        /* binary angle: 32768 == 360 degrees */
        angle += 256;

        if (++frame % 60 == 0) {
            print_stats(fifo_hook_get_frame());
        }

        fifo_hook_reset_frame();
    }

    return 0;
}
