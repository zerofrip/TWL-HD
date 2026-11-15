// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI for tools/test_tex_capture_golden.py (stage 5C-1).
 *
 * Reads one compact little-endian description from stdin:
 *
 *   u32 max_txdt_bytes, max_pldt_bytes, max_records
 *   u32 gxcm_count
 *   gxcm_count * { u32 cmd_word, u32 value }
 *   u32 mem_size
 *   mem_size bytes of fake source memory
 *   u32 fail_mask   (bit 0 texel, 1 palette, 2 all index, 3 small index)
 *
 * Runs twlhd_tex_capture_plan() + twlhd_tex_capture_apply() and prints the
 * plan textually. Exit code 2 means bad usage or a planner error.
 * Host-only (BUILD/UNIT).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twlhd_tex_capture.h"

#define MAX_GXCM 65536u
#define MAX_MEM 0x80000u

static const uint8_t *g_mem;
static uint32_t g_mem_size;
static uint32_t g_fail;

static int fake_read(void *ctx, uint32_t kind, uint32_t offset, uint32_t bytes,
                     uint8_t *dst) {
    (void)ctx;
    if (kind > 2u) return 1;
    if (kind == TWLHD_TEX_CAP_READ_TEXEL) {
        if (g_fail & 1u) return 1;
    } else if (kind == TWLHD_TEX_CAP_READ_PALETTE) {
        if (g_fail & 2u) return 1;
    } else {
        if (g_fail & 4u) return 1;
        if (dst && bytes == 2u && (g_fail & 8u)) return 1;
    }
    if ((uint64_t)offset + (uint64_t)bytes > (uint64_t)g_mem_size) return 1;
    if (dst) memcpy(dst, g_mem + offset, bytes);
    return 0;
}

static int read_u32(FILE *in, uint32_t *out) {
    uint8_t b[4];

    if (fread(b, 1, 4, in) != 4) return 0;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static void print_hex(const uint8_t *data, uint32_t size) {
    uint32_t i;

    for (i = 0; i < size; i++) printf("%02x", (unsigned)data[i]);
    printf("\n");
}

int main(void) {
    TwlhdTexCapLimits limits;
    TwlhdV2CmdEntry *gxcm = NULL;
    uint8_t *mem = NULL;
    TwlhdTexCapPlan *plan = NULL;
    uint8_t *txdt = NULL;
    uint8_t *pldt = NULL;
    TwlhdV2TexEntry *entries = NULL;
    uint32_t gxcm_count = 0;
    uint32_t mem_size = 0;
    uint32_t fail = 0;
    uint32_t i;
    TwlhdTexCapResult result;
    int bad = 0;

    if (!read_u32(stdin, &limits.max_txdt_bytes) ||
        !read_u32(stdin, &limits.max_pldt_bytes) ||
        !read_u32(stdin, &limits.max_records) ||
        !read_u32(stdin, &gxcm_count)) {
        fprintf(stderr, "short read: header\n");
        return 2;
    }
    if (gxcm_count > MAX_GXCM) {
        fprintf(stderr, "gxcm_count too large: %u\n", (unsigned)gxcm_count);
        return 2;
    }
    gxcm = (TwlhdV2CmdEntry *)calloc(gxcm_count ? gxcm_count : 1u,
                                     sizeof(*gxcm));
    if (!gxcm) return 2;
    for (i = 0; i < gxcm_count; i++) {
        if (!read_u32(stdin, &gxcm[i].cmd_word) ||
            !read_u32(stdin, &gxcm[i].value)) {
            fprintf(stderr, "short read: gxcm %u\n", (unsigned)i);
            bad = 1;
            goto cleanup;
        }
    }
    if (!read_u32(stdin, &mem_size)) {
        fprintf(stderr, "short read: mem_size\n");
        bad = 1;
        goto cleanup;
    }
    if (mem_size > MAX_MEM) {
        fprintf(stderr, "mem_size too large: %u\n", (unsigned)mem_size);
        bad = 1;
        goto cleanup;
    }
    mem = (uint8_t *)calloc(mem_size ? mem_size : 1u, 1);
    if (!mem) {
        bad = 1;
        goto cleanup;
    }
    if (mem_size != 0u && fread(mem, 1, mem_size, stdin) != mem_size) {
        fprintf(stderr, "short read: memory\n");
        bad = 1;
        goto cleanup;
    }
    if (!read_u32(stdin, &fail)) {
        fprintf(stderr, "short read: fail_mask\n");
        bad = 1;
        goto cleanup;
    }

    g_mem = mem;
    g_mem_size = mem_size;
    g_fail = fail;

    plan = (TwlhdTexCapPlan *)calloc(1u, sizeof(*plan));
    if (!plan) {
        bad = 1;
        goto cleanup;
    }
    result = twlhd_tex_capture_plan(&limits, gxcm, gxcm_count, fake_read, NULL,
                                    plan);
    if (result != TWLHD_TEX_CAP_OK) {
        printf("result=%s\n", twlhd_tex_capture_result_string(result));
        bad = 1;
        goto cleanup;
    }

    txdt = (uint8_t *)calloc(plan->stats.bytes_txdt ? plan->stats.bytes_txdt
                                                    : 1u,
                             1);
    pldt = (uint8_t *)calloc(plan->stats.bytes_pldt ? plan->stats.bytes_pldt
                                                    : 1u,
                             1);
    entries = (TwlhdV2TexEntry *)calloc(
        plan->record_count ? plan->record_count : 1u, sizeof(*entries));
    if (!txdt || !pldt || !entries) {
        bad = 1;
        goto cleanup;
    }
    result = twlhd_tex_capture_apply(plan, fake_read, NULL, txdt,
                                     plan->stats.bytes_txdt, pldt,
                                     plan->stats.bytes_pldt, entries,
                                     plan->record_count);
    if (result != TWLHD_TEX_CAP_OK) {
        printf("result=%s\n", twlhd_tex_capture_result_string(result));
        bad = 1;
        goto cleanup;
    }

    printf("result=OK\n");
    printf("limits txdt=%u pldt=%u records=%u\n",
           (unsigned)plan->limits.max_txdt_bytes,
           (unsigned)plan->limits.max_pldt_bytes,
           (unsigned)plan->limits.max_records);
    printf("keys_seen=%u\n", (unsigned)plan->stats.keys_seen);
    printf("keys_kept=%u\n", (unsigned)plan->stats.keys_kept);
    printf("keys_dropped_budget=%u\n",
           (unsigned)plan->stats.keys_dropped_budget);
    printf("keys_dropped_read_error=%u\n",
           (unsigned)plan->stats.keys_dropped_read_error);
    printf("keys_dropped_unsupported=%u\n",
           (unsigned)plan->stats.keys_dropped_unsupported);
    printf("conflicts_same_key=%u\n",
           (unsigned)plan->stats.conflicts_same_key);
    printf("bytes_txdt=%u\n", (unsigned)plan->stats.bytes_txdt);
    printf("bytes_pldt=%u\n", (unsigned)plan->stats.bytes_pldt);
    printf("overflow=%d\n", plan->stats.overflow);
    printf("record_count=%u\n", (unsigned)plan->record_count);
    for (i = 0; i < plan->record_count; i++) {
        const TwlhdTexCapRecord *rec = &plan->records[i];

        printf("record %u param=%u pltt=%u texel_bytes=%u palette_bytes=%u "
               "index_bytes=%u texel_offset=%u palette_offset=%u "
               "index_offset=%u\n",
               (unsigned)i, (unsigned)rec->teximage_param,
               (unsigned)rec->pltt_base, (unsigned)rec->texel_bytes,
               (unsigned)rec->palette_bytes, (unsigned)rec->index_bytes,
               (unsigned)rec->texel_offset, (unsigned)rec->palette_offset,
               (unsigned)rec->index_offset);
    }
    printf("txdt %u\n", (unsigned)plan->stats.bytes_txdt);
    print_hex(txdt, plan->stats.bytes_txdt);
    printf("pldt %u\n", (unsigned)plan->stats.bytes_pldt);
    print_hex(pldt, plan->stats.bytes_pldt);

cleanup:
    free(gxcm);
    free(mem);
    free(plan);
    free(txdt);
    free(pldt);
    free(entries);
    return bad ? 2 : 0;
}
