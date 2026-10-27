// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or
// Analog-Controls-for-NDS-Games-on-3DS. See docs/RTCOM_AUDIT.md §Licensing.

#ifndef RTCOM_DRIVER_H
#define RTCOM_DRIVER_H

#include <nds.h>

/*
 * Command exchange contract (single-byte mailbox, one IRQ per exchange).
 *
 * The ARM7 writes a parameter byte to RTC register 112 and a request/opcode
 * byte to RTC register 113; register 113 is written last and acts as the
 * trigger.  The TwlBg RTCom service acknowledges by raising IRQ_NETWORK; the
 * ARM7 then reads the status byte from register 113.
 *
 * The opcode and status values below are hardware/protocol facts cited from
 * the reference implementation at
 *   /home/one/3ds/RtcPwalker/arm7_rtcom_patch/arm7/source/rtcom.h
 * (GPLv3).  Only the numeric values are reused; the names, comments, and
 * control flow are original to TWL-HD.
 */

/* Raw probe literal used only by rtcom_init().  This is the value the
 * reference sends during beginComm; it is intentionally NOT one of the named
 * service opcodes below. */
#define RTCOM_REQ_PROBE 0x01u

/* RTC command opcodes exchanged with the TwlBg RTCom service. */
#define RTCOM_OP_KIL            0x00u
#define RTCOM_OP_UPLOAD_UCODE   0x41u
#define RTCOM_OP_FINISH_UCODE   0x42u
#define RTCOM_OP_EXECUTE_UCODE  0x44u
#define RTCOM_OP_NEXT           0x81u
#define RTCOM_OP_KEEPALIVE      0x83u
#define RTCOM_OP_SYNC_KIL       0xFEu

/* Status byte values returned by the RTCom service. */
#define RTCOM_STATUS_READY 0x00u
#define RTCOM_STATUS_ACK   0x80u
#define RTCOM_STATUS_DONE  0x82u

typedef enum {
    RTCOM_OK                 = 0,
    RTCOM_ERR_TIMEOUT        = 1,
    RTCOM_ERR_NOT_READY      = 2,
    RTCOM_ERR_UPLOAD_FAILED  = 3,
    RTCOM_ERR_BAD_ARG        = 4,
} RtcomStatus;

/* Test message for Phase 3B round-trip (8 bytes; NOT TwlhdFrame). */
typedef struct {
    u8 magic[4];   /* "TWLH" */
    u8 payload[4]; /* arbitrary test bytes */
} RtcomTestMsg;
#define RTCOM_TEST_MAGIC  { 'T', 'W', 'L', 'H' }

RtcomStatus rtcom_init(void);
RtcomStatus rtcom_upload_ucode(const u8 *blob, u32 len);
RtcomStatus rtcom_execute_ucode(u8 param);

#endif /* RTCOM_DRIVER_H */
