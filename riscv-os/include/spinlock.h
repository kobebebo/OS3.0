// include/spinlock.h
#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include "types.h"
#include "riscv.h"

// 简单自旋锁：在本实验的单核环境里，主要用于关闭中断 + 原子设置标志
struct spinlock {
    char *name;   // 锁的名字，方便调试
    int  locked;  // 0 表示未持有，1 表示已经被某个执行流持有
};

// 原子交换：返回旧值
static inline int
atomic_xchg(volatile int *addr, int newval)
{
    // GCC/Clang 提供的内建原子指令，适用于 RISC-V
    return __sync_lock_test_and_set(addr, newval);
}

// 初始化自旋锁
static inline void
initlock(struct spinlock *lk, char *name)
{
    lk->name   = name;
    lk->locked = 0;
}

// 当前是否持有该锁（这里只是看标志位，足够实验使用）
static inline int
holding(struct spinlock *lk)
{
    return lk->locked != 0;
}

// 加锁：关闭中断 + 自旋等待 locked 变为 0
static inline void
acquire(struct spinlock *lk)
{
    intr_off();               // 关闭中断，避免在持锁期间被打断

    // 自旋等待其它执行流释放该锁
    while (atomic_xchg(&lk->locked, 1) != 0) {
        // busy wait
    }
}

// 解锁：清除标志位 + 重新打开中断
static inline void
release(struct spinlock *lk)
{
    lk->locked = 0;
    intr_on();
}

#endif // _SPINLOCK_H_
