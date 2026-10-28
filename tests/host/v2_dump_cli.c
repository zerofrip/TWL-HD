// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI used by tools/test_twlhd_v2.py.
 *
 * Prints a stable, line-oriented summary of what the C v2 parser accepted:
 *   result=<name>
 *   flags=<n>
 *   frame=<n>
 *   chunks=<n>
 *   unknown=<n>
 *   entries=<n>
 *   rsta=<n>
 *   meta_backend=<n>
 *   meta_coverage=<n>
 *   count 0x<8 hex cmd_word> <n>
 *
 * Exits 0 for any parse result so the Python side compares semantics rather
 * than exit codes. Exits 2 on usage/IO errors.
 */

#include <stdio.h>
#include <stdlib.h>

#include "twlhd_v2_parse.h"

int main(int argc, char **argv) {
    FILE *file;
    long length;
    size_t size;
    uint8_t *data;
    TwlhdV2View view;
    TwlhdV2Result result;
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

    result = twlhd_v2_parse(data, size, &view);
    printf("result=%s\n", twlhd_v2_result_string(result));

    if (result == TWLHD_V2_OK) {
        printf("flags=%lu\n", (unsigned long)view.flags);
        printf("frame=%lu\n", (unsigned long)view.frame_index);
        printf("chunks=%lu\n", (unsigned long)view.chunk_count);
        printf("unknown=%lu\n", (unsigned long)view.unknown_chunks_skipped);
        printf("entries=%lu\n", (unsigned long)view.gxcm_count);
        printf("rsta=%lu\n", (unsigned long)view.rsta_count);
        printf("meta_backend=%lu\n", (unsigned long)view.meta_backend_id);
        printf("meta_coverage=%lu\n", (unsigned long)view.meta_coverage_flags);

        for (i = 0; i < view.gxcm_count; i++) {
            uint32_t word = twlhd_v2_gxcm_cmd_word(&view, i);
            uint32_t j;
            uint32_t count = 0;

            for (j = 0; j < i; j++) {
                if (twlhd_v2_gxcm_cmd_word(&view, j) == word) break;
            }
            if (j != i) continue;

            for (j = i; j < view.gxcm_count; j++) {
                if (twlhd_v2_gxcm_cmd_word(&view, j) == word) count++;
            }
            printf("count 0x%08lX %lu\n", (unsigned long)word,
                   (unsigned long)count);
        }
    }

    free(data);
    return 0;
}
