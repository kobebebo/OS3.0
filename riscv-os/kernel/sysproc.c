// kernel/sysproc.c
#include "types.h"
#include "printf.h"
#include "proc.h"
#include "syscall.h"

// 时钟 tick 计数，在 trap.c 中定义
extern volatile uint64 ticks;

// 返回当前“进程”的 pid
uint64
sys_getpid(void)
{
    if (current_proc == 0)
        return 0;
    return (uint64)current_proc->pid;
}

// 返回自启动以来的 tick 数
uint64
sys_uptime(void)
{
    return (uint64)ticks;
}

// 简单的“暂停”：忙等 n 个 tick
uint64
sys_pause(void)
{
    int n;
    argint(0, &n);
    if (n < 0)
        n = 0;

    uint64 start = ticks;
    while ((uint64)(ticks - start) < (uint64)n) {
        // 忙等，等待时钟中断增加 ticks
    }
    return 0;
}

// 测试整数参数传递：返回 a0 + a1
uint64
sys_test_add(void)
{
    int x, y;
    argint(0, &x);
    argint(1, &y);
    return (uint64)(x + y);
}

// 测试字符串参数传递：打印字符串并返回长度
uint64
sys_test_str(void)
{
    char buf[64];
    int len = argstr(0, buf, sizeof(buf));
    if (len < 0)
        return (uint64)-1;

    printf("[sys_test_str] got string: \"%s\"\n", buf);
    return (uint64)len;
}
