#include "types.h"
#include "riscv.h"

void main(void);
static void timerinit(void);

// entry.S 在 M 模式下跳到这里
void
start(void)
{
    // 1. M Previous Privilege 模式设置为 S，用于 mret
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);

    // 2. mret 之后跳到 main（S 模式）
    w_mepc((uint64)main);

    // 3. 先关闭分页
    w_satp(0);

    // 4. 将异常和中断委托给 S 模式
    w_medeleg(0xffff);
    w_mideleg(0xffff);

    // 打开 S 模式的外部中断和时钟中断（具体中断源后面再细分）
    w_sie(r_sie() | SIE_SEIE | SIE_STIE);

    // 5. 配置 PMP：允许 S 模式访问所有物理内存
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    // 6. 初始化时钟（机器模式 timer → S 模式中断）
    timerinit();

    // 7. 把 hartid 放到 tp 中
    int id = (int)r_mhartid();
    w_tp((uint64)id);

    // 8. 切到 S 模式并进入 main()
    asm volatile("mret");
}

// 机器模式下初始化时钟，之后中断由 S 模式处理
static void
timerinit(void)
{
    // 打开机器模式的 S timer interrupt
    w_mie(r_mie() | MIE_STIE);

    // 使能 Sstc 扩展（stimecmp）
    w_menvcfg(r_menvcfg() | (1L << 63));

    // 允许 S 模式使用 time/stimecmp
    w_mcounteren(r_mcounteren() | 2);

    // 请求第一次时钟中断（大约 0.1s 后）
    w_stimecmp(r_time() + 1000000);
}
