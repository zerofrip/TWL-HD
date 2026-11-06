// SPDX-License-Identifier: Apache-2.0
/*
 * Headless libretro host for deterministic screenshots of the ARM11 3dsx demo.
 *
 * The Azahar libretro core is loaded with dlopen() at run time; nothing from
 * Azahar is vendored, linked or compiled into this tool. The core path comes
 * from --core or AZAHAR_LIBRETRO. See tools/azahar_host/README.md.
 *
 * Exit codes: 2 for usage/parse errors, 1 for runtime failures.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__has_include)
#if __has_include(<libretro.h>)
#include <libretro.h>
#define AH_HAVE_REAL_LIBRETRO 1
#endif
#endif
#ifndef AH_HAVE_REAL_LIBRETRO
#include "azahar_host_libretro_min.h"
#endif

#include "azahar_host_util.h"

#define AH_MAX_PRESSES 64
#define AH_MAX_SHOTS 256

struct press_spec {
    int id;
    unsigned from;
    unsigned to;
};

struct core_api {
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    unsigned (*retro_api_version)(void);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(void (*)(int16_t, int16_t));
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_set_controller_port_device)(unsigned, unsigned);
    void (*retro_get_system_info)(struct retro_system_info *);
    void (*retro_get_system_av_info)(struct retro_system_av_info *);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_unload_game)(void);
    void (*retro_run)(void);
};

static struct core_api g_api;

static char g_home_dir[PATH_MAX];
static char g_data_dir[PATH_MAX];
static char g_config_dir[PATH_MAX];
static char g_cache_dir[PATH_MAX];
static char g_save_dir[PATH_MAX];
static char g_system_dir[PATH_MAX];
static int g_system_dir_override;   /* 1 → caller-supplied, skip auto-create */

static unsigned char *g_swfb;
static size_t g_swfb_cap;

static unsigned char *g_frame;
static size_t g_frame_cap;
static unsigned g_frame_width;
static unsigned g_frame_height;
static int g_have_frame;

static unsigned g_current_frame;
static struct press_spec g_presses[AH_MAX_PRESSES];
static size_t g_press_count;

static int g_options_answered;

static void host_log(const char *fmt, ...) {
    char buffer[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[azahar_host] %s\n", buffer);
}

/* Core log callback: prefix every line with the core tag. */
static void core_log(enum retro_log_level level, const char *fmt, ...) {
    char buffer[2048];
    va_list ap;
    const char *p;

    (void)level;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);

    p = buffer;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
        fprintf(stderr, "[azahar_libretro] %.*s\n", (int)len, p);
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
}

static const char *option_value(const char *key) {
    static const struct {
        const char *key;
        const char *value;
    } options[] = {
        /* Software rendering, no JIT, both screens stacked (400x480 at 1x). */
        {"citra_graphics_api", "Software"},
        {"citra_use_cpu_jit", "disabled"},
        {"citra_layout_option", "default"},
        {"citra_swap_screen", "Top"},
        {"citra_use_virtual_sd", "enabled"},
        {"citra_resolution_factor", "1"},
        {"citra_region_value", "Auto"},
        {"citra_language_value", "English"},
        {"citra_use_libretro_save_path", "LibRetro Default"},
        {"citra_use_disk_shader_cache", "disabled"},
    };
    size_t i;

    for (i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        if (strcmp(key, options[i].key) == 0) {
            return options[i].value;
        }
    }
    return NULL;
}

static bool env_callback(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const enum retro_pixel_format *fmt =
            (const enum retro_pixel_format *)data;
        if (fmt != NULL && *fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            host_log("pixel format XRGB8888 accepted");
            return true;
        }
        host_log("pixel format refused (only XRGB8888 is supported)");
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        const char *value;

        if (var == NULL || var->key == NULL) {
            return false;
        }
        value = option_value(var->key);
        var->value = value;
        if (value == NULL) {
            return false;
        }
        g_options_answered++;
        host_log("core option %s=%s", var->key, value);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data != NULL) {
            *(bool *)data = false;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (data == NULL) {
            return false;
        }
        *(char **)data = g_system_dir;
        host_log("system directory: %s", g_system_dir);
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (data == NULL) {
            return false;
        }
        *(char **)data = g_save_dir;
        host_log("save directory: %s", g_save_dir);
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        struct retro_log_callback *cb = (struct retro_log_callback *)data;
        if (cb == NULL) {
            return false;
        }
        cb->log = core_log;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
        struct retro_framebuffer *fb = (struct retro_framebuffer *)data;
        size_t need;

        if (fb == NULL || fb->width == 0 || fb->height == 0) {
            return false;
        }
        need = (size_t)fb->width * (size_t)fb->height * 4u;
        if (need > g_swfb_cap) {
            unsigned char *grown = (unsigned char *)realloc(g_swfb, need);
            if (grown == NULL) {
                return false;
            }
            g_swfb = grown;
            g_swfb_cap = need;
        }
        fb->data = g_swfb;
        fb->pitch = (size_t)fb->width * 4u;
        fb->format = RETRO_PIXEL_FORMAT_XRGB8888;
        fb->access_flags = RETRO_MEMORY_ACCESS_WRITE;
        fb->memory_flags = 0;
        host_log("software framebuffer %ux%u", fb->width, fb->height);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        const struct retro_system_av_info *info =
            (const struct retro_system_av_info *)data;
        if (info != NULL) {
            host_log("geometry %ux%u", info->geometry.base_width,
                     info->geometry.base_height);
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        host_log("core option definitions accepted");
        return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SHUTDOWN:
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER:
        host_log("hardware renderer refused (software only)");
        return false;
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE:
    case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
    case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK:
    case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK:
        return false;
    default:
        return false;
    }
}

static void video_refresh_callback(const void *data, unsigned width,
                                   unsigned height, size_t pitch) {
    size_t stride;
    unsigned y;

    if (data == NULL || width == 0 || height == 0) {
        return;
    }
    if (pitch == 0) {
        pitch = (size_t)width * 4u;
    }
    stride = pitch;
    if (stride < (size_t)width * 4u) {
        host_log("video frame with a pitch smaller than one row (%zu)", stride);
        return;
    }

    if ((size_t)width * (size_t)height * 4u > g_frame_cap) {
        unsigned char *grown = (unsigned char *)realloc(
            g_frame, (size_t)width * (size_t)height * 4u);
        if (grown == NULL) {
            return;
        }
        g_frame = grown;
        g_frame_cap = (size_t)width * (size_t)height * 4u;
    }
    g_frame_width = width;
    g_frame_height = height;

    for (y = 0; y < height; y++) {
        memcpy(g_frame + (size_t)y * width * 4u,
               (const unsigned char *)data + (size_t)y * stride,
               (size_t)width * 4u);
    }
    g_have_frame = 1;
}

static void audio_sample_callback(int16_t left, int16_t right) {
    (void)left;
    (void)right;
}

static size_t audio_sample_batch_callback(const int16_t *data, size_t frames) {
    (void)data;
    return frames;
}

static void input_poll_callback(void) {}

static int16_t input_state_callback(unsigned port, unsigned device,
                                    unsigned index, unsigned id) {
    size_t i;

    if (port != 0 || device != RETRO_DEVICE_JOYPAD || index != 0) {
        return 0;
    }
    for (i = 0; i < g_press_count; i++) {
        if (g_presses[i].id == (int)id && g_current_frame >= g_presses[i].from &&
            g_current_frame <= g_presses[i].to) {
            return 1;
        }
    }
    return 0;
}

static int write_ppm(const char *path, unsigned width, unsigned height) {
    unsigned char *rgb;
    size_t pixels = (size_t)width * (size_t)height;
    FILE *file;

    rgb = (unsigned char *)malloc(pixels * 3u);
    if (rgb == NULL) {
        return -1;
    }
    if (xrgb8888_to_rgb24(g_frame, rgb, width, height,
                          (long)width * 4) != 0) {
        free(rgb);
        return -1;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        free(rgb);
        return -1;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0 ||
        fwrite(rgb, 1, pixels * 3u, file) != pixels * 3u) {
        fclose(file);
        free(rgb);
        return -1;
    }
    if (fclose(file) != 0) {
        free(rgb);
        return -1;
    }
    free(rgb);
    return 0;
}

static int make_dir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in;
    FILE *out;
    char buffer[65536];
    size_t got;
    int rc = 0;

    in = fopen(src, "rb");
    if (in == NULL) {
        return -1;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((got = fread(buffer, 1, sizeof buffer, in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            rc = -1;
            break;
        }
    }
    if (ferror(in)) {
        rc = -1;
    }
    if (fclose(in) != 0) {
        rc = -1;
    }
    if (fclose(out) != 0) {
        rc = -1;
    }
    return rc;
}

static int copy_tree(const char *src, const char *dst) {
    struct stat st;
    DIR *dir;
    struct dirent *entry;

    if (stat(src, &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (make_dir(dst) != 0) {
            return -1;
        }
        dir = opendir(src);
        if (dir == NULL) {
            return -1;
        }
        while ((entry = readdir(dir)) != NULL) {
            char child_src[PATH_MAX];
            char child_dst[PATH_MAX];

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (snprintf(child_src, sizeof child_src, "%s/%s", src,
                         entry->d_name) >= (int)sizeof child_src ||
                snprintf(child_dst, sizeof child_dst, "%s/%s", dst,
                         entry->d_name) >= (int)sizeof child_dst) {
                closedir(dir);
                return -1;
            }
            if (copy_tree(child_src, child_dst) != 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        return 0;
    }
    if (S_ISREG(st.st_mode)) {
        return copy_file(src, dst);
    }
    return 0; /* sockets, fifos, symlinks: not part of an SD image */
}

static int setup_environment(const char *sd_root, const char *sys_dir) {
    char root[] = "/tmp/azahar-host-XXXXXX";
    char sd_dir[PATH_MAX + 32];

    if (mkdtemp(root) == NULL) {
        host_log("mkdtemp failed: %s", strerror(errno));
        return -1;
    }
    snprintf(g_home_dir, sizeof g_home_dir, "%s/home", root);
    snprintf(g_data_dir, sizeof g_data_dir, "%s/data", root);
    snprintf(g_config_dir, sizeof g_config_dir, "%s/config", root);
    snprintf(g_cache_dir, sizeof g_cache_dir, "%s/cache", root);
    snprintf(g_save_dir, sizeof g_save_dir, "%s/save", root);
    snprintf(g_system_dir, sizeof g_system_dir, "%s/system", root);
    if (sys_dir != NULL) {
        /* caller-supplied system directory; use as-is, do not auto-create */
        snprintf(g_system_dir, sizeof g_system_dir, "%s", sys_dir);
        g_system_dir_override = 1;
    }

    if (make_dir(g_home_dir) != 0 || make_dir(g_data_dir) != 0 ||
        make_dir(g_config_dir) != 0 || make_dir(g_cache_dir) != 0 ||
        make_dir(g_save_dir) != 0 ||
        (!g_system_dir_override && make_dir(g_system_dir) != 0)) {
        host_log("could not create the temporary environment under %s", root);
        return -1;
    }

    /* Must happen before dlopen(): the core reads these at user-path init. */
    if (setenv("HOME", g_home_dir, 1) != 0 ||
        setenv("XDG_DATA_HOME", g_data_dir, 1) != 0 ||
        setenv("XDG_CONFIG_HOME", g_config_dir, 1) != 0 ||
        setenv("XDG_CACHE_HOME", g_cache_dir, 1) != 0) {
        host_log("setenv failed: %s", strerror(errno));
        return -1;
    }
    host_log("isolated environment: %s", root);
    host_log("save dir %s; system dir %s", g_save_dir, g_system_dir);

    if (sd_root != NULL) {
        /* Azahar maps sdmc: to <user dir>/sdmc, and the user dir to
         * <save dir>/Azahar (see core_settings.cpp ParseStorageOptions). */
        snprintf(sd_dir, sizeof sd_dir, "%s/Azahar/sdmc", g_save_dir);
        {
            char azahar_dir[PATH_MAX + 32];
            snprintf(azahar_dir, sizeof azahar_dir, "%s/Azahar", g_save_dir);
            if (make_dir(azahar_dir) != 0) {
                host_log("could not create %s", azahar_dir);
                return -1;
            }
        }
        if (copy_tree(sd_root, sd_dir) != 0) {
            host_log("could not stage --sd-root %s into %s: %s", sd_root,
                     sd_dir, strerror(errno));
            return -1;
        }
        host_log("staged --sd-root %s as sdmc:/ (%s)", sd_root, sd_dir);
    }
    return 0;
}

static int load_core(const char *path) {
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (handle == NULL) {
        host_log("dlopen(%s) failed: %s", path, dlerror());
        return -1;
    }
#define AH_SYM(field, name)                                                   \
    do {                                                                      \
        *(void **)(&g_api.field) = dlsym(handle, name);                       \
        if (g_api.field == NULL) {                                            \
            host_log("core is missing %s: %s", name, dlerror());              \
            return -1;                                                        \
        }                                                                     \
    } while (0)

    AH_SYM(retro_init, "retro_init");
    AH_SYM(retro_deinit, "retro_deinit");
    AH_SYM(retro_api_version, "retro_api_version");
    AH_SYM(retro_set_environment, "retro_set_environment");
    AH_SYM(retro_set_video_refresh, "retro_set_video_refresh");
    AH_SYM(retro_set_audio_sample, "retro_set_audio_sample");
    AH_SYM(retro_set_audio_sample_batch, "retro_set_audio_sample_batch");
    AH_SYM(retro_set_input_poll, "retro_set_input_poll");
    AH_SYM(retro_set_input_state, "retro_set_input_state");
    AH_SYM(retro_get_system_info, "retro_get_system_info");
    AH_SYM(retro_get_system_av_info, "retro_get_system_av_info");
    AH_SYM(retro_load_game, "retro_load_game");
    AH_SYM(retro_unload_game, "retro_unload_game");
    AH_SYM(retro_run, "retro_run");
#undef AH_SYM
    *(void **)(&g_api.retro_set_controller_port_device) =
        dlsym(handle, "retro_set_controller_port_device");
    return 0;
}

static int parse_u32_arg(const char *text, unsigned *out) {
    const char *p = text;
    unsigned value;

    if (ah_parse_u32(&p, &value) != 0 || *p != '\0') {
        return -1;
    }
    *out = value;
    return 0;
}

static void usage(FILE *stream) {
    fprintf(stream,
            "usage: azahar_host --game <file> --frames <n> [options]\n"
            "  --core <so>       Azahar libretro core (or AZAHAR_LIBRETRO)\n"
            "  --game <file>     .3dsx/.3ds/... to load (required)\n"
            "  --frames <n>      frames to run, numbered from 0 (required)\n"
            "  --press NAME:a-b  hold a button on frames a..b inclusive\n"
            "                    (A,B,X,Y,L,R,START,SELECT,UP,DOWN,LEFT,RIGHT)\n"
            "  --shot <list>     comma-separated frames to save as PPM\n"
            "  --out <prefix>    PPM prefix (default: azahar_shot)\n"
            "  --sd-root <dir>   copy a directory tree to sdmc:/ in the temp root\n"
            "  --sys-dir <dir>   use <dir> as the libretro system directory\n"
            "                    (3DS NAND/BannerData; default: auto temp dir)\n");
}

int main(int argc, char **argv) {
    const char *core_path = getenv("AZAHAR_LIBRETRO");
    const char *game_path = NULL;
    const char *out_prefix = "azahar_shot";
    const char *sd_root = NULL;
    const char *sys_dir = NULL;
    unsigned frames = 0;
    int have_frames = 0;
    unsigned shots[AH_MAX_SHOTS];
    size_t shot_count = 0;
    struct retro_system_info sys_info;
    struct retro_system_av_info av_info;
    struct retro_game_info game_info;
    int loaded = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--core") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            core_path = argv[i];
        } else if (strcmp(arg, "--game") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            game_path = argv[i];
        } else if (strcmp(arg, "--frames") == 0) {
            if (++i >= argc || parse_u32_arg(argv[i], &frames) != 0) {
                usage(stderr);
                return 2;
            }
            have_frames = 1;
        } else if (strcmp(arg, "--press") == 0) {
            int id;
            unsigned from;
            unsigned to;

            if (++i >= argc || g_press_count >= AH_MAX_PRESSES ||
                parse_button_spec(argv[i], &id, &from, &to) != 0) {
                fprintf(stderr, "azahar_host: bad --press value\n");
                usage(stderr);
                return 2;
            }
            g_presses[g_press_count].id = id;
            g_presses[g_press_count].from = from;
            g_presses[g_press_count].to = to;
            g_press_count++;
        } else if (strcmp(arg, "--shot") == 0) {
            int count;

            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            count = parse_frame_list(argv[i], shots, AH_MAX_SHOTS);
            if (count < 0) {
                fprintf(stderr, "azahar_host: bad --shot value\n");
                usage(stderr);
                return 2;
            }
            shot_count = (size_t)count;
        } else if (strcmp(arg, "--out") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            out_prefix = argv[i];
        } else if (strcmp(arg, "--sd-root") == 0) {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            sd_root = argv[i];
        } else if (strcmp(arg, "--sys-dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "azahar_host: --sys-dir needs an argument\n");
                return 2;
            }
            sys_dir = argv[i];
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "azahar_host: unknown argument %s\n", arg);
            usage(stderr);
            return 2;
        }
    }

    if (core_path == NULL || core_path[0] == '\0') {
        fprintf(stderr, "azahar_host: no core given (use --core or AZAHAR_LIBRETRO)\n");
        usage(stderr);
        return 2;
    }
    if (game_path == NULL || !have_frames || frames == 0) {
        usage(stderr);
        return 2;
    }
    for (i = 0; i < (int)shot_count; i++) {
        if (shots[i] >= frames) {
            fprintf(stderr,
                    "azahar_host: --shot frame %u is outside 0..%u\n",
                    shots[i], frames - 1u);
            return 2;
        }
    }
    {
        FILE *probe = fopen(core_path, "rb");
        if (probe == NULL) {
            host_log("cannot read core %s: %s", core_path, strerror(errno));
            return 1;
        }
        fclose(probe);
    }
    {
        FILE *probe = fopen(game_path, "rb");
        if (probe == NULL) {
            host_log("cannot read game %s: %s", game_path, strerror(errno));
            return 1;
        }
        fclose(probe);
    }

    if (setup_environment(sd_root, sys_dir) != 0) {
        return 1;
    }
    if (load_core(core_path) != 0) {
        return 1;
    }

    g_api.retro_set_environment(env_callback);
    g_api.retro_set_video_refresh(video_refresh_callback);
    g_api.retro_set_audio_sample(audio_sample_callback);
    g_api.retro_set_audio_sample_batch(audio_sample_batch_callback);
    g_api.retro_set_input_poll(input_poll_callback);
    g_api.retro_set_input_state(input_state_callback);
    if (g_api.retro_set_controller_port_device != NULL) {
        g_api.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    }

    host_log("core API version %u", g_api.retro_api_version());
    g_api.retro_init();

    memset(&sys_info, 0, sizeof sys_info);
    g_api.retro_get_system_info(&sys_info);
    host_log("core %s %s", sys_info.library_name ? sys_info.library_name : "?",
             sys_info.library_version ? sys_info.library_version : "?");

    memset(&game_info, 0, sizeof game_info);
    game_info.path = game_path;
    if (!g_api.retro_load_game(&game_info)) {
        host_log("retro_load_game failed for %s", game_path);
        g_api.retro_deinit();
        return 1;
    }
    loaded = 1;
    host_log("core option values accepted (%d lookups answered)", g_options_answered);

    memset(&av_info, 0, sizeof av_info);
    g_api.retro_get_system_av_info(&av_info);
    host_log("av_info geometry %ux%u @ %.3f fps", av_info.geometry.base_width,
             av_info.geometry.base_height, av_info.timing.fps);

    for (g_current_frame = 0; g_current_frame < frames; g_current_frame++) {
        int save_this = 0;
        size_t s;

        input_poll_callback();
        g_api.retro_run();

        if (g_current_frame == 0 && !g_have_frame) {
            host_log("no frame produced by the first retro_run()");
            g_api.retro_unload_game();
            g_api.retro_deinit();
            return 1;
        }

        for (s = 0; s < shot_count; s++) {
            if (shots[s] == g_current_frame) {
                save_this = 1;
                break;
            }
        }
        if (save_this || g_current_frame + 1u == frames) {
            char path[PATH_MAX];

            if (save_this) {
                snprintf(path, sizeof path, "%s_%u.ppm", out_prefix,
                         g_current_frame);
                if (write_ppm(path, g_frame_width, g_frame_height) != 0) {
                    host_log("could not write %s: %s", path, strerror(errno));
                    g_api.retro_unload_game();
                    g_api.retro_deinit();
                    return 1;
                }
                host_log("wrote %s (%ux%u)", path, g_frame_width,
                         g_frame_height);
            }
            if (g_current_frame + 1u == frames) {
                snprintf(path, sizeof path, "%s_last.ppm", out_prefix);
                if (write_ppm(path, g_frame_width, g_frame_height) != 0) {
                    host_log("could not write %s: %s", path, strerror(errno));
                    g_api.retro_unload_game();
                    g_api.retro_deinit();
                    return 1;
                }
                host_log("wrote %s (%ux%u)", path, g_frame_width,
                         g_frame_height);
            }
        }
    }

    if (loaded) {
        g_api.retro_unload_game();
    }
    g_api.retro_deinit();
    free(g_frame);
    free(g_swfb);
    return 0;
}
