/* Overlay 4 KiB — VMA matches compile-app.sh / Core/py32f071xb.ld */
APP_VMA    = DEFINED(APP_VMA) ? APP_VMA : 0x20000280;
APP_LENGTH = 0x1000;
ENTRY(app_main)
MEMORY { APP (rwx) : ORIGIN = APP_VMA, LENGTH = APP_LENGTH }
SECTIONS {
    .app APP_VMA : {
        KEEP(*(.text.entry))
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
           "Sonde M20 overlay overflows 4 KiB")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.comment) *(.note.*) }
}
