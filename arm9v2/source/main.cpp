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

#ifndef TWLHD_DEMO_DIAGSCREEN
#define TWLHD_DEMO_DIAGSCREEN 0
#endif

#if TWLHD_DEMO_DIAGSCREEN == 2
/* Recorder bypass: compile libnds' inline helpers against raw GFX_* macros. */
#include <fat.h>
#include <nds.h>
#include "twlhd_gx_shim.hpp"
#else
#include "twlhd_gx_shim.hpp"

#include <fat.h>
#include <nds.h>
#endif
#include <stdio.h>

#include "demo_frame.hpp"
#include "twlhd_gx_record.h"
#include "twlhd_tex_capture.h"
#include "twlhd_tex_source.h"

/* Opt-in test build: dump the frame automatically (0 disables it). */
#ifndef TWLHD_DEMO_AUTODUMP_FRAME
#define TWLHD_DEMO_AUTODUMP_FRAME 0
#endif

#if TWLHD_DEMO_SCENE == 5
/* Textured build: larger serialize buffer + planner scratch + source backend. */
static uint8_t g_serialize[TWLHD_GX_REC_SERIALIZE_TEX_MAX];
static TwlhdGxRecTexScratch g_tex_scratch;
static TwlhdTexSourceCtx g_tex_source;
#else
/* Static, heap-free: ~270 KB total on the DS. */
static uint8_t g_serialize[TWLHD_GX_REC_SERIALIZE_MAX];
#endif
static int g_fat_ready;

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
#if TWLHD_DEMO_DIAGSCREEN != 0
    iprintf("DIAGSCREEN=%d ... HARDWARE UNVERIFIED\n",
            TWLHD_DEMO_DIAGSCREEN);
#if TWLHD_DEMO_DIAGSCREEN == 2
    iprintf("recorder disabled\n");
#endif
#endif
    iprintf("frames self-contained except glInit state\n");
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

#if TWLHD_DEMO_SCENE == 5
    {
        TwlhdTexCapLimits limits;
        TwlhdTexCapStats stats;

        twlhd_tex_capture_default_limits(&limits);
        result = twlhd_gx_rec_serialize_tex(
            rec, &g_tex_scratch, g_serialize, sizeof g_serialize, &written,
            &limits, twlhd_tex_source_read, &g_tex_source, &stats);
        if (result == TWLHD_V2_OK) {
            iprintf("textures kept %lu  dropped b/r/u %lu/%lu/%lu\n",
                    (unsigned long)stats.keys_kept,
                    (unsigned long)stats.keys_dropped_budget,
                    (unsigned long)stats.keys_dropped_read_error,
                    (unsigned long)stats.keys_dropped_unsupported);
        }
    }
#else
    result = twlhd_gx_rec_serialize(rec, g_serialize, sizeof g_serialize,
                                    &written);
#endif
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

#if TWLHD_DEMO_DIAGSCREEN == 1
    /* DIAGSCREEN=1: replicate the libnds example display-setup order for the
     * cube scene (videoSetMode -> glInit -> AA/clear/viewport/projection/lookAt
     * -> console -> recorder). HARDWARE UNVERIFIED. */
    videoSetMode(MODE_0_3D);
    glInit(); /* library-internal writes are not proxied: INITIAL_STATE_UNKNOWN */
    glEnable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70, 256.0 / 192.0, 0.1, 40.0);
    gluLookAt(0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
    consoleDemoInit(); /* statistics console on the sub screen */
#elif TWLHD_DEMO_DIAGSCREEN == 4
    /* DIAGSCREEN=4: force the main engine onto the top physical screen. */
    videoSetMode(MODE_0_3D);
    lcdMainOnTop();
    consoleDemoInit(); /* statistics console on the sub screen */
    glInit(); /* library-internal writes are not proxied: INITIAL_STATE_UNKNOWN */
#else
    videoSetMode(MODE_0_3D);
    consoleDemoInit(); /* statistics console on the sub screen */

    glInit(); /* library-internal writes are not proxied: INITIAL_STATE_UNKNOWN */
#endif

#if TWLHD_DEMO_DIAGSCREEN == 3
    /* DIAGSCREEN=3: for SCENE=5, wait one vblank after upload so the
     * LCDC->texture/palette bank switch is settled before the first draw.
     * For the default cube this variant is a no-op beyond the banner.
     * HARDWARE UNVERIFIED. */
#endif

#if TWLHD_DEMO_DIAGSCREEN != 2
    twlhd_gx_rec_init(rec);
#else
    (void)rec; /* recorder bypassed for this variant */
#endif
#if TWLHD_DEMO_SCENE == 5
    twlhd_tex_source_reset(&g_tex_source);
    twlhd_demo_scene5_register_sources(&g_tex_source);
    twlhd_demo_scene5_upload_textures();
#if TWLHD_DEMO_DIAGSCREEN == 3
    swiWaitForVBlank();
#endif
#endif
    refresh_console();

    while (1) {
#if TWLHD_DEMO_DIAGSCREEN != 2
        twlhd_gx_rec_arm(rec);
#endif
        twlhd_demo_draw_frame((unsigned)angle);
        swiWaitForVBlank();

        scanKeys();
        if (keysDown() & KEY_A) {
            (void)dump_frame();
        }

#if TWLHD_DEMO_AUTODUMP_FRAME > 0
        if (frame == TWLHD_DEMO_AUTODUMP_FRAME) {
            (void)dump_frame();
            iprintf("AUTODUMP @ frame %d (test build)\n",
                    TWLHD_DEMO_AUTODUMP_FRAME);
        }
#endif

        /* binary angle: 32768 == 360 degrees */
        angle += 256;

        if (++frame % 60 == 0) refresh_console();
    }

    return 0;
}
