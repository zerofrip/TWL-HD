// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD run options read from a small text file (sdmc:/twlhd_opts.txt).
 *
 * Pure C11, allocation-free, host-tested. The only option today is the
 * interpreter option mask (TWLHD_GX_OPT_*), so one build can replay retail
 * frames (DS stack + culling) and the older synthetic test frames (no
 * culling) without being rebuilt.
 *
 * File format (ASCII, at most TWLHD_OPTS_MAX_BYTES bytes):
 *   - one `key = value` pair per line; blank lines and lines starting with
 *     '#' are ignored; a '#' after a value starts a comment;
 *   - `gx_opts = <number>`  decimal or 0x-hex mask of known TWLHD_GX_OPT_* bits;
 *   - `gx = <word> [<word>...]`  words `cull`, `ds_stack`, `none`, separated
 *     by spaces or commas (`none` is the empty mask);
 *   - unknown keys, unknown words, unknown mask bits and malformed numbers
 *     are errors. On an error the output is left unchanged (fail closed:
 *     the caller keeps its compile-time default).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWLHD_OPTS_MAX_BYTES 512u

typedef enum {
    TWLHD_OPTS_OK = 0,
    TWLHD_OPTS_ERR_NULL,
    TWLHD_OPTS_ERR_SIZE,   /* longer than TWLHD_OPTS_MAX_BYTES */
    TWLHD_OPTS_ERR_SYNTAX, /* not a `key = value` line, unknown key or word */
    TWLHD_OPTS_ERR_VALUE   /* malformed number or unknown mask bits */
} TwlhdOptsResult;

/* Bits accepted in a gx mask (TWLHD_GX_OPT_CULL | TWLHD_GX_OPT_DS_STACK). */
#define TWLHD_OPTS_GX_KNOWN_BITS 0x3u

/*
 * Parse `text` (not necessarily NUL-terminated). On TWLHD_OPTS_OK *gx_opts is
 * set when the file contains a gx_opts / gx line (the last one wins) and left
 * untouched otherwise; on any error it is left untouched.
 */
TwlhdOptsResult twlhd_opts_parse(const char *text, size_t len,
                                 uint32_t *gx_opts);

const char *twlhd_opts_result_string(TwlhdOptsResult result);

#ifdef __cplusplus
}
#endif
