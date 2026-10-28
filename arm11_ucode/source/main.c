// SPDX-License-Identifier: Apache-2.0
// Clean-room implementation — no code derived from RtcPwalker or Analog-Controls.
//
// IMPORTANT: This blob is uploaded at runtime into a reserved window
// (base 0x00201000, max 60 KiB) inside the TwlBg.cxi process under TWL_FIRM.
// It is NOT a standalone executable. It cannot be run or tested independently
// by this project's own tooling. Hardware testing requires:
//   - A TWPatch-generated TwlBg.cxi with RTCom enabled
//   - A DS title with the ARM7 patch injected via nds-bootstrap/generate_patch.py
// See docs/RTCOM_HARDWARE_TEST.md for the full test procedure.
//
// HARDWARE UNVERIFIED: this code has not been executed on real hardware.
//
// The entry point is _start in source/ucode_entry.s, which calls this C
// function with the reference contract:
//   int handleCommand(u8 param, u32 stage)
// The function must return an int in r0, must not loop forever, and must
// tolerate any param/stage value by returning a safe default (0).

typedef unsigned char  u8;
typedef unsigned int   u32;
typedef int            s32;

int handleCommand(u8 param, u32 stage) {
    /* The RTCom service dispatches requests to this handler with
     *   r0 = parameter byte (register 112)
     *   r1 = call-sequence stage within one logical exchange.
     * The return value in r0 becomes the data/status written back by the
     * service.  Stage 0 is the first call after EXECUTE_UCODE dispatches to
     * the blob; stage 1 is the second call (typically a NEXT follow-up used
     * to return data via register 112).
     *
     * Ucode injection and the round-trip echo test are currently gated off in
     * arm7/source/main.c, so this stub returns a safe default for every input
     * rather than implementing a stage-dependent payload. */
    (void)param;
    (void)stage;
    return 0;
}
