// SPDX-License-Identifier: Apache-2.0
//
// ds-scale-test: static DS top-screen test pattern for measuring how a
// capture board / New 3DS presents the 256x192 DS top screen.
//
// No 3D, no animation, no TWL-HD GX code: only libnds 2D bitmap registers.

#include <nds.h>
#include <stdio.h>

#include "pattern.h"

// Bit 15 marks a BGR555 bitmap pixel opaque (background.h BgType_Bmp16).
#define PATTERN_BLACK 0x8000

static void paint_op(const PatternOp *op, u16 *gfx) {
    int px, py;

    switch (op->kind) {
    case PATTERN_OP_RECT:
        for (py = op->y; py < op->y + op->h; py++)
            for (px = op->x; px < op->x + op->w; px++)
                gfx[py * PATTERN_WIDTH + px] = op->color;
        break;
    case PATTERN_OP_FRAME:
        for (px = op->x; px < op->x + op->w; px++) {
            gfx[op->y * PATTERN_WIDTH + px] = op->color;
            gfx[(op->y + op->h - 1) * PATTERN_WIDTH + px] = op->color;
        }
        for (py = op->y; py < op->y + op->h; py++) {
            gfx[py * PATTERN_WIDTH + op->x] = op->color;
            gfx[py * PATTERN_WIDTH + op->x + op->w - 1] = op->color;
        }
        break;
    case PATTERN_OP_ALT_COLUMNS:
        for (py = op->y; py < op->y + op->h; py++)
            for (px = op->x; px < op->x + op->w; px++)
                gfx[py * PATTERN_WIDTH + px] =
                    ((px - op->x) % op->period == op->phase)
                        ? op->color : PATTERN_BLACK;
        break;
    case PATTERN_OP_ALT_ROWS:
        for (py = op->y; py < op->y + op->h; py++)
            for (px = op->x; px < op->x + op->w; px++)
                gfx[py * PATTERN_WIDTH + px] =
                    ((py - op->y) % op->period == op->phase)
                        ? op->color : PATTERN_BLACK;
        break;
    case PATTERN_OP_GRADIENT:
        for (py = op->y; py < op->y + op->h; py++)
            for (px = op->x; px < op->x + op->w; px++)
                gfx[py * PATTERN_WIDTH + px] =
                    PATTERN_GRADIENT_BGR555[(px - op->x) / op->step_px];
        break;
    }
}

int main(void) {
    u16 *gfx;
    int bg;
    int i;

    // MODE_5_2D: four 2D backgrounds; only BG3 (the 16-bit bitmap) is shown.
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);

    // The DS 16-bit bitmap sizes have no 256x192 mode, so use 256x256 and
    // paint only the visible 256x192 rows. 256*256*2 B = 128 KB = VRAM_A.
    bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    gfx = bgGetGfxPtr(bg);

    for (i = 0; i < PATTERN_WIDTH * PATTERN_HEIGHT; i++)
        gfx[i] = PATTERN_BLACK;

    for (i = 0; i < (int)PATTERN_OP_COUNT; i++)
        paint_op(&PATTERN_OPS[i], gfx);

    consoleDemoInit();  // sub engine, VRAM_C, BG0
    iprintf("\n  DS SCALE TEST\n  id: %s\n", PATTERN_ID);

    while (1)
        swiWaitForVBlank();

    return 0;
}
