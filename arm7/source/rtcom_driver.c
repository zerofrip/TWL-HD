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

#define RTCOM_MAX_UCODE_SIZE (60u * 1024u)

/* Saved bus state for an exclusive RTCom exchange.  The reference beginComm
 * assumes no other code touches REG_RCNT, REG_RTC, or the IRQ_NETWORK bit
 * while a multi-step exchange is in flight, so the driver enters a critical
 * section and restores REG_RCNT when the exchange ends. */
typedef struct {
    u16 rcnt;
    int ime;
} RtcomBusState;

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

static void rtcom_configure_rcnt(void) {
    /* Write the exact reference value (0x8100) rather than preserving stale
     * GPIO direction bits across repeated probe/upload sessions. */
    RTCOM_REG_RCNT = RCNT_MODE_GPIO | RCNT_SI_IRQ_ENABLE;
}

static void rtcom_bus_lock(RtcomBusState *state) {
    /* Disable interrupts for the duration of the exchange so no other code
     * can bit-bang the RTC bus or observe a partial RTCom state. */
    state->ime = enterCriticalSection();

    /* Save REG_RCNT so it can be restored exactly after the exchange. */
    state->rcnt = RTCOM_REG_RCNT;

    /* Acknowledge any stale IRQ_NETWORK flag before the first request, so a
     * left-over interrupt cannot be mistaken for a fresh acknowledge. */
    REG_IF = RTCOM_IRQ_NETWORK;

    rtcom_configure_rcnt();

    /* Enabling the SI/RTC IRQ path can itself raise IRQ_NETWORK as a side
     * effect of turning the mode on.  Clear it again so that self-raised flag
     * is not mistaken for the probe's real acknowledge. */
    REG_IF = RTCOM_IRQ_NETWORK;
}

static void rtcom_bus_unlock(const RtcomBusState *state) {
    /* Drop any pending IRQ_NETWORK flag before restoring RCNT, so it cannot
     * fire once interrupts are re-enabled. */
    REG_IF = RTCOM_IRQ_NETWORK;

    RTCOM_REG_RCNT = state->rcnt;
    leaveCriticalSection(state->ime);
}

static RtcomStatus rtcom_poll_ack(u32 timeout_iters, u8 *out_status) {
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

    /* A leftover IRQ_NETWORK flag from the exchange that timed out must not
     * be visible to the next poll. */
    REG_IF = RTCOM_IRQ_NETWORK;

    return RTCOM_ERR_TIMEOUT;
}

static RtcomStatus rtcom_send_request_status(u8 request, u8 param,
                                             u8 *out_status) {
    /* Register 112 receives the parameter byte; register 113 receives the
     * request/trigger byte and is written last so the service latches both. */
    rtcom_write_byte(RTCOM_RTC_WRITE_112, param);
    rtcom_write_byte(RTCOM_RTC_WRITE_113, request);

    return rtcom_poll_ack(RTCOM_ACK_TIMEOUT, out_status);
}

static RtcomStatus rtcom_send_request(u8 request, u8 param) {
    u8 status = 0;
    RtcomStatus st;

    st = rtcom_send_request_status(request, param, &status);
    if (st != RTCOM_OK) {
        return st;
    }

    if (status != RTCOM_STATUS_ACK) {
        return RTCOM_ERR_NOT_READY;
    }

    return RTCOM_OK;
}

/* Single-argument request variants write only register 113, matching the
 * reference's requestAsync(request) / request(request) overloads.  No
 * register-112 write is performed at all. */
static RtcomStatus rtcom_send_request_113_status(u8 request, u8 *out_status) {
    rtcom_write_byte(RTCOM_RTC_WRITE_113, request);

    return rtcom_poll_ack(RTCOM_ACK_TIMEOUT, out_status);
}

static RtcomStatus rtcom_send_request_113(u8 request) {
    u8 status = 0;
    RtcomStatus st;

    st = rtcom_send_request_113_status(request, &status);
    if (st != RTCOM_OK) {
        return st;
    }

    if (status != RTCOM_STATUS_ACK) {
        return RTCOM_ERR_NOT_READY;
    }

    return RTCOM_OK;
}

static RtcomStatus rtcom_send_request_113_done(u8 request) {
    u8 status = 0;
    RtcomStatus st;

    st = rtcom_send_request_113_status(request, &status);
    if (st != RTCOM_OK) {
        return st;
    }

    if (status != RTCOM_STATUS_DONE) {
        return RTCOM_ERR_NOT_READY;
    }

    return RTCOM_OK;
}

static void rtcom_send_async_113(u8 request) {
    /* Fire a single-argument request without waiting for its acknowledge.
     * Used only for KEEPALIVE, where the reply is expected as the following
     * DONE status. */
    rtcom_write_byte(RTCOM_RTC_WRITE_113, request);
}

static RtcomStatus rtcom_wait_done(u32 timeout_iters) {
    u8 status = 0;
    RtcomStatus st;

    st = rtcom_poll_ack(timeout_iters, &status);
    if (st != RTCOM_OK) {
        return st;
    }

    if (status != RTCOM_STATUS_DONE) {
        return RTCOM_ERR_NOT_READY;
    }

    return RTCOM_OK;
}

RtcomStatus rtcom_init(void) {
    RtcomBusState state;
    u32 attempt;

    rtcom_bus_lock(&state);

    for (attempt = 0; attempt < RTCOM_INIT_RETRIES; attempt++) {
        if (rtcom_send_request_113_done(RTCOM_REQ_PROBE) == RTCOM_OK) {
            rtcom_bus_unlock(&state);
            return RTCOM_OK;
        }
    }

    rtcom_bus_unlock(&state);
    return RTCOM_ERR_NOT_READY;
}

static RtcomStatus rtcom_attempt_upload(const u8 *blob, u32 len,
                                        const u8 *len_bytes) {
    RtcomBusState state;
    u32 i;
    RtcomStatus st;

    rtcom_bus_lock(&state);

    st = rtcom_send_request(RTCOM_OP_UPLOAD_UCODE, len_bytes[0]);
    if (st != RTCOM_OK) {
        goto fail;
    }

    for (i = 1; i < 4; i++) {
        st = rtcom_send_request(RTCOM_OP_NEXT, len_bytes[i]);
        if (st != RTCOM_OK) {
            goto fail;
        }
    }

    for (i = 0; i < len; i++) {
        st = rtcom_send_request(RTCOM_OP_NEXT, blob[i]);
        if (st != RTCOM_OK) {
            goto fail;
        }
    }

    /* After the last payload byte the reference sends KEEPALIVE without
     * waiting for ACK, then polls for DONE. */
    rtcom_send_async_113(RTCOM_OP_KEEPALIVE);

    st = rtcom_wait_done(RTCOM_ACK_TIMEOUT);
    if (st != RTCOM_OK) {
        goto fail;
    }

    st = rtcom_send_request_113(RTCOM_OP_FINISH_UCODE);
    if (st != RTCOM_OK) {
        goto fail;
    }

    rtcom_bus_unlock(&state);
    return RTCOM_OK;

fail:
    rtcom_bus_unlock(&state);
    return st;
}

RtcomStatus rtcom_upload_ucode(const u8 *blob, u32 len) {
    u8 len_bytes[4];
    u32 attempt;

    if (len == 0 || len > RTCOM_MAX_UCODE_SIZE) {
        return RTCOM_ERR_BAD_ARG;
    }

    if (!blob) {
        return RTCOM_ERR_UPLOAD_FAILED;
    }

    len_bytes[0] = (u8)(len & 0xFFu);
    len_bytes[1] = (u8)((len >> 8) & 0xFFu);
    len_bytes[2] = (u8)((len >> 16) & 0xFFu);
    len_bytes[3] = (u8)((len >> 24) & 0xFFu);

    /* Retry the WHOLE upload attempt, not individual steps.  Resending a
     * single step after the service has already shifted its receive cursor
     * would corrupt the blob position, so a failed attempt must restart from
     * UPLOAD_UCODE. */
    for (attempt = 0; attempt < RTCOM_UPLOAD_RETRIES; attempt++) {
        if (rtcom_attempt_upload(blob, len, len_bytes) == RTCOM_OK) {
            return RTCOM_OK;
        }
    }

    return RTCOM_ERR_UPLOAD_FAILED;
}

RtcomStatus rtcom_execute_ucode(u8 param) {
    RtcomBusState state;
    RtcomStatus st;

    rtcom_bus_lock(&state);

    st = rtcom_send_request(RTCOM_OP_EXECUTE_UCODE, param);

    rtcom_bus_unlock(&state);
    return st;
}
