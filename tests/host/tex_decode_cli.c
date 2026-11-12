// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI for tools/test_tex_decode_golden.py.
 *
 * Reads a v2 file, parses it, decodes every TXDS record with the host texture
 * decoder and prints one line per record followed by one line per pixel:
 *
 *   record <i> result=<name> w=<w> h=<h>
 *   px <index> <r> <g> <b> <a>
 *
 * Pixel lines are printed only when the record decoded (result=OK). The index
 * is the row-major pixel index, so index = y*width + x.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twlhd_tex_decode.h"
#include "twlhd_v2_parse.h"

/* 1024x1024 is the largest DS texture, so 4 MiB covers every record. */
#define MAX_OUT_BYTES (1024u * 1024u * 4u)

int main(int argc, char **argv) {
    FILE *file;
    long length;
    size_t size;
    uint8_t *data;
    uint8_t *out;
    TwlhdV2View view;
    TwlhdV2Result pr;
    uint32_t count;
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

    out = (uint8_t *)malloc(MAX_OUT_BYTES);
    if (!out) {
        fprintf(stderr, "out of memory\n");
        free(data);
        return 2;
    }

    count = twlhd_v2_txds_count(&view);
    for (i = 0; i < count; i++) {
        TwlhdV2TexRecord rec;
        TwlhdTexResult result;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t pixels;
        uint32_t j;

        if (!twlhd_v2_txds_record(&view, i, &rec)) {
            fprintf(stderr, "bad TXDS record %u\n", (unsigned)i);
            free(out);
            free(data);
            return 2;
        }
        (void)twlhd_tex_dimensions(rec.teximage_param, &width, &height);
        pixels = width * height;

        result = twlhd_tex_decode(&rec, twlhd_v2_tex_texel_ptr(&view, i),
                                  twlhd_v2_tex_palette_ptr(&view, i),
                                  twlhd_v2_tex_index_ptr(&view, i), out,
                                  pixels * 4u);
        printf("record %u result=%s w=%u h=%u\n", (unsigned)i,
               twlhd_tex_result_string(result), (unsigned)width,
               (unsigned)height);
        if (result != TWLHD_TEX_OK) continue;

        for (j = 0; j < pixels; j++) {
            printf("px %u %u %u %u %u\n", (unsigned)j, (unsigned)out[j * 4u],
                   (unsigned)out[j * 4u + 1u], (unsigned)out[j * 4u + 2u],
                   (unsigned)out[j * 4u + 3u]);
        }
    }

    free(out);
    free(data);
    return 0;
}
