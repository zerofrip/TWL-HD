// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for common/twlhd_attr_pad.c.
 */

#include "twlhd_attr_pad.h"

#include "tests_host.h"

#include <stdint.h>

static void check_ok(uint32_t stride, uint32_t declared_bytes,
                     uint32_t real_components, uint64_t real_perm,
                     uint64_t expected_perm, uint32_t expected_components,
                     const char *name) {
    uint64_t perm = 0;
    uint32_t comps = 0;

    CHECK_EQ_INT(twlhd_attr_pad_perm(stride, declared_bytes, real_components,
                                     real_perm, &perm, &comps), 0);
    CHECK_EQ_INT(comps, (long long)expected_components);
    CHECK_EQ_INT(perm, (long long)expected_perm);
    test_report(name);
}

static void check_err(uint32_t stride, uint32_t declared_bytes,
                      uint32_t real_components, uint64_t real_perm,
                      int expected_err, const char *name) {
    uint64_t perm = 0xDEADBEEFu;
    uint32_t comps = 0xBADu;

    CHECK_EQ_INT(twlhd_attr_pad_perm(stride, declared_bytes, real_components,
                                     real_perm, &perm, &comps), expected_err);
    test_report(name);
}

int main(void) {
    /* stride 48 / 28 declared bytes: 20-byte gap = 0xF + 0xC padding. */
    check_ok(48, 28, 2, 0x10u, 0xCF10u, 4,
             "attr_pad/48-28-two-loaders");

    /* Stride equals declared bytes: no padding. */
    check_ok(28, 28, 2, 0x10u, 0x10u, 2,
             "attr_pad/no-gap-unchanged");

    /* Positions-only 12/12: one component, no padding. */
    check_ok(12, 12, 1, 0x0u, 0x0u, 1,
             "attr_pad/12-12-position-only");

    /* FIXED48-style: 48-byte stride, 12 declared bytes, 36-byte gap. */
    check_ok(48, 12, 1, 0x0u, 0xCFF0u, 4,
             "attr_pad/48-12-fixed48");

    /* Large gap: 100 bytes = 6*16 + 4, requiring a nibble in the high word. */
    check_ok(128, 28, 2, 0x10u, 0xCFFFFFF10u, 9,
             "attr_pad/128-28-large-gap");

    /* Error: stride smaller than declared bytes. */
    check_err(28, 48, 2, 0x10u,
              TWLHD_ATTR_PAD_ERR_STRIDE,
              "attr_pad/err-stride-too-small");

    /* Error: gap is not a multiple of 4. */
    check_err(50, 28, 2, 0x10u,
              TWLHD_ATTR_PAD_ERR_GAP,
              "attr_pad/err-gap-alignment");

    /* Error: too many real components. */
    check_err(48, 12, 13, 0x0u,
              TWLHD_ATTR_PAD_ERR_REAL_COMPS,
              "attr_pad/err-real-components");

    /* Error: total components would exceed 12. */
    check_err(256, 12, 1, 0x0u,
              TWLHD_ATTR_PAD_ERR_TOTAL_COMPS,
              "attr_pad/err-total-components");

    return test_exit_code();
}
