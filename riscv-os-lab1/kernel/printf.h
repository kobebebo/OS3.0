#ifndef PRINTF_H
#define PRINTF_H

#include <stdarg.h>

void kvprintf(const char *fmt, va_list ap);
void kprintf(const char *fmt, ...);

#endif /* PRINTF_H */
