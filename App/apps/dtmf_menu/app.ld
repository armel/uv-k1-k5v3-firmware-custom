/* Overlay-app link script for DTMF Menu */

APP_VMA    = DEFINED(APP_VMA) ? APP_VMA : 0x20000280;
APP_LENGTH = 0x1000;       /* 4 KiB sector-cache overlay */

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
           "DTMF Menu overlay app overflows the 4 KiB overlay")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.comment) *(.note.*) }
}

