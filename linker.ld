ENTRY(_start)
SECTIONS {
    . = 0x100000;
    __kernel_start = .;
    .text   : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    . = ALIGN(4096);
    __guard_end = .;
    .data   : { *(.data*) }
    .bss    : { __bss_start = .; *(COMMON) *(.bss*) __bss_end = .; }
}
