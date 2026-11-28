// SPDX-License-Identifier: Apache-2.0
#include "twlhd_opts.h"

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int word_eq(const char *s, size_t n, const char *lit) {
    size_t i = 0;
    while (i < n && lit[i] != '\0') {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lit[i]) return 0;
        i++;
    }
    return i == n && lit[i] == '\0';
}

/* Parse an unsigned decimal or 0x-hex number occupying exactly [s, s+n). */
static int parse_number(const char *s, size_t n, uint32_t *out) {
    uint64_t v = 0;
    size_t i = 0;
    unsigned base = 10;

    if (n == 0) return 0;
    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    }
    for (; i < n; i++) {
        unsigned d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10u;
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10u;
        else return 0;
        v = v * base + d;
        if (v > 0xFFFFFFFFu) return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static TwlhdOptsResult parse_line(const char *s, size_t n, uint32_t *gx,
                                  int *have) {
    size_t i = 0, key_start, key_len, val_start, val_end;
    uint32_t mask = 0;

    while (i < n && is_space(s[i])) i++;
    if (i == n || s[i] == '#') return TWLHD_OPTS_OK; /* blank / comment */

    key_start = i;
    while (i < n && is_word_char(s[i])) i++;
    key_len = i - key_start;
    if (key_len == 0) return TWLHD_OPTS_ERR_SYNTAX;
    while (i < n && is_space(s[i])) i++;
    if (i == n || s[i] != '=') return TWLHD_OPTS_ERR_SYNTAX;
    i++;
    while (i < n && is_space(s[i])) i++;
    val_start = i;
    val_end = n;
    for (i = val_start; i < n; i++) {
        if (s[i] == '#') { val_end = i; break; }
    }
    while (val_end > val_start && is_space(s[val_end - 1])) val_end--;
    if (val_end == val_start) return TWLHD_OPTS_ERR_VALUE;

    if (word_eq(s + key_start, key_len, "gx_opts")) {
        if (!parse_number(s + val_start, val_end - val_start, &mask))
            return TWLHD_OPTS_ERR_VALUE;
    } else if (word_eq(s + key_start, key_len, "gx")) {
        size_t p = val_start;
        while (p < val_end) {
            size_t w;
            while (p < val_end && (is_space(s[p]) || s[p] == ',')) p++;
            if (p >= val_end) break;
            w = p;
            while (p < val_end && is_word_char(s[p])) p++;
            if (p == w) return TWLHD_OPTS_ERR_SYNTAX;
            if (word_eq(s + w, p - w, "cull")) mask |= 0x1u;
            else if (word_eq(s + w, p - w, "ds_stack")) mask |= 0x2u;
            else if (word_eq(s + w, p - w, "none")) mask |= 0u;
            else return TWLHD_OPTS_ERR_SYNTAX;
        }
    } else {
        return TWLHD_OPTS_ERR_SYNTAX;
    }

    if ((mask & ~TWLHD_OPTS_GX_KNOWN_BITS) != 0u) return TWLHD_OPTS_ERR_VALUE;
    *gx = mask;
    *have = 1;
    return TWLHD_OPTS_OK;
}

TwlhdOptsResult twlhd_opts_parse(const char *text, size_t len,
                                 uint32_t *gx_opts) {
    size_t pos = 0;
    uint32_t gx = 0;
    int have = 0;

    if (text == NULL || gx_opts == NULL) return TWLHD_OPTS_ERR_NULL;
    if (len > TWLHD_OPTS_MAX_BYTES) return TWLHD_OPTS_ERR_SIZE;

    while (pos < len) {
        size_t end = pos;
        TwlhdOptsResult r;
        while (end < len && text[end] != '\n') end++;
        r = parse_line(text + pos, end - pos, &gx, &have);
        if (r != TWLHD_OPTS_OK) return r;
        pos = end + 1;
    }
    if (have) *gx_opts = gx;
    return TWLHD_OPTS_OK;
}

const char *twlhd_opts_result_string(TwlhdOptsResult result) {
    switch (result) {
    case TWLHD_OPTS_OK: return "OK";
    case TWLHD_OPTS_ERR_NULL: return "ERR_NULL";
    case TWLHD_OPTS_ERR_SIZE: return "ERR_SIZE";
    case TWLHD_OPTS_ERR_SYNTAX: return "ERR_SYNTAX";
    case TWLHD_OPTS_ERR_VALUE: return "ERR_VALUE";
    }
    return "ERR_UNKNOWN";
}
