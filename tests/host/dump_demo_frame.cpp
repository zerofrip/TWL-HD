// SPDX-License-Identifier: Apache-2.0
/*
 * Host expected-stream generator for the emulator validation harness.
 *
 * Compiles the real arm9v2 demo frame body against the devkitPro libnds
 * headers with mocked registers and math stubs (the same seam as
 * tests/host/test_demo_frame.cpp), draws one frame at the requested angle and
 * writes the resulting GXCM/RSTA stream as text:
 *
 *   G <idx> 0x<cmd_word 8 hex> 0x<value 8 hex>
 *   R <idx> 0x<addr 8 hex> 0x<value 8 hex> <gx_index> <width>
 *
 * The math commands (0x18/0x19/0x1A) are captured too, but their values differ
 * from the emulator: the host stubs cannot reproduce the DS divider, sqrt or
 * trig tables. tools/emulator/compare_capture.py checks those analytically.
 *
 * Usage: dump_demo_frame <angle> <output.txt>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char **argv) {
    FILE *file;
    unsigned long angle;
    uint32_t i;

    char *endptr;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <angle> <output.txt>\n", argv[0]);
        return 2;
    }

    if (argv[1][0] == '\0') {
        fprintf(stderr, "error: angle must be a non-empty integer 0..65535\n");
        return 2;
    }

    angle = strtoul(argv[1], &endptr, 0);
    if (*endptr != '\0' || angle > 65535) {
        fprintf(stderr, "error: angle must be an integer 0..65535\n");
        return 2;
    }

    mock_reset();
    twlhd_gx_rec_init(&g_rec);
    twlhd_gx_rec_arm(&g_rec);

    twlhd_demo_draw_frame((unsigned)angle);

    file = fopen(argv[2], "w");
    if (!file) {
        fprintf(stderr, "cannot open %s for writing\n", argv[2]);
        return 2;
    }

    for (i = 0; i < g_rec.gxcm_count; i++) {
        fprintf(file, "G %lu 0x%08lX 0x%08lX\n", (unsigned long)i,
                (unsigned long)g_rec.gxcm[i].cmd_word,
                (unsigned long)g_rec.gxcm[i].value);
    }
    for (i = 0; i < g_rec.rsta_count; i++) {
        fprintf(file, "R %lu 0x%08lX 0x%08lX %lu %lu\n", (unsigned long)i,
                (unsigned long)g_rec.rsta[i].addr,
                (unsigned long)g_rec.rsta[i].value,
                (unsigned long)g_rec.rsta[i].gx_index,
                (unsigned long)g_rec.rsta[i].width);
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "error closing %s\n", argv[2]);
        return 2;
    }

    printf("wrote %lu GXCM + %lu RSTA entries to %s\n",
           (unsigned long)g_rec.gxcm_count, (unsigned long)g_rec.rsta_count,
           argv[2]);
    return 0;
}
