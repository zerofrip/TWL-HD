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
 * For normal service requests (COMM, SEND_BYTE, FINISH_UPLOAD, EXEC) the
 * expected status byte is RTCOM_STATUS_DONE (0x80).  The round-trip echo
 * request uses RTCOM_OP_ECHO: the service calls the ARM11 ucode with
 *   r0 = parameter byte from register 112
 *   r1 = request opcode from register 113
 * and the value the ucode returns in r0 is written back as the status byte.
 * Therefore an ECHO exchange with parameter B returns ~B as its status byte in
 * the SAME single-ack read; there is no second IRQ wait.
 */

/* RTC command opcodes exchanged with the TwlBg RTCom service. */
#define RTCOM_OP_COMM          0x41u
#define RTCOM_OP_SEND_BYTE     0x42u
#define RTCOM_OP_ECHO          0x43u
#define RTCOM_OP_FINISH_UPLOAD 0x44u
#define RTCOM_OP_EXEC          0x81u
#define RTCOM_OP_STATUS        0x83u
#define RTCOM_OP_DONE          0xFEu

/* Status byte values returned by the RTCom service. */
#define RTCOM_STATUS_IDLE 0x00u
#define RTCOM_STATUS_DONE 0x80u
#define RTCOM_STATUS_BUSY 0x82u

typedef enum {
    RTCOM_OK                 = 0,
    RTCOM_ERR_TIMEOUT        = 1,
    RTCOM_ERR_NOT_READY      = 2,
    RTCOM_ERR_UPLOAD_FAILED  = 3,
} RtcomStatus;

/* Test message for Phase 3B round-trip (8 bytes; NOT TwlhdFrame). */
typedef struct {
    u8 magic[4];   /* "TWLH" */
    u8 payload[4]; /* arbitrary test bytes */
} RtcomTestMsg;
#define RTCOM_TEST_MAGIC  { 'T', 'W', 'L', 'H' }

RtcomStatus rtcom_init(void);
RtcomStatus rtcom_send_request(u8 request, u8 param);
RtcomStatus rtcom_send_request_status(u8 request, u8 param, u8 *out_status);
RtcomStatus rtcom_poll_ack(u32 timeout_iters, u8 *out_status);
RtcomStatus rtcom_upload_ucode(const u8 *blob, u32 len);
RtcomStatus rtcom_execute_ucode(void);

#endif /* RTCOM_DRIVER_H */
