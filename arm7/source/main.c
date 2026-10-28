// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or
// Analog-Controls-for-NDS-Games-on-3DS. See docs/RTCOM_AUDIT.md §Licensing.

#include <nds.h>

#include "rtcom_driver.h"

/* ARM11 ucode injection is disabled by default until the entry/return contract
 * has been independently reviewed and found safe.  When this flag is 0 the
 * driver skips rtcom_upload_ucode() / rtcom_execute_ucode() and the round-trip
 * echo test, reporting RTCOM_TEST_UCODE_DISABLED /
 * RTCOM_TEST_ROUNDTRIP_SKIPPED to the ARM9.  See docs/RTCOM_AUDIT.md and
 * docs/RTCOM_HARDWARE_TEST.md.
 *
 * INVARIANT: with TWLHD_RTCOM_UCODE_ENABLED at 0, the only RTCom traffic
 * emitted by a full main() run is the rtcom_init() probe (RTCOM_REQ_PROBE,
 * 0x01); the ARM9 status report uses PXI and sends nothing to RTCom.  No
 * UPLOAD_UCODE, FINISH_UCODE, EXECUTE_UCODE, NEXT, or KEEPALIVE request is
 * ever sent. */
#define TWLHD_RTCOM_UCODE_ENABLED 0

/* The real blob is linked in by the ucode build step. These weak, zero-length
 * definitions keep build-only verification linkable. */
__attribute__((weak)) const u8  _arm11_ucode_start[1] = { 0 };
__attribute__((weak)) const u32 _arm11_ucode_size = 0;

/* Result codes reported to the ARM9 over the PXI user channel. */
#define RTCOM_TEST_PASS             0u
#define RTCOM_TEST_DATA_MISMATCH    4u
#define RTCOM_TEST_UCODE_DISABLED   5u
#define RTCOM_TEST_ROUNDTRIP_SKIPPED 6u

static void rtcom_report(u32 code) {
    pxiSend(PxiChannel_User1, code);
}

/* Round-trip echo test.
 *
 * The real protocol does not have a dedicated ECHO opcode.  Per the reference
 * handleCommand1 contract the round-trip value is retrieved with an
 * EXECUTE_UCODE(param) exchange followed by a NEXT exchange and a read of
 * register 112.  That design is not wired up here, so the test is deliberately
 * skipped in both the gate-off and gate-on configurations pending further
 * protocol work. */
static RtcomStatus rtcom_test_roundtrip(void) {
    return (RtcomStatus)RTCOM_TEST_ROUNDTRIP_SKIPPED;
}

int main(void) {
    RtcomStatus st;

    st = rtcom_init();
    rtcom_report((u32)st);

#if TWLHD_RTCOM_UCODE_ENABLED
    /* HARDWARE UNVERIFIED: the reference keeps the probe and the upload in one
     * continuous bus-lock / IRQ session.  This codebase ends the session inside
     * rtcom_init() and starts a new one in rtcom_upload_ucode(), briefly
     * re-enabling interrupts between the two.  That creates a small window in
     * which an in-progress exchange could in principle be disturbed.  The
     * default gate-off run is unaffected because the upload is never called.
     * See docs/RTCOM_AUDIT.md §Command Exchange Model. */
    if (st == RTCOM_OK) {
        st = rtcom_upload_ucode(_arm11_ucode_start, _arm11_ucode_size);
        rtcom_report((u32)st);
    }

    if (st == RTCOM_OK) {
        st = rtcom_execute_ucode(0);
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
