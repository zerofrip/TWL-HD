// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or
// Analog-Controls-for-NDS-Games-on-3DS. See docs/RTCOM_AUDIT.md §Licensing.

#ifndef RTCOM_DRIVER_H
#define RTCOM_DRIVER_H

#include <nds.h>

/* RTC command opcodes exchanged with the TwlBg RTCom service. */
#define RTCOM_OP_COMM          0x41u
#define RTCOM_OP_SEND_BYTE     0x42u
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
RtcomStatus rtcom_poll_ack(u32 timeout_iters, u8 *out_status);
RtcomStatus rtcom_upload_ucode(const u8 *blob, u32 len);
RtcomStatus rtcom_execute_ucode(void);

#endif /* RTCOM_DRIVER_H */
