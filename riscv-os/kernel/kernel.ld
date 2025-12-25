OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
  /* QEMU virt 上的内核加载地址 */
  . = 0x80000000;

  .text : ALIGN(4)
  {
    *(.text .text.*)
    PROVIDE(etext = .);     /* 代码段结束 */
  }

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4)
  {
    *(.data .data.*)
  }

  .bss : ALIGN(16)
  {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  }

  /* 整个内核（含 BSS 和栈）结束位置 */
  PROVIDE(kernel_end = .);

  /* 丢弃调试段，减小体积 */
  /DISCARD/ :
  {
    *(.eh_frame)
    *(.comment)
  }
}
