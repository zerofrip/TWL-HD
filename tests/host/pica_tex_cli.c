// SPDX-License-Identifier: Apache-2.0
/*
 * Host CLI for tools/test_pica_tex_golden.py.
 *
 * Usage: pica_tex_cli <swizzle|unswizzle> <width> <height>
 *
 * Reads exactly width*height*4 raw RGBA8 bytes (R,G,B,A row-major, top row
 * first) from stdin and writes:
 *
 *   result=<name>
 *   byte <index> <value>
 *
 * one byte line per output byte. Exit code 2 means bad usage/IO; a rejected
 * conversion still prints its result line and exits 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twlhd_pica_tex.h"

#define MAX_BYTES (1024u * 1024u * 4u)

int main(int argc, char **argv) {
    unsigned long width;
    unsigned long height;
    unsigned long bytes;
    uint8_t *in;
    uint8_t *out;
    TwlhdPicaTexResult result;
    unsigned long i;
    int do_swizzle;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <swizzle|unswizzle> <width> <height>\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "swizzle") == 0) {
        do_swizzle = 1;
    } else if (strcmp(argv[1], "unswizzle") == 0) {
        do_swizzle = 0;
    } else {
        fprintf(stderr, "bad mode: %s\n", argv[1]);
        return 2;
    }
    width = strtoul(argv[2], NULL, 10);
    height = strtoul(argv[3], NULL, 10);
    if (width == 0u || height == 0u || width > 1024u || height > 1024u) {
        fprintf(stderr, "bad size %lux%lu\n", width, height);
        return 2;
    }
    bytes = width * height * 4u;

    in = (uint8_t *)malloc(bytes);
    out = (uint8_t *)malloc(bytes);
    if (!in || !out) {
        fprintf(stderr, "out of memory\n");
        free(in);
        free(out);
        return 2;
    }
    if (fread(in, 1, bytes, stdin) != bytes) {
        fprintf(stderr, "short read\n");
        free(in);
        free(out);
        return 2;
    }

    if (do_swizzle) {
        result = twlhd_pica_tex_swizzle_rgba8(in, (uint32_t)width,
                                              (uint32_t)height, out,
                                              (uint32_t)bytes);
    } else {
        result = twlhd_pica_tex_unswizzle_rgba8(in, (uint32_t)width,
                                                (uint32_t)height, out,
                                                (uint32_t)bytes);
    }

    printf("result=%s\n", twlhd_pica_tex_result_string(result));
    if (result != TWLHD_PICA_TEX_OK) {
        free(in);
        free(out);
        return 0;
    }
    for (i = 0; i < bytes; i++)
        printf("byte %lu %u\n", i, (unsigned)out[i]);

    free(in);
    free(out);
    return 0;
}
