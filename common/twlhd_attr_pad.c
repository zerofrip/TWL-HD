// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4C - PICA200 vertex-attribute padding computation.
 *
 * See common/twlhd_attr_pad.h for the hypothesis and API contract.
 */

#include "twlhd_attr_pad.h"

#include <stddef.h>

int twlhd_attr_pad_perm(uint32_t stride, uint32_t declared_bytes,
                        uint32_t real_components, uint64_t real_perm,
                        uint64_t *out_perm, uint32_t *out_components) {
    uint32_t gap;
    uint32_t pad_components;
    uint32_t total_components;
    uint32_t full_16;
    uint32_t remainder;
    uint64_t perm;
    uint32_t i;
    uint32_t idx;

    if (out_perm == NULL || out_components == NULL) {
        return TWLHD_ATTR_PAD_ERR_STRIDE;
    }

    if (stride < declared_bytes) {
        return TWLHD_ATTR_PAD_ERR_STRIDE;
    }

    if (real_components > 12) {
        return TWLHD_ATTR_PAD_ERR_REAL_COMPS;
    }

    gap = stride - declared_bytes;
    if (gap == 0) {
        *out_perm = real_perm;
        *out_components = real_components;
        return 0;
    }

    if ((gap & 3u) != 0) {
        return TWLHD_ATTR_PAD_ERR_GAP;
    }

    full_16 = gap / 16;
    remainder = gap % 16;
    pad_components = full_16 + (remainder ? 1u : 0u);
    total_components = real_components + pad_components;

    if (total_components > 12) {
        return TWLHD_ATTR_PAD_ERR_TOTAL_COMPS;
    }

    /* Preserve the real permutation nibbles, then append padding. */
    perm = real_perm;
    for (i = real_components; i < total_components; i++) {
        uint32_t nibble;

        if (i < real_components + full_16) {
            nibble = 0xFu;
        } else {
            switch (remainder) {
            case 4:  nibble = 0xCu; break;
            case 8:  nibble = 0xDu; break;
            case 12: nibble = 0xEu; break;
            default: nibble = 0xCu; break; /* unreachable when gap%4==0 */
            }
        }

        idx = i;
        perm &= ~(0xFull << (idx * 4));
        perm |= (uint64_t)nibble << (idx * 4);
    }

    *out_perm = perm;
    *out_components = total_components;
    return 0;
}
