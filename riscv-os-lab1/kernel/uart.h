#ifndef UART_H
#define UART_H

#include <stdint.h>

/* UART MMIO 基地址 */
#define UART0_BASE 0x10000000UL

void uart_init(void);
void uart_putc(char ch);
char uart_getc(void);

#endif /* UART_H */
