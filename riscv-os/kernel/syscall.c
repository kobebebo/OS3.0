// kernel/syscall.c
#include "syscall.h"
#include "types.h"
#include "printf.h"
#include "proc.h"

// 当前正在处理的系统调用帧（模拟 trapframe 中的 a0~a7）
// 因为当前内核是单核 + 无并发，这里用一个全局指针就够了。
static struct syscall_frame *cur_frame = 0;

// 从 a0~a5 中取原始参数
static uint64
argraw(int n)
{
    if (cur_frame == 0) {
        panic("argraw: cur_frame is null");
    }

    switch (n) {
    case 0: return cur_frame->a0;
    case 1: return cur_frame->a1;
    case 2: return cur_frame->a2;
    case 3: return cur_frame->a3;
    case 4: return cur_frame->a4;
    case 5: return cur_frame->a5;
    default:
        panic("argraw: bad arg index");
        return 0;
    }
}

// 取第 n 个参数，并按 int 解释
void
argint(int n, int *ip)
{
    *ip = (int)argraw(n);
}

// 取第 n 个参数，并按指针（地址）解释
void
argaddr(int n, uint64 *ip)
{
    *ip = argraw(n);
}

// 取字符串参数：当前实验中还没有真正的用户地址空间，
// 直接把 addr 当成内核里的指针来用。
int
argstr(int n, char *buf, int max)
{
    uint64 addr;
    argaddr(n, &addr);
    const char *src = (const char *)addr;

    if (src == 0 || max <= 0) {
        return -1;
    }

    int i;
    for (i = 0; i < max - 1; i++) {
        char c = src[i];
        buf[i] = c;
        if (c == 0)
            break;
    }
    buf[i] = 0;
    return i;
}

// ---- 实验 6：从 sysproc.c 提供的具体 sys_* 实现 ----
extern uint64 sys_getpid(void);
extern uint64 sys_uptime(void);
extern uint64 sys_pause(void);
extern uint64 sys_test_add(void);
extern uint64 sys_test_str(void);

// ---- 实验 7：从 sysfile.c 提供的文件相关 sys_* 实现 ----
extern uint64 sys_open(void);
extern uint64 sys_read(void);
extern uint64 sys_write(void);
extern uint64 sys_close(void);
extern uint64 sys_fstat(void);
extern uint64 sys_dup(void);

// syscalls[] 表：根据系统调用号索引到函数指针
typedef uint64 (*syscall_func_t)(void);

static syscall_func_t syscalls[] = {
    [SYS_getpid]   = sys_getpid,
    [SYS_uptime]   = sys_uptime,
    [SYS_pause]    = sys_pause,
    [SYS_test_add] = sys_test_add,
    [SYS_test_str] = sys_test_str,

    [SYS_open]     = sys_open,
    [SYS_read]     = sys_read,
    [SYS_write]    = sys_write,
    [SYS_close]    = sys_close,
    [SYS_fstat]    = sys_fstat,
    [SYS_dup]      = sys_dup,
};

// syscall 分发入口：
// 真实系统中会在 trap 里被调用，这里由 test.c 手工调用。
void
syscall(struct syscall_frame *f)
{
    cur_frame = f;

    int num = (int)f->a7;
    uint64 ret = (uint64)-1;

    if (num > 0 &&
        num < (int)(sizeof(syscalls) / sizeof(syscalls[0])) &&
        syscalls[num] != 0) {
        ret = syscalls[num]();
    } else {
        printf("pid %d: unknown syscall %d\n",
               current_proc ? current_proc->pid : -1, num);
        ret = (uint64)-1;
    }

    // 按 RISC-V 约定，通过 a0 返回结果
    f->a0 = ret;

    cur_frame = 0;
}
