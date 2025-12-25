#ifndef _RISCV_H_
#define _RISCV_H_

#include "types.h"

// ------------ status / interrupt bits ------------

// sstatus bits
#define SSTATUS_SIE   (1L << 1)   // S-level global interrupt enable
#define SSTATUS_SPIE  (1L << 5)   // previous SIE
#define SSTATUS_SPP   (1L << 8)   // previous privilege (1 = S, 0 = U)

// mstatus bits
#define MSTATUS_MPP_MASK (3L << 11)
#define MSTATUS_MPP_S    (1L << 11)

// sie / mie bits
#define SIE_SEIE   (1L << 9)   // external interrupt enable
#define SIE_STIE   (1L << 5)   // timer interrupt enable

#define MIE_STIE   (1L << 5)   // machine timer interrupt enable

// ------------ S-mode CSRs ------------

static inline uint64
r_sstatus(void)
{
    uint64 x;
    asm volatile("csrr %0, sstatus" : "=r"(x));
    return x;
}

static inline void
w_sstatus(uint64 x)
{
    asm volatile("csrw sstatus, %0" : : "r"(x));
}

static inline uint64
r_sie(void)
{
    uint64 x;
    asm volatile("csrr %0, sie" : "=r"(x));
    return x;
}

static inline void
w_sie(uint64 x)
{
    asm volatile("csrw sie, %0" : : "r"(x));
}

static inline void
w_stvec(uint64 x)
{
    asm volatile("csrw stvec, %0" : : "r"(x));
}

static inline uint64
r_scause(void)
{
    uint64 x;
    asm volatile("csrr %0, scause" : "=r"(x));
    return x;
}

static inline uint64
r_sepc(void)
{
    uint64 x;
    asm volatile("csrr %0, sepc" : "=r"(x));
    return x;
}

static inline void
w_sepc(uint64 x)
{
    asm volatile("csrw sepc, %0" : : "r"(x));
}

static inline uint64
r_stval(void)
{
    uint64 x;
    asm volatile("csrr %0, stval" : "=r"(x));
    return x;
}

static inline uint64
r_satp(void)
{
    uint64 x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}

static inline void
w_satp(uint64 x)
{
    asm volatile("csrw satp, %0" : : "r"(x));
}

// ------------ M-mode CSRs ------------

static inline uint64
r_mstatus(void)
{
    uint64 x;
    asm volatile("csrr %0, mstatus" : "=r"(x));
    return x;
}

static inline void
w_mstatus(uint64 x)
{
    asm volatile("csrw mstatus, %0" : : "r"(x));
}

static inline void
w_mepc(uint64 x)
{
    asm volatile("csrw mepc, %0" : : "r"(x));
}

static inline uint64
r_medeleg(void)
{
    uint64 x;
    asm volatile("csrr %0, medeleg" : "=r"(x));
    return x;
}

static inline void
w_medeleg(uint64 x)
{
    asm volatile("csrw medeleg, %0" : : "r"(x));
}

static inline uint64
r_mideleg(void)
{
    uint64 x;
    asm volatile("csrr %0, mideleg" : "=r"(x));
    return x;
}

static inline void
w_mideleg(uint64 x)
{
    asm volatile("csrw mideleg, %0" : : "r"(x));
}

static inline uint64
r_mie(void)
{
    uint64 x;
    asm volatile("csrr %0, mie" : "=r"(x));
    return x;
}

static inline void
w_mie(uint64 x)
{
    asm volatile("csrw mie, %0" : : "r"(x));
}

static inline void
w_pmpaddr0(uint64 x)
{
    asm volatile("csrw pmpaddr0, %0" : : "r"(x));
}

static inline void
w_pmpcfg0(uint64 x)
{
    asm volatile("csrw pmpcfg0, %0" : : "r"(x));
}

static inline uint64
r_menvcfg(void)
{
    uint64 x;
    asm volatile("csrr %0, menvcfg" : "=r"(x));
    return x;
}

static inline void
w_menvcfg(uint64 x)
{
    asm volatile("csrw menvcfg, %0" : : "r"(x));
}

static inline uint64
r_mcounteren(void)
{
    uint64 x;
    asm volatile("csrr %0, mcounteren" : "=r"(x));
    return x;
}

static inline void
w_mcounteren(uint64 x)
{
    asm volatile("csrw mcounteren, %0" : : "r"(x));
}

static inline uint64
r_mhartid(void)
{
    uint64 x;
    asm volatile("csrr %0, mhartid" : "=r"(x));
    return x;
}

// tp: current hart id / cpu id 存在这
static inline uint64
r_tp(void)
{
    uint64 x;
    asm volatile("mv %0, tp" : "=r"(x));
    return x;
}

static inline void
w_tp(uint64 x)
{
    asm volatile("mv tp, %0" : : "r"(x));
}

// ------------ timer CSRs (Sstc) ------------

static inline uint64
r_time(void)
{
    uint64 x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

static inline void
w_stimecmp(uint64 x)
{
    asm volatile("csrw stimecmp, %0" : : "r"(x));
}

// ------------ sfence ------------

static inline void
sfence_vma(void)
{
    asm volatile("sfence.vma zero, zero");
}

// ------------ interrupt helpers ------------

static inline void
intr_on(void)
{
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void
intr_off(void)
{
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

static inline int
intr_get(void)
{
    return (r_sstatus() & SSTATUS_SIE) != 0;
}

#endif
