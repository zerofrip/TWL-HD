/* SPDX-License-Identifier: Apache-2.0
 *
 * Linker script for the TWL-HD ARM11 ucode blob.
 * ORIGIN and LENGTH are hardware facts: TwlBg.cxi reserves a 60 KiB
 * execution window starting at 0x00201000 for RTCom ucode payloads.
 * This is NOT a coincidence of the reference project — it is the fixed
 * address where the ARM7 driver uploads the blob.
 */
ENTRY(_start)

MEMORY {
    UCODE (rwx) : ORIGIN = 0x00201000, LENGTH = 60K
}

SECTIONS {
    .text   : {
        KEEP(*(.text.entry))
        *(.text*)
    } > UCODE
    .rodata : { *(.rodata*) } > UCODE
    .data   : { *(.data*)   } > UCODE
    .bss    : { *(.bss*)    } > UCODE
}
