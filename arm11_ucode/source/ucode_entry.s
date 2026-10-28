/* SPDX-License-Identifier: Apache-2.0
 *
 * Original entry stub for the TWL-HD ARM11 ucode blob.
 *
 * Calling convention (verified against reference ucode_entry.s files in
 * RtcPwalker and Analog-Controls-for-NDS-Games-on-3DS):
 *   - r0 = param byte (first argument)
 *   - r1 = stage word (second argument)
 *   - r0 = return value on exit
 *   - r1-r12 and lr are preserved across the call
 *   - the stub returns to its caller (no infinite loop)
 *
 * The stub relies on the stack pointer already being valid in the TwlBg
 * context; push/pop are used only to save/restore caller state.
 */

.section ".text.entry", "ax"
.global _start
.align 4
.arm

_start:
    push {r1-r12, lr}
    bl   handleCommand
    pop  {r1-r12, pc}
