// SPDX-License-Identifier: Apache-2.0
/*
 * Header-only, pure-data blend/alpha-test configurations for the clip-space
 * render path and the DEBUG render path.
 *
 * HARDWARE UNVERIFIED: blend equations/factors are the standard PICA
 * "modulative/translucent" choice, not a console measurement. See
 * docs/HARDWARE_TEST_PROCEDURE_3DS.md section 12 Run 14 (lines 566-622):
 * the renderer never set blend state; on the console alpha 16/31 and 4/31
 * quads drew OPAQUE and the alpha-0 quad was not drawn. Run 15 approval is
 * still required to confirm this explicit blend state matches hardware.
 */
#pragma once

#include <3ds/gpu/enums.h>
#include <stdbool.h>

typedef struct {
    GPU_BLENDEQUATION colorEq;
    GPU_BLENDEQUATION alphaEq;
    GPU_BLENDFACTOR srcColor;
    GPU_BLENDFACTOR dstColor;
    GPU_BLENDFACTOR srcAlpha;
    GPU_BLENDFACTOR dstAlpha;
} TwlhdBlendConfig;

static inline TwlhdBlendConfig twlhd_blend_config_alpha(void) {
    TwlhdBlendConfig cfg = {
        GPU_BLEND_ADD, GPU_BLEND_ADD,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
    };
    return cfg;
}

static inline TwlhdBlendConfig twlhd_blend_config_opaque(void) {
    TwlhdBlendConfig cfg = {
        GPU_BLEND_ADD, GPU_BLEND_ADD,
        GPU_ONE, GPU_ZERO,
        GPU_ONE, GPU_ZERO,
    };
    return cfg;
}

typedef struct {
    bool enable;
    GPU_TESTFUNC func;
    int ref;
} TwlhdAlphaTestConfig;

static inline TwlhdAlphaTestConfig twlhd_alpha_test_disabled(void) {
    TwlhdAlphaTestConfig cfg = {false, GPU_ALWAYS, 0};
    return cfg;
}

/* Alpha test that keeps alpha==0 fragments invisible on the opaque path while
 * passing every alpha>0 fragment unchanged. PICA passes a fragment when
 * alpha > ref, so ref 0 with GPU_GREATER discards exactly alpha==0. */
static inline TwlhdAlphaTestConfig twlhd_alpha_test_gt_zero(void) {
    TwlhdAlphaTestConfig cfg = {true, GPU_GREATER, 0};
    return cfg;
}
