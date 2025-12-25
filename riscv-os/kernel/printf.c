#include <stdarg.h>

#include "types.h"
#include "console.h"
#include "printf.h"

static char digits[] = "0123456789abcdef";

// 把整数按指定进制输出
static void
printint(long long xx, int base, int sign)
{
    char buf[32];
    int i = 0;
    unsigned long long x;

    if (sign && xx < 0) {
        x = -xx;
        sign = 1;
    } else {
        x = xx;
        sign = 0;
    }

    // 反向取模生成数字
    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    if (sign)
        buf[i++] = '-';

    // 逆序输出
    while (--i >= 0)
        console_putc(buf[i]);
}

// 打印 64 位指针：0x + 16 个十六进制数字
static void
printptr(uint64 x)
{
    int i;
    console_putc('0');
    console_putc('x');
    for (i = 0; i < (int)(sizeof(uint64) * 2); i++, x <<= 4) {
        console_putc(digits[x >> (sizeof(uint64) * 8 - 4)]);
    }
}

// 内核 printf：支持 %d %u %x %p %s %c %%
int
printf(const char *fmt, ...)
{
    va_list ap;
    const char *p;
    int c;
    char *s;

    va_start(ap, fmt);
    for (p = fmt; (c = *p & 0xff) != 0; p++) {
        if (c != '%') {
            console_putc(c);
            continue;
        }

        // 解析格式符
        p++;
        c = *p & 0xff;
        if (c == 0)
            break;

        switch (c) {
        case 'd':   // 有符号十进制
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'u':   // 无符号十进制
            printint(va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':   // 无符号十六进制
            printint(va_arg(ap, unsigned int), 16, 0);
            break;
        case 'p':   // 指针
            printptr(va_arg(ap, uint64));
            break;
        case 's':   // 字符串
            s = va_arg(ap, char *);
            if (s == 0)
                s = "(null)";
            while (*s)
                console_putc(*s++);
            break;
        case 'c':   // 单个字符
            console_putc(va_arg(ap, int));
            break;
        case '%':   // 输出一个 '%'
            console_putc('%');
            break;
        default:
            // 未知格式：原样打印出 '%x'
            console_putc('%');
            console_putc(c);
            break;
        }
    }
    va_end(ap);

    return 0;
}


// 内核 panic：打印信息后死循环
void
panic(const char *s)
{
    printf("panic: %s\n", s);
    // 简化版：直接死循环
    while (1) {
        // 可以在这里加上 WFI 等待中断
    }
}

// 目前不需要做额外初始化，留空即可
void
printf_init(void)
{
}


