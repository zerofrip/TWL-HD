// SPDX-License-Identifier: Apache-2.0
/*
 * Host tests for tools/ntr_capture/ntr_capture_util.h.
 *
 * All expectations are literal protocol bytes derived from NTRViewer-HR:
 * ntr_hb.c:377-397 (TCP command), ntr_rp.c:1815-1948 (UDP frames),
 * ui_renderer_sdl.c:450 (rotation).
 */
#include "tests_host.h"

#include "ntr_capture_util.h"

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void test_build_remote_play(void) {
    uint8_t buf[NTR_CAP_TCP_HEADER_SIZE];
    static const uint8_t expected[84] = {
        0x78, 0x56, 0x34, 0x12, /* magic 0x12345678 */
        0x07, 0x00, 0x00, 0x00, /* seq 7 */
        0x00, 0x00, 0x00, 0x00, /* type 0 */
        0x85, 0x03, 0x00, 0x00, /* cmd 901 */
        0x01, 0x01, 0x00, 0x00, /* arg0: top priority 1, factor 1 */
        0x5f, 0x00, 0x00, 0x00, /* arg1: quality 95 */
        0x00, 0x00, 0x20, 0x00, /* arg2: 16 * 128 * 1024 */
        0xdc, 0xe5, 0xaf, 0x53, /* arg3: guarding magic 1404036572 */
        0x41, 0x1f, 0x00, 0x00, /* arg4: UDP port 8001 */
        0x00, 0x00, 0x00, 0x00, /* arg5: audio off */
        0x00, 0x00, 0x00, 0x00, /* arg6: full width off */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* data_len 0 */
    };

    CHECK_EQ_INT(ntr_cap_build_remote_play(buf, sizeof(buf), 7, 8001, 1, 1, 95,
                                           16),
                 0);
    CHECK_EQ_INT(sizeof(buf), 84);
    CHECK_EQ_INT(rd_u32le(buf + 0), 0x12345678u);
    CHECK_EQ_INT(rd_u32le(buf + 4), 7);
    CHECK_EQ_INT(rd_u32le(buf + 8), 0);
    CHECK_EQ_INT(rd_u32le(buf + 12), 901);
    CHECK_EQ_INT(rd_u32le(buf + 16), 0x0101);
    CHECK_EQ_INT(rd_u32le(buf + 20), 95);
    CHECK_EQ_INT(rd_u32le(buf + 24), 16 * 128 * 1024);
    CHECK_EQ_INT(rd_u32le(buf + 28), 1404036572);
    CHECK_EQ_INT(rd_u32le(buf + 32), 8001);
    CHECK_EQ_INT(rd_u32le(buf + 36), 0);
    CHECK_EQ_INT(rd_u32le(buf + 40), 0);
    CHECK_EQ_INT(rd_u32le(buf + 80), 0);
    CHECK(memcmp(buf, expected, sizeof(expected)) == 0);

    /* --screen top: priority top, factor 0 means top screen only. */
    CHECK_EQ_INT(ntr_cap_build_remote_play(buf, sizeof(buf), 0, 8001, 1, 0, 95,
                                           16),
                 0);
    CHECK_EQ_INT(rd_u32le(buf + 16), 0x0100);
    /* --screen bottom: priority bottom, factor 0. */
    CHECK_EQ_INT(ntr_cap_build_remote_play(buf, sizeof(buf), 0, 8001, 0, 0, 95,
                                           16),
                 0);
    CHECK_EQ_INT(rd_u32le(buf + 16), 0x0000);

    /* Rejects a short buffer, leaves the last byte untouched. */
    CHECK_EQ_INT(ntr_cap_build_remote_play(buf, 83, 0, 8001, 1, 1, 95, 16), -1);
    CHECK_EQ_INT(ntr_cap_build_remote_play(NULL, 84, 0, 8001, 1, 1, 95, 16),
                 -1);
}

static void test_build_heartbeat(void) {
    uint8_t buf[NTR_CAP_TCP_HEADER_SIZE];
    size_t i;

    CHECK_EQ_INT(ntr_cap_build_heartbeat(buf, sizeof(buf), 42), 0);
    CHECK_EQ_INT(rd_u32le(buf + 0), 0x12345678u);
    CHECK_EQ_INT(rd_u32le(buf + 4), 42);
    CHECK_EQ_INT(rd_u32le(buf + 8), 0);
    CHECK_EQ_INT(rd_u32le(buf + 12), 0);
    for (i = 16; i < sizeof(buf); i++) {
        CHECK_EQ_INT(buf[i], 0);
    }
    CHECK_EQ_INT(ntr_cap_build_heartbeat(buf, 83, 0), -1);
    CHECK_EQ_INT(ntr_cap_build_heartbeat(NULL, 84, 0), -1);
}

static void make_udp(uint8_t *pkt, uint8_t frame_id, uint8_t flags,
                     uint8_t format, uint8_t packet_no) {
    pkt[0] = frame_id;
    pkt[1] = flags;
    pkt[2] = format;
    pkt[3] = packet_no;
}

static void test_udp_parse(void) {
    uint8_t pkt[NTR_CAP_RP_DATA_HDR_SIZE + NTR_CAP_RP_PACKET_DATA_SIZE];
    struct ntr_cap_udp_header h;

    memset(pkt, 0xa5, sizeof(pkt));
    make_udp(pkt, 0x41, 0x00, 0x02, 3);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), &h), 0);
    CHECK_EQ_INT(h.frame_id, 0x41);
    CHECK_EQ_INT(h.is_top, 1);
    CHECK_EQ_INT(h.is_end, 0);
    CHECK_EQ_INT(h.downsample, 0);
    CHECK_EQ_INT(h.packet, 3);
    CHECK_EQ_INT((long long)(h.payload - pkt), 4);
    CHECK_EQ_INT(h.payload_len, 1444);

    make_udp(pkt, 0x42, 0x01, 0x02, 0);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), &h), 0);
    CHECK_EQ_INT(h.is_top, 0);

    make_udp(pkt, 0x43, 0x10, 0x02, 0);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), &h), 0);
    CHECK_EQ_INT(h.is_top, 1);
    CHECK_EQ_INT(h.is_end, 1);

    /* Lossless bit is tolerated by the viewer's format check, downsample 3. */
    make_udp(pkt, 0x44, 0x00, 0x0f, 0);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), &h), 0);
    CHECK_EQ_INT(h.downsample, 3);

    make_udp(pkt, 0x45, 0x00, 0x04, 0); /* invalid format bits */
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), &h), -1);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, 3, &h), -1);
    CHECK_EQ_INT(ntr_cap_udp_parse(NULL, sizeof(pkt), &h), -1);
    CHECK_EQ_INT(ntr_cap_udp_parse(pkt, sizeof(pkt), NULL), -1);
}

static int feed_simple(struct ntr_cap_reassembler *rs, uint8_t frame_id,
                       uint8_t flags, uint8_t packet_no, int end,
                       size_t payload_len) {
    uint8_t pkt[NTR_CAP_RP_DATA_HDR_SIZE + NTR_CAP_RP_PACKET_DATA_SIZE];
    if (payload_len > NTR_CAP_RP_PACKET_DATA_SIZE) {
        return NTR_CAP_FEED_ERROR;
    }
    make_udp(pkt, frame_id, (uint8_t)(flags | (end ? 0x10 : 0x00)), 0x02,
             packet_no);
    memset(pkt + 4, (int)(frame_id + packet_no), payload_len);
    return ntr_cap_feed(rs, pkt, 4 + payload_len);
}

static void test_feed_complete(void) {
    static struct ntr_cap_reassembler rs;
    struct ntr_cap_frame *f;

    ntr_cap_reassembler_init(&rs);
    CHECK_EQ_INT(feed_simple(&rs, 0x10, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x10, 0x00, 1, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x10, 0x00, 2, 1, 100),
                 NTR_CAP_FEED_READY);
    f = &rs.screen[0];
    CHECK_EQ_INT(f->active, 1);
    CHECK_EQ_INT(f->complete, 1);
    CHECK_EQ_INT(f->end_packet, 2);
    CHECK_EQ_INT(f->size, 2 * 1444 + 100);
    CHECK_EQ_INT(f->data[0], 0x10);
    CHECK_EQ_INT(f->data[1444], 0x11);
    CHECK_EQ_INT(f->data[2 * 1444], 0x12);
    /* A late packet of the completed frame is ignored. */
    CHECK_EQ_INT(feed_simple(&rs, 0x10, 0x00, 3, 0, 1444),
                 NTR_CAP_FEED_IGNORED);
}

static void test_feed_duplicate(void) {
    static struct ntr_cap_reassembler rs;

    ntr_cap_reassembler_init(&rs);
    CHECK_EQ_INT(feed_simple(&rs, 0x20, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x20, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_DUPLICATE);
    CHECK_EQ_INT(feed_simple(&rs, 0x20, 0x00, 1, 1, 10),
                 NTR_CAP_FEED_READY);
}

static void test_feed_dropped_packet_discards_frame(void) {
    static struct ntr_cap_reassembler rs;
    struct ntr_cap_frame *f;

    ntr_cap_reassembler_init(&rs);
    CHECK_EQ_INT(feed_simple(&rs, 0x30, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    /* Packet 1 is lost; the end packet alone must not complete the frame. */
    CHECK_EQ_INT(feed_simple(&rs, 0x30, 0x00, 2, 1, 10),
                 NTR_CAP_FEED_ACCEPTED);
    f = &rs.screen[0];
    CHECK_EQ_INT(f->end_seen, 1);
    CHECK_EQ_INT(f->complete, 0);

    /* The next frame identity discards the incomplete one. */
    CHECK_EQ_INT(feed_simple(&rs, 0x31, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(f->id[0], 0x31);
    CHECK_EQ_INT(f->complete, 0);
    CHECK_EQ_INT(f->have[1], 0);
    CHECK_EQ_INT(feed_simple(&rs, 0x31, 0x00, 1, 1, 10),
                 NTR_CAP_FEED_READY);
}

static void test_feed_out_of_order(void) {
    static struct ntr_cap_reassembler rs;

    ntr_cap_reassembler_init(&rs);
    /* End packet first, then the two earlier packets. */
    CHECK_EQ_INT(feed_simple(&rs, 0x40, 0x00, 2, 1, 10),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x40, 0x00, 1, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x40, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_READY);
    CHECK_EQ_INT(rs.screen[0].complete, 1);
    CHECK_EQ_INT(rs.screen[0].size, 2 * 1444 + 10);
}

static void test_feed_rejects(void) {
    static struct ntr_cap_reassembler rs;
    uint8_t pkt[4 + 100];

    ntr_cap_reassembler_init(&rs);
    /* Non-end packet with the wrong length is dropped. */
    CHECK_EQ_INT(feed_simple(&rs, 0x50, 0x00, 0, 0, 100),
                 NTR_CAP_FEED_IGNORED);
    /* Too-short and malformed headers are errors. */
    CHECK_EQ_INT(ntr_cap_feed(&rs, pkt, 3), NTR_CAP_FEED_ERROR);
    make_udp(pkt, 0x51, 0x00, 0x04, 0); /* invalid format bits */
    CHECK_EQ_INT(ntr_cap_feed(&rs, pkt, sizeof(pkt)), NTR_CAP_FEED_ERROR);
    CHECK_EQ_INT(ntr_cap_feed(NULL, pkt, sizeof(pkt)), NTR_CAP_FEED_ERROR);
    CHECK_EQ_INT(ntr_cap_feed(&rs, NULL, sizeof(pkt)), NTR_CAP_FEED_ERROR);
}

static void test_feed_screens_independent(void) {
    static struct ntr_cap_reassembler rs;

    ntr_cap_reassembler_init(&rs);
    CHECK_EQ_INT(feed_simple(&rs, 0x60, 0x00, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(feed_simple(&rs, 0x60, 0x00, 1, 1, 10), NTR_CAP_FEED_READY);
    CHECK_EQ_INT(rs.screen[0].complete, 1);
    /* Same frame id, bottom-screen flag: a separate reassembly slot. */
    CHECK_EQ_INT(feed_simple(&rs, 0x60, 0x01, 0, 0, 1444),
                 NTR_CAP_FEED_ACCEPTED);
    CHECK_EQ_INT(rs.screen[0].complete, 1);
    CHECK_EQ_INT(rs.screen[1].complete, 0);
    CHECK_EQ_INT(feed_simple(&rs, 0x60, 0x01, 1, 1, 10), NTR_CAP_FEED_READY);
    CHECK_EQ_INT(rs.screen[1].complete, 1);
}

static void test_rotate(void) {
    /* 3x2 portrait, one RGB triple per pixel. */
    static const uint8_t src[3 * 2 * 3] = {
        1,  2,  3,  4,  5,  6,  7,  8,  9,   /* src row 0 */
        10, 11, 12, 13, 14, 15, 16, 17, 18   /* src row 1 */
    };
    /* 90 degrees counter-clockwise: 2x3 landscape. */
    static const uint8_t expected[3 * 2 * 3] = {
        7,  8,  9,  16, 17, 18, /* dst row 0 = src col 2 */
        4,  5,  6,  13, 14, 15, /* dst row 1 = src col 1 */
        1,  2,  3,  10, 11, 12  /* dst row 2 = src col 0 */
    };
    uint8_t dst[sizeof(expected)];

    CHECK_EQ_INT(ntr_cap_rotate_ccw(src, 3, 2, 3, dst), 0);
    CHECK(memcmp(dst, expected, sizeof(expected)) == 0);
    CHECK_EQ_INT(dst[0], 7);
    CHECK_EQ_INT(dst[3], 16);
    CHECK_EQ_INT(dst[6], 4);

    CHECK_EQ_INT(ntr_cap_rotate_ccw(NULL, 3, 2, 3, dst), -1);
    CHECK_EQ_INT(ntr_cap_rotate_ccw(src, 3, 2, 3, NULL), -1);
    CHECK_EQ_INT(ntr_cap_rotate_ccw(src, 0, 2, 3, dst), -1);
    CHECK_EQ_INT(ntr_cap_rotate_ccw(src, 3, 2, 0, dst), -1);
}

static void test_ppm_header(void) {
    char buf[64];

    CHECK_EQ_INT(ntr_cap_ppm_header(buf, sizeof(buf), 400, 240), 15);
    CHECK(strcmp(buf, "P6\n400 240\n255\n") == 0);
    CHECK_EQ_INT(ntr_cap_ppm_header(buf, sizeof(buf), 320, 240), 15);
    CHECK(strcmp(buf, "P6\n320 240\n255\n") == 0);
    CHECK_EQ_INT(ntr_cap_ppm_header(buf, sizeof(buf), 3, 2), 11);
    CHECK(strcmp(buf, "P6\n3 2\n255\n") == 0);

    CHECK_EQ_INT(ntr_cap_ppm_header(buf, 5, 400, 240), -1);
    CHECK_EQ_INT(ntr_cap_ppm_header(NULL, sizeof(buf), 400, 240), -1);
    CHECK_EQ_INT(ntr_cap_ppm_header(buf, sizeof(buf), 0, 240), -1);
    CHECK_EQ_INT(ntr_cap_ppm_header(buf, sizeof(buf), 400, -1), -1);
}

int main(void) {
    test_build_remote_play();
    test_report("tcp/remote-play");
    test_build_heartbeat();
    test_report("tcp/heartbeat");
    test_udp_parse();
    test_report("udp/parse");
    test_feed_complete();
    test_report("udp/feed-complete");
    test_feed_duplicate();
    test_report("udp/feed-duplicate");
    test_feed_dropped_packet_discards_frame();
    test_report("udp/feed-dropped-packet");
    test_feed_out_of_order();
    test_report("udp/feed-out-of-order");
    test_feed_rejects();
    test_report("udp/feed-rejects");
    test_feed_screens_independent();
    test_report("udp/feed-screens");
    test_rotate();
    test_report("rotate/ccw");
    test_ppm_header();
    test_report("ppm/header");
    return test_exit_code();
}
