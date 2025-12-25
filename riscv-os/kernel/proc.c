#include "types.h"
#include "memlayout.h"
#include "printf.h"
#include "pmm.h"
#include "proc.h"

struct proc procs[NPROC];
struct proc *current_proc = 0;

// 调度器自己用的上下文（在“main 那个栈”上）
static struct context sched_context;
static int next_pid = 1;

// swtch.S
extern void swtch(struct context *old, struct context *new);

// 简单字符串拷贝
static void
kstrncpy(char *dst, const char *src, int n)
{
  int i = 0;
  for (; i < n - 1 && src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = 0;
}

// 初始化进程表
void
proc_init(void)
{
  for (int i = 0; i < NPROC; i++) {
    procs[i].pid    = 0;
    procs[i].state  = PROC_UNUSED;
    procs[i].kstack = 0;
    procs[i].name[0] = 0;
  }
  current_proc = 0;
  next_pid = 1;
  printf("proc_init: NPROC=%d\n", NPROC);
}

// 内部：分配一个 UNUSED 的 proc，设置好栈和入口函数
static struct proc *
alloc_proc(void (*fn)(void), const char *name)
{
  struct proc *p = 0;

  for (int i = 0; i < NPROC; i++) {
    if (procs[i].state == PROC_UNUSED) {
      p = &procs[i];
      break;
    }
  }

  if (p == 0) {
    return 0;
  }

  p->pid   = next_pid++;
  p->state = PROC_RUNNABLE;

  // 分配一页作为内核栈（pmm_init 已在实验三里做过）
  void *stack = alloc_page();
  if (stack == 0) {
    panic("alloc_proc: alloc_page for kstack failed");
  }
  p->kstack = (uint64)stack;

  // 设置初始上下文：返回地址 = 线程函数；栈顶 = kstack + PGSIZE
  p->context.ra = (uint64)fn;
  p->context.sp = p->kstack + PGSIZE;

  kstrncpy(p->name, name ? name : "kthread", sizeof(p->name));

  return p;
}

// 对外接口：创建内核线程
struct proc *
kproc_create(void (*fn)(void), const char *name)
{
  struct proc *p = alloc_proc(fn, name);
  if (p == 0) {
    printf("kproc_create: no free proc slot\n");
    return 0;
  }

  printf("kproc_create: pid=%d name=%s kstack=%p\n",
         p->pid, p->name, (uint64)p->kstack);
  return p;
}

// 简单调度器：轮询所有 RUNNABLE 线程，直到都变成 ZOMBIE
void
scheduler_run(void)
{
  printf("[scheduler] start\n");

  for (;;) {
    int runnable = 0;

    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &procs[i];

      if (p->state == PROC_RUNNABLE) {
        runnable = 1;
        current_proc = p;
        p->state = PROC_RUNNING;

        printf("[scheduler] switch to pid=%d (%s)\n", p->pid, p->name);

        // 切到线程上下文；等线程 yield 或 exit 再切回 sched_context
        swtch(&sched_context, &p->context);

        // 回到这里说明线程主动让出了 CPU（yield 或 exit）
        current_proc = 0;
      }
    }

    if (!runnable) {
      break;  // 没有可运行线程了，退回测试代码
    }
  }

  printf("[scheduler] no runnable procs, return\n");
}

// 线程主动让出 CPU（协作式调度）
void
yield(void)
{
  if (current_proc == 0) {
    return; // 还没进入 scheduler，就忽略
  }

  struct proc *p = current_proc;
  p->state = PROC_RUNNABLE;

  printf("[yield] pid=%d (%s)\n", p->pid, p->name);

  swtch(&p->context, &sched_context);
}

// 线程退出：标记 ZOMBIE，释放栈，切回调度器
void
kproc_exit(void)
{
  if (current_proc == 0) {
    panic("kproc_exit: no current_proc");
  }

  struct proc *p = current_proc;
  printf("[kproc_exit] pid=%d (%s)\n", p->pid, p->name);

  p->state = PROC_ZOMBIE;

  if (p->kstack) {
    free_page((void *)p->kstack);
    p->kstack = 0;
  }

  swtch(&p->context, &sched_context);

  // 不该再回来
  panic("kproc_exit: returned");
}
