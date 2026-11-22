/* Generated from pattern.json by gen_pattern.py. Do not edit. */
#ifndef DS_SCALE_TEST_PATTERN_H
#define DS_SCALE_TEST_PATTERN_H

#define PATTERN_WIDTH 256
#define PATTERN_HEIGHT 192
#define PATTERN_ID "ds-scale-test-1"

#define PATTERN_OP_RECT 0
#define PATTERN_OP_FRAME 1
#define PATTERN_OP_ALT_COLUMNS 2
#define PATTERN_OP_ALT_ROWS 3
#define PATTERN_OP_GRADIENT 4

typedef struct {
    int kind;
    int x, y, w, h;
    unsigned short color;
    int period, phase;
    int step_px, step_value;
} PatternOp;

static const PatternOp PATTERN_OPS[] = {
    {0, 128, 0, 1, 192, 0xFFFF, 0, 0, 0, 0}, /* cross_v */
    {0, 0, 96, 256, 1, 0xFFFF, 0, 0, 0, 0}, /* cross_h */
    {0, 127, 95, 3, 3, 0xFFFF, 0, 0, 0, 0}, /* block3x3 */
    {2, 0, 40, 256, 8, 0xFFFF, 2, 0, 0, 0}, /* hstrip */
    {3, 40, 0, 8, 192, 0xFFFF, 2, 0, 0, 0}, /* vstrip */
    {4, 0, 140, 256, 16, 0x8000, 0, 0, 16, 16}, /* gradient */
    {0, 1, 1, 8, 8, 0x801F, 0, 0, 0, 0}, /* corner_tl */
    {0, 247, 1, 8, 8, 0x83E0, 0, 0, 0, 0}, /* corner_tr */
    {0, 1, 183, 8, 8, 0xFC00, 0, 0, 0, 0}, /* corner_bl */
    {0, 247, 183, 8, 8, 0x83FF, 0, 0, 0, 0}, /* corner_br */
    {1, 0, 0, 256, 192, 0xFFFF, 0, 0, 0, 0}, /* frame */
};

#define PATTERN_OP_COUNT (sizeof(PATTERN_OPS) / sizeof(PATTERN_OPS[0]))

static const unsigned short PATTERN_GRADIENT_BGR555[] = {
    0x8000, 0x8842, 0x9084, 0x98C6, 0xA108, 0xA94A, 0xB18C, 0xB9CE,
    0xC210, 0xCA52, 0xCE73, 0xD6B5, 0xDEF7, 0xE739, 0xEF7B, 0xF7BD,
};

#define PATTERN_GRADIENT_COUNT (sizeof(PATTERN_GRADIENT_BGR555) / sizeof(PATTERN_GRADIENT_BGR555[0]))

#endif
