// SPDX-License-Identifier: Apache-2.0
/*
 * TWL-HD Phase 4B - TwlhdFrame wire parser.
 *
 * See common/twlhd_frame_parse.h. HARDWARE UNVERIFIED (host-tested only).
 */

#include "twlhd_frame_parse.h"

#include <string.h>

#define CMD_PARAMS_OFFSET 8u

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int twlhd_cmd_type_param_count(uint32_t type, uint8_t *out_count) {
    uint8_t count;

    /* Mirrors g_param_count[] in arm9/source/fifo_hook.c. */
    switch (type) {
    case TWLHD_CMD_MTX_MODE:     count = 1;  break;
    case TWLHD_CMD_MTX_PUSH:     count = 0;  break;
    case TWLHD_CMD_MTX_POP:      count = 1;  break;
    case TWLHD_CMD_MTX_STORE:    count = 1;  break;
    case TWLHD_CMD_MTX_RESTORE:  count = 1;  break;
    case TWLHD_CMD_MTX_IDENTITY: count = 0;  break;
    case TWLHD_CMD_MTX_LOAD_4x4: count = 16; break;
    case TWLHD_CMD_MTX_LOAD_4x3: count = 12; break;
    case TWLHD_CMD_MTX_MULT_4x4: count = 16; break;
    case TWLHD_CMD_MTX_MULT_4x3: count = 12; break;
    case TWLHD_CMD_MTX_MULT_3x3: count = 9;  break;
    case TWLHD_CMD_COLOR:        count = 1;  break;
    case TWLHD_CMD_NORMAL:       count = 1;  break;
    case TWLHD_CMD_TEX_COORD:    count = 1;  break;
    case TWLHD_CMD_VTX_16:       count = 2;  break;
    case TWLHD_CMD_VTX_10:       count = 1;  break;
    case TWLHD_CMD_VTX_XY:       count = 1;  break;
    case TWLHD_CMD_VTX_XZ:       count = 1;  break;
    case TWLHD_CMD_VTX_YZ:       count = 1;  break;
    case TWLHD_CMD_BEGIN_VTXS:   count = 1;  break;
    case TWLHD_CMD_END_VTXS:     count = 0;  break;
    case TWLHD_CMD_VIEWPORT:     count = 1;  break;
    default:
        return 0;
    }

    if (out_count) *out_count = count;
    return 1;
}

TwlhdParseResult twlhd_frame_parse(const uint8_t *data, size_t size,
                                   TwlhdParsedFrame *out) {
    uint32_t cmd_count;
    uint32_t i;

    if (!data || !out) return TWLHD_PARSE_ERR_NULL;

    /* Stricter than twlhd_dump.py, which only rejects short files. */
    if (size != TWLHD_FRAME_WIRE_SIZE) return TWLHD_PARSE_ERR_SIZE;

    out->version = read_le32(data + 0);
    out->frame_index = read_le32(data + 4);
    cmd_count = read_le32(data + 8);

    if (out->version != TWLHD_FORMAT_VERSION) return TWLHD_PARSE_ERR_VERSION;
    if (cmd_count > TWLHD_RING_CAPACITY) return TWLHD_PARSE_ERR_CMD_COUNT;

    out->cmd_count = cmd_count;

    for (i = 0; i < cmd_count; i++) {
        const uint8_t *slot = data + TWLHD_FRAME_HEADER_SIZE +
                              (size_t)i * TWLHD_CMD_WIRE_SIZE;
        uint32_t type = read_le32(slot);
        uint8_t param_count = slot[4];
        uint8_t expected;

        if (param_count > 16) return TWLHD_PARSE_ERR_PARAM_COUNT;

        /* Known types must match the GXFIFO table exactly. Unknown types are
         * tolerated and passed through for downstream skipping/counting. */
        if (twlhd_cmd_type_param_count(type, &expected) &&
            param_count != expected) {
            return TWLHD_PARSE_ERR_PARAM_COUNT;
        }

        out->cmds[i].type = type;
        out->cmds[i].param_count = param_count;
        out->cmds[i].params = slot + CMD_PARAMS_OFFSET;
    }

    return TWLHD_PARSE_OK;
}

const char *twlhd_parse_result_string(TwlhdParseResult result) {
    switch (result) {
    case TWLHD_PARSE_OK:              return "OK";
    case TWLHD_PARSE_ERR_NULL:        return "ERR_NULL";
    case TWLHD_PARSE_ERR_SIZE:        return "ERR_SIZE";
    case TWLHD_PARSE_ERR_VERSION:     return "ERR_VERSION";
    case TWLHD_PARSE_ERR_CMD_COUNT:   return "ERR_CMD_COUNT";
    case TWLHD_PARSE_ERR_PARAM_COUNT: return "ERR_PARAM_COUNT";
    default:                          return "ERR_UNKNOWN";
    }
}

uint32_t twlhd_cmd_param(const TwlhdParsedCmd *cmd, uint32_t index) {
    if (!cmd || !cmd->params || index >= 16) return 0;
    return read_le32(cmd->params + index * 4u);
}
