// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI used by tools/test_c_parser_consistency.py.
 *
 * Prints a stable, line-oriented summary of what the C parser accepted:
 *   result=<name>
 *   version=<n>
 *   frame=<n>
 *   cmd_count=<n>
 *   count 0x<8 hex> <n>
 *
 * Exits 0 for any parse result (including rejection) so the Python side can
 * compare semantics rather than exit codes. Exits 2 on usage/IO errors.
 */

#include <stdio.h>
#include <stdlib.h>

#include "twlhd_frame_parse.h"

int main(int argc, char **argv) {
    FILE *file;
    long length;
    size_t size;
    uint8_t *data;
    TwlhdParsedFrame *parsed;
    TwlhdParseResult result;
    uint32_t i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <frame.bin>\n", argv[0]);
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
    parsed = (TwlhdParsedFrame *)malloc(sizeof *parsed);
    if (!data || !parsed) {
        fprintf(stderr, "out of memory\n");
        free(data);
        free(parsed);
        fclose(file);
        return 2;
    }

    if (size > 0 && fread(data, 1, size, file) != size) {
        fprintf(stderr, "short read\n");
        free(data);
        free(parsed);
        fclose(file);
        return 2;
    }
    fclose(file);

    result = twlhd_frame_parse(data, size, parsed);
    printf("result=%s\n", twlhd_parse_result_string(result));

    if (result == TWLHD_PARSE_OK) {
        printf("version=%lu\n", (unsigned long)parsed->version);
        printf("frame=%lu\n", (unsigned long)parsed->frame_index);
        printf("cmd_count=%lu\n", (unsigned long)parsed->cmd_count);

        for (i = 0; i < parsed->cmd_count; i++) {
            uint32_t type = parsed->cmds[i].type;
            uint32_t j;
            uint32_t count = 0;

            for (j = 0; j < i; j++) {
                if (parsed->cmds[j].type == type) break;
            }
            if (j != i) continue;

            for (j = i; j < parsed->cmd_count; j++) {
                if (parsed->cmds[j].type == type) count++;
            }
            printf("count 0x%08lX %lu\n",
                   (unsigned long)type, (unsigned long)count);
        }
    }

    free(data);
    free(parsed);
    return 0;
}
