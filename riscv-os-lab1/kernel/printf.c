#include "printf.h"
#include "uart.h"

/*
 * 内部帮助函数：输出单个字符。
 */
static void putc(char ch) {
    if (ch == '\n') {
        uart_putc('\r');
    }
    uart_putc(ch);
}

/*
 * 输出无符号整数，支持不同进制。
 */
static void print_uint(unsigned long value, unsigned base, int width, int pad_zero) {
    char buf[32];
    static const char digits[] = "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value != 0 && i < (int)sizeof(buf)) {
            buf[i++] = digits[value % base];
            value /= base;
        }
    }

    while (i < width) {
        buf[i++] = pad_zero ? '0' : ' ';
    }

    while (i > 0) {
        putc(buf[--i]);
    }
}

void kvprintf(const char *fmt, va_list ap) {
    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            putc(*fmt);
            continue;
        }

        ++fmt;
        if (*fmt == '\0') {
            break;
        }

        int width = 0;
        int pad_zero = 0;

        if (*fmt == '0') {
            pad_zero = 1;
            ++fmt;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }

        switch (*fmt) {
        case 'd': {
            long val = va_arg(ap, int);
            if (val < 0) {
                putc('-');
                print_uint((unsigned long)(-val), 10, width, pad_zero);
            } else {
                print_uint((unsigned long)val, 10, width, pad_zero);
            }
            break;
        }
        case 'x': {
            unsigned long val = va_arg(ap, unsigned int);
            print_uint(val, 16, width, pad_zero);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) {
                str = "(null)";
            }
            while (*str) {
                putc(*str++);
            }
            break;
        }
        case 'c': {
            char ch = (char)va_arg(ap, int);
            putc(ch);
            break;
        }
        case '%': {
            putc('%');
            break;
        }
        default: {
            /* 未知格式化字符，原样输出以便调试 */
            putc('%');
            putc(*fmt);
            break;
        }
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
