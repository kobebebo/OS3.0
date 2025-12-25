#ifndef _CONSOLE_H_
#define _CONSOLE_H_

// 初始化控制台（内部会调用 uart_init）
void console_init(void);

// 输出单个字符到控制台（最终会走到 UART）
void console_putc(int c);

// 输出一个以 '\0' 结尾的字符串
void console_puts(const char *s);

// 使用 ANSI 转义序列清屏，并把光标移动到左上角
void clear_screen(void);

#endif
