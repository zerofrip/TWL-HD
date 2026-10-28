// SPDX-License-Identifier: Apache-2.0
/*
 * Host test for arm9v2/source/demo_frame.cpp.
 *
 * Compiles the real demo frame body against the devkitPro libnds headers with
 * mocked registers and minimal math stubs. Verifies that a captured frame is
 * self-contained: it carries the projection, viewport, rendering state and the
 * cube geometry, and closes with SWAP_BUFFERS.
 */

#include <stdint.h>

#include <nds/ndstypes.h>

/* Skip libnds' hardware-divider math.h and stub the few functions the frame
 * body actually calls. */
#define MATH_ARM9_INCLUDE

static inline s32 mock_mulf32(s32 a, s32 b) {
    return (s32)(((int64_t)a * b) >> 12);
}

static inline s32 mock_divf32(s32 num, s32 den) {
    return den == 0 ? 0 : (s32)(((int64_t)num << 12) / den);
}

static inline void mock_normalizef32(s32 *a) {
    (void)a;
}

static inline void mock_crossf32(s32 *a, s32 *b, s32 *result) {
    result[0] = a[1] * b[2] - a[2] * b[1];
    result[1] = a[2] * b[0] - a[0] * b[2];
    result[2] = a[0] * b[1] - a[1] * b[0];
}

static inline s32 mock_dotf32(s32 *a, s32 *b) {
    return (s32)((((int64_t)a[0] * b[0] +
                  (int64_t)a[1] * b[1] +
                  (int64_t)a[2] * b[2]) >> 12));
}

#define mulf32 mock_mulf32
#define divf32 mock_divf32
#define normalizef32 mock_normalizef32
#define crossf32 mock_crossf32
#define dotf32 mock_dotf32
#define inttof32(n) ((n) * (1 << 12))
#define floattof32(n) ((s32)((n) * (1 << 12)))

static uint32_t mock_read(uint32_t addr, uint32_t width);
static void mock_write(uint32_t addr, uint32_t width, uint32_t value);

#define TWLHD_SHIM_HW_READ(addr, width) mock_read((addr), (width))
#define TWLHD_SHIM_HW_WRITE(addr, width, value) \
    mock_write((addr), (width), (value))

#include "twlhd_gx_shim.hpp"
#include <nds/arm9/videoGL.h>

#include "demo_frame.hpp"
#include "tests_host.h"

#define MOCK_SLOTS 256u

typedef struct {
    uint32_t addr;
    uint32_t width;
    uint32_t value;
} MockReg;

static MockReg g_regs[MOCK_SLOTS];
static uint32_t g_reg_count;

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
}

/* libnds links these from the ARM9 trig LUT; the host needs stubs. */
extern "C" s16 sinLerp(s16 angle) {
    return (s16)(angle / 16);
}

extern "C" s16 cosLerp(s16 angle) {
    return (s16)(4096 - (angle / 16));
}

extern "C" s32 tanLerp(s16 angle) {
    (void)angle;
    return 4096; /* 1.0 in 4.12 fixed point */
}

/* libnds defines this in videoGL.c. */
u32 glCurClearColor = 0;

/* Compile the demo frame body in this translation unit so it uses the mock
 * seam defined above. */
#include "demo_frame.cpp"

static TwlhdGxRecorder &g_rec = *twlhd_gx_rec_global();
static uint8_t g_out[TWLHD_GX_REC_SERIALIZE_MAX];

static int has_cmd_word(uint32_t id) {
    uint32_t i;

    for (i = 0; i < g_rec.gxcm_count; i++) {
        if (g_rec.gxcm[i].cmd_word == id) return 1;
    }
    return 0;
}

static uint32_t count_cmd_word(uint32_t id) {
    uint32_t i;
    uint32_t count = 0;

    for (i = 0; i < g_rec.gxcm_count; i++) {
        if (g_rec.gxcm[i].cmd_word == id) count++;
    }
    return count;
}

static int has_rsta_addr(uint32_t addr) {
    uint32_t i;

    for (i = 0; i < g_rec.rsta_count; i++) {
        if (g_rec.rsta[i].addr == addr) return 1;
    }
    return 0;
}

static void test_frame_is_self_contained(void) {
    TwlhdV2View view;
    size_t size = 0;

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_demo_draw_frame(256u);

    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec),
                 TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK(g_rec.gxcm_count > 0u);
    CHECK_EQ_INT(g_rec.gxcm[g_rec.gxcm_count - 1u].cmd_word,
                 TWLHD_V2_CMD_SWAP_BUFFERS);

    /* Projection setup. */
    CHECK(has_cmd_word(TWLHD_V2_CMD_MTX_MODE));
    CHECK(has_cmd_word(TWLHD_V2_CMD_MTX_IDENTITY));
    CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_MULT_4x4), 16u);

    /* Viewport and polygon state. */
    CHECK(has_cmd_word(TWLHD_V2_CMD_VIEWPORT));
    CHECK(has_cmd_word(TWLHD_V2_CMD_POLYGON_ATTR));

    /* Cube: one BEGIN_VTXS with quad primitive, 24 VTX_16 vertices. */
    CHECK(has_cmd_word(TWLHD_V2_CMD_BEGIN_VTXS));
    CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_VTX_16), 24u * 2u);
    CHECK(has_cmd_word(TWLHD_V2_CMD_NORMAL));
    CHECK(has_cmd_word(TWLHD_V2_CMD_COLOR));

    /* Rendering state recorded as RSTA. */
    CHECK(has_rsta_addr(0x04000350u)); /* clear colour */
    CHECK(has_rsta_addr(0x04000060u)); /* DISP3DCNT (antialias) */

    /* Serializes and parses cleanly. */
    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
    CHECK_EQ_INT(view.flags, TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK_EQ_INT(view.gxcm_count, g_rec.gxcm_count);
    CHECK_EQ_INT(view.rsta_count, g_rec.rsta_count);
}

int main(void) {
    test_frame_is_self_contained();
    test_report("demo-frame/self-contained");
    return test_exit_code();
}
