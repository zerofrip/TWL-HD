// SPDX-License-Identifier: Apache-2.0
/*
 * Host unit tests for arm11/include/twlhd_blend_gate.h.
 */

#include "twlhd_blend_gate.h"

#include <stdio.h>
#include <string.h>

#include "tests_host.h"
#include "twlhd_v2_format.h"

static void write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void make_gxcm(uint8_t *out, uint32_t cmd_word, uint32_t value) {
    write_u32(out, cmd_word);
    write_u32(out + 4, value);
}

static void make_rsta(uint8_t *out, uint32_t addr, uint32_t value,
                      uint32_t gx_index, uint32_t width) {
    write_u32(out, addr);
    write_u32(out + 4, value);
    write_u32(out + 8, gx_index);
    write_u32(out + 12, width);
}

static void test_default_gate(void) {
    TwlhdBlendGate gate = twlhd_blend_gate_default();
    CHECK(!gate.seen);
    CHECK(gate.enabled);
    test_report("blend-gate/default");
}

static void test_bit_set(void) {
    uint8_t gxcm[8];
    uint8_t rsta[16];
    TwlhdV2View view = {0};

    make_gxcm(gxcm, TWLHD_V2_CMD_BEGIN_VTXS, 0);
    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x18u, 0u, 4u);

    view.gxcm = gxcm;
    view.gxcm_count = 1;
    view.rsta = rsta;
    view.rsta_count = 1;

    CHECK(twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/bit-set");
}

static void test_bit_clear(void) {
    uint8_t gxcm[8];
    uint8_t rsta[16];
    TwlhdV2View view = {0};

    make_gxcm(gxcm, TWLHD_V2_CMD_BEGIN_VTXS, 0);
    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x10u, 0u, 4u);

    view.gxcm = gxcm;
    view.gxcm_count = 1;
    view.rsta = rsta;
    view.rsta_count = 1;

    CHECK(!twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/bit-clear");
}

static void test_last_write_wins(void) {
    uint8_t gxcm[24];
    uint8_t rsta[32];
    TwlhdV2View view = {0};

    /* Two non-BEGIN entries, then BEGIN at index 2, so both RSTA writes are
     * at or before the first draw. */
    make_gxcm(gxcm + 0, TWLHD_V2_CMD_MTX_MODE, 0);
    make_gxcm(gxcm + 8, TWLHD_V2_CMD_MTX_IDENTITY, 0);
    make_gxcm(gxcm + 16, TWLHD_V2_CMD_BEGIN_VTXS, 0);

    make_rsta(rsta + 0, TWLHD_BLEND_GATE_ADDR, 0x18u, 0u, 4u);
    make_rsta(rsta + 16, TWLHD_BLEND_GATE_ADDR, 0x10u, 1u, 4u);

    view.gxcm = gxcm;
    view.gxcm_count = 3;
    view.rsta = rsta;
    view.rsta_count = 2;

    CHECK(!twlhd_blend_gate_from_view(&view));

    /* Reverse order: set is the last qualifying write. */
    make_rsta(rsta + 0, TWLHD_BLEND_GATE_ADDR, 0x10u, 0u, 4u);
    make_rsta(rsta + 16, TWLHD_BLEND_GATE_ADDR, 0x18u, 1u, 4u);
    CHECK(twlhd_blend_gate_from_view(&view));

    test_report("blend-gate/last-write-wins");
}

static void test_after_first_draw_ignored(void) {
    uint8_t gxcm[24];
    uint8_t rsta[16];
    TwlhdV2View view = {0};

    /* Some non-BEGIN commands, then BEGIN at index 2. */
    make_gxcm(gxcm + 0, TWLHD_V2_CMD_MTX_MODE, 0);
    make_gxcm(gxcm + 8, TWLHD_V2_CMD_MTX_IDENTITY, 0);
    make_gxcm(gxcm + 16, TWLHD_V2_CMD_BEGIN_VTXS, 0);

    /* Clear written after the first draw begins. */
    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x10u, 3u, 4u);

    view.gxcm = gxcm;
    view.gxcm_count = 3;
    view.rsta = rsta;
    view.rsta_count = 1;

    CHECK(twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/after-first-draw-ignored");
}

static void test_no_entry_default(void) {
    uint8_t gxcm[8];
    TwlhdV2View view = {0};

    make_gxcm(gxcm, TWLHD_V2_CMD_BEGIN_VTXS, 0);
    view.gxcm = gxcm;
    view.gxcm_count = 1;

    CHECK(twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/no-entry-default");
}

static void test_other_addresses_ignored(void) {
    uint8_t gxcm[8];
    uint8_t rsta[32];
    TwlhdV2View view = {0};

    make_gxcm(gxcm, TWLHD_V2_CMD_BEGIN_VTXS, 0);
    make_rsta(rsta + 0, 0x04000061u, 0x18u, 0u, 4u);
    make_rsta(rsta + 16, 0x04000350u, 0x18u, 0u, 4u);

    view.gxcm = gxcm;
    view.gxcm_count = 1;
    view.rsta = rsta;
    view.rsta_count = 2;

    CHECK(twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/other-addresses-ignored");
}

static void test_only_bit3_counts(void) {
    uint8_t gxcm[8];
    uint8_t rsta[16];
    TwlhdV2View view = {0};

    make_gxcm(gxcm, TWLHD_V2_CMD_BEGIN_VTXS, 0);
    view.gxcm = gxcm;
    view.gxcm_count = 1;
    view.rsta = rsta;
    view.rsta_count = 1;

    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x04u, 0u, 4u);
    CHECK(!twlhd_blend_gate_from_view(&view));

    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x10u, 0u, 4u);
    CHECK(!twlhd_blend_gate_from_view(&view));

    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0xF7u, 0u, 4u);
    CHECK(!twlhd_blend_gate_from_view(&view));

    make_rsta(rsta, TWLHD_BLEND_GATE_ADDR, 0x08u, 0u, 4u);
    CHECK(twlhd_blend_gate_from_view(&view));

    test_report("blend-gate/only-bit-3-counts");
}

static void test_first_draw_scan_no_begin(void) {
    uint8_t gxcm[16];
    TwlhdV2View view = {0};

    make_gxcm(gxcm + 0, TWLHD_V2_CMD_MTX_MODE, 0);
    make_gxcm(gxcm + 8, TWLHD_V2_CMD_MTX_IDENTITY, 0);

    view.gxcm = gxcm;
    view.gxcm_count = 2;

    CHECK_EQ_INT(twlhd_blend_gate_first_draw(&view), 2u);
    CHECK(twlhd_blend_gate_from_view(&view));
    test_report("blend-gate/first-draw-scan-no-begin");
}

int main(void) {
    test_default_gate();
    test_bit_set();
    test_bit_clear();
    test_last_write_wins();
    test_after_first_draw_ignored();
    test_no_entry_default();
    test_other_addresses_ignored();
    test_only_bit3_counts();
    test_first_draw_scan_no_begin();
    return test_exit_code();
}
