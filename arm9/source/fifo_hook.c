/*
 * TWL-HD Phase 1 - GXFIFO capture hook.
 *
 * devkitARM/libnds does not expose a writable interception point for the
 * packed 3D command FIFO: the public gl* helpers write straight to the MMIO
 * registers. The feasible approach is therefore to wrap the handful of
 * libnds primitives that map 1:1 onto a GXFIFO command, call the real libnds
 * function, and replay the same command + parameters into a ring buffer.
 */

#include <nds.h>

#include "fifo_hook.h"

static TwlhdFrame g_frame;
static int g_capturing;

/* GXFIFO parameter word count per command byte (GBATEK 3D I/O ports). */
static const uint8_t g_param_count[256] = {
    [TWLHD_CMD_MTX_MODE]     = 1,
    [TWLHD_CMD_MTX_PUSH]     = 0,
    [TWLHD_CMD_MTX_POP]      = 1,
    [TWLHD_CMD_MTX_STORE]    = 1,
    [TWLHD_CMD_MTX_RESTORE]  = 1,
    [TWLHD_CMD_MTX_IDENTITY] = 0,
    [TWLHD_CMD_MTX_LOAD_4x4] = 16,
    [TWLHD_CMD_MTX_LOAD_4x3] = 12,
    [TWLHD_CMD_MTX_MULT_4x4] = 16,
    [TWLHD_CMD_MTX_MULT_4x3] = 12,
    [TWLHD_CMD_MTX_MULT_3x3] = 9,
    [TWLHD_CMD_COLOR]        = 1,
    [TWLHD_CMD_NORMAL]       = 1,
    [TWLHD_CMD_TEX_COORD]    = 1,
    [TWLHD_CMD_VTX_16]       = 2,
    [TWLHD_CMD_VTX_10]       = 1,
    [TWLHD_CMD_VTX_XY]       = 1,
    [TWLHD_CMD_VTX_XZ]       = 1,
    [TWLHD_CMD_VTX_YZ]       = 1,
    [TWLHD_CMD_BEGIN_VTXS]   = 1,
    [TWLHD_CMD_END_VTXS]     = 0,
    [TWLHD_CMD_VIEWPORT]     = 1,
};

/* Append one decoded command. Silently drops commands once the frame is full. */
static void fifo_hook_record(uint8_t cmd_byte, const uint32_t *params) {
    uint8_t count;
    uint8_t i;
    TwlhdCmd *cmd;

    if (!g_capturing || g_frame.cmd_count >= TWLHD_RING_CAPACITY) return;

    count = g_param_count[cmd_byte];
    if (count > 16) count = 16;

    cmd = &g_frame.cmds[g_frame.cmd_count];
    cmd->type = (TwlhdCmdType)cmd_byte;
    cmd->param_count = count;
    for (i = 0; i < count; i++) cmd->params[i] = params[i];

    g_frame.cmd_count++;
}

void fifo_hook_init(void) {
    g_frame.version = TWLHD_FORMAT_VERSION;
    g_frame.frame_index = 0;
    g_frame.cmd_count = 0;
    g_capturing = 0;
}

void fifo_hook_start_capture(void) {
    g_capturing = 1;
}

void fifo_hook_stop_capture(void) {
    g_capturing = 0;
    fifo_hook_transport_send(&g_frame);
}

const TwlhdFrame *fifo_hook_get_frame(void) {
    return &g_frame;
}

void fifo_hook_reset_frame(void) {
    g_frame.frame_index++;
    g_frame.cmd_count = 0;
}

/* Phase 3 replaces this via the weak symbol; Phase 1 does nothing. */
__attribute__((weak)) void fifo_hook_transport_send(const TwlhdFrame *frame) {
    (void)frame;
}

void fifo_hook_glBegin(uint32_t primitive) {
    uint32_t params[1];

    params[0] = primitive;
    glBegin((GL_GLBEGIN_ENUM)primitive);
    fifo_hook_record(TWLHD_CMD_BEGIN_VTXS, params);
}

void fifo_hook_glEnd(void) {
    glEnd();
    fifo_hook_record(TWLHD_CMD_END_VTXS, 0);
}

void fifo_hook_glVertex3v16(int16_t x, int16_t y, int16_t z) {
    uint32_t params[2];

    params[0] = ((uint32_t)(uint16_t)y << 16) | ((uint16_t)x & 0xFFFF);
    params[1] = (uint32_t)z;
    glVertex3v16((v16)x, (v16)y, (v16)z);
    fifo_hook_record(TWLHD_CMD_VTX_16, params);
}

void fifo_hook_glColor3b(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t params[1];

    params[0] = (uint32_t)RGB15(red >> 3, green >> 3, blue >> 3);
    glColor3b(red, green, blue);
    fifo_hook_record(TWLHD_CMD_COLOR, params);
}

void fifo_hook_glNormal(uint32_t packed_normal) {
    uint32_t params[1];

    params[0] = packed_normal;
    glNormal(packed_normal);
    fifo_hook_record(TWLHD_CMD_NORMAL, params);
}

void fifo_hook_glTexCoord2t16(int16_t u, int16_t v) {
    uint32_t params[1];

    params[0] = (uint32_t)TEXTURE_PACK(u, v);
    glTexCoord2t16((t16)u, (t16)v);
    fifo_hook_record(TWLHD_CMD_TEX_COORD, params);
}

void fifo_hook_glMatrixMode(uint32_t mode) {
    uint32_t params[1];

    params[0] = mode;
    glMatrixMode((GL_MATRIX_MODE_ENUM)mode);
    fifo_hook_record(TWLHD_CMD_MTX_MODE, params);
}

void fifo_hook_glPushMatrix(void) {
    glPushMatrix();
    fifo_hook_record(TWLHD_CMD_MTX_PUSH, 0);
}

void fifo_hook_glPopMatrix(uint32_t num) {
    uint32_t params[1];

    params[0] = num;
    glPopMatrix((int)num);
    fifo_hook_record(TWLHD_CMD_MTX_POP, params);
}

void fifo_hook_glLoadIdentity(void) {
    glLoadIdentity();
    fifo_hook_record(TWLHD_CMD_MTX_IDENTITY, 0);
}

void fifo_hook_glViewport(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    uint32_t params[1];

    params[0] = (uint32_t)x1 | ((uint32_t)y1 << 8) |
                ((uint32_t)x2 << 16) | ((uint32_t)y2 << 24);
    glViewport(x1, y1, x2, y2);
    fifo_hook_record(TWLHD_CMD_VIEWPORT, params);
}
