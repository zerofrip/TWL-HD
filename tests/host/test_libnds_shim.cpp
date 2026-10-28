// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for arm9v2/include/twlhd_gx_shim.hpp.
 *
 * This is real libnds header code (videoGL.h inline helpers) compiled for the
 * host against the installed devkitPro headers, with the MMIO seam redirected
 * to a mock register file. It is UNIT VERIFIED, not a DS run; all runtime
 * behavior remains HARDWARE UNVERIFIED.
 */

#include <stdint.h>

static uint32_t mock_read(uint32_t addr, uint32_t width);
static void mock_write(uint32_t addr, uint32_t width, uint32_t value);

#define TWLHD_SHIM_HW_READ(addr, width) mock_read((addr), (width))
#define TWLHD_SHIM_HW_WRITE(addr, width, value) \
    mock_write((addr), (width), (value))

#include "twlhd_gx_shim.hpp"

#include <nds/arm9/videoGL.h>

#include "tests_host.h"

#define MOCK_SLOTS 128u

typedef struct {
    uint32_t addr;
    uint32_t width;
    uint32_t value;
} MockReg;

static MockReg g_regs[MOCK_SLOTS];
static uint32_t g_reg_count;
static uint32_t g_hw_write_count;

static MockReg *find_reg(uint32_t addr) {
    uint32_t i;

    for (i = 0; i < g_reg_count; i++) {
        if (g_regs[i].addr == addr) return &g_regs[i];
    }
    return 0;
}

static uint32_t mock_read(uint32_t addr, uint32_t width) {
    MockReg *reg = find_reg(addr);
    uint32_t value = reg ? reg->value : 0u;

    if (width == 1u) return value & 0xFFu;
    if (width == 2u) return value & 0xFFFFu;
    return value;
}

static void mock_write(uint32_t addr, uint32_t width, uint32_t value) {
    MockReg *reg = find_reg(addr);

    g_hw_write_count++;
    if (!reg) {
        if (g_reg_count >= MOCK_SLOTS) return;
        reg = &g_regs[g_reg_count++];
        reg->addr = addr;
        reg->width = width;
    }
    reg->value = value;
}

static void mock_reset(void) {
    g_reg_count = 0;
    g_hw_write_count = 0;
}

/* libnds links these from the ARM9 trig LUT; the hosts need stubs. */
extern "C" s16 sinLerp(s16 angle) {
    return (s16)(angle / 16);
}

extern "C" s16 cosLerp(s16 angle) {
    return (s16)(4096 - (angle / 16));
}

/* libnds defines this in videoGL.c. */
u32 glCurClearColor = 0;

/* The shim routes into the process-wide recorder, so the test uses it. */
static TwlhdGxRecorder &g_rec = *twlhd_gx_rec_global();
static uint8_t g_out[4096];

static void check_entry(uint32_t index, uint32_t cmd_id) {
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec) > index, 1);
    CHECK_EQ_INT(g_rec.gxcm[index].cmd_word, cmd_id);
}

static void test_inline_helpers_record(void) {
    TwlhdV2View view;
    size_t size = 0;
    uint32_t i;
    static const uint32_t expected_ids[] = {
        0x10u, 0x15u, 0x11u, 0x40u, 0x20u, 0x21u, 0x23u, 0x23u,
        0x41u, 0x29u, 0x60u, 0x50u,
    };

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushMatrix();
    glClearColor(0, 0, 0, 31); /* RSTA: 0x04000350 */
    glBegin(GL_QUADS);
    glColor3b(255, 0, 0);
    glNormal(0x1FF00000u);
    glVertex3v16((v16)0x1000, (v16)0x2000, (v16)0x3000);
    glEnd();
    glPolyFmt(POLY_ALPHA(31));
    glViewport(0, 0, 255, 191);
    glClearDepth(GL_MAX_DEPTH); /* RSTA: 0x04000354 */
    glClearPolyID(63);          /* RSTA: 0x04000350 */
    glEnable(GL_ANTIALIAS);     /* RSTA: 0x04000060 (read-modify-write) */
    GFX_FOG_TABLE[3] = 0x1Fu;   /* RSTA: 0x04000363 (byte) */
    GFX_TOON_TABLE[2] = 0x1234u;/* RSTA: 0x04000384 (halfword) */
    GFX_EDGE_TABLE[1] = 0x7FFFu;/* RSTA: 0x04000332 (halfword) */
    glFlush(0);

    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 12u);
    for (i = 0; i < 12u; i++) {
        check_entry(i, expected_ids[i]);
    }
    CHECK_EQ_INT(g_rec.gxcm[6].value, 0x20001000u);
    CHECK_EQ_INT(g_rec.gxcm[7].value, 0x3000u);
    CHECK_EQ_INT(g_rec.gxcm[8].cmd_word, 0x41u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec), TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(twlhd_gx_rec_is_armed(&g_rec), 0);

    CHECK_EQ_INT(twlhd_gx_rec_rsta_count(&g_rec), 7u);
    CHECK_EQ_INT(g_rec.rsta[0].addr, 0x04000350u);
    CHECK_EQ_INT(g_rec.rsta[1].addr, 0x04000354u);
    CHECK_EQ_INT(g_rec.rsta[2].addr, 0x04000350u);
    CHECK_EQ_INT(g_rec.rsta[3].addr, 0x04000060u);
    CHECK_EQ_INT(g_rec.rsta[4].addr, 0x04000363u);
    CHECK_EQ_INT(g_rec.rsta[5].addr, 0x04000384u);
    CHECK_EQ_INT(g_rec.rsta[6].addr, 0x04000332u);

    /* The proxy also performs the real (mocked) hardware write. */
    CHECK(g_hw_write_count >= 12u + 7u);
    CHECK_EQ_INT((uint32_t)GFX_CONTROL, (uint32_t)GL_ANTIALIAS);
    CHECK_EQ_INT((uint32_t)GFX_FOG_TABLE[3], 0x1Fu);
    CHECK_EQ_INT((uint32_t)GFX_TOON_TABLE[2], 0x1234u);
    CHECK_EQ_INT((uint32_t)GFX_EDGE_TABLE[1], 0x7FFFu);

    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.gxcm_count, 12u);
    CHECK_EQ_INT(view.rsta_count, 7u);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
}

static void test_matrix_helpers_record(void) {
    uint32_t i;

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    glRotateXi(256);
    glRotateYi(256);
    glTranslatef32(0, 0, 0);
    glScalef32(inttof32(1), inttof32(1), inttof32(1));
    glFlush(0);

    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 9u + 9u + 3u + 3u + 1u);
    for (i = 0; i < 9u; i++) CHECK_EQ_INT(g_rec.gxcm[i].cmd_word, 0x1Au);
    for (i = 9u; i < 18u; i++) CHECK_EQ_INT(g_rec.gxcm[i].cmd_word, 0x1Au);
    for (i = 18u; i < 21u; i++) CHECK_EQ_INT(g_rec.gxcm[i].cmd_word, 0x1Cu);
    for (i = 21u; i < 24u; i++) CHECK_EQ_INT(g_rec.gxcm[i].cmd_word, 0x1Bu);
    CHECK_EQ_INT(g_rec.gxcm[24].cmd_word, 0x50u);
    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec), TWLHD_V2_FLAG_FRAME_COMPLETE);

    /* glRotateXi writes inttof32(1) first, then 0, then 0. */
    CHECK_EQ_INT(g_rec.gxcm[0].value, (uint32_t)inttof32(1));
    CHECK_EQ_INT(g_rec.gxcm[1].value, 0u);
}

static void test_disarmed_helpers_are_ignored(void) {
    mock_reset();
    twlhd_gx_rec_init(&g_rec);

    glBegin(GL_TRIANGLES);
    glEnd();
    CHECK_EQ_INT(twlhd_gx_rec_entry_count(&g_rec), 0u);
    CHECK_EQ_INT(twlhd_gx_rec_ignored_writes(&g_rec), 2u);
    CHECK_EQ_INT(g_hw_write_count, 2u); /* hardware still written */
}

int main(void) {
    test_inline_helpers_record();
    test_report("shim/inline-helpers");
    test_matrix_helpers_record();
    test_report("shim/matrix-helpers");
    test_disarmed_helpers_are_ignored();
    test_report("shim/disarmed");
    return test_exit_code();
}
