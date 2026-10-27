// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or
// Analog-Controls-for-NDS-Games-on-3DS. See docs/RTCOM_AUDIT.md §Licensing.

#include <nds.h>

#include "rtcom_driver.h"

/* 3-wire RTC bus at REG_RCNT_RTC (0x4000138).
 *
 * Pin mapping and direction bits are taken from Calico's
 * calico/nds/arm7/gpio.h:
 *   bit 0 = SIO (bidirectional data)
 *   bit 1 = SCK (clock)
 *   bit 2 = CS  (chip select)
 *   bit 4 = SIO direction (1 = output)
 *   bit 5 = SCK direction (1 = output)
 *   bit 6 = CS  direction (1 = output)
 *
 * The reference rtcom.cpp uses the same layout (CS_0/CS_1, SCK_0/SCK_1,
 * SIO_0/SIO_1 macros) and shifts every byte out MSB-first, including both
 * command bytes and data bytes.  We therefore send command and data MSB-first.
 * The function name rtcTransferReversed() in the reference is misleading; the
 * comments and loop inside it are explicit about "high bit first".
 */
#define RTCOM_REG_RTC  REG_RCNT_RTC
#define RTCOM_REG_RCNT REG_RCNT

#define RTCOM_PIN_SIO 0x0001u
#define RTCOM_PIN_SCK 0x0002u
#define RTCOM_PIN_CS  0x0004u

#define RTCOM_PIN_SIO_OUT 0x0010u
#define RTCOM_PIN_SCK_OUT 0x0020u
#define RTCOM_PIN_CS_OUT  0x0040u
#define RTCOM_DIR_OUT     (RTCOM_PIN_SIO_OUT | RTCOM_PIN_SCK_OUT | RTCOM_PIN_CS_OUT)

/* RTC command bytes; registers 112 (0x70) and 113 (0x71) carry the parameter
 * and request/status bytes of the RTCom mailbox.  Register 113 is written last
 * and acts as the trigger; register 112 holds the parameter byte.  This order
 * matches the reference's rtcom_requestAsync(u8 request, u8 param), which writes
 * param to register 112 and request to register 113. */
#define RTCOM_RTC_WRITE_112 0x6Cu
#define RTCOM_RTC_READ_112  0x6Du
#define RTCOM_RTC_WRITE_113 0x6Eu
#define RTCOM_RTC_READ_113  0x6Fu

/* IRQ_NETWORK is IRQ_RTC in libnds 2.0/Calico (calico/nds/irq.h: IRQ_RTC = 1U<<7).
 * Use the macro from the installed header rather than a hard-coded bit number. */
#define RTCOM_IRQ_NETWORK IRQ_NETWORK

#define RTCOM_BIT_DELAY_SLOW 48u
#define RTCOM_BIT_DELAY_FAST 14u
#define RTCOM_ACK_TIMEOUT    2062500u
#define RTCOM_INIT_RETRIES   10u
#define RTCOM_UPLOAD_RETRIES 10u

static void rtcom_delay(u32 iters) {
    volatile u32 i;

    for (i = 0; i < iters; i++) {
        /* Bus timing is loop-count based on the DS ARM7; nothing to do. */
    }
}

static void rtcom_begin(void) {
    u16 base = (RTCOM_REG_RTC & ~(RTCOM_DIR_OUT | RTCOM_PIN_CS |
                                  RTCOM_PIN_SCK | RTCOM_PIN_SIO)) |
               RTCOM_DIR_OUT;

    /* Raise CS while SCK and SIO are high, matching the reference begin sequence. */
    RTCOM_REG_RTC = base | RTCOM_PIN_CS_OUT | RTCOM_PIN_CS |
                    RTCOM_PIN_SCK_OUT | RTCOM_PIN_SCK |
                    RTCOM_PIN_SIO_OUT | RTCOM_PIN_SIO;
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
}

static void rtcom_end(void) {
    /* Drop CS low, leave SCK high (direction bits remain outputs). */
    RTCOM_REG_RTC = (RTCOM_REG_RTC & ~RTCOM_PIN_CS) |
                    RTCOM_PIN_CS_OUT | RTCOM_PIN_SCK_OUT | RTCOM_PIN_SCK;
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
}

static void rtcom_put_bit(u8 bit) {
    u16 line = (RTCOM_REG_RTC & ~(RTCOM_PIN_SIO | RTCOM_PIN_SCK)) |
               RTCOM_PIN_CS_OUT | RTCOM_PIN_CS |
               RTCOM_PIN_SCK_OUT | RTCOM_PIN_SIO_OUT;

    if (bit) {
        line |= RTCOM_PIN_SIO;
    }

    RTCOM_REG_RTC = line;
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
    RTCOM_REG_RTC = (u16)(line | RTCOM_PIN_SCK);
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
}

static u8 rtcom_get_bit(void) {
    /* SIO_OUT must be clear here: SIO is an input while the RTC chip drives
     * the data.  The write path (rtcom_put_bit) keeps SIO_OUT set. */
    u16 line = (RTCOM_REG_RTC & ~(RTCOM_PIN_SIO | RTCOM_PIN_SCK |
                                  RTCOM_PIN_SIO_OUT)) |
               RTCOM_PIN_CS_OUT | RTCOM_PIN_CS |
               RTCOM_PIN_SCK_OUT;
    u8 bit;

    RTCOM_REG_RTC = line;
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
    RTCOM_REG_RTC = (u16)(line | RTCOM_PIN_SCK);
    rtcom_delay(RTCOM_BIT_DELAY_SLOW);
    bit = (RTCOM_REG_RTC & RTCOM_PIN_SIO) ? 1u : 0u;

    return bit;
}

static void rtcom_write_byte(u8 cmd, u8 val) {
    int i;

    rtcom_begin();

    for (i = 7; i >= 0; i--) {
        rtcom_put_bit((u8)((cmd >> i) & 1u));
    }

    for (i = 7; i >= 0; i--) {
        rtcom_put_bit((u8)((val >> i) & 1u));
    }

    rtcom_end();
}

static void rtcom_read_byte(u8 cmd, u8 *val) {
    u8 out = 0;
    int i;

    rtcom_begin();

    for (i = 7; i >= 0; i--) {
        rtcom_put_bit((u8)((cmd >> i) & 1u));
    }

    for (i = 0; i < 8; i++) {
        out = (u8)((out << 1) | rtcom_get_bit());
    }

    rtcom_end();

    if (val) {
        *val = out;
    }
}

RtcomStatus rtcom_poll_ack(u32 timeout_iters, u8 *out_status) {
    u32 i;

    for (i = 0; i < timeout_iters; i++) {
        if (REG_IF & RTCOM_IRQ_NETWORK) {
            u8 status = 0;

            REG_IF = RTCOM_IRQ_NETWORK;
            rtcom_read_byte(RTCOM_RTC_READ_113, &status);

            if (out_status) {
                *out_status = status;
            }

            return RTCOM_OK;
        }
    }

    return RTCOM_ERR_TIMEOUT;
}

RtcomStatus rtcom_send_request_status(u8 request, u8 param, u8 *out_status) {
    /* Register 112 receives the parameter byte; register 113 receives the
     * request/trigger byte and is written last so the service latches both. */
    rtcom_write_byte(RTCOM_RTC_WRITE_112, param);
    rtcom_write_byte(RTCOM_RTC_WRITE_113, request);

    return rtcom_poll_ack(RTCOM_ACK_TIMEOUT, out_status);
}

RtcomStatus rtcom_send_request(u8 request, u8 param) {
    u8 status = 0;
    RtcomStatus st;

    st = rtcom_send_request_status(request, param, &status);
    if (st != RTCOM_OK) {
        return st;
    }

    if (status != RTCOM_STATUS_DONE) {
        return RTCOM_ERR_NOT_READY;
    }

    return RTCOM_OK;
}

RtcomStatus rtcom_init(void) {
    u32 attempt;

    /* Enable the RTC/network IRQ path and select GPIO mode on RCNT, per
     * Calico (RCNT_SI_IRQ_ENABLE = bit 8, RCNT_MODE_GPIO = bit 15). */
    RTCOM_REG_RCNT = (RTCOM_REG_RCNT & ~RCNT_MODE_MASK) |
                     RCNT_MODE_GPIO | RCNT_SI_IRQ_ENABLE;

    for (attempt = 0; attempt < RTCOM_INIT_RETRIES; attempt++) {
        if (rtcom_send_request(RTCOM_OP_COMM, 0) == RTCOM_OK) {
            return RTCOM_OK;
        }
    }

    return RTCOM_ERR_NOT_READY;
}

RtcomStatus rtcom_upload_ucode(const u8 *blob, u32 len) {
    u32 i;

    if (!blob && len != 0) {
        return RTCOM_ERR_UPLOAD_FAILED;
    }

    for (i = 0; i < len; i++) {
        RtcomStatus st = RTCOM_ERR_UPLOAD_FAILED;
        u32 attempt;

        for (attempt = 0; attempt < RTCOM_UPLOAD_RETRIES; attempt++) {
            st = rtcom_send_request(RTCOM_OP_SEND_BYTE, blob[i]);
            if (st == RTCOM_OK) {
                break;
            }
        }

        if (st != RTCOM_OK) {
            return RTCOM_ERR_UPLOAD_FAILED;
        }
    }

    if (rtcom_send_request(RTCOM_OP_FINISH_UPLOAD, 0) != RTCOM_OK) {
        return RTCOM_ERR_UPLOAD_FAILED;
    }

    return RTCOM_OK;
}

RtcomStatus rtcom_execute_ucode(void) {
    return rtcom_send_request(RTCOM_OP_EXEC, 0);
}
