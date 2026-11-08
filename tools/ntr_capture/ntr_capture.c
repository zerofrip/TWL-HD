// SPDX-License-Identifier: Apache-2.0
/*
 * Headless NTR-HR "JPEG Compat (UDP)" capture client.
 *
 * BUILD/UNIT only: this program is the only thing that talks to a console, and
 * it only does so when run with --console-ip. Nothing here was executed
 * against a console during implementation.
 *
 * Protocol sources (MIT-licensed NTRViewer-HR):
 *   main.c:823-829            menu TCP thread on port 8000
 *   ntr_hb.c:9-19,160-179     TCP packet header and sender
 *   ntr_hb.c:327              heartbeat command 0
 *   ntr_hb.c:377-395          remote-play command 901 and its arguments
 *   ntr_common.c:22           UDP port 8001
 *   ntr_rp.c:1815-1948        UDP 4-byte header, reassembly, end packet
 *   ntr_rp.c:1795-1796        screen mapping (whole byte 1) and full-width bit
 *   const.h:189-192,229-231   screen sizes and packet sizes
 *   ui_renderer_sdl.c:450     -90 degree display rotation
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <jpeglib.h>

#include "ntr_capture_util.h"

#define NTR_CAP_HEARTBEAT_MS 250
#define NTR_CAP_UDP_BUFFER 2048
#define NTR_CAP_UDP_RCVBUF (6 * 1024 * 1024)
#define NTR_CAP_CONNECT_TIMEOUT_MS 5000

enum screen_mode { SCREEN_TOP = 0, SCREEN_BOTTOM = 1, SCREEN_BOTH = 2 };

struct options {
    const char *console_ip;
    const char *pc_ip;
    const char *out_prefix;
    int tcp_port;
    int udp_port;
    int screen;
    int quality;
    int priority_factor;
    int bandwidth;
    int frames;
    int timeout_s;
    int dry_run;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static void vlog_ts(const char *fmt, va_list ap) {
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d] ", tm.tm_year + 1900,
            tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

static void log_ts(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vlog_ts(fmt, ap);
    va_end(ap);
}

static void die_usage(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

static void die_runtime(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vlog_ts(fmt, ap);
    va_end(ap);
    exit(1);
}

static void usage(FILE *fp) {
    fprintf(fp,
            "usage: ntr_capture --console-ip <ip> --pc-ip <ip> [options]\n"
            "\n"
            "Headless NTR-HR remote-play capture (JPEG Compat / UDP).\n"
            "\n"
            "  --console-ip <ip>      console IPv4 address (required unless\n"
            "                         --dry-run; --host is an alias)\n"
            "  --console-port <n>     console TCP port (default 8000; --port is\n"
            "                         an alias)\n"
            "  --pc-ip <ip>           local IPv4 address to bind the UDP socket\n"
            "                         to (required unless --dry-run)\n"
            "  --udp-port <n>         local UDP listen port (default 8001)\n"
            "  --screen top|bottom|both  which screen(s) to stream (default both)\n"
            "  --quality <n>          JPEG quality 10..100 (default 95)\n"
            "  --priority-factor <n>  priority factor for --screen both, 0..255\n"
            "                         (default 1; 0 streams only the top screen)\n"
            "  --bandwidth <n>        bandwidth limit in Mbps 4..20 (default 16)\n"
            "  --frames <n>           frames per screen to write (default 1)\n"
            "  --timeout <sec>        overall receive timeout (default 15)\n"
            "  --out <prefix>         PPM prefix (default ntr_capture)\n"
            "  --dry-run              print the exact TCP bytes; open no sockets\n"
            "  --help                 show this help\n"
            "\n"
            "Exit codes: 2 usage errors, 1 runtime errors, 0 success.\n");
}

static int parse_int(const char *s, int min, int max, int *out) {
    char *end = NULL;
    long v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min || v > max) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static void print_hex(const char *label, const uint8_t *buf, size_t len) {
    size_t i;

    printf("%s (%zu bytes):\n", label, len);
    for (i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("  %04zu  ", i);
        }
        printf("%02x ", buf[i]);
        if (i % 16 == 15 || i + 1 == len) {
            printf("\n");
        }
    }
}

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_block(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static int tcp_connect(const char *host, int port, int timeout_ms) {
    struct sockaddr_in addr;
    struct pollfd pfd;
    int fd;
    int err = 0;
    socklen_t err_len = sizeof(err);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    if (set_nonblock(fd) != 0) {
        goto fail;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            goto fail;
        }
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, timeout_ms) <= 0) {
            errno = ETIMEDOUT;
            goto fail;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0) {
            goto fail;
        }
        if (err != 0) {
            errno = err;
            goto fail;
        }
    }
    if (set_block(fd) != 0) {
        goto fail;
    }
    return fd;

fail:
    err = errno;
    close(fd);
    errno = err;
    return -1;
}

static int udp_bind(const char *pc_ip, int port) {
    struct sockaddr_in addr;
    int fd;
    int rcvbuf = NTR_CAP_UDP_RCVBUF;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    {
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, pc_ip, &addr.sin_addr) != 1) {
        int err = EINVAL;
        close(fd);
        errno = err;
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int err = errno;
        close(fd);
        errno = err;
        return -1;
    }
    if (set_nonblock(fd) != 0) {
        int err = errno;
        close(fd);
        errno = err;
        return -1;
    }
    return fd;
}

static int send_all(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* ---- JPEG decoding (system libjpeg-turbo; nothing vendored) ---------- */

struct jpeg_error_ctx {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
};

static void jpeg_error_exit(j_common_ptr cinfo) {
    struct jpeg_error_ctx *err = (struct jpeg_error_ctx *)cinfo->err;
    longjmp(err->jump, 1);
}

static void jpeg_noop_message(j_common_ptr cinfo) {
    (void)cinfo;
}

static int decode_jpeg_rgb(const uint8_t *data, size_t size, uint8_t **out,
                           int *out_w, int *out_h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_ctx jerr;
    volatile int created = 0;
    uint8_t *volatile rgb = NULL;
    int w = 0;
    int h = 0;

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;
    jerr.pub.output_message = jpeg_noop_message;
    if (setjmp(jerr.jump) != 0) {
        if (created) {
            jpeg_destroy_decompress(&cinfo);
        }
        free(rgb);
        return -1;
    }
    jpeg_create_decompress(&cinfo);
    created = 1;
    jpeg_mem_src(&cinfo, data, (unsigned long)size);
    jpeg_read_header(&cinfo, TRUE);
    /* Reject an oversized header before allocating scanline storage. */
    if (cinfo.image_width > NTR_CAP_PORTRAIT_WIDTH ||
        cinfo.image_height > NTR_CAP_TOP_PORTRAIT_HEIGHT) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    w = (int)cinfo.output_width;
    h = (int)cinfo.output_height;
    if (cinfo.output_components != 3 || w <= 0 || h <= 0 ||
        w > NTR_CAP_PORTRAIT_WIDTH || h > NTR_CAP_TOP_PORTRAIT_HEIGHT) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    rgb = malloc((size_t)w * (size_t)h * 3u);
    if (rgb == NULL) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb + (size_t)cinfo.output_scanline * (size_t)w * 3u;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    *out = rgb;
    *out_w = w;
    *out_h = h;
    return 0;
}

/* ---- frame output ---------------------------------------------------- */

static int write_ppm(const char *path, const uint8_t *rgb, int width,
                     int height) {
    char header[64];
    FILE *fp;
    int header_len;
    size_t pixels = (size_t)width * (size_t)height * 3u;

    header_len = ntr_cap_ppm_header(header, sizeof(header), width, height);
    if (header_len < 0) {
        return -1;
    }
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fwrite(header, 1, (size_t)header_len, fp) != (size_t)header_len ||
        fwrite(rgb, 1, pixels, fp) != pixels) {
        fclose(fp);
        return -1;
    }
    return fclose(fp) == 0 ? 0 : -1;
}

static int emit_frame(struct ntr_cap_reassembler *rs, int screen,
                      const char *prefix, unsigned counters[2]) {
    struct ntr_cap_frame *f = &rs->screen[screen];
    const char *name = screen == 0 ? "top" : "bottom";
    int expected_w = NTR_CAP_PORTRAIT_WIDTH;
    int expected_h =
        screen == 0 ? NTR_CAP_TOP_PORTRAIT_HEIGHT : NTR_CAP_BOTTOM_PORTRAIT_HEIGHT;
    uint8_t *rgb = NULL;
    uint8_t *rotated = NULL;
    int w = 0;
    int h = 0;
    int out_w;
    int out_h;
    char path[1024];
    int ret = 0;

    if (f->downsample != 0) {
        log_ts("screen %s: downsample %u is not supported; frame skipped", name,
               f->downsample);
        f->active = 0;
        f->complete = 0;
        return 0;
    }
    if (f->full_width != 0) {
        log_ts("screen %s: full-width frame (800 wide) is not supported; "
               "frame skipped",
               name);
        f->active = 0;
        f->complete = 0;
        return 0;
    }
    if (decode_jpeg_rgb(f->data, f->size, &rgb, &w, &h) != 0) {
        log_ts("screen %s: JPEG decode failed (%u bytes); frame skipped", name,
               f->size);
        goto out;
    }
    if (w != expected_w || h != expected_h) {
        log_ts("screen %s: unexpected JPEG %dx%d (want %dx%d); frame skipped",
               name, w, h, expected_w, expected_h);
        goto out;
    }
    out_w = h;
    out_h = w;
    rotated = malloc((size_t)out_w * (size_t)out_h * 3u);
    if (rotated == NULL) {
        log_ts("screen %s: out of memory for %dx%d frame", name, out_w, out_h);
        goto out;
    }
    if (ntr_cap_rotate_ccw(rgb, w, h, 3, rotated) != 0) {
        log_ts("screen %s: rotation failed", name);
        goto out;
    }
    snprintf(path, sizeof(path), "%s_%s_%u.ppm", prefix, name,
             counters[screen]);
    if (write_ppm(path, rotated, out_w, out_h) != 0) {
        log_ts("screen %s: cannot write %s: %s", name, path, strerror(errno));
        ret = -1;
        goto out;
    }
    log_ts("screen %s: frame %u %dx%d -> %dx%d, wrote %s", name,
           counters[screen], w, h, out_w, out_h, path);
    counters[screen]++;
    ret = 0;

out:
    free(rgb);
    free(rotated);
    f->active = 0;
    f->complete = 0;
    return ret;
}

/* ---- main ------------------------------------------------------------ */

int main(int argc, char **argv) {
    struct options o;
    uint8_t heartbeat[NTR_CAP_TCP_HEADER_SIZE];
    uint8_t remote_play[NTR_CAP_TCP_HEADER_SIZE];
    struct ntr_cap_reassembler *rs;
    struct in_addr console_addr = {0};
    unsigned counters[2] = {0, 0};
    unsigned heartbeats_sent = 0;
    int want_top;
    int want_bottom;
    int tcp_fd = -1;
    int udp_fd = -1;
    uint32_t seq = 2;
    uint64_t last_heartbeat;
    uint64_t last_heartbeat_log;
    uint64_t deadline;
    int top_priority;
    int priority_factor;
    int exit_code = 0;
    int malformed = 0;
    int foreign = 0;
    int i;

    memset(&o, 0, sizeof(o));
    o.console_ip = NULL;
    o.pc_ip = NULL;
    o.out_prefix = "ntr_capture";
    o.tcp_port = 8000;
    o.udp_port = 8001;
    o.screen = SCREEN_BOTH;
    o.quality = 95;
    o.priority_factor = 1;
    o.bandwidth = 16;
    o.frames = 1;
    o.timeout_s = 15;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(arg, "--console-ip") == 0 ||
                   strcmp(arg, "--host") == 0) {
            if (++i >= argc) {
                die_usage("%s needs an IPv4 address", arg);
            }
            o.console_ip = argv[i];
        } else if (strcmp(arg, "--console-port") == 0 ||
                   strcmp(arg, "--port") == 0) {
            if (++i >= argc || parse_int(argv[i], 1, 65535, &o.tcp_port) != 0) {
                die_usage("%s needs a value in 1..65535", arg);
            }
        } else if (strcmp(arg, "--pc-ip") == 0) {
            if (++i >= argc) {
                die_usage("--pc-ip needs an IPv4 address");
            }
            o.pc_ip = argv[i];
        } else if (strcmp(arg, "--udp-port") == 0) {
            if (++i >= argc || parse_int(argv[i], 1, 65535, &o.udp_port) != 0) {
                die_usage("--udp-port needs a value in 1..65535");
            }
        } else if (strcmp(arg, "--screen") == 0) {
            if (++i >= argc) {
                die_usage("--screen needs top, bottom or both");
            }
            if (strcmp(argv[i], "top") == 0) {
                o.screen = SCREEN_TOP;
            } else if (strcmp(argv[i], "bottom") == 0) {
                o.screen = SCREEN_BOTTOM;
            } else if (strcmp(argv[i], "both") == 0) {
                o.screen = SCREEN_BOTH;
            } else {
                die_usage("--screen needs top, bottom or both");
            }
        } else if (strcmp(arg, "--quality") == 0) {
            if (++i >= argc ||
                parse_int(argv[i], NTR_CAP_JPEG_QUALITY_MIN,
                          NTR_CAP_JPEG_QUALITY_MAX, &o.quality) != 0) {
                die_usage("--quality needs a value in 10..100");
            }
        } else if (strcmp(arg, "--priority-factor") == 0) {
            if (++i >= argc ||
                parse_int(argv[i], 0, 255, &o.priority_factor) != 0) {
                die_usage("--priority-factor needs a value in 0..255");
            }
        } else if (strcmp(arg, "--bandwidth") == 0) {
            if (++i >= argc ||
                parse_int(argv[i], NTR_CAP_BANDWIDTH_MIN,
                          NTR_CAP_BANDWIDTH_MAX, &o.bandwidth) != 0) {
                die_usage("--bandwidth needs a value in 4..20");
            }
        } else if (strcmp(arg, "--frames") == 0) {
            if (++i >= argc || parse_int(argv[i], 1, 10000, &o.frames) != 0) {
                die_usage("--frames needs a value in 1..10000");
            }
        } else if (strcmp(arg, "--timeout") == 0) {
            if (++i >= argc || parse_int(argv[i], 1, 3600, &o.timeout_s) != 0) {
                die_usage("--timeout needs a value in 1..3600 seconds");
            }
        } else if (strcmp(arg, "--out") == 0) {
            if (++i >= argc || argv[i][0] == '\0') {
                die_usage("--out needs a file prefix");
            }
            o.out_prefix = argv[i];
        } else {
            die_usage("unknown option: %s", arg);
        }
    }

    if (o.console_ip == NULL && !o.dry_run) {
        die_usage("--console-ip is required (or use --dry-run)");
    }
    if (o.pc_ip == NULL && !o.dry_run) {
        die_usage("--pc-ip is required (or use --dry-run)");
    }
    if (o.console_ip != NULL &&
        inet_pton(AF_INET, o.console_ip, &console_addr) != 1) {
        die_usage("--console-ip is not an IPv4 address: %s", o.console_ip);
    }
    if (o.pc_ip != NULL) {
        struct in_addr probe;
        if (inet_pton(AF_INET, o.pc_ip, &probe) != 1) {
            die_usage("--pc-ip is not an IPv4 address: %s", o.pc_ip);
        }
    }
    if (o.screen == SCREEN_TOP) {
        top_priority = 1;
        priority_factor = 0;
    } else if (o.screen == SCREEN_BOTTOM) {
        top_priority = 0;
        priority_factor = 0;
    } else {
        top_priority = 1;
        priority_factor = o.priority_factor;
    }
    want_top = o.screen != SCREEN_BOTTOM;
    want_bottom = o.screen != SCREEN_TOP;

    if (ntr_cap_build_heartbeat(heartbeat, sizeof(heartbeat), 0) != 0 ||
        ntr_cap_build_remote_play(remote_play, sizeof(remote_play), 1,
                                  (uint16_t)o.udp_port, top_priority,
                                  priority_factor, o.quality,
                                  o.bandwidth) != 0) {
        die_runtime("internal error: cannot build the TCP command packets");
    }

    if (o.dry_run) {
        printf("dry-run: no sockets opened\n");
        printf("tcp %s:%d, udp %s:%d (%s, JPEG Compat)\n",
               o.console_ip != NULL ? o.console_ip : "0.0.0.0", o.tcp_port,
               o.pc_ip != NULL ? o.pc_ip : "0.0.0.0", o.udp_port,
               o.screen == SCREEN_TOP ? "top"
               : o.screen == SCREEN_BOTTOM ? "bottom"
                                           : "both");
        printf("viewer order: heartbeat cmd 0 seq 0, then remote-play cmd 901 "
               "seq 1 (ntr_hb.c:327,393-395)\n");
        print_hex("heartbeat cmd 0 seq 0", heartbeat, sizeof(heartbeat));
        print_hex("remote-play cmd 901 seq 1", remote_play,
                  sizeof(remote_play));
        return 0;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Bind UDP first: a bind failure must exit before a console session. */
    log_ts("binding UDP %s:%d", o.pc_ip, o.udp_port);
    udp_fd = udp_bind(o.pc_ip, o.udp_port);
    if (udp_fd < 0) {
        die_runtime("UDP bind on %s:%d failed: %s", o.pc_ip, o.udp_port,
                    strerror(errno));
    }
    log_ts("UDP bound on %s:%d", o.pc_ip, o.udp_port);

    log_ts("connecting to %s:%d (TCP)", o.console_ip, o.tcp_port);
    tcp_fd = tcp_connect(o.console_ip, o.tcp_port, NTR_CAP_CONNECT_TIMEOUT_MS);
    if (tcp_fd < 0) {
        die_runtime("TCP connect to %s:%d failed: %s", o.console_ip,
                    o.tcp_port, strerror(errno));
    }
    log_ts("TCP connected to %s:%d", o.console_ip, o.tcp_port);

    if (send_all(tcp_fd, heartbeat, sizeof(heartbeat)) != 0) {
        die_runtime("sending heartbeat (seq 0) failed: %s", strerror(errno));
    }
    heartbeats_sent = 1;
    log_ts("sent heartbeat (cmd 0, seq 0, 84 bytes)");
    if (send_all(tcp_fd, remote_play, sizeof(remote_play)) != 0) {
        die_runtime("sending remote-play command (seq 1) failed: %s",
                    strerror(errno));
    }
    log_ts("sent remote-play (cmd 901, seq 1, 84 bytes, udp port %d)",
           o.udp_port);

    rs = malloc(sizeof(*rs));
    if (rs == NULL) {
        die_runtime("out of memory for the frame reassembler");
    }
    ntr_cap_reassembler_init(rs);

    last_heartbeat = now_ms();
    last_heartbeat_log = last_heartbeat;
    deadline = last_heartbeat + (uint64_t)o.timeout_s * 1000u;

    while (!g_stop) {
        struct pollfd pfd[2];
        int ready;
        uint64_t now = now_ms();

        if ((want_top ? counters[0] >= (unsigned)o.frames : 1) &&
            (want_bottom ? counters[1] >= (unsigned)o.frames : 1)) {
            break;
        }
        if (now >= deadline) {
            log_ts("timeout after %ds: top %u/%d, bottom %u/%d frames",
                   o.timeout_s, counters[0], want_top ? o.frames : 0,
                   counters[1], want_bottom ? o.frames : 0);
            exit_code = 1;
            break;
        }
        if (now - last_heartbeat >= NTR_CAP_HEARTBEAT_MS) {
            ntr_cap_build_heartbeat(heartbeat, sizeof(heartbeat), seq++);
            if (send_all(tcp_fd, heartbeat, sizeof(heartbeat)) != 0) {
                log_ts("heartbeat send failed: %s", strerror(errno));
                exit_code = 1;
                break;
            }
            heartbeats_sent++;
            last_heartbeat = now;
            if (now - last_heartbeat_log >= 4000u) {
                log_ts("heartbeats sent: %u total, seq %u latest",
                       heartbeats_sent, seq - 1u);
                last_heartbeat_log = now;
            }
        }

        pfd[0].fd = tcp_fd;
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        pfd[1].fd = udp_fd;
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;
        ready = poll(pfd, 2, 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_ts("poll failed: %s", strerror(errno));
            exit_code = 1;
            break;
        }
        if (ready == 0) {
            continue;
        }
        if (pfd[0].revents & POLLIN) {
            uint8_t sink[512];
            ssize_t n = recv(tcp_fd, sink, sizeof(sink), MSG_DONTWAIT);
            if (n == 0) {
                log_ts("TCP connection closed by the console");
                exit_code = 1;
                break;
            }
            /* n < 0 with EAGAIN is a spurious wake-up; any data is ignored. */
        }
        if (pfd[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            log_ts("TCP connection closed by the console");
            exit_code = 1;
            break;
        }
        if (pfd[1].revents & POLLIN) {
            uint8_t packet[NTR_CAP_UDP_BUFFER];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(udp_fd, packet, sizeof(packet), 0,
                                 (struct sockaddr *)&from, &from_len);
            int result;

            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    log_ts("UDP receive failed: %s", strerror(errno));
                    exit_code = 1;
                    break;
                }
                continue;
            }
            if (!ntr_cap_addr_is(&from.sin_addr, &console_addr)) {
                /* Only the console's datagrams belong in the reassembler. */
                if (foreign++ == 0) {
                    log_ts("first datagram from non-console address %s ignored",
                           inet_ntoa(from.sin_addr));
                }
                continue;
            }
            result = ntr_cap_feed(rs, packet, (size_t)n);
            if (result == NTR_CAP_FEED_ERROR) {
                if (malformed++ == 0) {
                    log_ts("ignoring a malformed UDP packet (%zd bytes)", n);
                }
                continue;
            }
            if (result != NTR_CAP_FEED_READY) {
                continue;
            }
            for (i = 0; i < 2; i++) {
                if (!rs->screen[i].complete) {
                    continue;
                }
                if ((i == 0 && want_top) || (i == 1 && want_bottom)) {
                    if (emit_frame(rs, i, o.out_prefix, counters) != 0) {
                        exit_code = 1;
                        break;
                    }
                } else {
                    rs->screen[i].active = 0;
                    rs->screen[i].complete = 0;
                }
            }
            if (exit_code != 0) {
                break;
            }
        }
    }

    if (g_stop) {
        log_ts("interrupted");
        exit_code = 1;
    }
    log_ts("heartbeats sent: %u total", heartbeats_sent);
    log_ts("datagrams from non-console addresses ignored: %d", foreign);
    log_ts("closing sockets (the viewer has no stop command; close only, "
           "ntr_hb.c:26-37)");
    if (tcp_fd >= 0) {
        shutdown(tcp_fd, SHUT_RDWR);
        close(tcp_fd);
    }
    if (udp_fd >= 0) {
        close(udp_fd);
    }
    free(rs);
    return exit_code;
}
