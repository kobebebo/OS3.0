#include "types.h"
#include "uart.h"
#include "console.h"

#define BACKSPACE 0x100

// 输出一个字符到控制台
void console_putc(int c)
{
    if (c == BACKSPACE) {
        // 模拟终端的退格效果：\b + 空格 + \b
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
    } else {
        // 这里不做换行转换，直接交给 UART 发送
        uart_putc((char)c);
    }
}

// 输出一个以 '\0' 结尾的字符串
void console_puts(const char *s)
{
    while (*s) {
        console_putc((unsigned char)*s++);
    }
}

// 使用 ANSI 转义序列清屏并把光标放到左上角
// \x1b 就是 ESC
// [2J 清屏，[H 光标回到 (1,1)
void clear_screen(void)
{
    console_puts("\x1b[2J\x1b[H");
}

// 控制台初始化：目前只需要初始化 UART
void console_init(void)
{
    uart_init();
}
