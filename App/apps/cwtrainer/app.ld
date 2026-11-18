APP_VMA = DEFINED(APP_VMA) ? APP_VMA : 0x20000280;
ENTRY(app_main)
MEMORY { APP (rwx) : ORIGIN = APP_VMA, LENGTH = 0x1000 }
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
    ASSERT(__app_end <= APP_VMA + 0x1000, "CW Trainer exceeds 4096 bytes")
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.comment) *(.note.*) }
}
