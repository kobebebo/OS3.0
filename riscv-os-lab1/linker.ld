OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ram (rxw) : ORIGIN = 0x80200000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(ram);

    .text : {
        *(.text .text.*)
    } > ram

    .rodata : {
        *(.rodata .rodata.*)
    } > ram

    .data : {
        *(.data .data.*)
    } > ram

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > ram

    . = ALIGN(16);
    __stack_bottom = .;
    . += 16K;
    . = ALIGN(16);
    stack_top = .;
}
