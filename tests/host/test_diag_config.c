// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for arm11/include/twlhd_diag_config.h.
 *
 * The real libctru <3ds/gpu/enums.h> compiles on the host, so this test asserts
 * every mode field by the actual enum values. It also structurally checks
 * arm11/source/main.c and arm11/source/renderer.c for the expected call-site
 * counts and literal strings.
 */

#include "twlhd_diag_config.h"

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

/* Extract the body of a function from C source text. The body starts at the
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
        fprintf(stderr, "test_diag_config: missing header for %s\n", name);
        CHECK(0);
        return NULL;
    }

    end = strstr(start, "\n}");
    if (end == NULL) {
        fprintf(stderr, "test_diag_config: missing closing brace for %s\n",
                name);
        CHECK(0);
        return NULL;
    }

    len = (size_t)(end - start);
    if (len >= out_size) {
        fprintf(stderr, "test_diag_config: body of %s exceeds buffer\n", name);
        CHECK(0);
        return NULL;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int blend_config_equal(const TwlhdBlendConfig *a,
                              const TwlhdBlendConfig *b) {
    return a->colorEq == b->colorEq && a->alphaEq == b->alphaEq &&
           a->srcColor == b->srcColor && a->dstColor == b->dstColor &&
           a->srcAlpha == b->srcAlpha && a->dstAlpha == b->dstAlpha;
}

static void check_blend_config(const TwlhdBlendConfig *actual,
                               const TwlhdBlendConfig *expected,
                               const char *label) {
    CHECK_EQ_INT(actual->colorEq, expected->colorEq);
    CHECK_EQ_INT(actual->alphaEq, expected->alphaEq);
    CHECK_EQ_INT(actual->srcColor, expected->srcColor);
    CHECK_EQ_INT(actual->dstColor, expected->dstColor);
    CHECK_EQ_INT(actual->srcAlpha, expected->srcAlpha);
    CHECK_EQ_INT(actual->dstAlpha, expected->dstAlpha);
    (void)label;
}

static void test_mode_0_normal(void) {
    const TwlhdDiagConfig *cfg = twlhd_diag_config(0);
    TwlhdBlendConfig alpha = twlhd_blend_config_alpha();

    CHECK_EQ_INT(cfg->mode, 0);
    check_blend_config(&cfg->blend, &alpha, "mode-0 blend");
    CHECK_EQ_INT(cfg->rgb_op[0], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_op[1], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_op[2], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_src, GPU_PRIMARY_COLOR);
    CHECK_EQ_INT(cfg->alpha_src, GPU_PRIMARY_COLOR);
    CHECK_EQ_INT(cfg->alpha_op, GPU_TEVOP_A_SRC_ALPHA);
    CHECK_EQ_INT(cfg->constant_color, 0xFF000000);
    CHECK(strcmp(cfg->name, "normal") == 0);
    test_report("diag-config/mode-0-normal");
}

static void test_mode_1_alpha_as_gray(void) {
    const TwlhdDiagConfig *cfg = twlhd_diag_config(1);
    TwlhdBlendConfig opaque = twlhd_blend_config_opaque();

    CHECK_EQ_INT(cfg->mode, 1);
    check_blend_config(&cfg->blend, &opaque, "mode-1 blend");
    CHECK_EQ_INT(cfg->rgb_op[0], GPU_TEVOP_RGB_SRC_ALPHA);
    CHECK_EQ_INT(cfg->rgb_op[1], GPU_TEVOP_RGB_SRC_ALPHA);
    CHECK_EQ_INT(cfg->rgb_op[2], GPU_TEVOP_RGB_SRC_ALPHA);
    CHECK_EQ_INT(cfg->rgb_src, GPU_PRIMARY_COLOR);
    CHECK_EQ_INT(cfg->alpha_src, GPU_CONSTANT);
    CHECK_EQ_INT(cfg->alpha_op, GPU_TEVOP_A_SRC_ALPHA);
    CHECK_EQ_INT(cfg->constant_color, 0xFF000000);
    CHECK(strcmp(cfg->name, "alpha-as-gray") == 0);
    test_report("diag-config/mode-1-alpha-as-gray");
}

static void test_mode_2_const_alpha_half(void) {
    const TwlhdDiagConfig *cfg = twlhd_diag_config(2);
    TwlhdBlendConfig alpha = twlhd_blend_config_alpha();

    CHECK_EQ_INT(cfg->mode, 2);
    check_blend_config(&cfg->blend, &alpha, "mode-2 blend");
    CHECK_EQ_INT(cfg->rgb_op[0], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_op[1], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_op[2], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(cfg->rgb_src, GPU_PRIMARY_COLOR);
    CHECK_EQ_INT(cfg->alpha_src, GPU_CONSTANT);
    CHECK_EQ_INT(cfg->alpha_op, GPU_TEVOP_A_SRC_ALPHA);
    CHECK_EQ_INT(cfg->constant_color, 0x80000000);
    CHECK(strcmp(cfg->name, "const-alpha-0.5") == 0);
    test_report("diag-config/mode-2-const-alpha-half");
}

static void test_mode_separation(void) {
    const TwlhdDiagConfig *m0 = twlhd_diag_config(0);
    const TwlhdDiagConfig *m1 = twlhd_diag_config(1);
    const TwlhdDiagConfig *m2 = twlhd_diag_config(2);

    /* Constant colours differ between mode 2 and modes 0/1. */
    CHECK(m2->constant_color != m0->constant_color);
    CHECK(m2->constant_color != m1->constant_color);

    /* Mode 1 uses opaque blend, modes 0 and 2 use alpha blend. */
    CHECK(!blend_config_equal(&m1->blend, &m0->blend));
    CHECK(blend_config_equal(&m0->blend, &m2->blend));

    /* Mode 1 alpha source is CONSTANT; modes 0 and 2 source PRIMARY_COLOR. */
    CHECK_EQ_INT(m1->alpha_src, GPU_CONSTANT);
    CHECK_EQ_INT(m0->alpha_src, GPU_PRIMARY_COLOR);
    CHECK_EQ_INT(m2->alpha_src, GPU_CONSTANT);

    /* Mode 1 RGB operand selects alpha; modes 0 and 2 select colour. */
    CHECK_EQ_INT(m1->rgb_op[0], GPU_TEVOP_RGB_SRC_ALPHA);
    CHECK_EQ_INT(m0->rgb_op[0], GPU_TEVOP_RGB_SRC_COLOR);
    CHECK_EQ_INT(m2->rgb_op[0], GPU_TEVOP_RGB_SRC_COLOR);
    test_report("diag-config/mode-separation");
}

static void test_cycle(void) {
    CHECK_EQ_INT(twlhd_diag_next(0), 1);
    CHECK_EQ_INT(twlhd_diag_next(1), 2);
    CHECK_EQ_INT(twlhd_diag_next(2), 0);
    CHECK_EQ_INT(twlhd_diag_next(3), 0);
    CHECK_EQ_INT(twlhd_diag_next(7), 0);
    CHECK_EQ_INT(twlhd_diag_next(0xFFFFFFFFu), 0);
    CHECK_EQ_INT(twlhd_diag_next(twlhd_diag_next(twlhd_diag_next(0))), 0);
    test_report("diag-config/cycle");
}

static void test_source_files(const char *path) {
    static char buf[65536];
    FILE *f = fopen(path, "rb");
    size_t n;
    const char *clip;
    const char *clip_tex;
    const char *debug;
    const char *console;
    const char *setter;
    static char clip_body[65536];
    static char clip_tex_body[65536];
    static char debug_body[65536];
    static char console_body[65536];
    static char set_body[65536];

    CHECK(f != NULL);
    if (!f) {
        test_report("diag-config/source-files");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    CHECK(n < sizeof(buf) - 1); /* fail loud if source outgrows the buffer */
    buf[n] = '\0';
    fclose(f);

    if (strstr(path, "main.c") != NULL) {
        CHECK_EQ_INT(count_substr(buf, "twlhd_diag_config("), 1);
        CHECK_EQ_INT(count_substr(buf, "renderer_set_diag_mode("), 1);

        console = extract_function(buf, "static void update_console(void)",
                                   "update_console", console_body,
                                   sizeof(console_body));
        if (console != NULL) {
            CHECK_EQ_INT(count_substr(console, "build: %s blend=explicit\\n"),
                         1);
            CHECK_EQ_INT(count_substr(console, "twlhd_diag_config("), 1);
        }
    } else if (strstr(path, "renderer.c") != NULL) {
        CHECK_EQ_INT(count_substr(buf, "twlhd_diag_config("), 2);

        CHECK_EQ_INT(count_substr(buf, "g_diag_mode ="), 1);
        CHECK_EQ_INT(count_substr(buf, "g_diag_mode = mode;"), 1);
        CHECK_EQ_INT(count_substr(buf, "mode > TWLHD_DIAG_MODE_COUNT - 1"), 1);

        setter = extract_function(buf, "renderer_set_diag_mode(unsigned mode)",
                                  "renderer_set_diag_mode", set_body,
                                  sizeof(set_body));
        if (setter != NULL) {
            CHECK_EQ_INT(count_substr(setter, "g_diag_mode"), 1);
        }
        CHECK_EQ_INT(count_substr(buf, "g_diag_mode"), 5);

        clip = extract_function(buf, "static void draw_clip_internal(",
                                "draw_clip_internal", clip_body,
                                sizeof(clip_body));
        clip_tex = extract_function(buf,
                                    "static void draw_clip_textured_entries(",
                                    "draw_clip_textured_entries",
                                    clip_tex_body, sizeof(clip_tex_body));
        debug = extract_function(buf, "static void draw_debug_internal(",
                                 "draw_debug_internal", debug_body,
                                 sizeof(debug_body));

        if (clip != NULL) {
            CHECK_EQ_INT(count_substr(clip, "twlhd_diag_config("), 1);
            CHECK_EQ_INT(count_substr(clip, "twlhd_diag_config(g_diag_mode)"), 1);
            CHECK_EQ_INT(count_substr(clip, "twlhd_blend_config_alpha("), 0);
            CHECK_EQ_INT(count_substr(clip, "C3D_AlphaBlend("), 1);
            CHECK_EQ_INT(count_substr(clip, "C3D_AlphaTest("), 1);
            CHECK_EQ_INT(count_substr(clip, "C3D_TexEnvColor("), 1);
        }

        if (clip_tex != NULL) {
            CHECK_EQ_INT(count_substr(clip_tex, "twlhd_diag_config("), 1);
            CHECK_EQ_INT(count_substr(clip_tex, "twlhd_diag_config(g_diag_mode)"), 1);
        }

        if (debug != NULL) {
            CHECK_EQ_INT(count_substr(debug, "twlhd_diag_config("), 0);
            CHECK_EQ_INT(count_substr(debug, "twlhd_blend_config_opaque("), 1);
        }
    }

    test_report(strstr(path, "main.c") != NULL ?
                "diag-config/source-files/main" :
                "diag-config/source-files/renderer");
}

static void test_renderer_call_sites(void) {
    const char *paths[] = {
        "arm11/source/main.c",
        "../arm11/source/main.c",
        "../../arm11/source/main.c",
    };
    const char *renderer_paths[] = {
        "arm11/source/renderer.c",
        "../arm11/source/renderer.c",
        "../../arm11/source/renderer.c",
    };
    size_t i;
    int found_main = 0;
    int found_renderer = 0;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) {
            fclose(f);
            test_source_files(paths[i]);
            found_main = 1;
            break;
        }
    }
    CHECK(found_main);

    for (i = 0; i < sizeof(renderer_paths) / sizeof(renderer_paths[0]); i++) {
        FILE *f = fopen(renderer_paths[i], "rb");
        if (f) {
            fclose(f);
            test_source_files(renderer_paths[i]);
            found_renderer = 1;
            break;
        }
    }
    CHECK(found_renderer);
}

int main(void) {
    test_mode_0_normal();
    test_mode_1_alpha_as_gray();
    test_mode_2_const_alpha_half();
    test_mode_separation();
    test_cycle();
    test_renderer_call_sites();
    return test_exit_code();
}
