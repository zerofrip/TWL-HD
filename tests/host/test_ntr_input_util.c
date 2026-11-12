// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for tools/ntr_capture/ntr_input_util.h.
 *
 * All expectations are literal protocol values derived from NTRViewer-HR:
 * ntr_rp.h:6-12 (frame layout), ui_input_redirection.c:467-473 (idle frame),
 * ui_input_redirection.c:383,417-429,478-481 (touch encoding),
 * ui_input_redirection.c:651-703 (active-low hidPad bits), ntr_rp.c:2738
 * (UDP port 4950).
 */
#include "tests_host.h"

#include "ntr_input_util.h"

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void test_button_bits(void) {
    /* hidPad is active-low: 0xfff idle, a cleared bit is a pressed button. */
    static const struct {
        const char *name;
        int bit;
        uint32_t hid_pad;
    } cases[] = {
        {"A", 0, 0xffe},   {"B", 1, 0xffd},      {"SELECT", 2, 0xffb},
        {"START", 3, 0xff7}, {"RIGHT", 4, 0xfef}, {"LEFT", 5, 0xfdf},
        {"UP", 6, 0xfbf},  {"DOWN", 7, 0xf7f},    {"R", 8, 0xeff},
        {"L", 9, 0xdff},   {"X", 10, 0xbff},      {"Y", 11, 0x7ff},
    };
    uint8_t frame[NTR_INPUT_FRAME_SIZE];
    size_t i;

    CHECK_EQ_INT(sizeof(frame), 20);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t mask = 0;
        CHECK_EQ_INT(ntr_input_button_bit(cases[i].name), cases[i].bit);
        CHECK_EQ_INT(ntr_input_parse_buttons(cases[i].name, &mask), 0);
        CHECK_EQ_INT(mask, 1u << (unsigned)cases[i].bit);
        CHECK_EQ_INT(ntr_input_build_frame(frame, mask, 0, 0, 0), 0);
        CHECK_EQ_INT(rd_u32le(frame + 0), cases[i].hid_pad);
    }
    /* Unknown names are detectable (the CLI turns this into exit 2). */
    CHECK_EQ_INT(ntr_input_button_bit("Z"), -1);
    CHECK_EQ_INT(ntr_input_button_bit("a"), -1);
    CHECK_EQ_INT(ntr_input_button_bit(""), -1);
    CHECK_EQ_INT(ntr_input_button_bit(NULL), -1);
}

static void test_default_frame(void) {
    static const uint8_t expected[NTR_INPUT_FRAME_SIZE] = {
        0xff, 0x0f, 0x00, 0x00, /* hidPad 0xfff: nothing pressed */
        0x00, 0x00, 0x00, 0x02, /* touchScreenState 0x2000000: no touch */
        0xff, 0xf7, 0x7f, 0x00, /* circlePadState 0x7ff7ff */
        0x81, 0x00, 0x80, 0x80, /* cppState 0x80800081 */
        0x00, 0x00, 0x00, 0x00  /* interfaceButtons 0 */
    };
    uint8_t frame[NTR_INPUT_FRAME_SIZE];

    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, 0, 0, 0), 0);
    CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
    CHECK_EQ_INT(rd_u32le(frame + 0), 0xfff);
    CHECK_EQ_INT(rd_u32le(frame + 4), 0x2000000);
    CHECK_EQ_INT(rd_u32le(frame + 8), 0x7ff7ff);
    CHECK_EQ_INT(rd_u32le(frame + 12), 0x80800081);
    CHECK_EQ_INT(rd_u32le(frame + 16), 0);

    /* A touch position is ignored unless touch_valid is set. */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, 160, 120, 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 4), 0x2000000);
}

static void test_combined_presses(void) {
    uint8_t frame[NTR_INPUT_FRAME_SIZE];

    CHECK_EQ_INT(ntr_input_build_frame(frame, 0x001u | 0x002u, 0, 0, 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 0), 0xffc); /* A+B */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0x800u, 0, 0, 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 0), 0x7ff); /* Y */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0xc0fu, 0, 0, 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 0), 0x3f0); /* A,B,SELECT,START too */
    /* Every whitelisted button at once still leaves interfaceButtons zero. */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0xfffu, 0, 0, 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 0), 0);
    CHECK_EQ_INT(rd_u32le(frame + 16), 0);
}

static void test_touch_encoding(void) {
    /* (1 << 24) | (y << 12) | x, with x/y scaled from bottom-screen pixels to
     * the 12-bit raw range 0..0xfff exactly as the viewer does:
     * raw = 0xfff * pixel / screen_dimension (320 wide, 240 high). */
    static const struct {
        int x;
        int y;
        uint32_t touch;
    } cases[] = {
        {0, 0, 0x1000000u},      /* raw (0, 0) */
        {319, 239, 0x1fedff2u},  /* raw x 4082 (0xff2), y 4077 (0xfed) */
        {160, 120, 0x17ff7ffu},  /* raw x 2047, y 2047 (centre) */
        {1, 1, 0x101100cu},      /* raw x 12 (0xc), y 17 (0x11) */
    };
    uint8_t frame[NTR_INPUT_FRAME_SIZE];
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK_EQ_INT(ntr_input_build_frame(frame, 0, cases[i].x, cases[i].y,
                                           1),
                     0);
        CHECK_EQ_INT(rd_u32le(frame + 4), cases[i].touch);
    }

    /* Pressed button + touch in one frame, whole frame literal. */
    {
        static const uint8_t expected[NTR_INPUT_FRAME_SIZE] = {
            0xfe, 0x0f, 0x00, 0x00, /* hidPad 0xffe: A pressed */
            0xff, 0xf7, 0x7f, 0x01, /* touch (160, 120) */
            0xff, 0xf7, 0x7f, 0x00,
            0x81, 0x00, 0x80, 0x80,
            0x00, 0x00, 0x00, 0x00
        };
        CHECK_EQ_INT(ntr_input_build_frame(frame, 0x001u, 160, 120, 1), 0);
        CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
    }
}

static void test_build_frame_rejects(void) {
    uint8_t frame[NTR_INPUT_FRAME_SIZE];

    memset(frame, 0xaa, sizeof(frame));
    /* Range-checked, not clamped: 320/240 and negatives are usage errors. */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, 320, 0, 1), -1);
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, 0, 240, 1), -1);
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, -1, 0, 1), -1);
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0, 0, -1, 1), -1);
    /* A rejected call leaves the buffer untouched. */
    CHECK_EQ_INT(frame[0], 0xaa);
    CHECK_EQ_INT(frame[4], 0xaa);
    /* Masks outside the 12 pad bits are rejected, not truncated. */
    CHECK_EQ_INT(ntr_input_build_frame(frame, 0x1000u, 0, 0, 0), -1);
    CHECK_EQ_INT(ntr_input_build_frame(NULL, 0, 0, 0, 0), -1);
}

static void test_parse_buttons(void) {
    uint32_t mask = 0xdeadbeef;

    CHECK_EQ_INT(ntr_input_parse_buttons("A", &mask), 0);
    CHECK_EQ_INT(mask, 0x001);
    CHECK_EQ_INT(ntr_input_parse_buttons("A,B", &mask), 0);
    CHECK_EQ_INT(mask, 0x003);
    CHECK_EQ_INT(ntr_input_parse_buttons("X,Y,START", &mask), 0);
    CHECK_EQ_INT(mask, 0xc08);
    CHECK_EQ_INT(ntr_input_parse_buttons("SELECT", &mask), 0);
    CHECK_EQ_INT(mask, 0x004);
    CHECK_EQ_INT(ntr_input_parse_buttons("A,A", &mask), 0);
    CHECK_EQ_INT(mask, 0x001); /* OR-ed, not duplicated */

    CHECK_EQ_INT(ntr_input_parse_buttons("Z", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("AV", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("A,", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons(",A", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("A,,B", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("AAAAAAA", &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons(NULL, &mask), -1);
    CHECK_EQ_INT(ntr_input_parse_buttons("A", NULL), -1);
}

static void test_validate_ipv4(void) {
    CHECK_EQ_INT(ntr_input_validate_ipv4("192.168.1.32"), 1);
    CHECK_EQ_INT(ntr_input_validate_ipv4("0.0.0.0"), 1);
    CHECK_EQ_INT(ntr_input_validate_ipv4("255.255.255.255"), 1);
    CHECK_EQ_INT(ntr_input_validate_ipv4("127.0.0.1"), 1);

    CHECK_EQ_INT(ntr_input_validate_ipv4("192.168.1.256"), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4("192.168.1"), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4("192.168.1.32.5"), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4("console.local"), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4(" 192.168.1.32"), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4("192.168.1.32 "), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4(""), 0);
    CHECK_EQ_INT(ntr_input_validate_ipv4(NULL), 0);
}

static void test_clamp_and_hold(void) {
    CHECK_EQ_INT(ntr_input_clamp(49, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 50);
    CHECK_EQ_INT(ntr_input_clamp(5001, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 5000);
    CHECK_EQ_INT(ntr_input_clamp(0, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 50);
    CHECK_EQ_INT(ntr_input_clamp(150, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 150);
    CHECK_EQ_INT(ntr_input_clamp(50, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 50);
    CHECK_EQ_INT(ntr_input_clamp(5000, NTR_INPUT_HOLD_MS_MIN,
                                 NTR_INPUT_HOLD_MS_MAX),
                 5000);

    /* ~60 Hz frames covering the hold, rounded up. */
    CHECK_EQ_INT(ntr_input_hold_frames(50), 3);
    CHECK_EQ_INT(ntr_input_hold_frames(150), 9);
    CHECK_EQ_INT(ntr_input_hold_frames(5000), 300);
    CHECK_EQ_INT(ntr_input_hold_frames(16), 1);
    CHECK_EQ_INT(ntr_input_hold_frames(17), 2);

    /* The protocol port is fixed at 4950 and is not a CLI value. */
    CHECK_EQ_INT(NTR_INPUT_UDP_PORT, 4950);
    CHECK_EQ_INT(NTR_INPUT_FRAME_SIZE, 20);
}

static void test_repeat_and_parse_int(void) {
    int v;

    /* --repeat bounds: the header owns them so they are testable without a
     * socket; the CLI calls repeat_ok() as its only range check. */
    CHECK_EQ_INT(ntr_input_repeat_ok(0), 0);
    CHECK_EQ_INT(ntr_input_repeat_ok(1), 1);
    CHECK_EQ_INT(ntr_input_repeat_ok(2), 1);
    CHECK_EQ_INT(ntr_input_repeat_ok(19), 1);
    CHECK_EQ_INT(ntr_input_repeat_ok(20), 1);
    CHECK_EQ_INT(ntr_input_repeat_ok(21), 0);
    CHECK_EQ_INT(ntr_input_repeat_ok(-1), 0);
    CHECK_EQ_INT(ntr_input_repeat_ok(-1000000000L), 0);
    CHECK_EQ_INT(ntr_input_repeat_ok(1000000000L), 0);

    /* Strict decimal parse shared by every numeric option. */
    v = -1;
    CHECK_EQ_INT(ntr_input_parse_int("1", 1, 20, &v), 0);
    CHECK_EQ_INT(v, 1);
    CHECK_EQ_INT(ntr_input_parse_int("20", 1, 20, &v), 0);
    CHECK_EQ_INT(v, 20);
    CHECK_EQ_INT(ntr_input_parse_int("0", 0, 60000, &v), 0);
    CHECK_EQ_INT(v, 0);
    CHECK_EQ_INT(ntr_input_parse_int("-1000000", -1000000, 1000000, &v), 0);
    CHECK_EQ_INT(v, -1000000);

    /* Rejected: non-numeric, empty, trailing junk, overflow, out of range. */
    CHECK_EQ_INT(ntr_input_parse_int("abc", -1000000000, 1000000000, &v), -1);
    CHECK_EQ_INT(ntr_input_parse_int("", -1000000000, 1000000000, &v), -1);
    CHECK_EQ_INT(ntr_input_parse_int("5x", -1000000000, 1000000000, &v), -1);
    CHECK_EQ_INT(ntr_input_parse_int("99999999999", -1000000000, 1000000000,
                                     &v),
                 -1);
    CHECK_EQ_INT(ntr_input_parse_int(NULL, -1000000000, 1000000000, &v), -1);
    CHECK_EQ_INT(ntr_input_parse_int("0", 1, 20, &v), -1);
    CHECK_EQ_INT(ntr_input_parse_int("21", 1, 20, &v), -1);

    /* Literal pins for limits only the CLI consumes (no pure consumer). */
    CHECK_EQ_INT(NTR_INPUT_HOLD_MS_DEFAULT, 150);
    CHECK_EQ_INT(NTR_INPUT_HOLD_MS_MIN, 50);
    CHECK_EQ_INT(NTR_INPUT_HOLD_MS_MAX, 5000);
    CHECK_EQ_INT(NTR_INPUT_REPEAT_DEFAULT, 1);
    CHECK_EQ_INT(NTR_INPUT_REPEAT_MAX, 20);
    CHECK_EQ_INT(NTR_INPUT_GAP_MS_DEFAULT, 250);
    CHECK_EQ_INT(NTR_INPUT_GAP_MS_MAX, 60000);
    CHECK_EQ_INT(NTR_INPUT_RELEASE_FRAMES_DEFAULT, 6);
    CHECK_EQ_INT(NTR_INPUT_RELEASE_FRAMES_MAX, 600);
}

int main(void) {
    test_button_bits();
    test_report("input/button-bits");
    test_default_frame();
    test_report("input/default-frame");
    test_combined_presses();
    test_report("input/combined-presses");
    test_touch_encoding();
    test_report("input/touch-encoding");
    test_build_frame_rejects();
    test_report("input/build-frame-rejects");
    test_parse_buttons();
    test_report("input/parse-buttons");
    test_validate_ipv4();
    test_report("input/validate-ipv4");
    test_clamp_and_hold();
    test_report("input/clamp-hold");
    test_repeat_and_parse_int();
    test_report("input/repeat-parse-int");
    return test_exit_code();
}
