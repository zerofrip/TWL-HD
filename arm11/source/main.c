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
#include "twlhd_v2_parse.h"
#include "twlhd_gx_interp.h"
#include "twlhd_vertex_convert.h"
#include "twlhd_seq.h"

#define FRAME_PATH "sdmc:/twlhd_frame.bin"
#define MAX_VERTICES (3u * TWLHD_RING_CAPACITY)
#define CLIP_VERTEX_CAPACITY 6144u

typedef struct {
    const char *mode;
    const char *source;
    int have_frame;
    int is_v2;
    TwlhdParseResult parse_result;
    uint32_t frame_index;
    uint32_t cmd_count;
    uint32_t input_vertices;
    uint32_t triangles;
    uint32_t gxcm_count;
    TwlhdConvertStats stats;
    TwlhdGxStats gx_stats;
} SceneInfo;

static uint8_t g_frame_bytes[TWLHD_V2_FILE_MAX];
static TwlhdParsedFrame g_parsed;
static TwlhdVertex g_vertices[MAX_VERTICES];
static TwlhdV2View g_v2view;
static TwlhdGxVertex g_clip_vertices[CLIP_VERTEX_CAPACITY];
_Static_assert(sizeof(TwlhdGxVertex) == 32, "TwlhdGxVertex must be 32 bytes for the clip vertex layout");

static SceneInfo g_scene;
static char g_status[96];
static char g_transport_status[64];
static int g_fs_ready;

static int g_seq_on;
static uint32_t g_seq_counter;
static TwlhdSeqState g_seq_state;
static char g_seq_path[32];
static char g_seq_source[32];

static void set_builtin_scene(void) {
    uint32_t renderer_vertices = renderer_vertex_count();

    memset(&g_scene, 0, sizeof g_scene);
    g_scene.mode = "BUILT-IN TRIANGLE";
    g_scene.source = "built-in";
    g_scene.have_frame = 0;
    g_scene.input_vertices = renderer_vertices;
    g_scene.triangles = renderer_vertices / 3u;
}

static uint32_t clamp_max(uint32_t v) { return v > 999999u ? 999999u : v; }
static uint32_t clamp_4digit(uint32_t v) { return v > 9999u ? 9999u : v; }

static void status_line(const char *s) {
    char line[40]; size_t i;
    for (i = 0; i < sizeof(line) - 1 && s[i] != '\0'; i++) line[i] = s[i];
    line[i] = '\0';
    printf("%s\n", line);
}

static int load_v1(uint32_t *out_vertex_count,
                   TwlhdConvertStats *out_stats,
                   TwlhdParseResult *out_parse,
                   uint32_t *out_frame_index,
                   uint32_t *out_cmd_count) {
    TwlhdParseResult parse_result;
    TwlhdConvertResult convert_result;
    uint32_t capacity;
    uint32_t vertex_count;

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

static int load_sd_frame(const char *path,
                         uint32_t *out_vertex_count,
                         TwlhdConvertStats *out_stats,
                         TwlhdParseResult *out_parse,
                         uint32_t *out_frame_index,
                         uint32_t *out_cmd_count,
                         int *out_is_v2,
                         TwlhdGxViewport *out_viewport,
                         TwlhdGxStats *out_gx_stats,
                         uint32_t *out_gxcm_count,
                         int v2_only) {
    FILE *file;
    long size;
    size_t read_bytes;
    TwlhdFileFormat format;

    if (!g_fs_ready) {
        snprintf(g_status, sizeof g_status, "SD: filesystem init failed");
        return -1;
    }

    file = fopen(path, "rb");
    if (!file) {
        snprintf(g_status, sizeof g_status, "SD: cannot open %s", path);
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        snprintf(g_status, sizeof g_status, "SD: seek failed");
        fclose(file);
        return -1;
    }

    size = ftell(file);
    if (size <= 0 || (uint32_t)size > TWLHD_V2_FILE_MAX) {
        snprintf(g_status, sizeof g_status,
                 "SD: size %ld, max %u", size,
                 (unsigned)TWLHD_V2_FILE_MAX);
        fclose(file);
        return -1;
    }

    rewind(file);
    read_bytes = fread(g_frame_bytes, 1, (size_t)size, file);
    if (read_bytes != (size_t)size) {
        snprintf(g_status, sizeof g_status,
                 "SD: short read (%u of %ld bytes)",
                 (unsigned)read_bytes, size);
        fclose(file);
        return -1;
    }
    fclose(file);

    format = twlhd_detect_format(g_frame_bytes, (size_t)size);
    if (format == TWLHD_FILE_V2) {
        TwlhdV2Result v2_result;
        TwlhdGxResult gx_result;
        uint32_t vertex_count;

        v2_result = twlhd_v2_parse(g_frame_bytes, (size_t)size, &g_v2view);
        if (v2_result != TWLHD_V2_OK) {
            snprintf(g_status, sizeof g_status, "v2 parse: %s",
                     twlhd_v2_result_string(v2_result));
            return -1;
        }

        gx_result = twlhd_gx_interpret(&g_v2view, g_clip_vertices,
                                       CLIP_VERTEX_CAPACITY,
                                       &vertex_count, out_viewport,
                                       out_gx_stats);
        if (gx_result != TWLHD_GX_OK) {
            snprintf(g_status, sizeof g_status,
                     "gx:%s s%lu c%lu i%lu",
                     twlhd_gx_result_string(gx_result),
                     (unsigned long)clamp_4digit(out_gx_stats->stack_errors),
                     (unsigned long)clamp_4digit(out_gx_stats->capacity_dropped),
                     (unsigned long)clamp_4digit(out_gx_stats->dropped_incomplete));
            return -1;
        }

        *out_vertex_count = vertex_count;
        *out_is_v2 = 1;
        *out_gxcm_count = g_v2view.gxcm_count;
        *out_frame_index = g_v2view.frame_index;
        *out_cmd_count = g_v2view.gxcm_count;
        *out_parse = TWLHD_PARSE_OK;

        if (vertex_count == 0) {
            snprintf(g_status, sizeof g_status,
                     "SD: v2 loaded, 0 triangles (valid)");
        } else {
            snprintf(g_status, sizeof g_status,
                     "SD: v2 loaded, %u verts -> %u tris",
                     (unsigned)vertex_count, (unsigned)(vertex_count / 3u));
        }
    } else if (!v2_only && format == TWLHD_FILE_V1) {
        if (load_v1(out_vertex_count, out_stats, out_parse,
                    out_frame_index, out_cmd_count) != 0) {
            return -1;
        }
        *out_is_v2 = 0;
    } else {
        snprintf(g_status, sizeof g_status,
                 "SD: unknown format (size %u)", (unsigned)size);
        return -1;
    }

    return 0;
}

/*
 * Worst-case column counts for direct printf() lines in update_console().
 * The bottom-screen console is 40 columns wide (0-39). status_line() is a
 * separate helper that truncates its argument to 39 chars.
 *
 * | line                  | format                                  | worst-case string                         | cols |
 * |-----------------------|-----------------------------------------|-------------------------------------------|------|
 * | title                 | "TWL-HD Phase 4B/4C renderer\n"         | TWL-HD Phase 4B/4C renderer               | 27   |
 * | status banner         | "HARDWARE UNVERIFIED (build-only)\n\n"   | HARDWARE UNVERIFIED (build-only)          | 32   |
 * | seq status            | "seq %03u/%03u loop %lu\n"               | seq 063/064 loop 9999                     | 21   |
 * | mode                  | "mode:   %s\n"                          | mode:   SD FRAME (DEBUG CAMERA)           | 31   |
 * | mode (builtin)        | "mode:   %s\n"                          | mode:   BUILT-IN TRIANGLE                 | 25   |
 * | mode (failed)         | "mode:   %s\n"                          | mode:   RENDERER FAILED                   | 23   |
 * | source                | "source: %s\n"                          | source: sdmc:/twlhd_frame.bin             | 29   |
 * | camera (v2)           | "camera: DS matrices (v2 GXCM)\n\n"      | camera: DS matrices (v2 GXCM)             | 29   |
 * | camera (v1)           | "camera: DEBUG CAMERA (fixed, not DS)\n\n" | camera: DEBUG CAMERA (fixed, not DS)    | 36   |
 * | parse (v1 have_frame) | "parse:  %s\n"                          | parse:  ERR_PARAM_COUNT                   | 23   |
 * | parse (no frame)      | "parse:  -- (no SD frame)\n"             | parse:  -- (no SD frame)                  | 24   |
 * | frame (v1 have_frame) | "frame:  %lu  cmds: %lu\n"               | frame:  999999  cmds: 999999              | 28   |
 * | frame (v2 have_frame) | "frame:%lu gxcm:%lu tris:%lu\n"          | frame:999999 gxcm:999999 tris:999999      | 36   |
 * | frame (no frame)      | "frame:  --  cmds: --\n"                 | frame:  --  cmds: --                      | 20   |
 * | verts (v1)            | "in: %lu  tris: %lu\n"                   | in: 999999  tris: 999999                  | 24   |
 * | verts (v2)            | "vtx in: %lu\n"                          | vtx in: 999999                            | 14   |
 * | render verts          | "render verts: %lu\n"                    | render verts: 999999                      | 20   |
 * | unsupported (v1)      | "uns mtx:%lu vp:%lu vtx:%lu unk:%lu\n"   | uns mtx:9999 vp:9999 vtx:9999 unk:9999    | 38   |
 * | ignored (v1)          | "ignored: %lu outside-begin\n"            | ignored: 999999 outside-begin             | 29   |
 * | dropped (v1)          | "dropped: %lu incomplete\n\n"             | dropped: 999999 incomplete                | 26   |
 * | gx counters (v2)      | "stk%lu cap%lu inc%lu uns%lu vd%lu\n"    | stk9999 cap9999 inc9999 uns9999 vd9999    | 38   |
 * | key legend            | "A=load SD  Y=builtin  X=seq  START=exit\n" | A=load SD  Y=builtin  X=seq  START=exit  | 39   |
 */
static void update_console(void) {
    consoleClear();
    printf("TWL-HD Phase 4B/4C renderer\n");
    printf("HARDWARE UNVERIFIED (build-only)\n\n");
    if (g_seq_on) {
        printf("seq %03u/%03u loop %lu\n",
               (unsigned)clamp_4digit(g_seq_state.displayed),
               (unsigned)clamp_4digit(g_seq_state.first_pass_count),
               (unsigned long)clamp_4digit(g_seq_state.loop_count));
    }
    printf("mode:   %s\n", g_scene.mode);
    printf("source: %s\n", g_scene.source);
    if (g_scene.is_v2) {
        printf("camera: DS matrices (v2 GXCM)\n\n");
    } else {
        printf("camera: DEBUG CAMERA (fixed, not DS)\n\n");
    }

    if (g_scene.have_frame) {
        if (g_scene.is_v2) {
            printf("frame:%lu gxcm:%lu tris:%lu\n",
                   (unsigned long)clamp_max(g_scene.frame_index),
                   (unsigned long)clamp_max(g_scene.gxcm_count),
                   (unsigned long)clamp_max(g_scene.gx_stats.triangles_out));
        } else {
            printf("parse:  %s\n",
                   twlhd_parse_result_string(g_scene.parse_result));
            printf("frame:  %lu  cmds: %lu\n",
                   (unsigned long)clamp_max(g_scene.frame_index),
                   (unsigned long)clamp_max(g_scene.cmd_count));
        }
    } else {
        printf("parse:  -- (no SD frame)\n");
        printf("frame:  --  cmds: --\n");
    }

    if (g_scene.is_v2) {
        printf("vtx in: %lu\n",
               (unsigned long)clamp_max(g_scene.gx_stats.vertices_in));
    } else {
        printf("in: %lu  tris: %lu\n",
               (unsigned long)clamp_max(g_scene.input_vertices),
               (unsigned long)clamp_max(g_scene.triangles));
    }

    printf("render verts: %lu\n",
           (unsigned long)clamp_max(renderer_vertex_count()));

    if (!g_scene.is_v2) {
        printf("uns mtx:%lu vp:%lu vtx:%lu unk:%lu\n",
               (unsigned long)clamp_4digit(g_scene.stats.unsupported_matrix),
               (unsigned long)clamp_4digit(g_scene.stats.unsupported_viewport),
               (unsigned long)clamp_4digit(g_scene.stats.unsupported_vertex),
               (unsigned long)clamp_4digit(g_scene.stats.unsupported_unknown));
        printf("ignored: %lu outside-begin\n",
               (unsigned long)clamp_max(g_scene.stats.ignored_outside_begin));
        printf("dropped: %lu incomplete\n\n",
               (unsigned long)clamp_max(g_scene.stats.dropped_incomplete));
    } else {
        printf("stk%lu cap%lu inc%lu uns%lu vd%lu\n",
               (unsigned long)clamp_4digit(g_scene.gx_stats.stack_errors),
               (unsigned long)clamp_4digit(g_scene.gx_stats.capacity_dropped),
               (unsigned long)clamp_4digit(g_scene.gx_stats.dropped_incomplete),
               (unsigned long)clamp_4digit(g_scene.gx_stats.unsupported),
               (unsigned long)clamp_4digit(g_scene.gx_stats.geometry_incomplete));
    }

    printf("A=load SD  Y=builtin  X=seq  START=exit\n");
    status_line(g_transport_status);
    status_line(g_status);
}

int main(void) {
    int renderer_status;
    int transport_status;
    int pending_load = 0;
    int pending_builtin = 0;
    int pending_is_v2 = 0;
    uint32_t pending_vertex_count = 0;
    TwlhdConvertStats pending_stats;
    TwlhdParseResult pending_parse = TWLHD_PARSE_OK;
    uint32_t pending_frame_index = 0;
    uint32_t pending_cmd_count = 0;
    uint32_t pending_gxcm_count = 0;
    TwlhdGxViewport pending_viewport;
    TwlhdGxStats pending_gx_stats;
    const char *pending_source = NULL;
    int seq_pending = 0;

    memset(&pending_stats, 0, sizeof pending_stats);
    memset(&pending_viewport, 0, sizeof pending_viewport);
    memset(&pending_gx_stats, 0, sizeof pending_gx_stats);

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
            g_seq_on = 0;
        }

        if (keys & KEY_X) {
            if (g_seq_on) {
                g_seq_on = 0;
                snprintf(g_status, sizeof g_status, "seq off");
            } else {
                FILE *seq_file;

                twlhd_seq_path(g_seq_path, sizeof g_seq_path, 0);
                seq_file = fopen(g_seq_path, "rb");
                if (!seq_file) {
                    snprintf(g_status, sizeof g_status,
                             "seq: no seq_000.bin");
                } else {
                    fclose(seq_file);
                    g_seq_on = 1;
                    g_seq_counter = SEQ_PERIOD - 1;
                    twlhd_seq_reset(&g_seq_state);
                    snprintf(g_status, sizeof g_status, "seq on");
                }
            }
            update_console();
        }

        if (keys & KEY_A) {
            g_seq_on = 0;
            if (load_sd_frame(FRAME_PATH, &pending_vertex_count,
                              &pending_stats, &pending_parse,
                              &pending_frame_index, &pending_cmd_count,
                              &pending_is_v2, &pending_viewport,
                              &pending_gx_stats, &pending_gxcm_count,
                              0) == 0) {
                pending_load = 1;
                pending_builtin = 0;
                pending_source = FRAME_PATH;
            }
            update_console();
        }

        if (g_seq_on) {
            g_seq_counter++;
            if (g_seq_counter >= SEQ_PERIOD) {
                FILE *probe;
                g_seq_counter = 0;

                if (twlhd_seq_path(g_seq_path, sizeof g_seq_path,
                                   g_seq_state.index) != 0) {
                    /* Path format error (shouldn't happen) — treat as missing. */
                    twlhd_seq_advance(&g_seq_state, 0);
                    update_console();
                } else {
                    probe = fopen(g_seq_path, "rb");
                    if (!probe) {
                        /* File absent: wrap to 000, count a loop, seq stays ON. */
                        twlhd_seq_advance(&g_seq_state, 0);
                        snprintf(g_status, sizeof g_status, "seq: wrapped");
                        update_console();
                    } else {
                        fclose(probe);
                        /* File present: format/parse error turns seq OFF. */
                        if (load_sd_frame(g_seq_path, &pending_vertex_count,
                                          &pending_stats, &pending_parse,
                                          &pending_frame_index,
                                          &pending_cmd_count,
                                          &pending_is_v2, &pending_viewport,
                                          &pending_gx_stats,
                                          &pending_gxcm_count, 1) == 0) {
                            pending_load = 1;
                            pending_builtin = 0;
                            memcpy(g_seq_source, g_seq_path,
                                   sizeof g_seq_source);
                            pending_source = g_seq_source;
                            seq_pending = 1;
                            twlhd_seq_advance(&g_seq_state, 1);
                        } else {
                            g_seq_on = 0;
                            update_console();
                        }
                    }
                }
            }
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
            int was_seq = seq_pending;

            seq_pending = 0;
            if (pending_is_v2) {
                result = renderer_set_clip_scene(g_clip_vertices,
                                                 pending_vertex_count,
                                                 &pending_viewport);
            } else {
                result = renderer_set_geometry(g_vertices, pending_vertex_count);
            }

            if (result == TWLHD_RENDER_OK) {
                memset(&g_scene, 0, sizeof g_scene);
                if (pending_is_v2) {
                    g_scene.mode = "SD v2 (DS MATRICES)";
                    g_scene.is_v2 = 1;
                    g_scene.gxcm_count = pending_gxcm_count;
                    g_scene.gx_stats = pending_gx_stats;
                    g_scene.triangles = pending_gx_stats.triangles_out;
                } else {
                    g_scene.mode = "SD FRAME (DEBUG CAMERA)";
                    g_scene.input_vertices = pending_stats.input_vertices;
                    g_scene.triangles = pending_stats.triangles;
                    g_scene.stats = pending_stats;
                }
                g_scene.source = pending_source;
                g_scene.have_frame = 1;
                g_scene.parse_result = pending_parse;
                g_scene.frame_index = pending_frame_index;
                g_scene.cmd_count = pending_cmd_count;
                if (pending_is_v2) {
                    g_scene.input_vertices = pending_vertex_count;
                }
            } else {
                snprintf(g_status, sizeof g_status, "%s: %s",
                         pending_is_v2 ? "clip" : "upload",
                         renderer_result_string(result));
                if (was_seq) {
                    g_seq_on = 0;
                }
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
