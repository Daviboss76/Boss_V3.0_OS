ENTRY(_start)
PHDRS {
    txt PT_LOAD FLAGS(5);
    dat PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x10001000;
    .text   : { *(.text.start) *(.text*) } :txt
    .rodata : { *(.rodata*) } :txt
    . = ALIGN(0x1000);
    .data   : { *(.data*) LONG(0) } :dat
    .bss    : { *(COMMON) *(.bss*) } :dat
}
