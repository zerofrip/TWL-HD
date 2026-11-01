// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4C - declare PICA200 vertex-attribute padding so that the
 * buffer's declared bytes match its stride.
 *
 * Pure C11, no allocation, host-tested.
 */
#pragma once

#include <stdint.h>

#define TWLHD_ATTR_PAD_ERR_STRIDE      1
#define TWLHD_ATTR_PAD_ERR_GAP         2
#define TWLHD_ATTR_PAD_ERR_REAL_COMPS  3
#define TWLHD_ATTR_PAD_ERR_TOTAL_COMPS 4

/*
 * Compute a padded permutation and component count for a PICA200 vertex buffer.
 *
 * real_perm uses one nibble per component; nibbles 0..11 name attribute
 * registers, while 0xC/0xD/0xE/0xF declare padding of 4/8/12/16 bytes.
 *
 * On success returns 0 and writes the padded permutation/component count.
 * If stride == declared_bytes the outputs equal the inputs (no padding).
 *
 * Error codes (distinct, nonzero):
 *   TWLHD_ATTR_PAD_ERR_STRIDE      - stride < declared_bytes
 *   TWLHD_ATTR_PAD_ERR_GAP         - (stride - declared_bytes) is not a
 *                                    multiple of 4
 *   TWLHD_ATTR_PAD_ERR_REAL_COMPS  - real_components > 12
 *   TWLHD_ATTR_PAD_ERR_TOTAL_COMPS - real + padding components exceeds 12
 */
int twlhd_attr_pad_perm(uint32_t stride, uint32_t declared_bytes,
                        uint32_t real_components, uint64_t real_perm,
                        uint64_t *out_perm, uint32_t *out_components);
