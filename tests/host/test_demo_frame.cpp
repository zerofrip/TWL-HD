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
#include "twlhd_gx_interp.h"
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

/* The expected VIEWPORT word is a literal chosen per test binary. A variant
 * build passes TWLHD_TEST_EXPECT_VP_WORD together with the four
 * TWLHD_DEMO_VP_* overrides; fail the build if those overrides are missing or
 * changed, so the literal cannot silently stop matching the frame body. */
#ifdef TWLHD_TEST_EXPECT_VP_WORD
#if TWLHD_DEMO_VP_X1 != 32 || TWLHD_DEMO_VP_Y1 != 96 || \
    TWLHD_DEMO_VP_X2 != 159 || TWLHD_DEMO_VP_Y2 != 191
#error "TWLHD_TEST_EXPECT_VP_WORD needs TWLHD_DEMO_VP_* = 32,96,159,191"
#endif
#endif

#ifndef TWLHD_TEST_EXPECT_VP_WORD
#define TWLHD_TEST_EXPECT_VP_WORD 0xBFFF0000u
#endif

/* The expected cube count is a literal chosen per test binary. A variant build
 * passes TWLHD_TEST_EXPECT_CUBES together with the matching
 * -DTWLHD_DEMO_CUBES; fail the build if the define is missing or changed, so
 * the literal cannot silently stop matching the frame body. */
#ifdef TWLHD_TEST_EXPECT_CUBES
#if TWLHD_DEMO_CUBES != TWLHD_TEST_EXPECT_CUBES
#error "TWLHD_TEST_EXPECT_CUBES needs -DTWLHD_DEMO_CUBES=<same value>"
#endif
#endif

/* The expected primitive scene is a literal chosen per test binary. A variant
 * build passes TWLHD_TEST_EXPECT_SCENE together with the matching
 * -DTWLHD_DEMO_SCENE; fail the build if the define is missing or changed, so
 * the literals cannot silently stop matching the frame body. */
#ifdef TWLHD_TEST_EXPECT_SCENE
#if TWLHD_DEMO_SCENE != TWLHD_TEST_EXPECT_SCENE
#error "TWLHD_TEST_EXPECT_SCENE needs -DTWLHD_DEMO_SCENE=<same value>"
#endif
#endif

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
#if !defined(TWLHD_TEST_EXPECT_CUBES) && !defined(TWLHD_TEST_EXPECT_SCENE)
    CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_VTX_16), 24u * 2u);
#endif
#ifndef TWLHD_TEST_EXPECT_SCENE
    CHECK(has_cmd_word(TWLHD_V2_CMD_NORMAL));
#endif
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

/* The recorded VIEWPORT word is x1 | y1 << 8 | x2 << 16 | y2 << 24, packed by
 * libnds glViewport. The expectation is the literal TWLHD_TEST_EXPECT_VP_WORD:
 * the default build takes 0xBFFF0000u and the variant-viewport build passes
 * -DTWLHD_TEST_EXPECT_VP_WORD=0xBF9F6020u, so the check is falsifiable. */
static void test_viewport_word(void) {
    uint32_t vp;
    uint32_t expected;

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);
    twlhd_demo_draw_frame(256u);

    vp = find_first_cmd(g_rec.gxcm, g_rec.gxcm_count, TWLHD_V2_CMD_VIEWPORT);
    CHECK(vp != UINT32_MAX);

    expected = TWLHD_TEST_EXPECT_VP_WORD;

    if (vp != UINT32_MAX) {
        CHECK_EQ_INT(g_rec.gxcm[vp].value, expected);
    }
}

#if TWLHD_DEMO_CUBES == 170 || TWLHD_DEMO_CUBES == 2
/* Literal expectations for the multi-cube build: one cube records 24 vertices
 * as 48 VTX_16 writes (glVertex3v16 writes two 32-bit words per vertex), and
 * interprets to 6 quads -> 12 triangles -> 36 vertices. */
static TwlhdGxVertex g_interp_out[6144u];

/* Hand-derived literals for the multi-cube grid. Nothing here calls the
 * production formulas: inttof32(n) is exact and mulf32(a, b) == (a*b)>>12 is
 * exact, so
 *   half_spacing = floattof32(0.42) >> 1 == ((s32)(0.42 * 4096)) >> 1 == 860
 *   x = (2*col - 16) * 860, y = (2*row - (CUBE_ROWS-1)) * 860
 *   cube scale   = floattof32(0.12) == (s32)(0.12 * 4096) == 491
 * The truncation is the point: 0.42 -> 1720 (not 1721), 0.12 -> 491 (not
 * 492). A cube spans 2 * 491 == 982 in x and y, below the 1720 grid spacing,
 * so neighbours cannot overlap. */
#define CUBE_COLS 17u
#if TWLHD_DEMO_CUBES == 170
#define CUBE_ROWS 10u
#else
#define CUBE_ROWS 1u
#endif
#define CUBE_PER_ROW (TWLHD_DEMO_CUBES / CUBE_ROWS)
#define CUBE_DISTINCT_X (TWLHD_DEMO_CUBES < 17 ? TWLHD_DEMO_CUBES : 17)

static const int32_t g_cube_x[CUBE_COLS] = {
    -13760, -12040, -10320, -8600, -6880, -5160, -3440, -1720, 0,
    1720, 3440, 5160, 6880, 8600, 10320, 12040, 13760
};

static const int32_t g_cube_y[CUBE_ROWS] = {
#if TWLHD_DEMO_CUBES == 170
    -7740, -6020, -4300, -2580, -860, 860, 2580, 4300, 6020, 7740
#else
    0
#endif
};

#if TWLHD_DEMO_CUBES == 170
static const int32_t g_cube_xy[TWLHD_DEMO_CUBES][2] = {
    /* row 0: y = -7740 */
    {-13760, -7740}, {-12040, -7740}, {-10320, -7740}, {-8600, -7740},
    {-6880, -7740}, {-5160, -7740}, {-3440, -7740}, {-1720, -7740},
    {0, -7740}, {1720, -7740}, {3440, -7740}, {5160, -7740},
    {6880, -7740}, {8600, -7740}, {10320, -7740}, {12040, -7740},
    {13760, -7740},
    /* row 1: y = -6020 */
    {-13760, -6020}, {-12040, -6020}, {-10320, -6020}, {-8600, -6020},
    {-6880, -6020}, {-5160, -6020}, {-3440, -6020}, {-1720, -6020},
    {0, -6020}, {1720, -6020}, {3440, -6020}, {5160, -6020},
    {6880, -6020}, {8600, -6020}, {10320, -6020}, {12040, -6020},
    {13760, -6020},
    /* row 2: y = -4300 */
    {-13760, -4300}, {-12040, -4300}, {-10320, -4300}, {-8600, -4300},
    {-6880, -4300}, {-5160, -4300}, {-3440, -4300}, {-1720, -4300},
    {0, -4300}, {1720, -4300}, {3440, -4300}, {5160, -4300},
    {6880, -4300}, {8600, -4300}, {10320, -4300}, {12040, -4300},
    {13760, -4300},
    /* row 3: y = -2580 */
    {-13760, -2580}, {-12040, -2580}, {-10320, -2580}, {-8600, -2580},
    {-6880, -2580}, {-5160, -2580}, {-3440, -2580}, {-1720, -2580},
    {0, -2580}, {1720, -2580}, {3440, -2580}, {5160, -2580},
    {6880, -2580}, {8600, -2580}, {10320, -2580}, {12040, -2580},
    {13760, -2580},
    /* row 4: y = -860 */
    {-13760, -860}, {-12040, -860}, {-10320, -860}, {-8600, -860},
    {-6880, -860}, {-5160, -860}, {-3440, -860}, {-1720, -860},
    {0, -860}, {1720, -860}, {3440, -860}, {5160, -860},
    {6880, -860}, {8600, -860}, {10320, -860}, {12040, -860},
    {13760, -860},
    /* row 5: y = 860 */
    {-13760, 860}, {-12040, 860}, {-10320, 860}, {-8600, 860},
    {-6880, 860}, {-5160, 860}, {-3440, 860}, {-1720, 860},
    {0, 860}, {1720, 860}, {3440, 860}, {5160, 860},
    {6880, 860}, {8600, 860}, {10320, 860}, {12040, 860},
    {13760, 860},
    /* row 6: y = 2580 */
    {-13760, 2580}, {-12040, 2580}, {-10320, 2580}, {-8600, 2580},
    {-6880, 2580}, {-5160, 2580}, {-3440, 2580}, {-1720, 2580},
    {0, 2580}, {1720, 2580}, {3440, 2580}, {5160, 2580},
    {6880, 2580}, {8600, 2580}, {10320, 2580}, {12040, 2580},
    {13760, 2580},
    /* row 7: y = 4300 */
    {-13760, 4300}, {-12040, 4300}, {-10320, 4300}, {-8600, 4300},
    {-6880, 4300}, {-5160, 4300}, {-3440, 4300}, {-1720, 4300},
    {0, 4300}, {1720, 4300}, {3440, 4300}, {5160, 4300},
    {6880, 4300}, {8600, 4300}, {10320, 4300}, {12040, 4300},
    {13760, 4300},
    /* row 8: y = 6020 */
    {-13760, 6020}, {-12040, 6020}, {-10320, 6020}, {-8600, 6020},
    {-6880, 6020}, {-5160, 6020}, {-3440, 6020}, {-1720, 6020},
    {0, 6020}, {1720, 6020}, {3440, 6020}, {5160, 6020},
    {6880, 6020}, {8600, 6020}, {10320, 6020}, {12040, 6020},
    {13760, 6020},
    /* row 9: y = 7740 */
    {-13760, 7740}, {-12040, 7740}, {-10320, 7740}, {-8600, 7740},
    {-6880, 7740}, {-5160, 7740}, {-3440, 7740}, {-1720, 7740},
    {0, 7740}, {1720, 7740}, {3440, 7740}, {5160, 7740},
    {6880, 7740}, {8600, 7740}, {10320, 7740}, {12040, 7740},
    {13760, 7740}
};
#else
static const int32_t g_cube_xy[TWLHD_DEMO_CUBES][2] = {
    {-13760, 0},
    {-12040, 0}
};
#endif

/* Every 32-bit port write is one gxcm entry, so a call to glTranslatef32 or
 * glScalef32 records exactly three consecutive MTX_TRANS / MTX_SCALE entries.
 * Collect each such run; returns the number of runs found. */
static uint32_t collect_xyz_runs(uint32_t cmd, int32_t out[][3],
                                 uint32_t max_groups) {
    uint32_t i = 0u;
    uint32_t groups = 0u;

    while (i < g_rec.gxcm_count) {
        if (g_rec.gxcm[i].cmd_word != cmd) {
            i++;
            continue;
        }
        if (i + 2u >= g_rec.gxcm_count ||
            g_rec.gxcm[i + 1u].cmd_word != cmd ||
            g_rec.gxcm[i + 2u].cmd_word != cmd) {
            i++;
            continue;
        }
        if (groups < max_groups) {
            out[groups][0] = (int32_t)g_rec.gxcm[i].value;
            out[groups][1] = (int32_t)g_rec.gxcm[i + 1u].value;
            out[groups][2] = (int32_t)g_rec.gxcm[i + 2u].value;
        }
        groups++;
        i += 3u;
    }
    return groups;
}

static void test_cubes_variant(void) {
    TwlhdV2View view;
    TwlhdGxViewport vp;
    TwlhdGxStats stats;
    size_t size = 0;
    uint32_t out_vertices = 0;
    uint32_t expected_vtx16;
    uint32_t expected_in;
    uint32_t expected_out;

#if TWLHD_DEMO_CUBES == 170
    expected_vtx16 = 8160u;
    expected_in = 4080u;
    expected_out = 6120u;
#else
    expected_vtx16 = 96u;
    expected_in = 48u;
    expected_out = 72u;
#endif

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_demo_draw_frame(256u);

    CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec),
                 TWLHD_V2_FLAG_FRAME_COMPLETE);
    CHECK(!twlhd_gx_rec_overflowed(&g_rec));
    CHECK(g_rec.gxcm_count < 16384u);
    CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_VTX_16), expected_vtx16);

    CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out, &size),
                 TWLHD_V2_OK);
    CHECK(size < 1048576u);
    CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);

    CHECK_EQ_INT(twlhd_gx_interpret(&view, g_interp_out, 6144u, &out_vertices,
                                    &vp, &stats),
                 TWLHD_GX_OK);
    CHECK_EQ_INT(out_vertices, expected_out);
    CHECK_EQ_INT(stats.vertices_in, expected_in);
    CHECK_EQ_INT(stats.stack_errors, 0u);
    CHECK_EQ_INT(stats.capacity_dropped, 0u);

    /* ------------------------------------------------------------------
     * Literal-expectation layout checks over the recorded stream.
     *
     * Per cube the multi-cube path records one PUSH/POP pair, one MTX_TRANS
     * run (translate), one MTX_SCALE run and one BEGIN/END pair. The path
     * itself records one preamble translate (0,0,0) and scale (1,1,1) before
     * the loop (demo_frame.cpp:155-156), so there are CUBES+1 runs of each.
     * The preamble records no shared rotation: only the per-cube
     * rotateX/rotateY pair rotates, so the 17x10 grid is not rotated as one
     * slab around the camera-space origin.
     * ------------------------------------------------------------------ */
    {
        int32_t trans[256][3];
        int32_t scal[256][3];
        int32_t xs[TWLHD_DEMO_CUBES];
        int32_t ys[TWLHD_DEMO_CUBES];
        uint32_t trans_groups;
        uint32_t scale_groups;
        uint32_t i;
        uint32_t k;

        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_PUSH),
                     TWLHD_DEMO_CUBES);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_POP),
                     TWLHD_DEMO_CUBES);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_TRANS),
                     3u * (TWLHD_DEMO_CUBES + 1u));
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_SCALE),
                     3u * (TWLHD_DEMO_CUBES + 1u));

        trans_groups = collect_xyz_runs(TWLHD_V2_CMD_MTX_TRANS, trans, 256u);
        scale_groups = collect_xyz_runs(TWLHD_V2_CMD_MTX_SCALE, scal, 256u);
        CHECK_EQ_INT(trans_groups, TWLHD_DEMO_CUBES + 1u);
        CHECK_EQ_INT(scale_groups, TWLHD_DEMO_CUBES + 1u);

        if (trans_groups == TWLHD_DEMO_CUBES + 1u) {
            /* Preamble group 0 is the (0,0,0) translate. */
            CHECK_EQ_INT(trans[0][0], 0);
            CHECK_EQ_INT(trans[0][1], 0);
            CHECK_EQ_INT(trans[0][2], 0);

            for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
                CHECK_EQ_INT(trans[i + 1u][0], g_cube_xy[i][0]);
                CHECK_EQ_INT(trans[i + 1u][1], g_cube_xy[i][1]);
                CHECK_EQ_INT(trans[i + 1u][2], 0);
                xs[i] = trans[i + 1u][0];
                ys[i] = trans[i + 1u][1];
            }

            /* Grid structure: every literal x/y appears, each row holds
             * exactly CUBE_PER_ROW cubes and no (x,y) pair repeats. Under
             * `i % 16` the last column collapses onto the first; under
             * `i / 16` the row set gains an 11th, out-of-table value. */
            {
                uint8_t seen_x[CUBE_COLS] = {0};
                uint32_t per_row[CUBE_ROWS] = {0};
                uint32_t distinct_x = 0u;

                for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
                    uint32_t col = CUBE_COLS;
                    uint32_t row = CUBE_ROWS;

                    for (k = 0u; k < CUBE_COLS; k++) {
                        if (xs[i] == g_cube_x[k]) {
                            col = k;
                            break;
                        }
                    }
                    for (k = 0u; k < CUBE_ROWS; k++) {
                        if (ys[i] == g_cube_y[k]) {
                            row = k;
                            break;
                        }
                    }
                    CHECK(col < CUBE_COLS);
                    CHECK(row < CUBE_ROWS);
                    if (col < CUBE_COLS && !seen_x[col]) {
                        seen_x[col] = 1u;
                        distinct_x++;
                    }
                    if (row < CUBE_ROWS) per_row[row]++;
                }

                CHECK_EQ_INT(distinct_x, CUBE_DISTINCT_X);
                for (k = 0u; k < CUBE_ROWS; k++) {
                    CHECK_EQ_INT(per_row[k], CUBE_PER_ROW);
                }

                for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
                    for (k = i + 1u; k < TWLHD_DEMO_CUBES; k++) {
                        CHECK(!(xs[i] == xs[k] && ys[i] == ys[k]));
                    }
                }

                /* Neighbours are one spacing apart: +1720 along a row and
                 * +1720 down a column, with no cross-axis drift. */
                for (i = 0u; i + 1u < TWLHD_DEMO_CUBES; i++) {
                    if ((i % CUBE_COLS) + 1u == CUBE_COLS) continue;
                    CHECK_EQ_INT(xs[i + 1u] - xs[i], 1720);
                    CHECK_EQ_INT(ys[i + 1u] - ys[i], 0);
                }
                for (i = 0u; i + CUBE_COLS < TWLHD_DEMO_CUBES; i++) {
                    CHECK_EQ_INT(ys[i + CUBE_COLS] - ys[i], 1720);
                    CHECK_EQ_INT(xs[i + CUBE_COLS] - xs[i], 0);
                }
            }
        }

        if (scale_groups == TWLHD_DEMO_CUBES + 1u) {
            CHECK_EQ_INT(scal[0][0], 4096);
            CHECK_EQ_INT(scal[0][1], 4096);
            CHECK_EQ_INT(scal[0][2], 4096);
            for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
                CHECK_EQ_INT(scal[i + 1u][0], 491);
                CHECK_EQ_INT(scal[i + 1u][1], 491);
                CHECK_EQ_INT(scal[i + 1u][2], 491);
            }
        }
    }

    /* ------------------------------------------------------------------
     * Interpreted-space checks.
     *
     * The host seam stubs normalizef32() to a no-op and crossf32() without
     * its 4.12 shift, so the stubbed gluLookAtf32 y axis overflows to zero
     * and every interpreted clip[1] is 0. x survives, so centroids/extents
     * are compared on raw clip[0] (a monotone affine image of world x); row
     * separation is covered by the translation-group checks above.
     * ------------------------------------------------------------------ */
    if (out_vertices == 36u * TWLHD_DEMO_CUBES) {
        double cx[TWLHD_DEMO_CUBES];
        double bx0[TWLHD_DEMO_CUBES];
        double bx1[TWLHD_DEMO_CUBES];
        uint32_t distinct = 0u;
        uint32_t i;
        uint32_t k;

        for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
            double sum = 0.0;
            double lo = 0.0;
            double hi = 0.0;

            for (k = 0u; k < 36u; k++) {
                double x = g_interp_out[36u * i + k].clip[0];

                sum += x;
                if (k == 0u || x < lo) lo = x;
                if (k == 0u || x > hi) hi = x;
            }
            cx[i] = sum / 36.0;
            bx0[i] = lo;
            bx1[i] = hi;
        }

        /* One distinct x centroid per grid column (17 for c170, 2 for c2).
         * `i % 16` collapses two columns and leaves one fewer. */
        for (i = 0u; i < TWLHD_DEMO_CUBES; i++) {
            uint32_t duplicate = 0u;

            for (k = 0u; k < i; k++) {
                if (cx[i] - cx[k] < 0.001 && cx[k] - cx[i] < 0.001) {
                    duplicate = 1u;
                    break;
                }
            }
            if (!duplicate) distinct++;
        }
        CHECK_EQ_INT(distinct, CUBE_DISTINCT_X);

        /* Cubes in the same row must not overlap on the interpreted x axis.
         * With floattof32(0.02) the 982-wide cubes overlap their neighbour. */
        for (i = 0u; i + 1u < TWLHD_DEMO_CUBES; i++) {
            if ((i % CUBE_COLS) + 1u == CUBE_COLS) continue;
            CHECK(bx1[i] < bx0[i + 1u]);
            CHECK(cx[i + 1u] > cx[i]);
        }

        /* Same column, next row: same x extent (y is not observable here). */
        for (i = 0u; i + CUBE_COLS < TWLHD_DEMO_CUBES; i++) {
            CHECK_NEAR(cx[i + CUBE_COLS], cx[i], 0.001);
            CHECK_NEAR(bx0[i + CUBE_COLS], bx0[i], 0.001);
            CHECK_NEAR(bx1[i + CUBE_COLS], bx1[i], 0.001);
        }
    }
}

/* --------------------------------------------------------------------------
 * Angle-sensitive capture checks.
 *
 * The per-cube block is the only place that rotates: a shared
 * glRotateXi/glRotateYi pair after gluLookAtf32 would rotate the whole grid as
 * one slab around the camera-space origin and emit 18 extra MTX_MULT_3x3
 * entries before the first MTX_PUSH. Each glRotateXi/glRotateYi records exactly
 * nine 0x1A words, so the fixed stream holds 18 * CUBES of them, all after the
 * first PUSH. The camera contributes 12 MTX_MULT_4x3 words (gluLookAtf32) and
 * no MTX_MULT_3x3.
 *
 * TWLHD_TEST_FRUSTUM_MARGIN is measured, not derived: with the host math stubs
 * the lookAt side row is unnormalised, so the largest observed |clip0|/|clip3|
 * is ~2753.7 at angles 0/7680/30720 for both c170 and c2 (|clip1| is 0). A
 * literal above that locks the harness without flaking. Examples of the mutant
 * (shared pair re-added) die on the entry-position/count checks, not here: the
 * linear host trig only contracts, so its geometry stays bounded.
 * -------------------------------------------------------------------------- */
#define TWLHD_TEST_FRUSTUM_MARGIN 3072.0

static void test_cubes_angle_sensitive(void) {
    static const unsigned angles[] = {0u, 7680u, 30720u};
    TwlhdV2View view;
    TwlhdGxViewport vp;
    TwlhdGxStats stats;
    size_t size = 0;
    uint32_t out_vertices = 0;
    uint32_t a;

    for (a = 0u; a < sizeof angles / sizeof angles[0]; a++) {
        uint32_t first_push;
        uint32_t first_rot;
        uint32_t i;

        mock_reset();
        twlhd_gx_rec_init(&g_rec);
        twlhd_gx_rec_arm(&g_rec);

        twlhd_demo_draw_frame(angles[a]);

        first_push = find_first_cmd(g_rec.gxcm, g_rec.gxcm_count,
                                    TWLHD_V2_CMD_MTX_PUSH);
        first_rot = find_first_cmd(g_rec.gxcm, g_rec.gxcm_count,
                                   TWLHD_V2_CMD_MTX_MULT_3x3);
        CHECK(first_push != UINT32_MAX);
        CHECK(first_rot != UINT32_MAX);
        CHECK(first_rot > first_push);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_MULT_3x3),
                     18u * TWLHD_DEMO_CUBES);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_MULT_4x3), 12u);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_MTX_PUSH),
                     TWLHD_DEMO_CUBES);

        CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out,
                                            &size),
                     TWLHD_V2_OK);
        CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
        CHECK_EQ_INT(twlhd_gx_interpret(&view, g_interp_out, 6144u,
                                        &out_vertices, &vp, &stats),
                     TWLHD_GX_OK);
        CHECK_EQ_INT(out_vertices, 36u * TWLHD_DEMO_CUBES);
        CHECK_EQ_INT(stats.stack_errors, 0u);
        CHECK_EQ_INT(stats.capacity_dropped, 0u);

        if (out_vertices == 36u * TWLHD_DEMO_CUBES) {
            for (i = 0u; i < out_vertices; i++) {
                double w = fabs((double)g_interp_out[i].clip[3]);

                CHECK(fabs((double)g_interp_out[i].clip[0]) <=
                      w * TWLHD_TEST_FRUSTUM_MARGIN);
                CHECK(fabs((double)g_interp_out[i].clip[1]) <=
                      w * TWLHD_TEST_FRUSTUM_MARGIN);
            }
        }
    }
}
#endif

#ifdef TWLHD_TEST_EXPECT_SCENE
/* Same measured host-stub bound as the cube path (see the comment at the
 * TWLHD_TEST_FRUSTUM_MARGIN definition above); the scene vertices are inside
 * the unit cube, so the same literal bounds all of them. */
#define TWLHD_TEST_FRUSTUM_MARGIN 3072.0

/* Expectations for the primitive-scene builds. VTX_16 is two 32-bit writes per
 * vertex; the output index literals are written by hand from the interpreter's
 * documented assembly (common/twlhd_gx_interp.c:365-438), not derived from its
 * macros. Each input vertex carries red == i + 1, so the interpreted red
 * channel recovers the input index. */
#if TWLHD_DEMO_SCENE == 1
#define SCENE_BEGIN_TYPE 0u /* GL_TRIANGLES */
#define SCENE_IN_VERTICES 24u
#define SCENE_OUT_VERTICES 24u
#define SCENE_QUADS_SPLIT 0u
static const uint8_t g_scene_index[SCENE_OUT_VERTICES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23
};
static const int16_t g_scene_xy[SCENE_IN_VERTICES][2] = {
    {-2243, -1424}, {-1443, -1424}, {-1843,  -624},
    {-1014, -1424}, { -214, -1424}, { -614,  -624},
    {  214, -1424}, { 1014, -1424}, {  614,  -624},
    { 1443, -1424}, { 2243, -1424}, { 1843,  -624},
    {-2243,   624}, {-1443,   624}, {-1843,  1424},
    {-1014,   624}, { -214,   624}, { -614,  1424},
    {  214,   624}, { 1014,   624}, {  614,  1424},
    { 1443,   624}, { 2243,   624}, { 1843,  1424},
};
#elif TWLHD_DEMO_SCENE == 2
#define SCENE_BEGIN_TYPE 2u /* GL_TRIANGLE_STRIP */
#define SCENE_IN_VERTICES 12u
#define SCENE_OUT_VERTICES 30u
#define SCENE_QUADS_SPLIT 0u
static const uint8_t g_scene_index[SCENE_OUT_VERTICES] = {
    0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5, 4, 5, 6,
    6, 5, 7, 6, 7, 8, 8, 7, 9, 8, 9, 10, 10, 9, 11
};
static const int16_t g_scene_xy[SCENE_IN_VERTICES][2] = {
    {-2560, -1024}, {-2560, 1024}, {-1536, -1024}, {-1536, 1024},
    { -512, -1024}, { -512, 1024}, {  512, -1024}, {  512, 1024},
    { 1536, -1024}, { 1536, 1024}, { 2560, -1024}, { 2560, 1024},
};
#else
#define SCENE_BEGIN_TYPE 3u /* GL_QUAD_STRIP */
#define SCENE_IN_VERTICES 12u
#define SCENE_OUT_VERTICES 30u
#define SCENE_QUADS_SPLIT 5u
static const uint8_t g_scene_index[SCENE_OUT_VERTICES] = {
    0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4, 4, 5, 7,
    4, 7, 6, 6, 7, 9, 6, 9, 8, 8, 9, 11, 8, 11, 10
};
static const int16_t g_scene_xy[SCENE_IN_VERTICES][2] = {
    {-2560, -1024}, {-2560, 1024}, {-1536, -1024}, {-1536, 1024},
    { -512, -1024}, { -512, 1024}, {  512, -1024}, {  512, 1024},
    { 1536, -1024}, { 1536, 1024}, { 2560, -1024}, { 2560, 1024},
};
#endif

static TwlhdGxVertex g_scene_out[64];

static void test_scene_variant(void) {
    static const unsigned angles[] = {0u, 7680u, 30720u};
    TwlhdV2View view;
    TwlhdGxViewport vp;
    TwlhdGxStats stats;
    size_t size = 0;
    uint32_t out_vertices = 0;
    uint32_t a;
    uint32_t i;

    for (a = 0u; a < sizeof angles / sizeof angles[0]; a++) {
        uint32_t begin;
        uint32_t recovered;

        mock_reset();
        twlhd_gx_rec_init(&g_rec);
        twlhd_gx_rec_arm(&g_rec);

        twlhd_demo_draw_frame(angles[a]);

        CHECK_EQ_INT(twlhd_gx_rec_flags(&g_rec),
                     TWLHD_V2_FLAG_FRAME_COMPLETE);
        CHECK(!twlhd_gx_rec_overflowed(&g_rec));

        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_VTX_16),
                     SCENE_IN_VERTICES * 2u);
        CHECK_EQ_INT(count_cmd_word(TWLHD_V2_CMD_COLOR), SCENE_IN_VERTICES);

        begin = find_first_cmd(g_rec.gxcm, g_rec.gxcm_count,
                               TWLHD_V2_CMD_BEGIN_VTXS);
        CHECK(begin != UINT32_MAX);
        if (begin != UINT32_MAX) {
            CHECK_EQ_INT(g_rec.gxcm[begin].value, SCENE_BEGIN_TYPE);
        }

        CHECK(frame_ordering_ok(g_rec.gxcm, g_rec.gxcm_count));

        /* Position assertion (angle 0). The host math seam deliberately stubs
         * normalizef32() to a no-op and tanLerp() to 1.0, so the interpreted
         * clips do not follow the device camera formula: calibration at angle
         * 0 measured clip[3] == 16 (not 4) and x/w off the literal formula.
         * Assert the raw VTX_16 words the frame records instead: two per
         * vertex, the first packing the table's int16 (x, y), the second
         * z == 0. This is exact (no tolerance) and kills any swapped vertex or
         * any coordinate change of one 1/256 unit (16 here), which the clip
         * checks cannot see with the stubbed up axis. */
        if (a == 0u) {
            uint32_t vtx_word = 0u;
            uint32_t e;

            for (e = 0u; e < g_rec.gxcm_count; e++) {
                uint32_t v;
                uint32_t expected;

                if (g_rec.gxcm[e].cmd_word != TWLHD_V2_CMD_VTX_16) continue;
                v = vtx_word / 2u;
                CHECK(v < SCENE_IN_VERTICES);
                if (v >= SCENE_IN_VERTICES) break;
                if ((vtx_word & 1u) == 0u) {
                    expected =
                        ((uint32_t)(uint16_t)g_scene_xy[v][1] << 16) |
                        (uint32_t)(uint16_t)g_scene_xy[v][0];
                } else {
                    expected = 0u;
                }
                CHECK_EQ_INT(g_rec.gxcm[e].value, expected);
                vtx_word++;
            }
            CHECK_EQ_INT(vtx_word, SCENE_IN_VERTICES * 2u);
        }

        CHECK_EQ_INT(twlhd_gx_rec_serialize(&g_rec, g_out, sizeof g_out,
                                            &size),
                     TWLHD_V2_OK);
        CHECK_EQ_INT(twlhd_v2_parse(g_out, size, &view), TWLHD_V2_OK);
        CHECK_EQ_INT(twlhd_gx_interpret(&view, g_scene_out, 64u,
                                        &out_vertices, &vp, &stats),
                     TWLHD_GX_OK);

        CHECK_EQ_INT(out_vertices, SCENE_OUT_VERTICES);
        CHECK_EQ_INT(stats.vertices_in, SCENE_IN_VERTICES);
        CHECK_EQ_INT(stats.triangles_out, SCENE_OUT_VERTICES / 3u);
        CHECK_EQ_INT(stats.quads_split, SCENE_QUADS_SPLIT);
        CHECK_EQ_INT(stats.capacity_dropped, 0u);
        CHECK_EQ_INT(stats.stack_errors, 0u);
        CHECK_EQ_INT(stats.geometry_incomplete, 0u);
        CHECK_EQ_INT(stats.dropped_incomplete, 0u);

        if (out_vertices == SCENE_OUT_VERTICES) {
            for (i = 0u; i < out_vertices; i++) {
                double w = fabs((double)g_scene_out[i].clip[3]);

                recovered = (uint32_t)(int)(g_scene_out[i].rgba[0] * 31.0f +
                                            0.5f) - 1u;
                CHECK_EQ_INT(recovered, g_scene_index[i]);

                CHECK(fabs((double)g_scene_out[i].clip[0]) <=
                      w * TWLHD_TEST_FRUSTUM_MARGIN);
                CHECK(fabs((double)g_scene_out[i].clip[1]) <=
                      w * TWLHD_TEST_FRUSTUM_MARGIN);
            }
        }
    }
}
#endif

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
#ifdef TWLHD_TEST_EXPECT_SCENE
    test_scene_variant();
    test_report("demo-frame/scene");
#endif

#ifdef TWLHD_TEST_EXPECT_CUBES
    test_cubes_variant();
    test_report("demo-frame/cubes");

    test_cubes_angle_sensitive();
    test_report("demo-frame/cubes-angle");
#endif

    test_frame_is_self_contained();
    test_report("demo-frame/self-contained");

    test_viewport_word();
    test_report("demo-frame/viewport-word");

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
