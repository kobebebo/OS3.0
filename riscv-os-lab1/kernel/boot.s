    .section .text
    .globl _start

/*
 * 裸机启动入口：设置栈、清空 .bss，然后跳转到 C 入口 main。
 */
_start:
    /* 加载栈顶地址到 sp，确保 C 代码有足够栈空间 */
    la      sp, stack_top

    /* 使用 a0 保存当前要清零的位置，a1 保存结尾 */
    la      a0, __bss_start
    la      a1, __bss_end

    /* 循环写零到整个 .bss 段 */
1:
    bgeu    a0, a1, 2f          /* 如果 a0 >= a1，说明清零完成 */
    sd      zero, 0(a0)         /* 以 8 字节为步长存零 */
    addi    a0, a0, 8           /* 移动到下一个字双字 */
    j       1b                  /* 继续循环 */

2:
    /* 调用 C 入口函数 main */
    call    main

3:
    /* main 返回则保持低功耗等待中断，死循环 */
    wfi
    j       3b
