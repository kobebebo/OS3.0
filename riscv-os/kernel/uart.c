#include "types.h"
#include "memlayout.h"
#include "uart.h"

// 16550a 寄存器偏移
#define RHR 0                 // 接收缓冲（读）
#define THR 0                 // 发送缓冲（写）
#define IER 1                 // 中断使能
#define FCR 2                 // FIFO 控制
#define LCR 3                 // 线路控制
#define LSR 5                 // 线路状态

// IER 位
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)

// FCR 位
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)

// LCR 位
#define LCR_EIGHT_BITS  (3 << 0)
#define LCR_BAUD_LATCH  (1 << 7)

// LSR 位
#define LSR_TX_IDLE     (1 << 5)   // 发送保持寄存器空

static inline unsigned char
uart_read_reg(int reg)
{
    return *(volatile unsigned char *)(UART0 + reg);
}

static inline void
uart_write_reg(int reg, unsigned char value)
{
    *(volatile unsigned char *)(UART0 + reg) = value;
}

void
uart_init(void)
{
    // 1. 先关掉所有中断
    uart_write_reg(IER, 0x00);

    // 2. 打开 DLAB，设置波特率除数
    uart_write_reg(LCR, LCR_BAUD_LATCH);

    // 波特率：38400（假设 1.8432MHz 时钟）
    uart_write_reg(0, 0x03);   // 除数低 8 位
    uart_write_reg(1, 0x00);   // 除数高 8 位

    // 3. 8 位数据，无奇偶校验，1 个停止位，退出 DLAB
    uart_write_reg(LCR, LCR_EIGHT_BITS);

    // 4. 启用 FIFO 并清空
    uart_write_reg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

    // 这里我们不打开 UART 中断，用轮询方式发送即可
}

void
uart_putc(char c)
{
    // 等待 THR 空
    while ((uart_read_reg(LSR) & LSR_TX_IDLE) == 0)
        ;

    uart_write_reg(THR, (unsigned char)c);
}

void
uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');   // QEMU 里用 CRLF 效果更好
        uart_putc(*s++);
    }
}
