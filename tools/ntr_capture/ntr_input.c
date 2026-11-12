// SPDX-License-Identifier: Apache-2.0
/*
 * Headless NTR-HR input redirection client: presses buttons and touches the
 * bottom screen on a 3DS running Rosalina (Luma3DS) input redirection.
 *
 * BUILD/UNIT only: this program only sends when run with --console-ip.
 * Nothing here was executed against a console during implementation.
 *
 * Protocol sources (MIT-licensed NTRViewer-HR, plus Luma3DS):
 *   ntr_rp.h:6-12              20-byte input_redirection_frame_t
 *   ntr_rp.c:2738-2763         UDP port 4950 and the sendto() call
 *   ui_input_redirection.c:467-473  idle frame values
 *   ui_input_redirection.c:383,478-481  touch coordinate encoding
 *   ui_input_redirection.c:651-703  active-low hidPad button bits
 *   main.c:883                 60 Hz input redirection timer
 *   ntr-hr/include/func.h:34   the pad register is active-low
 */
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "ntr_input_util.h"

struct options {
    const char *console_ip;
    uint32_t press_mask;
    int hold_ms;
    int repeat;
    int gap_ms;
    int touch_valid;
    int touch_x;
    int touch_y;
    int release_frames;
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
            "usage: ntr_input --console-ip <ip> [options]\n"
            "\n"
            "Press buttons / touch the bottom screen through Rosalina\n"
            "(Luma3DS) input redirection, UDP port %d.\n"
            "\n"
            "  --console-ip <ip>      console IPv4 address (required unless\n"
            "                         --dry-run; no hostnames)\n"
            "  --press A,B,X,Y,...    buttons to hold, comma-separated\n"
            "                         (A B X Y L R START SELECT UP DOWN LEFT\n"
            "                         RIGHT; default none)\n"
            "  --hold-ms <n>          how long to hold, clamped to 50..5000\n"
            "                         (default 150)\n"
            "  --repeat <n>           iterations, 1..20 (default 1)\n"
            "  --gap-ms <n>           idle wait between iterations, 0..60000\n"
            "                         (default 250)\n"
            "  --touch <x>,<y>        also touch the bottom screen at pixel\n"
            "                         x 0..319, y 0..239 while holding\n"
            "  --release-frames <n>   idle frames after each hold, 1..600\n"
            "                         (default 6)\n"
            "  --dry-run              print the exact UDP frame bytes; open no\n"
            "                         sockets\n"
            "  --help                 show this help\n"
            "\n"
            "The UDP port is fixed; there is no option for it.\n"
            "Exit codes: 2 usage errors, 1 runtime errors, 0 success.\n",
            NTR_INPUT_UDP_PORT);
}

/* Parse "x,y" bottom-screen pixels; both coordinates are range-checked. */
static int parse_touch(const char *s, int *x, int *y) {
    char *end = NULL;
    long vx;
    long vy;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    vx = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != ',') {
        return -1;
    }
    s = end + 1;
    errno = 0;
    vy = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    if (vx < 0 || vx > NTR_INPUT_TOUCH_MAX_X || vy < 0 ||
        vy > NTR_INPUT_TOUCH_MAX_Y) {
        return -1;
    }
    *x = (int)vx;
    *y = (int)vy;
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

static int send_frame(int fd, const struct sockaddr_in *addr,
                      const uint8_t *frame) {
    ssize_t n;

    do {
        n = sendto(fd, frame, NTR_INPUT_FRAME_SIZE, 0,
                   (const struct sockaddr *)addr, sizeof(*addr));
    } while (n < 0 && errno == EINTR);
    return n == (ssize_t)NTR_INPUT_FRAME_SIZE ? 0 : -1;
}

/* Deadline-based sleep: repeated relative sleeps drift and change how long
 * the console sees a press. */
static void wait_next_frame(struct timespec *next) {
    ntr_input_deadline_advance(next, NTR_INPUT_FRAME_NS);
    /* clock_nanosleep() returns the error number and does NOT set errno; the
     * old errno == EINTR check read stale errno and never retried. */
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next, NULL) ==
           EINTR) {
        /* keep waiting until the deadline */
    }
}

static void sleep_ms(int ms) {
    struct timespec ts;

    if (ms <= 0) {
        return;
    }
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    /* No retry on EINTR: a signal should shorten the gap so the caller can
     * notice the stop request. */
    (void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

int main(int argc, char **argv) {
    struct options o;
    struct sockaddr_in addr;
    struct timespec next;
    struct timespec now;
    uint8_t pressed[NTR_INPUT_FRAME_SIZE];
    uint8_t idle[NTR_INPUT_FRAME_SIZE];
    uint32_t button_mask = 0;
    int hold_frames;
    int fd = -1;
    int exit_code = 0;
    int i;

    memset(&o, 0, sizeof(o));
    o.hold_ms = NTR_INPUT_HOLD_MS_DEFAULT;
    o.repeat = NTR_INPUT_REPEAT_DEFAULT;
    o.gap_ms = NTR_INPUT_GAP_MS_DEFAULT;
    o.release_frames = NTR_INPUT_RELEASE_FRAMES_DEFAULT;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(arg, "--dry-run") == 0) {
            o.dry_run = 1;
        } else if (strcmp(arg, "--console-ip") == 0) {
            if (++i >= argc) {
                die_usage("%s needs an IPv4 address", arg);
            }
            o.console_ip = argv[i];
        } else if (strcmp(arg, "--press") == 0) {
            if (++i >= argc ||
                ntr_input_parse_buttons(argv[i], &button_mask) != 0) {
                die_usage("--press needs a comma-separated list of %s",
                          "A,B,X,Y,L,R,START,SELECT,UP,DOWN,LEFT,RIGHT");
            }
        } else if (strcmp(arg, "--hold-ms") == 0) {
            if (++i >= argc ||
                ntr_input_parse_int(argv[i], -1000000, 1000000, &o.hold_ms) !=
                    0) {
                die_usage("--hold-ms needs an integer");
            }
            o.hold_ms = ntr_input_clamp(o.hold_ms, NTR_INPUT_HOLD_MS_MIN,
                                        NTR_INPUT_HOLD_MS_MAX);
        } else if (strcmp(arg, "--repeat") == 0) {
            if (++i >= argc ||
                ntr_input_parse_int(argv[i], INT_MIN, INT_MAX, &o.repeat) !=
                    0 ||
                !ntr_input_repeat_ok(o.repeat)) {
                die_usage("--repeat needs a value in 1..%d",
                          NTR_INPUT_REPEAT_MAX);
            }
        } else if (strcmp(arg, "--gap-ms") == 0) {
            if (++i >= argc ||
                ntr_input_parse_int(argv[i], 0, NTR_INPUT_GAP_MS_MAX,
                                    &o.gap_ms) != 0) {
                die_usage("--gap-ms needs a value in 0..%d",
                          NTR_INPUT_GAP_MS_MAX);
            }
        } else if (strcmp(arg, "--touch") == 0) {
            if (++i >= argc ||
                parse_touch(argv[i], &o.touch_x, &o.touch_y) != 0) {
                die_usage("--touch needs X,Y with X 0..%d and Y 0..%d",
                          NTR_INPUT_TOUCH_MAX_X, NTR_INPUT_TOUCH_MAX_Y);
            }
            o.touch_valid = 1;
        } else if (strcmp(arg, "--release-frames") == 0) {
            if (++i >= argc ||
                ntr_input_parse_int(argv[i], 1, NTR_INPUT_RELEASE_FRAMES_MAX,
                                    &o.release_frames) != 0) {
                die_usage("--release-frames needs a value in 1..%d",
                          NTR_INPUT_RELEASE_FRAMES_MAX);
            }
        } else {
            die_usage("unknown option: %s", arg);
        }
    }

    if (o.console_ip == NULL && !o.dry_run) {
        die_usage("--console-ip is required (or use --dry-run)");
    }
    if (o.console_ip != NULL && !ntr_input_validate_ipv4(o.console_ip)) {
        die_usage("--console-ip is not an IPv4 address: %s", o.console_ip);
    }

    if (ntr_input_build_frame(pressed, button_mask, o.touch_x, o.touch_y,
                              o.touch_valid) != 0 ||
        ntr_input_build_frame(idle, 0, 0, 0, 0) != 0) {
        die_runtime("internal error: cannot build the input frames");
    }

    if (o.dry_run) {
        printf("dry-run: no sockets opened\n");
        printf("udp %s:%d, %d frame(s) per hold, %d release frame(s), "
               "%d iteration(s)\n",
               o.console_ip != NULL ? o.console_ip : "0.0.0.0",
               NTR_INPUT_UDP_PORT, ntr_input_hold_frames(o.hold_ms),
               o.release_frames, o.repeat);
        print_hex("pressed frame", pressed, sizeof(pressed));
        print_hex("release frame", idle, sizeof(idle));
        return 0;
    }

    hold_frames = ntr_input_hold_frames(o.hold_ms);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NTR_INPUT_UDP_PORT);
    if (inet_pton(AF_INET, o.console_ip, &addr.sin_addr) != 1) {
        die_usage("--console-ip is not an IPv4 address: %s", o.console_ip);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        die_runtime("UDP socket failed: %s", strerror(errno));
    }

    log_ts("sending input-redirection frames to %s:%d (UDP): press mask "
           "0x%03x, touch %s, hold %d ms (%d frames), repeat %d, gap %d ms",
           o.console_ip, NTR_INPUT_UDP_PORT, (unsigned)button_mask,
           o.touch_valid ? "yes" : "no", o.hold_ms, hold_frames, o.repeat,
           o.gap_ms);

    for (i = 0; i < o.repeat; i++) {
        unsigned sent_pressed = 0;
        unsigned sent_idle = 0;
        int n;

        /* Re-base the frame deadline from CLOCK_MONOTONIC at the start of
         * every iteration: the previous iteration's deadlines are already in
         * the past, so without this the 60 Hz spacing collapses and --hold-ms
         * is ignored whenever --repeat > 1. */
        clock_gettime(CLOCK_MONOTONIC, &now);
        ntr_input_iter_rebase(&next, &now);

        for (n = 0; n < hold_frames && !g_stop; n++) {
            if (send_frame(fd, &addr, pressed) != 0) {
                close(fd);
                die_runtime("sending a frame to %s:%d failed: %s",
                            o.console_ip, NTR_INPUT_UDP_PORT,
                            strerror(errno));
            }
            sent_pressed++;
            wait_next_frame(&next);
        }
        /* Always release, including after an interrupt: the console's HID
         * patch keeps applying the last frame, so a missed release leaves
         * the buttons held. */
        for (n = 0; n < o.release_frames; n++) {
            if (send_frame(fd, &addr, idle) != 0) {
                close(fd);
                die_runtime("sending a release frame to %s:%d failed: %s",
                            o.console_ip, NTR_INPUT_UDP_PORT,
                            strerror(errno));
            }
            sent_idle++;
            wait_next_frame(&next);
        }
        log_ts("sent %u pressed + %u release frame(s) (20 bytes) to %s:%d, "
               "iteration %d/%d",
               sent_pressed, sent_idle, o.console_ip, NTR_INPUT_UDP_PORT,
               i + 1, o.repeat);
        if (g_stop) {
            break;
        }
        if (i + 1 < o.repeat) {
            sleep_ms(o.gap_ms);
        }
    }

    if (g_stop) {
        log_ts("interrupted; release frames were sent");
        exit_code = 1;
    }
    close(fd);
    return exit_code;
}
