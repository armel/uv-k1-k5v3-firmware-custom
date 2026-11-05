/* Overlay-app link script (POC).
 *
 * The blob is linked to run at the PY25Q16 sector-cache overlay VMA (shared with
 * the multiboot RAM stub — never both active at once). Code + rodata + data +
 * bss must all fit in the 4 KiB overlay. objcopy -O binary emits text+rodata+
 * data only; the loader zeroes the overlay before copying, so bss starts clean.
 *
 * VMA must match ADDR(.mb_workspace) in Core/py32f071xb.ld. If the firmware RAM
 * layout shifts, update APP_VMA and the loader's compile-time assert catches a
 * mismatch. */

/* APP_VMA is the target firmware's __mb_workspace_start (read from its .map). It
 * varies with the RAM layout, so pass it from the build: -Wl,--defsym,APP_VMA=0x...
 * The default is only a fallback for a standalone size check. */
APP_VMA    = DEFINED(APP_VMA) ? APP_VMA : 0x20000280;
APP_LENGTH = 0x1000;       /* 4 KiB sector-cache overlay                        */

ENTRY(app_main)

MEMORY {
    APP (rwx) : ORIGIN = APP_VMA, LENGTH = APP_LENGTH
}

SECTIONS {
    .app APP_VMA : {
        KEEP(*(.text.entry))      /* app_main pinned to offset 0 */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
        __app_bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __app_bss_end = .;
    } > APP

    __app_end = .;
    ASSERT(__app_end <= APP_VMA + APP_LENGTH,
           "Beacon overlay app overflows the 4 KiB overlay")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.comment) *(.note.*) }
}
