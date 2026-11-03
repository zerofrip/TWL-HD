// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_seq.h.
 */

#include "twlhd_seq.h"

#include "tests_host.h"

int main(void) {
    TwlhdSeqState s;
    char path[64];
    int i;

    /* (a) Reset puts everything at zero. */
    twlhd_seq_reset(&s);
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.displayed, 0);
    CHECK_EQ_INT(s.loop_count, 0);
    CHECK_EQ_INT(s.first_pass_count, 0);
    test_report("seq/reset");

    /* (b) Path formatting for the first and last frames. */
    CHECK_EQ_INT(twlhd_seq_path(path, sizeof path, 0), 0);
    CHECK(strcmp(path, "sdmc:/twlhd_seq/seq_000.bin") == 0);
    CHECK_EQ_INT(twlhd_seq_path(path, sizeof path, 63), 0);
    CHECK(strcmp(path, "sdmc:/twlhd_seq/seq_063.bin") == 0);
    test_report("seq/path-format");

    /* (c) Truncation on a too-small buffer is reported as an error. */
    CHECK_EQ_INT(twlhd_seq_path(path, 10, 0), -1);
    CHECK_EQ_INT(twlhd_seq_path(path, 27, 0), -1);
    CHECK_EQ_INT(twlhd_seq_path(path, 28, 0), 0);
    CHECK(strcmp(path, "sdmc:/twlhd_seq/seq_000.bin") == 0);
    test_report("seq/path-truncation");

    /* (d) Sequential advance 0 -> 1 -> ... -> 63. */
    twlhd_seq_reset(&s);
    for (i = 0; i < 63; i++) {
        CHECK_EQ_INT(s.index, (uint32_t)i);
        twlhd_seq_advance(&s, 1);
    }
    CHECK_EQ_INT(s.index, 63);
    CHECK_EQ_INT(s.displayed, 62);
    CHECK_EQ_INT(s.loop_count, 0);
    CHECK_EQ_INT(s.first_pass_count, 0);
    test_report("seq/sequential-advance");

    /* (e) Wrapping at 64 returns to 0 and counts one loop. */
    twlhd_seq_advance(&s, 1);
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.displayed, 63);
    CHECK_EQ_INT(s.loop_count, 1);
    CHECK_EQ_INT(s.first_pass_count, SEQ_MAX_FRAMES);
    test_report("seq/wrap-at-max");

    /* (f) Loop counter increments on every wrap. */
    for (i = 0; i < 64; i++) {
        twlhd_seq_advance(&s, 1);
    }
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.loop_count, 2);
    test_report("seq/loop-increments");

    /* (g) Missing file forces an early wrap and loop increment. */
    twlhd_seq_reset(&s);
    for (i = 0; i < 5; i++) {
        twlhd_seq_advance(&s, 1);
    }
    CHECK_EQ_INT(s.index, 5);
    CHECK_EQ_INT(s.displayed, 4);
    twlhd_seq_advance(&s, 0);
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.displayed, 4);
    CHECK_EQ_INT(s.loop_count, 1);
    CHECK_EQ_INT(s.first_pass_count, 5);
    test_report("seq/wrap-on-missing-file");

    /* (h) Missing first file (seq_000) guard: exists=0 at index 0 wraps. */
    twlhd_seq_reset(&s);
    CHECK_EQ_INT(twlhd_seq_path(path, sizeof path, s.index), 0);
    CHECK(strcmp(path, "sdmc:/twlhd_seq/seq_000.bin") == 0);
    twlhd_seq_advance(&s, 0);
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.displayed, 0);
    CHECK_EQ_INT(s.loop_count, 1);
    CHECK_EQ_INT(s.first_pass_count, 0);
    test_report("seq/start-guard-missing-000");

    /* (i) First-wrap frame count is preserved across later wraps. */
    twlhd_seq_reset(&s);
    for (i = 0; i < 5; i++) {
        twlhd_seq_advance(&s, 1);
    }
    twlhd_seq_advance(&s, 0);
    CHECK_EQ_INT(s.loop_count, 1);
    CHECK_EQ_INT(s.first_pass_count, 5);
    for (i = 0; i < 64; i++) {
        twlhd_seq_advance(&s, 1);
    }
    CHECK_EQ_INT(s.index, 0);
    CHECK_EQ_INT(s.loop_count, 2);
    CHECK_EQ_INT(s.first_pass_count, 5);
    test_report("seq/first-wrap-count-preserved");

    return test_exit_code();
}
