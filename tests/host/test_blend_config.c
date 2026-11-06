// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for arm11/include/twlhd_blend_config.h.
 *
 * The real libctru <3ds/gpu/enums.h> compiles on the host, so this test asserts
 * every field by the actual enum values. It also structurally checks
 * arm11/source/renderer.c for the expected call-site counts as a proxy for
 * the real call sites (the exact placement is only provably correct via
 * emulator/hardware evidence).
 */

#include "twlhd_blend_config.h"

#include <stdio.h>
#include <string.h>

#include "tests_host.h"

static int count_substr(const char *text, const char *needle) {
    int count = 0;
    size_t n = strlen(needle);
    const char *p = text;

    if (n == 0) return 0;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += n;
    }
    return count;
}

static void test_alpha_blend_config(void) {
    TwlhdBlendConfig cfg = twlhd_blend_config_alpha();

    CHECK_EQ_INT(cfg.colorEq, GPU_BLEND_ADD);
    CHECK_EQ_INT(cfg.alphaEq, GPU_BLEND_ADD);
    CHECK_EQ_INT(cfg.srcColor, GPU_SRC_ALPHA);
    CHECK_EQ_INT(cfg.dstColor, GPU_ONE_MINUS_SRC_ALPHA);
    CHECK_EQ_INT(cfg.srcAlpha, GPU_SRC_ALPHA);
    CHECK_EQ_INT(cfg.dstAlpha, GPU_ONE_MINUS_SRC_ALPHA);

    /* Catch srcAlpha/dstAlpha swap mutants: the two alpha slots must differ. */
    CHECK(cfg.srcAlpha != cfg.dstAlpha);
    test_report("blend-config/alpha");
}

static void test_opaque_blend_config(void) {
    TwlhdBlendConfig cfg = twlhd_blend_config_opaque();

    CHECK_EQ_INT(cfg.colorEq, GPU_BLEND_ADD);
    CHECK_EQ_INT(cfg.alphaEq, GPU_BLEND_ADD);
    CHECK_EQ_INT(cfg.srcColor, GPU_ONE);
    CHECK_EQ_INT(cfg.dstColor, GPU_ZERO);
    CHECK_EQ_INT(cfg.srcAlpha, GPU_ONE);
    CHECK_EQ_INT(cfg.dstAlpha, GPU_ZERO);
    test_report("blend-config/opaque");
}

static void test_alpha_test_disabled(void) {
    TwlhdAlphaTestConfig cfg = twlhd_alpha_test_disabled();

    CHECK_EQ_INT(cfg.enable, 0);
    CHECK_EQ_INT(cfg.func, GPU_ALWAYS);
    CHECK_EQ_INT(cfg.ref, 0);
    test_report("blend-config/alpha-test-disabled");
}

static void test_renderer_call_sites(void) {
    static char buf[65536];
    FILE *f = NULL;
    const char *paths[] = {
        "arm11/source/renderer.c",
        "../arm11/source/renderer.c",
        "../../arm11/source/renderer.c",
    };
    size_t i;
    size_t n;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        f = fopen(paths[i], "rb");
        if (f) break;
    }
    CHECK(f != NULL);
    if (!f) {
        test_report("blend-config/renderer-call-sites");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    CHECK_EQ_INT(count_substr(buf, "C3D_AlphaBlend("), 2);
    CHECK_EQ_INT(count_substr(buf, "C3D_AlphaTest("), 2);
    CHECK_EQ_INT(count_substr(buf, "twlhd_blend_config_alpha("), 1);
    CHECK_EQ_INT(count_substr(buf, "twlhd_blend_config_opaque("), 1);
    CHECK_EQ_INT(count_substr(buf, "twlhd_alpha_test_disabled("), 2);
    test_report("blend-config/renderer-call-sites");
}

int main(void) {
    test_alpha_blend_config();
    test_opaque_blend_config();
    test_alpha_test_disabled();
    test_renderer_call_sites();
    return test_exit_code();
}
