#include "types.h"
#include "printf.h"
#include "riscv.h"
#include "trap.h"

// S 模式全局时钟计数
volatile uint64 ticks = 0;

// kernelvec.S 中的符号
extern void kernelvec(void);

void
trapinit(void)
{
    printf("trapinit: simple trap system init\n");
}

// 为当前 hart 设置 stvec 指向 kernelvec
void
trapinithart(void)
{
    w_stvec((uint64)kernelvec);
}

// 时钟中断处理：增加 ticks，并预约下一次中断
static void
clockintr(void)
{
    ticks++;

    // 重新设置下一次时钟中断（约 0.1s）
    uint64 now = r_time();
    w_stimecmp(now + 1000000);

    // 每 10 次打印一次，避免刷屏过快
    if (ticks % 10 == 0) {
        printf("[exp4] clockintr: ticks=%d\n", (int)ticks);
    }
}

// 内核态 trap 统一入口
void
kerneltrap(void)
{
    uint64 scause  = r_scause();
    uint64 sepc    = r_sepc();
    uint64 sstatus = r_sstatus();

    // 只应该在 S 模式触发
    if ((sstatus & SSTATUS_SPP) == 0) {
        panic("kerneltrap: not from supervisor mode");
    }
    if (intr_get() != 0) {
        panic("kerneltrap: interrupts enabled");
    }

    // 仅处理 S 模式时钟中断：scause = 1<<63 | 5
    if (scause == (0x8000000000000000ULL | 5)) {
        clockintr();
    } else {
        printf("kerneltrap: unexpected scause=0x%d sepc=0x%d stval=0x%d\n",
               scause, sepc, r_stval());
        panic("kerneltrap");
    }

    // 恢复 trap 前的 sepc / sstatus，方便 sret 回去继续执行
    w_sepc(sepc);
    w_sstatus(sstatus);
}
