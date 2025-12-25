#ifndef _PROC_H_
#define _PROC_H_

#include "types.h"

// 和 swtch.S 对齐的上下文结构：顺序必须是 ra, sp, s0-s11
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved registers
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 内核线程状态（别和之前 trap.c 的 enum 混）
typedef enum {
  PROC_UNUSED = 0,
  PROC_RUNNABLE,
  PROC_RUNNING,
  PROC_ZOMBIE,
} procstate_t;

#define NPROC 4    // 实验就搞几个内核线程够用了

// 极简版“进程/内核线程”结构
struct proc {
  int pid;                 // 简单自增 pid
  procstate_t state;       // 当前状态
  uint64 kstack;           // 内核栈起始虚拟地址（1 页）
  struct context context;  // 用于 swtch 的上下文
  char name[16];           // 调试用名字
};

// 全局进程表 & 当前正在运行的线程
extern struct proc procs[NPROC];
extern struct proc *current_proc;

// 接口：初始化、创建线程、调度器、让出 CPU、退出
void proc_init(void);
struct proc *kproc_create(void (*fn)(void), const char *name);
void scheduler_run(void);
void yield(void);
void kproc_exit(void);

#endif
