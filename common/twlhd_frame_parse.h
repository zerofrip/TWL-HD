// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - TwlhdFrame wire parser.
 *
 * Clean-room implementation for TWL-HD: decodes the fixed wire layout from
 * common/twlhd_frame_format.h one field at a time. The caller's buffer is
 * never cast to a struct, so the parser is independent of host compiler
 * padding and of devkitARM's -fshort-enums.
 *
 * Validation mirrors tools/twlhd_dump.py:64-93 and additionally rejects a
 * known command type whose param_count differs from the GXFIFO parameter
 * table in arm9/source/fifo_hook.c:22-45. Note that twlhd_dump.py tolerates
 * files larger than 73740 bytes while this parser requires the exact size.
 *
 * HARDWARE UNVERIFIED: this module is host-tested only; it does not touch
 * either console's hardware.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "twlhd_frame_format.h"

/* 12-byte header + 1024 command slots of 72 bytes. */
#define TWLHD_FRAME_WIRE_SIZE 73740u
#define TWLHD_CMD_WIRE_SIZE   72u
#define TWLHD_FRAME_HEADER_SIZE 12u

typedef enum {
    TWLHD_PARSE_OK = 0,
    TWLHD_PARSE_ERR_NULL,
    TWLHD_PARSE_ERR_SIZE,
    TWLHD_PARSE_ERR_VERSION,
    TWLHD_PARSE_ERR_CMD_COUNT,
    TWLHD_PARSE_ERR_PARAM_COUNT,
} TwlhdParseResult;

typedef struct {
    uint32_t type;
    uint8_t param_count;
    /* Points at the 16 little-endian parameter words inside the caller's
     * buffer. Read them with twlhd_cmd_param(); do not dereference directly
     * on platforms that fault on unaligned loads. */
    const uint8_t *params;
} TwlhdParsedCmd;

typedef struct {
    uint32_t version;
    uint32_t frame_index;
    uint32_t cmd_count;
    TwlhdParsedCmd cmds[TWLHD_RING_CAPACITY];
} TwlhdParsedFrame;

/* Returns TWLHD_PARSE_OK and fills *out, or an ERR_* value. On error *out may
 * be partially filled and must be ignored. */
TwlhdParseResult twlhd_frame_parse(const uint8_t *data, size_t size,
                                   TwlhdParsedFrame *out);

const char *twlhd_parse_result_string(TwlhdParseResult result);

/* Expected GXFIFO parameter word count for a known command type.
 * Returns 1 and writes *out_count when known, 0 when the type is unknown. */
int twlhd_cmd_type_param_count(uint32_t type, uint8_t *out_count);

/* Little-endian read of parameter word `index` (0..15). Returns 0 when the
 * command or index is out of range. */
uint32_t twlhd_cmd_param(const TwlhdParsedCmd *cmd, uint32_t index);
