#ifndef _PRINTF_H_
#define _PRINTF_H_

// 内核格式化输出，打印到串口控制台
int printf(const char *fmt, ...);

// 内核 panic：打印信息后死循环
void panic(const char *s);

// 预留的初始化函数，目前可以是空实现
void printf_init(void);

#endif
