// SPDX-License-Identifier: Apache-2.0
/*
 * Minimal, hand-written libretro declarations used only when the real
 * libretro.h is not on the include path.
 *
 * The real header stays external and unvendored: build with
 *   make azahar-host LIBRETRO_INCLUDE=/path/to/libretro-common/include
 * and azahar_host.c includes <libretro.h> instead of this file.
 *
 * Everything below is transcribed, minimal, mirrors libretro.h as shipped by
 * Azahar (externals/libretro-common/libretro-common/include/libretro.h,
 * retrieved 2026-11-07); not copied. Only the types, structs and constants
 * this frontend actually uses are declared. If a struct layout or enum value
 * here drifts from the real ABI the behaviour is undefined, so prefer the
 * real header.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RETRO_API_VERSION 1

typedef bool (*retro_environment_t)(unsigned cmd, void *data);
typedef void (*retro_video_refresh_t)(const void *data, unsigned width,
                                      unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (*retro_input_poll_t)(void);
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device,
                                       unsigned index, unsigned id);

enum retro_log_level {
    RETRO_LOG_DEBUG = 0,
    RETRO_LOG_INFO,
    RETRO_LOG_WARN,
    RETRO_LOG_ERROR,
    RETRO_LOG_DUMMY = INT32_MAX
};

typedef void (*retro_log_printf_t)(enum retro_log_level level,
                                   const char *fmt, ...);

struct retro_log_callback {
    retro_log_printf_t log;
};

struct retro_variable {
    const char *key;
    const char *value;
};

struct retro_game_info {
    const char *path;
    const void *data;
    size_t size;
    const char *meta;
};

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

enum retro_pixel_format {
    RETRO_PIXEL_FORMAT_0RGB1555 = 0,
    RETRO_PIXEL_FORMAT_XRGB8888 = 1,
    RETRO_PIXEL_FORMAT_RGB565 = 2,
    RETRO_PIXEL_FORMAT_UNKNOWN = INT32_MAX
};

struct retro_framebuffer {
    void *data;
    unsigned width;
    unsigned height;
    size_t pitch;
    enum retro_pixel_format format;
    unsigned access_flags;
    unsigned memory_flags;
};

#define RETRO_MEMORY_ACCESS_WRITE (1u << 0)

#define RETRO_DEVICE_JOYPAD 1

#define RETRO_DEVICE_ID_JOYPAD_B 0
#define RETRO_DEVICE_ID_JOYPAD_Y 1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START 3
#define RETRO_DEVICE_ID_JOYPAD_UP 4
#define RETRO_DEVICE_ID_JOYPAD_DOWN 5
#define RETRO_DEVICE_ID_JOYPAD_LEFT 6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT 7
#define RETRO_DEVICE_ID_JOYPAD_A 8
#define RETRO_DEVICE_ID_JOYPAD_X 9
#define RETRO_DEVICE_ID_JOYPAD_L 10
#define RETRO_DEVICE_ID_JOYPAD_R 11

#define RETRO_ENVIRONMENT_EXPERIMENTAL 0x10000u

#define RETRO_ENVIRONMENT_SET_MESSAGE 6
#define RETRO_ENVIRONMENT_SHUTDOWN 7
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS 11
#define RETRO_ENVIRONMENT_SET_HW_RENDER 14
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
#define RETRO_ENVIRONMENT_SET_VARIABLES 16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE 17
#define RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK 21
#define RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK 22
#define RETRO_ENVIRONMENT_GET_SENSOR_INTERFACE \
    (25u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO 35
#define RETRO_ENVIRONMENT_SET_MEMORY_MAPS \
    (36u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_GEOMETRY 37
#define RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER \
    (40u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE \
    (41u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE \
    (43u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS 44
#define RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT \
    (44u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_VFS_INTERFACE \
    (45u | RETRO_ENVIRONMENT_EXPERIMENTAL)
#define RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION 52
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS 53
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER 56
#define RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 67
#define RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE \
    (75u | RETRO_ENVIRONMENT_EXPERIMENTAL)
