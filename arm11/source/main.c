// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B/4C - ARM11 debug renderer application.
 *
 * Loads a TwlhdFrame dump from sdmc:/twlhd_frame.bin (A), converts it to a
 * non-indexed triangle list and draws it on the top screen with a fixed
 * DEBUG camera. Y restores the deterministic built-in triangle, START exits.
 *
 * The bottom screen console reports what was parsed. Everything here is
 * HARDWARE UNVERIFIED: build-verified only, never run in the sandbox.
 */

#include <3ds.h>
#include <citro3d.h>

#include <stdio.h>
#include <string.h>

#include "renderer.h"
#include "rtcom_transport.h"
#include "twlhd_frame_parse.h"
#include "twlhd_vertex_convert.h"

#define FRAME_PATH "sdmc:/twlhd_frame.bin"
#define MAX_VERTICES (3u * TWLHD_RING_CAPACITY)

typedef struct {
    const char *mode;
    const char *source;
    int have_frame;
    TwlhdParseResult parse_result;
    uint32_t frame_index;
    uint32_t cmd_count;
    uint32_t input_vertices;
    uint32_t triangles;
    TwlhdConvertStats stats;
} SceneInfo;

static uint8_t g_frame_bytes[TWLHD_FRAME_WIRE_SIZE];
static TwlhdParsedFrame g_parsed;
static TwlhdVertex g_vertices[MAX_VERTICES];

static SceneInfo g_scene;
static char g_status[96];
static char g_transport_status[64];
static int g_fs_ready;

static void set_builtin_scene(void) {
    uint32_t renderer_vertices = renderer_vertex_count();

    memset(&g_scene, 0, sizeof g_scene);
    g_scene.mode = "BUILT-IN TRIANGLE";
    g_scene.source = "built-in";
    g_scene.have_frame = 0;
    g_scene.input_vertices = renderer_vertices;
    g_scene.triangles = renderer_vertices / 3u;
}

static int load_sd_frame(uint32_t *out_vertex_count,
                         TwlhdConvertStats *out_stats,
                         TwlhdParseResult *out_parse,
                         uint32_t *out_frame_index,
                         uint32_t *out_cmd_count) {
    FILE *file;
    long size;
    size_t read_bytes;
    TwlhdParseResult parse_result;
    TwlhdConvertResult convert_result;
    uint32_t capacity;
    uint32_t vertex_count;

    if (!g_fs_ready) {
        snprintf(g_status, sizeof g_status, "SD: filesystem init failed");
        return -1;
    }

    file = fopen(FRAME_PATH, "rb");
    if (!file) {
        snprintf(g_status, sizeof g_status, "SD: cannot open %s", FRAME_PATH);
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        snprintf(g_status, sizeof g_status, "SD: seek failed");
        fclose(file);
        return -1;
    }

    size = ftell(file);
    if (size != (long)TWLHD_FRAME_WIRE_SIZE) {
        snprintf(g_status, sizeof g_status,
                 "SD: size %ld, expected %u", size,
                 (unsigned)TWLHD_FRAME_WIRE_SIZE);
        fclose(file);
        return -1;
    }

    rewind(file);
    read_bytes = fread(g_frame_bytes, 1, TWLHD_FRAME_WIRE_SIZE, file);
    if (read_bytes != TWLHD_FRAME_WIRE_SIZE) {
        snprintf(g_status, sizeof g_status,
                 "SD: short read (%u of %u bytes)",
                 (unsigned)read_bytes, (unsigned)TWLHD_FRAME_WIRE_SIZE);
        fclose(file);
        return -1;
    }
    fclose(file);

    parse_result = twlhd_frame_parse(g_frame_bytes, TWLHD_FRAME_WIRE_SIZE,
                                     &g_parsed);
    if (parse_result != TWLHD_PARSE_OK) {
        snprintf(g_status, sizeof g_status, "parse: %s",
                 twlhd_parse_result_string(parse_result));
        return -1;
    }

    capacity = twlhd_convert_vertex_bound(g_parsed.cmd_count);
    if (capacity > MAX_VERTICES) capacity = MAX_VERTICES;

    convert_result = twlhd_convert_frame(&g_parsed, g_vertices, capacity,
                                         &vertex_count, out_stats);
    if (convert_result != TWLHD_CONVERT_OK) {
        snprintf(g_status, sizeof g_status, "convert: %s",
                 convert_result == TWLHD_CONVERT_ERR_CAPACITY ?
                 "ERR_CAPACITY" : "ERR_NULL");
        return -1;
    }

    *out_vertex_count = vertex_count;
    *out_parse = parse_result;
    *out_frame_index = g_parsed.frame_index;
    *out_cmd_count = g_parsed.cmd_count;

    if (vertex_count == 0) {
        snprintf(g_status, sizeof g_status,
                 "SD: frame loaded, 0 triangles (valid)");
    } else {
        snprintf(g_status, sizeof g_status,
                 "SD: frame loaded, %u verts -> %u tris",
                 (unsigned)vertex_count, (unsigned)(vertex_count / 3u));
    }
    return 0;
}

static void update_console(void) {
    consoleClear();
    printf("TWL-HD Phase 4B/4C renderer\n");
    printf("HARDWARE UNVERIFIED (build-only)\n\n");
    printf("mode:   %s\n", g_scene.mode);
    printf("source: %s\n", g_scene.source);
    printf("camera: DEBUG CAMERA (fixed, not DS)\n\n");

    if (g_scene.have_frame) {
        printf("parse:  %s\n",
               twlhd_parse_result_string(g_scene.parse_result));
        printf("frame:  %lu  cmds: %lu\n",
               (unsigned long)g_scene.frame_index,
               (unsigned long)g_scene.cmd_count);
    } else {
        printf("parse:  -- (no SD frame)\n");
        printf("frame:  --  cmds: --\n");
    }

    printf("input verts: %lu  tris: %lu\n",
           (unsigned long)g_scene.input_vertices,
           (unsigned long)g_scene.triangles);
    printf("render verts: %lu\n",
           (unsigned long)renderer_vertex_count());
    printf("unsupported: mtx %lu vp %lu vtx %lu unk %lu\n",
           (unsigned long)g_scene.stats.unsupported_matrix,
           (unsigned long)g_scene.stats.unsupported_viewport,
           (unsigned long)g_scene.stats.unsupported_vertex,
           (unsigned long)g_scene.stats.unsupported_unknown);
    printf("ignored: %lu outside-begin\n",
           (unsigned long)g_scene.stats.ignored_outside_begin);
    printf("dropped: %lu incomplete\n\n",
           (unsigned long)g_scene.stats.dropped_incomplete);

    printf("A=load SD  Y=builtin  START=exit\n");
    printf("%s\n", g_transport_status);
    printf("%s\n", g_status);
}

int main(void) {
    int renderer_status;
    int transport_status;
    int pending_load = 0;
    int pending_builtin = 0;
    uint32_t pending_vertex_count = 0;
    TwlhdConvertStats pending_stats;
    TwlhdParseResult pending_parse = TWLHD_PARSE_OK;
    uint32_t pending_frame_index = 0;
    uint32_t pending_cmd_count = 0;

    memset(&pending_stats, 0, sizeof pending_stats);

    gfxInitDefault();
    gfxSet3D(false);
    consoleInit(GFX_BOTTOM, NULL);

    g_fs_ready = R_SUCCEEDED(fsInit());

    renderer_status = renderer_init();
    if (renderer_status == TWLHD_RENDER_OK) {
        set_builtin_scene();
        snprintf(g_status, sizeof g_status, "ready");
    } else {
        memset(&g_scene, 0, sizeof g_scene);
        g_scene.mode = "RENDERER FAILED";
        g_scene.source = "--";
        snprintf(g_status, sizeof g_status, "renderer: %s",
                 renderer_result_string(renderer_status));
    }

    transport_status = rtcom_transport_init();
    if (transport_status == TWLHD_ERR_NOT_IMPLEMENTED) {
        snprintf(g_transport_status, sizeof g_transport_status,
                 "RTCom transport: not implemented");
    } else {
        snprintf(g_transport_status, sizeof g_transport_status,
                 "RTCom transport: init returned %d", transport_status);
    }

    update_console();

    while (aptMainLoop()) {
        u32 keys;

        hidScanInput();
        keys = hidKeysDown();

        if (keys & KEY_START) break;

        if (!renderer_is_ready()) {
            gspWaitForVBlank();
            continue;
        }

        if (keys & KEY_Y) {
            pending_builtin = 1;
            pending_load = 0;
        }

        if (keys & KEY_A) {
            if (load_sd_frame(&pending_vertex_count, &pending_stats,
                              &pending_parse, &pending_frame_index,
                              &pending_cmd_count) == 0) {
                pending_load = 1;
                pending_builtin = 0;
            }
            update_console();
        }

        if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
            renderer_draw();
            C3D_FrameEnd(0);
        }

        if (pending_builtin) {
            int result = renderer_use_builtin_triangle();

            if (result == TWLHD_RENDER_OK) {
                set_builtin_scene();
                snprintf(g_status, sizeof g_status, "built-in triangle");
            } else {
                snprintf(g_status, sizeof g_status, "builtin: %s",
                         renderer_result_string(result));
            }
            pending_builtin = 0;
            update_console();
        } else if (pending_load) {
            int result;

            result = renderer_set_geometry(g_vertices, pending_vertex_count);
            if (result == TWLHD_RENDER_OK) {
                memset(&g_scene, 0, sizeof g_scene);
                g_scene.mode = "SD FRAME (DEBUG CAMERA)";
                g_scene.source = FRAME_PATH;
                g_scene.have_frame = 1;
                g_scene.parse_result = pending_parse;
                g_scene.frame_index = pending_frame_index;
                g_scene.cmd_count = pending_cmd_count;
                g_scene.input_vertices = pending_stats.input_vertices;
                g_scene.triangles = pending_stats.triangles;
                g_scene.stats = pending_stats;
            } else {
                snprintf(g_status, sizeof g_status, "upload: %s",
                         renderer_result_string(result));
            }
            pending_load = 0;
            update_console();
        }
    }

    renderer_shutdown();
    if (g_fs_ready) fsExit();
    gfxExit();
    return 0;
}
