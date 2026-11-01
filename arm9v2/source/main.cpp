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

#include "demo_frame.hpp"
#include "twlhd_gx_record.h"

/* Opt-in test build: dump the frame automatically (0 disables it). */
#ifndef TWLHD_DEMO_AUTODUMP_FRAME
#define TWLHD_DEMO_AUTODUMP_FRAME 0
#endif

/* Static, heap-free: ~270 KB total on the DS. */
static uint8_t g_serialize[TWLHD_GX_REC_SERIALIZE_MAX];
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

    twlhd_gx_rec_init(rec);
    refresh_console();

    while (1) {
        twlhd_gx_rec_arm(rec);
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
