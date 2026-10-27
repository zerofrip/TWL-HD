// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or
// Analog-Controls-for-NDS-Games-on-3DS. See docs/RTCOM_AUDIT.md §Licensing.

#include <nds.h>

#include "rtcom_driver.h"

/* ARM11 ucode injection is disabled by default until the entry/return contract
 * has been independently reviewed and found safe.  When this flag is 0 the
 * driver skips rtcom_upload_ucode() / rtcom_execute_ucode() and reports
 * RTCOM_TEST_UCODE_DISABLED to the ARM9.  See docs/RTCOM_AUDIT.md and
 * docs/RTCOM_HARDWARE_TEST.md. */
#define TWLHD_RTCOM_UCODE_ENABLED 0

/* The real blob is linked in by the ucode build step. These weak, zero-length
 * definitions keep build-only verification linkable. */
__attribute__((weak)) const u8  _arm11_ucode_start[1] = { 0 };
__attribute__((weak)) const u32 _arm11_ucode_size = 0;

/* Result codes reported to the ARM9 over the PXI user channel. */
#define RTCOM_TEST_PASS          0u
#define RTCOM_TEST_DATA_MISMATCH 4u
#define RTCOM_TEST_UCODE_DISABLED 5u
#define RTCOM_TEST_RETRIES       10u

static void rtcom_report(u32 code) {
    pxiSend(PxiChannel_User1, code);
}

/* Send the 8-byte test message and check that every echo is bitwise inverted. */
static RtcomStatus rtcom_test_roundtrip(void) {
    RtcomTestMsg msg = { RTCOM_TEST_MAGIC, { 0xAA, 0xBB, 0xCC, 0xDD } };
    const u8 *raw = (const u8 *)&msg;
    u32 i;

    for (i = 0; i < sizeof(msg); i++) {
        RtcomStatus st = RTCOM_ERR_UPLOAD_FAILED;
        u32 attempt;

        for (attempt = 0; attempt < RTCOM_TEST_RETRIES; attempt++) {
            st = rtcom_send_request(RTCOM_OP_SEND_BYTE, raw[i]);
            if (st == RTCOM_OK) {
                break;
            }
        }

        if (st != RTCOM_OK) {
            return st;
        }

        {
            u8 echo = 0;

            st = rtcom_poll_ack(2062500u, &echo);
            if (st != RTCOM_OK) {
                return st;
            }

            if (echo != (u8)~raw[i]) {
                return (RtcomStatus)RTCOM_TEST_DATA_MISMATCH;
            }
        }
    }

    return RTCOM_OK;
}

int main(void) {
    RtcomStatus st;

    st = rtcom_init();
    rtcom_report((u32)st);

#if TWLHD_RTCOM_UCODE_ENABLED
    if (st == RTCOM_OK) {
        st = rtcom_upload_ucode(_arm11_ucode_start, _arm11_ucode_size);
        rtcom_report((u32)st);
    }

    if (st == RTCOM_OK) {
        st = rtcom_execute_ucode();
        rtcom_report((u32)st);
    }
#else
    rtcom_report(RTCOM_TEST_UCODE_DISABLED);
#endif

    if (st == RTCOM_OK) {
        st = rtcom_test_roundtrip();
        rtcom_report(st == RTCOM_OK ? RTCOM_TEST_PASS : (u32)st);
    }

    while (1) {
        swiWaitForVBlank();
    }

    return 0;
}
