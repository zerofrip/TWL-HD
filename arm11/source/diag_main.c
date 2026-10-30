// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C - ARM11 hardware-hang bisecting diagnostic.
 *
 * Stage 0 boots with gfxInitDefault() + a bottom-screen console only: no
 * renderer_init(), no fsInit(), no RTCom, no SD access. The renderer is
 * brought up on demand and every citro3d/PICA step is printed BEFORE it runs,
 * so the last marker left on the bottom screen names the call that stalled.
 *
 * A = renderer_set_trace() + renderer_init()
 * B = draw one frame (trace around FrameBegin/draw/FrameEnd)
 * X = draw frames in a loop, START escapes back to the menu
 *
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy.
 */

#include <3ds.h>
#include <citro3d.h>

#include <stdio.h>

#include "renderer.h"

#define MARKER_RING 8u

/* Last MARKER_RING trace steps, newest at (g_marker_next - 1). */
static const char *g_markers[MARKER_RING];
static unsigned g_marker_next;
static int g_frame_count;
static unsigned long g_vblank_count;
/* Set between C3D_FrameBegin and C3D_FrameEnd: no swap may happen in there. */
static int g_in_frame;

/* Runs immediately before each traced citro3d/PICA step. A C3D frame may be
 * open, so only the display swap is conditional; the print itself writes into
 * the single-buffered bottom framebuffer and is visible right away.
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy. */
static void trace_cb(const char *step) {
    printf("  > %s\n", step);
    gfxFlushBuffers();
    if (!g_in_frame) {
        /* citro3d owns the top-screen present via C3D_FrameEnd; swap bottom only. */
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
    g_markers[g_marker_next & (MARKER_RING - 1u)] = step;
    g_marker_next++;
}

static void print_markers(void) {
    unsigned count = g_marker_next < MARKER_RING ? g_marker_next : MARKER_RING;
    unsigned first = g_marker_next - count;
    unsigned i;

    printf("last %u markers:\n", count);
    for (i = 0; i < count; i++) {
        printf("  %s\n", g_markers[(first + i) & (MARKER_RING - 1u)]);
    }
}

static void render_status(const char *status) {
    consoleClear();
    printf("TWL-HD arm11 hardware-hang bisect\n");
    printf("HARDWARE UNVERIFIED (build-only) - do not deploy\n\n");
    printf("A=renderer_init  B=one frame  X=loop\n");
    printf("START=exit\n\n");
    printf("status: %s\n", status);
    printf("frames: %d  vblanks: %lu\n\n", g_frame_count, g_vblank_count);
    print_markers();
    gfxFlushBuffers();
    /* citro3d owns the top-screen present via C3D_FrameEnd; swap bottom only. */
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

static int draw_one_frame(void) {
    int began;

    renderer_trace("FrameBegin");
    began = C3D_FrameBegin(C3D_FRAME_SYNCDRAW) ? 1 : 0;
    if (began) {
        g_in_frame = 1;
        renderer_draw();
        renderer_trace("C3D_FrameEnd");
        C3D_FrameEnd(0);
        g_in_frame = 0;
    }
    renderer_trace("FrameEnd done");
    if (began) g_frame_count++;
    return began;
}

int main(void) {
    char status[64];
    int exit_loop = 0;

    gfxInitDefault();
    gfxSet3D(false);
    /* Single-buffer the console so a marker printed mid-stall is already on the
     * scanned-out buffer; doubles would leave every other marker hidden until
     * the next swap, which never comes if the GPU call hangs. */
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_BOTTOM, NULL);

    snprintf(status, sizeof status, "stage 0: renderer NOT initialized");
    render_status(status);

    while (aptMainLoop() && !exit_loop) {
        u32 keys;

        hidScanInput();
        keys = hidKeysDown();

        if (keys & KEY_START) break;

        if (keys & KEY_A) {
            int result;

            if (renderer_is_ready()) {
                snprintf(status, sizeof status, "already initialized");
            } else {
                renderer_set_trace(trace_cb);
                result = renderer_init();
                snprintf(status, sizeof status, "renderer_init: %s",
                         renderer_result_string(result));
            }
            render_status(status);
        }

        if (keys & KEY_B) {
            if (!renderer_is_ready()) {
                snprintf(status, sizeof status, "B: press A first");
            } else if (draw_one_frame()) {
                snprintf(status, sizeof status, "one frame drawn");
            } else {
                snprintf(status, sizeof status, "FrameBegin refused");
            }
            render_status(status);
        }

        if (keys & KEY_X) {
            if (!renderer_is_ready()) {
                snprintf(status, sizeof status, "X: press A first");
                render_status(status);
            } else {
                while (aptMainLoop()) {
                    draw_one_frame();
                    snprintf(status, sizeof status,
                             "looping, START stops");
                    render_status(status);
                    g_vblank_count++;
                    gspWaitForVBlank();
                    hidScanInput();
                    if (hidKeysDown() & KEY_START) {
                        exit_loop = 1;
                        break;
                    }
                }
            }
        }

        g_vblank_count++;
        gspWaitForVBlank();
    }

    renderer_shutdown();
    gfxExit();
    return 0;
}
