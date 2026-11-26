// SPDX-License-Identifier: Apache-2.0
/*
 * Structural host test for the arm11 renderer clear colour.
 *
 * Verifies that TWLHD_RENDER_CLEAR_COLOR is defined exactly once in the
 * freestanding header, that the default is opaque black, that the override
 * macro can restore the old diagnostic colour, and that renderer.c uses the
 * macro at both clear sites with no stray local definition.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_render_config.h"

#ifndef OVERRIDE_TEST

static void test_default_clear_colour(void) {
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_COLOR, 0x000000FFu);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_R, 0x00u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_G, 0x00u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_B, 0x00u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_A, 0xFFu);
    test_report("render-clear/default-colour");
}

static int count_substr(const char *haystack, const char *needle) {
    int count = 0;
    const char *p = haystack;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

static void test_renderer_source_uses_header_macro(void) {
    static char buf[65536];
    const char *paths[] = {
        "arm11/source/renderer.c",
        "../arm11/source/renderer.c",
        "../../arm11/source/renderer.c",
    };
    FILE *f = NULL;
    size_t i, n;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        f = fopen(paths[i], "rb");
        if (f) {
            break;
        }
    }
    CHECK(f != NULL);
    if (!f) {
        test_report("render-clear/source-renderer");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    CHECK(n < sizeof(buf) - 1);
    buf[n] = '\0';
    fclose(f);

    /* No local definition of the old macro. */
    CHECK_EQ_INT(count_substr(buf, "#define RENDER_CLEAR_COLOR"), 0);
    /* The two C3D_RenderTargetClear sites both use the header macro. */
    CHECK_EQ_INT(count_substr(buf, "TWLHD_RENDER_CLEAR_COLOR"), 2);
    /* Sanity: the old literal is gone. */
    CHECK_EQ_INT(count_substr(buf, "0x101018FFu"), 0);

    test_report("render-clear/source-renderer");
}

static void test_header_defines_clear_colour_once(void) {
    static char buf[65536];
    const char *paths[] = {
        "arm11/include/twlhd_render_config.h",
        "../arm11/include/twlhd_render_config.h",
        "../../arm11/include/twlhd_render_config.h",
    };
    FILE *f = NULL;
    size_t i, n;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        f = fopen(paths[i], "rb");
        if (f) {
            break;
        }
    }
    CHECK(f != NULL);
    if (!f) {
        test_report("render-clear/header-defines-once");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    CHECK(n < sizeof(buf) - 1);
    buf[n] = '\0';
    fclose(f);

    /* The default macro is defined exactly once in the header. */
    CHECK_EQ_INT(count_substr(buf, "#define TWLHD_RENDER_CLEAR_COLOR"), 1);

    test_report("render-clear/header-defines-once");
}

static void test_makefile_has_append_hook(void) {
    static char buf[65536];
    const char *paths[] = {
        "arm11/Makefile",
        "../arm11/Makefile",
        "../../arm11/Makefile",
    };
    FILE *f = NULL;
    size_t i, n;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        f = fopen(paths[i], "rb");
        if (f) {
            break;
        }
    }
    CHECK(f != NULL);
    if (!f) {
        test_report("render-clear/makefile-hook");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    CHECK(n < sizeof(buf) - 1);
    buf[n] = '\0';
    fclose(f);

    /* Append-style hook for TWLHD_EXTRA_CFLAGS must exist (EQ-4z). */
    CHECK(strstr(buf, "CFLAGS += $(TWLHD_EXTRA_CFLAGS)") != NULL);
    /* The documented append-style override example must be present (never CFLAGS=...). */
    CHECK(strstr(buf, "TWLHD_EXTRA_CFLAGS=-DTWLHD_RENDER_CLEAR_COLOR") != NULL);

    test_report("render-clear/makefile-hook");
}

int main(void) {
    test_default_clear_colour();
    test_renderer_source_uses_header_macro();
    test_header_defines_clear_colour_once();
    test_makefile_has_append_hook();
    return test_exit_code();
}

#else /* OVERRIDE_TEST */

int main(void) {
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_COLOR, 0x101018FFu);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_R, 0x10u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_G, 0x10u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_B, 0x18u);
    CHECK_EQ_INT(TWLHD_RENDER_CLEAR_A, 0xFFu);
    test_report("render-clear/override-colour");
    return test_exit_code();
}

#endif
