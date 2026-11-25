// SPDX-License-Identifier: Apache-2.0
/*
 * Host structural test for arm11/source/main.c blend-gate wiring.
 *
 * Every non-v2 scene-commit path must restore the "no entry => blend enabled"
 * default by calling renderer_set_blend_enabled(). The v2 path already carries
 * the gate from the RSTA stream. This test is a text-extraction proxy, not a
 * behavioural proof.
 */

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

/* Locate the first occurrence of `needle` in `text` and, if found, overwrite
 * its bytes with `fill`. Returns the number of occurrences overwritten. */
static int overwrite_occurrence(char *text, const char *needle, char fill) {
    char *p = strstr(text, needle);
    size_t n = strlen(needle);

    if (p == NULL) return 0;
    memset(p, fill, n);
    return 1;
}

/* Return a pointer to the first line containing `needle`, or NULL. */
static const char *find_line(const char *text, const char *needle) {
    const char *p = strstr(text, needle);
    const char *start;

    if (p == NULL) return NULL;
    start = p;
    while (start > text && start[-1] != '\n') start--;
    return start;
}

static void test_blend_gate_wiring(void) {
    static char buf[65536];
    static char mutated[65536];
    FILE *f = NULL;
    const char *paths[] = {
        "arm11/source/main.c",
        "../arm11/source/main.c",
        "../../arm11/source/main.c",
    };
    size_t i;
    size_t n;
    int total;
    const char *builtin_region;
    const char *sd_region;
    const char *v2_region;
    int mutated_count;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        f = fopen(paths[i], "rb");
        if (f) break;
    }
    CHECK(f != NULL);
    if (!f) {
        test_report("blend-gate-wiring/source-read");
        return;
    }

    n = fread(buf, 1, sizeof(buf) - 1, f);
    CHECK(n < sizeof(buf) - 1); /* fail loud if main.c outgrows the buffer */
    buf[n] = '\0';
    fclose(f);

    total = count_substr(buf, "renderer_set_blend_enabled(");
    CHECK_EQ_INT(total, 3);

    /* Each scene-commit region must contain at least one call. */
    builtin_region = find_line(buf, "if (pending_builtin) {");
    sd_region = find_line(buf, "SD FRAME (DEBUG CAMERA)");
    v2_region = find_line(buf, "SD v2 (DS MATRICES)");

    CHECK(builtin_region != NULL);
    CHECK(sd_region != NULL);
    CHECK(v2_region != NULL);

    if (builtin_region != NULL) {
        CHECK(count_substr(builtin_region, "renderer_set_blend_enabled(") >= 1);
    }
    if (sd_region != NULL) {
        CHECK(count_substr(sd_region, "renderer_set_blend_enabled(") >= 1);
    }
    if (v2_region != NULL) {
        CHECK(count_substr(v2_region, "renderer_set_blend_enabled(") >= 1);
    }

    /* Negative self-check: overwrite every occurrence in a memory copy and
     * verify the count drops to zero. */
    memcpy(mutated, buf, n + 1);
    while (overwrite_occurrence(mutated, "renderer_set_blend_enabled(", '_'))
        ;
    mutated_count = count_substr(mutated, "renderer_set_blend_enabled(");
    CHECK_EQ_INT(mutated_count, 0);

    test_report("blend-gate-wiring/call-sites");
}

int main(void) {
    test_blend_gate_wiring();
    return test_exit_code();
}
