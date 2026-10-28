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

/* Ordering rules verified on the captured GX command stream.
 *  - first MTX_MODE has value 0 (projection) and is immediately followed by
 *    MTX_IDENTITY;
 *  - all 16 MTX_MULT_4x4 entries sit between that projection MTX_MODE and the
 *    first MTX_MODE with value 2 (position/modelview);
 *  - VIEWPORT appears before the first BEGIN_VTXS;
 *  - BEGIN_VTXS appears after the first modelview MTX_MODE;
 *  - the final entry is SWAP_BUFFERS.
 */
typedef enum {
    ORD_OK = 0,
    ORD_EMPTY,
    ORD_LAST_NOT_SWAP,
    ORD_NO_PROJECTION_MODE,
    ORD_NO_IDENTITY_AFTER_PROJ,
    ORD_NO_MODELVIEW_MODE,
    ORD_NO_BEGIN,
    ORD_VIEWPORT_AFTER_BEGIN,
    ORD_BEGIN_BEFORE_MODELVIEW,
    ORD_MULT_BEFORE_PROJECTION,
    ORD_MULT_AFTER_MODELVIEW
} OrdResult;

static OrdResult frame_ordering_check(const TwlhdV2CmdEntry *entries,
                                      uint32_t count) {
    uint32_t i;
    uint32_t first_proj = UINT32_MAX;
    uint32_t first_modelview = UINT32_MAX;
    uint32_t first_begin = UINT32_MAX;
    uint32_t first_viewport = UINT32_MAX;

    if (count == 0u) return ORD_EMPTY;
    if (entries[count - 1u].cmd_word != TWLHD_V2_CMD_SWAP_BUFFERS) {
        return ORD_LAST_NOT_SWAP;
    }

    for (i = 0u; i < count; i++) {
        uint32_t cmd = entries[i].cmd_word;
        uint32_t value = entries[i].value;

        if (cmd == TWLHD_V2_CMD_MTX_MODE) {
            if (first_proj == UINT32_MAX && value == 0u) {
                first_proj = i;
            }
            if (first_modelview == UINT32_MAX && value == 2u) {
                first_modelview = i;
            }
        }
        if (cmd == TWLHD_V2_CMD_BEGIN_VTXS && first_begin == UINT32_MAX) {
            first_begin = i;
        }
        if (cmd == TWLHD_V2_CMD_VIEWPORT && first_viewport == UINT32_MAX) {
            first_viewport = i;
        }
    }

    if (first_proj == UINT32_MAX) return ORD_NO_PROJECTION_MODE;
    if (first_proj + 1u >= count ||
        entries[first_proj + 1u].cmd_word != TWLHD_V2_CMD_MTX_IDENTITY) {
        return ORD_NO_IDENTITY_AFTER_PROJ;
    }
    if (first_modelview == UINT32_MAX) return ORD_NO_MODELVIEW_MODE;
    if (first_begin == UINT32_MAX) return ORD_NO_BEGIN;
    if (first_viewport == UINT32_MAX || first_viewport >= first_begin) {
        return ORD_VIEWPORT_AFTER_BEGIN;
    }
    if (first_begin <= first_modelview) return ORD_BEGIN_BEFORE_MODELVIEW;

    for (i = 0u; i < count; i++) {
        if (entries[i].cmd_word == TWLHD_V2_CMD_MTX_MULT_4x4) {
            if (i <= first_proj) return ORD_MULT_BEFORE_PROJECTION;
            if (i >= first_modelview) return ORD_MULT_AFTER_MODELVIEW;
        }
    }

    return ORD_OK;
}

static int frame_ordering_ok(const TwlhdV2CmdEntry *entries, uint32_t count) {
    return frame_ordering_check(entries, count) == ORD_OK;
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

    /* The captured stream follows the expected ordering. */
    CHECK(frame_ordering_ok(g_rec.gxcm, g_rec.gxcm_count));
}

static void copy_entries(TwlhdV2CmdEntry *dst,
                         const TwlhdV2CmdEntry *src,
                         uint32_t count) {
    uint32_t i;

    for (i = 0u; i < count; i++) dst[i] = src[i];
}

static uint32_t find_first_cmd(const TwlhdV2CmdEntry *entries, uint32_t count,
                               uint32_t cmd) {
    uint32_t i;

    for (i = 0u; i < count; i++) {
        if (entries[i].cmd_word == cmd) return i;
    }
    return UINT32_MAX;
}

static uint32_t find_first_cmd_value(const TwlhdV2CmdEntry *entries,
                                     uint32_t count, uint32_t cmd,
                                     uint32_t value) {
    uint32_t i;

    for (i = 0u; i < count; i++) {
        if (entries[i].cmd_word == cmd && entries[i].value == value) return i;
    }
    return UINT32_MAX;
}

static uint32_t count_entries(const TwlhdV2CmdEntry *entries, uint32_t count,
                              uint32_t cmd) {
    uint32_t i;
    uint32_t n = 0u;

    for (i = 0u; i < count; i++) {
        if (entries[i].cmd_word == cmd) n++;
    }
    return n;
}

/* Assert the mutation is exactly the intended one: same length, last entry
 * SWAP_BUFFERS, 16 MTX_MULT_4x4 entries, and the required commands present.
 * Does NOT assert projection/identity here; each test states its own rule. */
static void assert_mutation_well_formed(const TwlhdV2CmdEntry *entries,
                                        uint32_t count) {
    CHECK_EQ_INT(count, g_rec.gxcm_count);
    CHECK_EQ_INT(entries[count - 1u].cmd_word, TWLHD_V2_CMD_SWAP_BUFFERS);
    CHECK_EQ_INT(count_entries(entries, count, TWLHD_V2_CMD_MTX_MULT_4x4), 16u);
    CHECK(find_first_cmd(entries, count, TWLHD_V2_CMD_VIEWPORT) != UINT32_MAX);
    CHECK(find_first_cmd(entries, count, TWLHD_V2_CMD_BEGIN_VTXS) != UINT32_MAX);
    CHECK(find_first_cmd(entries, count, TWLHD_V2_CMD_END_VTXS) != UINT32_MAX);
}

static void assert_projection_identity(const TwlhdV2CmdEntry *entries,
                                       uint32_t count) {
    uint32_t proj = find_first_cmd_value(entries, count, TWLHD_V2_CMD_MTX_MODE,
                                         0u);
    CHECK(proj != UINT32_MAX);
    CHECK(proj + 1u < count);
    CHECK_EQ_INT(entries[proj + 1u].cmd_word, TWLHD_V2_CMD_MTX_IDENTITY);
}

static void test_ordering_rejects_bad_projection_mode(void) {
    TwlhdV2CmdEntry out[TWLHD_GX_REC_MAX_ENTRIES];
    uint32_t idx;

    copy_entries(out, g_rec.gxcm, g_rec.gxcm_count);
    idx = find_first_cmd_value(out, g_rec.gxcm_count, TWLHD_V2_CMD_MTX_MODE,
                               0u);
    CHECK(idx != UINT32_MAX);
    out[idx].value = 2u; /* projection becomes modelview */

    assert_mutation_well_formed(out, g_rec.gxcm_count);
    CHECK_EQ_INT(frame_ordering_check(out, g_rec.gxcm_count),
                 ORD_NO_PROJECTION_MODE);
}

static void test_ordering_rejects_no_identity_after_projection(void) {
    TwlhdV2CmdEntry out[TWLHD_GX_REC_MAX_ENTRIES];
    uint32_t proj;

    copy_entries(out, g_rec.gxcm, g_rec.gxcm_count);
    proj = find_first_cmd_value(out, g_rec.gxcm_count, TWLHD_V2_CMD_MTX_MODE,
                                0u);
    CHECK(proj != UINT32_MAX && proj + 1u < g_rec.gxcm_count);
    CHECK_EQ_INT(out[proj + 1u].cmd_word, TWLHD_V2_CMD_MTX_IDENTITY);
    /* Overwrite the identity entry so the projection mode is no longer
     * followed by MTX_IDENTITY, while keeping the stream length. */
    out[proj + 1u].cmd_word = TWLHD_V2_CMD_MTX_MODE;
    out[proj + 1u].value = 0u;

    assert_mutation_well_formed(out, g_rec.gxcm_count);
    CHECK_EQ_INT(frame_ordering_check(out, g_rec.gxcm_count),
                 ORD_NO_IDENTITY_AFTER_PROJ);
}

static void test_ordering_rejects_viewport_after_begin(void) {
    TwlhdV2CmdEntry src[TWLHD_GX_REC_MAX_ENTRIES];
    TwlhdV2CmdEntry out[TWLHD_GX_REC_MAX_ENTRIES];
    TwlhdV2CmdEntry viewport;
    uint32_t vp, begin;
    uint32_t i, dst;

    copy_entries(src, g_rec.gxcm, g_rec.gxcm_count);
    vp = find_first_cmd(src, g_rec.gxcm_count, TWLHD_V2_CMD_VIEWPORT);
    begin = find_first_cmd(src, g_rec.gxcm_count, TWLHD_V2_CMD_BEGIN_VTXS);
    CHECK(vp != UINT32_MAX && begin != UINT32_MAX && vp < begin);
    viewport = src[vp];

    dst = 0u;
    for (i = 0u; i <= begin; i++) {
        if (i == vp) continue; /* drop the original VIEWPORT position */
        out[dst++] = src[i];
    }
    out[dst++] = viewport; /* VIEWPORT now immediately after BEGIN_VTXS */
    for (i = begin + 1u; i < g_rec.gxcm_count; i++) {
        out[dst++] = src[i];
    }
    CHECK_EQ_INT(dst, g_rec.gxcm_count);

    assert_mutation_well_formed(out, g_rec.gxcm_count);
    assert_projection_identity(out, g_rec.gxcm_count);
    CHECK_EQ_INT(frame_ordering_check(out, g_rec.gxcm_count),
                 ORD_VIEWPORT_AFTER_BEGIN);
}

static void test_ordering_rejects_mult_after_modelview(void) {
    TwlhdV2CmdEntry out[TWLHD_GX_REC_MAX_ENTRIES];
    uint32_t mult[16];
    uint32_t modelview;
    uint32_t src, dst, j;
    uint32_t mult_count = 0u;

    copy_entries(out, g_rec.gxcm, g_rec.gxcm_count);
    modelview = find_first_cmd_value(out, g_rec.gxcm_count,
                                     TWLHD_V2_CMD_MTX_MODE, 2u);
    CHECK(modelview != UINT32_MAX);

    /* Collect the 16 MTX_MULT_4x4 entries that appear before modelview. */
    for (src = 0u; src < modelview && mult_count < 16u; src++) {
        if (out[src].cmd_word == TWLHD_V2_CMD_MTX_MULT_4x4) {
            mult[mult_count++] = src;
        }
    }
    CHECK_EQ_INT(mult_count, 16u);

    /* Build a new stream with the multiply entries moved to just after the
     * modelview MTX_MODE, preserving each entry's original value. */
    dst = 0u;
    for (src = 0u; src <= modelview; src++) {
        if (out[src].cmd_word == TWLHD_V2_CMD_MTX_MULT_4x4) continue;
        out[dst++] = out[src];
    }
    for (j = 0u; j < mult_count; j++) {
        out[dst++] = g_rec.gxcm[mult[j]];
    }
    for (src = modelview + 1u; src < g_rec.gxcm_count; src++) {
        out[dst++] = out[src];
    }
    CHECK_EQ_INT(dst, g_rec.gxcm_count);

    assert_mutation_well_formed(out, g_rec.gxcm_count);
    assert_projection_identity(out, g_rec.gxcm_count);
    CHECK_EQ_INT(frame_ordering_check(out, g_rec.gxcm_count),
                 ORD_MULT_AFTER_MODELVIEW);
}

int main(void) {
    test_frame_is_self_contained();
    test_report("demo-frame/self-contained");

    test_ordering_rejects_bad_projection_mode();
    test_report("demo-frame/ordering-rejects-bad-projection-mode");

    test_ordering_rejects_no_identity_after_projection();
    test_report("demo-frame/ordering-rejects-no-identity-after-proj");

    test_ordering_rejects_viewport_after_begin();
    test_report("demo-frame/ordering-rejects-viewport-after-begin");

    test_ordering_rejects_mult_after_modelview();
    test_report("demo-frame/ordering-rejects-mult-after-modelview");

    return test_exit_code();
}
