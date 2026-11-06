// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for arm11/include/twlhd_blend_config.h.
 *
 * The real libctru <3ds/gpu/enums.h> compiles on the host, so this test asserts
 * every field by the actual enum values. It also structurally checks
 * arm11/source/renderer.c by extracting each function body (from the start of
 * its header line to the first subsequent line that opens with '}' at column 0,
 * located by the two-byte sequence "\n}") and asserting literal per-function
 * call-site counts. This is a text-extraction proxy, not a behavioural proof:
 * exact blend behaviour is only provably correct via emulator/hardware evidence.
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

/* Extract the body of a function from renderer.c text. The body starts at the
 * beginning of the header line identified by `header` and ends just before the
 * first "\n}" sequence after that header (the function's own closing brace at
 * column 0). Returns `out` on success, or NULL after failing a CHECK when the
 * header or closing brace is missing. */
static const char *extract_function(const char *text, const char *header,
                                    const char *name, char *out,
                                    size_t out_size) {
    const char *start = strstr(text, header);
    const char *end;
    size_t len;

    if (start == NULL) {
        fprintf(stderr, "test_blend_config: missing header for %s\n", name);
        CHECK(0);
        return NULL;
    }

    end = strstr(start, "\n}");
    if (end == NULL) {
        fprintf(stderr, "test_blend_config: missing closing brace for %s\n",
                name);
        CHECK(0);
        return NULL;
    }

    len = (size_t)(end - start);
    if (len >= out_size) {
        fprintf(stderr, "test_blend_config: body of %s exceeds buffer\n", name);
        CHECK(0);
        return NULL;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return out;
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
    CHECK(n < sizeof(buf) - 1); /* fail loud if renderer.c outgrows the buffer */
    buf[n] = '\0';
    fclose(f);

    CHECK_EQ_INT(count_substr(buf, "C3D_AlphaBlend("), 2);
    CHECK_EQ_INT(count_substr(buf, "C3D_AlphaTest("), 2);
    CHECK_EQ_INT(count_substr(buf, "twlhd_blend_config_alpha("), 0);
    CHECK_EQ_INT(count_substr(buf, "twlhd_blend_config_opaque("), 1);
    CHECK_EQ_INT(count_substr(buf, "twlhd_alpha_test_disabled("), 2);

    {
        static char clip_body[65536];
        static char debug_body[65536];
        const char *clip = extract_function(
            buf, "static void draw_clip_internal(", "draw_clip_internal",
            clip_body, sizeof(clip_body));
        const char *debug = extract_function(
            buf, "static void draw_debug_internal(", "draw_debug_internal",
            debug_body, sizeof(debug_body));

        if (clip != NULL) {
            CHECK_EQ_INT(count_substr(clip, "twlhd_blend_config_alpha("), 0);
            CHECK_EQ_INT(count_substr(clip, "twlhd_blend_config_opaque("), 0);
            CHECK_EQ_INT(count_substr(clip, "C3D_AlphaBlend("), 1);
            CHECK_EQ_INT(count_substr(clip, "C3D_AlphaTest("), 1);
        }

        if (debug != NULL) {
            CHECK_EQ_INT(count_substr(debug, "twlhd_blend_config_opaque("), 1);
            CHECK_EQ_INT(count_substr(debug, "twlhd_blend_config_alpha("), 0);
            CHECK_EQ_INT(count_substr(debug, "C3D_AlphaBlend("), 1);
            CHECK_EQ_INT(count_substr(debug, "C3D_AlphaTest("), 1);
        }
    }

    test_report("blend-config/renderer-call-sites");
}

int main(void) {
    test_alpha_blend_config();
    test_opaque_blend_config();
    test_alpha_test_disabled();
    test_renderer_call_sites();
    return test_exit_code();
}
