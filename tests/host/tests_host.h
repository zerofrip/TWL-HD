// SPDX-License-Identifier: Apache-2.0
/*
 * Tiny assert-style harness for the TWL-HD host tests. No dependencies.
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_tests_run;
static int g_tests_failed;
static int g_checks_failed;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            g_checks_failed++;                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                      \
    do {                                                                     \
        long long a_ = (long long)(actual);                                  \
        long long e_ = (long long)(expected);                                \
        if (a_ != e_) {                                                      \
            g_checks_failed++;                                               \
            fprintf(stderr, "%s:%d: expected %s == %lld, got %lld\n",        \
                    __FILE__, __LINE__, #expected, e_, a_);                  \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                              \
    do {                                                                     \
        double a_ = (double)(actual);                                        \
        double e_ = (double)(expected);                                      \
        if (!(fabs(a_ - e_) <= (double)(tolerance))) {                       \
            g_checks_failed++;                                               \
            fprintf(stderr, "%s:%d: expected %s ~= %f, got %f\n",            \
                    __FILE__, __LINE__, #expected, e_, a_);                  \
        }                                                                    \
    } while (0)

static void test_report(const char *name) {
    g_tests_run++;
    if (g_checks_failed != 0) {
        g_tests_failed++;
        fprintf(stderr, "FAIL %s (%d failed checks)\n", name, g_checks_failed);
    } else {
        printf("ok   %s\n", name);
    }
    g_checks_failed = 0;
}

static int test_exit_code(void) {
    if (g_tests_failed != 0) {
        fprintf(stderr, "%d test(s) failed\n", g_tests_failed);
        return 1;
    }
    printf("%d test(s) passed\n", g_tests_run);
    return 0;
}
