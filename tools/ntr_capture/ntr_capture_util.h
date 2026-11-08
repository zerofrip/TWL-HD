// SPDX-License-Identifier: Apache-2.0
/*
 * Pure, header-only helpers for tools/ntr_capture/ntr_capture.c.
 *
 * No sockets and no libjpeg here: everything is libc-only so tests/host can
 * include this header under the same -std=c11 -Wall -Wextra -Werror flags as
 * the rest of the host suite.
 *
 * Protocol facts are transcribed from the MIT-licensed NTRViewer-HR sources
 * (see tools/ntr_capture/README.md for the file:line citations).
 */
#pragma once

#include <arpa/inet.h> /* struct in_addr only; no socket is opened here */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NTR_CAP_TCP_MAGIC 0x12345678u
#define NTR_CAP_TCP_ARGS_COUNT 16u
#define NTR_CAP_TCP_HEADER_SIZE 84u
#define NTR_CAP_TCP_CMD_HEARTBEAT 0u
#define NTR_CAP_TCP_CMD_REMOTE_PLAY 901u
#define NTR_CAP_REMOTE_PLAY_MAGIC 1404036572u

#define NTR_CAP_RP_DATA_HDR_SIZE 4u
#define NTR_CAP_RP_PACKET_DATA_SIZE 1444u /* 1448 - 4, const.h:229-231 */
#define NTR_CAP_RP_MAX_PACKET_COUNT 480u /* ntr_rp.c:45 */
#define NTR_CAP_RP_MAX_FRAME_SIZE \
    (NTR_CAP_RP_MAX_PACKET_COUNT * NTR_CAP_RP_PACKET_DATA_SIZE)

/* JPEG Compat (UDP) screen geometry (const.h:189-192). */
#define NTR_CAP_TOP_WIDTH 400
#define NTR_CAP_TOP_HEIGHT 240
#define NTR_CAP_BOTTOM_WIDTH 320
#define NTR_CAP_BOTTOM_HEIGHT 240
#define NTR_CAP_PORTRAIT_WIDTH 240
#define NTR_CAP_TOP_PORTRAIT_HEIGHT 400
#define NTR_CAP_BOTTOM_PORTRAIT_HEIGHT 320

/* Remote-play limits (const.h:220-227). */
#define NTR_CAP_JPEG_QUALITY_MIN 10
#define NTR_CAP_JPEG_QUALITY_MAX 100
#define NTR_CAP_BANDWIDTH_MIN 4
#define NTR_CAP_BANDWIDTH_MAX 20

struct ntr_cap_udp_header {
    uint8_t frame_id;
    uint8_t is_top; /* 1 = top screen, 0 = bottom (ntr_rp.c:1795) */
    uint8_t is_end; /* last packet of the frame (ntr_rp.c:1833) */
    uint8_t downsample;
    uint16_t packet;
    const uint8_t *payload;
    size_t payload_len;
};

/*
 * Build the 84-byte TCP packet the viewer sends (ntr_hb.c:12-19,160-179,
 * 377-395). `args` may be NULL when argc is 0. Little-endian on the wire: the
 * viewer memcpy()s a native struct and the reference viewer is x86-64.
 */
static inline void ntr_cap_put_u32le(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
    out[2] = (uint8_t)((v >> 16) & 0xffu);
    out[3] = (uint8_t)((v >> 24) & 0xffu);
}

static inline int ntr_cap_build_remote_play(uint8_t *out, size_t out_size,
                                            uint32_t seq, uint16_t udp_port,
                                            int top_screen_priority,
                                            int priority_factor,
                                            int jpeg_quality,
                                            int bandwidth_mbps) {
    uint32_t args[7];
    size_t i;

    if (out == NULL || out_size < NTR_CAP_TCP_HEADER_SIZE) {
        return -1;
    }
    memset(out, 0, NTR_CAP_TCP_HEADER_SIZE);
    ntr_cap_put_u32le(out + 0, NTR_CAP_TCP_MAGIC);
    ntr_cap_put_u32le(out + 4, seq);
    ntr_cap_put_u32le(out + 8, 0); /* type */
    ntr_cap_put_u32le(out + 12, NTR_CAP_TCP_CMD_REMOTE_PLAY);

    /* ntr_hb.c:378-386 */
    args[0] = ((uint32_t)(top_screen_priority ? 1 : 0) << 8) |
              ((uint32_t)priority_factor & 0xffu);
    args[1] = (uint32_t)jpeg_quality;
    args[2] = (uint32_t)bandwidth_mbps * 128u * 1024u;
    args[3] = NTR_CAP_REMOTE_PLAY_MAGIC;
    args[4] = (uint32_t)udp_port;
    args[5] = 0; /* audio off */
    args[6] = 0; /* full width off */

    for (i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
        ntr_cap_put_u32le(out + 16 + i * 4, args[i]);
    }
    ntr_cap_put_u32le(out + 80, 0); /* data_len */
    return 0;
}

/* Viewer heartbeat packet (ntr_hb.c:327). */
static inline int ntr_cap_build_heartbeat(uint8_t *out, size_t out_size,
                                          uint32_t seq) {
    if (out == NULL || out_size < NTR_CAP_TCP_HEADER_SIZE) {
        return -1;
    }
    memset(out, 0, NTR_CAP_TCP_HEADER_SIZE);
    ntr_cap_put_u32le(out + 0, NTR_CAP_TCP_MAGIC);
    ntr_cap_put_u32le(out + 4, seq);
    ntr_cap_put_u32le(out + 8, 0);
    ntr_cap_put_u32le(out + 12, NTR_CAP_TCP_CMD_HEARTBEAT);
    ntr_cap_put_u32le(out + 80, 0);
    return 0;
}

/* Parse and validate the 4-byte UDP header (ntr_rp.c:1815-1838). */
static inline int ntr_cap_udp_parse(const uint8_t *packet, size_t len,
                                    struct ntr_cap_udp_header *out) {
    if (packet == NULL || out == NULL || len < NTR_CAP_RP_DATA_HDR_SIZE) {
        return -1;
    }
    if ((packet[2] & (uint8_t)~0x0du) != 2u) {
        return -1;
    }
    out->frame_id = packet[0];
    out->is_top = (uint8_t)((packet[1] & 0x01u) ? 0u : 1u);
    out->is_end = (uint8_t)((packet[1] & 0x10u) ? 1u : 0u);
    out->downsample = (uint8_t)((packet[2] & 0x0cu) >> 2);
    out->packet = packet[3];
    out->payload = packet + NTR_CAP_RP_DATA_HDR_SIZE;
    out->payload_len = len - NTR_CAP_RP_DATA_HDR_SIZE;
    return 0;
}

/* Sender-address filter for the UDP receive path. */
static inline int ntr_cap_addr_is(const struct in_addr *sender,
                                  const struct in_addr *expected) {
    if (sender == NULL || expected == NULL) {
        return 0;
    }
    return memcmp(&sender->s_addr, &expected->s_addr,
                  sizeof(sender->s_addr)) == 0;
}

struct ntr_cap_frame {
    uint8_t id[3]; /* frame_id, flags, format (ntr_rp.c:1843) */
    uint8_t active;
    uint8_t complete;
    uint8_t end_seen;
    uint8_t downsample;
    uint16_t end_packet;
    uint32_t size;
    uint8_t have[NTR_CAP_RP_MAX_PACKET_COUNT];
    uint8_t data[NTR_CAP_RP_MAX_FRAME_SIZE];
};

struct ntr_cap_reassembler {
    struct ntr_cap_frame screen[2]; /* [0] top, [1] bottom */
};

enum ntr_cap_feed_result {
    NTR_CAP_FEED_ERROR = -1,
    NTR_CAP_FEED_IGNORED = 0,
    NTR_CAP_FEED_ACCEPTED = 1,
    NTR_CAP_FEED_READY = 2,
    NTR_CAP_FEED_DUPLICATE = 3
};

static inline void ntr_cap_reassembler_init(struct ntr_cap_reassembler *rs) {
    if (rs != NULL) {
        memset(rs, 0, sizeof(*rs));
    }
}

/*
 * Feed one UDP datagram. Mirrors the viewer: a frame is identified by its
 * first three header bytes; a new identity starts a new frame and discards
 * whatever was pending (so a lost packet drops the whole frame); duplicate
 * packet numbers are ignored; the end flag triggers the completeness check
 * (ntr_rp.c:1843-1948).
 */
static inline int ntr_cap_feed(struct ntr_cap_reassembler *rs,
                               const uint8_t *packet, size_t len) {
    struct ntr_cap_udp_header h;
    struct ntr_cap_frame *f;
    uint8_t id[3];
    unsigned packet_no;
    unsigned i;

    if (rs == NULL || ntr_cap_udp_parse(packet, len, &h) != 0) {
        return NTR_CAP_FEED_ERROR;
    }
    /* The end bit is not part of the frame identity (ntr_rp.c:1836). */
    id[0] = packet[0];
    id[1] = (uint8_t)(packet[1] & (uint8_t)~0x10u);
    id[2] = packet[2];
    f = &rs->screen[h.is_top ? 0 : 1];
    if (!f->active || memcmp(f->id, id, 3) != 0) {
        memset(f->have, 0, sizeof(f->have));
        memcpy(f->id, id, 3);
        f->active = 1;
        f->complete = 0;
        f->end_seen = 0;
        f->downsample = h.downsample;
        f->end_packet = 0;
        f->size = 0;
    }
    if (f->complete) {
        return NTR_CAP_FEED_IGNORED;
    }
    if (h.payload_len > NTR_CAP_RP_PACKET_DATA_SIZE) {
        return NTR_CAP_FEED_IGNORED;
    }
    if (!h.is_end && h.payload_len != NTR_CAP_RP_PACKET_DATA_SIZE) {
        return NTR_CAP_FEED_IGNORED;
    }

    packet_no = h.packet;
    /* 256-packet wraparound, as the viewer does (ntr_rp.c:1900-1902). */
    if (packet_no + 128u < f->end_packet) {
        packet_no += 256u;
    }
    if (packet_no >= NTR_CAP_RP_MAX_PACKET_COUNT) {
        return NTR_CAP_FEED_IGNORED;
    }
    if (f->have[packet_no]) {
        return NTR_CAP_FEED_DUPLICATE;
    }

    memcpy(f->data + (size_t)packet_no * NTR_CAP_RP_PACKET_DATA_SIZE,
           h.payload, h.payload_len);
    f->have[packet_no] = 1;
    if (packet_no > f->end_packet) {
        f->end_packet = (uint16_t)packet_no;
        f->size = (uint32_t)packet_no * NTR_CAP_RP_PACKET_DATA_SIZE +
                  (uint32_t)h.payload_len;
    }
    if (h.is_end) {
        /* The end packet bounds the frame; its length is authoritative, so a
         * single-packet frame (packet 0 with the end bit) gets a real size. */
        if (packet_no >= f->end_packet) {
            f->end_packet = (uint16_t)packet_no;
            f->size = (uint32_t)packet_no * NTR_CAP_RP_PACKET_DATA_SIZE +
                      (uint32_t)h.payload_len;
        }
        f->end_seen = 1;
    }
    if (!f->end_seen) {
        return NTR_CAP_FEED_ACCEPTED;
    }
    for (i = 0; i <= f->end_packet; i++) {
        if (!f->have[i]) {
            return NTR_CAP_FEED_ACCEPTED;
        }
    }
    f->complete = 1;
    return NTR_CAP_FEED_READY;
}

/*
 * Rotate the 240x400 decoded portrait image to the viewer's 400x240 landscape
 * orientation. The SDL renderer rotates the upright texture by -90 degrees
 * (ui_renderer_sdl.c:450); the OpenGL renderer swaps texture coordinates the
 * same way. Output width is src_h, output height is src_w.
 *
 * dst[dy][dx] = src[dx][src_w - 1 - dy]
 */
static inline int ntr_cap_rotate_ccw(const uint8_t *src, int src_w, int src_h,
                                     int channels, uint8_t *dst) {
    int dy;
    int dx;

    if (src == NULL || dst == NULL || src_w <= 0 || src_h <= 0 ||
        channels <= 0) {
        return -1;
    }
    for (dy = 0; dy < src_w; dy++) {
        uint8_t *dst_row = dst + (size_t)dy * (size_t)src_h * (size_t)channels;
        for (dx = 0; dx < src_h; dx++) {
            const uint8_t *src_pixel =
                src + ((size_t)dx * (size_t)src_w +
                       (size_t)(src_w - 1 - dy)) *
                          (size_t)channels;
            memcpy(dst_row + (size_t)dx * (size_t)channels, src_pixel,
                   (size_t)channels);
        }
    }
    return 0;
}

/* P6 PPM header, the same form tools/azahar_host writes. */
static inline int ntr_cap_ppm_header(char *buf, size_t cap, int width,
                                     int height) {
    int n;

    if (buf == NULL || width <= 0 || height <= 0) {
        return -1;
    }
    n = snprintf(buf, cap, "P6\n%d %d\n255\n", width, height);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}
