/* Overlay app linked into the 4 KiB PY25Q16 sector-cache workspace. */
APP_VMA    = DEFINED(APP_VMA) ? APP_VMA : 0x20000280;
APP_LENGTH = 0x1000;

ENTRY(app_main)

MEMORY {
    APP (rwx) : ORIGIN = APP_VMA, LENGTH = APP_LENGTH
}

SECTIONS {
    .app APP_VMA : {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
    } > APP

    __app_end = .;
    ASSERT(__app_end <= APP_VMA + APP_LENGTH,
           "Space Impact overlay app overflows the 4 KiB overlay")

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.comment) *(.note.*) }
}
