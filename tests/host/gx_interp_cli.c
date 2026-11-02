// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI for tools/test_gx_interp_golden.py.
 *
 * Reads a v2 file, parses it, runs the GX interpreter, and prints one line per
 * output vertex followed by the stats block.
 */

#include <stdio.h>
#include <stdlib.h>

#include "twlhd_gx_interp.h"
#include "twlhd_v2_parse.h"

int main(int argc, char **argv) {
    FILE *file;
    long length;
    size_t size;
    uint8_t *data;
    TwlhdV2View view;
    TwlhdV2Result pr;
    TwlhdGxResult gr;
    TwlhdGxVertex *out;
    uint32_t out_count = 0u;
    TwlhdGxViewport viewport;
    TwlhdGxStats stats;
    uint32_t capacity;
    uint32_t i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <frame_v2.bin>\n", argv[0]);
        return 2;
    }

    file = fopen(argv[1], "rb");
    if (!file) {
        perror("fopen");
        return 2;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(file);
        return 2;
    }
    length = ftell(file);
    if (length < 0) {
        perror("ftell");
        fclose(file);
        return 2;
    }
    rewind(file);

    size = (size_t)length;
    data = (uint8_t *)malloc(size ? size : 1u);
    if (!data) {
        fprintf(stderr, "out of memory\n");
        fclose(file);
        return 2;
    }

    if (size > 0 && fread(data, 1, size, file) != size) {
        fprintf(stderr, "short read\n");
        free(data);
        fclose(file);
        return 2;
    }
    fclose(file);

    pr = twlhd_v2_parse(data, size, &view);
    if (pr != TWLHD_V2_OK) {
        fprintf(stderr, "parse error: %s\n", twlhd_v2_result_string(pr));
        free(data);
        return 2;
    }

    /* The demo fixture produces 24 vertices; leave headroom for larger frames. */
    capacity = view.gxcm_count;
    if (capacity < 24u) capacity = 24u;
    out = (TwlhdGxVertex *)malloc((size_t)capacity * sizeof(TwlhdGxVertex));
    if (!out) {
        fprintf(stderr, "out of memory\n");
        free(data);
        return 2;
    }

    gr = twlhd_gx_interpret(&view, out, capacity, &out_count, &viewport,
                            &stats);
    printf("result=%s\n", twlhd_gx_result_string(gr));
    printf("viewport %u %u %u %u %d\n",
           (unsigned)viewport.x1, (unsigned)viewport.y1,
           (unsigned)viewport.x2, (unsigned)viewport.y2,
           viewport.present);

    for (i = 0; i < out_count; i++) {
        printf("%u %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
               (unsigned)i,
               (double)out[i].clip[0], (double)out[i].clip[1],
               (double)out[i].clip[2], (double)out[i].clip[3],
               (double)out[i].rgba[0], (double)out[i].rgba[1],
               (double)out[i].rgba[2], (double)out[i].rgba[3]);
    }

    printf("stats commands=%lu vertices_in=%lu triangles_out=%lu "
           "quads_split=%lu dropped_incomplete=%lu ignored_outside_begin=%lu "
           "ignored_no_effect=%lu unsupported=%lu raw_fifo_skipped=%lu "
           "stack_errors=%lu capacity_dropped=%lu geometry_incomplete=%lu\n",
           (unsigned long)stats.commands, (unsigned long)stats.vertices_in,
           (unsigned long)stats.triangles_out, (unsigned long)stats.quads_split,
           (unsigned long)stats.dropped_incomplete,
           (unsigned long)stats.ignored_outside_begin,
           (unsigned long)stats.ignored_no_effect,
           (unsigned long)stats.unsupported,
           (unsigned long)stats.raw_fifo_skipped,
           (unsigned long)stats.stack_errors,
           (unsigned long)stats.capacity_dropped,
           (unsigned long)stats.geometry_incomplete);

    free(out);
    free(data);
    return 0;
}
