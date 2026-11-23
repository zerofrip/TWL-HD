/* Generated from quads.json by gen_quads.py. Do not edit. */
#ifndef DS_ALPHA_TEST_QUADS_H
#define DS_ALPHA_TEST_QUADS_H

#define DSALPHA_ID "ds-alpha-test-1"
#define DSALPHA_CLEAR_R 0
#define DSALPHA_CLEAR_G 0
#define DSALPHA_CLEAR_B 0

typedef struct {
    const char *name;
    int x, y, w, h;
    unsigned alpha, poly_id;
    int z;
    unsigned r5, g5, b5;
} AlphaPoly;

static const AlphaPoly ALPHA_BACKDROPS[] = {
    { "black", 0, 0, 128, 192, 31, 1, 0, 0, 0, 0 },
    { "grey", 128, 0, 128, 192, 31, 2, 0, 16, 16, 16 },
};

static const AlphaPoly ALPHA_QUADS[] = {
    { "L4", 2, 32, 16, 128, 4, 3, 64, 31, 31, 31 },
    { "L8", 20, 32, 16, 128, 8, 4, 64, 31, 31, 31 },
    { "L12", 38, 32, 16, 128, 12, 5, 64, 31, 31, 31 },
    { "L16", 56, 32, 16, 128, 16, 6, 64, 31, 31, 31 },
    { "L20", 74, 32, 16, 128, 20, 7, 64, 31, 31, 31 },
    { "L24", 92, 32, 16, 128, 24, 8, 64, 31, 31, 31 },
    { "L28", 110, 32, 16, 128, 28, 9, 64, 31, 31, 31 },
    { "R4", 130, 32, 16, 128, 4, 10, 64, 31, 31, 31 },
    { "R8", 148, 32, 16, 128, 8, 11, 64, 31, 31, 31 },
    { "R12", 166, 32, 16, 128, 12, 12, 64, 31, 31, 31 },
    { "R16", 184, 32, 16, 128, 16, 13, 64, 31, 31, 31 },
    { "R20", 202, 32, 16, 128, 20, 14, 64, 31, 31, 31 },
    { "R24", 220, 32, 16, 128, 24, 15, 64, 31, 31, 31 },
    { "R28", 238, 32, 16, 128, 28, 16, 64, 31, 31, 31 },
};

#define ALPHA_BACKDROP_COUNT (sizeof(ALPHA_BACKDROPS) / sizeof(ALPHA_BACKDROPS[0]))
#define ALPHA_QUAD_COUNT (sizeof(ALPHA_QUADS) / sizeof(ALPHA_QUADS[0]))

#endif
