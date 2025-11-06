#include "uart.h"
#include "printf.h"

/*
 * 主函数：初始化串口并输出若干调试信息，最后提示启动完成。
 */
void main(void) {
    uart_init();
    kprintf("[BOOT] lab1 start @0x80200000\n");
    kprintf("hello, uart\n");
    kprintf("hex=0x%x dec=%d\n", 0x1234, 0x1234);
    kprintf("boot ok\n");

    /* main 不返回，避免回到启动代码 */
    for (;;) {
        /* 进入等待中断的低功耗状态 */
        __asm__ volatile ("wfi");
    }
}
