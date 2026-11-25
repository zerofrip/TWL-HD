// SPDX-License-Identifier: Apache-2.0
/*
 * EQ-4x blend gate: derive the DS alpha-blend enable (GFX_CONTROL / DISP3DCNT
 * 0x04000060 bit 3) from the v2 RSTA stream.
 *
 * Pure C11, no 3ds headers, host-testable. The default is "blend enabled" so
 * frames with no RSTA entry for 0x04000060 keep the previous unconditional
 * alpha-blending behaviour.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "twlhd_v2_parse.h"
#include "twlhd_v2_format.h"

#define TWLHD_BLEND_GATE_ADDR 0x04000060u
#define TWLHD_BLEND_GATE_BIT  (1u << 3)

typedef struct {
    bool seen;     /* true if at least one qualifying write was observed */
    bool enabled;  /* true if the last qualifying write had bit 3 set */
} TwlhdBlendGate;

static inline TwlhdBlendGate twlhd_blend_gate_default(void) {
    /* No evidence either way: preserve the historical unconditional blend. */
    TwlhdBlendGate gate = {false, true};
    return gate;
}

static inline void twlhd_blend_gate_observe(TwlhdBlendGate *gate,
                                            uint32_t addr,
                                            uint32_t value,
                                            uint32_t gx_index,
                                            uint32_t first_draw_index) {
    if (gate == NULL) return;
    if (addr != TWLHD_BLEND_GATE_ADDR) return;
    if (gx_index > first_draw_index) return;
    gate->seen = true;
    gate->enabled = (value & TWLHD_BLEND_GATE_BIT) != 0;
}

static inline uint32_t twlhd_blend_gate_first_draw(const TwlhdV2View *view) {
    uint32_t i;
    uint32_t n;

    if (view == NULL) return 0;
    n = view->gxcm_count;
    for (i = 0; i < n; i++) {
        uint32_t cmd = twlhd_v2_gxcm_cmd_word(view, i);
        if ((cmd & TWLHD_V2_CMD_ID_MASK) == TWLHD_V2_CMD_BEGIN_VTXS) return i;
    }
    /* Nothing drawn: every write is "at or before" the (non-existent) draw. */
    return n;
}

static inline bool twlhd_blend_gate_from_view(const TwlhdV2View *view) {
    TwlhdBlendGate gate = twlhd_blend_gate_default();
    uint32_t first;
    uint32_t i;
    uint32_t n;

    if (view == NULL) return gate.enabled;

    first = twlhd_blend_gate_first_draw(view);
    n = view->rsta_count;
    for (i = 0; i < n; i++) {
        twlhd_blend_gate_observe(&gate,
                                 twlhd_v2_rsta_addr(view, i),
                                 twlhd_v2_rsta_value(view, i),
                                 twlhd_v2_rsta_gx_index(view, i),
                                 first);
    }
    return gate.enabled;
}
