// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C - ARM11 hardware-hang bisecting diagnostic (build v2).
 *
 * Stage 0 boots with gfxInitDefault() + a bottom-screen console only: no
 * renderer_init(), no fsInit(), no RTCom, no SD access. The renderer is
 * brought up on demand and every citro3d/PICA step is traced BEFORE it runs,
 * so the last marker on the bottom screen names the call that stalled.
 *
 * TEST MATRIX the tester should run (flags are independent toggles):
 *   1) L then B with all flags OFF  -> baseline.
 *   2) L then B with SKIP_DRAW      -> [clear only] test.
 *   3) L then B with SKIP_CLEAR     -> [draw only] test.
 *   4) L then B with OFFICIAL_ATTR  -> simple_tri-style attribute/buffer path.
 *   5) L then B with OFFICIAL_ATTR + ORTHO + NO_STATE + NO_UNIFORMS
 *      -> minimal official-style draw path.
 *   6) L then B with ORTHO + NO_STATE + NO_UNIFORMS + NO_BIND
 *      -> minimal existing draw path.
 *
 * HOW TO READ: GPU work starts at C3D_FrameEnd, so a hung GPU typically shows
 * up as the NEXT frame's FrameBegin (or the probe's NONBLOCK FrameBegin)
 * never completing. The B button now runs exactly ONE frame and then probes
 * for up to 120 vblanks using C3D_FrameBegin(C3D_FRAME_NONBLOCK):
 *   "COMPLETED after K vblanks"  -> the previous frame was processed.
 *   "NOT COMPLETE after 120 vblanks (GPU wedged?)" -> the GPU never signalled.
 *
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy.
 */

#include <3ds.h>
#include <citro3d.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "renderer.h"

#define MARKER_RING 8u
#define RESULT_RING 6u
#define PROBE_VBLANKS 120

/* Last MARKER_RING trace steps, newest at (g_marker_next - 1). */
static const char *g_markers[MARKER_RING];
static unsigned g_marker_next;

/* Last RESULT_RING probe results, newest at (g_result_next - 1). */
static char g_results[RESULT_RING][80];
static unsigned g_result_next;

static int g_frame_count;
static unsigned long g_vblank_count;
/* Set between C3D_FrameBegin and C3D_FrameEnd: no swap may happen in there. */
static int g_in_frame;

static uint32_t g_flags;
static uint32_t g_init_flags;
static int g_cursor;
static int g_wedged;

/* Flag metadata for the UI. Order matches the cursor. */
static const struct {
    uint32_t bit;
    const char *name;
} g_flag_defs[] = {
    {TWLHD_RDBG_SKIP_DRAW,     "SKIP_DRAW"},
    {TWLHD_RDBG_SKIP_CLEAR,    "SKIP_CLEAR"},
    {TWLHD_RDBG_OFFICIAL_ATTR, "OFFICIAL_ATTR"},
    {TWLHD_RDBG_ORTHO,         "ORTHO"},
    {TWLHD_RDBG_NO_STATE,      "NO_STATE"},
    {TWLHD_RDBG_NO_UNIFORMS,   "NO_UNIFORMS"},
    {TWLHD_RDBG_NO_BIND,       "NO_BIND"},
};

#define FLAG_COUNT (sizeof g_flag_defs / sizeof g_flag_defs[0])

/* Flags that affect renderer_init() only; all other flags are read at draw
 * time and can be changed without re-initializing. */
#define INIT_FLAGS (TWLHD_RDBG_ORTHO | TWLHD_RDBG_NO_STATE)

/* Effective flags the renderer actually used: draw-time flags from the
 * renderer's current setting plus init-time flags captured at the last L. */
static uint32_t effective_flags(void) {
    return (renderer_get_debug_flags() & ~INIT_FLAGS) |
           (g_init_flags & INIT_FLAGS);
}

/* Runs immediately before each traced citro3d/PICA step. A C3D frame may be
 * open, so only the display swap is conditional; the print itself would
 * disturb the fixed UI layout, so only the marker ring is updated here.
 * The bottom framebuffer is flushed through the kernel cache (not GSP),
 * because gfxFlushBuffers can block if GSP is wedged, which would hide the
 * last marker before the hang.
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy. */
static void trace_cb(const char *step) {
    u16 w, h;
    u8 *fb;

    /* Flush the bottom framebuffer through the kernel cache, not GSP. */
    fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, (u32)(w * h * 3));
    if (!g_in_frame) {
        /* citro3d owns the top-screen present via C3D_FrameEnd; swap bottom only. */
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
    g_markers[g_marker_next & (MARKER_RING - 1u)] = step;
    g_marker_next++;
}

static void add_result(const char *fmt, ...) {
    va_list ap;
    char *slot = g_results[g_result_next & (RESULT_RING - 1u)];

    va_start(ap, fmt);
    vsnprintf(slot, sizeof g_results[0], fmt, ap);
    va_end(ap);
    g_result_next++;
}

static void print_markers(void) {
    unsigned count = g_marker_next < MARKER_RING ? g_marker_next : MARKER_RING;
    unsigned first = g_marker_next - count;
    unsigned i;

    printf("last %u markers:\n", MARKER_RING);
    for (i = MARKER_RING; i > count; i--) {
        printf("  -\n");
    }
    for (i = count; i > 0; i--) {
        const char *m = g_markers[(first + i - 1) & (MARKER_RING - 1u)];
        printf("  %s\n", m ? m : "-");
    }
}

static void print_results(void) {
    unsigned count = g_result_next < RESULT_RING ? g_result_next : RESULT_RING;
    unsigned first = g_result_next - count;
    unsigned i;

    printf("last %u results:\n", RESULT_RING);
    for (i = RESULT_RING; i > count; i--) {
        printf("\n");
    }
    for (i = count; i > 0; i--) {
        const char *r = g_results[(first + i - 1) & (RESULT_RING - 1u)];
        printf("%s\n", r[0] ? r : " ");
    }
}

static void print_flags(void) {
    unsigned i;
    for (i = 0; i < FLAG_COUNT; i++) {
        char cursor = (i == (unsigned)g_cursor) ? '>' : ' ';
        char mark = (g_flags & g_flag_defs[i].bit) ? 'x' : ' ';
        const char *hint = "";
        if ((g_flag_defs[i].bit & INIT_FLAGS) &&
            ((g_flags & g_flag_defs[i].bit) !=
             (g_init_flags & g_flag_defs[i].bit))) {
            hint = " *";
        }
        printf("%c %-13s [%c]%s\n", cursor, g_flag_defs[i].name, mark, hint);
    }
}

static void update_heartbeat(void) {
    u16 w, h;
    u8 *fb;

    /* Redraw only the heartbeat counter on the title line (1-based ANSI).
     * Col 33 + "hb:%4lu" ends at col 39, inside the 40-column budget. */
    printf("\x1b[1;33Hhb:%4lu", g_vblank_count);
    fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, (u32)(w * h * 3));
}

/* Screen budget for the bottom console: 40 columns x 30 rows.
 * Every printed line must be <=39 characters. Exact row budget:
 *   1  title + heartbeat
 *   1  subtitle
 *   1  blank
 *   7  flags
 *   1  blank
 *   1  key legend
 *   1  "ORTHO,NO_STATE need L" hint
 *   1  status
 *   1  results header
 *   6  result rows
 *   1  markers header
 *   8  marker rows
 *  ---
 *  30  total
 */
static void render_ui(const char *status) {
    consoleClear();
    printf("TWL-HD arm11 diag v2\n");
    printf("HARDWARE UNVERIFIED\n");
    printf("\n");
    print_flags();
    printf("\n");
    printf("A=tog U/D=mv L=init B=1frm START=exit\n");
    printf("ORTHO,NO_STATE need L\n");
    printf("status: %s\n", status);
    print_results();
    print_markers();
    if (g_wedged) {
        /* Once the GPU is wedged, avoid gfxFlushBuffers/gfxScreenSwapBuffers
         * because they may block on GSP. The single-buffered console is
         * already visible after a kernel cache flush. */
        u16 w, h;
        u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb,
                                 (u32)(w * h * 3));
    } else {
        gfxFlushBuffers();
        /* citro3d owns the top-screen present via C3D_FrameEnd; swap bottom only. */
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
    }
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

static void probe_previous_frame(char *status, size_t status_size) {
    int completed = 0;
    int k;

    renderer_trace("probe");
    for (k = 0; k < PROBE_VBLANKS; k++) {
        gspWaitForVBlank();
        g_vblank_count++;
        if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
            C3D_FrameEnd(0);
            completed = 1;
            add_result("%02lX f%d OK k=%d",
                       (unsigned long)effective_flags(), g_frame_count, k + 1);
            break;
        }
    }

    if (!completed) {
        add_result("%02lX f%d STUCK %d",
                   (unsigned long)effective_flags(), g_frame_count,
                   PROBE_VBLANKS);
        g_wedged = 1;
        snprintf(status, status_size, "GPU WEDGED - hold POWER");
    }
}

static void reinit_renderer(char *status, size_t status_size) {
    int result;

    if (renderer_is_ready()) {
        renderer_shutdown();
    }
    renderer_set_trace(trace_cb);
    renderer_set_debug_flags(g_flags);
    g_init_flags = g_flags;
    result = renderer_init();
    snprintf(status, status_size, "renderer_init: %s",
             renderer_result_string(result));
}

int main(void) {
    char status[80];
    int need_redraw = 1;

    gfxInitDefault();
    gfxSet3D(false);
    /* Single-buffer the console so a marker printed mid-stall is already on the
     * scanned-out buffer; doubles would leave every other marker hidden until
     * the next swap, which never comes if the GPU call hangs. */
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_BOTTOM, NULL);

    g_flags = 0;
    g_init_flags = 0;
    g_cursor = 0;
    g_wedged = 0;
    snprintf(status, sizeof status, "stage 0: renderer NOT initialized");

    while (aptMainLoop()) {
        u32 keys;

        hidScanInput();
        keys = hidKeysDown();

        if (g_wedged) {
            /* Once a frame never completed, do not touch the GPU again.
             * renderer_shutdown() would block draining the citro3d queue, and
             * gfxExit() also talks to GSP. We keep looping with the wedged
             * message and let the user power off the console. */
            if (need_redraw) {
                render_ui(status);
                need_redraw = 0;
            }
            update_heartbeat();
            g_vblank_count++;
            gspWaitForVBlank();
            continue;
        }

        if (keys & KEY_START) break;

        if (keys & KEY_UP) {
            if (g_cursor > 0) g_cursor--;
            need_redraw = 1;
        }
        if (keys & KEY_DOWN) {
            if (g_cursor < (int)FLAG_COUNT - 1) g_cursor++;
            need_redraw = 1;
        }
        if (keys & KEY_A) {
            g_flags ^= g_flag_defs[g_cursor].bit;
            snprintf(status, sizeof status, "toggled %s",
                     g_flag_defs[g_cursor].name);
            need_redraw = 1;
        }

        if (keys & KEY_L) {
            reinit_renderer(status, sizeof status);
            need_redraw = 1;
        }

        if (keys & KEY_B) {
            if (!renderer_is_ready()) {
                snprintf(status, sizeof status, "B: press L first");
            } else {
                renderer_set_debug_flags(g_flags);
                if (draw_one_frame()) {
                    probe_previous_frame(status, sizeof status);
                } else {
                    add_result("%02lX f%d REFUSED",
                               (unsigned long)effective_flags(),
                               g_frame_count);
                }
                snprintf(status, sizeof status, "one frame probed");
            }
            need_redraw = 1;
        }

        if (need_redraw) {
            render_ui(status);
            need_redraw = 0;
        }
        update_heartbeat();

        g_vblank_count++;
        gspWaitForVBlank();
    }

    if (g_wedged) {
        /* Once the GPU is wedged, never reach renderer_shutdown() or gfxExit()
         * on any exit path (including aptMainLoop() returning false). Stay in a
         * heartbeat loop and let the user power off. HOME handling may also
         * hang; this is acceptable because the GPU is wedged and no further
         * damage is possible. */
        while (1) {
            gspWaitForVBlank();
            update_heartbeat();
            g_vblank_count++;
        }
    }

    renderer_shutdown();
    gfxExit();
    return 0;
}
