// include/syscall.h
#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include "types.h"

// --- 系统调用号（实验 6 + 实验 7） ---
// 1~5：实验六
#define SYS_getpid    1    // 返回当前“进程”的 pid（这里其实是内核线程）
#define SYS_uptime    2    // 返回时钟 tick 计数
#define SYS_pause     3    // 忙等 n 个 tick
#define SYS_test_add  4    // 测试整型参数：返回 a0 + a1
#define SYS_test_str  5    // 测试字符串参数：打印字符串并返回长度

// 6~11：实验七 文件系统相关
#define SYS_open      6    // 打开文件
#define SYS_read      7    // 读文件
#define SYS_write     8    // 写文件
#define SYS_close     9    // 关闭文件
#define SYS_fstat     10   // 查询文件状态
#define SYS_dup       11   // 复制文件描述符

// --- 简化版“系统调用帧” ---
// 模拟 RISC-V 中 a0~a7 寄存器的内容。
// 真正用到用户态时，可以直接用 trapframe 中的 a0..a7。
struct syscall_frame {
    uint64 a0;
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;   // 存放系统调用号（syscall number）
};

// syscall 分发入口
void syscall(struct syscall_frame *f);

// 参数获取接口
void argint(int n, int *ip);
void argaddr(int n, uint64 *ip);
int  argstr(int n, char *buf, int max);

#endif
