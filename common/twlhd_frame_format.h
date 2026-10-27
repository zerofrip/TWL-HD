#pragma once

#include <stdint.h>

/*
 * TWL-HD capture frame format (Phase 1).
 *
 * Shared between the ARM9 capture hook and the (not yet implemented) ARM11
 * transport/renderer. All fields are fixed width so the struct can be sent
 * over RTCom byte-for-byte on both cores.
 */

#define TWLHD_FORMAT_VERSION  1
#define TWLHD_RING_CAPACITY   1024   /* commands per captured frame */

/* GXFIFO command ids, see docs/GEOMETRY_FIFO.md (GBATEK 3D I/O ports). */
typedef enum {
    TWLHD_CMD_UNKNOWN      = 0x00,
    TWLHD_CMD_MTX_MODE     = 0x10,
    TWLHD_CMD_MTX_PUSH     = 0x11,
    TWLHD_CMD_MTX_POP      = 0x12,
    TWLHD_CMD_MTX_STORE    = 0x13,
    TWLHD_CMD_MTX_RESTORE  = 0x14,
    TWLHD_CMD_MTX_IDENTITY = 0x15,
    TWLHD_CMD_MTX_LOAD_4x4 = 0x16,
    TWLHD_CMD_MTX_LOAD_4x3 = 0x17,
    TWLHD_CMD_MTX_MULT_4x4 = 0x18,
    TWLHD_CMD_MTX_MULT_4x3 = 0x19,
    TWLHD_CMD_MTX_MULT_3x3 = 0x1A,
    TWLHD_CMD_COLOR        = 0x20,
    TWLHD_CMD_NORMAL       = 0x21,
    TWLHD_CMD_TEX_COORD    = 0x22,
    TWLHD_CMD_VTX_16       = 0x23,
    TWLHD_CMD_VTX_10       = 0x24,
    TWLHD_CMD_VTX_XY       = 0x25,
    TWLHD_CMD_VTX_XZ       = 0x26,
    TWLHD_CMD_VTX_YZ       = 0x27,
    TWLHD_CMD_BEGIN_VTXS   = 0x40,
    TWLHD_CMD_END_VTXS     = 0x41,
    TWLHD_CMD_VIEWPORT     = 0x60,
} TwlhdCmdType;

typedef struct {
    /* Stored as uint32_t, not TwlhdCmdType: devkitARM builds with
     * -fshort-enums, which would give the enum a 1-byte size and change the
     * wire layout. */
    uint32_t     type;        /* TwlhdCmdType value            */
    uint8_t      param_count; /* number of 32-bit params       */
    uint32_t     params[16];  /* raw parameter words           */
} TwlhdCmd;

typedef struct {
    uint32_t  version;     /* TWLHD_FORMAT_VERSION          */
    uint32_t  frame_index; /* frames captured since init    */
    uint32_t  cmd_count;   /* valid entries in cmds[]       */
    TwlhdCmd  cmds[TWLHD_RING_CAPACITY];
} TwlhdFrame;

/* Locks the wire layout: 4 byte type, 1 byte count, 3 bytes padding, 16 words. */
_Static_assert(sizeof(TwlhdCmd) == 72, "TwlhdCmd layout changed");
