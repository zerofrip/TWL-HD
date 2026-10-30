// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C - ARM11 hardware-hang bisecting diagnostic (build v5).
 *
 * Stage 0 boots with gfxInitDefault() + a bottom-screen console only: no
 * renderer_init(), no fsInit(), no RTCom, no SD access. The renderer is
 * brought up on demand and every citro3d/PICA step is traced BEFORE it runs,
 * so the last marker on the bottom screen names the call that stalled.
 *
 * UI FLAGS (8 toggleable rows; hidden flags still work if set by other code):
 *   SKIP_DRAW      -> do not call C3D_DrawArrays.
 *   OFFICIAL_ATTR  -> simple_tri-style attribute/buffer path.
 *   FIXED48        -> one loader, fixed white, stride 48.
 *   PACKED28       -> two loaders, 28-byte packed buffer.
 *   SPLIT          -> two loaders, two non-interleaved buffers.
 *   STRIDE         -> two loaders, parametric stride/count/pad.
 *   NORMAL_COPY    -> normal layout from a private 48-byte copy.
 *   PAD_DECL       -> declare vertex padding so declared bytes == stride.
 *
 * Layout flags are mutually exclusive. If several are set the renderer uses
 * this precedence:
 *   OFFICIAL_ATTR > LAYOUT_SPLIT > LAYOUT_STRIDE > LAYOUT_PACKED28
 *     > LAYOUT_FIXED48 > LAYOUT_NORMAL_COPY
 * and falls back to the normal 48-byte interleaved layout when none are set.
 *
 * STRIDE PARAMETERS (adjust with LEFT/RIGHT on the parameter rows):
 *   STRIDE  {28,32,36,40,44,48,52,56,64,96} bytes
 *   VERTS   {3,6,36,300,3000} vertices (multiple of 3)
 *   PAD     {0,256,4096} bytes
 * Parameters take effect on the next B together with renderer_set_debug_flags().
 *
 * HOW TO READ: GPU work starts at C3D_FrameEnd, so a hung GPU typically shows
 * up as the NEXT frame's FrameBegin (or the probe's NONBLOCK FrameBegin)
 * never completing. The B button now runs exactly ONE frame and then probes
 * for up to 120 vblanks using C3D_FrameBegin(C3D_FRAME_NONBLOCK):
 *   "COMPLETED after K vblanks"  -> the previous frame was processed.
 *   "NOT COMPLETE after 120 vblanks (GPU wedged?)" -> the GPU never signalled.
 * Result lines show the effective hex mask (%04lX) and the active stride,
 * vertex count and pad: %04lX s%u v%u p%u f%d OK k=%d
 *                                            ... STUCK 120.
 *
 * Run 5 plan (PAD_DECL hypothesis):
 *   1) L, PAD_DECL on, B            -> real NORMAL path with padding;
 *                                      expect OK + RGB triangle if the
 *                                      hypothesis holds.
 *   2) STRIDE + PAD_DECL, VERTS 36/300/3000 at STRIDE=48 (size check).
 *   3) STRIDE sweep across the STRIDE presets with PAD_DECL on.
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

#define MARKER_RING 6u
#define RESULT_RING 4u
#define PROBE_VBLANKS 120

/* Ring indexing uses modulo (%), so the sizes do not need to be powers of two.
 * A zero size would divide by zero, so assert it is nonzero. */
_Static_assert(MARKER_RING != 0, "MARKER_RING must be nonzero");
_Static_assert(RESULT_RING != 0, "RESULT_RING must be nonzero");

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
    {TWLHD_RDBG_SKIP_DRAW,         "SKIP_DRAW"},
    {TWLHD_RDBG_OFFICIAL_ATTR,     "OFFICIAL_ATTR"},
    {TWLHD_RDBG_LAYOUT_FIXED48,    "FIXED48"},
    {TWLHD_RDBG_LAYOUT_PACKED28,   "PACKED28"},
    {TWLHD_RDBG_LAYOUT_SPLIT,      "SPLIT"},
    {TWLHD_RDBG_LAYOUT_STRIDE,     "STRIDE"},
    {TWLHD_RDBG_LAYOUT_NORMAL_COPY,"NORMAL_COPY"},
    {TWLHD_RDBG_PAD_DECL,          "PAD_DECL"},
};

#define FLAG_COUNT (sizeof g_flag_defs / sizeof g_flag_defs[0])

/* Parameter rows that follow the flag rows in the UI. */
#define PARAM_COUNT 3

static const char *g_param_names[PARAM_COUNT] = {
    "STRIDE", "VERTS", "PAD"
};

static uint32_t g_param_values[PARAM_COUNT] = {
    48, 3, 0
};

static const uint32_t g_stride_presets[] = {
    28, 32, 36, 40, 44, 48, 52, 56, 64, 96
};
static const uint32_t g_verts_presets[] = {
    3, 6, 36, 300, 3000
};
static const uint32_t g_pad_presets[] = {
    0, 256, 4096
};

static const struct {
    const uint32_t *presets;
    unsigned count;
} g_param_presets[PARAM_COUNT] = {
    {g_stride_presets, sizeof g_stride_presets / sizeof g_stride_presets[0]},
    {g_verts_presets,  sizeof g_verts_presets  / sizeof g_verts_presets[0]},
    {g_pad_presets,    sizeof g_pad_presets    / sizeof g_pad_presets[0]},
};

/* Flags that affect renderer_init() only; all other flags are read at draw
 * time and can be changed without re-initializing. The hidden init-time flags
 * are no longer toggleable in the UI, so this path is kept only for safety. */
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
    g_markers[g_marker_next % MARKER_RING] = step;
    g_marker_next++;
}

static void add_result(const char *fmt, ...) {
    va_list ap;
    char *slot = g_results[g_result_next % RESULT_RING];

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
        const char *m = g_markers[(first + i - 1) % MARKER_RING];
        printf("  %.37s\n", m ? m : "-");
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
        const char *r = g_results[(first + i - 1) % RESULT_RING];
        printf("%.39s\n", r[0] ? r : " ");
    }
}

static int param_preset_index(unsigned param) {
    const uint32_t *presets = g_param_presets[param].presets;
    unsigned count = g_param_presets[param].count;
    unsigned i;
    for (i = 0; i < count; i++) {
        if (presets[i] == g_param_values[param]) return (int)i;
    }
    return 0;
}

static void adjust_param(unsigned param, int delta) {
    const uint32_t *presets = g_param_presets[param].presets;
    unsigned count = g_param_presets[param].count;
    int idx = param_preset_index(param);
    idx += delta;
    while (idx < 0) idx += (int)count;
    while (idx >= (int)count) idx -= (int)count;
    g_param_values[param] = presets[idx];
}

static void print_menu(void) {
    unsigned i;
    unsigned total_rows = FLAG_COUNT + PARAM_COUNT;

    for (i = 0; i < total_rows; i++) {
        char cursor = (i == (unsigned)g_cursor) ? '>' : ' ';
        if (i < FLAG_COUNT) {
            char mark = (g_flags & g_flag_defs[i].bit) ? 'x' : ' ';
            const char *hint = "";
            if ((g_flag_defs[i].bit & INIT_FLAGS) &&
                ((g_flags & g_flag_defs[i].bit) !=
                 (g_init_flags & g_flag_defs[i].bit))) {
                hint = " *";
            }
            printf("%c %-13s [%c]%s\n",
                   cursor, g_flag_defs[i].name, mark, hint);
        } else {
            unsigned p = i - FLAG_COUNT;
            printf("%c %-8s [%lu]\n",
                   cursor, g_param_names[p],
                   (unsigned long)g_param_values[p]);
        }
    }
}

static void update_heartbeat(void) {
    u16 w, h;
    u8 *fb;

    /* Redraw only the heartbeat counter on the title line (1-based ANSI).
     * Col 33 + "hb:%4lu" ends at col 39, inside the 40-column budget.
     * Modulo keeps the counter from ever growing past four digits. */
    printf("\x1b[1;33Hhb:%4lu", g_vblank_count % 10000UL);
    fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, (u32)(w * h * 3));
}

/* Screen budget for the bottom console: 40 columns x 30 rows.
 * libctru scrolls once the 30th newline is emitted, so the UI must use at
 * most 29 newline-terminated rows. Every printed line must be <=39 visible
 * characters. Exact row budget:
 *   1  title + heartbeat
 *   8  flags
 *   3  parameter rows
 *   1  key legend
 *   1  "PAD_DECL: declare vertex padding" hint
 *   1  status
 *   1  results header
 *   4  result rows
 *   1  markers header
 *   6  marker rows
 *  ---
 *  27  total (<=29)
 *
 * Visible-character budget check for every printf in this file:
 *   "TWL-HD arm11 diag v5"              21
 *   "%c %-13s [%c]%s"                   21 max (1+1+13+1+3+2)
 *   "%c %-8s [%lu]"                     21 max (1+1+8+1+5+4)
 *   "A=tog U/D=mv L/R=adj L=init B=go"  34
 *   "PAD_DECL: declare vertex padding"  34
 *   "status: %.31s"                     39
 *   "last %u results:"                  15
 *   "%.39s" (result row)                39
 *   "last %u markers:"                  15
 *   "  -"                               3
 *   "  %.37s" (marker row)              39
 *   "hb:%4lu" (col 33..39)              7
 */
static void render_ui(const char *status) {
    consoleClear();
    printf("TWL-HD arm11 diag v5\n");
    print_menu();
    printf("A=tog U/D=mv L/R=adj L=init B=go\n");
    printf("PAD_DECL: declare vertex padding\n");
    printf("status: %.31s\n", status);
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
            add_result("%04lX s%u v%u p%u f%d OK k=%d",
                       (unsigned long)effective_flags(),
                       (unsigned)g_param_values[0], (unsigned)g_param_values[1],
                       (unsigned)g_param_values[2], g_frame_count, k + 1);
            break;
        }
    }

    if (!completed) {
        add_result("%04lX s%u v%u p%u f%d STUCK %d",
                   (unsigned long)effective_flags(),
                   (unsigned)g_param_values[0], (unsigned)g_param_values[1],
                   (unsigned)g_param_values[2], g_frame_count,
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
    renderer_set_debug_stride(g_param_values[0]);
    renderer_set_debug_vertex_count(g_param_values[1]);
    renderer_set_debug_pad(g_param_values[2]);
    renderer_set_debug_flags(g_flags);
    g_init_flags = g_flags;
    result = renderer_init();
    snprintf(status, status_size, "init: %.24s",
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
    snprintf(status, sizeof status, "stage0: no renderer");

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
            if (g_cursor < (int)(FLAG_COUNT + PARAM_COUNT) - 1) g_cursor++;
            need_redraw = 1;
        }
        if (keys & KEY_A) {
            if (g_cursor < (int)FLAG_COUNT) {
                g_flags ^= g_flag_defs[g_cursor].bit;
                snprintf(status, sizeof status, "toggled %s",
                         g_flag_defs[g_cursor].name);
            }
            /* A on a parameter row does nothing. */
            need_redraw = 1;
        }
        if (keys & KEY_LEFT) {
            if (g_cursor >= (int)FLAG_COUNT) {
                adjust_param((unsigned)g_cursor - FLAG_COUNT, -1);
                need_redraw = 1;
            }
        }
        if (keys & KEY_RIGHT) {
            if (g_cursor >= (int)FLAG_COUNT) {
                adjust_param((unsigned)g_cursor - FLAG_COUNT, 1);
                need_redraw = 1;
            }
        }

        if (keys & KEY_L) {
            reinit_renderer(status, sizeof status);
            need_redraw = 1;
        }

        if (keys & KEY_B) {
            if (!renderer_is_ready()) {
                snprintf(status, sizeof status, "B: press L first");
            } else {
                renderer_set_debug_stride(g_param_values[0]);
                renderer_set_debug_vertex_count(g_param_values[1]);
                renderer_set_debug_pad(g_param_values[2]);
                renderer_set_debug_flags(g_flags);
                if (draw_one_frame()) {
                    probe_previous_frame(status, sizeof status);
                } else {
                    add_result("%04lX s%u v%u p%u f%d REFUSED",
                               (unsigned long)effective_flags(),
                               (unsigned)g_param_values[0],
                               (unsigned)g_param_values[1],
                               (unsigned)g_param_values[2],
                               g_frame_count);
                }
                if (!g_wedged) {
                    snprintf(status, sizeof status, "one frame probed");
                }
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
