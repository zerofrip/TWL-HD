/* Generated from quads.json by gen_quads.py. Do not edit. */
#ifndef DS_VTXCOLOR_TEST_QUADS_H
#define DS_VTXCOLOR_TEST_QUADS_H

#define DSVTX_ID "ds-vtxcolor-test-1"
#define DSVTX_CLEAR_R 8
#define DSVTX_CLEAR_G 8
#define DSVTX_CLEAR_B 8

typedef struct {
    const char *name;
    int x, y, w, h;
    int has_colour;
    unsigned r5, g5, b5;
} VtxQuad;

static const VtxQuad VTX_QUADS[] = {
    { "qA", 16, 32, 64, 64, 0, 0, 0, 0 },
    { "qB", 96, 32, 64, 64, 1, 31, 31, 31 },
    { "qC", 176, 32, 64, 64, 1, 0, 0, 0 },
    { "qD", 16, 112, 64, 64, 1, 16, 16, 16 },
    { "qE", 96, 112, 64, 64, 1, 31, 0, 0 },
};

#define VTX_QUAD_COUNT (sizeof(VTX_QUADS) / sizeof(VTX_QUADS[0]))

#endif
