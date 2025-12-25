    .section .text
    .globl _start
_start:
    # 1. 清零 BSS 段 [__bss_start, __bss_end)
    la   t0, __bss_start
    la   t1, __bss_end

    bgeu t0, t1, 1f          # 如果没有 BSS，直接跳过
0:
    sb   zero, 0(t0)         # 清 1 字节
    addi t0, t0, 1
    bltu t0, t1, 0b
1:
    # 2. 设置栈指针
    la   sp, stack_top

    # 3. 跳转到 C 代码的 start()，在 M 模式下做初始化后 mret 到 S 模式 main()
    call start

2:
    j 2b

    # 4. 简单给栈留一块空间
    .section .bss
    .align 16
    .globl stack_bottom
stack_bottom:
    .space 4096              # 4KB 栈
    .globl stack_top
stack_top:
