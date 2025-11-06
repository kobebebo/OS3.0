#include "uart.h"

/*
 * 16550A UART 寄存器偏移，单位为字节。
 */
#define UART_RHR 0x00  /* 接收缓冲寄存器（只读） */
#define UART_THR 0x00  /* 发送保持寄存器（只写） */
#define UART_IER 0x01  /* 中断使能寄存器 */
#define UART_FCR 0x02  /* FIFO 控制寄存器 */
#define UART_LCR 0x03  /* 线路控制寄存器 */
#define UART_LSR 0x05  /* 线路状态寄存器 */
#define UART_LCR_DLAB 0x80
#define UART_LSR_DR   0x01
#define UART_LSR_THRE 0x20

static inline void mmio_write(uintptr_t addr, uint8_t value) {
    *(volatile uint8_t *)addr = value;
}

static inline uint8_t mmio_read(uintptr_t addr) {
    return *(volatile uint8_t *)addr;
}

void uart_init(void) {
    /*
     * 关闭所有中断，配置波特率除数（假设输入时钟 1.8432MHz，设置为 115200），
     * 启用 FIFO，并设置 8N1 格式。
     */
    mmio_write(UART0_BASE + UART_IER, 0x00);

    /* 进入 DLAB 模式设置波特率，除数 = 1 */
    mmio_write(UART0_BASE + UART_LCR, UART_LCR_DLAB);
    mmio_write(UART0_BASE + UART_THR, 0x01); /* DLL */
    mmio_write(UART0_BASE + UART_IER, 0x00); /* DLM */

    /* 启用 FIFO */
    mmio_write(UART0_BASE + UART_FCR, 0x07);

    /* 设置 8 位数据、1 位停止位、无校验，并关闭 DLAB */
    mmio_write(UART0_BASE + UART_LCR, 0x03);
}

void uart_putc(char ch) {
    /* 等待发送保持寄存器为空 */
    while ((mmio_read(UART0_BASE + UART_LSR) & UART_LSR_THRE) == 0) {
    }
    mmio_write(UART0_BASE + UART_THR, (uint8_t)ch);
}

char uart_getc(void) {
    /* 轮询等待接收数据就绪 */
    while ((mmio_read(UART0_BASE + UART_LSR) & UART_LSR_DR) == 0) {
    }
    return (char)mmio_read(UART0_BASE + UART_RHR);
}
