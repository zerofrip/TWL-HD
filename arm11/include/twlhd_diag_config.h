// SPDX-License-Identifier: Apache-2.0
/*
 * Header-only, pure-data diagnostic TexEnv/blend configurations for the
 * clip-space render path. Three modes that separate "fragment alpha differs
 * from vertex alpha" from "blend registers are ineffective".
 *
 * HARDWARE UNVERIFIED (build-verified only); this build's purpose is
 * real-hardware bisection. Do not deploy.
 */
#pragma once

#include "twlhd_blend_config.h"

#include <3ds/gpu/enums.h>
#include <3ds/types.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned mode;                 /* 0/1/2 */
    TwlhdBlendConfig blend;        /* via twlhd_blend_config_alpha()/opaque() */
    GPU_TEVOP_RGB rgb_op[3];       /* operands for C3D_TexEnvOpRgb (slot 1..3) */
    GPU_TEVSRC rgb_src;            /* source for all three RGB slots */
    GPU_TEVSRC alpha_src;          /* source for the Alpha slot */
    GPU_TEVOP_A alpha_op;          /* operand for C3D_TexEnvOpAlpha (slot 1) */
    u32 constant_color;            /* RGBA8: R in LSB, alpha in bits 24-31 */
    const char *name;
} TwlhdDiagConfig;

#define TWLHD_DIAG_MODE_COUNT 3

/* Return the static configuration for `mode`. Invalid modes are clamped to 0. */
static inline const TwlhdDiagConfig *twlhd_diag_config(unsigned mode) {
    static const TwlhdDiagConfig modes[TWLHD_DIAG_MODE_COUNT] = {
        /* Mode 0 "normal": explicit alpha blend, TexEnv passes primary vertex
         * colour/alpha through unchanged. Function is GPU_REPLACE for both
         * channels, so the RGB operands and the alpha source/operand are the
         * identity/default values; the constant is reset to a known value so
         * a mode-1/2 constant cannot leak into mode 0. */
        [0] = {
            .mode = 0,
            .blend = {
                .colorEq = GPU_BLEND_ADD,
                .alphaEq = GPU_BLEND_ADD,
                .srcColor = GPU_SRC_ALPHA,
                .dstColor = GPU_ONE_MINUS_SRC_ALPHA,
                .srcAlpha = GPU_SRC_ALPHA,
                .dstAlpha = GPU_ONE_MINUS_SRC_ALPHA,
            },
            .rgb_op = {
                GPU_TEVOP_RGB_SRC_COLOR,
                GPU_TEVOP_RGB_SRC_COLOR,
                GPU_TEVOP_RGB_SRC_COLOR,
            },
            .rgb_src = GPU_PRIMARY_COLOR,
            .alpha_src = GPU_PRIMARY_COLOR,
            .alpha_op = GPU_TEVOP_A_SRC_ALPHA,
            .constant_color = 0xFF000000, /* alpha byte 0xFF; RGB channels 0 */
            .name = "normal",
        },
        /* Mode 1 "alpha-as-gray": blending OFF (opaque), RGB replaces with
         * source alpha (grey = raw fragment alpha), Alpha channel sources the
         * constant and outputs 1.0 so the grey value is fully visible. */
        [1] = {
            .mode = 1,
            .blend = {
                .colorEq = GPU_BLEND_ADD,
                .alphaEq = GPU_BLEND_ADD,
                .srcColor = GPU_ONE,
                .dstColor = GPU_ZERO,
                .srcAlpha = GPU_ONE,
                .dstAlpha = GPU_ZERO,
            },
            .rgb_op = {
                GPU_TEVOP_RGB_SRC_ALPHA,
                GPU_TEVOP_RGB_SRC_ALPHA,
                GPU_TEVOP_RGB_SRC_ALPHA,
            },
            .rgb_src = GPU_PRIMARY_COLOR,
            .alpha_src = GPU_CONSTANT,
            .alpha_op = GPU_TEVOP_A_SRC_ALPHA,
            .constant_color = 0xFF000000, /* alpha byte 0xFF -> output alpha 1 */
            .name = "alpha-as-gray",
        },
        /* Mode 2 "const-alpha-0.5": explicit alpha blend, RGB unchanged,
         * Alpha channel sources the constant and outputs 0.5 (alpha byte 0x80
         * in bits 24-31). If the blend registers are effective the fragment
         * blends at 50%; if they are ineffective it will still draw opaque. */
        [2] = {
            .mode = 2,
            .blend = {
                .colorEq = GPU_BLEND_ADD,
                .alphaEq = GPU_BLEND_ADD,
                .srcColor = GPU_SRC_ALPHA,
                .dstColor = GPU_ONE_MINUS_SRC_ALPHA,
                .srcAlpha = GPU_SRC_ALPHA,
                .dstAlpha = GPU_ONE_MINUS_SRC_ALPHA,
            },
            .rgb_op = {
                GPU_TEVOP_RGB_SRC_COLOR,
                GPU_TEVOP_RGB_SRC_COLOR,
                GPU_TEVOP_RGB_SRC_COLOR,
            },
            .rgb_src = GPU_PRIMARY_COLOR,
            .alpha_src = GPU_CONSTANT,
            .alpha_op = GPU_TEVOP_A_SRC_ALPHA,
            .constant_color = 0x80000000, /* alpha byte 0x80 -> output alpha 0.5 */
            .name = "const-alpha-0.5",
        },
    };

    if (mode >= TWLHD_DIAG_MODE_COUNT) mode = 0;
    return &modes[mode];
}

/* Cycle 0 -> 1 -> 2 -> 0; any invalid value returns 0. */
static inline unsigned twlhd_diag_next(unsigned mode) {
    if (mode == 0) return 1;
    if (mode == 1) return 2;
    return 0; /* mode 2 or invalid */
}
