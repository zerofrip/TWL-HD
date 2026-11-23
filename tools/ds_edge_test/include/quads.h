/* Generated from quads.json by gen_quads.py. Do not edit. */
#ifndef DS_EDGE_TEST_QUADS_H
#define DS_EDGE_TEST_QUADS_H

#define DSEDGE_ID "ds-edge-test-1"
#define DSEDGE_CLEAR_R 0
#define DSEDGE_CLEAR_G 0
#define DSEDGE_CLEAR_B 0

typedef struct {
    const char *name;
    int x, y, w, h;
    unsigned alpha, poly_id;
    int z;
    unsigned r5, g5, b5;
} EdgePoly;

static const EdgePoly EDGE_BACKDROPS[] = {
    { "black", 0, 0, 256, 192, 31, 1, 0, 0, 0, 0 },
};

static const EdgePoly EDGE_QUADS[] = {
    { "top_band", 0, 0, 256, 16, 31, 2, 64, 31, 31, 31 },
    { "bottom_band", 0, 176, 256, 16, 31, 3, 64, 31, 31, 31 },
    { "left_band", 0, 80, 16, 32, 31, 4, 64, 31, 31, 31 },
    { "right_band", 240, 80, 16, 32, 31, 5, 64, 31, 31, 31 },
    { "sq1", 32, 32, 32, 32, 31, 6, 64, 31, 31, 31 },
    { "sq2", 160, 32, 32, 32, 31, 7, 64, 31, 31, 31 },
    { "sq3", 32, 128, 32, 32, 31, 8, 64, 31, 31, 31 },
    { "sq4", 160, 128, 32, 32, 31, 9, 64, 31, 31, 31 },
    { "hline", 64, 96, 128, 1, 31, 10, 64, 31, 31, 31 },
    { "vline", 208, 64, 1, 96, 31, 11, 64, 31, 31, 31 },
};

#define EDGE_BACKDROP_COUNT (sizeof(EDGE_BACKDROPS) / sizeof(EDGE_BACKDROPS[0]))
#define EDGE_QUAD_COUNT (sizeof(EDGE_QUADS) / sizeof(EDGE_QUADS[0]))

#endif
