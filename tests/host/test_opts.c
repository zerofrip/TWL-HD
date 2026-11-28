// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_opts.c and for the arm11 wiring of the options
 * file (text-extraction proxy, like test_blend_config.c).
 */

#include <stdio.h>
#include <string.h>

#include "twlhd_gx_interp.h"
#include "twlhd_opts.h"

#include "tests_host.h"

#define SENTINEL 0xA5A5A5A5u

static TwlhdOptsResult parse(const char *text, uint32_t *gx) {
    return twlhd_opts_parse(text, strlen(text), gx);
}

static void test_gx_opts_numbers(void) {
    uint32_t gx = SENTINEL;

    CHECK_EQ_INT(parse("gx_opts = 0x3\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 3);
    CHECK_EQ_INT(parse("gx_opts=1", &gx), TWLHD_OPTS_OK); /* no newline */
    CHECK_EQ_INT(gx, 1);
    CHECK_EQ_INT(parse("  GX_OPTS =\t2  # comment\r\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 2);
    CHECK_EQ_INT(parse("gx_opts = 0\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 0);
    CHECK_EQ_INT(parse("gx_opts = 0X3\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 3);
    test_report("opts/gx-opts-numbers");
}

static void test_gx_words(void) {
    uint32_t gx = SENTINEL;

    CHECK_EQ_INT(parse("gx = cull ds_stack\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, TWLHD_GX_OPT_CULL | TWLHD_GX_OPT_DS_STACK);
    CHECK_EQ_INT(parse("gx = ds_stack,cull\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 3);
    CHECK_EQ_INT(parse("gx = Cull\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, TWLHD_GX_OPT_CULL);
    CHECK_EQ_INT(parse("gx = none\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 0);
    test_report("opts/gx-words");
}

static void test_masks_match_interpreter_flags(void) {
    CHECK_EQ_INT(TWLHD_OPTS_GX_KNOWN_BITS,
                 TWLHD_GX_OPT_CULL | TWLHD_GX_OPT_DS_STACK);
    test_report("opts/known-bits-equal-interpreter-flags");
}

static void test_comments_blank_last_wins(void) {
    uint32_t gx = SENTINEL;

    CHECK_EQ_INT(parse("", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, SENTINEL); /* nothing to set: untouched */
    CHECK_EQ_INT(parse("# only a comment\n\n   \n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, SENTINEL);
    CHECK_EQ_INT(parse("gx_opts = 1\ngx_opts = 2\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 2);
    CHECK_EQ_INT(parse("gx_opts = 1\ngx = ds_stack\n", &gx), TWLHD_OPTS_OK);
    CHECK_EQ_INT(gx, 2);
    test_report("opts/comments-blank-last-wins");
}

static void test_errors_leave_output_untouched(void) {
    static const char *const syntax[] = {
        "gx_opts 3\n",      "= 3\n",         "bogus = 1\n",
        "gx = flyer\n",     "gx_opts: 3\n",  "gx_opts = 1\nnonsense\n",
        "gx = cull!\n",
    };
    static const char *const value[] = {
        "gx_opts =\n",      "gx_opts = 4\n", "gx_opts = 0x80000000\n",
        "gx_opts = abc\n",  "gx_opts = 0x\n","gx_opts = 4294967296\n",
        "gx_opts = -1\n",   "gx_opts = 1 2\n", "gx_opts = 1\ngx_opts = 8\n",
    };
    size_t i;
    uint32_t gx;

    for (i = 0; i < sizeof syntax / sizeof syntax[0]; i++) {
        gx = SENTINEL;
        CHECK_EQ_INT(parse(syntax[i], &gx), TWLHD_OPTS_ERR_SYNTAX);
        CHECK_EQ_INT(gx, SENTINEL);
    }
    for (i = 0; i < sizeof value / sizeof value[0]; i++) {
        gx = SENTINEL;
        CHECK_EQ_INT(parse(value[i], &gx), TWLHD_OPTS_ERR_VALUE);
        CHECK_EQ_INT(gx, SENTINEL);
    }
    test_report("opts/errors-leave-output-untouched");
}

static void test_null_and_size(void) {
    static char big[TWLHD_OPTS_MAX_BYTES + 2u];
    uint32_t gx = SENTINEL;

    CHECK_EQ_INT(twlhd_opts_parse(NULL, 0, &gx), TWLHD_OPTS_ERR_NULL);
    CHECK_EQ_INT(twlhd_opts_parse("gx_opts=1", 9, NULL), TWLHD_OPTS_ERR_NULL);
    memset(big, '#', sizeof big);
    CHECK_EQ_INT(twlhd_opts_parse(big, TWLHD_OPTS_MAX_BYTES, &gx),
                 TWLHD_OPTS_OK);
    CHECK_EQ_INT(twlhd_opts_parse(big, TWLHD_OPTS_MAX_BYTES + 1u, &gx),
                 TWLHD_OPTS_ERR_SIZE);
    CHECK_EQ_INT(gx, SENTINEL);
    CHECK(strcmp(twlhd_opts_result_string(TWLHD_OPTS_ERR_SIZE), "ERR_SIZE") == 0);
    test_report("opts/null-and-size");
}

static void test_not_nul_terminated(void) {
    const char text[] = {'g', 'x', '_', 'o', 'p', 't', 's', '=', '3', 'X'};
    uint32_t gx = SENTINEL;

    CHECK_EQ_INT(twlhd_opts_parse(text, 9, &gx), TWLHD_OPTS_OK); /* excludes X */
    CHECK_EQ_INT(gx, 3);
    gx = SENTINEL;
    CHECK_EQ_INT(twlhd_opts_parse(text, 10, &gx), TWLHD_OPTS_ERR_VALUE);
    CHECK_EQ_INT(gx, SENTINEL);
    test_report("opts/not-nul-terminated");
}

static int count_substr(const char *hay, const char *needle) {
    int n = 0;
    size_t len = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

static void test_arm11_wiring(void) {
    static char buf[262144];
    FILE *f = NULL;
    const char *paths[] = {"arm11/source/main.c", "../arm11/source/main.c",
                           "../../arm11/source/main.c"};
    size_t i, n;

    for (i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        f = fopen(paths[i], "rb");
        if (f) break;
    }
    CHECK(f != NULL);
    if (!f) { test_report("opts/arm11-wiring"); return; }
    n = fread(buf, 1, sizeof buf - 1, f);
    CHECK(n < sizeof buf - 1);
    buf[n] = '\0';
    fclose(f);

    CHECK_EQ_INT(count_substr(buf, "#define OPTS_PATH \"sdmc:/twlhd_opts.txt\""), 1);
    CHECK_EQ_INT(count_substr(buf, "twlhd_opts_parse("), 1);
    CHECK_EQ_INT(count_substr(buf, "static uint32_t g_gx_opts = TWLHD_ARM11_GX_OPTS;"), 1);
    /* The interpreter is called with the runtime mask, not the constant. */
    CHECK_EQ_INT(count_substr(buf, "twlhd_gx_interpret_auto("), 1);
    CHECK_EQ_INT(count_substr(buf, "g_gx_opts, &used_gx_opts);"), 1);
    CHECK_EQ_INT(count_substr(buf, "twlhd_gx_interpret_opts("), 0);
    CHECK_EQ_INT(count_substr(buf, "TWLHD_ARM11_GX_OPTS);"), 0);
    CHECK_EQ_INT(count_substr(buf, "renderer_trace(\"auto cull\");"), 1);
    /* The file is re-read on every frame load. */
    CHECK_EQ_INT(count_substr(buf, "    load_opts_file();\n"), 1);
    test_report("opts/arm11-wiring");
}

int main(void) {
    test_gx_opts_numbers();
    test_gx_words();
    test_masks_match_interpreter_flags();
    test_comments_blank_last_wins();
    test_errors_leave_output_untouched();
    test_null_and_size();
    test_not_nul_terminated();
    test_arm11_wiring();
    return test_exit_code();
}
